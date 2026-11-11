#pragma once

#include "CoreMinimal.h"
#include "Pipeline/AdaptivePipelineStage.h"
#include "AdaptiveContextStage.generated.h"

/**
 *  4 Context evaluation: what the player is currently doing. The game reports it through
 *  UAdaptivePipelineSubsystem::SetContext, e.g. SetContext("Context.Combat", true) while enemies
 *  are near. Contexts are a set of names chosen by the game, so several can be active at once and
 *  the framework needs no game types.
 *
 *  - Changes are logged as context records the moment the game reports them.
 *  - Every update passes the active contexts on in Frame.Contexts, and flags the update in which
 *    the set changed (Frame.bContextChanged).
 *
 *  The game also reports the player's current objective (SetObjective), e.g. the next place to
 *  reach in a navigation level. It is logged as objective records and passed on in Frame.Objective.
 *
 *  Selection doesn't use the context yet; filtering interventions by context comes in #17, #18.
 */
UCLASS(meta = (DisplayName = "Context Stage"))
class ADAPTIVECOMPANIONFRAMEWORK_API UAdaptiveContextStage : public UAdaptivePipelineStage
{
	GENERATED_BODY()

public:

	/** Activates or deactivates a context. Calls that don't change anything are ignored, so the game can call it every frame */
	virtual void SetContext(FName Name, bool bActive);

	bool IsContextActive(FName Name) const { return Active.Contains(Name); }

	/** The active contexts, in the order they became active */
	const TArray<FName>& GetActiveContexts() const { return Active; }

	/** Sets the objective the player currently works toward. Calls that don't change name or location are ignored */
	virtual void SetObjective(FName Name, const FVector& Location);

	/** The player has no objective (any more) */
	virtual void ClearObjective();

	/** NAME_None if there is no objective */
	FName GetObjective() const { return Objective; }
	const FVector& GetObjectiveLocation() const { return ObjectiveLocation; }

	virtual void Update(FAdaptivePipelineFrame& Frame) override;

protected:

	void LogChange(FName Name, bool bActive) const;
	void ShowInOverlay(FName Name, bool bActive) const;

	void LogObjective(FName Name, bool bActive) const;

	TArray<FName> Active;

	FName Objective;
	FVector ObjectiveLocation = FVector::ZeroVector;

	/** The active contexts as passed on in the previous update, to detect changes between updates */
	TArray<FName> PreviousActive;
};
