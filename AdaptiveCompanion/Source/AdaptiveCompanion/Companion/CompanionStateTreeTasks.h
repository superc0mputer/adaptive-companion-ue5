#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "StateTreeConditionBase.h"
#include "CompanionStateTreeTasks.generated.h"

class AAIController;

/**
 *  Instance data struct for the Follow Player task
 */
USTRUCT()
struct FCompanionFollowPlayerInstanceData
{
	GENERATED_BODY()

	/** AI Controller of the companion */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AAIController> Controller;

	/** Distance the companion tries to keep to the player */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = 0, Units = "cm"))
	float FollowDistance = 250.0f;

	/** The companion only starts moving again once the player is further away than this (avoids jittering) */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = 0, Units = "cm"))
	float StartMovingDistance = 400.0f;

	/** Beyond this distance the companion runs instead of walking */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = 0, Units = "cm"))
	float RunDistance = 900.0f;

	/** Speed used while running to catch up */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = 0, Units = "cm/s"))
	float RunSpeed = 750.0f;

	/** If the companion wants to move but makes no progress for this long, the task fails so the tree can reposition it */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = 0, Units = "s"))
	float StuckTime = 2.0f;

	/** Time the companion has wanted to move without actually moving */
	float TimeWithoutProgress = 0.0f;
};

/**
 *  StateTree task that keeps the companion at a distance to the player. Runs until the companion gets stuck.
 */
USTRUCT(meta = (DisplayName = "Follow Player", Category = "Companion"))
struct FCompanionFollowPlayerTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	FCompanionFollowPlayerTask()
	{
		bShouldStateChangeOnReselect = false;
	}

	using FInstanceDataType = FCompanionFollowPlayerInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif // WITH_EDITOR
};

////////////////////////////////////////////////////////////////////

/**
 *  Instance data struct for the Reposition Near Player task
 */
USTRUCT()
struct FCompanionRepositionInstanceData
{
	GENERATED_BODY()

	/** AI Controller of the companion */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AAIController> Controller;

	/** How far behind the player the companion is placed */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = 0, Units = "cm"))
	float DistanceBehindPlayer = 250.0f;

	/** How far a candidate spot may be moved to land on the navmesh */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = 0, Units = "cm"))
	float SearchRadius = 400.0f;

	/** If true, only spots the player can't see are used */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	bool bOnlyHiddenSpots = true;

	/** Extra angle added to the camera's field of view, so spots right at the screen edge also count as visible */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = 0, Units = "degrees", EditCondition = "bOnlyHiddenSpots"))
	float ViewMarginAngle = 10.0f;
};

/**
 *  StateTree task that teleports the companion to a reachable spot behind the player.
 *  Candidate spots are tried from directly behind the player outwards to its sides. With bOnlyHiddenSpots the companion
 *  never appears in view; combine with the Companion Hidden From Player condition so it also never vanishes in view.
 *  Fails if no spot is usable.
 */
USTRUCT(meta = (DisplayName = "Reposition Near Player", Category = "Companion"))
struct FCompanionRepositionTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	FCompanionRepositionTask()
	{
		bShouldCallTick = false;
	}

	using FInstanceDataType = FCompanionRepositionInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif // WITH_EDITOR
};

////////////////////////////////////////////////////////////////////

/**
 *  Instance data struct for the Intervention Placeholder task
 */
USTRUCT()
struct FCompanionInterventionPlaceholderInstanceData
{
	GENERATED_BODY()

	/** AI Controller of the companion */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AAIController> Controller;

	/** Minimum time the intervention lasts, even if the companion is already next to the player */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = 0, Units = "s"))
	float Duration = 2.0f;

	/** The intervention ends after this long even if the companion hasn't reached the player */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = 0, Units = "s"))
	float MaxDuration = 6.0f;

	/** Speed while running to the player */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = 0, Units = "cm/s"))
	float RunSpeed = 750.0f;

	/** How close the companion gets to the player */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = 0, Units = "cm"))
	float AcceptanceRadius = 150.0f;

	/** Text shown above the companion's head */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	FString Bark = TEXT("Hang in there, I'm coming!");

	/** How long the bark is shown */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = 0, Units = "s"))
	float BarkDuration = 3.0f;

	/** Time spent in the intervention so far */
	float ElapsedTime = 0.0f;

	/** The intervention being carried out, reported back to the pipeline */
	FName Intervention;

	/** How the task ended, reported in ExitState: completed, failed, or interrupted if the state was left while it still ran */
	FName Outcome;
	FString FailReason;
};

/**
 *  Placeholder for an action's behavior: the companion runs to the player and shows a text bark
 *  above its head. Succeeds once it has arrived and Duration has passed; fails if it hasn't
 *  arrived after MaxDuration. Reports "started" and then "completed", "failed" or "interrupted"
 *  (the state was left while it still ran) to the pipeline.
 *  The bark is debug text, so it doesn't show in Shipping builds.
 *  New actions get it from the commandlet until their behavior exists; since #22 every action of this game has one.
 */
USTRUCT(meta = (DisplayName = "Intervention Placeholder", Category = "Companion"))
struct FCompanionInterventionPlaceholderTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FCompanionInterventionPlaceholderInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif // WITH_EDITOR
};

////////////////////////////////////////////////////////////////////

class ACompanionPathTrail;

/**
 *  Instance data struct for the Navigation Hint task
 */
USTRUCT()
struct FCompanionNavigationHintInstanceData
{
	GENERATED_BODY()

	/** AI Controller of the companion */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AAIController> Controller;

	/** How far along the path the companion runs ahead of the player */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = 0, Units = "cm"))
	float LeadDistance = 1000.0f;

	/** How much of the path the floor trail shows, from the player on */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = 0, Units = "cm"))
	float TrailLength = 2000.0f;

	/** Speed while running ahead */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = 0, Units = "cm/s"))
	float RunSpeed = 600.0f;

	/** The player counts as following once their path to the objective got this much shorter */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = 0, Units = "cm"))
	float FollowDistance = 500.0f;

	/** The hint stays at least this long, so the player has time to see it */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = 0, Units = "s"))
	float MinDuration = 3.0f;

	/** The hint ends after this long even if the player doesn't follow */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = 0, Units = "s"))
	float MaxDuration = 15.0f;

	/** How often the player's path to the objective is measured */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = 0.1, Units = "s"))
	float ProgressCheckInterval = 0.5f;

	/** Text shown above the companion's head */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	FString Bark = TEXT("This way!");

	/** How long the bark is shown */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = 0, Units = "s"))
	float BarkDuration = 3.0f;

	float ElapsedTime = 0.0f;
	float TimeSinceProgressCheck = 0.0f;

	FName Intervention;
	FName Objective;
	FVector ObjectiveLocation = FVector::ZeroVector;

	/** Length of the player's path to the objective when the hint started */
	float StartPathLength = 0.0f;

	/** Further along the path than the companion's spot, so it faces the way on */
	FVector LookTarget = FVector::ZeroVector;

	TWeakObjectPtr<ACompanionPathTrail> Trail;

	/** How the task ended, reported in ExitState: completed, failed, or interrupted if the state was left while it still ran */
	FName Outcome;
	FString Detail;
};

/**
 *  Action.NavigationHint (#20): shows the player the way to the current objective. A trail of discs
 *  appears on the floor along the navmesh path (the first TrailLength of it), and the companion
 *  runs LeadDistance ahead along the same path, then turns to face the way on.
 *
 *  Completes after MinDuration once the player followed (their path to the objective got
 *  FollowDistance shorter) or the objective was reached; after MaxDuration it completes anyway.
 *  The detail says which. Fails if there is no objective or no full path to it. Reports "started"
 *  and the final phase (or "interrupted") to the pipeline like the placeholder.
 */
USTRUCT(meta = (DisplayName = "Navigation Hint", Category = "Companion"))
struct FCompanionNavigationHintTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FCompanionNavigationHintInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif // WITH_EDITOR
};

////////////////////////////////////////////////////////////////////

class ACombatEnemy;
class ACompanionThreatMarker;
class SWidget;
class UGameViewportClient;

/**
 *  Instance data struct for the Threat Callout task
 */
USTRUCT()
struct FCompanionThreatCalloutInstanceData
{
	GENERATED_BODY()

	/** AI Controller of the companion */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AAIController> Controller;

	/** The call-out ends after this long if the player doesn't look */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = 0, Units = "s"))
	float MaxDuration = 5.0f;

	/** How long the bark above the companion is shown */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = 0, Units = "s"))
	float BarkDuration = 2.5f;

	float ElapsedTime = 0.0f;

	FName Intervention;

	TWeakObjectPtr<ACombatEnemy> Threat;
	TWeakObjectPtr<ACompanionThreatMarker> Marker;
	TWeakObjectPtr<UGameViewportClient> Viewport;
	TSharedPtr<SWidget> Indicator;

	/** How the task ended, reported in ExitState: completed, failed, or interrupted if the state was left while it still ran */
	FName Outcome;
	FString Detail;
};

/**
 *  Action.ThreatCallout (#21): warns the player about the nearest living enemy within the combat
 *  radius that is outside the camera's view (the threat need's "close and unseen"). Three cues: a
 *  pulsing red arrow at the screen edge pointing at it with "Behind you!" / "Left!" / "Right!"
 *  (SCompanionThreatIndicator), a red cone above the enemy (ACompanionThreatMarker), and the
 *  companion stopping and turning to face it with the same words as a bark.
 *
 *  Completes as soon as the enemy is in view ("threat in view", whether the player turned or the enemy walked in), when it dies or leaves the combat
 *  radius ("threat gone"), or after MaxDuration ("player didn't look"). Fails if no enemy is out of
 *  view at the start. Reports "started" and the final phase (or "interrupted") to the pipeline.
 */
USTRUCT(meta = (DisplayName = "Threat Callout", Category = "Companion"))
struct FCompanionThreatCalloutTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FCompanionThreatCalloutInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif // WITH_EDITOR
};

////////////////////////////////////////////////////////////////////

/**
 *  Instance data struct for the Draw Aggro task
 */
USTRUCT()
struct FCompanionDrawAggroInstanceData
{
	GENERATED_BODY()

	/** AI Controller of the companion */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AAIController> Controller;

	/** How long the drawn enemies go after the companion instead of the player */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = 0, Units = "s"))
	float TauntDuration = 12.0f;

	/** At most this many enemies are drawn, so the player still has a fight */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = 1))
	int32 MaxEnemies = 2;

	/** Only enemies within this distance of the companion are drawn */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = 0, Units = "cm"))
	float TauntRadius = 1200.0f;

	/** After taunting, the companion moves this far from the player, so the drawn enemies follow it away */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = 0, Units = "cm"))
	float LeadAwayDistance = 700.0f;

	/** The companion taunts once it is this close to the first enemy */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = 0, Units = "cm"))
	float AcceptanceRadius = 300.0f;

	/** Fails if the companion hasn't reached an enemy after this long */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = 0, Units = "s"))
	float ReachTime = 5.0f;

	/** Speed while running to the enemies */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = 0, Units = "cm/s"))
	float RunSpeed = 750.0f;

	/** Text shown above the companion's head when it taunts */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	FString Bark = TEXT("Over here!");

	float ElapsedTime = 0.0f;
	float TauntTime = 0.0f;
	bool bTaunting = false;

	FName Intervention;

	/** The enemy the companion runs to */
	TWeakObjectPtr<ACombatEnemy> FirstEnemy;

	/** The enemies taunted, released in ExitState */
	TArray<TWeakObjectPtr<ACombatEnemy>> Drawn;

	/** Living enemies near the player when the taunt started */
	int32 EnemiesNearby = 0;

	/** How the task ended, reported in ExitState: completed, failed, or interrupted if the state was left while it still ran */
	FName Outcome;
	FString Detail;
};

/**
 *  Action.DrawAggro (#22): takes pressure off the player in a fight. The companion runs to the
 *  nearest enemy near the player, then taunts up to MaxEnemies enemies near itself ("Over here!"):
 *  for TauntDuration they go after the companion instead of the player (ACombatEnemy::SetTauntTarget),
 *  then they are sent back. Right after taunting it moves LeadAwayDistance away from the player, so
 *  the drawn enemies leave the player's side instead of fighting the companion next to it. Enemies only damage the player, so the companion takes no damage.
 *
 *  Completes after TauntDuration ("drew 2 of 4 enemies"), when all drawn enemies are dead
 *  ("enemies defeated") or when the player died. Fails if no enemy is near the player or the
 *  companion can't reach one within ReachTime. The taunt is cleared in every case, so the enemies
 *  return to the player right away. Reports "started" and the final phase (or "interrupted").
 */
USTRUCT(meta = (DisplayName = "Draw Aggro", Category = "Companion"))
struct FCompanionDrawAggroTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FCompanionDrawAggroInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif // WITH_EDITOR
};

////////////////////////////////////////////////////////////////////

/**
 *  Instance data struct for the Player Distance condition
 */
USTRUCT()
struct FCompanionPlayerDistanceConditionInstanceData
{
	GENERATED_BODY()

	/** AI Controller of the companion */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AAIController> Controller;

	/** Distance to compare against */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = 0, Units = "cm"))
	float Distance = 2000.0f;
};

/**
 *  StateTree condition that passes if the player is further away from the companion than Distance.
 *  Fails if there is no player pawn.
 */
USTRUCT(DisplayName = "Player Further Than", meta = (Category = "Companion"))
struct FCompanionPlayerDistanceCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FCompanionPlayerDistanceConditionInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif // WITH_EDITOR
};

////////////////////////////////////////////////////////////////////

/**
 *  Instance data struct for the Companion Hidden From Player condition
 */
USTRUCT()
struct FCompanionHiddenFromPlayerConditionInstanceData
{
	GENERATED_BODY()

	/** AI Controller of the companion */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AAIController> Controller;

	/** Extra angle added to the camera's field of view, so a companion right at the screen edge also counts as visible */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = 0, Units = "degrees"))
	float ViewMarginAngle = 10.0f;
};

/**
 *  StateTree condition that passes if the player's camera can't see the companion.
 *  Used to keep tricks like teleporting out of sight, as Elizabeth does in BioShock Infinite.
 */
USTRUCT(DisplayName = "Companion Hidden From Player", meta = (Category = "Companion"))
struct FCompanionHiddenFromPlayerCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FCompanionHiddenFromPlayerConditionInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif // WITH_EDITOR
};

////////////////////////////////////////////////////////////////////

/**
 *  Instance data struct for the Is Current Intervention condition
 */
USTRUCT()
struct FCompanionIsCurrentInterventionConditionInstanceData
{
	GENERATED_BODY()

	/** AI Controller of the companion */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AAIController> Controller;

	/** The action this state carries out, e.g. Action.NavigationHint */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	FName Intervention;
};

/**
 *  StateTree condition that passes if the companion was told to carry out this intervention
 *  (ACompanionAIController::GetCurrentIntervention). Picks the child of the Intervention branch.
 */
USTRUCT(DisplayName = "Is Current Intervention", meta = (Category = "Companion"))
struct FCompanionIsCurrentInterventionCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FCompanionIsCurrentInterventionConditionInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif // WITH_EDITOR
};

////////////////////////////////////////////////////////////////////

/**
 *  Instance data struct for the Report Intervention Failed task
 */
USTRUCT()
struct FCompanionReportFailedInstanceData
{
	GENERATED_BODY()

	/** AI Controller of the companion */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AAIController> Controller;

	/** Why it failed, logged with the failed phase */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	FString Reason = TEXT("the companion has no state for this action");
};

/**
 *  StateTree task that reports the current intervention as failed and fails right away. Used by the
 *  last child of the Intervention branch, which catches actions without a state of their own.
 */
USTRUCT(meta = (DisplayName = "Report Intervention Failed", Category = "Companion"))
struct FCompanionReportFailedTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FCompanionReportFailedInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif // WITH_EDITOR
};
