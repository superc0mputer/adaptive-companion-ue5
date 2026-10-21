#include "Pipeline/AdaptiveTelemetryStage.h"
#include "Pipeline/AdaptivePipelineSubsystem.h"
#include "Debug/AdaptiveDebugSubsystem.h"
#include "Logging/AdaptiveLogRecordTypes.h"
#include "Logging/AdaptiveLogSubsystem.h"

void UAdaptiveTelemetryStage::ReportEvent(FName Name, double Value)
{
	const UAdaptivePipelineSubsystem* Pipeline = GetPipeline();
	PendingEvents.Add({ Name, Value, Pipeline ? Pipeline->GetTime() : 0.0 });

	// logged now rather than at the next update, so the record's timestamp is when it happened
	LogSignal(TEXT("event"), Name, Value);
}

void UAdaptiveTelemetryStage::ReportValue(FName Name, double Value)
{
	Values.Add(Name, Value);
}

void UAdaptiveTelemetryStage::Update(FAdaptivePipelineFrame& Frame)
{
	Frame.Events = MoveTemp(PendingEvents);
	PendingEvents.Reset();
	Frame.Values = Values;

	UAdaptiveDebugSubsystem* Debug = GetPipeline() ? GetPipeline()->GetDebug() : nullptr;

	for (const TPair<FName, double>& Value : Values)
	{
		const double* Logged = LoggedValues.Find(Value.Key);
		if (bLogEveryUpdate || !Logged || *Logged != Value.Value)
		{
			LogSignal(TEXT("value"), Value.Key, Value.Value);
			LoggedValues.Add(Value.Key, Value.Value);
		}

		if (Debug)
		{
			Debug->SetNumber(EAdaptiveDebugSection::Telemetry, Value.Key.ToString(), Value.Value);
		}
	}

	for (const FAdaptiveTelemetryEvent& Event : Frame.Events)
	{
		const int32 Count = ++EventCounts.FindOrAdd(Event.Name);
		if (Debug)
		{
			Debug->SetText(EAdaptiveDebugSection::Telemetry, Event.Name.ToString(), FString::Printf(TEXT("%.2f (%dx)"), Event.Value, Count));
		}
	}
}

void UAdaptiveTelemetryStage::LogSignal(const TCHAR* Kind, FName Name, double Value) const
{
	UAdaptiveLogSubsystem* Log = GetPipeline() ? GetPipeline()->GetLog() : nullptr;
	if (!Log)
	{
		return;
	}

	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("name"), Name.ToString());
	Payload->SetStringField(TEXT("kind"), Kind);
	Payload->SetNumberField(TEXT("value"), Value);
	Log->LogRecord(AdaptiveLogRecordType::Signal, Payload);
}
