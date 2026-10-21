#include "Pipeline/AdaptivePipelineConfig.h"
#include "Pipeline/AdaptivePipelineStages.h"
#include "Pipeline/AdaptiveTelemetryStage.h"

UAdaptivePipelineConfig::UAdaptivePipelineConfig()
{
	// every slot starts with the stage's base class, so a new asset runs as is
	Telemetry = CreateDefaultSubobject<UAdaptiveTelemetryStage>(TEXT("Telemetry"));
	Features = CreateDefaultSubobject<UAdaptiveFeatureStage>(TEXT("Features"));
	Need = CreateDefaultSubobject<UAdaptiveNeedStage>(TEXT("Need"));
	Context = CreateDefaultSubobject<UAdaptiveContextStage>(TEXT("Context"));
	Selection = CreateDefaultSubobject<UAdaptiveSelectionStage>(TEXT("Selection"));
	Behavior = CreateDefaultSubobject<UAdaptiveBehaviorStage>(TEXT("Behavior"));
}
