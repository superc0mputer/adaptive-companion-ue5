#pragma once

#include "CoreMinimal.h"
#include "Pipeline/AdaptivePipelineStage.h"
#include "AdaptiveNeedStage.generated.h"

class UAdaptiveNeedEstimator;

/**
 *  3 Assistance-need estimation. The Estimator turns the features into one score per dimension
 *  (rule-based by default, #14); the stage writes them to Frame.Needs, logs them and shows them in
 *  the overlay.
 *
 *  Levels are still naive: a dimension is Elevated while its score is at or above Threshold.
 *  Smoothing and proper levels come in #15.
 */
UCLASS(meta = (DisplayName = "Need Stage"))
class ADAPTIVECOMPANIONFRAMEWORK_API UAdaptiveNeedStage : public UAdaptivePipelineStage
{
	GENERATED_BODY()

public:

	UAdaptiveNeedStage();

	/** Computes the scores. Pick another class to swap the estimation method */
	UPROPERTY(EditAnywhere, Instanced, Category = "Need", meta = (NoResetToDefault))
	TObjectPtr<UAdaptiveNeedEstimator> Estimator;

	/** A dimension is Elevated while its score is at or above this */
	UPROPERTY(EditAnywhere, Category = "Need", meta = (ClampMin = "0", ClampMax = "1"))
	double Threshold = 0.6;

	/** A score is logged when its value rounded to this step, its reason or whether it is active changed */
	UPROPERTY(EditAnywhere, Category = "Need", meta = (ClampMin = "0.0001", ClampMax = "1"))
	double LogStep = 0.01;

	/** Logs the estimator's definitions, so every session log says how the needs were computed */
	virtual void Initialize(UAdaptivePipelineSubsystem& InPipeline) override;

	virtual void Update(FAdaptivePipelineFrame& Frame) override;

protected:

	struct FDimensionState
	{
		bool bElevated = false;
		TOptional<double> LoggedScore;
		FString LoggedReason;
		bool bLoggedActive = true;
	};

	TMap<FName, FDimensionState> States;
};
