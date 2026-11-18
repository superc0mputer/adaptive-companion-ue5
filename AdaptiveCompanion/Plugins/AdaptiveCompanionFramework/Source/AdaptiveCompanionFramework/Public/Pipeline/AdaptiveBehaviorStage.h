#pragma once

#include "CoreMinimal.h"
#include "Pipeline/AdaptivePipelineStage.h"
#include "Pipeline/AdaptiveLatencyTracker.h"
#include "Pipeline/AdaptiveOutcomeTracker.h"
#include "Pipeline/AdaptivePresentProbe.h"
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
 *
 *  It also measures each intervention's outcome (#23, FAdaptiveOutcomeTracker): the need it was
 *  selected for and the action's outcome features at the selection, at the end and over
 *  OutcomeWindow after the end, logged as an "outcome" record with the intervention's id.
 *
 *  And how long each intervention took to reach the screen (#25, FAdaptiveLatencyTracker): from
 *  the need's raw threshold and level change over selection, dispatch and the game's "started" to
 *  the first frame presented after it (FAdaptivePresentProbe), logged as a "latency" record.
 */
UCLASS(meta = (DisplayName = "Behavior Stage"))
class ADAPTIVECOMPANIONFRAMEWORK_API UAdaptiveBehaviorStage : public UAdaptivePipelineStage
{
	GENERATED_BODY()

public:

	/** An intervention whose end the game doesn't report counts as finished after this time, logged as timed_out */
	UPROPERTY(EditAnywhere, Category = "Behavior", meta = (ClampMin = "1", Units = "s"))
	float MaxInterventionDuration = 30.f;

	/** The outcome's after values are the mean over this time after the intervention ended. Cut short by the next intervention or a context change */
	UPROPERTY(EditAnywhere, Category = "Outcome", meta = (ClampMin = "0.1", Units = "s"))
	float OutcomeWindow = 10.f;

	virtual void Initialize(UAdaptivePipelineSubsystem& InPipeline) override;
	virtual void Deinitialize() override;

	virtual void Update(FAdaptivePipelineFrame& Frame) override;

	/** The intervention still running at Now, NAME_None if none. Ends it with timed_out after MaxInterventionDuration */
	virtual FName UpdateRunning(double Now);

	/** Logs a phase of an intervention reported by the game, e.g. "started" or "failed" with a reason in Detail */
	virtual void ReportPhase(FName Intervention, FName Phase, const FString& Detail = FString());

	/** True if the handler refused the intervention dispatched in the last update */
	bool WasLastDispatchRejected() const { return bLastDispatchRejected; }

	/** The intervention currently running, NAME_None if none (as of the last update or report) */
	FName GetRunning() const { return Running; }

	/** Outcomes still being measured */
	const FAdaptiveOutcomeTracker& GetOutcomes() const { return Outcomes; }

	/** Called for every finished outcome, after it is logged. For the tests and anyone analysing outcomes live */
	TFunction<void(const FAdaptiveOutcome&)> OnOutcome;

	/** Called for every finished latency, after it is logged */
	TFunction<void(const FAdaptiveLatency&)> OnLatency;

	/** Latencies still being measured */
	const FAdaptiveLatencyTracker& GetLatencies() const { return Latencies; }

protected:

	void LogPhase(FName Intervention, FName Phase, const FString& Detail = FString(), int32 Receivers = INDEX_NONE) const;
	void LogOutcome(const FAdaptiveOutcome& Outcome) const;
	void FlushOutcomes();
	void PollPresents();
	void LogLatencies(TArray<FAdaptiveLatency> Finished) const;
	void ShowOutcomes(double Now) const;

	FName Running;
	double RunningSince = 0.0;
	bool bLastDispatchRejected = false;

	/** Session time each intervention was last dispatched, for the duration of its final phase */
	TMap<FName, double> DispatchTimes;

	/** Id of each action's latest intervention, for its phase records */
	TMap<FName, int32> Ids;

	/** Time of the last update, used for reports outside a session (tests) */
	double LastUpdateTime = 0.0;

	/** The session time, or the last update's time outside a session */
	double GetNow() const;

	FAdaptiveOutcomeTracker Outcomes;

	FAdaptiveLatencyTracker Latencies;

	/** Per need, the ElevatedSince of the last intervention selected for it, to tell repeats apart */
	TMap<FName, double> LatencyEpisodes;

	/** Null where nothing is rendered; the latencies then end at "started" */
	TSharedPtr<FAdaptivePresentProbe, ESPMode::ThreadSafe> PresentProbe;
};
