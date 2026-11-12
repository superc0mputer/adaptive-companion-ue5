#pragma once

#include "CoreMinimal.h"
#include "Pipeline/AdaptivePipelineStage.h"
#include "Pipeline/AdaptiveFeatureTypes.h"
#include "AdaptiveFeatureStage.generated.h"

/**
 *  2 Feature extraction: turns the raw telemetry into features in [0, 1], one per entry in
 *  Features. Each feature aggregates one signal over a sliding time window (count, sum, ratio,
 *  latest, mean, slope, fraction below/above a threshold) and maps the result linearly from a
 *  configured [Min, Max] to [0, 1].
 *
 *  Events are kept with the time they were reported; values are sampled once per update. Every
 *  signal's samples are kept as long as the longest window that reads it needs them.
 *
 *  The default list is the single feature of the first loop (#6), damage taken in the last 5 s
 *  where 5 counts as 1. A game sets its own list in the pipeline config asset.
 */
UCLASS(meta = (DisplayName = "Feature Stage"))
class ADAPTIVECOMPANIONFRAMEWORK_API UAdaptiveFeatureStage : public UAdaptivePipelineStage
{
	GENERATED_BODY()

public:

	UAdaptiveFeatureStage();

	/** The features to compute, in the order they are shown in the overlay */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feature", meta = (TitleProperty = "Name"))
	TArray<FAdaptiveFeatureDefinition> Features;

	/** A feature is logged when its value, rounded to this step, changed. Keeps slowly drifting values from logging at every update */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feature", meta = (ClampMin = "0.0001", ClampMax = "1"))
	double LogStep = 0.01;

	/** Logs the definitions, so every session log says which features it was computed with */
	virtual void Initialize(UAdaptivePipelineSubsystem& InPipeline) override;

	virtual void Update(FAdaptivePipelineFrame& Frame) override;

	/** Raw (not normalized) value of a feature in the last update, unset if it had no data. For tests and the overlay */
	TOptional<double> GetRawValue(FName FeatureName) const;

protected:

	struct FFeatureState
	{
		bool bValid = false;

		/** Samples up to this time don't count (reset event, gate closed) */
		double ResetTime = -UE_BIG_NUMBER;

		TOptional<double> Raw;
		TOptional<double> LoggedValue;
	};

	/** Checks the definitions and resets the per-feature state when the list changed */
	void PrepareStates();

	void LogFeature(const FAdaptiveFeatureDefinition& Definition, double Value, const TOptional<double>& Raw) const;

	/** Samples per event name, oldest first */
	TMap<FName, TArray<FAdaptiveFeatureSample>> EventSamples;

	/** Samples per value name, one per update, oldest first */
	TMap<FName, TArray<FAdaptiveFeatureSample>> ValueSamples;

	/** Parallel to Features */
	TArray<FFeatureState> States;
};
