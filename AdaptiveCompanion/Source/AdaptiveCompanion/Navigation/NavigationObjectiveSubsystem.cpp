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

	// objectives register in their BeginPlay, right after this; report the first one on the first frame
	InWorld.GetTimerManager().SetTimerForNextTick(this, &UNavigationObjectiveSubsystem::UpdateCurrent);
}

void UNavigationObjectiveSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(CheckTimer);
	}

	Objectives.Reset();

	// the pipeline outlives the level
	if (bReportedAny)
	{
		if (UAdaptivePipelineSubsystem* Pipeline = UAdaptivePipelineSubsystem::Get(GetWorld()))
		{
			Pipeline->ClearObjective();
		}
	}

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

void UNavigationObjectiveSubsystem::UpdateCurrent()
{
	ANavigationObjective* Current = GetCurrentObjective();
	if (bReportedAny && Current == ReportedCurrent.Get())
	{
		return;
	}

	for (ANavigationObjective* Objective : Objectives)
	{
		if (IsValid(Objective))
		{
			Objective->SetCurrent(Objective == Current);
		}
	}

	if (UAdaptivePipelineSubsystem* Pipeline = UAdaptivePipelineSubsystem::Get(GetWorld()))
	{
		if (Current)
		{
			Pipeline->SetObjective(FName(*FString::Printf(TEXT("Objective.%d"), Current->Order)), Current->GetActorLocation());
		}
		else if (bReportedAny)
		{
			Pipeline->ClearObjective();
		}
	}

	ReportedCurrent = Current;
	bReportedAny = true;
}

void UNavigationObjectiveSubsystem::CheckReached()
{
	// objectives register in their BeginPlay and may still be added, so this runs on every check
	UpdateCurrent();

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

	UpdateCurrent();

	OnObjectiveReached.Broadcast(Objective);
}
