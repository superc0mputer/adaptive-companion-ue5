#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CombatTelemetrySubsystem.generated.h"

namespace CombatTelemetry
{
	/** Number of living enemies within the combat radius */
	inline const FName EnemiesNearby = TEXT("Combat.EnemiesNearby");

	/** Distance to the nearest living enemy in m, capped at the combat radius */
	inline const FName NearestEnemyDistance = TEXT("Combat.NearestEnemyDistance");
}

/**
 *  Reports the threat around the player to the pipeline as values, in every game level without
 *  placing anything in it. Combat events (attacks, hits, kills, deaths) are reported by
 *  ACombatCharacter and ACombatEnemy themselves.
 *
 *  Uses the combat radius and check interval of UGameplayContextSettings, so "nearby" means the
 *  same as for Context.Combat. Both values are reported at every check, also when no enemy is
 *  near, and reset when the level ends.
 */
UCLASS()
class ADAPTIVECOMPANION_API UCombatTelemetrySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:

	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

protected:

	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	void UpdateTelemetry();

	void Report(int32 EnemiesNearby, double NearestEnemyDistance) const;

	FTimerHandle UpdateTimer;
};
