#pragma once

#include "CoreMinimal.h"
#include "AdaptiveFeatureTypes.generated.h"

/** How a feature turns the samples of a signal within its window into one raw number */
UENUM(BlueprintType)
enum class EAdaptiveFeatureAggregation : uint8
{
	/** Value signal: the most recent sample */
	Latest,

	/** Value signal: mean of the samples in the window */
	Mean,

	/** Value signal: change per second, least-squares fit over the window. Needs samples spanning at least half the window */
	Slope,

	/** Value signal: share of the samples in the window below Threshold (0-1) */
	FractionBelow,

	/** Value signal: share of the samples in the window above Threshold (0-1) */
	FractionAbove,

	/** Event signal: number of events in the window */
	Count,

	/** Event signal: sum of the events' values in the window */
	Sum,

	/** Event signals: number of Signal events divided by the number of Denominator events in the window */
	Ratio,
};

/**
 *  One feature: which signal it reads, how it aggregates it over a time window and how the result
 *  is mapped to [0, 1]. The list of definitions is set in the pipeline config asset, so the
 *  framework knows no signal names of its own.
 */
USTRUCT(BlueprintType)
struct ADAPTIVECOMPANIONFRAMEWORK_API FAdaptiveFeatureDefinition
{
	GENERATED_BODY()

	/** Name in Frame.Features, the log and the overlay */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feature")
	FName Name;

	/** Telemetry signal (event or value, depending on the aggregation) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feature")
	FName Signal;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feature")
	EAdaptiveFeatureAggregation Aggregation = EAdaptiveFeatureAggregation::Latest;

	/** Ratio only: the event counted below the line */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feature", meta = (EditCondition = "Aggregation == EAdaptiveFeatureAggregation::Ratio", EditConditionHides))
	FName Denominator;

	/** Only samples younger than this count. Ignored by Latest */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feature", meta = (ClampMin = "0.1", Units = "s"))
	float Window = 5.f;

	/** FractionBelow / FractionAbove only */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feature", meta = (EditCondition = "Aggregation == EAdaptiveFeatureAggregation::FractionBelow || Aggregation == EAdaptiveFeatureAggregation::FractionAbove", EditConditionHides))
	double Threshold = 0.0;

	/** Raw value that maps to 0 (or to 1 if inverted) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Normalization")
	double Min = 0.0;

	/** Raw value that maps to 1 (or to 0 if inverted). Values outside [Min, Max] are clamped */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Normalization")
	double Max = 1.0;

	/** Maps Min to 1 and Max to 0, so that 1 always means "more of a problem" */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Normalization")
	bool bInvert = false;

	/** Feature value (already in [0, 1]) while there is no data: nothing reported yet, no Denominator events, a Slope window not yet half full */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Normalization", meta = (ClampMin = "0", ClampMax = "1"))
	double Default = 0.0;

	/** Optional event that restarts the window: samples up to and including it no longer count */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conditions")
	FName ResetEvent;

	/** Optional value signal: the feature is only computed while it is non-zero, otherwise it is Default and the window restarts when it becomes non-zero again */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conditions")
	FName OnlyWhileNonZero;
};

/** One sample of a signal: an event, or a value sampled at an update */
struct FAdaptiveFeatureSample
{
	/** Session time (s) */
	double Time = 0.0;
	double Value = 0.0;
};

/**
 *  The aggregations and the normalization, separate from the stage so they can be tested on their
 *  own. Samples are passed oldest first and already restricted to the window.
 */
namespace AdaptiveFeatureMath
{
	/**
	 *  Raw value of an aggregation over Samples (and DenominatorSamples for Ratio). Returns an unset
	 *  optional if there is no data, e.g. no samples, no denominator events, a Slope over less than
	 *  MinSlopeSpan seconds.
	 */
	ADAPTIVECOMPANIONFRAMEWORK_API TOptional<double> Aggregate(EAdaptiveFeatureAggregation Aggregation, TConstArrayView<FAdaptiveFeatureSample> Samples,
		TConstArrayView<FAdaptiveFeatureSample> DenominatorSamples = {}, double Threshold = 0.0, double MinSlopeSpan = 0.0);

	/** Maps Raw linearly from [Min, Max] to [0, 1] (to [1, 0] if inverted), clamped. Max <= Min gives a step at Min */
	ADAPTIVECOMPANIONFRAMEWORK_API double Normalize(double Raw, double Min, double Max, bool bInvert);

	/** True for the aggregations that read events rather than values */
	ADAPTIVECOMPANIONFRAMEWORK_API bool UsesEvents(EAdaptiveFeatureAggregation Aggregation);

	/** The samples younger than WindowStart, given samples oldest first */
	ADAPTIVECOMPANIONFRAMEWORK_API TConstArrayView<FAdaptiveFeatureSample> After(TConstArrayView<FAdaptiveFeatureSample> Samples, double WindowStart);

	/** Value rounded to a multiple of Step, for logging only when a value visibly changed */
	ADAPTIVECOMPANIONFRAMEWORK_API double RoundTo(double Value, double Step);
}
