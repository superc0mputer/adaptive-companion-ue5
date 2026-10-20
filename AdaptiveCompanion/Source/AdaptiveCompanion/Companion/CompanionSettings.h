#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CompanionSettings.generated.h"

class ACompanionCharacter;

/**
 *  Project settings for the prototype's companion (Project Settings > Game > Companion)
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Companion"))
class ADAPTIVECOMPANION_API UCompanionSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:

	UCompanionSettings();

	/** If true, a companion is spawned next to the player in every game level that does not already contain one */
	UPROPERTY(Config, EditAnywhere, Category = "Spawning")
	bool bSpawnCompanion = true;

	/** Companion class to spawn, usually BP_Companion */
	UPROPERTY(Config, EditAnywhere, Category = "Spawning", meta = (EditCondition = "bSpawnCompanion"))
	TSoftClassPtr<ACompanionCharacter> CompanionClass;

	/** Spawn offset relative to the player (X forward, Y right) */
	UPROPERTY(Config, EditAnywhere, Category = "Spawning", meta = (EditCondition = "bSpawnCompanion"))
	FVector SpawnOffset = FVector(-200.0f, 150.0f, 0.0f);

	/** If true, levels whose NavMeshBoundsVolumes don't cover all walkable geometry get an extra volume around it at runtime */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation")
	bool bCoverWalkableGeometry = true;

	/** Extra space added around the walkable geometry for generated navigation bounds */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation", meta = (EditCondition = "bCoverWalkableGeometry", Units = "cm"))
	float GeneratedNavBoundsPadding = 500.0f;

	/** Geometry larger than this half-extent (e.g. sky spheres) is ignored when generating navigation bounds */
	UPROPERTY(Config, EditAnywhere, Category = "Navigation", meta = (EditCondition = "bCoverWalkableGeometry", Units = "cm"))
	float GeneratedNavBoundsMaxHalfExtent = 25000.0f;
};
