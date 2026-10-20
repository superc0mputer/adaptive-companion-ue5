#include "CompanionAIController.h"
#include "CompanionCharacter.h"
#include "CompanionGameplayTags.h"
#include "Components/StateTreeAIComponent.h"
#include "EngineUtils.h"
#include "AdaptiveCompanion.h"

ACompanionAIController::ACompanionAIController()
{
	StateTreeAI = CreateDefaultSubobject<UStateTreeAIComponent>(TEXT("StateTreeAI"));

	// the StateTree comes from the pawn, so only start it once we possess one
	bStartAILogicOnPossess = false;
	StateTreeAI->SetStartLogicAutomatically(false);
}

void ACompanionAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	const ACompanionCharacter* Companion = Cast<ACompanionCharacter>(InPawn);
	if (!Companion || !Companion->CompanionStateTree)
	{
		UE_LOG(LogAdaptiveCompanion, Warning, TEXT("'%s' has no CompanionStateTree assigned, the companion will stand still."), *GetNameSafe(InPawn));
		return;
	}

	StateTreeAI->SetStateTree(Companion->CompanionStateTree);
	StateTreeAI->StartLogic();
}

void ACompanionAIController::OnUnPossess()
{
	StateTreeAI->StopLogic(TEXT("Unpossessed"));

	Super::OnUnPossess();
}

void ACompanionAIController::SendCompanionEvent(FGameplayTag EventTag)
{
	StateTreeAI->SendStateTreeEvent(EventTag);
}

// Lets you test the Intervention branch before the pipeline exists (#24)
static FAutoConsoleCommandWithWorld GCompanionTriggerInterventionCommand(
	TEXT("Companion.TriggerIntervention"),
	TEXT("Sends the Companion.Event.Intervention event to every companion's StateTree."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		for (TActorIterator<ACompanionAIController> It(World); It; ++It)
		{
			It->SendCompanionEvent(CompanionTags::Event_Intervention);
		}
	}));
