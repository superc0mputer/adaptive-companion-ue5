#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "UObject/Interface.h"
#include "AdaptiveCompanionAction.generated.h"

/**
 *  One thing the companion can do to help, e.g. a navigation hint. Describes what the selection
 *  needs to know about the action (which needs it helps with, in which contexts it fits, how costly
 *  and intrusive it is), but not how it works: that is up to the game, which carries it out.
 *
 *  The game creates one asset per action and registers them with the pipeline together with the
 *  object that carries them out (UAdaptivePipelineSubsystem::RegisterActions).
 */
UCLASS(BlueprintType)
class ADAPTIVECOMPANIONFRAMEWORK_API UAdaptiveCompanionAction : public UDataAsset
{
	GENERATED_BODY()

public:

	/** Name in the log, the overlay and OnIntervention, e.g. "Action.NavigationHint" */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Action")
	FName ActionName;

	/** What the action does, for people reading the asset */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Action", meta = (MultiLine = true))
	FText Description;

	/** How much the action helps with each need dimension, 0-1. Dimensions not listed: 0 (R in the utility) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Selection")
	TMap<FName, float> Relevance;

	/** The action is only possible while all of these contexts are active */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Context")
	TArray<FName> RequiredContexts;

	/** The action is not possible while any of these contexts is active */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Context")
	TArray<FName> BlockedContexts;

	/** How well the action fits each context, 0-1; contexts not listed: 1. With several active contexts the lowest counts (S in the utility) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Context")
	TMap<FName, float> Suitability;

	/** Cost of the action, 0-1, used when the game doesn't report a current one (C in the utility) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Selection", meta = (ClampMin = "0", ClampMax = "1"))
	float BaseCost = 0.2f;

	/** How much the action takes over from the player, 0-1, e.g. a short call-out is low, taking over a fight high (I in the utility) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Selection", meta = (ClampMin = "0", ClampMax = "1"))
	float Intrusiveness = 0.3f;

	/** Minimum time between two uses of the action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Selection", meta = (ClampMin = "0", Units = "s"))
	float Cooldown = 10.f;

	/** True if the contexts allow the action. Otherwise Reason says which context is missing or blocks it */
	bool AllowsContexts(const TSet<FName>& ActiveContexts, FString& OutReason) const;

	/** The lowest suitability among the active contexts, 1 if none of them is listed */
	double GetSuitability(const TSet<FName>& ActiveContexts) const;

	/** Relevance for one need dimension, 0 if not listed */
	double GetRelevance(FName Dimension) const;
};

/** The game's answer to "can you do this right now?" */
USTRUCT(BlueprintType)
struct ADAPTIVECOMPANIONFRAMEWORK_API FAdaptiveActionCheck
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Action")
	bool bPossible = true;

	/** Why not, e.g. "no enemy out of view". Shown in the overlay and logged; keep it free of changing numbers */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Action")
	FString Reason;

	/** Current cost, 0-1, e.g. higher when the companion is far away. Negative: use the asset's BaseCost */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Action")
	float Cost = -1.f;
};

UINTERFACE(BlueprintType, MinimalAPI)
class UAdaptiveCompanionActionHandler : public UInterface
{
	GENERATED_BODY()
};

/**
 *  Implemented by the game object that carries out a set of actions, e.g. the companion's AI
 *  controller. The pipeline asks it every update whether each of its actions is possible, and tells
 *  it to carry one out when Selection picked it. The handler then reports the action's phases with
 *  UAdaptivePipelineSubsystem::ReportInterventionPhase (started, then completed, failed or interrupted).
 */
class ADAPTIVECOMPANIONFRAMEWORK_API IAdaptiveCompanionActionHandler
{
	GENERATED_BODY()

public:

	/** Whether the action can be carried out right now, given the game state. Contexts are already checked by the framework */
	UFUNCTION(BlueprintNativeEvent, Category = "Adaptive Companion|Actions")
	FAdaptiveActionCheck CheckAction(const UAdaptiveCompanionAction* Action);

	/** Starts carrying out the action. Returns false if it can't (logged as rejected) */
	UFUNCTION(BlueprintNativeEvent, Category = "Adaptive Companion|Actions")
	bool PerformAction(const UAdaptiveCompanionAction* Action);
};

/** One registered action and the object that carries it out */
USTRUCT()
struct ADAPTIVECOMPANIONFRAMEWORK_API FAdaptiveRegisteredAction
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<const UAdaptiveCompanionAction> Action;

	/** Weak, so a handler that is destroyed without unregistering doesn't stay alive */
	UPROPERTY()
	TWeakObjectPtr<UObject> Handler;
};
