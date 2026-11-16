#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CameraInputTelemetrySubsystem.generated.h"

class APlayerController;

namespace CameraInputTelemetry
{
	/** Angular speed of the camera's view direction in deg/s, rounded to 5 deg/s */
	inline const FName TurnRate = TEXT("Camera.TurnRate");

	/** Horizontal angle between the camera's view direction and the direction to the current objective (0..180 deg), 0 without an objective */
	inline const FName ObjectiveAngle = TEXT("Camera.ObjectiveAngle");

	/** Living enemies within the combat radius whose center is outside the camera's view */
	inline const FName EnemiesOutOfView = TEXT("Combat.EnemiesOutOfView");

	/** Whole seconds since the last player input of any kind (keys, mouse, gamepad) */
	inline const FName IdleTime = TEXT("Input.IdleTime");
}

/**
 *  Reports how the player looks around and whether they give any input to the pipeline, in every
 *  game level without placing anything in it.
 *
 *  Reads the player's camera manager instead of the character, so it works with every pawn class
 *  (the combat character and the default third-person character). The objective is the one the
 *  game reported to the framework (UAdaptivePipelineSubsystem::GetCurrentObjective), "nearby"
 *  uses the combat radius of UGameplayContextSettings, and input comes from Slate's last user
 *  interaction time, so no input binding knows about telemetry.
 *
 *  All four values are reported at every check of UGameplayContextSettings and reset to 0 when the
 *  level ends.
 */
UCLASS()
class ADAPTIVECOMPANION_API UCameraInputTelemetrySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:

	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	/** Enemies out of view at the last check, e.g. for the companion's threat call-out */
	int32 GetEnemiesOutOfView() const { return LastEnemiesOutOfView; }

protected:

	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	void UpdateTelemetry();

	/** Degrees per second the view direction turned since the last check */
	double MeasureTurnRate(const FVector& ViewDirection);

	/** Living enemies within the combat radius of the player that are outside the camera's view frustum */
	int32 CountEnemiesOutOfView(const APlayerController* Controller, const FVector& CameraLocation, const FRotator& CameraRotation, float FOV) const;

	static double MeasureIdleTime();

	void Report(double TurnRate, double ObjectiveAngle, int32 EnemiesOutOfView, double IdleTime) const;

	FTimerHandle UpdateTimer;

	/** View direction and world time at the last check, for the turn rate */
	FVector LastViewDirection = FVector::ZeroVector;
	double LastViewTime = 0.0;
	bool bHasLastView = false;

	int32 LastEnemiesOutOfView = 0;
};
