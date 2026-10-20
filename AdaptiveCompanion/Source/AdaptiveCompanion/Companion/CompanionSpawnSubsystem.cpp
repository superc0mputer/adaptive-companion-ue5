#include "CompanionSpawnSubsystem.h"
#include "CompanionCharacter.h"
#include "CompanionSettings.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"
#include "AdaptiveCompanion.h"

bool UCompanionSpawnSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UCompanionSpawnSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (!GetDefault<UCompanionSettings>()->bSpawnCompanion)
	{
		return;
	}

	// a companion placed by hand takes precedence
	if (TActorIterator<ACompanionCharacter>(&InWorld))
	{
		return;
	}

	// the player pawn may not exist yet, so retry until it does
	InWorld.GetTimerManager().SetTimer(SpawnRetryTimer, this, &UCompanionSpawnSubsystem::TrySpawnCompanion, 0.25f, true, 0.0f);
}

void UCompanionSpawnSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SpawnRetryTimer);
	}

	Super::Deinitialize();
}

void UCompanionSpawnSubsystem::TrySpawnCompanion()
{
	UWorld* World = GetWorld();
	const APawn* Player = UGameplayStatics::GetPlayerPawn(World, 0);
	if (!Player)
	{
		return;
	}

	World->GetTimerManager().ClearTimer(SpawnRetryTimer);

	const UCompanionSettings* Settings = GetDefault<UCompanionSettings>();
	UClass* CompanionClass = Settings->CompanionClass.LoadSynchronous();
	if (!CompanionClass)
	{
		UE_LOG(LogAdaptiveCompanion, Warning, TEXT("No companion spawned: set Companion Class in Project Settings > Game > Companion."));
		return;
	}

	const FRotator PlayerYaw(0.0f, Player->GetActorRotation().Yaw, 0.0f);
	const FVector SpawnLocation = Player->GetActorLocation() + PlayerYaw.RotateVector(Settings->SpawnOffset);

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	ACompanionCharacter* Companion = World->SpawnActor<ACompanionCharacter>(CompanionClass, SpawnLocation, PlayerYaw, SpawnParams);
	if (!Companion)
	{
		UE_LOG(LogAdaptiveCompanion, Warning, TEXT("Failed to spawn companion '%s'."), *GetNameSafe(CompanionClass));
		return;
	}

	if (!Companion->GetController())
	{
		Companion->SpawnDefaultController();
	}

	UE_LOG(LogAdaptiveCompanion, Log, TEXT("Spawned companion '%s' next to the player."), *Companion->GetName());
}
