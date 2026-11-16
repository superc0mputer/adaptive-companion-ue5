#pragma once

#include "CoreMinimal.h"
#include "Pipeline/AdaptivePipelineStage.h"
#include "AdaptiveSelectionStage.generated.h"

/** Weights of the utility's terms */
USTRUCT(BlueprintType)
struct ADAPTIVECOMPANIONFRAMEWORK_API FAdaptiveUtilityWeights
{
	GENERATED_BODY()

	/** w_r: relevance to the elevated need × that need's score */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Utility", meta = (ClampMin = "0"))
	double Relevance = 1.0;

	/** w_s: suitability in the active contexts */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Utility", meta = (ClampMin = "0"))
	double Suitability = 0.3;

	/** w_p: repetition penalty */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Utility", meta = (ClampMin = "0"))
	double Repetition = 0.5;

	/** w_c: current cost */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Utility", meta = (ClampMin = "0"))
	double Cost = 0.3;

	/** w_i: intrusiveness, scaled by (1 - need score) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Utility", meta = (ClampMin = "0"))
	double Intrusiveness = 0.4;
};

/**
 *  5 Intervention selection (#18). Rates every available companion action with the utility from
 *  the exposé,
 *
 *    U = w_r·R + w_s·S − w_p·P − w_c·C − w_i·I·(1 − n)
 *
 *  where R is the action's relevance to an elevated need times that need's smoothed score (the best
 *  need counts), n that score, S the suitability, C the current cost and I the intrusiveness of the
 *  action (#17), and P a repetition penalty that halves every RepetitionHalfLife seconds since the
 *  action was last selected. Candidates must be available, off their cooldown and relevant to an
 *  elevated need. The best candidate is selected if its utility reaches MinUtility; otherwise, and
 *  while no need is elevated, an intervention is running or the last one was less than
 *  MinTimeBetweenInterventions ago, the companion keeps its default behavior.
 */
UCLASS(meta = (DisplayName = "Selection Stage"))
class ADAPTIVECOMPANIONFRAMEWORK_API UAdaptiveSelectionStage : public UAdaptivePipelineStage
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, Category = "Selection")
	FAdaptiveUtilityWeights Weights;

	/** The best candidate is only selected at or above this utility; below it the companion does nothing */
	UPROPERTY(EditAnywhere, Category = "Selection")
	double MinUtility = 0.5;

	/** Minimum time between two interventions of any kind (frequency limit) */
	UPROPERTY(EditAnywhere, Category = "Selection", meta = (ClampMin = "0", Units = "s"))
	float MinTimeBetweenInterventions = 5.f;

	/** The repetition penalty of an action starts at 1 when it is selected and halves after this time */
	UPROPERTY(EditAnywhere, Category = "Selection", meta = (ClampMin = "0.1", Units = "s"))
	float RepetitionHalfLife = 30.f;

	virtual void Update(FAdaptivePipelineFrame& Frame) override;

protected:

	void Evaluate(FAdaptivePipelineFrame& Frame) const;
	void Decide(FAdaptivePipelineFrame& Frame);
	void Log(const FAdaptivePipelineFrame& Frame, const FString& Decision) const;
	void ShowInOverlay(const FAdaptivePipelineFrame& Frame, const FString& Decision) const;

	/** Session time when each action was last selected */
	TMap<FName, double> LastSelected;

	/** Session time of the last intervention of any kind */
	double LastInterventionTime = -UE_BIG_NUMBER;

	/** Decision and reason last logged, so "do nothing" is only logged when its reason changes */
	FString LoggedDecision;
	FString LoggedReason;
};
