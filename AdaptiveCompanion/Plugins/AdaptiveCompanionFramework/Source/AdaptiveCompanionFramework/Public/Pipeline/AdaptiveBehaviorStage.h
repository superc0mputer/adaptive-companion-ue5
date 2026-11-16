#pragma once

#include "CoreMinimal.h"
#include "Pipeline/AdaptivePipelineStage.h"
#include "AdaptiveBehaviorStage.generated.h"

/**
 *  6 Behavior execution. Hands the selected intervention to the game through
 *  UAdaptivePipelineSubsystem::OnIntervention, and records what the game reports back about it
 *  (UAdaptivePipelineSubsystem::ReportInterventionPhase). The framework doesn't know what an
 *  intervention does; the game's companion carries it out.
 *
 *  It tracks the dispatched intervention as running until the game reports a final phase (ended,
 *  completed, failed, interrupted) or MaxInterventionDuration passes, so Selection doesn't start a
 *  second one meanwhile (#18). More status handling comes in #19.
 */
UCLASS(meta = (DisplayName = "Behavior Stage"))
class ADAPTIVECOMPANIONFRAMEWORK_API UAdaptiveBehaviorStage : public UAdaptivePipelineStage
{
	GENERATED_BODY()

public:

	/** An intervention whose end the game doesn't report counts as finished after this time, logged as timed_out */
	UPROPERTY(EditAnywhere, Category = "Behavior", meta = (ClampMin = "1", Units = "s"))
	float MaxInterventionDuration = 30.f;

	virtual void Update(FAdaptivePipelineFrame& Frame) override;

	/** The intervention still running at Now, NAME_None if none. Ends it with timed_out after MaxInterventionDuration */
	virtual FName UpdateRunning(double Now);

	/** Logs a phase of an intervention reported by the game, e.g. "started" or "ended" */
	virtual void ReportPhase(FName Intervention, FName Phase);

protected:

	void LogPhase(FName Intervention, const FString& Phase, int32 Receivers = INDEX_NONE) const;

	FName Running;
	double RunningSince = 0.0;
};
