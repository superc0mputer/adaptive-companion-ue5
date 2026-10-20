#include "CompanionCharacter.h"
#include "CompanionAIController.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

ACompanionCharacter::ACompanionCharacter()
{
	// same capsule and mesh placement as the player character
	GetCapsuleComponent()->InitCapsuleSize(42.0f, 96.0f);
	GetMesh()->SetRelativeLocationAndRotation(FVector(0.0f, 0.0f, -96.0f), FRotator(0.0f, -90.0f, 0.0f));

	// face the direction of movement instead of the controller
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->bOrientRotationToMovement = true;
	Movement->RotationRate = FRotator(0.0f, 500.0f, 0.0f);
	Movement->MaxWalkSpeed = DefaultWalkSpeed;

	// the mannequin AnimBP only plays locomotion while there is acceleration, which path following doesn't produce by default
	Movement->GetNavMovementProperties()->bUseAccelerationForPaths = true;

	// spawned at runtime, so make sure it always gets its controller
	AIControllerClass = ACompanionAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
}
