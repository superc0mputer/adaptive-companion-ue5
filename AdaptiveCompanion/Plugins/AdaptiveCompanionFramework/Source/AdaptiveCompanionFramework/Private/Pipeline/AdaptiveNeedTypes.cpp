#include "Pipeline/AdaptiveNeedTypes.h"

double AdaptiveNeedMath::EvaluateRule(const FAdaptiveNeedRule& Rule, const TMap<FName, double>& Features)
{
	if (Rule.Features.IsEmpty())
	{
		return 0.0;
	}

	double Smallest = 1.0;
	for (const FAdaptiveNeedCondition& Condition : Rule.Features)
	{
		const double Value = FMath::Clamp(Features.FindRef(Condition.Feature), 0.0, 1.0);
		Smallest = FMath::Min(Smallest, Condition.bNot ? 1.0 - Value : Value);
	}
	return FMath::Clamp(Rule.Weight, 0.0, 1.0) * Smallest;
}

double AdaptiveNeedMath::Combine(TConstArrayView<double> Values)
{
	double NoneFires = 1.0;
	for (const double Value : Values)
	{
		NoneFires *= 1.0 - FMath::Clamp(Value, 0.0, 1.0);
	}
	return 1.0 - NoneFires;
}

FString AdaptiveNeedMath::Describe(const FAdaptiveNeedRule& Rule)
{
	if (!Rule.Name.IsNone())
	{
		return Rule.Name.ToString();
	}

	TArray<FString> Parts;
	for (const FAdaptiveNeedCondition& Condition : Rule.Features)
	{
		Parts.Add(Condition.bNot ? TEXT("not ") + Condition.Feature.ToString() : Condition.Feature.ToString());
	}
	return FString::Join(Parts, TEXT(" & "));
}
