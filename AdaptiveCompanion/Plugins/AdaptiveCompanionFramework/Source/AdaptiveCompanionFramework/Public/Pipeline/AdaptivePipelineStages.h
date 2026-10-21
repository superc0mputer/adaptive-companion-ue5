#pragma once

#include "CoreMinimal.h"
#include "Pipeline/AdaptivePipelineStage.h"
#include "Pipeline/AdaptiveFeatureStage.h"
#include "Pipeline/AdaptiveNeedStage.h"
#include "Pipeline/AdaptiveSelectionStage.h"
#include "Pipeline/AdaptiveBehaviorStage.h"
#include "AdaptivePipelineStages.generated.h"

// Stages that are still empty are declared here; each one gets its own file once it is
// implemented. Stages 2, 3, 5 and 6 are included above, stage 1 (Telemetry) is in AdaptiveTelemetryStage.h.

/** 4 Context evaluation: what the player is currently doing and which actions are possible (#7) */
UCLASS(meta = (DisplayName = "Context Stage"))
class ADAPTIVECOMPANIONFRAMEWORK_API UAdaptiveContextStage : public UAdaptivePipelineStage
{
	GENERATED_BODY()
};
