#include "Replay/AdaptiveReplayRunner.h"
#include "Pipeline/AdaptiveBehaviorStage.h"
#include "Pipeline/AdaptiveContextStage.h"
#include "Pipeline/AdaptiveFeatureStage.h"
#include "Pipeline/AdaptiveNeedEstimator.h"
#include "Pipeline/AdaptiveNeedStage.h"
#include "HAL/PlatformTime.h"
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

void UAdaptiveReplayRunner::Setup(const UAdaptivePipelineConfig* Config, int32 InCopies)
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

	Copies = FMath::Max(1, InCopies);
	CopyNames.Reset();
	if (Copies > 1)
	{
		AddCopies(Copies);
	}
}

namespace
{
	FName CopyName(FName Name, int32 Copy)
	{
		return Name.IsNone() ? Name : FName(*FString::Printf(TEXT("%s#%d"), *Name.ToString(), Copy));
	}
}

void UAdaptiveReplayRunner::AddCopies(int32 InCopies)
{
	for (UAdaptivePipelineStage* Stage : Stages)
	{
		if (UAdaptiveFeatureStage* Features = Cast<UAdaptiveFeatureStage>(Stage))
		{
			const TArray<FAdaptiveFeatureDefinition> Originals = Features->Features;
			for (int32 Copy = 2; Copy <= InCopies; ++Copy)
			{
				for (FAdaptiveFeatureDefinition Definition : Originals)
				{
					// every name a definition reads is a signal or a feature, and both are copied
					Definition.Name = CopyName(Definition.Name, Copy);
					Definition.Signal = CopyName(Definition.Signal, Copy);
					Definition.Denominator = CopyName(Definition.Denominator, Copy);
					Definition.ResetEvent = CopyName(Definition.ResetEvent, Copy);
					Definition.OnlyWhileNonZero = CopyName(Definition.OnlyWhileNonZero, Copy);
					Features->Features.Add(Definition);
				}
			}
		}
		else if (UAdaptiveNeedStage* Need = Cast<UAdaptiveNeedStage>(Stage))
		{
			// the estimator is the config asset's subobject unless the stage got its own copy; never change the asset's
			if (Need->Estimator && Need->Estimator->GetOuter() != Need)
			{
				Need->Estimator = DuplicateObject(Need->Estimator.Get(), Need);
			}
			UAdaptiveRuleNeedEstimator* Rules = Cast<UAdaptiveRuleNeedEstimator>(Need->Estimator);
			if (!Rules)
			{
				Warnings.Add(TEXT("Copies need the rule-based estimator; only the features were copied."));
				continue;
			}

			const TArray<FAdaptiveNeedDimension> Originals = Rules->Dimensions;
			const TMap<FName, FAdaptiveNeedLevelSettings> Levels = Need->DimensionLevels;
			for (int32 Copy = 2; Copy <= InCopies; ++Copy)
			{
				for (FAdaptiveNeedDimension Dimension : Originals)
				{
					if (const FAdaptiveNeedLevelSettings* Override = Levels.Find(Dimension.Name))
					{
						Need->DimensionLevels.Add(CopyName(Dimension.Name, Copy), *Override);
					}
					Dimension.Name = CopyName(Dimension.Name, Copy);
					for (FAdaptiveNeedRule& Rule : Dimension.Rules)
					{
						for (FAdaptiveNeedCondition& Condition : Rule.Features)
						{
							Condition.Feature = CopyName(Condition.Feature, Copy);
						}
					}
					Rules->Dimensions.Add(Dimension);
				}
			}
		}
	}
}

const TArray<FName>& UAdaptiveReplayRunner::GetCopyNames(FName Name)
{
	TArray<FName>& Names = CopyNames.FindOrAdd(Name);
	if (Names.IsEmpty())
	{
		for (int32 Copy = 2; Copy <= Copies; ++Copy)
		{
			Names.Add(CopyName(Name, Copy));
		}
	}
	return Names;
}

TArray<FString> UAdaptiveReplayRunner::GetStageNames() const
{
	TArray<FString> Names;
	for (const UAdaptivePipelineStage* Stage : Stages)
	{
		FString Name = Stage->GetClass()->GetName();
		Name.RemoveFromStart(TEXT("Adaptive"));
		Name.RemoveFromEnd(TEXT("Stage"));
		Names.Add(Name);
	}
	return Names;
}

int32 UAdaptiveReplayRunner::GetFeatureCount() const
{
	for (const UAdaptivePipelineStage* Stage : Stages)
	{
		if (const UAdaptiveFeatureStage* Features = Cast<UAdaptiveFeatureStage>(Stage))
		{
			return Features->Features.Num();
		}
	}
	return 0;
}

int32 UAdaptiveReplayRunner::GetDimensionCount() const
{
	for (const UAdaptivePipelineStage* Stage : Stages)
	{
		const UAdaptiveNeedStage* Need = Cast<UAdaptiveNeedStage>(Stage);
		if (const UAdaptiveRuleNeedEstimator* Rules = Need ? Cast<UAdaptiveRuleNeedEstimator>(Need->Estimator) : nullptr)
		{
			return Rules->Dimensions.Num();
		}
	}
	return 0;
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
		if (Copies > 1)
		{
			for (const FName Copy : GetCopyNames(Event.Name))
			{
				Telemetry->ReportEventAt(Copy, Event.Value, Event.Time);
			}
		}
	}
	for (const TPair<FName, double>& Value : Update.Values)
	{
		Telemetry->ReportValue(Value.Key, Value.Value);
		if (Copies > 1)
		{
			for (const FName Copy : GetCopyNames(Value.Key))
			{
				Telemetry->ReportValue(Copy, Value.Value);
			}
		}
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
	Costs.Reset();
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

		if (bMeasure)
		{
			FAdaptiveUpdateCost& Cost = Costs.AddDefaulted_GetRef();
			const uint64 UpdateStart = FPlatformTime::Cycles64();
			for (UAdaptivePipelineStage* Stage : Stages)
			{
				const uint64 StageStart = FPlatformTime::Cycles64();
				Stage->Update(Frame);
				Cost.Parts.Add(FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - StageStart));
			}
			Cost.Total = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - UpdateStart);
			Cost.Events = Frame.Events.Num();
			Cost.Values = Frame.Values.Num();
			Cost.Features = Frame.Features.Num();
			Cost.Needs = Frame.Needs.Num();
			Cost.Actions = Frame.Actions.Num();
		}
		else
		{
			for (UAdaptivePipelineStage* Stage : Stages)
			{
				Stage->Update(Frame);
			}
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
