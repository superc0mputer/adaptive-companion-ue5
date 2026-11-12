#include "Pipeline/AdaptiveNeedEstimator.h"
#include "AdaptiveCompanionFramework.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

UAdaptiveRuleNeedEstimator::UAdaptiveRuleNeedEstimator()
{
	// the first loop's need (#6), so a config without rules still drives the naive selection
	FAdaptiveNeedRule DamageRecent;
	DamageRecent.Features.Add({ TEXT("Combat.DamageRecent"), false });

	FAdaptiveNeedDimension Combat;
	Combat.Name = TEXT("Combat");
	Combat.Rules.Add(DamageRecent);
	Dimensions.Add(Combat);
}

void UAdaptiveRuleNeedEstimator::Prepare()
{
	if (bValid.Num() == Dimensions.Num())
	{
		return;
	}

	bValid.Init(false, Dimensions.Num());
	bCheckedFeatures = false;

	TSet<FName> Names;
	for (int32 Index = 0; Index < Dimensions.Num(); ++Index)
	{
		const FAdaptiveNeedDimension& Dimension = Dimensions[Index];

		const TCHAR* Problem = nullptr;
		if (Dimension.Name.IsNone())
		{
			Problem = TEXT("has no name");
		}
		else if (Names.Contains(Dimension.Name))
		{
			Problem = TEXT("has the same name as an earlier dimension");
		}

		if (Problem)
		{
			UE_LOG(LogAdaptiveCompanionFramework, Warning, TEXT("Need dimension %d (%s) %s and is skipped."), Index, *Dimension.Name.ToString(), Problem);
			continue;
		}

		for (int32 RuleIndex = 0; RuleIndex < Dimension.Rules.Num(); ++RuleIndex)
		{
			if (Dimension.Rules[RuleIndex].Features.IsEmpty())
			{
				UE_LOG(LogAdaptiveCompanionFramework, Warning, TEXT("Rule %d of need dimension %s has no features and never fires."), RuleIndex, *Dimension.Name.ToString());
			}
		}

		Names.Add(Dimension.Name);
		bValid[Index] = true;
	}
}

void UAdaptiveRuleNeedEstimator::WarnMissingFeatures(const FAdaptivePipelineFrame& Frame)
{
	if (bCheckedFeatures)
	{
		return;
	}
	bCheckedFeatures = true;

	TSet<FName> Warned;
	for (int32 Index = 0; Index < Dimensions.Num(); ++Index)
	{
		if (!bValid[Index])
		{
			continue;
		}

		for (const FAdaptiveNeedRule& Rule : Dimensions[Index].Rules)
		{
			for (const FAdaptiveNeedCondition& Condition : Rule.Features)
			{
				if (!Frame.Features.Contains(Condition.Feature) && !Warned.Contains(Condition.Feature))
				{
					Warned.Add(Condition.Feature);
					UE_LOG(LogAdaptiveCompanionFramework, Warning, TEXT("Need dimension %s reads feature %s, which the Features stage doesn't compute. It counts as 0."),
						*Dimensions[Index].Name.ToString(), *Condition.Feature.ToString());
				}
			}
		}
	}
}

TSharedPtr<FJsonObject> UAdaptiveRuleNeedEstimator::Describe()
{
	Prepare();

	TArray<TSharedPtr<FJsonValue>> DimensionValues;
	for (int32 Index = 0; Index < Dimensions.Num(); ++Index)
	{
		if (!bValid[Index])
		{
			continue;
		}

		const FAdaptiveNeedDimension& Dimension = Dimensions[Index];

		TArray<TSharedPtr<FJsonValue>> RuleValues;
		for (const FAdaptiveNeedRule& Rule : Dimension.Rules)
		{
			TArray<TSharedPtr<FJsonValue>> FeatureValues;
			for (const FAdaptiveNeedCondition& Condition : Rule.Features)
			{
				FeatureValues.Add(MakeShared<FJsonValueString>(Condition.bNot ? TEXT("not ") + Condition.Feature.ToString() : Condition.Feature.ToString()));
			}

			TSharedRef<FJsonObject> RuleObject = MakeShared<FJsonObject>();
			RuleObject->SetStringField(TEXT("rule"), AdaptiveNeedMath::Describe(Rule));
			RuleObject->SetArrayField(TEXT("features"), FeatureValues);
			RuleObject->SetNumberField(TEXT("weight"), Rule.Weight);
			RuleValues.Add(MakeShared<FJsonValueObject>(RuleObject));
		}

		TArray<TSharedPtr<FJsonValue>> ContextValues;
		for (FName Context : Dimension.RequiredContexts)
		{
			ContextValues.Add(MakeShared<FJsonValueString>(Context.ToString()));
		}

		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("name"), Dimension.Name.ToString());
		Object->SetArrayField(TEXT("rules"), RuleValues);
		Object->SetArrayField(TEXT("required_contexts"), ContextValues);
		Object->SetBoolField(TEXT("requires_objective"), Dimension.bRequiresObjective);
		DimensionValues.Add(MakeShared<FJsonValueObject>(Object));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("dimensions"), DimensionValues);
	return Result;
}

void UAdaptiveRuleNeedEstimator::Estimate(const FAdaptivePipelineFrame& Frame, TArray<FAdaptiveNeedEstimate>& Out)
{
	Prepare();
	WarnMissingFeatures(Frame);

	TArray<double> RuleValues;
	for (int32 Index = 0; Index < Dimensions.Num(); ++Index)
	{
		if (!bValid[Index])
		{
			continue;
		}

		const FAdaptiveNeedDimension& Dimension = Dimensions[Index];
		FAdaptiveNeedEstimate& Estimate = Out.AddDefaulted_GetRef();
		Estimate.Dimension = Dimension.Name;

		Estimate.bActive = (!Dimension.bRequiresObjective || !Frame.Objective.IsNone())
			&& !Dimension.RequiredContexts.ContainsByPredicate([&Frame](FName Context) { return !Frame.Contexts.Contains(Context); });
		if (!Estimate.bActive)
		{
			continue;
		}

		RuleValues.Reset();
		int32 Strongest = INDEX_NONE;
		for (int32 RuleIndex = 0; RuleIndex < Dimension.Rules.Num(); ++RuleIndex)
		{
			const double Value = AdaptiveNeedMath::EvaluateRule(Dimension.Rules[RuleIndex], Frame.Features);
			RuleValues.Add(Value);
			if (Value > 0.0 && (Strongest == INDEX_NONE || Value > RuleValues[Strongest]))
			{
				Strongest = RuleIndex;
			}
		}

		Estimate.Score = AdaptiveNeedMath::Combine(RuleValues);
		if (Strongest != INDEX_NONE)
		{
			Estimate.Reason = AdaptiveNeedMath::Describe(Dimension.Rules[Strongest]);
		}
	}
}
