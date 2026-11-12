#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Pipeline/AdaptiveNeedTypes.h"
#include "Pipeline/AdaptivePipelineFrame.h"
#include "AdaptiveNeedEstimator.generated.h"

class FJsonObject;

/**
 *  Turns the features of one update into one score per assistance-need dimension. The Need stage
 *  holds one estimator, picked in the config asset, and applies the levels on top of its output,
 *  so estimators can be swapped (rule-based #14, machine learning #43) without touching the rest.
 *
 *  Like the stages, the object in the config asset is a template; every session gets its own copy.
 */
UCLASS(Abstract, EditInlineNew, DefaultToInstanced, CollapseCategories)
class ADAPTIVECOMPANIONFRAMEWORK_API UAdaptiveNeedEstimator : public UObject
{
	GENERATED_BODY()

public:

	/** Called once when the session starts. The returned fields go into the need_definitions record */
	virtual TSharedPtr<FJsonObject> Describe() { return nullptr; }

	/** Fills Out with one estimate per dimension, in the same order every update */
	virtual void Estimate(const FAdaptivePipelineFrame& Frame, TArray<FAdaptiveNeedEstimate>& Out) PURE_VIRTUAL(UAdaptiveNeedEstimator::Estimate, );
};

/**
 *  Rule-based estimator (#14). Each dimension is a list of rules; a rule fires with
 *  Weight × min(its features), and a dimension's score is the noisy-OR of its rules,
 *  1 - Π(1 - rule). A dimension whose contexts are not active is inactive and 0.
 *
 *  The default is the first loop's single dimension, Combat = Combat.DamageRecent, so a config
 *  without rules still works. A game sets its own dimensions in the pipeline config asset.
 */
UCLASS(meta = (DisplayName = "Rule-Based Estimator"))
class ADAPTIVECOMPANIONFRAMEWORK_API UAdaptiveRuleNeedEstimator : public UAdaptiveNeedEstimator
{
	GENERATED_BODY()

public:

	UAdaptiveRuleNeedEstimator();

	/** The dimensions, in the order they are shown in the overlay */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rules", meta = (TitleProperty = "Name"))
	TArray<FAdaptiveNeedDimension> Dimensions;

	virtual TSharedPtr<FJsonObject> Describe() override;
	virtual void Estimate(const FAdaptivePipelineFrame& Frame, TArray<FAdaptiveNeedEstimate>& Out) override;

protected:

	/** Checks the dimensions once and remembers which are valid */
	void Prepare();

	/** Warns once per feature a rule reads that the Features stage doesn't compute (usually a typo) */
	void WarnMissingFeatures(const FAdaptivePipelineFrame& Frame);

	/** Parallel to Dimensions */
	TArray<bool> bValid;

	bool bCheckedFeatures = false;
};
