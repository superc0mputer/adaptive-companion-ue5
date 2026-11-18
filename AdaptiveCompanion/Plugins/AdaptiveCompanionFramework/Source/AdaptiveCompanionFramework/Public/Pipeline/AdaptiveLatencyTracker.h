#pragma once

#include "CoreMinimal.h"

/**
 *  When each step of one intervention happened, from the need's raw score reaching its threshold
 *  to the first frame on screen that shows the companion's reaction (#25). Session times in
 *  seconds; -1 for a step that didn't happen or couldn't be measured.
 *
 *    Threshold    the raw need score reached the Elevated enter threshold
 *    LevelChange  the smoothed need rose from Normal (the state change)
 *    Selected     the pipeline update that selected the intervention began
 *    Dispatched   the intervention was handed to the game's handler
 *    Started      the game reported "started" (its behavior began, cues spawned)
 *    Presented    the render thread finished the first frame drawn after Started, right before present
 */
struct ADAPTIVECOMPANIONFRAMEWORK_API FAdaptiveLatency
{
	int32 Id = 0;
	FName Action;

	/** The need it was selected for */
	FName Need;

	double Threshold = -1.0;
	double LevelChange = -1.0;
	double Selected = -1.0;
	double Dispatched = -1.0;
	double Started = -1.0;
	double Presented = -1.0;

	/** GFrameCounter at selection and at "started", GFrameCounterRenderThread of the presented frame */
	uint64 SelectedFrame = 0;
	uint64 StartedFrame = 0;
	uint64 PresentedFrame = 0;

	/** Why it was finished before reaching the screen: the final phase it ended with, or "not presented" */
	FString Incomplete;

	/**
	 *  Not the first intervention for its need's current Elevated episode (the need stayed up and it
	 *  was selected again). Threshold and LevelChange then belong to the first one, so the spans that
	 *  start there (selection wait, response, total) don't describe this one's reaction
	 */
	bool bRepeat = false;

	/** To - From, or -1 if either is missing */
	static double Span(double From, double To) { return From >= 0.0 && To >= 0.0 ? To - From : -1.0; }

	/** Raw score at the threshold until the level changed: the smoothing's delay */
	double GetSmoothing() const { return Span(Threshold, LevelChange); }

	/** Level change until selection: 0 if selected in the same update, longer if cooldowns, a running intervention or unavailable actions held it back */
	double GetSelectionWait() const { return Span(LevelChange, Selected); }

	/** The pipeline update's work until the handler was called */
	double GetPipeline() const { return Span(Selected, Dispatched); }

	/** Handler called until the game's behavior began (the StateTree handles the event on its next tick) */
	double GetHandoff() const { return Span(Dispatched, Started); }

	/** Behavior began until the frame showing it was ready to present */
	double GetRender() const { return Span(Started, Presented); }

	/** Selection until on screen: the framework's and the game's reaction time */
	double GetReaction() const { return Span(Selected, Presented); }

	/** State change until on screen: H2's response time */
	double GetResponse() const { return Span(LevelChange, Presented); }

	/** Raw threshold until on screen, including the smoothing */
	double GetTotal() const { return Span(Threshold, Presented); }
};

/**
 *  Collects the timestamps of each intervention as the Behavior stage sees them. Plain C++, so it
 *  can be tested without a game. An intervention is finished once it reached the screen, ended
 *  before it was started (failed, rejected, ...), or waited longer than PresentTimeout for a frame.
 */
class ADAPTIVECOMPANIONFRAMEWORK_API FAdaptiveLatencyTracker
{
public:

	/** A frame that hasn't arrived after this time won't any more (no rendering, window minimized) */
	double PresentTimeout = 1.0;

	/** At dispatch, with everything up to Dispatched filled in */
	void Start(const FAdaptiveLatency& Latency);

	/**
	 *  The game reported "started" for Action. Returns the intervention's id if it now waits for its
	 *  frame (bWaitForPresent), 0 otherwise. Without bWaitForPresent it is finished right away
	 */
	int32 OnStarted(FName Action, double Time, uint64 Frame, bool bWaitForPresent);

	/** The frame drawn after the intervention with Id started is ready to present */
	void OnPresented(int32 Id, double Time, uint64 Frame);

	/** Action ended with a final phase. Finishes it if it never started */
	void OnEnded(FName Action, FName Phase);

	/** Finishes interventions that waited too long for their frame */
	void Update(double Now);

	/** The interventions finished since the last call */
	TArray<FAdaptiveLatency> TakeFinished();

	/** Finishes and returns all, open ones with what was measured (session end) */
	TArray<FAdaptiveLatency> Flush(const FString& Reason);

	const TArray<FAdaptiveLatency>& GetOpen() const { return Open; }

private:

	TArray<FAdaptiveLatency> Open;
	TArray<FAdaptiveLatency> Finished;
};
