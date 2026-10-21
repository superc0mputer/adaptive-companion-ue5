#pragma once

#include "CoreMinimal.h"
#include "Pipeline/AdaptivePipelineStage.h"
#include "AdaptiveSelectionStage.generated.h"

/**
 *  5 Intervention selection. Deliberately naive for now (#6): one fixed intervention, selected
 *  whenever one need dimension becomes Elevated. No cooldown beyond that: it is selected again
 *  only after the need went back to Normal.
 *  With the defaults: Intervention.Combat.Placeholder when Combat becomes Elevated.
 *
 *  Replaced by utility-based selection over the game's actions in #18.
 */
UCLASS(meta = (DisplayName = "Selection Stage"))
class ADAPTIVECOMPANIONFRAMEWORK_API UAdaptiveSelectionStage : public UAdaptivePipelineStage
{
	GENERATED_BODY()

public:

	/** Need dimension that triggers the intervention */
	UPROPERTY(EditAnywhere, Category = "Selection")
	FName NeedName = TEXT("Combat");

	/** Intervention selected when the need becomes Elevated. The game decides what it means */
	UPROPERTY(EditAnywhere, Category = "Selection")
	FName InterventionName = TEXT("Intervention.Combat.Placeholder");

	virtual void Update(FAdaptivePipelineFrame& Frame) override;
};
