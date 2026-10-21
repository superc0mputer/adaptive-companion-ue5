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

	/** Loads the selected config asset, or returns the defaults if none is selected or it can't be loaded */
	const UAdaptivePipelineConfig* LoadConfig() const;
};
