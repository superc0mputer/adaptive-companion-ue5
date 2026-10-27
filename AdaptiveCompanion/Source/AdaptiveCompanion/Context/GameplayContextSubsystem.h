#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayContextSubsystem.generated.h"

namespace GameplayContext
{
	/** A living enemy is near the player, see UGameplayContextSettings */
	inline const FName Combat = TEXT("Context.Combat");

	/** The player moves through the level outside of combat */
	inline const FName Navigation = TEXT("Context.Navigation");
}

/**
 *  Works out what the player is currently doing and reports it to the pipeline as contexts
 *  (UAdaptivePipelineSubsystem::SetContext), in every game level without placing anything in it.
 *
 *  Context.Combat while a living ACombatEnemy is within CombatRadius of the player, until none
 *  has been for CombatLingerTime; Context.Navigation otherwise. Both are deactivated when the
 *  level ends, so the pipeline has no stale context during a level change.
 */
UCLASS()
class ADAPTIVECOMPANION_API UGameplayContextSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:

	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	bool IsInCombat() const { return bInCombat; }

protected:

	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	void UpdateContext();

	/** True if a living enemy is within CombatRadius of the player */
	bool IsEnemyNearPlayer() const;

	void Report(bool bCombat, bool bNavigation) const;

	FTimerHandle UpdateTimer;

	bool bInCombat = false;

	/** World time when an enemy was last near the player */
	double LastEnemyNearTime = 0.0;
};
