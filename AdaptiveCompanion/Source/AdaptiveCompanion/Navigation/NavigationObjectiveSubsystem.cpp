#include "NavigationObjectiveSubsystem.h"
#include "NavigationObjective.h"
#include "NavigationTelemetrySubsystem.h"
#include "GameplayContextSettings.h"
#include "AdaptiveCompanion.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"
#include "Pipeline/AdaptivePipelineSubsystem.h"

bool UNavigationObjectiveSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UNavigationObjectiveSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	const float Interval = GetDefault<UGameplayContextSettings>()->CheckInterval;
	InWorld.GetTimerManager().SetTimer(CheckTimer, this, &UNavigationObjectiveSubsystem::CheckReached, Interval, true, 0.0f);
}

void UNavigationObjectiveSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(CheckTimer);
	}

	Objectives.Reset();

	Super::Deinitialize();
}

void UNavigationObjectiveSubsystem::RegisterObjective(ANavigationObjective* Objective)
{
	Objectives.AddUnique(Objective);
}

void UNavigationObjectiveSubsystem::UnregisterObjective(ANavigationObjective* Objective)
{
	Objectives.Remove(Objective);
}

ANavigationObjective* UNavigationObjectiveSubsystem::GetCurrentObjective() const
{
	// looked up on every call rather than sorted once, so Order can still be changed after spawning
	ANavigationObjective* Current = nullptr;
	for (ANavigationObjective* Objective : Objectives)
	{
		if (IsValid(Objective) && !Objective->IsReached() && (!Current || Objective->Order < Current->Order))
		{
			Current = Objective;
		}
	}
	return Current;
}

void UNavigationObjectiveSubsystem::CheckReached()
{
	ANavigationObjective* Objective = GetCurrentObjective();
	const APawn* Player = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	if (!Objective || !Player || FVector::Dist(Player->GetActorLocation(), Objective->GetActorLocation()) > Objective->ReachRadius)
	{
		return;
	}

	Objective->SetReached();
	UE_LOG(LogAdaptiveCompanion, Log, TEXT("Navigation objective %d (%s) reached."), Objective->Order, *Objective->GetName());

	if (UAdaptivePipelineSubsystem* Pipeline = UAdaptivePipelineSubsystem::Get(GetWorld()))
	{
		Pipeline->ReportEvent(NavigationTelemetry::ObjectiveReached, Objective->Order);
	}

	OnObjectiveReached.Broadcast(Objective);
}
