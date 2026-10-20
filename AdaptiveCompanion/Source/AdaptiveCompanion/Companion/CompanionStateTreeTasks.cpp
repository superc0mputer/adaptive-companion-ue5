#include "CompanionStateTreeTasks.h"
#include "CompanionCharacter.h"
#include "CompanionVisibility.h"
#include "StateTreeExecutionContext.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "AdaptiveCompanion.h"

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

	if (InstanceData.Controller)
	{
		InstanceData.Controller->StopMovement();
	}

	UE_LOG(LogAdaptiveCompanion, Log, TEXT("Companion intervention started (placeholder)."));
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FCompanionInterventionPlaceholderTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	InstanceData.ElapsedTime += DeltaTime;

	return InstanceData.ElapsedTime >= InstanceData.Duration ? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Running;
}

void FCompanionInterventionPlaceholderTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	UE_LOG(LogAdaptiveCompanion, Log, TEXT("Companion intervention ended (placeholder)."));
}

#if WITH_EDITOR
FText FCompanionInterventionPlaceholderTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting /*= EStateTreeNodeFormatting::Text*/) const
{
	return FText::FromString("<b>Intervention Placeholder</b>");
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
