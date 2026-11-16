#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CompanionPathTrail.generated.h"

class UInstancedStaticMeshComponent;

/**
 *  A trail of glowing discs on the floor along a path, shown by the companion's navigation hint (#20).
 *  The discs appear one after another from the start of the path, so the trail shows the direction,
 *  not only the way. Spawned by the Navigation Hint task and destroyed when it ends.
 */
UCLASS()
class ADAPTIVECOMPANION_API ACompanionPathTrail : public AActor
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = Components)
	TObjectPtr<UInstancedStaticMeshComponent> Discs;

public:

	ACompanionPathTrail();

	/** Distance between two discs */
	UPROPERTY(EditAnywhere, Category = "Trail", meta = (ClampMin = 10, Units = "cm"))
	float Spacing = 80.0f;

	/** How fast the trail grows along the path once shown */
	UPROPERTY(EditAnywhere, Category = "Trail", meta = (ClampMin = 1, Units = "cm/s"))
	float RevealSpeed = 2000.0f;

	/** Places discs along the path (navmesh points, e.g. UNavigationPath::PathPoints) up to MaxLength from its start */
	void ShowPath(const TArray<FVector>& PathPoints, float MaxLength);

	/** Number of discs placed by ShowPath, including those not revealed yet */
	int32 GetDiscCount() const { return DiscTransforms.Num(); }

	virtual void Tick(float DeltaSeconds) override;

protected:

	/** Every disc's transform; they are added to the component one by one as the trail grows */
	TArray<FTransform> DiscTransforms;

	float RevealedLength = 0.0f;
};
