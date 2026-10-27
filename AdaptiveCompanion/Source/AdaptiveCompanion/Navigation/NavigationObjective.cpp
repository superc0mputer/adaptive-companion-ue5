#include "NavigationObjective.h"
#include "NavigationObjectiveSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

ANavigationObjective::ANavigationObjective()
{
	PrimaryActorTick.bCanEverTick = false;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));

	// the actor's location is on the floor, where the path to it ends
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	// a 3 m pillar standing on the floor, only for the player to see, so it doesn't affect collision or the navmesh
	Marker = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Marker"));
	Marker->SetupAttachment(RootComponent);
	Marker->SetStaticMesh(CylinderMesh.Object);
	Marker->SetRelativeLocation(FVector(0.0f, 0.0f, 150.0f));
	Marker->SetRelativeScale3D(FVector(0.5f, 0.5f, 3.0f));
	Marker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Marker->SetCanEverAffectNavigation(false);

	// movable, so objectives can also be placed or moved at runtime
	RootComponent->SetMobility(EComponentMobility::Movable);
	Marker->SetMobility(EComponentMobility::Movable);
}

void ANavigationObjective::BeginPlay()
{
	Super::BeginPlay();

	if (UNavigationObjectiveSubsystem* Objectives = GetWorld()->GetSubsystem<UNavigationObjectiveSubsystem>())
	{
		Objectives->RegisterObjective(this);
	}
}

void ANavigationObjective::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UNavigationObjectiveSubsystem* Objectives = GetWorld()->GetSubsystem<UNavigationObjectiveSubsystem>())
	{
		Objectives->UnregisterObjective(this);
	}

	Super::EndPlay(EndPlayReason);
}

void ANavigationObjective::SetReached()
{
	bReached = true;
	SetActorHiddenInGame(true);
}
