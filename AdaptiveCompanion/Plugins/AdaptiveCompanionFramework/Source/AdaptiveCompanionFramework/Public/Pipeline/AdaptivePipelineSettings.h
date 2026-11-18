#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "AdaptivePipelineSettings.generated.h"

class UAdaptivePipelineConfig;

/**
 *  Pipeline settings (Project Settings > Plugins > Adaptive Companion Pipeline). Only selects the
 *  config asset; the settings themselves live in that asset.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Adaptive Companion Pipeline"))
class ADAPTIVECOMPANIONFRAMEWORK_API UAdaptivePipelineSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:

	UAdaptivePipelineSettings();

	/** If false, the pipeline doesn't run and reported telemetry is ignored */
	UPROPERTY(Config, EditAnywhere, Category = "Pipeline")
	bool bEnabled = true;

	/** The framework settings of this game. Without one the pipeline runs with the defaults */
	UPROPERTY(Config, EditAnywhere, Category = "Pipeline", meta = (EditCondition = "bEnabled"))
	TSoftObjectPtr<UAdaptivePipelineConfig> Config;

	/** Writes what the game gives the pipeline at every update into the session log (input records), so the session can be replayed without the game (#24). About 3 KB/s */
	UPROPERTY(Config, EditAnywhere, Category = "Pipeline", meta = (EditCondition = "bEnabled"))
	bool bRecordInputs = true;

	/** Measures what the pipeline costs per update and per stage, and the frame times around it, and logs a summary every PerformanceWindow (perf records) and at the end (perf_summary, #25) */
	UPROPERTY(Config, EditAnywhere, Category = "Measurements", meta = (EditCondition = "bEnabled"))
	bool bMeasurePerformance = true;

	/** Seconds per perf record */
	UPROPERTY(Config, EditAnywhere, Category = "Measurements", meta = (EditCondition = "bEnabled && bMeasurePerformance", ClampMin = "1", Units = "s"))
	float PerformanceWindow = 5.f;

	/** An update that takes longer than this is logged on its own with its stages' times (perf_spike), so rare maxima can be explained */
	UPROPERTY(Config, EditAnywhere, Category = "Measurements", meta = (EditCondition = "bEnabled && bMeasurePerformance", ClampMin = "0.1", Units = "ms"))
	float SpikeThreshold = 5.f;

	/** Loads the selected config asset, or returns the defaults if none is selected or it can't be loaded */
	const UAdaptivePipelineConfig* LoadConfig() const;
};
