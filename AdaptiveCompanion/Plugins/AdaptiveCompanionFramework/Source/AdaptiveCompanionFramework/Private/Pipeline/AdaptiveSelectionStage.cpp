#include "Pipeline/AdaptiveSelectionStage.h"
#include "Debug/AdaptiveDebugSubsystem.h"
#include "Logging/AdaptiveLogRecordTypes.h"
#include "Logging/AdaptiveLogSubsystem.h"

void UAdaptiveSelectionStage::Update(FAdaptivePipelineFrame& Frame)
{
	const FAdaptiveNeedState* Need = Frame.Needs.Find(NeedName);
	if (!Need || !Need->bElevated || !Need->bChanged)
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
		Log->LogRecord(AdaptiveLogRecordType::Intervention, Payload);
	}

	if (UAdaptiveDebugSubsystem* Debug = GetDebug())
	{
		Debug->SetText(EAdaptiveDebugSection::Selection, TEXT("Selected"), FString::Printf(TEXT("%s at %.1f s"), *InterventionName.ToString(), Frame.Time));
	}
}
