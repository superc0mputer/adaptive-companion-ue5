#include "Pipeline/AdaptiveBehaviorStage.h"
#include "Pipeline/AdaptiveCompanionAction.h"
#include "Pipeline/AdaptivePipelineSubsystem.h"
#include "AdaptiveCompanionFramework.h"
#include "Debug/AdaptiveDebugSubsystem.h"
#include "Logging/AdaptiveLogRecordTypes.h"
#include "Logging/AdaptiveLogSubsystem.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
	double Round3(double Value)
	{
		return FMath::RoundToDouble(Value * 1000.0) / 1000.0;
	}

	FString FormatValue(const TOptional<double>& Value)
	{
		return Value.IsSet() ? FString::Printf(TEXT("%.2f"), *Value) : FString(TEXT("–"));
	}
}

bool AdaptiveInterventionPhase::IsFinal(FName Phase)
{
	static const TSet<FName> FinalPhases = { Completed, Failed, Interrupted, Rejected, TimedOut, TEXT("ended") };
	return FinalPhases.Contains(Phase);
}

void UAdaptiveBehaviorStage::Initialize(UAdaptivePipelineSubsystem& InPipeline)
{
	Super::Initialize(InPipeline);
	if (UAdaptiveLogSubsystem* Log = GetLog())
	{
		Log->OnSessionEnding.AddUObject(this, &UAdaptiveBehaviorStage::FlushOutcomes);
	}
	if (UAdaptiveDebugSubsystem* Debug = GetDebug())
	{
		Debug->SetText(EAdaptiveDebugSection::Behavior, TEXT("Outcome"), TEXT("none yet"));
	}
}

void UAdaptiveBehaviorStage::Deinitialize()
{
	// normally done already, when the log announced the session's end
	FlushOutcomes();
	Super::Deinitialize();
}

void UAdaptiveBehaviorStage::FlushOutcomes()
{
	// outcomes still open are logged with what was measured, so none is lost
	for (const FAdaptiveOutcome& Outcome : Outcomes.Flush(TEXT("session ended")))
	{
		LogOutcome(Outcome);
	}
}

double UAdaptiveBehaviorStage::GetNow() const
{
	const UAdaptivePipelineSubsystem* Pipeline = GetPipeline();
	return Pipeline ? Pipeline->GetTime() : LastUpdateTime;
}

void UAdaptiveBehaviorStage::Update(FAdaptivePipelineFrame& Frame)
{
	LastUpdateTime = Frame.Time;
	Outcomes.AfterWindow = OutcomeWindow;

	// before the new intervention starts, so a selection in this update cuts the previous window short
	for (const FAdaptiveOutcome& Outcome : Outcomes.Update(Frame))
	{
		LogOutcome(Outcome);
	}

	if (Frame.Intervention.IsNone())
	{
		ShowOutcomes(Frame.Time);
		return;
	}

	Running = Frame.Intervention;
	RunningSince = Frame.Time;
	DispatchTimes.Add(Frame.Intervention, Frame.Time);

	const FAdaptiveActionState* Action = Frame.Actions.FindByPredicate([&Frame](const FAdaptiveActionState& State) { return State.Name == Frame.Intervention; });
	UObject* Handler = Action ? Action->Handler : nullptr;

	// the outcome starts before the handler is called, so an end it reports synchronously is caught
	Ids.Add(Frame.Intervention, Frame.InterventionId);
	const FAdaptiveActionEvaluation* Evaluation = Frame.Evaluations.FindByPredicate([&Frame](const FAdaptiveActionEvaluation& Candidate) { return Candidate.Action == Frame.Intervention; });
	Outcomes.Start(Frame.InterventionId, Frame.Intervention, Evaluation ? Evaluation->Need : NAME_None,
		Action && Action->Action ? Action->Action->OutcomeFeatures : TArray<FName>(), Frame.Time);

	// logged before the handler is called, so a phase it reports synchronously comes after it
	LogPhase(Frame.Intervention, AdaptiveInterventionPhase::Dispatched);

	const bool bAccepted = Handler && IsValid(Handler) && Handler->GetClass()->ImplementsInterface(UAdaptiveCompanionActionHandler::StaticClass())
		&& IAdaptiveCompanionActionHandler::Execute_PerformAction(Handler, Action->Action);
	if (!bAccepted)
	{
		ReportPhase(Frame.Intervention, AdaptiveInterventionPhase::Rejected, Handler ? TEXT("the handler refused") : TEXT("no handler"));
	}

	// a notification for anyone else interested; carrying it out is the handler's job
	if (UAdaptivePipelineSubsystem* Pipeline = GetPipeline())
	{
		Pipeline->OnIntervention.Broadcast(Frame.Intervention);
	}

	ShowOutcomes(Frame.Time);
}

void UAdaptiveBehaviorStage::ReportPhase(FName Intervention, FName Phase, const FString& Detail)
{
	LogPhase(Intervention, Phase, Detail);

	if (AdaptiveInterventionPhase::IsFinal(Phase))
	{
		Outcomes.End(Intervention, Phase, Detail, GetNow());
		if (Intervention == Running)
		{
			Running = NAME_None;
		}
	}
}

FName UAdaptiveBehaviorStage::UpdateRunning(double Now)
{
	if (!Running.IsNone() && Now - RunningSince >= MaxInterventionDuration)
	{
		ReportPhase(Running, AdaptiveInterventionPhase::TimedOut, TEXT("the game didn't report an end"));
	}
	return Running;
}

void UAdaptiveBehaviorStage::LogPhase(FName Intervention, FName Phase, const FString& Detail, int32 Receivers) const
{
	const double Now = GetNow();
	const double* Dispatched = DispatchTimes.Find(Intervention);
	const bool bFinal = AdaptiveInterventionPhase::IsFinal(Phase);

	if (UAdaptiveLogSubsystem* Log = GetLog())
	{
		TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
		if (const int32* Id = Ids.Find(Intervention))
		{
			Payload->SetNumberField(TEXT("id"), *Id);
		}
		Payload->SetStringField(TEXT("name"), Intervention.ToString());
		Payload->SetStringField(TEXT("phase"), Phase.ToString());
		if (!Detail.IsEmpty())
		{
			Payload->SetStringField(TEXT("detail"), Detail);
		}
		if (bFinal && Dispatched)
		{
			Payload->SetNumberField(TEXT("duration"), FMath::RoundToDouble((Now - *Dispatched) * 100.0) / 100.0);
		}
		if (Receivers != INDEX_NONE)
		{
			Payload->SetNumberField(TEXT("receivers"), Receivers);
		}
		Log->LogRecord(AdaptiveLogRecordType::Intervention, Payload);
	}

	if (UAdaptiveDebugSubsystem* Debug = GetDebug())
	{
		FString Text = FString::Printf(TEXT("%s at %.1f s"), *Phase.ToString(), Now);
		if (bFinal && Dispatched)
		{
			Text += FString::Printf(TEXT(", after %.1f s"), Now - *Dispatched);
		}
		if (!Detail.IsEmpty())
		{
			Text += TEXT(": ") + Detail;
		}
		Debug->SetText(EAdaptiveDebugSection::Behavior, Intervention.ToString(), Text);
	}
}

void UAdaptiveBehaviorStage::LogOutcome(const FAdaptiveOutcome& Outcome) const
{
	if (UAdaptiveLogSubsystem* Log = GetLog())
	{
		TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetNumberField(TEXT("id"), Outcome.Id);
		Payload->SetStringField(TEXT("name"), Outcome.Action.ToString());
		Payload->SetStringField(TEXT("need"), Outcome.Need.ToString());
		Payload->SetStringField(TEXT("phase"), Outcome.Phase.ToString());
		if (!Outcome.Detail.IsEmpty())
		{
			Payload->SetStringField(TEXT("detail"), Outcome.Detail);
		}
		if (Outcome.HasEnded())
		{
			Payload->SetNumberField(TEXT("duration"), Round3(Outcome.EndTime - Outcome.SelectedTime));
		}
		Payload->SetNumberField(TEXT("after_duration"), Round3(Outcome.AfterDuration));
		Payload->SetStringField(TEXT("cut_short"), Outcome.CutShort);

		TSharedRef<FJsonObject> Level = MakeShared<FJsonObject>();
		Level->SetStringField(TEXT("baseline"), AdaptiveNeedMath::LevelName(Outcome.BaselineLevel));
		Level->SetStringField(TEXT("end"), AdaptiveNeedMath::LevelName(Outcome.EndLevel));
		Level->SetStringField(TEXT("after"), AdaptiveNeedMath::LevelName(Outcome.AfterLevel));
		Payload->SetObjectField(TEXT("level"), Level);
		Payload->SetBoolField(TEXT("relieved"), Outcome.IsRelieved());

		// keyed by name, so pandas' json_normalize gives columns like measures.Threat.change
		TSharedRef<FJsonObject> Measures = MakeShared<FJsonObject>();
		for (const FAdaptiveOutcomeMeasure& Measure : Outcome.Measures)
		{
			TSharedRef<FJsonObject> Values = MakeShared<FJsonObject>();
			auto SetValue = [&Values](const TCHAR* Field, const TOptional<double>& Value)
			{
				if (Value.IsSet())
				{
					Values->SetNumberField(Field, Round3(*Value));
				}
				else
				{
					Values->SetField(Field, MakeShared<FJsonValueNull>());
				}
			};
			Values->SetStringField(TEXT("kind"), Measure.bNeed ? TEXT("need") : TEXT("feature"));
			SetValue(TEXT("baseline"), Measure.Baseline);
			SetValue(TEXT("end"), Measure.End);
			SetValue(TEXT("after"), Measure.After);
			SetValue(TEXT("change"), Measure.GetChange());
			SetValue(TEXT("end_change"), Measure.GetEndChange());
			Measures->SetObjectField(Measure.Name.ToString(), Values);
		}
		Payload->SetObjectField(TEXT("measures"), Measures);
		Log->LogRecord(AdaptiveLogRecordType::Outcome, Payload);
	}

	if (UAdaptiveDebugSubsystem* Debug = GetDebug())
	{
		// "#3 Action.ThreatCallout: Threat 0.72 → 0.18, relieved · Threat.EnemiesUnseen 1.00 → 0.00"
		TArray<FString> Parts;
		for (const FAdaptiveOutcomeMeasure& Measure : Outcome.Measures)
		{
			FString Part = FString::Printf(TEXT("%s %s → %s"), *Measure.Name.ToString(), *FormatValue(Measure.Baseline), *FormatValue(Measure.After.IsSet() ? Measure.After : Measure.End));
			if (Measure.bNeed)
			{
				Part += Outcome.IsRelieved() ? TEXT(", relieved") : TEXT(", not relieved");
			}
			Parts.Add(Part);
		}
		FString Text = FString::Printf(TEXT("#%d %s: %s"), Outcome.Id, *Outcome.Action.ToString(), *FString::Join(Parts, TEXT(" · ")));
		if (!Outcome.CutShort.IsEmpty())
		{
			Text += FString::Printf(TEXT(" (cut short: %s)"), *Outcome.CutShort);
		}
		Debug->SetText(EAdaptiveDebugSection::Behavior, TEXT("Outcome"), Text);
	}

	if (OnOutcome)
	{
		OnOutcome(Outcome);
	}
}

void UAdaptiveBehaviorStage::ShowOutcomes(double Now) const
{
	UAdaptiveDebugSubsystem* Debug = GetDebug();
	if (!Debug)
	{
		return;
	}

	TArray<FString> Parts;
	for (const FAdaptiveOutcome& Outcome : Outcomes.GetOpen())
	{
		Parts.Add(Outcome.HasEnded()
			? FString::Printf(TEXT("#%d %.0f s left"), Outcome.Id, FMath::Max(0.0, OutcomeWindow - (Now - Outcome.EndTime)))
			: FString::Printf(TEXT("#%d running"), Outcome.Id));
	}
	if (Parts.IsEmpty())
	{
		Debug->RemoveValue(EAdaptiveDebugSection::Behavior, TEXT("Measuring"));
	}
	else
	{
		Debug->SetText(EAdaptiveDebugSection::Behavior, TEXT("Measuring"), FString::Join(Parts, TEXT(", ")));
	}
}
