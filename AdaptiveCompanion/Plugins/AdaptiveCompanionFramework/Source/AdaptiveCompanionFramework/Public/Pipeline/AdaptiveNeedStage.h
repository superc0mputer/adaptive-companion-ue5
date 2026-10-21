#pragma once

#include "CoreMinimal.h"
#include "Pipeline/AdaptivePipelineStage.h"
#include "AdaptiveNeedStage.generated.h"

/**
 *  3 Assistance-need estimation. Deliberately naive for now (#6): one dimension whose score is
 *  one feature, and a single threshold that switches it between Normal and Elevated.
 *  With the defaults: Combat = Combat.DamageRecent, Elevated from 0.6.
 *
 *  Replaced by weighted scores with smoothing and hysteresis in #14, #15.
 */
UCLASS(meta = (DisplayName = "Need Stage"))
class ADAPTIVECOMPANIONFRAMEWORK_API UAdaptiveNeedStage : public UAdaptivePipelineStage
{
	GENERATED_BODY()

public:

	/** Name of the need dimension in Frame.Needs */
	UPROPERTY(EditAnywhere, Category = "Need")
	FName NeedName = TEXT("Combat");

	/** Feature used as the score */
	UPROPERTY(EditAnywhere, Category = "Need")
	FName FeatureName = TEXT("Combat.DamageRecent");

	/** The need is Elevated while the score is at or above this */
	UPROPERTY(EditAnywhere, Category = "Need", meta = (ClampMin = "0", ClampMax = "1"))
	float Threshold = 0.6f;

	virtual void Update(FAdaptivePipelineFrame& Frame) override;

protected:

	bool bElevated = false;
};
