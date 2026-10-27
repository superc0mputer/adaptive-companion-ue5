#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "NavigationObjectiveSubsystem.generated.h"

class ANavigationObjective;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnNavigationObjectiveReached, ANavigationObjective* /*Objective*/);

/**
 *  Tracks which ANavigationObjective the player has to reach next: the unreached one with the
 *  lowest Order. Once the player is within its ReachRadius it counts as reached, which is reported
 *  to the pipeline as Nav.ObjectiveReached (value = its Order) and broadcast in OnObjectiveReached.
 *
 *  Objectives register themselves on BeginPlay, so levels without any simply have no current
 *  objective. Uses the check interval of UGameplayContextSettings.
 */
UCLASS()
class ADAPTIVECOMPANION_API UNavigationObjectiveSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:

	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	void RegisterObjective(ANavigationObjective* Objective);
	void UnregisterObjective(ANavigationObjective* Objective);

	/** The objective the player has to reach next, or nullptr if there is none (left) */
	ANavigationObjective* GetCurrentObjective() const;

	FOnNavigationObjectiveReached OnObjectiveReached;

protected:

	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	void CheckReached();

	UPROPERTY()
	TArray<TObjectPtr<ANavigationObjective>> Objectives;

	FTimerHandle CheckTimer;
};
