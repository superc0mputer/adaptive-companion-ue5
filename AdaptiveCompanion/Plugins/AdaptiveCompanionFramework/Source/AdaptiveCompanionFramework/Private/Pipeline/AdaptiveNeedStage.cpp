#include "Pipeline/AdaptiveNeedStage.h"
#include "Debug/AdaptiveDebugSubsystem.h"
#include "Logging/AdaptiveLogRecordTypes.h"
#include "Logging/AdaptiveLogSubsystem.h"

void UAdaptiveNeedStage::Update(FAdaptivePipelineFrame& Frame)
{
	FAdaptiveNeedState State;
	State.Score = Frame.Features.FindRef(FeatureName);
	State.bElevated = State.Score >= Threshold;
	State.bChanged = State.bElevated != bElevated;
	Frame.Needs.Add(NeedName, State);

	const TCHAR* Level = State.bElevated ? TEXT("Elevated") : TEXT("Normal");

	if (State.bChanged)
	{
		bElevated = State.bElevated;

		if (UAdaptiveLogSubsystem* Log = GetLog())
		{
			TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
			Payload->SetStringField(TEXT("need"), NeedName.ToString());
			Payload->SetNumberField(TEXT("score"), State.Score);
			Payload->SetStringField(TEXT("level"), Level);
			Log->LogRecord(AdaptiveLogRecordType::State, Payload);
		}
	}

	if (UAdaptiveDebugSubsystem* Debug = GetDebug())
	{
		Debug->SetBar(EAdaptiveDebugSection::Need, NeedName.ToString(), State.Score);
		Debug->SetText(EAdaptiveDebugSection::Need, NeedName.ToString() + TEXT(" level"), Level);
	}
}
