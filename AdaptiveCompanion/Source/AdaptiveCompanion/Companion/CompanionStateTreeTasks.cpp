#include "CompanionStateTreeTasks.h"
#include "CompanionAIController.h"
#include "CompanionCharacter.h"
#include "CompanionVisibility.h"
#include "CompanionPathTrail.h"
#include "NavigationPath.h"
#include "StateTreeExecutionContext.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "DrawDebugHelpers.h"
#include "AdaptiveCompanion.h"
#include "Pipeline/AdaptiveBehaviorStage.h"
#include "Pipeline/AdaptivePipelineSubsystem.h"

namespace
{
	/** Restores the walking speed configured on the companion */
	void ResetCompanionSpeed(APawn* Pawn)
	{
		if (const ACompanionCharacter* Companion = Cast<ACompanionCharacter>(Pawn))
		{
			Companion->GetCharacterMovement()->MaxWalkSpeed = Companion->DefaultWalkSpeed;
		}
	}
}

EStateTreeRunStatus FCompanionFollowPlayerTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	InstanceData.TimeWithoutProgress = 0.0f;

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FCompanionFollowPlayerTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	AAIController* Controller = InstanceData.Controller;
	ACharacter* Companion = Controller ? Controller->GetPawn<ACharacter>() : nullptr;
	if (!Companion)
	{
		return EStateTreeRunStatus::Failed;
	}

	// the player pawn is missing while it respawns, so just wait
	APawn* Player = UGameplayStatics::GetPlayerPawn(Controller, 0);
	if (!Player)
	{
		Controller->StopMovement();
		InstanceData.TimeWithoutProgress = 0.0f;
		return EStateTreeRunStatus::Running;
	}

	const float Distance = FVector::Dist(Companion->GetActorLocation(), Player->GetActorLocation());
	const bool bWantsToMove = Distance > InstanceData.StartMovingDistance;

	// start a new move once the player has walked off. MoveToActor keeps tracking the player while it runs
	if (bWantsToMove && Controller->GetMoveStatus() != EPathFollowingStatus::Moving)
	{
		Controller->MoveToActor(Player, InstanceData.FollowDistance, true, true, false, nullptr, true);
	}

	// run to catch up when far behind
	if (const ACompanionCharacter* CompanionCharacter = Cast<ACompanionCharacter>(Companion))
	{
		Companion->GetCharacterMovement()->MaxWalkSpeed = Distance > InstanceData.RunDistance ? InstanceData.RunSpeed : CompanionCharacter->DefaultWalkSpeed;
	}

	// stuck detection: we want to move but aren't moving (no path, blocked by geometry, ...)
	if (bWantsToMove && Companion->GetVelocity().Size2D() < 10.0f)
	{
		InstanceData.TimeWithoutProgress += DeltaTime;

		if (InstanceData.TimeWithoutProgress >= InstanceData.StuckTime)
		{
			UE_LOG(LogAdaptiveCompanion, Log, TEXT("Companion is stuck %.0f cm away from the player."), Distance);
			return EStateTreeRunStatus::Failed;
		}
	}
	else
	{
		InstanceData.TimeWithoutProgress = 0.0f;
	}

	return EStateTreeRunStatus::Running;
}

void FCompanionFollowPlayerTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	if (InstanceData.Controller)
	{
		InstanceData.Controller->StopMovement();
		ResetCompanionSpeed(InstanceData.Controller->GetPawn());
	}
}

#if WITH_EDITOR
FText FCompanionFollowPlayerTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting /*= EStateTreeNodeFormatting::Text*/) const
{
	const FInstanceDataType* InstanceData = InstanceDataView.GetPtr<FInstanceDataType>();
	return FText::FromString(FString::Printf(TEXT("<b>Follow Player</b> at %.0f cm"), InstanceData ? InstanceData->FollowDistance : 0.0f));
}
#endif // WITH_EDITOR

////////////////////////////////////////////////////////////////////

EStateTreeRunStatus FCompanionRepositionTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	AAIController* Controller = InstanceData.Controller;
	ACharacter* Companion = Controller ? Controller->GetPawn<ACharacter>() : nullptr;
	const APlayerController* PlayerController = UGameplayStatics::GetPlayerController(Controller, 0);
	const APawn* Player = PlayerController ? PlayerController->GetPawn() : nullptr;
	if (!Companion || !Player)
	{
		return EStateTreeRunStatus::Failed;
	}

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Companion->GetWorld());
	const float HalfHeight = Companion->GetSimpleCollisionHalfHeight();
	const FVector PlayerLocation = Player->GetActorLocation();
	const FVector Behind = -Player->GetActorForwardVector().GetSafeNormal2D();
	const FVector QueryExtent(InstanceData.SearchRadius, InstanceData.SearchRadius, 500.0f);

	// try spots from directly behind the player outwards to its sides
	static const float CandidateAngles[] = { 0.0f, 30.0f, -30.0f, 60.0f, -60.0f, 90.0f, -90.0f };
	for (const float Angle : CandidateAngles)
	{
		FVector TargetLocation = PlayerLocation + Behind.RotateAngleAxis(Angle, FVector::UpVector) * InstanceData.DistanceBehindPlayer;

		FNavLocation NavLocation;
		if (NavSys && NavSys->ProjectPointToNavigation(TargetLocation, NavLocation, QueryExtent))
		{
			// navmesh points are on the floor, the actor location is the capsule center
			TargetLocation = NavLocation.Location + FVector(0.0f, 0.0f, HalfHeight);
		}

		if (InstanceData.bOnlyHiddenSpots && CompanionVisibility::IsVisibleToPlayer(*PlayerController, *Companion, TargetLocation, HalfHeight, InstanceData.ViewMarginAngle))
		{
			continue;
		}

		const FRotator FacePlayer(0.0f, (PlayerLocation - TargetLocation).Rotation().Yaw, 0.0f);

		Controller->StopMovement();
		if (Companion->TeleportTo(TargetLocation, FacePlayer))
		{
			UE_LOG(LogAdaptiveCompanion, Log, TEXT("Companion repositioned near the player (%.0f degrees from behind)."), Angle);
			return EStateTreeRunStatus::Succeeded;
		}
	}

	UE_LOG(LogAdaptiveCompanion, Log, TEXT("Companion could not be repositioned: every spot near the player is visible or blocked."));
	return EStateTreeRunStatus::Failed;
}

#if WITH_EDITOR
FText FCompanionRepositionTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting /*= EStateTreeNodeFormatting::Text*/) const
{
	return FText::FromString("<b>Reposition Near Player</b>");
}
#endif // WITH_EDITOR

////////////////////////////////////////////////////////////////////

EStateTreeRunStatus FCompanionInterventionPlaceholderTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	InstanceData.ElapsedTime = 0.0f;
	InstanceData.Outcome = NAME_None;
	InstanceData.FailReason.Reset();

	ACompanionAIController* Controller = Cast<ACompanionAIController>(InstanceData.Controller);
	ACharacter* Companion = Controller ? Controller->GetPawn<ACharacter>() : nullptr;
	InstanceData.Intervention = Controller ? Controller->GetCurrentIntervention() : NAME_None;
	if (!Companion)
	{
		InstanceData.Outcome = AdaptiveInterventionPhase::Failed;
		InstanceData.FailReason = TEXT("no companion");
		return EStateTreeRunStatus::Failed;
	}

	Controller->StopMovement();
	if (APawn* Player = UGameplayStatics::GetPlayerPawn(Controller, 0))
	{
		Companion->GetCharacterMovement()->MaxWalkSpeed = InstanceData.RunSpeed;
		Controller->MoveToActor(Player, InstanceData.AcceptanceRadius, true, true, false, nullptr, true);
	}

	// the selected action's name below the bark, so it is visible which one ran until the real behaviors exist (#20-#22)
	const FString Text = FString::Printf(TEXT("%s\n(%s)"), *InstanceData.Bark, *InstanceData.Intervention.ToString());
	// attached to the companion, so it moves along
	DrawDebugString(Companion->GetWorld(), FVector(0.0f, 0.0f, 120.0f), Text, Companion, FColor::White, InstanceData.BarkDuration, true, 1.5f);

	if (UAdaptivePipelineSubsystem* Pipeline = UAdaptivePipelineSubsystem::Get(Companion))
	{
		Pipeline->ReportInterventionPhase(InstanceData.Intervention, AdaptiveInterventionPhase::Started);
	}

	UE_LOG(LogAdaptiveCompanion, Log, TEXT("Companion intervention '%s' started (placeholder)."), *InstanceData.Intervention.ToString());
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FCompanionInterventionPlaceholderTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	InstanceData.ElapsedTime += DeltaTime;

	const bool bArrived = !InstanceData.Controller || InstanceData.Controller->GetMoveStatus() != EPathFollowingStatus::Moving;
	if (bArrived && InstanceData.ElapsedTime >= InstanceData.Duration)
	{
		InstanceData.Outcome = AdaptiveInterventionPhase::Completed;
		return EStateTreeRunStatus::Succeeded;
	}
	if (InstanceData.ElapsedTime >= InstanceData.MaxDuration)
	{
		InstanceData.Outcome = AdaptiveInterventionPhase::Failed;
		InstanceData.FailReason = TEXT("didn't reach the player in time");
		return EStateTreeRunStatus::Failed;
	}
	return EStateTreeRunStatus::Running;
}

void FCompanionInterventionPlaceholderTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	if (InstanceData.Controller)
	{
		InstanceData.Controller->StopMovement();
		ResetCompanionSpeed(InstanceData.Controller->GetPawn());
	}

	// no outcome yet: the state was left while the task still ran, e.g. another event or the companion was unpossessed
	const FName Phase = InstanceData.Outcome.IsNone() ? AdaptiveInterventionPhase::Interrupted : InstanceData.Outcome;
	if (UAdaptivePipelineSubsystem* Pipeline = UAdaptivePipelineSubsystem::Get(InstanceData.Controller))
	{
		Pipeline->ReportInterventionPhase(InstanceData.Intervention, Phase, InstanceData.FailReason);
	}

	UE_LOG(LogAdaptiveCompanion, Log, TEXT("Companion intervention '%s' %s (placeholder)."), *InstanceData.Intervention.ToString(), *Phase.ToString());
}

#if WITH_EDITOR
FText FCompanionInterventionPlaceholderTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting /*= EStateTreeNodeFormatting::Text*/) const
{
	return FText::FromString("<b>Intervention Placeholder</b>");
}
#endif // WITH_EDITOR

////////////////////////////////////////////////////////////////////

namespace
{
	/** Navmesh path points from From to To; false without a full path */
	bool FindFullPath(UWorld* World, const FVector& From, const FVector& To, TArray<FVector>& OutPoints)
	{
		const UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(World, From, To);
		if (!Path || !Path->IsValid() || Path->IsPartial() || Path->PathPoints.Num() < 2)
		{
			return false;
		}
		OutPoints = Path->PathPoints;
		return true;
	}

	float PathLength(const TArray<FVector>& Points)
	{
		float Length = 0.0f;
		for (int32 i = 1; i < Points.Num(); ++i)
		{
			Length += FVector::Dist(Points[i - 1], Points[i]);
		}
		return Length;
	}

	/** The point Distance along the path, or its end if the path is shorter */
	FVector PointAlongPath(const TArray<FVector>& Points, float Distance)
	{
		for (int32 i = 1; i < Points.Num(); ++i)
		{
			const float SegmentLength = FVector::Dist(Points[i - 1], Points[i]);
			if (Distance <= SegmentLength)
			{
				return FMath::Lerp(Points[i - 1], Points[i], Distance / FMath::Max(SegmentLength, KINDA_SMALL_NUMBER));
			}
			Distance -= SegmentLength;
		}
		return Points.Last();
	}
}

EStateTreeRunStatus FCompanionNavigationHintTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	InstanceData.ElapsedTime = 0.0f;
	InstanceData.TimeSinceProgressCheck = 0.0f;
	InstanceData.Outcome = NAME_None;
	InstanceData.Detail.Reset();
	InstanceData.Trail.Reset();

	ACompanionAIController* Controller = Cast<ACompanionAIController>(InstanceData.Controller);
	ACharacter* Companion = Controller ? Controller->GetPawn<ACharacter>() : nullptr;
	InstanceData.Intervention = Controller ? Controller->GetCurrentIntervention() : NAME_None;
	const APawn* Player = UGameplayStatics::GetPlayerPawn(Controller, 0);
	UAdaptivePipelineSubsystem* Pipeline = UAdaptivePipelineSubsystem::Get(Controller);

	auto Fail = [&InstanceData](const TCHAR* Reason)
	{
		InstanceData.Outcome = AdaptiveInterventionPhase::Failed;
		InstanceData.Detail = Reason;
		return EStateTreeRunStatus::Failed;
	};

	if (!Companion || !Player)
	{
		return Fail(TEXT("no companion or player"));
	}
	if (!Pipeline || !Pipeline->GetCurrentObjective(InstanceData.Objective, InstanceData.ObjectiveLocation))
	{
		return Fail(TEXT("no objective"));
	}

	UWorld* World = Companion->GetWorld();
	TArray<FVector> PathPoints;
	if (!FindFullPath(World, Player->GetActorLocation(), InstanceData.ObjectiveLocation, PathPoints))
	{
		return Fail(TEXT("no path to the objective"));
	}
	InstanceData.StartPathLength = PathLength(PathPoints);

	// the trail on the floor, from the player along the way
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (ACompanionPathTrail* Trail = World->SpawnActor<ACompanionPathTrail>(ACompanionPathTrail::StaticClass(), FTransform::Identity, SpawnParams))
	{
		Trail->ShowPath(PathPoints, InstanceData.TrailLength);
		InstanceData.Trail = Trail;
	}

	// the companion runs ahead to a spot on the same path and then faces the way on
	const FVector LeadSpot = PointAlongPath(PathPoints, InstanceData.LeadDistance);
	InstanceData.LookTarget = PointAlongPath(PathPoints, InstanceData.LeadDistance + 300.0f);
	if (InstanceData.LookTarget.Equals(LeadSpot, 10.0f))
	{
		// the spot is the objective itself; face away from the player
		InstanceData.LookTarget = LeadSpot + (LeadSpot - Player->GetActorLocation()).GetSafeNormal2D() * 300.0f;
	}

	Controller->StopMovement();
	Companion->GetCharacterMovement()->MaxWalkSpeed = InstanceData.RunSpeed;
	Controller->MoveToLocation(LeadSpot, 50.0f, false, true, false, true, nullptr, false);

	DrawDebugString(World, FVector(0.0f, 0.0f, 120.0f), InstanceData.Bark, Companion, FColor::White, InstanceData.BarkDuration, true, 1.5f);

	Pipeline->ReportInterventionPhase(InstanceData.Intervention, AdaptiveInterventionPhase::Started);
	UE_LOG(LogAdaptiveCompanion, Log, TEXT("Companion navigation hint to %s started: path %.0f cm, trail of %d discs."),
		*InstanceData.Objective.ToString(), InstanceData.StartPathLength, InstanceData.Trail.IsValid() ? InstanceData.Trail->GetDiscCount() : 0);
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FCompanionNavigationHintTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	InstanceData.ElapsedTime += DeltaTime;
	InstanceData.TimeSinceProgressCheck += DeltaTime;

	AAIController* Controller = InstanceData.Controller;
	ACharacter* Companion = Controller ? Controller->GetPawn<ACharacter>() : nullptr;
	const APawn* Player = UGameplayStatics::GetPlayerPawn(Controller, 0);
	const UAdaptivePipelineSubsystem* Pipeline = UAdaptivePipelineSubsystem::Get(Controller);
	if (!Companion)
	{
		InstanceData.Outcome = AdaptiveInterventionPhase::Failed;
		InstanceData.Detail = TEXT("no companion");
		return EStateTreeRunStatus::Failed;
	}

	// once at its spot, turn to face the way on
	if (Controller->GetMoveStatus() != EPathFollowingStatus::Moving)
	{
		const FVector ToTarget = (InstanceData.LookTarget - Companion->GetActorLocation()).GetSafeNormal2D();
		if (!ToTarget.IsNearlyZero())
		{
			Companion->SetActorRotation(FMath::RInterpTo(Companion->GetActorRotation(), ToTarget.Rotation(), DeltaTime, 8.0f));
		}
	}

	// the objective was reached (or another one became current): the hint is done right away
	FName Objective;
	FVector ObjectiveLocation;
	if (!Pipeline || !Pipeline->GetCurrentObjective(Objective, ObjectiveLocation) || Objective != InstanceData.Objective)
	{
		InstanceData.Outcome = AdaptiveInterventionPhase::Completed;
		InstanceData.Detail = TEXT("objective reached");
		return EStateTreeRunStatus::Succeeded;
	}

	if (Player && InstanceData.TimeSinceProgressCheck >= InstanceData.ProgressCheckInterval)
	{
		InstanceData.TimeSinceProgressCheck = 0.0f;

		// path length rather than straight distance: in a maze the right way often leads away from the objective first
		TArray<FVector> PathPoints;
		if (FindFullPath(Companion->GetWorld(), Player->GetActorLocation(), InstanceData.ObjectiveLocation, PathPoints)
			&& InstanceData.StartPathLength - PathLength(PathPoints) >= InstanceData.FollowDistance
			&& InstanceData.ElapsedTime >= InstanceData.MinDuration)
		{
			InstanceData.Outcome = AdaptiveInterventionPhase::Completed;
			InstanceData.Detail = TEXT("player followed");
			return EStateTreeRunStatus::Succeeded;
		}
	}

	if (InstanceData.ElapsedTime >= InstanceData.MaxDuration)
	{
		InstanceData.Outcome = AdaptiveInterventionPhase::Completed;
		InstanceData.Detail = TEXT("player didn't follow");
		return EStateTreeRunStatus::Succeeded;
	}
	return EStateTreeRunStatus::Running;
}

void FCompanionNavigationHintTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	if (ACompanionPathTrail* Trail = InstanceData.Trail.Get())
	{
		Trail->Destroy();
	}
	InstanceData.Trail.Reset();

	if (InstanceData.Controller)
	{
		InstanceData.Controller->StopMovement();
		ResetCompanionSpeed(InstanceData.Controller->GetPawn());
	}

	const FName Phase = InstanceData.Outcome.IsNone() ? AdaptiveInterventionPhase::Interrupted : InstanceData.Outcome;
	if (UAdaptivePipelineSubsystem* Pipeline = UAdaptivePipelineSubsystem::Get(InstanceData.Controller))
	{
		Pipeline->ReportInterventionPhase(InstanceData.Intervention, Phase, InstanceData.Detail);
	}

	UE_LOG(LogAdaptiveCompanion, Log, TEXT("Companion navigation hint %s after %.1f s%s%s."), *Phase.ToString(), InstanceData.ElapsedTime,
		InstanceData.Detail.IsEmpty() ? TEXT("") : TEXT(": "), *InstanceData.Detail);
}

#if WITH_EDITOR
FText FCompanionNavigationHintTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting /*= EStateTreeNodeFormatting::Text*/) const
{
	const FInstanceDataType* InstanceData = InstanceDataView.GetPtr<FInstanceDataType>();
	return FText::FromString(FString::Printf(TEXT("<b>Navigation Hint</b> %.0f cm ahead"), InstanceData ? InstanceData->LeadDistance : 0.0f));
}
#endif // WITH_EDITOR

////////////////////////////////////////////////////////////////////

bool FCompanionPlayerDistanceCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	const APawn* Companion = InstanceData.Controller ? InstanceData.Controller->GetPawn() : nullptr;
	const APawn* Player = UGameplayStatics::GetPlayerPawn(InstanceData.Controller, 0);
	if (!Companion || !Player)
	{
		return false;
	}

	return FVector::Dist(Companion->GetActorLocation(), Player->GetActorLocation()) > InstanceData.Distance;
}

#if WITH_EDITOR
FText FCompanionPlayerDistanceCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting /*= EStateTreeNodeFormatting::Text*/) const
{
	const FInstanceDataType* InstanceData = InstanceDataView.GetPtr<FInstanceDataType>();
	return FText::FromString(FString::Printf(TEXT("<b>Player Further Than</b> %.0f cm"), InstanceData ? InstanceData->Distance : 0.0f));
}
#endif // WITH_EDITOR

////////////////////////////////////////////////////////////////////

bool FCompanionHiddenFromPlayerCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	const ACharacter* Companion = InstanceData.Controller ? InstanceData.Controller->GetPawn<ACharacter>() : nullptr;
	const APlayerController* PlayerController = UGameplayStatics::GetPlayerController(InstanceData.Controller, 0);
	if (!Companion || !PlayerController)
	{
		return false;
	}

	return !CompanionVisibility::IsVisibleToPlayer(*PlayerController, *Companion, Companion->GetActorLocation(), Companion->GetSimpleCollisionHalfHeight(), InstanceData.ViewMarginAngle);
}

#if WITH_EDITOR
FText FCompanionHiddenFromPlayerCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting /*= EStateTreeNodeFormatting::Text*/) const
{
	return FText::FromString("<b>Companion Hidden From Player</b>");
}
#endif // WITH_EDITOR

////////////////////////////////////////////////////////////////////

bool FCompanionIsCurrentInterventionCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	const ACompanionAIController* Controller = Cast<ACompanionAIController>(InstanceData.Controller);
	return Controller && !InstanceData.Intervention.IsNone() && Controller->GetCurrentIntervention() == InstanceData.Intervention;
}

#if WITH_EDITOR
FText FCompanionIsCurrentInterventionCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting /*= EStateTreeNodeFormatting::Text*/) const
{
	const FInstanceDataType* InstanceData = InstanceDataView.GetPtr<FInstanceDataType>();
	return FText::FromString(FString::Printf(TEXT("<b>Is Current Intervention</b> %s"), InstanceData ? *InstanceData->Intervention.ToString() : TEXT("")));
}
#endif // WITH_EDITOR

////////////////////////////////////////////////////////////////////

EStateTreeRunStatus FCompanionReportFailedTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	const ACompanionAIController* Controller = Cast<ACompanionAIController>(InstanceData.Controller);
	const FName Intervention = Controller ? Controller->GetCurrentIntervention() : NAME_None;

	if (UAdaptivePipelineSubsystem* Pipeline = UAdaptivePipelineSubsystem::Get(InstanceData.Controller))
	{
		Pipeline->ReportInterventionPhase(Intervention, AdaptiveInterventionPhase::Failed, InstanceData.Reason);
	}

	UE_LOG(LogAdaptiveCompanion, Warning, TEXT("Companion intervention '%s' failed: %s."), *Intervention.ToString(), *InstanceData.Reason);
	return EStateTreeRunStatus::Failed;
}

#if WITH_EDITOR
FText FCompanionReportFailedTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting /*= EStateTreeNodeFormatting::Text*/) const
{
	return FText::FromString("<b>Report Intervention Failed</b>");
}
#endif // WITH_EDITOR
