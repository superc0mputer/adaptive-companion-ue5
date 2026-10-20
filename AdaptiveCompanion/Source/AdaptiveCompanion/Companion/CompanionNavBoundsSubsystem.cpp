#include "CompanionNavBoundsSubsystem.h"
#include "CompanionSettings.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "Components/BrushComponent.h"
#include "PhysicsEngine/BodySetup.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "AdaptiveCompanion.h"

bool UCompanionNavBoundsSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UCompanionNavBoundsSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	const UCompanionSettings* Settings = GetDefault<UCompanionSettings>();
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&InWorld);
	if (!Settings->bCoverWalkableGeometry || !NavSys)
	{
		return;
	}

	// walkable geometry: everything that blocks pawns, ignoring huge things like sky spheres
	FBox LevelBox(ForceInit);
	for (TActorIterator<AActor> It(&InWorld); It; ++It)
	{
		It->ForEachComponent<UPrimitiveComponent>(false, [&](const UPrimitiveComponent* Component)
		{
			if (Component->IsRegistered() && Component->IsCollisionEnabled()
				&& Component->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block
				&& Component->Bounds.BoxExtent.GetMax() < Settings->GeneratedNavBoundsMaxHalfExtent)
			{
				LevelBox += Component->Bounds.GetBox();
			}
		});
	}

	if (!LevelBox.IsValid)
	{
		return;
	}

	LevelBox = LevelBox.ExpandBy(Settings->GeneratedNavBoundsPadding);

	// levels whose own volumes already cover everything walkable are left alone
	FBox ExistingBounds(ForceInit);
	for (const FNavigationBounds& Bounds : NavSys->GetNavigationBounds())
	{
		ExistingBounds += Bounds.AreaBox;
	}

	if (ExistingBounds.IsValid && ExistingBounds.IsInside(LevelBox))
	{
		return;
	}

	ANavMeshBoundsVolume* Volume = InWorld.SpawnActor<ANavMeshBoundsVolume>(LevelBox.GetCenter(), FRotator::ZeroRotator);
	if (!Volume)
	{
		return;
	}

	// brushes can't be built at runtime, but the volume's bounds also come from its collision box.
	// Spawning resets the brush collision, so set the box afterwards and tell the navigation system about it
	UBrushComponent* BrushComponent = Volume->GetBrushComponent();
	UBodySetup* BodySetup = NewObject<UBodySetup>(BrushComponent);
	const FVector Size = LevelBox.GetSize();
	BodySetup->AggGeom.BoxElems.Add(FKBoxElem(Size.X, Size.Y, Size.Z));
	BrushComponent->BrushBodySetup = BodySetup;
	BrushComponent->UpdateBounds();
	NavSys->OnNavigationBoundsUpdated(Volume);

	UE_LOG(LogAdaptiveCompanion, Log, TEXT("Navigation in level '%s' didn't cover all walkable geometry, spawned a NavMeshBoundsVolume covering %s."), *InWorld.GetMapName(), *LevelBox.ToString());
}
