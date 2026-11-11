#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NavigationObjective.generated.h"

class UStaticMeshComponent;

/**
 *  A place the player has to reach. Objectives in a level are reached in ascending Order;
 *  UNavigationObjectiveSubsystem tracks which one is current. Place it on the floor, so the
 *  navmesh path to it can be found.
 *
 *  While playing, only the current objective is shown: a 3 m pillar, which walls hide, and
 *  optionally a tall beacon above it that is visible over walls.
 */
UCLASS()
class ADAPTIVECOMPANION_API ANavigationObjective : public AActor
{
	GENERATED_BODY()

	/** Pillar that marks the objective in the level until it is reached */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Components, meta = (AllowPrivateAccess = "true"))
	UStaticMeshComponent* Marker;

	/** Thin column above the pillar, shown if bShowBeacon */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Components, meta = (AllowPrivateAccess = "true"))
	UStaticMeshComponent* Beacon;

public:

	ANavigationObjective();

	/** Objectives are reached in ascending order */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Objective")
	int32 Order = 0;

	/** The objective counts as reached once the player is within this distance */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Objective", meta = (ClampMin = "0", Units = "cm"))
	float ReachRadius = 300.0f;

	/** Whether the beacon shows while this objective is current. Without it the player only sees the objective in line of sight */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Objective")
	bool bShowBeacon = true;

	/** Scenario phase of the way to this objective, logged by ANavigationScenario. Never reported to the pipeline */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Objective")
	FName Phase;

	bool IsReached() const { return bReached; }

	/** Marks the objective as reached and hides its marker */
	void SetReached();

	/** Shows the marker (and beacon) only while the objective is the current one */
	void SetCurrent(bool bCurrent);

protected:

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	bool bReached = false;
};
