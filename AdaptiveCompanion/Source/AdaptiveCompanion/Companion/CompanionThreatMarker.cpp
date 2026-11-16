#include "CompanionThreatMarker.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

ACompanionThreatMarker::ACompanionThreatMarker()
{
	PrimaryActorTick.bCanEverTick = false;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeMesh(TEXT("/Engine/BasicShapes/Cone.Cone"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShapeMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent->SetMobility(EComponentMobility::Movable);

	// the engine cone is 1 m tall with its pivot in the middle; upside down and scaled to 40 cm, its tip at the root
	Cone = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Cone"));
	Cone->SetupAttachment(RootComponent);
	Cone->SetStaticMesh(ConeMesh.Object);
	Cone->SetMaterial(0, ShapeMaterial.Object);
	Cone->SetRelativeRotation(FRotator(180.0f, 0.0f, 0.0f));
	Cone->SetRelativeScale3D(FVector(0.3f, 0.3f, 0.4f));
	Cone->SetRelativeLocation(FVector(0.0f, 0.0f, 20.0f));
	Cone->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Cone->SetCanEverAffectNavigation(false);
	Cone->SetCastShadow(false);
	Cone->SetMobility(EComponentMobility::Movable);
}

void ACompanionThreatMarker::BeginPlay()
{
	Super::BeginPlay();

	if (UMaterialInstanceDynamic* Material = Cone->CreateDynamicMaterialInstance(0))
	{
		// the same red as the arrow on screen
		Material->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.0f, 0.1f, 0.05f));
	}
}

void ACompanionThreatMarker::AttachAbove(AActor* Target, float Gap)
{
	if (!Target)
	{
		return;
	}

	float Radius = 0.0f;
	float HalfHeight = 0.0f;
	Target->GetSimpleCollisionCylinder(Radius, HalfHeight);

	AttachToActor(Target, FAttachmentTransformRules::KeepRelativeTransform);
	SetActorRelativeLocation(FVector(0.0f, 0.0f, HalfHeight + Gap));
	// stays upright when the enemy is knocked around
	RootComponent->SetUsingAbsoluteRotation(true);
	SetActorRotation(FRotator::ZeroRotator);
}
