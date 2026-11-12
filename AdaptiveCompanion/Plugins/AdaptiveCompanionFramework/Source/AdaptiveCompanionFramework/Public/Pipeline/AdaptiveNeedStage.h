#pragma once

#include "CoreMinimal.h"
#include "Pipeline/AdaptivePipelineStage.h"
#include "Pipeline/AdaptiveNeedTypes.h"
#include "AdaptiveNeedStage.generated.h"

class UAdaptiveNeedEstimator;

/**
 *  3 Assistance-need estimation. The Estimator turns the features into one raw score per dimension
 *  (rule-based by default, #14). The stage smooths each score over time and maps it to a level
 *  (Normal, Elevated, High) with separate enter and exit thresholds (#15), so the state doesn't
 *  flicker. It writes the result to Frame.Needs, logs it and shows it in the overlay.
 *
 *  Smoothing and levels are applied the same way to every estimator.
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

	/** Smoothing and level thresholds of every dimension without its own entry in DimensionLevels */
	UPROPERTY(EditAnywhere, Category = "Levels")
	FAdaptiveNeedLevelSettings Levels;

	/** Smoothing and level thresholds for single dimensions, by dimension name */
	UPROPERTY(EditAnywhere, Category = "Levels")
	TMap<FName, FAdaptiveNeedLevelSettings> DimensionLevels;

	/** A dimension is logged when its smoothed or raw score rounded to this step, its reason or whether it is active changed */
	UPROPERTY(EditAnywhere, Category = "Need", meta = (ClampMin = "0.0001", ClampMax = "1"))
	double LogStep = 0.01;

	/** Logs the estimator's definitions, so every session log says how the needs were computed */
	virtual void Initialize(UAdaptivePipelineSubsystem& InPipeline) override;

	virtual void Update(FAdaptivePipelineFrame& Frame) override;

	/** The settings that apply to a dimension */
	const FAdaptiveNeedLevelSettings& GetLevelSettings(FName Dimension) const;

protected:

	struct FDimensionState
	{
		double Smoothed = 0.0;
		EAdaptiveNeedLevel Level = EAdaptiveNeedLevel::Normal;
		TOptional<double> LoggedScore;
		TOptional<double> LoggedRaw;
		FString LoggedReason;
		bool bLoggedActive = true;
	};

	TMap<FName, FDimensionState> States;

	/** Frame.Time of the previous update, unset before the first */
	TOptional<double> PreviousTime;
};
