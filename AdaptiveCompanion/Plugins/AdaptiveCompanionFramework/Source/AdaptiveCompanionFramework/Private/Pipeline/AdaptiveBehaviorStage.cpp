#include "Pipeline/AdaptiveBehaviorStage.h"
#include "Pipeline/AdaptiveCompanionAction.h"
#include "Pipeline/AdaptivePipelineSubsystem.h"
#include "AdaptiveCompanionFramework.h"
#include "Debug/AdaptiveDebugSubsystem.h"
#include "Logging/AdaptiveLogRecordTypes.h"
#include "Logging/AdaptiveLogSubsystem.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "CoreGlobals.h"
#include "Engine/GameInstance.h"
#include "HAL/PlatformTime.h"

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

	/** Session time with 0.1 ms precision, null if missing */
	TSharedRef<FJsonValue> TimeValue(double Seconds)
	{
		return Seconds >= 0.0 ? StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueNumber>(FMath::RoundToDouble(Seconds * 10000.0) / 10000.0))
			: StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueNull>());
	}

	/** A span in ms with 0.01 ms precision, null if missing */
	TSharedRef<FJsonValue> MsValue(double Seconds)
	{
		return Seconds >= 0.0 ? StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueNumber>(FMath::RoundToDouble(Seconds * 100000.0) / 100.0))
			: StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueNull>());
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
		Debug->SetText(EAdaptiveDebugSection::Measurements, TEXT("Latency"), TEXT("none yet"));
	}
	PresentProbe = FAdaptivePresentProbe::Create(InPipeline.GetGameInstance());
}

void UAdaptiveBehaviorStage::Deinitialize()
{
	// normally done already, when the log announced the session's end
	FlushOutcomes();
	if (PresentProbe)
	{
		PresentProbe->Shutdown();
		PresentProbe.Reset();
	}
	Super::Deinitialize();
}

void UAdaptiveBehaviorStage::FlushOutcomes()
{
	// outcomes still open are logged with what was measured, so none is lost
	for (const FAdaptiveOutcome& Outcome : Outcomes.Flush(TEXT("session ended")))
	{
		LogOutcome(Outcome);
	}
	PollPresents();
	LogLatencies(Latencies.Flush(TEXT("session ended")));
}

void UAdaptiveBehaviorStage::PollPresents()
{
	if (!PresentProbe)
	{
		return;
	}

	// the probe's times are FPlatformTime::Seconds(); the session clock is the same clock with another origin
	const double Offset = GetNow() - FPlatformTime::Seconds();
	FAdaptivePresentProbe::FPresent Present;
	while (PresentProbe->Poll(Present))
	{
		Latencies.OnPresented(Present.Id, Present.Seconds + Offset, Present.Frame);
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
	bLastDispatchRejected = false;
	Outcomes.AfterWindow = OutcomeWindow;

	PollPresents();
	Latencies.Update(GetNow());
	LogLatencies(Latencies.TakeFinished());

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

	FAdaptiveLatency Latency;
	Latency.Id = Frame.InterventionId;
	Latency.Action = Frame.Intervention;
	Latency.Need = Evaluation ? Evaluation->Need : NAME_None;
	if (const FAdaptiveNeedState* NeedState = Frame.Needs.Find(Latency.Need))
	{
		Latency.Threshold = NeedState->ThresholdTime;
		Latency.LevelChange = NeedState->ElevatedSince;
		const double* Episode = LatencyEpisodes.Find(Latency.Need);
		Latency.bRepeat = Episode && NeedState->ElevatedSince >= 0.0 && *Episode == NeedState->ElevatedSince;
		LatencyEpisodes.Add(Latency.Need, NeedState->ElevatedSince);
	}
	Latency.Selected = Frame.Time;
	Latency.SelectedFrame = GFrameCounter;
	Latency.Dispatched = GetNow();
	Latencies.Start(Latency);

	const bool bAccepted = Handler && IsValid(Handler) && Handler->GetClass()->ImplementsInterface(UAdaptiveCompanionActionHandler::StaticClass())
		&& IAdaptiveCompanionActionHandler::Execute_PerformAction(Handler, Action->Action);
	if (!bAccepted)
	{
		bLastDispatchRejected = true;
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

	if (Phase == AdaptiveInterventionPhase::Started)
	{
		const int32 Waiting = Latencies.OnStarted(Intervention, GetNow(), GFrameCounter, PresentProbe.IsValid());
		if (Waiting != 0)
		{
			PresentProbe->Request(Waiting);
		}
	}

	if (AdaptiveInterventionPhase::IsFinal(Phase))
	{
		Latencies.OnEnded(Intervention, Phase);
		Outcomes.End(Intervention, Phase, Detail, GetNow());
		if (Intervention == Running)
		{
			Running = NAME_None;
		}
	}

	// finished by this phase (never started, or nothing renders): logged now rather than at the next update
	LogLatencies(Latencies.TakeFinished());
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

void UAdaptiveBehaviorStage::LogLatencies(TArray<FAdaptiveLatency> Finished) const
{
	UAdaptiveLogSubsystem* Log = GetLog();
	UAdaptiveDebugSubsystem* Debug = GetDebug();

	for (const FAdaptiveLatency& Latency : Finished)
	{
		if (Log)
		{
			TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
			Payload->SetNumberField(TEXT("id"), Latency.Id);
			Payload->SetStringField(TEXT("name"), Latency.Action.ToString());
			Payload->SetStringField(TEXT("need"), Latency.Need.ToString());
			Payload->SetBoolField(TEXT("repeat"), Latency.bRepeat);

			TSharedRef<FJsonObject> Times = MakeShared<FJsonObject>();
			Times->SetField(TEXT("threshold"), TimeValue(Latency.Threshold));
			Times->SetField(TEXT("level_change"), TimeValue(Latency.LevelChange));
			Times->SetField(TEXT("selected"), TimeValue(Latency.Selected));
			Times->SetField(TEXT("dispatched"), TimeValue(Latency.Dispatched));
			Times->SetField(TEXT("started"), TimeValue(Latency.Started));
			Times->SetField(TEXT("presented"), TimeValue(Latency.Presented));
			Payload->SetObjectField(TEXT("times"), Times);

			TSharedRef<FJsonObject> Ms = MakeShared<FJsonObject>();
			Ms->SetField(TEXT("smoothing"), MsValue(Latency.GetSmoothing()));
			Ms->SetField(TEXT("selection_wait"), MsValue(Latency.GetSelectionWait()));
			Ms->SetField(TEXT("pipeline"), MsValue(Latency.GetPipeline()));
			Ms->SetField(TEXT("handoff"), MsValue(Latency.GetHandoff()));
			Ms->SetField(TEXT("render"), MsValue(Latency.GetRender()));
			Ms->SetField(TEXT("reaction"), MsValue(Latency.GetReaction()));
			Ms->SetField(TEXT("response"), MsValue(Latency.GetResponse()));
			Ms->SetField(TEXT("total"), MsValue(Latency.GetTotal()));
			Payload->SetObjectField(TEXT("ms"), Ms);

			TSharedRef<FJsonObject> Frames = MakeShared<FJsonObject>();
			if (Latency.StartedFrame > 0)
			{
				Frames->SetNumberField(TEXT("handoff"), double(Latency.StartedFrame - Latency.SelectedFrame));
			}
			if (Latency.PresentedFrame > 0 && Latency.PresentedFrame >= Latency.StartedFrame)
			{
				Frames->SetNumberField(TEXT("render"), double(Latency.PresentedFrame - Latency.StartedFrame));
			}
			Payload->SetObjectField(TEXT("frames"), Frames);

			if (!Latency.Incomplete.IsEmpty())
			{
				Payload->SetStringField(TEXT("incomplete"), Latency.Incomplete);
			}
			Log->LogRecord(AdaptiveLogRecordType::Latency, Payload);
		}

		if (Debug)
		{
			// "#3 Action.ThreatCallout: selection → screen 38.1 ms, level change → screen 38.1 ms, threshold → screen 612 ms"
			FString Text = FString::Printf(TEXT("#%d %s: "), Latency.Id, *Latency.Action.ToString());
			if (Latency.Presented >= 0.0 && Latency.bRepeat)
			{
				Text += FString::Printf(TEXT("selection → screen %.1f ms (repeat in the same episode)"), Latency.GetReaction() * 1000.0);
			}
			else if (Latency.Presented >= 0.0)
			{
				Text += FString::Printf(TEXT("selection → screen %.1f ms, level change → screen %.0f ms, threshold → screen %.0f ms"),
					Latency.GetReaction() * 1000.0, Latency.GetResponse() * 1000.0, Latency.GetTotal() * 1000.0);
			}
			else if (Latency.Started >= 0.0)
			{
				Text += FString::Printf(TEXT("selection → started %.1f ms, %s"), Latency.GetPipeline() * 1000.0 + Latency.GetHandoff() * 1000.0, *Latency.Incomplete);
			}
			else
			{
				Text += FString::Printf(TEXT("never started (%s)"), *Latency.Incomplete);
			}
			Debug->SetText(EAdaptiveDebugSection::Measurements, TEXT("Latency"), Text);
		}

		if (OnLatency)
		{
			OnLatency(Latency);
		}
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
