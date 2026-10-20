#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "GameplayTagContainer.h"
#include "CompanionAIController.generated.h"

class UStateTreeAIComponent;

/**
 *  AI Controller for the companion. Runs the StateTree assigned on the possessed ACompanionCharacter.
 */
UCLASS()
class ADAPTIVECOMPANION_API ACompanionAIController : public AAIController
{
	GENERATED_BODY()

	/** StateTree Component */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStateTreeAIComponent> StateTreeAI;

public:

	ACompanionAIController();

	/** Sends an event to the companion's StateTree, e.g. to start an intervention */
	UFUNCTION(BlueprintCallable, Category = "Companion")
	void SendCompanionEvent(FGameplayTag EventTag);

protected:

	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
};
