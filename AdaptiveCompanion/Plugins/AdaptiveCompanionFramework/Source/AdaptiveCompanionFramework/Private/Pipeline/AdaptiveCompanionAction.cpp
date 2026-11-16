#include "Pipeline/AdaptiveCompanionAction.h"

bool UAdaptiveCompanionAction::AllowsContexts(const TSet<FName>& ActiveContexts, FString& OutReason) const
{
	for (const FName Context : RequiredContexts)
	{
		if (!ActiveContexts.Contains(Context))
		{
			OutReason = FString::Printf(TEXT("needs %s"), *Context.ToString());
			return false;
		}
	}

	for (const FName Context : BlockedContexts)
	{
		if (ActiveContexts.Contains(Context))
		{
			OutReason = FString::Printf(TEXT("not during %s"), *Context.ToString());
			return false;
		}
	}

	return true;
}

double UAdaptiveCompanionAction::GetSuitability(const TSet<FName>& ActiveContexts) const
{
	double Lowest = 1.0;
	for (const TPair<FName, float>& Pair : Suitability)
	{
		if (ActiveContexts.Contains(Pair.Key))
		{
			Lowest = FMath::Min(Lowest, FMath::Clamp(double(Pair.Value), 0.0, 1.0));
		}
	}
	return Lowest;
}

double UAdaptiveCompanionAction::GetRelevance(FName Dimension) const
{
	const float* Value = Relevance.Find(Dimension);
	return Value ? FMath::Clamp(double(*Value), 0.0, 1.0) : 0.0;
}
