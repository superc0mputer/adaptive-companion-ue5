#include "Pipeline/AdaptiveFeatureTypes.h"

namespace AdaptiveFeatureMath
{
	TOptional<double> Aggregate(EAdaptiveFeatureAggregation Aggregation, TConstArrayView<FAdaptiveFeatureSample> Samples,
		TConstArrayView<FAdaptiveFeatureSample> DenominatorSamples, double Threshold, double MinSlopeSpan)
	{
		switch (Aggregation)
		{
		case EAdaptiveFeatureAggregation::Count:
			// no events is a count of 0, not missing data
			return static_cast<double>(Samples.Num());

		case EAdaptiveFeatureAggregation::Sum:
		{
			double Sum = 0.0;
			for (const FAdaptiveFeatureSample& Sample : Samples)
			{
				Sum += Sample.Value;
			}
			return Sum;
		}

		case EAdaptiveFeatureAggregation::Ratio:
			if (DenominatorSamples.IsEmpty())
			{
				return {};
			}
			return static_cast<double>(Samples.Num()) / DenominatorSamples.Num();

		default:
			break;
		}

		// value aggregations: no samples means nothing was reported (yet)
		if (Samples.IsEmpty())
		{
			return {};
		}

		switch (Aggregation)
		{
		case EAdaptiveFeatureAggregation::Latest:
			return Samples.Last().Value;

		case EAdaptiveFeatureAggregation::Mean:
		{
			double Sum = 0.0;
			for (const FAdaptiveFeatureSample& Sample : Samples)
			{
				Sum += Sample.Value;
			}
			return Sum / Samples.Num();
		}

		case EAdaptiveFeatureAggregation::FractionBelow:
		case EAdaptiveFeatureAggregation::FractionAbove:
		{
			const bool bBelow = Aggregation == EAdaptiveFeatureAggregation::FractionBelow;
			int32 Matching = 0;
			for (const FAdaptiveFeatureSample& Sample : Samples)
			{
				Matching += (bBelow ? Sample.Value < Threshold : Sample.Value > Threshold) ? 1 : 0;
			}
			return static_cast<double>(Matching) / Samples.Num();
		}

		case EAdaptiveFeatureAggregation::Slope:
		{
			const double Span = Samples.Last().Time - Samples[0].Time;
			if (Samples.Num() < 2 || Span <= 0.0 || Span < MinSlopeSpan)
			{
				return {};
			}

			// least squares, relative to the means so large session times don't cost precision
			double MeanTime = 0.0;
			double MeanValue = 0.0;
			for (const FAdaptiveFeatureSample& Sample : Samples)
			{
				MeanTime += Sample.Time;
				MeanValue += Sample.Value;
			}
			MeanTime /= Samples.Num();
			MeanValue /= Samples.Num();

			double Covariance = 0.0;
			double Variance = 0.0;
			for (const FAdaptiveFeatureSample& Sample : Samples)
			{
				Covariance += (Sample.Time - MeanTime) * (Sample.Value - MeanValue);
				Variance += FMath::Square(Sample.Time - MeanTime);
			}
			return Covariance / Variance;
		}

		default:
			checkNoEntry();
			return {};
		}
	}

	double Normalize(double Raw, double Min, double Max, bool bInvert)
	{
		const double Value = Max > Min ? FMath::Clamp((Raw - Min) / (Max - Min), 0.0, 1.0) : (Raw >= Min ? 1.0 : 0.0);
		return bInvert ? 1.0 - Value : Value;
	}

	bool UsesEvents(EAdaptiveFeatureAggregation Aggregation)
	{
		return Aggregation == EAdaptiveFeatureAggregation::Count
			|| Aggregation == EAdaptiveFeatureAggregation::Sum
			|| Aggregation == EAdaptiveFeatureAggregation::Ratio;
	}

	TConstArrayView<FAdaptiveFeatureSample> After(TConstArrayView<FAdaptiveFeatureSample> Samples, double WindowStart)
	{
		// oldest first, so the first sample inside the window splits the array
		int32 First = 0;
		while (First < Samples.Num() && Samples[First].Time <= WindowStart)
		{
			++First;
		}
		return Samples.RightChop(First);
	}
}

double AdaptiveFeatureMath::RoundTo(double Value, double Step)
{
	// for steps like 0.01 divide by the whole number 100 instead of multiplying by 0.01, so the
	// result is the double closest to 0.83 and the log shows 0.83, not 0.8300000000000001
	const double Inverse = FMath::RoundToDouble(1.0 / Step);
	if (Inverse >= 1.0 && FMath::IsNearlyEqual(Inverse * Step, 1.0))
	{
		return FMath::RoundToDouble(Value * Inverse) / Inverse;
	}
	return FMath::RoundToDouble(Value / Step) * Step;
}
