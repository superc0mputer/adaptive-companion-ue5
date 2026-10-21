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
};

/**
 *  Placeholder for interventions, the same for every intervention: the companion runs to the player
 *  and shows a text bark above its head. Succeeds once it has arrived and Duration has passed.
 *  Reports "started" and "ended" to the pipeline, so the session log shows the reaction.
 *  The bark is debug text, so it doesn't show in Shipping builds.
 *  Replaced by real behaviors in #24.
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
