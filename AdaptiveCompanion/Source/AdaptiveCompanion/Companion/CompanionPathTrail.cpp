#include "CompanionPathTrail.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

ACompanionPathTrail::ACompanionPathTrail()
{
	PrimaryActorTick.bCanEverTick = true;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShapeMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	Discs = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Discs"));
	RootComponent = Discs;
	Discs->SetStaticMesh(CylinderMesh.Object);
	Discs->SetMaterial(0, ShapeMaterial.Object);
	// only for the player to see
	Discs->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Discs->SetCanEverAffectNavigation(false);
	Discs->SetCastShadow(false);
	Discs->SetMobility(EComponentMobility::Movable);
}

void ACompanionPathTrail::ShowPath(const TArray<FVector>& PathPoints, float MaxLength)
{
	Discs->ClearInstances();
	DiscTransforms.Reset();
	RevealedLength = 0.0f;

	if (UMaterialInstanceDynamic* Material = Discs->CreateDynamicMaterialInstance(0))
	{
		// cyan, so the trail differs from the orange objectives and the grey walls
		Material->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.0f, 0.8f, 1.0f));
	}

	// discs every Spacing along the polyline, starting one spacing away from the start (where the player stands)
	float Covered = 0.0f;
	float NextDisc = Spacing;
	for (int32 i = 1; i < PathPoints.Num() && NextDisc <= MaxLength; ++i)
	{
		const FVector From = PathPoints[i - 1];
		const FVector To = PathPoints[i];
		const float SegmentLength = FVector::Dist(From, To);

		while (NextDisc <= Covered + SegmentLength && NextDisc <= MaxLength)
		{
			FVector Location = FMath::Lerp(From, To, (NextDisc - Covered) / FMath::Max(SegmentLength, KINDA_SMALL_NUMBER));

			// navmesh points float a little above the floor, so put the disc on the actual floor
			FHitResult Hit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(CompanionPathTrail), false, this);
			if (GetWorld()->LineTraceSingleByChannel(Hit, Location + FVector(0.0f, 0.0f, 100.0f), Location - FVector(0.0f, 0.0f, 200.0f), ECC_Visibility, Params))
			{
				Location = Hit.ImpactPoint;
			}

			// a 40 cm disc, 2 cm thick, just above the floor so it doesn't flicker with it
			DiscTransforms.Emplace(FRotator::ZeroRotator, Location + FVector(0.0f, 0.0f, 2.0f), FVector(0.4f, 0.4f, 0.02f));
			NextDisc += Spacing;
		}

		Covered += SegmentLength;
	}
}

void ACompanionPathTrail::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const int32 Shown = Discs->GetInstanceCount();
	if (Shown >= DiscTransforms.Num())
	{
		return;
	}

	RevealedLength += RevealSpeed * DeltaSeconds;
	const int32 ToShow = FMath::Min(DiscTransforms.Num(), FMath::FloorToInt32(RevealedLength / Spacing) + 1);
	for (int32 i = Shown; i < ToShow; ++i)
	{
		Discs->AddInstance(DiscTransforms[i], true);
	}
}
