#pragma once

#include "CoreMinimal.h"
#include "Pipeline/AdaptivePipelineStage.h"
#include "Pipeline/AdaptiveFeatureStage.h"
#include "Pipeline/AdaptiveNeedStage.h"
#include "AdaptivePipelineStages.generated.h"

// Stages that are still empty are declared here; each one gets its own file once it is
// implemented. Stages 2 and 3 are included above, stage 1 (Telemetry) is in AdaptiveTelemetryStage.h.

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
