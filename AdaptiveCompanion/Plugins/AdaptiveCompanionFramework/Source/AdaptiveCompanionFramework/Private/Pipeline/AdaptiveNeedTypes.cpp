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

double AdaptiveNeedMath::Smooth(double Previous, double Target, double DeltaTime, double RiseHalfLife, double FallHalfLife)
{
	const double HalfLife = Target > Previous ? RiseHalfLife : FallHalfLife;
	if (HalfLife <= 0.0)
	{
		return Target;
	}

	// share of the previous value that remains after DeltaTime: 0.5 per half-life
	const double Keep = FMath::Pow(0.5, FMath::Max(DeltaTime, 0.0) / HalfLife);
	return Keep * Previous + (1.0 - Keep) * Target;
}

EAdaptiveNeedLevel AdaptiveNeedMath::NextLevel(EAdaptiveNeedLevel Current, double Score, const FAdaptiveNeedLevelSettings& Settings)
{
	// thresholds made consistent, so a misconfigured asset can't make a level oscillate
	const double ElevatedEnter = Settings.ElevatedEnter;
	const double ElevatedExit = FMath::Min(Settings.ElevatedExit, ElevatedEnter);
	const double HighEnter = FMath::Max(Settings.HighEnter, ElevatedEnter);
	const double HighExit = FMath::Clamp(Settings.HighExit, ElevatedExit, HighEnter);

	switch (Current)
	{
	case EAdaptiveNeedLevel::Normal:
		return Score >= HighEnter ? EAdaptiveNeedLevel::High : Score >= ElevatedEnter ? EAdaptiveNeedLevel::Elevated : EAdaptiveNeedLevel::Normal;
	case EAdaptiveNeedLevel::Elevated:
		return Score >= HighEnter ? EAdaptiveNeedLevel::High : Score < ElevatedExit ? EAdaptiveNeedLevel::Normal : EAdaptiveNeedLevel::Elevated;
	case EAdaptiveNeedLevel::High:
	default:
		return Score >= HighExit ? EAdaptiveNeedLevel::High : Score >= ElevatedExit ? EAdaptiveNeedLevel::Elevated : EAdaptiveNeedLevel::Normal;
	}
}

const TCHAR* AdaptiveNeedMath::LevelName(EAdaptiveNeedLevel Level)
{
	switch (Level)
	{
	case EAdaptiveNeedLevel::Elevated: return TEXT("Elevated");
	case EAdaptiveNeedLevel::High: return TEXT("High");
	default: return TEXT("Normal");
	}
}
