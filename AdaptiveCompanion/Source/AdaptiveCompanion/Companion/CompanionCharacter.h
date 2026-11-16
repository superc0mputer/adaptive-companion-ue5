#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "CompanionCharacter.generated.h"

class UStateTree;
class UAdaptiveCompanionAction;

/**
 *  AI companion that accompanies the player.
 *  Its behavior is driven by a StateTree run by ACompanionAIController.
 */
UCLASS(abstract)
class ADAPTIVECOMPANION_API ACompanionCharacter : public ACharacter
{
	GENERATED_BODY()

public:

	ACompanionCharacter();

	/** StateTree that drives this companion. Must use the StateTree AI Component schema */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Companion")
	TObjectPtr<UStateTree> CompanionStateTree;

	/** What this companion can do to help. Registered with the adaptive companion pipeline by ACompanionAIController */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Companion")
	TArray<TObjectPtr<UAdaptiveCompanionAction>> Actions;

	/** Default walking speed, restored when a behavior that changes the speed ends */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Companion", meta = (ClampMin = 0, Units = "cm/s"))
	float DefaultWalkSpeed = 500.0f;
};
