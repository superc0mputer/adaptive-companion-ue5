#include "NavigationScenario.h"
#include "ScenarioLog.h"
#include "NavigationObjective.h"
#include "NavigationObjectiveSubsystem.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"

namespace NavigationScenario
{
	const FName StartPhase = TEXT("Start");
	const FName EndPhase = TEXT("End");
}

ANavigationScenario::ANavigationScenario()
{
	PrimaryActorTick.bCanEverTick = false;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ANavigationScenario::BeginPlay()
{
	Super::BeginPlay();

	LogPhase(NavigationScenario::StartPhase, TEXT("started"), nullptr);

	if (UNavigationObjectiveSubsystem* Objectives = GetWorld()->GetSubsystem<UNavigationObjectiveSubsystem>())
	{
		ReachedHandle = Objectives->OnObjectiveReached.AddUObject(this, &ANavigationScenario::OnObjectiveReached);
	}

	// objectives register in their own BeginPlay, which may come after this one
	GetWorldTimerManager().SetTimerForNextTick(this, &ANavigationScenario::StartScenario);
}

void ANavigationScenario::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UNavigationObjectiveSubsystem* Objectives = GetWorld()->GetSubsystem<UNavigationObjectiveSubsystem>())
	{
		Objectives->OnObjectiveReached.Remove(ReachedHandle);
	}

	ScenarioLog::ClearOverlay(this, ScenarioName);

	Super::EndPlay(EndPlayReason);
}

void ANavigationScenario::StartScenario()
{
	StartCurrentLeg();
}

void ANavigationScenario::OnObjectiveReached(ANavigationObjective* Objective)
{
	LogPhase(Objective->Phase, TEXT("reached"), Objective);
	StartCurrentLeg();
}

void ANavigationScenario::StartCurrentLeg()
{
	const UNavigationObjectiveSubsystem* Objectives = GetWorld()->GetSubsystem<UNavigationObjectiveSubsystem>();
	if (ANavigationObjective* Current = Objectives ? Objectives->GetCurrentObjective() : nullptr)
	{
		LogPhase(Current->Phase, TEXT("started"), Current);
	}
	else
	{
		LogPhase(NavigationScenario::EndPhase, TEXT("started"), nullptr);
	}
}

void ANavigationScenario::LogPhase(FName Phase, const TCHAR* Event, const ANavigationObjective* Objective) const
{
	const int32 Order = Objective ? Objective->Order : INDEX_NONE;
	const FString OverlayText = Objective
		? FString::Printf(TEXT("%s, objective %d %s"), *Phase.ToString(), Order, Event)
		: FString::Printf(TEXT("%s, %s"), *Phase.ToString(), Event);

	ScenarioLog::Write(this, ScenarioName, Phase, Event, OverlayText, [Order](FJsonObject& Payload)
	{
		Payload.SetNumberField(TEXT("objective"), Order);
	});
}
