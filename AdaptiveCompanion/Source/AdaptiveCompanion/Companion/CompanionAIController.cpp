#include "CompanionAIController.h"
#include "CompanionCharacter.h"
#include "CompanionGameplayTags.h"
#include "Components/StateTreeAIComponent.h"
#include "EngineUtils.h"
#include "AdaptiveCompanion.h"
#include "Pipeline/AdaptivePipelineSubsystem.h"

ACompanionAIController::ACompanionAIController()
{
	StateTreeAI = CreateDefaultSubobject<UStateTreeAIComponent>(TEXT("StateTreeAI"));

	// the StateTree comes from the pawn, so only start it once we possess one
	bStartAILogicOnPossess = false;
	StateTreeAI->SetStartLogicAutomatically(false);
}

void ACompanionAIController::BeginPlay()
{
	Super::BeginPlay();

	if (UAdaptivePipelineSubsystem* Pipeline = UAdaptivePipelineSubsystem::Get(this))
	{
		Pipeline->OnIntervention.AddUniqueDynamic(this, &ACompanionAIController::HandlePipelineIntervention);
	}
}

void ACompanionAIController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// the pipeline outlives this controller (it belongs to the game instance)
	if (UAdaptivePipelineSubsystem* Pipeline = UAdaptivePipelineSubsystem::Get(this))
	{
		Pipeline->OnIntervention.RemoveDynamic(this, &ACompanionAIController::HandlePipelineIntervention);
	}

	Super::EndPlay(EndPlayReason);
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

void ACompanionAIController::StartIntervention(FName Intervention)
{
	// naive for now (#6): every intervention runs the same Intervention branch, which reads the name from here
	CurrentIntervention = Intervention;
	SendCompanionEvent(CompanionTags::Event_Intervention);
}

void ACompanionAIController::HandlePipelineIntervention(FName Intervention)
{
	UE_LOG(LogAdaptiveCompanion, Log, TEXT("Companion received intervention '%s' from the pipeline."), *Intervention.ToString());
	StartIntervention(Intervention);
}

// Tests the Intervention branch without going through the pipeline
static FAutoConsoleCommandWithWorld GCompanionTriggerInterventionCommand(
	TEXT("Companion.TriggerIntervention"),
	TEXT("Starts the intervention Intervention.Console on every companion, bypassing the pipeline."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		for (TActorIterator<ACompanionAIController> It(World); It; ++It)
		{
			It->StartIntervention(TEXT("Intervention.Console"));
		}
	}));
