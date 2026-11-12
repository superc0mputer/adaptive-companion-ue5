#include "Pipeline/AdaptiveSelectionStage.h"
#include "Debug/AdaptiveDebugSubsystem.h"
#include "Logging/AdaptiveLogRecordTypes.h"
#include "Logging/AdaptiveLogSubsystem.h"

void UAdaptiveSelectionStage::Update(FAdaptivePipelineFrame& Frame)
{
	const FAdaptiveNeedState* Need = Frame.Needs.Find(NeedName);
	const bool bElevated = Need && Need->Level != EAdaptiveNeedLevel::Normal;
	const bool bBecameElevated = bElevated && !bWasElevated;
	bWasElevated = bElevated;
	if (!bBecameElevated)
	{
		return;
	}

	Frame.Intervention = InterventionName;

	if (UAdaptiveLogSubsystem* Log = GetLog())
	{
		TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("name"), InterventionName.ToString());
		Payload->SetStringField(TEXT("phase"), TEXT("selected"));
		Payload->SetStringField(TEXT("need"), NeedName.ToString());
		Payload->SetNumberField(TEXT("score"), Need->Score);
		Payload->SetStringField(TEXT("level"), AdaptiveNeedMath::LevelName(Need->Level));
		Log->LogRecord(AdaptiveLogRecordType::Intervention, Payload);
	}

	if (UAdaptiveDebugSubsystem* Debug = GetDebug())
	{
		Debug->SetText(EAdaptiveDebugSection::Selection, TEXT("Selected"), FString::Printf(TEXT("%s at %.1f s"), *InterventionName.ToString(), Frame.Time));
	}
}
