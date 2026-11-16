#pragma once

#include "CoreMinimal.h"
#include "Pipeline/AdaptivePipelineStage.h"
#include "AdaptiveBehaviorStage.generated.h"

/** The phases of an intervention. The game may report others; these are the ones the framework knows */
namespace AdaptiveInterventionPhase
{
	/** Framework: Selection picked it */
	inline const FName Selected = TEXT("selected");
	/** Framework: handed to the action's handler */
	inline const FName Dispatched = TEXT("dispatched");
	/** Framework: the handler refused to carry it out */
	inline const FName Rejected = TEXT("rejected");
	/** Framework: the game never reported its end */
	inline const FName TimedOut = TEXT("timed_out");

	/** Game: the companion began carrying it out */
	inline const FName Started = TEXT("started");
	/** Game: finished as intended */
	inline const FName Completed = TEXT("completed");
	/** Game: couldn't finish, with a reason */
	inline const FName Failed = TEXT("failed");
	/** Game: stopped before it finished, e.g. the companion lost its pawn */
	inline const FName Interrupted = TEXT("interrupted");

	/** True for the phases that end an intervention (also "ended", used before #19) */
	ADAPTIVECOMPANIONFRAMEWORK_API bool IsFinal(FName Phase);
}

/**
 *  6 Behavior execution. Hands the selected action to the handler that registered it
 *  (IAdaptiveCompanionActionHandler::PerformAction, #19), notifies other listeners through
 *  UAdaptivePipelineSubsystem::OnIntervention, and records the phases the game reports back
 *  (UAdaptivePipelineSubsystem::ReportInterventionPhase). The framework doesn't know what an action
 *  does; the game's companion carries it out.
 *
 *  The dispatched intervention counts as running until the game reports a final phase (completed,
 *  failed, interrupted), the handler rejects it, or MaxInterventionDuration passes, so Selection
 *  doesn't start a second one meanwhile. Final phases are logged with the time since dispatch.
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

	/** Logs a phase of an intervention reported by the game, e.g. "started" or "failed" with a reason in Detail */
	virtual void ReportPhase(FName Intervention, FName Phase, const FString& Detail = FString());

	/** The intervention currently running, NAME_None if none (as of the last update or report) */
	FName GetRunning() const { return Running; }

protected:

	void LogPhase(FName Intervention, FName Phase, const FString& Detail = FString(), int32 Receivers = INDEX_NONE) const;

	FName Running;
	double RunningSince = 0.0;

	/** Session time each intervention was last dispatched, for the duration of its final phase */
	TMap<FName, double> DispatchTimes;
};
