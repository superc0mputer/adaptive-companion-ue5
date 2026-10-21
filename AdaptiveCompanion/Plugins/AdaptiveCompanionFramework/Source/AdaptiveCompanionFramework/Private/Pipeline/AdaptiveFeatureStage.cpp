#include "Pipeline/AdaptiveFeatureStage.h"
#include "Debug/AdaptiveDebugSubsystem.h"
#include "Logging/AdaptiveLogRecordTypes.h"
#include "Logging/AdaptiveLogSubsystem.h"

void UAdaptiveFeatureStage::Update(FAdaptivePipelineFrame& Frame)
{
	for (const FAdaptiveTelemetryEvent& Event : Frame.Events)
	{
		if (Event.Name == EventName)
		{
			WindowEvents.Add(Event);
		}
	}

	// events are added in time order, so the expired ones are at the front
	const double WindowStart = Frame.Time - Window;
	const int32 Expired = WindowEvents.IndexOfByPredicate([WindowStart](const FAdaptiveTelemetryEvent& Event) { return Event.Time > WindowStart; });
	WindowEvents.RemoveAt(0, Expired == INDEX_NONE ? WindowEvents.Num() : Expired);

	double Sum = 0.0;
	for (const FAdaptiveTelemetryEvent& Event : WindowEvents)
	{
		Sum += Event.Value;
	}

	const double Value = FMath::Clamp(Sum / MaxValue, 0.0, 1.0);
	Frame.Features.Add(FeatureName, Value);

	// the sum only changes when an event enters or leaves the window, so this logs rarely
	if (!LoggedValue.IsSet() || *LoggedValue != Value)
	{
		LoggedValue = Value;

		if (UAdaptiveLogSubsystem* Log = GetLog())
		{
			TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
			Payload->SetStringField(TEXT("name"), FeatureName.ToString());
			Payload->SetNumberField(TEXT("value"), Value);
			Log->LogRecord(AdaptiveLogRecordType::Feature, Payload);
		}
	}

	if (UAdaptiveDebugSubsystem* Debug = GetDebug())
	{
		Debug->SetBar(EAdaptiveDebugSection::Features, FeatureName.ToString(), Value);
	}
}
