#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Pipeline/AdaptiveCompanionAction.h"
#include "Replay/AdaptiveInputRecording.h"
#include "AdaptiveReplayRunner.generated.h"

class UAdaptiveBehaviorStage;
class UAdaptiveContextStage;
class UAdaptivePipelineConfig;
class UAdaptivePipelineStage;
class UAdaptiveTelemetryStage;

/** Stands in for the game during a replay: answers CheckAction and PerformAction as recorded */
UCLASS(Transient)
class ADAPTIVECOMPANIONFRAMEWORK_API UAdaptiveReplayActionHandler : public UObject, public IAdaptiveCompanionActionHandler
{
	GENERATED_BODY()

public:

	TMap<FName, FAdaptiveActionCheck> Answers;

	/** False in an update where the game refused the dispatched intervention */
	bool bAccept = true;

	virtual FAdaptiveActionCheck CheckAction_Implementation(const UAdaptiveCompanionAction* Action) override;
	virtual bool PerformAction_Implementation(const UAdaptiveCompanionAction* Action) override;
};

/** What the pipeline produced in one replayed update */
struct ADAPTIVECOMPANIONFRAMEWORK_API FAdaptiveReplayStep
{
	int64 Index = 0;
	double Time = 0.0;
	TMap<FName, FAdaptiveNeedState> Needs;
	TArray<FName> Contexts;

	/** Selected in this update, NAME_None if nothing was */
	FName Intervention;
	int32 InterventionId = 0;
	FString SelectionReason;

	/** Running before this update's selection */
	FName Running;

	bool operator==(const FAdaptiveReplayStep& Other) const;
};

/**
 *  Runs the pipeline's stages on recorded updates instead of live input (#24). The stages come from
 *  a config asset (normally the current one, so changed rules and weights can be tried on a
 *  recording), without a game instance, a log or the overlay. Updates run at their recorded times
 *  as fast as possible, so the result depends only on the recording and the config.
 *
 *  The game is not simulated: its answers about actions and the intervention phases it reported are
 *  replayed as recorded. They fit as long as the replay selects the same interventions as the
 *  recorded session.
 */
UCLASS(Transient)
class ADAPTIVECOMPANIONFRAMEWORK_API UAdaptiveReplayRunner : public UObject
{
	GENERATED_BODY()

public:

	/** Creates fresh stages from the config's templates; nullptr uses the defaults */
	void Setup(const UAdaptivePipelineConfig* Config);

	/** Runs every update and returns one step per update. Call Setup first; a second Run needs a new Setup */
	TArray<FAdaptiveReplayStep> Run(const TArray<FAdaptiveRecordedUpdate>& Updates);

	/** Problems found while running, e.g. an action asset that couldn't be loaded */
	const TArray<FString>& GetWarnings() const { return Warnings; }

private:

	void Apply(const FAdaptiveRecordedUpdate& Update);

	UPROPERTY()
	TArray<TObjectPtr<UAdaptivePipelineStage>> Stages;

	UPROPERTY()
	TObjectPtr<UAdaptiveTelemetryStage> Telemetry;

	UPROPERTY()
	TObjectPtr<UAdaptiveContextStage> Context;

	UPROPERTY()
	TObjectPtr<UAdaptiveBehaviorStage> Behavior;

	UPROPERTY()
	TObjectPtr<UAdaptiveReplayActionHandler> Handler;

	UPROPERTY()
	TMap<FString, TObjectPtr<UAdaptiveCompanionAction>> LoadedActions;

	TArray<FString> Warnings;
};
