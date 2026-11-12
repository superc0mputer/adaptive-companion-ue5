#include "Pipeline/AdaptiveBehaviorStage.h"
#include "Pipeline/AdaptiveCompanionAction.h"
#include "Pipeline/AdaptivePipelineSubsystem.h"
#include "AdaptiveCompanionFramework.h"
#include "Debug/AdaptiveDebugSubsystem.h"
#include "Logging/AdaptiveLogRecordTypes.h"
#include "Logging/AdaptiveLogSubsystem.h"

bool AdaptiveInterventionPhase::IsFinal(FName Phase)
{
	static const TSet<FName> FinalPhases = { Completed, Failed, Interrupted, Rejected, TimedOut, TEXT("ended") };
	return FinalPhases.Contains(Phase);
}

void UAdaptiveBehaviorStage::Update(FAdaptivePipelineFrame& Frame)
{
	if (Frame.Intervention.IsNone())
	{
		return;
	}

	Running = Frame.Intervention;
	RunningSince = Frame.Time;
	DispatchTimes.Add(Frame.Intervention, Frame.Time);

	const FAdaptiveActionState* Action = Frame.Actions.FindByPredicate([&Frame](const FAdaptiveActionState& State) { return State.Name == Frame.Intervention; });
	UObject* Handler = Action ? Action->Handler : nullptr;

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
}

void UAdaptiveBehaviorStage::ReportPhase(FName Intervention, FName Phase, const FString& Detail)
{
	LogPhase(Intervention, Phase, Detail);

	if (Intervention == Running && AdaptiveInterventionPhase::IsFinal(Phase))
	{
		Running = NAME_None;
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
	const UAdaptivePipelineSubsystem* Pipeline = GetPipeline();
	const double Now = Pipeline ? Pipeline->GetTime() : 0.0;
	const double* Dispatched = DispatchTimes.Find(Intervention);
	const bool bFinal = AdaptiveInterventionPhase::IsFinal(Phase);

	if (UAdaptiveLogSubsystem* Log = GetLog())
	{
		TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
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
