#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "NavigationTelemetrySubsystem.generated.h"

class ANavigationObjective;

namespace NavigationTelemetry
{
	/** Navmesh path length to the current objective in m, straight-line if no full path is found, 0 without an objective */
	inline const FName ObjectiveDistance = TEXT("Nav.ObjectiveDistance");

	/** Cosine between the horizontal movement direction and the direction of the path to the objective (-1..1), 0 while standing still */
	inline const FName HeadingAlignment = TEXT("Nav.HeadingAlignment");

	/** Horizontal speed of the player in m/s */
	inline const FName Speed = TEXT("Nav.Speed");

	/** The player entered a grid cell they had been in before and had left */
	inline const FName AreaRevisited = TEXT("Nav.AreaRevisited");

	/** The current objective was reached (value = its Order), reported by UNavigationObjectiveSubsystem */
	inline const FName ObjectiveReached = TEXT("Nav.ObjectiveReached");
}

/**
 *  Reports how the player moves relative to the current navigation objective
 *  (UNavigationObjectiveSubsystem) to the pipeline, in every game level without placing anything
 *  in it besides the objectives.
 *
 *  Distance, alignment and speed are reported at every check of UGameplayContextSettings, and
 *  reset to 0 when the level ends. Revisits are tracked on a grid (UNavigationTelemetrySettings);
 *  the visited cells are forgotten when an objective is reached, since the way to the next one
 *  may lead back through known areas.
 */
UCLASS()
class ADAPTIVECOMPANION_API UNavigationTelemetrySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

protected:

	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	void UpdateTelemetry();

	/** Reports Nav.AreaRevisited when the player enters a cell they left long enough ago */
	void UpdateVisitedCells(const FVector& PlayerLocation);

	/**
	 *  Length of the navmesh path between the two points in cm, and the horizontal direction it
	 *  starts in. Straight line and direction if no full path is found (e.g. off the navmesh)
	 */
	double MeasurePath(const APawn* Player, const FVector& Goal, FVector& OutDirection) const;

	void Report(double ObjectiveDistance, double HeadingAlignment, double Speed) const;

	void HandleObjectiveReached(ANavigationObjective* Objective);

	FTimerHandle UpdateTimer;

	/** World time when the player last left each cell they have been in */
	TMap<FIntPoint, double> CellLeftTimes;

	FIntPoint CurrentCell = FIntPoint::ZeroValue;

	bool bHasCurrentCell = false;
};
