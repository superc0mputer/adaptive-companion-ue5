#include "NavigationTelemetrySubsystem.h"
#include "NavigationTelemetrySettings.h"
#include "NavigationObjective.h"
#include "NavigationObjectiveSubsystem.h"
#include "GameplayContextSettings.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Kismet/GameplayStatics.h"
#include "Pipeline/AdaptivePipelineSubsystem.h"

bool UNavigationTelemetrySubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UNavigationTelemetrySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (UNavigationObjectiveSubsystem* Objectives = Collection.InitializeDependency<UNavigationObjectiveSubsystem>())
	{
		Objectives->OnObjectiveReached.AddUObject(this, &UNavigationTelemetrySubsystem::HandleObjectiveReached);
	}
}

void UNavigationTelemetrySubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	const float Interval = GetDefault<UGameplayContextSettings>()->CheckInterval;
	InWorld.GetTimerManager().SetTimer(UpdateTimer, this, &UNavigationTelemetrySubsystem::UpdateTelemetry, Interval, true, 0.0f);
}

void UNavigationTelemetrySubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(UpdateTimer);
	}

	// the pipeline outlives the level, so don't leave the last level's movement behind
	Report(0.0, 0.0, 0.0);

	Super::Deinitialize();
}

void UNavigationTelemetrySubsystem::UpdateTelemetry()
{
	// no player while respawning; keep the last values until there is one again
	const APawn* Player = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	if (!Player)
	{
		return;
	}

	const FVector Location = Player->GetActorLocation();
	const FVector Velocity = Player->GetVelocity() * FVector(1.0, 1.0, 0.0);
	const double Speed = Velocity.Size();

	UpdateVisitedCells(Location);

	double Distance = 0.0;
	double Alignment = 0.0;

	const UNavigationObjectiveSubsystem* Objectives = GetWorld()->GetSubsystem<UNavigationObjectiveSubsystem>();
	if (const ANavigationObjective* Objective = Objectives ? Objectives->GetCurrentObjective() : nullptr)
	{
		FVector Direction;
		Distance = MeasurePath(Player, Objective->GetActorLocation(), Direction);

		if (Speed >= GetDefault<UNavigationTelemetrySettings>()->MinMovingSpeed)
		{
			Alignment = FVector::DotProduct(Velocity / Speed, Direction);
		}
	}

	Report(Distance, Alignment, Speed);
}

void UNavigationTelemetrySubsystem::UpdateVisitedCells(const FVector& PlayerLocation)
{
	const UNavigationTelemetrySettings* Settings = GetDefault<UNavigationTelemetrySettings>();
	const FIntPoint Cell(FMath::FloorToInt32(PlayerLocation.X / Settings->CellSize), FMath::FloorToInt32(PlayerLocation.Y / Settings->CellSize));
	if (bHasCurrentCell && Cell == CurrentCell)
	{
		return;
	}

	// world time, so pausing doesn't count as being away
	const double Now = GetWorld()->GetTimeSeconds();

	if (bHasCurrentCell)
	{
		CellLeftTimes.Add(CurrentCell, Now);
	}

	const double* LeftTime = CellLeftTimes.Find(Cell);
	if (LeftTime && Now - *LeftTime >= Settings->RevisitMinAbsence)
	{
		if (UAdaptivePipelineSubsystem* Pipeline = UAdaptivePipelineSubsystem::Get(GetWorld()))
		{
			Pipeline->ReportEvent(NavigationTelemetry::AreaRevisited);
		}
	}

	CurrentCell = Cell;
	bHasCurrentCell = true;
}

double UNavigationTelemetrySubsystem::MeasurePath(const APawn* Player, const FVector& Goal, FVector& OutDirection) const
{
	const FVector Start = Player->GetActorLocation();

	// a partial path ends somewhere else, so its length says nothing about the distance to the goal
	const UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(), Start, Goal, const_cast<APawn*>(Player));
	if (Path && Path->IsValid() && !Path->IsPartial())
	{
		// the first point is the start itself and the next one can be right next to it at a corner,
		// so take the direction to the first point that is clearly away from the player
		for (int32 Index = 1; Index < Path->PathPoints.Num(); ++Index)
		{
			const FVector Offset = (Path->PathPoints[Index] - Start) * FVector(1.0, 1.0, 0.0);
			if (Offset.Size() > 50.0 || Index == Path->PathPoints.Num() - 1)
			{
				OutDirection = Offset.GetSafeNormal();
				return Path->GetPathLength();
			}
		}
	}

	OutDirection = ((Goal - Start) * FVector(1.0, 1.0, 0.0)).GetSafeNormal();
	return FVector::Dist(Start, Goal);
}

void UNavigationTelemetrySubsystem::Report(double ObjectiveDistance, double HeadingAlignment, double Speed) const
{
	// values are sampled and only logged on change, so this can run at every check
	if (UAdaptivePipelineSubsystem* Pipeline = UAdaptivePipelineSubsystem::Get(GetWorld()))
	{
		// rounded like the combat values, so tiny changes don't log a new value at every check
		Pipeline->ReportValue(NavigationTelemetry::ObjectiveDistance, FMath::RoundToDouble(ObjectiveDistance / 10.0) / 10.0);
		Pipeline->ReportValue(NavigationTelemetry::HeadingAlignment, FMath::RoundToDouble(HeadingAlignment * 100.0) / 100.0);
		Pipeline->ReportValue(NavigationTelemetry::Speed, FMath::RoundToDouble(Speed / 10.0) / 10.0);
	}
}

void UNavigationTelemetrySubsystem::HandleObjectiveReached(ANavigationObjective* Objective)
{
	// keep the current cell, so standing on the objective doesn't count as arriving somewhere
	CellLeftTimes.Reset();
}
