#include "CameraInputTelemetrySubsystem.h"
#include "GameplayContextSettings.h"
#include "CombatEnemy.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "Pipeline/AdaptivePipelineSubsystem.h"

bool UCameraInputTelemetrySubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UCameraInputTelemetrySubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	const float Interval = GetDefault<UGameplayContextSettings>()->CheckInterval;
	InWorld.GetTimerManager().SetTimer(UpdateTimer, this, &UCameraInputTelemetrySubsystem::UpdateTelemetry, Interval, true, 0.0f);
}

void UCameraInputTelemetrySubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(UpdateTimer);
	}

	// the pipeline outlives the level, so don't leave the last level's camera behind
	Report(0.0, 0.0, 0, 0.0);

	Super::Deinitialize();
}

void UCameraInputTelemetrySubsystem::UpdateTelemetry()
{
	// input doesn't need a camera, so idle time is measured even while there is none
	const double IdleTime = MeasureIdleTime();

	const APlayerController* Controller = GetWorld()->GetFirstPlayerController();
	const APlayerCameraManager* Camera = Controller ? Controller->PlayerCameraManager.Get() : nullptr;
	if (!Camera)
	{
		bHasLastView = false;
		Report(0.0, 0.0, 0, IdleTime);
		return;
	}

	// the camera, not the control rotation: it is what the player actually sees, camera lag included
	const FVector CameraLocation = Camera->GetCameraLocation();
	const FRotator CameraRotation = Camera->GetCameraRotation();
	const FVector ViewDirection = CameraRotation.Vector();

	const double TurnRate = MeasureTurnRate(ViewDirection);

	double ObjectiveAngle = 0.0;
	FName ObjectiveName;
	FVector ObjectiveLocation;
	const UAdaptivePipelineSubsystem* Pipeline = UAdaptivePipelineSubsystem::Get(GetWorld());
	if (Pipeline && Pipeline->GetCurrentObjective(ObjectiveName, ObjectiveLocation))
	{
		// horizontal only: looking up or down at a distant objective doesn't mean looking away from it
		const FVector Forward = (ViewDirection * FVector(1.0, 1.0, 0.0)).GetSafeNormal();
		const FVector ToObjective = ((ObjectiveLocation - CameraLocation) * FVector(1.0, 1.0, 0.0)).GetSafeNormal();
		if (!Forward.IsZero() && !ToObjective.IsZero())
		{
			ObjectiveAngle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Forward, ToObjective), -1.0, 1.0)));
		}
	}

	const int32 EnemiesOutOfView = CountEnemiesOutOfView(Controller, CameraLocation, CameraRotation, Camera->GetFOVAngle());

	Report(TurnRate, ObjectiveAngle, EnemiesOutOfView, IdleTime);
}

double UCameraInputTelemetrySubsystem::MeasureTurnRate(const FVector& ViewDirection)
{
	// world time, so pausing doesn't count as looking around slowly
	const double Now = GetWorld()->GetTimeSeconds();
	double TurnRate = 0.0;

	const double Elapsed = Now - LastViewTime;
	if (bHasLastView && Elapsed > UE_KINDA_SMALL_NUMBER)
	{
		const double Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(LastViewDirection, ViewDirection), -1.0, 1.0)));
		TurnRate = Angle / Elapsed;
	}

	LastViewDirection = ViewDirection;
	LastViewTime = Now;
	bHasLastView = true;
	return TurnRate;
}

int32 UCameraInputTelemetrySubsystem::CountEnemiesOutOfView(const APlayerController* Controller, const FVector& CameraLocation, const FRotator& CameraRotation, float FOV) const
{
	const APawn* Player = Controller->GetPawn();
	if (!Player)
	{
		return 0;
	}

	// the camera's FOV is horizontal; the vertical one follows from the viewport's aspect ratio
	int32 ViewportX = 0;
	int32 ViewportY = 0;
	Controller->GetViewportSize(ViewportX, ViewportY);
	const double AspectRatio = ViewportX > 0 && ViewportY > 0 ? double(ViewportY) / ViewportX : 9.0 / 16.0;
	const double HalfWidth = FMath::Tan(FMath::DegreesToRadians(FOV * 0.5));
	const double HalfHeight = HalfWidth * AspectRatio;

	const float Radius = GetDefault<UGameplayContextSettings>()->CombatRadius;
	const FVector PlayerLocation = Player->GetActorLocation();
	int32 Count = 0;

	for (TActorIterator<ACombatEnemy> It(GetWorld()); It; ++It)
	{
		// same "nearby" as Combat.EnemiesNearby: living, and within the radius of the player
		if (It->CurrentHP <= 0.0f || FVector::Dist(It->GetActorLocation(), PlayerLocation) > Radius)
		{
			continue;
		}

		// in camera space X points forward, Y right and Z up; the center has to be inside the frustum.
		// Walls aren't checked: an enemy behind a wall in front of the player is still "in view"
		const FVector Local = CameraRotation.UnrotateVector(It->GetActorLocation() - CameraLocation);
		const bool bInView = Local.X > 0.0 && FMath::Abs(Local.Y) <= Local.X * HalfWidth && FMath::Abs(Local.Z) <= Local.X * HalfHeight;
		if (!bInView)
		{
			++Count;
		}
	}

	return Count;
}

double UCameraInputTelemetrySubsystem::MeasureIdleTime()
{
	if (!FSlateApplication::IsInitialized())
	{
		return 0.0;
	}

	// Slate updates this on every key, mouse button, mouse move, wheel and gamepad input beyond the dead zone
	const FSlateApplication& Slate = FSlateApplication::Get();
	return FMath::Max(Slate.GetCurrentTime() - Slate.GetLastUserInteractionTime(), 0.0);
}

void UCameraInputTelemetrySubsystem::Report(double TurnRate, double ObjectiveAngle, int32 EnemiesOutOfView, double IdleTime) const
{
	// values are sampled and only logged on change, so this can run at every check
	if (UAdaptivePipelineSubsystem* Pipeline = UAdaptivePipelineSubsystem::Get(GetWorld()))
	{
		// rounded, so camera lag and tiny mouse moves don't log a new value at every check
		Pipeline->ReportValue(CameraInputTelemetry::TurnRate, FMath::RoundToDouble(TurnRate / 5.0) * 5.0);
		Pipeline->ReportValue(CameraInputTelemetry::ObjectiveAngle, FMath::RoundToDouble(ObjectiveAngle));
		Pipeline->ReportValue(CameraInputTelemetry::EnemiesOutOfView, EnemiesOutOfView);

		// whole seconds: idle time grows at every check, and no rule needs it finer than that
		Pipeline->ReportValue(CameraInputTelemetry::IdleTime, FMath::FloorToDouble(IdleTime));
	}
}
