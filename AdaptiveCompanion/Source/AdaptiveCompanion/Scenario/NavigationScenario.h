#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NavigationScenario.generated.h"

class ANavigationObjective;

/**
 *  Logs the phases of a navigation scenario: Start when the level starts, then one phase per
 *  ANavigationObjective (its Phase, "started" when it becomes current and "reached" when the
 *  player gets there), then End once the last one is reached. The objectives themselves and which
 *  one is current are handled by UNavigationObjectiveSubsystem; place this actor once in a level
 *  with objectives. Like all scenario phases, these are only logged, never reported to the pipeline.
 */
UCLASS()
class ADAPTIVECOMPANION_API ANavigationScenario : public AActor
{
	GENERATED_BODY()

public:

	ANavigationScenario();

	/** Name written to every scenario record */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scenario")
	FName ScenarioName = TEXT("Navigation");

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Runs one tick after BeginPlay, once all objectives have registered */
	void StartScenario();

	void OnObjectiveReached(ANavigationObjective* Objective);

	/** Logs the current objective's phase as started, or End if there is none left */
	void StartCurrentLeg();

	void LogPhase(FName Phase, const TCHAR* Event, const ANavigationObjective* Objective) const;

	FDelegateHandle ReachedHandle;
};
