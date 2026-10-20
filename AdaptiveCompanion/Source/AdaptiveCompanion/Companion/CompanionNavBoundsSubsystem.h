#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CompanionNavBoundsSubsystem.generated.h"

/**
 *  Spawns a NavMeshBoundsVolume around all walkable geometry when a game level's own volumes don't cover it,
 *  so the companion can follow the player everywhere in the unmodified Epic template levels
 *  (Lvl_ThirdPerson has no volume, Lvl_Combat only one around the enemy arena). Configured in UCompanionSettings.
 *  Relies on RuntimeGeneration=Dynamic for the navmesh (DefaultEngine.ini).
 */
UCLASS()
class ADAPTIVECOMPANION_API UCompanionNavBoundsSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:

	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

protected:

	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
};
