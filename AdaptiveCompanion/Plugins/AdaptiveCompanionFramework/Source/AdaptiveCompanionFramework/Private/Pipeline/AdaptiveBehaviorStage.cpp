#include "Pipeline/AdaptiveBehaviorStage.h"
#include "Pipeline/AdaptivePipelineSubsystem.h"
#include "AdaptiveCompanionFramework.h"
#include "Debug/AdaptiveDebugSubsystem.h"
#include "Logging/AdaptiveLogRecordTypes.h"
#include "Logging/AdaptiveLogSubsystem.h"

void UAdaptiveBehaviorStage::Update(FAdaptivePipelineFrame& Frame)
{
	UAdaptivePipelineSubsystem* Pipeline = GetPipeline();
	if (Frame.Intervention.IsNone() || !Pipeline)
	{
		return;
	}

	const int32 Receivers = Pipeline->OnIntervention.GetAllObjects().Num();
	if (Receivers == 0)
	{
		UE_LOG(LogAdaptiveCompanionFramework, Warning, TEXT("Intervention '%s' selected, but the game doesn't listen to OnIntervention."), *Frame.Intervention.ToString());
	}

	// logged before the broadcast, so a phase the game reports synchronously comes after it
	LogPhase(Frame.Intervention, TEXT("dispatched"), Receivers);
	Pipeline->OnIntervention.Broadcast(Frame.Intervention);
}

void UAdaptiveBehaviorStage::ReportPhase(FName Intervention, FName Phase)
{
	LogPhase(Intervention, Phase.ToString());
}

void UAdaptiveBehaviorStage::LogPhase(FName Intervention, const FString& Phase, int32 Receivers) const
{
	if (UAdaptiveLogSubsystem* Log = GetLog())
	{
		TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("name"), Intervention.ToString());
		Payload->SetStringField(TEXT("phase"), Phase);
		if (Receivers != INDEX_NONE)
		{
			Payload->SetNumberField(TEXT("receivers"), Receivers);
		}
		Log->LogRecord(AdaptiveLogRecordType::Intervention, Payload);
	}

	if (UAdaptiveDebugSubsystem* Debug = GetDebug())
	{
		const UAdaptivePipelineSubsystem* Pipeline = GetPipeline();
		Debug->SetText(EAdaptiveDebugSection::Behavior, Intervention.ToString(), FString::Printf(TEXT("%s at %.1f s"), *Phase, Pipeline ? Pipeline->GetTime() : 0.0));
	}
}
