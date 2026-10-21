#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "GameplayTagContainer.h"
#include "CompanionAIController.generated.h"

class UStateTreeAIComponent;

/**
 *  AI Controller for the companion. Runs the StateTree assigned on the possessed ACompanionCharacter,
 *  and starts its Intervention branch whenever the adaptive companion pipeline selects an intervention.
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

	/** Moves the StateTree into its Intervention branch for the given intervention */
	UFUNCTION(BlueprintCallable, Category = "Companion")
	void StartIntervention(FName Intervention);

	/** The intervention the Intervention branch is (or was last) carrying out */
	UFUNCTION(BlueprintPure, Category = "Companion")
	FName GetCurrentIntervention() const { return CurrentIntervention; }

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

	/** Bound to the pipeline's OnIntervention */
	UFUNCTION()
	void HandlePipelineIntervention(FName Intervention);

	FName CurrentIntervention;
};
