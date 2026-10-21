#pragma once

#include "CoreMinimal.h"
#include "Pipeline/AdaptivePipelineStage.h"
#include "AdaptiveFeatureStage.generated.h"

/**
 *  2 Feature extraction. Deliberately naive for now (#6): one feature, the sum of one event's
 *  values over a sliding window, divided by a fixed maximum and clamped to [0, 1].
 *  With the defaults: damage taken in the last 5 s, where 5 damage counts as 1.
 *
 *  Replaced by windowed aggregations over all signals in #13.
 */
UCLASS(meta = (DisplayName = "Feature Stage"))
class ADAPTIVECOMPANIONFRAMEWORK_API UAdaptiveFeatureStage : public UAdaptivePipelineStage
{
	GENERATED_BODY()

public:

	/** Event whose values are summed */
	UPROPERTY(EditAnywhere, Category = "Feature")
	FName EventName = TEXT("Combat.DamageTaken");

	/** Name of the resulting feature in Frame.Features */
	UPROPERTY(EditAnywhere, Category = "Feature")
	FName FeatureName = TEXT("Combat.DamageRecent");

	/** Only events younger than this count */
	UPROPERTY(EditAnywhere, Category = "Feature", meta = (ClampMin = "0.1", Units = "s"))
	float Window = 5.f;

	/** Sum that maps to 1 */
	UPROPERTY(EditAnywhere, Category = "Feature", meta = (ClampMin = "0.001"))
	float MaxValue = 5.f;

	virtual void Update(FAdaptivePipelineFrame& Frame) override;

protected:

	/** Events of EventName within the window, oldest first */
	TArray<FAdaptiveTelemetryEvent> WindowEvents;

	/** Value of the feature when it was last logged, unset before the first update */
	TOptional<double> LoggedValue;
};
