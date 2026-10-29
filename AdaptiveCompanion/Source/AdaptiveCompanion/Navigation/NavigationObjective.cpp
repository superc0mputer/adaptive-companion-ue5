#include "NavigationObjective.h"
#include "NavigationObjectiveSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

ANavigationObjective::ANavigationObjective()
{
	PrimaryActorTick.bCanEverTick = false;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShapeMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

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
	Marker->SetMaterial(0, ShapeMaterial.Object);

	// a 30 cm column from the top of the pillar up to 25 m, well above 4 m walls
	Beacon = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Beacon"));
	Beacon->SetupAttachment(RootComponent);
	Beacon->SetStaticMesh(CylinderMesh.Object);
	Beacon->SetRelativeLocation(FVector(0.0f, 0.0f, 1400.0f));
	Beacon->SetRelativeScale3D(FVector(0.3f, 0.3f, 22.0f));
	Beacon->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Beacon->SetCanEverAffectNavigation(false);
	Beacon->SetCastShadow(false);
	Beacon->SetMaterial(0, ShapeMaterial.Object);

	// movable, so objectives can also be placed or moved at runtime
	RootComponent->SetMobility(EComponentMobility::Movable);
	Marker->SetMobility(EComponentMobility::Movable);
	Beacon->SetMobility(EComponentMobility::Movable);
}

void ANavigationObjective::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	Beacon->SetVisibility(bShowBeacon);
}

void ANavigationObjective::BeginPlay()
{
	Super::BeginPlay();

	// orange, so objectives stand out against the grey level prototyping walls
	for (UStaticMeshComponent* Mesh : { Marker, Beacon })
	{
		if (UMaterialInstanceDynamic* Material = Mesh->CreateDynamicMaterialInstance(0))
		{
			Material->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.0f, 0.35f, 0.0f));
		}
	}

	// hidden until the subsystem makes it the current objective
	SetCurrent(false);

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
	SetCurrent(false);
}

void ANavigationObjective::SetCurrent(bool bCurrent)
{
	Marker->SetVisibility(bCurrent);
	Beacon->SetVisibility(bCurrent && bShowBeacon);
}
