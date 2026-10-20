#include "CompanionVisibility.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"

bool CompanionVisibility::IsVisibleToPlayer(const APlayerController& PlayerController, const AActor& Character, const FVector& Location, const float HalfHeight, const float ViewMarginAngle)
{
	FVector CameraLocation;
	FRotator CameraRotation;
	PlayerController.GetPlayerViewPoint(CameraLocation, CameraRotation);

	// FOV is horizontal, which is the wider angle, so this errs on the side of "visible"
	const float FOV = PlayerController.PlayerCameraManager ? PlayerController.PlayerCameraManager->GetFOVAngle() : 90.0f;
	const float CosHalfAngle = FMath::Cos(FMath::DegreesToRadians(FMath::Min(FOV * 0.5f + ViewMarginAngle, 180.0f)));

	FCollisionQueryParams Params(SCENE_QUERY_STAT(CompanionVisibility), false);
	Params.AddIgnoredActor(&Character);
	Params.AddIgnoredActor(PlayerController.GetPawn());

	const FVector SamplePoints[] = { Location, Location + FVector(0.0f, 0.0f, HalfHeight * 0.8f) };
	for (const FVector& Point : SamplePoints)
	{
		const FVector ToPoint = (Point - CameraLocation).GetSafeNormal();
		if (FVector::DotProduct(ToPoint, CameraRotation.Vector()) < CosHalfAngle)
		{
			continue;
		}

		FHitResult Hit;
		if (!Character.GetWorld()->LineTraceSingleByChannel(Hit, CameraLocation, Point, ECC_Visibility, Params))
		{
			return true;
		}
	}

	return false;
}
