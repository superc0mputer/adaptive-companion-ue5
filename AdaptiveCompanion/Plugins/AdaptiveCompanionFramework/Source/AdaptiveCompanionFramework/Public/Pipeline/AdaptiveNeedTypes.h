#pragma once

#include "CoreMinimal.h"
#include "AdaptiveNeedTypes.generated.h"

/** One feature a need rule reads, optionally negated (1 - value) */
USTRUCT(BlueprintType)
struct ADAPTIVECOMPANIONFRAMEWORK_API FAdaptiveNeedCondition
{
	GENERATED_BODY()

	/** Feature in Frame.Features */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rule")
	FName Feature;

	/** Reads 1 - value, e.g. "not standing still" */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rule")
	bool bNot = false;
};

/**
 *  One rule of a need dimension: fires with Weight × the smallest of its features' values, so it
 *  is only high while all of its features are high. An ambiguous feature (looking around) can be
 *  paired with one that backs it up (standing still).
 */
USTRUCT(BlueprintType)
struct ADAPTIVECOMPANIONFRAMEWORK_API FAdaptiveNeedRule
{
	GENERATED_BODY()

	/** Optional label for the log and the overlay. Without one the features are listed, e.g. "Nav.NoProgress & not Nav.StandingStill" */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rule")
	FName Name;

	/** All of them have to be high for the rule to fire */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rule")
	TArray<FAdaptiveNeedCondition> Features;

	/** The most this rule alone contributes to the dimension */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rule", meta = (ClampMin = "0", ClampMax = "1"))
	double Weight = 1.0;
};

/**
 *  One assistance-need dimension, e.g. "Navigation": its rules, combined by noisy-OR, and the
 *  contexts in which it is evaluated at all.
 */
USTRUCT(BlueprintType)
struct ADAPTIVECOMPANIONFRAMEWORK_API FAdaptiveNeedDimension
{
	GENERATED_BODY()

	/** Name in Frame.Needs, the log and the overlay */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Need")
	FName Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Need", meta = (TitleProperty = "Name"))
	TArray<FAdaptiveNeedRule> Rules;

	/** Evaluated only while all of these contexts are active. Otherwise the dimension is inactive and 0 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context")
	TArray<FName> RequiredContexts;

	/** Evaluated only while the game reports an objective */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context")
	bool bRequiresObjective = false;
};

/** What an estimator says about one dimension in one update */
struct FAdaptiveNeedEstimate
{
	FName Dimension;

	/** How much help the player needs in this dimension, in [0, 1] */
	double Score = 0.0;

	/** False while the dimension does not apply in the current context; Score is 0 then */
	bool bActive = true;

	/** What contributes most to the score, e.g. the strongest rule. Empty if nothing does */
	FString Reason;
};

/** Rule evaluation and combination, separate from the estimator so they can be tested on their own */
namespace AdaptiveNeedMath
{
	/** Weight × the smallest condition value (negated ones as 1 - value). Missing features count as 0. A rule without features is 0 */
	ADAPTIVECOMPANIONFRAMEWORK_API double EvaluateRule(const FAdaptiveNeedRule& Rule, const TMap<FName, double>& Features);

	/** Noisy-OR: 1 - Π(1 - v). One strong value is enough, several moderate ones add up, the result stays in [0, 1] */
	ADAPTIVECOMPANIONFRAMEWORK_API double Combine(TConstArrayView<double> Values);

	/** The rule's Name, or its features joined by " & ", negated ones with "not " */
	ADAPTIVECOMPANIONFRAMEWORK_API FString Describe(const FAdaptiveNeedRule& Rule);
}
