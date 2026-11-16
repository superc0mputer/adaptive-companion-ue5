#pragma once

#include "CoreMinimal.h"
#include "Pipeline/AdaptivePipelineFrame.h"

/** One value an outcome compares before and after an intervention: a need's smoothed score or a feature */
struct ADAPTIVECOMPANIONFRAMEWORK_API FAdaptiveOutcomeMeasure
{
	/** Need dimension or feature name */
	FName Name;

	/** True for the need the intervention was selected for, false for an outcome feature */
	bool bNeed = false;

	/** Value at the selection; unset if there was no data */
	TOptional<double> Baseline;

	/** Value when the intervention ended */
	TOptional<double> End;

	/** Mean over the window after the end; unset if it was cut short before any update */
	TOptional<double> After;

	/** After − Baseline, negative when it went down (lower is better for every measure). Unset without both */
	TOptional<double> GetChange() const;

	/** End − Baseline: the change while the intervention ran. Set even when the after window was cut short at once */
	TOptional<double> GetEndChange() const;
};

/** What happened after one intervention: its need and outcome features before, at the end and after, and how it ended */
struct ADAPTIVECOMPANIONFRAMEWORK_API FAdaptiveOutcome
{
	/** The intervention's id, the same as on its intervention records */
	int32 Id = 0;

	FName Action;

	/** The need the selection chose it for, NAME_None if unknown */
	FName Need;

	/** Final phase (completed, failed, …) and its detail, as the game reported them. None if the session ended first */
	FName Phase;
	FString Detail;

	/** Session times of the selection and of the final phase (-1 while running) */
	double SelectedTime = 0.0;
	double EndTime = -1.0;

	/** The need first, then the action's outcome features in their order */
	TArray<FAdaptiveOutcomeMeasure> Measures;

	/** The need's level at the selection, at the end, and at the last update measured */
	EAdaptiveNeedLevel BaselineLevel = EAdaptiveNeedLevel::Normal;
	EAdaptiveNeedLevel EndLevel = EAdaptiveNeedLevel::Normal;
	EAdaptiveNeedLevel AfterLevel = EAdaptiveNeedLevel::Normal;

	/** Seconds of the after window actually measured: the full window unless it was cut short */
	double AfterDuration = 0.0;

	/** Why the after window ended early: "next intervention", "context changed", "session ended". Empty if it ran its full length */
	FString CutShort;

	bool HasEnded() const { return EndTime >= 0.0; }

	/** The need was back to Normal at the last update measured */
	bool IsRelieved() const { return AfterLevel == EAdaptiveNeedLevel::Normal; }

	/** The need's measure, nullptr if there is none */
	const FAdaptiveOutcomeMeasure* FindMeasure(FName Name) const;

	// while measuring
	TArray<double> AfterSums;
	TArray<int32> AfterCounts;
	int32 AfterUpdates = 0;
	TSet<FName> EndContexts;
};

/**
 *  Measures what happens after each intervention (#23). The need's score and the features at the
 *  selection are its baseline; they are smoothed and windowed already, so no further averaging is
 *  needed (a mean over the seconds before lagged behind a need that had just risen). When it ends,
 *  the current values are its end values, and the following AfterWindow seconds are averaged into
 *  its after values. The window is cut short when the next intervention is selected, when the
 *  active contexts change, or when the session ends; the outcome is then finished with what was
 *  measured, and says why.
 *
 *  Plain C++ and independent of the session, so the tests can drive it with frames.
 */
class ADAPTIVECOMPANIONFRAMEWORK_API FAdaptiveOutcomeTracker
{
public:

	double AfterWindow = 10.0;

	/** Records this update's needs, features and contexts, advances the outcomes being measured, and returns those that finished */
	TArray<FAdaptiveOutcome> Update(const FAdaptivePipelineFrame& Frame);

	/** Starts an outcome for an intervention selected at Time, with the latest recorded values as its baseline */
	void Start(int32 Id, FName Action, FName Need, const TArray<FName>& Features, double Time);

	/** The intervention ended: takes the latest recorded values as its end values. False if no outcome of that action is running */
	bool End(FName Action, FName Phase, const FString& Detail, double Time);

	/** Finishes every outcome still open, e.g. when the session ends */
	TArray<FAdaptiveOutcome> Flush(const FString& Reason);

	/** Outcomes not finished yet, oldest first */
	const TArray<FAdaptiveOutcome>& GetOpen() const { return Open; }

private:

	struct FSample
	{
		double Time = 0.0;
		TMap<FName, double> Needs;
		TMap<FName, EAdaptiveNeedLevel> Levels;
		TMap<FName, double> Features;
		TSet<FName> Contexts;
	};

	static TOptional<double> ValueOf(const FSample& Sample, const FAdaptiveOutcomeMeasure& Measure);
	static void Finish(FAdaptiveOutcome& Outcome, const FString& CutShort);

	/** The latest update's values */
	TOptional<FSample> Latest;
	TArray<FAdaptiveOutcome> Open;
};
