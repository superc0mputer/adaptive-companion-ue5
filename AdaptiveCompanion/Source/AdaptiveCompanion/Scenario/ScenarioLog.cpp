#include "ScenarioLog.h"
#include "AdaptiveCompanion.h"
#include "Debug/AdaptiveDebugSubsystem.h"
#include "Logging/AdaptiveLogSubsystem.h"

namespace ScenarioLog
{
	FString OverlayName(FName Scenario)
	{
		return FString::Printf(TEXT("Scenario.%s"), *Scenario.ToString());
	}

	void Write(const UObject* WorldContextObject, FName Scenario, FName Phase, const TCHAR* Event,
		const FString& OverlayText, TFunctionRef<void(FJsonObject&)> AddFields)
	{
		if (UAdaptiveLogSubsystem* Log = UAdaptiveLogSubsystem::Get(WorldContextObject))
		{
			TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
			Payload->SetStringField(TEXT("scenario"), Scenario.ToString());
			Payload->SetStringField(TEXT("phase"), Phase.ToString());
			Payload->SetStringField(TEXT("event"), Event);
			AddFields(*Payload);
			Log->LogRecord(RecordType, Payload);
		}

		if (UAdaptiveDebugSubsystem* Debug = UAdaptiveDebugSubsystem::Get(WorldContextObject))
		{
			Debug->SetText(EAdaptiveDebugSection::Context, OverlayName(Scenario), OverlayText);
		}

		UE_LOG(LogAdaptiveCompanion, Log, TEXT("Scenario %s: %s %s"), *Scenario.ToString(), *Phase.ToString(), Event);
	}

	void Write(const UObject* WorldContextObject, FName Scenario, FName Phase, const TCHAR* Event,
		const FString& OverlayText)
	{
		Write(WorldContextObject, Scenario, Phase, Event, OverlayText, [](FJsonObject&) {});
	}

	void ClearOverlay(const UObject* WorldContextObject, FName Scenario)
	{
		if (UAdaptiveDebugSubsystem* Debug = UAdaptiveDebugSubsystem::Get(WorldContextObject))
		{
			Debug->RemoveValue(EAdaptiveDebugSection::Context, OverlayName(Scenario));
		}
	}
}
