#include "Replay/AdaptiveReplayRunner.h"
#include "Pipeline/AdaptiveBehaviorStage.h"
#include "Pipeline/AdaptiveContextStage.h"
#include "Pipeline/AdaptivePipelineConfig.h"
#include "Pipeline/AdaptivePipelineStages.h"
#include "Pipeline/AdaptiveTelemetryStage.h"

FAdaptiveActionCheck UAdaptiveReplayActionHandler::CheckAction_Implementation(const UAdaptiveCompanionAction* Action)
{
	const FAdaptiveActionCheck* Answer = Action ? Answers.Find(Action->ActionName) : nullptr;
	return Answer ? *Answer : FAdaptiveActionCheck();
}

bool UAdaptiveReplayActionHandler::PerformAction_Implementation(const UAdaptiveCompanionAction* Action)
{
	return bAccept;
}

bool FAdaptiveReplayStep::operator==(const FAdaptiveReplayStep& Other) const
{
	if (Index != Other.Index || Time != Other.Time || Intervention != Other.Intervention || InterventionId != Other.InterventionId
		|| SelectionReason != Other.SelectionReason || Running != Other.Running || Contexts != Other.Contexts || Needs.Num() != Other.Needs.Num())
	{
		return false;
	}
	for (const TPair<FName, FAdaptiveNeedState>& Need : Needs)
	{
		const FAdaptiveNeedState* OtherNeed = Other.Needs.Find(Need.Key);
		if (!OtherNeed || OtherNeed->Score != Need.Value.Score || OtherNeed->RawScore != Need.Value.RawScore
			|| OtherNeed->Level != Need.Value.Level || OtherNeed->bActive != Need.Value.bActive || OtherNeed->Reason != Need.Value.Reason)
		{
			return false;
		}
	}
	return true;
}

namespace
{
	template <typename TStage>
	TStage* MakeStage(UObject* Outer, const TStage* Template, TArray<TObjectPtr<UAdaptivePipelineStage>>& Stages)
	{
		// same as the pipeline: the template supplies the class and its parameters
		TStage* Stage = Template
			? NewObject<TStage>(Outer, Template->GetClass(), NAME_None, RF_Transient, const_cast<TStage*>(Template))
			: NewObject<TStage>(Outer);
		Stages.Add(Stage);
		return Stage;
	}
}

void UAdaptiveReplayRunner::Setup(const UAdaptivePipelineConfig* Config)
{
	if (!Config)
	{
		Config = GetDefault<UAdaptivePipelineConfig>();
	}

	Stages.Reset();
	Warnings.Reset();
	Handler = NewObject<UAdaptiveReplayActionHandler>(this);

	// the pipeline's update order: 1, 2, 4, 3, 5, 6
	Telemetry = MakeStage(this, Config->Telemetry.Get(), Stages);
	MakeStage(this, Config->Features.Get(), Stages);
	Context = MakeStage(this, Config->Context.Get(), Stages);
	MakeStage(this, Config->Need.Get(), Stages);
	MakeStage(this, Config->Selection.Get(), Stages);
	Behavior = MakeStage(this, Config->Behavior.Get(), Stages);
}

void UAdaptiveReplayRunner::Apply(const FAdaptiveRecordedUpdate& Update)
{
	for (const FAdaptiveRecordedPhase& Phase : Update.PhasesBefore)
	{
		Behavior->ReportPhase(Phase.Intervention, Phase.Phase, Phase.Detail);
	}

	for (const FAdaptiveTelemetryEvent& Event : Update.Events)
	{
		Telemetry->ReportEventAt(Event.Name, Event.Value, Event.Time);
	}
	for (const TPair<FName, double>& Value : Update.Values)
	{
		Telemetry->ReportValue(Value.Key, Value.Value);
	}

	if (Update.Contexts.IsSet())
	{
		for (const FName Active : TArray<FName>(Context->GetActiveContexts()))
		{
			if (!Update.Contexts->Contains(Active))
			{
				Context->SetContext(Active, false);
			}
		}
		for (const FName Active : *Update.Contexts)
		{
			Context->SetContext(Active, true);
		}
	}

	if (Update.bObjectiveChanged)
	{
		if (Update.Objective.IsNone())
		{
			Context->ClearObjective();
		}
		else
		{
			Context->SetObjective(Update.Objective, Update.ObjectiveLocation);
		}
	}

	if (Update.Registered.IsSet())
	{
		// the current assets, so changed actions can be tried on a recording
		TArray<UAdaptiveCompanionAction*> Actions;
		for (const FAdaptiveRecordedAction& Recorded : *Update.Registered)
		{
			TObjectPtr<UAdaptiveCompanionAction>& Action = LoadedActions.FindOrAdd(Recorded.Asset);
			if (!Action)
			{
				Action = LoadObject<UAdaptiveCompanionAction>(nullptr, *Recorded.Asset);
			}
			if (!Action)
			{
				Warnings.AddUnique(FString::Printf(TEXT("Action asset %s of %s couldn't be loaded; replayed without it."), *Recorded.Asset, *Recorded.Name.ToString()));
				continue;
			}
			Actions.Add(Action);
		}
		Context->RegisterActions(Handler, Actions);
	}

	for (const FAdaptiveRecordedAction& Answer : Update.Answers)
	{
		FAdaptiveActionCheck& Check = Handler->Answers.FindOrAdd(Answer.Name);
		Check.bPossible = Answer.bPossible;
		Check.Reason = Answer.Reason;
		Check.Cost = float(Answer.Cost);
	}
	Handler->bAccept = !Update.bRejected;
}

TArray<FAdaptiveReplayStep> UAdaptiveReplayRunner::Run(const TArray<FAdaptiveRecordedUpdate>& Updates)
{
	TArray<FAdaptiveReplayStep> Steps;
	if (Stages.IsEmpty())
	{
		Warnings.Add(TEXT("Run without Setup."));
		return Steps;
	}

	double PreviousTime = 0.0;
	for (int32 Index = 0; Index < Updates.Num(); ++Index)
	{
		const FAdaptiveRecordedUpdate& Update = Updates[Index];
		Apply(Update);

		FAdaptivePipelineFrame Frame;
		Frame.UpdateIndex = Update.Index;
		Frame.Time = Update.Time;
		Frame.DeltaTime = Index > 0 ? Update.Time - PreviousTime : 0.0;
		Frame.RunningIntervention = Behavior->UpdateRunning(Update.Time);

		for (UAdaptivePipelineStage* Stage : Stages)
		{
			Stage->Update(Frame);
		}

		for (const FAdaptiveRecordedPhase& Phase : Update.PhasesDuring)
		{
			Behavior->ReportPhase(Phase.Intervention, Phase.Phase, Phase.Detail);
		}

		FAdaptiveReplayStep& Step = Steps.AddDefaulted_GetRef();
		Step.Index = Frame.UpdateIndex;
		Step.Time = Frame.Time;
		Step.Needs = Frame.Needs;
		Step.Contexts = Frame.Contexts.Array();
		Step.Intervention = Frame.Intervention;
		Step.InterventionId = Frame.InterventionId;
		Step.SelectionReason = Frame.SelectionReason;
		Step.Running = Frame.RunningIntervention;
		PreviousTime = Update.Time;
	}
	return Steps;
}
