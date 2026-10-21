#pragma once

#include "CoreMinimal.h"
#include "Pipeline/AdaptivePipelineStage.h"
#include "AdaptivePipelineStages.generated.h"

// Stages 2-6 are still empty. Each one gets its own file once it is implemented.
// Stage 1 (Telemetry) is in AdaptiveTelemetryStage.h.

/** 2 Feature extraction: aggregates signals over time windows and normalizes them to [0, 1] (#13) */
UCLASS(meta = (DisplayName = "Feature Stage"))
class ADAPTIVECOMPANIONFRAMEWORK_API UAdaptiveFeatureStage : public UAdaptivePipelineStage
{
	GENERATED_BODY()
};

/** 3 Assistance-need estimation: combines features into one score per need dimension (#14, #15) */
UCLASS(meta = (DisplayName = "Need Stage"))
class ADAPTIVECOMPANIONFRAMEWORK_API UAdaptiveNeedStage : public UAdaptivePipelineStage
{
	GENERATED_BODY()
};

/** 4 Context evaluation: what the player is currently doing and which actions are possible (#7) */
UCLASS(meta = (DisplayName = "Context Stage"))
class ADAPTIVECOMPANIONFRAMEWORK_API UAdaptiveContextStage : public UAdaptivePipelineStage
{
	GENERATED_BODY()
};

/** 5 Intervention selection: picks the best companion action for need and context, or none (#18) */
UCLASS(meta = (DisplayName = "Selection Stage"))
class ADAPTIVECOMPANIONFRAMEWORK_API UAdaptiveSelectionStage : public UAdaptivePipelineStage
{
	GENERATED_BODY()
};

/** 6 Behavior execution: hands the selected action to the companion and tracks its status (#19) */
UCLASS(meta = (DisplayName = "Behavior Stage"))
class ADAPTIVECOMPANIONFRAMEWORK_API UAdaptiveBehaviorStage : public UAdaptivePipelineStage
{
	GENERATED_BODY()
};
