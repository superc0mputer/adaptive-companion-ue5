#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/**
 *  Writes the phases of the test scenarios, the same way for all of them: one "scenario" record per
 *  phase change, a line in the overlay's Context section and one in the output log. Phases are
 *  ground truth for evaluating the need estimation, so they are never reported to the pipeline.
 */
namespace ScenarioLog
{
	inline const FString RecordType = TEXT("scenario");

	/**
	 *  Logs {scenario, phase, event} plus the fields AddFields adds, and shows OverlayText as
	 *  "Scenario.<Scenario>" in the overlay
	 */
	void Write(const UObject* WorldContextObject, FName Scenario, FName Phase, const TCHAR* Event,
		const FString& OverlayText, TFunctionRef<void(FJsonObject&)> AddFields);

	void Write(const UObject* WorldContextObject, FName Scenario, FName Phase, const TCHAR* Event,
		const FString& OverlayText);

	/** Removes the scenario's line from the overlay, e.g. when its level ends */
	void ClearOverlay(const UObject* WorldContextObject, FName Scenario);
}
