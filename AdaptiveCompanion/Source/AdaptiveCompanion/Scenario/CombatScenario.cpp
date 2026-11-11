#include "CombatScenario.h"
#include "CombatCharacter.h"
#include "CombatEnemy.h"
#include "ScenarioLog.h"
#include "AdaptiveCompanion.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"

namespace CombatScenario
{
	const FName StartPhase = TEXT("Start");
	const FName EndPhase = TEXT("End");

	FCombatScenarioWave MakeWave(const TCHAR* Phase, std::initializer_list<FVector> Locations)
	{
		FCombatScenarioWave Wave;
		Wave.Phase = Phase;
		for (const FVector& Location : Locations)
		{
			// face the arena center, where the player usually is
			const FRotator Facing(0.0, (-Location).Rotation().Yaw, 0.0);
			Wave.SpawnPoints.Add(FTransform(Facing, Location));
		}
		return Wave;
	}
}

ACombatScenario::ACombatScenario()
{
	PrimaryActorTick.bCanEverTick = false;

	// the actor sits on the arena floor, in its center
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	// the player starts 10 m south of the center (-X). Enemies first come from the north, the
	// pressure wave flanks from both sides. Z is the capsule center above the floor
	Waves = {
		CombatScenario::MakeWave(TEXT("WarmUp"), { FVector(1000.0, 0.0, 100.0) }),
		CombatScenario::MakeWave(TEXT("BuildUp"), { FVector(1000.0, -500.0, 100.0), FVector(1000.0, 500.0, 100.0) }),
		CombatScenario::MakeWave(TEXT("Pressure"), {
			FVector(0.0, -1200.0, 100.0), FVector(400.0, -1200.0, 100.0),
			FVector(0.0, 1200.0, 100.0), FVector(400.0, 1200.0, 100.0) }),
	};
}

void ACombatScenario::BeginPlay()
{
	Super::BeginPlay();

	if (!EnemyClass)
	{
		UE_LOG(LogAdaptiveCompanion, Warning, TEXT("%s has no EnemyClass, the scenario won't spawn enemies"), *GetName());
	}

	BindPlayer();

	WaveIndex = INDEX_NONE;
	LogPhase(CombatScenario::StartPhase, TEXT("started"));

	WaveIndex = FMath::Clamp(FirstWave, 0, Waves.Num() - 1);
	GetWorldTimerManager().SetTimer(WaveTimer, this, &ACombatScenario::StartWave, FMath::Max(StartDelay, 0.01f));
}

void ACombatScenario::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(WaveTimer);

	ScenarioLog::ClearOverlay(this, ScenarioName);

	Super::EndPlay(EndPlayReason);
}

void ACombatScenario::BindPlayer()
{
	APlayerController* PlayerController = GetWorld()->GetFirstPlayerController();
	if (!PlayerController)
	{
		// the player logs in later than level actors start in some play modes
		GetWorldTimerManager().SetTimerForNextTick(this, &ACombatScenario::BindPlayer);
		return;
	}

	PlayerController->OnPossessedPawnChanged.AddUniqueDynamic(this, &ACombatScenario::OnPossessedPawnChanged);
	OnPossessedPawnChanged(nullptr, PlayerController->GetPawn());
}

void ACombatScenario::OnPossessedPawnChanged(APawn* OldPawn, APawn* NewPawn)
{
	ACombatCharacter* Character = Cast<ACombatCharacter>(NewPawn);
	if (!Character || Character->OnDied.IsBoundToObject(this))
	{
		return;
	}

	Character->OnDied.AddUObject(this, &ACombatScenario::OnPlayerDied);

	// the player is back after dying: replay the lost wave, or go on with the one that was due
	if (bWaitingForRespawn)
	{
		bWaitingForRespawn = false;
		GetWorldTimerManager().SetTimer(WaveTimer, this, &ACombatScenario::StartWave, FMath::Max(WaveDelay, 0.01f));
	}
}

void ACombatScenario::OnPlayerDied()
{
	if (WaveIndex >= Waves.Num())
	{
		return;
	}

	// nothing may spawn while the player is dead
	GetWorldTimerManager().ClearTimer(WaveTimer);
	bWaitingForRespawn = true;

	// a wave in progress is lost: remove its enemies and play it again from the start
	if (EnemiesLeft > 0)
	{
		LogPhase(GetCurrentPhase(), TEXT("failed"));

		TArray<TWeakObjectPtr<ACombatEnemy>> LostEnemies;
		for (ACombatEnemy* Enemy : WaveEnemies)
		{
			if (IsValid(Enemy))
			{
				Enemy->OnEnemyDied.RemoveDynamic(this, &ACombatScenario::OnEnemyDied);
				LostEnemies.Add(Enemy);
			}
		}
		WaveEnemies.Reset();

		// removed on the next tick: the player usually dies inside an enemy's attack trace,
		// which still iterates over its hits and crashes on enemies destroyed in between
		GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [LostEnemies]()
		{
			for (const TWeakObjectPtr<ACombatEnemy>& Enemy : LostEnemies)
			{
				if (Enemy.IsValid())
				{
					Enemy->Destroy();
				}
			}
		}));
		EnemiesLeft = 0;

		bRestarting = true;
	}
}

void ACombatScenario::StartWave()
{
	if (!Waves.IsValidIndex(WaveIndex))
	{
		return;
	}

	// same seed for every attempt at this wave, so replays start from the same random sequence
	FMath::RandInit(Seed + WaveIndex);
	FMath::SRandInit(Seed + WaveIndex);

	WaveEnemies.Reset();
	if (EnemyClass)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

		for (const FTransform& SpawnPoint : Waves[WaveIndex].SpawnPoints)
		{
			if (ACombatEnemy* Enemy = GetWorld()->SpawnActor<ACombatEnemy>(EnemyClass, SpawnPoint * GetActorTransform(), SpawnParams))
			{
				Enemy->OnEnemyDied.AddDynamic(this, &ACombatScenario::OnEnemyDied);
				WaveEnemies.Add(Enemy);
			}
		}
	}
	EnemiesLeft = WaveEnemies.Num();

	LogPhase(GetCurrentPhase(), bRestarting ? TEXT("restarted") : TEXT("started"));
	bRestarting = false;

	// a wave without enemies counts as cleared right away
	if (EnemiesLeft == 0)
	{
		++EnemiesLeft;
		OnEnemyDied();
	}
}

void ACombatScenario::OnEnemyDied()
{
	if (--EnemiesLeft > 0)
	{
		LogPhase(GetCurrentPhase(), TEXT("enemy_killed"));
		return;
	}

	LogPhase(GetCurrentPhase(), TEXT("cleared"));
	WaveEnemies.Reset();
	++WaveIndex;

	if (WaveIndex < Waves.Num())
	{
		GetWorldTimerManager().SetTimer(WaveTimer, this, &ACombatScenario::StartWave, FMath::Max(WaveDelay, 0.01f));
	}
	else
	{
		LogPhase(CombatScenario::EndPhase, TEXT("started"));
	}
}

FName ACombatScenario::GetCurrentPhase() const
{
	if (WaveIndex == INDEX_NONE)
	{
		return CombatScenario::StartPhase;
	}
	return Waves.IsValidIndex(WaveIndex) ? Waves[WaveIndex].Phase : CombatScenario::EndPhase;
}

void ACombatScenario::LogPhase(FName Phase, const TCHAR* Event) const
{
	const FString OverlayText = EnemiesLeft > 0
		? FString::Printf(TEXT("%s, %d enemies left"), *Phase.ToString(), EnemiesLeft)
		: FString::Printf(TEXT("%s, %s"), *Phase.ToString(), Event);

	ScenarioLog::Write(this, ScenarioName, Phase, Event, OverlayText, [this](FJsonObject& Payload)
	{
		Payload.SetNumberField(TEXT("wave"), WaveIndex);
		Payload.SetNumberField(TEXT("enemies_left"), EnemiesLeft);
	});
}
