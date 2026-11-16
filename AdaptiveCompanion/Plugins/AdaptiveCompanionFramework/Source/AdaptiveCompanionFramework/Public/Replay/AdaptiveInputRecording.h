#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Pipeline/AdaptivePipelineFrame.h"

/** The game's answer about one action in a recorded update, or one registered action */
struct ADAPTIVECOMPANIONFRAMEWORK_API FAdaptiveRecordedAction
{
	FName Name;

	/** Asset path, only for the list of registered actions */
	FString Asset;

	bool bPossible = true;
	FString Reason;

	/** The cost the pipeline used: the game's, or the asset's base cost */
	double Cost = 0.0;
};

/** An intervention phase the game reported */
struct ADAPTIVECOMPANIONFRAMEWORK_API FAdaptiveRecordedPhase
{
	FName Intervention;
	FName Phase;
	FString Detail;
	double Time = 0.0;
};

/**
 *  Everything the game gave the pipeline for one update (#24): the update's time, the events since
 *  the previous update, and what changed in values, contexts, objective, registered actions, the
 *  game's answers about them and the intervention phases it reported. Enough to run the same update
 *  again without the game.
 */
struct ADAPTIVECOMPANIONFRAMEWORK_API FAdaptiveRecordedUpdate
{
	int64 Index = 0;
	double Time = 0.0;

	TArray<FAdaptiveTelemetryEvent> Events;

	/** Values that changed since the previous update, or appeared */
	TMap<FName, double> Values;

	/** All active contexts, set only when they changed */
	TOptional<TArray<FName>> Contexts;

	/** Set only when the objective changed; NAME_None when it was cleared */
	bool bObjectiveChanged = false;
	FName Objective;
	FVector ObjectiveLocation = FVector::ZeroVector;

	/** All registered actions (name and asset), set only when the list changed */
	TOptional<TArray<FAdaptiveRecordedAction>> Registered;

	/** The game's answers that changed. Only actions the game was asked about (contexts allowed them) */
	TArray<FAdaptiveRecordedAction> Answers;

	/** Phases reported between the previous update and this one, applied before it */
	TArray<FAdaptiveRecordedPhase> PhasesBefore;

	/** Phases reported while this update ran (e.g. "started" when the action was handed over), applied after it */
	TArray<FAdaptiveRecordedPhase> PhasesDuring;

	/** The handler refused the intervention dispatched in this update */
	bool bRejected = false;
};

namespace AdaptiveRecording
{
	/** Version of the input record format, in the recording_start record */
	inline constexpr int32 FormatVersion = 1;

	ADAPTIVECOMPANIONFRAMEWORK_API TSharedRef<FJsonObject> ToJson(const FAdaptiveRecordedUpdate& Update);
	ADAPTIVECOMPANIONFRAMEWORK_API bool FromJson(const FJsonObject& Payload, FAdaptiveRecordedUpdate& OutUpdate);
}

/** Turns the pipeline's frames into recorded updates, keeping only what changed since the previous one */
class ADAPTIVECOMPANIONFRAMEWORK_API FAdaptiveInputRecorder
{
public:

	/** The update the frame went through. Call after all stages ran */
	FAdaptiveRecordedUpdate Record(const FAdaptivePipelineFrame& Frame, TArray<FAdaptiveRecordedPhase> PhasesBefore, TArray<FAdaptiveRecordedPhase> PhasesDuring, bool bRejected);

private:

	bool bFirst = true;
	TMap<FName, double> LastValues;
	TArray<FName> LastContexts;
	FName LastObjective;
	FVector LastObjectiveLocation = FVector::ZeroVector;
	TArray<FName> LastRegistered;
	TMap<FName, FAdaptiveRecordedAction> LastAnswers;
};
