#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NavigationObjective.generated.h"

class UStaticMeshComponent;

/**
 *  A place the player has to reach. Objectives in a level are reached in ascending Order;
 *  UNavigationObjectiveSubsystem tracks which one is current. Place it on the floor, so the
 *  navmesh path to it can be found.
 */
UCLASS()
class ADAPTIVECOMPANION_API ANavigationObjective : public AActor
{
	GENERATED_BODY()

	/** Pillar that marks the objective in the level until it is reached */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Components, meta = (AllowPrivateAccess = "true"))
	UStaticMeshComponent* Marker;

public:

	ANavigationObjective();

	/** Objectives are reached in ascending order */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Objective")
	int32 Order = 0;

	/** The objective counts as reached once the player is within this distance */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Objective", meta = (ClampMin = "0", Units = "cm"))
	float ReachRadius = 300.0f;

	bool IsReached() const { return bReached; }

	/** Marks the objective as reached and hides its marker */
	void SetReached();

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	bool bReached = false;
};
