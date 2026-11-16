#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Replay/AdaptiveReplayRunner.h"

/** One labeled interval of a recording: the needs that should be Elevated or High in it */
struct ADAPTIVECOMPANIONFRAMEWORK_API FAdaptiveReplayLabel
{
	double From = 0.0;
	double To = 0.0;
	TArray<FName> Needs;

	/** Optional: actions that would fit; the result says whether one of them was selected in the interval */
	TArray<FName> Actions;

	FString Note;
};

/**
 *  What a recording should show, written by hand or by the script that played it: labeled
 *  intervals with their needs. Outside every interval, and for needs not listed, every need should
 *  be Normal. Updates within Tolerance seconds after a label starts or ends aren't counted, since
 *  the smoothed needs take a moment to follow.
 *
 *  {"tolerance": 2, "labels": [{"from": 5, "to": 21, "needs": ["Disorientation"], "actions": ["Action.NavigationHint"], "note": "standing, looking around"}]}
 */
struct ADAPTIVECOMPANIONFRAMEWORK_API FAdaptiveReplayLabels
{
	double Tolerance = 0.0;
	TArray<FAdaptiveReplayLabel> Labels;

	static bool LoadFromString(const FString& Json, FAdaptiveReplayLabels& OutLabels, FString& OutError);
	static bool LoadFromFile(const FString& Path, FAdaptiveReplayLabels& OutLabels, FString& OutError);

	/** The needs expected at Time; false if Time is within the tolerance after a boundary */
	bool GetExpected(double Time, TSet<FName>& OutNeeds) const;
};

/** Per-update agreement of one need with the labels: elevated (Elevated or High) vs expected */
struct ADAPTIVECOMPANIONFRAMEWORK_API FAdaptiveNeedConfusion
{
	int32 TruePositives = 0;
	int32 FalsePositives = 0;
	int32 FalseNegatives = 0;
	int32 TrueNegatives = 0;

	/** Unset when undefined (nothing predicted, nothing expected) */
	TOptional<double> Precision() const;
	TOptional<double> Recall() const;
	TOptional<double> F1() const;
};

/** How one labeled interval went */
struct ADAPTIVECOMPANIONFRAMEWORK_API FAdaptiveLabelResult
{
	FAdaptiveReplayLabel Label;

	/** Per labeled need: seconds from the interval's start until it was Elevated; unset if it never was */
	TMap<FName, TOptional<double>> Delays;

	/** Interventions selected within the interval, in order */
	TArray<FName> Selected;

	/** One of the label's actions was selected; unset if the label lists none */
	TOptional<bool> bFittingAction;
};

/** The replay compared with the labels */
struct ADAPTIVECOMPANIONFRAMEWORK_API FAdaptiveReplayEvaluation
{
	TMap<FName, FAdaptiveNeedConfusion> Needs;
	TArray<FAdaptiveLabelResult> Labels;
	int32 CountedUpdates = 0;
	int32 SkippedUpdates = 0;

	static FAdaptiveReplayEvaluation Evaluate(const TArray<FAdaptiveReplayStep>& Steps, const FAdaptiveReplayLabels& Labels);

	TSharedRef<FJsonObject> ToJson() const;
	FString ToText() const;
};

/** What the live session logged, to compare the replay with */
struct ADAPTIVECOMPANIONFRAMEWORK_API FAdaptiveLiveRecord
{
	double Time = 0.0;

	/** Need for a level change, intervention for a selection */
	FName Name;

	/** Level for a level change, empty for a selection */
	FString Value;
};

/** A session log read for replaying: its recorded updates and what the live pipeline did */
struct ADAPTIVECOMPANIONFRAMEWORK_API FAdaptiveSessionRecording
{
	FString SessionId;
	FString Config;
	TArray<FAdaptiveRecordedUpdate> Updates;
	TArray<FAdaptiveLiveRecord> LiveLevelChanges;
	TArray<FAdaptiveLiveRecord> LiveSelections;

	/** False if the file can't be read or has no input records */
	static bool LoadSessionLog(const FString& Path, FAdaptiveSessionRecording& OutRecording, FString& OutError);
};

/** Whether the replay did what the live session did: the same level changes and selections, at most one update apart */
struct ADAPTIVECOMPANIONFRAMEWORK_API FAdaptiveLiveComparison
{
	int32 LiveLevelChanges = 0;
	int32 ReplayLevelChanges = 0;
	int32 MatchedLevelChanges = 0;
	int32 LiveSelections = 0;
	int32 ReplaySelections = 0;
	int32 MatchedSelections = 0;

	/** The first differences, for the report */
	TArray<FString> Differences;

	bool Matches() const;

	static FAdaptiveLiveComparison Compare(const TArray<FAdaptiveReplayStep>& Steps, const FAdaptiveSessionRecording& Recording, double MaxOffset);

	TSharedRef<FJsonObject> ToJson() const;
};
