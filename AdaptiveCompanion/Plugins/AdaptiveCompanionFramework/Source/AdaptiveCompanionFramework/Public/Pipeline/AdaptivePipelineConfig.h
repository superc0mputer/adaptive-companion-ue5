#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "AdaptivePipelineConfig.generated.h"

class UAdaptiveTelemetryStage;
class UAdaptiveFeatureStage;
class UAdaptiveNeedStage;
class UAdaptiveContextStage;
class UAdaptiveSelectionStage;
class UAdaptiveBehaviorStage;

/**
 *  Framework settings of one game: the update rate and which class (with which parameters) runs
 *  each pipeline stage. The game creates one asset of this type and selects it in
 *  Project Settings > Plugins > Adaptive Companion Pipeline.
 *
 *  A stage is replaced by picking a subclass in its slot; an empty slot uses the stage's base class.
 */
UCLASS(BlueprintType)
class ADAPTIVECOMPANIONFRAMEWORK_API UAdaptivePipelineConfig : public UDataAsset
{
	GENERATED_BODY()

public:

	UAdaptivePipelineConfig();

	/** Pipeline updates per second. The game can report telemetry at any time; the stages run at this rate */
	UPROPERTY(EditAnywhere, Category = "Pipeline", meta = (ClampMin = "1", ClampMax = "120", Units = "Hz"))
	float UpdateRate = 10.f;

	/** If true, no updates run while the game is paused. Reported events are kept for the next update */
	UPROPERTY(EditAnywhere, Category = "Pipeline")
	bool bPauseWithGame = true;

	UPROPERTY(EditAnywhere, Instanced, Category = "Stages", meta = (DisplayName = "1 Telemetry"))
	TObjectPtr<UAdaptiveTelemetryStage> Telemetry;

	UPROPERTY(EditAnywhere, Instanced, Category = "Stages", meta = (DisplayName = "2 Features"))
	TObjectPtr<UAdaptiveFeatureStage> Features;

	UPROPERTY(EditAnywhere, Instanced, Category = "Stages", meta = (DisplayName = "3 Assistance Need"))
	TObjectPtr<UAdaptiveNeedStage> Need;

	UPROPERTY(EditAnywhere, Instanced, Category = "Stages", meta = (DisplayName = "4 Context"))
	TObjectPtr<UAdaptiveContextStage> Context;

	UPROPERTY(EditAnywhere, Instanced, Category = "Stages", meta = (DisplayName = "5 Intervention Selection"))
	TObjectPtr<UAdaptiveSelectionStage> Selection;

	UPROPERTY(EditAnywhere, Instanced, Category = "Stages", meta = (DisplayName = "6 Behavior"))
	TObjectPtr<UAdaptiveBehaviorStage> Behavior;
};
