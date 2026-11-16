#include "CompanionAIController.h"
#include "CompanionActions.h"
#include "CompanionCharacter.h"
#include "CompanionGameplayTags.h"
#include "CompanionSettings.h"
#include "CameraInputTelemetrySubsystem.h"
#include "CombatEnemy.h"
#include "GameplayContextSettings.h"
#include "Components/StateTreeAIComponent.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
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
		Pipeline->UnregisterActions(this);
	}

	Super::EndPlay(EndPlayReason);
}

void ACompanionAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	const ACompanionCharacter* Companion = Cast<ACompanionCharacter>(InPawn);

	if (UAdaptivePipelineSubsystem* Pipeline = UAdaptivePipelineSubsystem::Get(this); Pipeline && Companion)
	{
		Pipeline->RegisterActions(this, ToRawPtrTArrayUnsafe(Companion->Actions));
	}
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

	if (UAdaptivePipelineSubsystem* Pipeline = UAdaptivePipelineSubsystem::Get(this))
	{
		Pipeline->UnregisterActions(this);
	}

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

FAdaptiveActionCheck ACompanionAIController::CheckAction_Implementation(const UAdaptiveCompanionAction* Action)
{
	const FName Name = Action ? Action->ActionName : NAME_None;
	if (Name == CompanionActions::NavigationHint)
	{
		return CheckNavigationHint();
	}
	if (Name == CompanionActions::ThreatCallout)
	{
		return CheckThreatCallout();
	}
	if (Name == CompanionActions::DrawAggro)
	{
		return CheckDrawAggro();
	}

	FAdaptiveActionCheck Unknown;
	Unknown.bPossible = false;
	Unknown.Reason = TEXT("the companion doesn't know this action");
	return Unknown;
}

FAdaptiveActionCheck ACompanionAIController::CheckNavigationHint()
{
	FAdaptiveActionCheck Check;

	FName Objective;
	FVector ObjectiveLocation;
	const UAdaptivePipelineSubsystem* Pipeline = UAdaptivePipelineSubsystem::Get(this);
	const APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!Pipeline || !Pipeline->GetCurrentObjective(Objective, ObjectiveLocation) || !Player)
	{
		Check.bPossible = false;
		Check.Reason = TEXT("no objective");
		return Check;
	}

	// the hint shows the way, so there has to be one. A path search every update would be wasteful,
	// so it is repeated at an interval, and right away when the objective changes
	const double Now = GetWorld()->GetTimeSeconds();
	const float Interval = GetDefault<UCompanionSettings>()->NavigationPathCheckInterval;
	if (Now - LastPathCheckTime >= Interval || !ObjectiveLocation.Equals(LastPathObjective, 1.0))
	{
		const UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(), Player->GetActorLocation(), ObjectiveLocation);
		bLastPathFound = Path && Path->IsValid() && !Path->IsPartial();
		LastPathCheckTime = Now;
		LastPathObjective = ObjectiveLocation;
	}

	if (!bLastPathFound)
	{
		Check.bPossible = false;
		Check.Reason = TEXT("no path to the objective");
	}
	return Check;
}

FAdaptiveActionCheck ACompanionAIController::CheckThreatCallout() const
{
	FAdaptiveActionCheck Check;
	const UCameraInputTelemetrySubsystem* Telemetry = GetWorld()->GetSubsystem<UCameraInputTelemetrySubsystem>();
	if (!Telemetry || Telemetry->GetEnemiesOutOfView() <= 0)
	{
		Check.bPossible = false;
		Check.Reason = TEXT("no enemy out of view");
	}
	return Check;
}

FAdaptiveActionCheck ACompanionAIController::CheckDrawAggro() const
{
	FAdaptiveActionCheck Check;

	const APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	const APawn* Companion = GetPawn();
	if (!Player || !Companion)
	{
		Check.bPossible = false;
		Check.Reason = TEXT("no player or companion");
		return Check;
	}

	// the enemy closest to the companion among the living ones near the player
	const float Radius = GetDefault<UGameplayContextSettings>()->CombatRadius;
	double Nearest = UE_BIG_NUMBER;
	for (TActorIterator<ACombatEnemy> It(GetWorld()); It; ++It)
	{
		if (It->CurrentHP > 0.0f && FVector::Dist(It->GetActorLocation(), Player->GetActorLocation()) <= Radius)
		{
			Nearest = FMath::Min(Nearest, FVector::Dist(It->GetActorLocation(), Companion->GetActorLocation()));
		}
	}

	const float MaxDistance = GetDefault<UCompanionSettings>()->DrawAggroMaxDistance;
	if (Nearest == UE_BIG_NUMBER)
	{
		Check.bPossible = false;
		Check.Reason = TEXT("no enemy near the player");
	}
	else if (Nearest > MaxDistance)
	{
		Check.bPossible = false;
		Check.Reason = TEXT("companion too far from the enemies");
	}
	else
	{
		// running further to the enemy takes longer and leaves the player alone for longer
		Check.Cost = Nearest / MaxDistance;
	}
	return Check;
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
