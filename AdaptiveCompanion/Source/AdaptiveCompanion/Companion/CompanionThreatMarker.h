#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CompanionThreatMarker.generated.h"

class UStaticMeshComponent;

/**
 *  A red cone pointing down at the threat the companion called out (#21), attached above the enemy's
 *  head, so once the player turns around they see which enemy was meant. Spawned and destroyed by the
 *  Threat Callout task.
 */
UCLASS()
class ADAPTIVECOMPANION_API ACompanionThreatMarker : public AActor
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = Components)
	TObjectPtr<UStaticMeshComponent> Cone;

public:

	ACompanionThreatMarker();

	/** Attaches the marker this far above the top of the target's collision */
	void AttachAbove(AActor* Target, float Gap = 40.0f);

protected:

	virtual void BeginPlay() override;
};
