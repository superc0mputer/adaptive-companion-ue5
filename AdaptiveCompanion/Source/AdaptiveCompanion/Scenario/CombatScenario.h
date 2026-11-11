#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CombatScenario.generated.h"

class ACombatEnemy;
class APawn;

/** One wave of a combat scenario: enemies that spawn together and have to be cleared */
USTRUCT(BlueprintType)
struct FCombatScenarioWave
{
	GENERATED_BODY()

	/** Phase name written to the session log while this wave runs */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wave")
	FName Phase;

	/** One enemy spawns at each point, relative to the scenario actor */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wave", meta = (MakeEditWidget))
	TArray<FTransform> SpawnPoints;
};

/**
 *  Runs a repeatable combat scenario in a level: a Start countdown, then the waves in order, each
 *  starting WaveDelay after the previous one is cleared, then End. Every phase change is written
 *  to the session log as a "scenario" record. The phases are ground truth for evaluating the need
 *  estimation, so they are never reported to the pipeline.
 *
 *  If the player dies, the current wave's enemies are removed and the wave starts again once the
 *  player has respawned. The global random stream is reseeded at every wave start. Other engine
 *  systems draw from it too, depending on frame timing, so this narrows how differently the
 *  enemies behave from run to run but doesn't make them deterministic.
 */
UCLASS()
class ADAPTIVECOMPANION_API ACombatScenario : public AActor
{
	GENERATED_BODY()

public:

	ACombatScenario();

	/** Name written to every scenario record */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scenario")
	FName ScenarioName = TEXT("Combat");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scenario")
	TSubclassOf<ACombatEnemy> EnemyClass;

	/** Waves in the order they are played */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scenario")
	TArray<FCombatScenarioWave> Waves;

	/** Countdown from the level start to the first wave */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scenario", meta = (ClampMin = "0", Units = "s"))
	float StartDelay = 5.0f;

	/** Pause after a wave is cleared, or after the player respawned, before the next wave starts */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scenario", meta = (ClampMin = "0", Units = "s"))
	float WaveDelay = 4.0f;

	/** Wave i reseeds the random stream with Seed + i */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scenario")
	int32 Seed = 11;

	/** Index of the wave the scenario starts with, to test a later wave without playing the ones before */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scenario|Testing", meta = (ClampMin = "0"))
	int32 FirstWave = 0;

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Binds to the first player controller once it exists, to follow the player's deaths */
	void BindPlayer();

	UFUNCTION()
	void OnPossessedPawnChanged(APawn* OldPawn, APawn* NewPawn);

	void OnPlayerDied();

	void StartWave();

	UFUNCTION()
	void OnEnemyDied();

	/** Writes a scenario record and updates the overlay */
	void LogPhase(FName Phase, const TCHAR* Event) const;

	FName GetCurrentPhase() const;

	/** Wave that runs or starts next; INDEX_NONE during Start, Waves.Num() at the End */
	int32 WaveIndex = INDEX_NONE;

	/** Whether the next StartWave is a repeat of a wave lost by dying */
	bool bRestarting = false;

	bool bWaitingForRespawn = false;

	UPROPERTY()
	TArray<TObjectPtr<ACombatEnemy>> WaveEnemies;

	int32 EnemiesLeft = 0;

	FTimerHandle WaveTimer;
};
