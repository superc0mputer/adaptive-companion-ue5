#include "GameplayContextSubsystem.h"
#include "GameplayContextSettings.h"
#include "CombatEnemy.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"
#include "Pipeline/AdaptivePipelineSubsystem.h"

bool UGameplayContextSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UGameplayContextSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	const float Interval = GetDefault<UGameplayContextSettings>()->CheckInterval;
	InWorld.GetTimerManager().SetTimer(UpdateTimer, this, &UGameplayContextSubsystem::UpdateContext, Interval, true, 0.0f);
}

void UGameplayContextSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(UpdateTimer);
	}

	// the pipeline outlives the level
	Report(false, false);

	Super::Deinitialize();
}

void UGameplayContextSubsystem::UpdateContext()
{
	// world time, so the linger time doesn't run out while the game is paused
	const double Now = GetWorld()->GetTimeSeconds();

	if (IsEnemyNearPlayer())
	{
		bInCombat = true;
		LastEnemyNearTime = Now;
	}
	else if (bInCombat && Now - LastEnemyNearTime >= GetDefault<UGameplayContextSettings>()->CombatLingerTime)
	{
		bInCombat = false;
	}

	Report(bInCombat, !bInCombat);
}

bool UGameplayContextSubsystem::IsEnemyNearPlayer() const
{
	const UWorld* World = GetWorld();
	const APawn* Player = UGameplayStatics::GetPlayerPawn(World, 0);
	if (!Player)
	{
		return false;
	}

	const FVector PlayerLocation = Player->GetActorLocation();
	const float Radius = GetDefault<UGameplayContextSettings>()->CombatRadius;

	for (TActorIterator<ACombatEnemy> It(World); It; ++It)
	{
		// dead enemies stay in the level as ragdolls for a while
		if (It->CurrentHP > 0.0f && FVector::DistSquared(It->GetActorLocation(), PlayerLocation) <= FMath::Square(Radius))
		{
			return true;
		}
	}
	return false;
}

void UGameplayContextSubsystem::Report(bool bCombat, bool bNavigation) const
{
	// SetContext ignores calls that don't change anything, so this can run at every check
	if (UAdaptivePipelineSubsystem* Pipeline = UAdaptivePipelineSubsystem::Get(GetWorld()))
	{
		Pipeline->SetContext(GameplayContext::Combat, bCombat);
		Pipeline->SetContext(GameplayContext::Navigation, bNavigation);
	}
}
