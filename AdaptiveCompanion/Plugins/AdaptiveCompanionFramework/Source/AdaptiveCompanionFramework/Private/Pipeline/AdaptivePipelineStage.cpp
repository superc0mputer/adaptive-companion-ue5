#include "Pipeline/AdaptivePipelineStage.h"
#include "Pipeline/AdaptivePipelineSubsystem.h"
#include "Engine/GameInstance.h"

void UAdaptivePipelineStage::Initialize(UAdaptivePipelineSubsystem& InPipeline)
{
	Pipeline = &InPipeline;
}

void UAdaptivePipelineStage::Deinitialize()
{
}

void UAdaptivePipelineStage::Update(FAdaptivePipelineFrame& Frame)
{
}

UWorld* UAdaptivePipelineStage::GetWorld() const
{
	// nullptr for the templates in the config asset, so the editor treats them as world-less
	return Pipeline ? Pipeline->GetGameInstance()->GetWorld() : nullptr;
}
