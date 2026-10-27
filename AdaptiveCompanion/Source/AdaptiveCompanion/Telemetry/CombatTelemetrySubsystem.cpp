#include "CombatTelemetrySubsystem.h"
#include "GameplayContextSettings.h"
#include "CombatEnemy.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"
#include "Pipeline/AdaptivePipelineSubsystem.h"

bool UCombatTelemetrySubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UCombatTelemetrySubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	const float Interval = GetDefault<UGameplayContextSettings>()->CheckInterval;
	InWorld.GetTimerManager().SetTimer(UpdateTimer, this, &UCombatTelemetrySubsystem::UpdateTelemetry, Interval, true, 0.0f);
}

void UCombatTelemetrySubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(UpdateTimer);
	}

	// the pipeline outlives the level, so don't leave the last level's threat behind
	Report(0, GetDefault<UGameplayContextSettings>()->CombatRadius);

	Super::Deinitialize();
}

void UCombatTelemetrySubsystem::UpdateTelemetry()
{
	const float Radius = GetDefault<UGameplayContextSettings>()->CombatRadius;
	int32 EnemiesNearby = 0;
	double NearestDistance = Radius;

	// no player while respawning, which counts as no enemy near
	if (const APawn* Player = UGameplayStatics::GetPlayerPawn(GetWorld(), 0))
	{
		const FVector PlayerLocation = Player->GetActorLocation();

		for (TActorIterator<ACombatEnemy> It(GetWorld()); It; ++It)
		{
			// dead enemies stay in the level as ragdolls for a while
			if (It->CurrentHP <= 0.0f)
			{
				continue;
			}

			const double Distance = FVector::Dist(It->GetActorLocation(), PlayerLocation);
			if (Distance <= Radius)
			{
				++EnemiesNearby;
				NearestDistance = FMath::Min(NearestDistance, Distance);
			}
		}
	}

	Report(EnemiesNearby, NearestDistance);
}

void UCombatTelemetrySubsystem::Report(int32 EnemiesNearby, double NearestEnemyDistance) const
{
	// values are sampled and only logged on change, so this can run at every check
	if (UAdaptivePipelineSubsystem* Pipeline = UAdaptivePipelineSubsystem::Get(GetWorld()))
	{
		Pipeline->ReportValue(CombatTelemetry::EnemiesNearby, EnemiesNearby);

		// rounded to 0.1 m, so walking around doesn't log a new value at every check
		Pipeline->ReportValue(CombatTelemetry::NearestEnemyDistance, FMath::RoundToDouble(NearestEnemyDistance / 10.0) / 10.0);
	}
}
