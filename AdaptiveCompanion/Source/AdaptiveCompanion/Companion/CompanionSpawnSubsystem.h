#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CompanionSpawnSubsystem.generated.h"

/**
 *  Spawns the companion next to the player when a game level starts, so the Epic template levels
 *  don't need to be modified. Skipped if the level already contains a companion.
 *  Configured in UCompanionSettings.
 */
UCLASS()
class ADAPTIVECOMPANION_API UCompanionSpawnSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:

	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

protected:

	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	/** Spawns the companion once the player has a pawn. Retried by timer until it succeeds */
	void TrySpawnCompanion();

	FTimerHandle SpawnRetryTimer;
};
