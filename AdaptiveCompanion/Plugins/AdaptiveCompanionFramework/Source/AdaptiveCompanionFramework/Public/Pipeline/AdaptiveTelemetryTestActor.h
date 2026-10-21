#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AdaptiveTelemetryTestActor.generated.h"

/**
 *  Reports test telemetry through the generic API: a continuous sine value every frame and an
 *  event at a fixed interval. Shows that reported values reach the session log and the overlay.
 *  Spawned next to the player with ac.Pipeline.TestActor, or placed in a level.
 */
UCLASS()
class ADAPTIVECOMPANIONFRAMEWORK_API AAdaptiveTelemetryTestActor : public AActor
{
	GENERATED_BODY()

public:

	AAdaptiveTelemetryTestActor();

	virtual void Tick(float DeltaSeconds) override;

	/** Reported every frame, between 0 and 1 */
	UPROPERTY(EditAnywhere, Category = "Telemetry Test")
	FName ValueName = TEXT("Test.Wave");

	UPROPERTY(EditAnywhere, Category = "Telemetry Test", meta = (ClampMin = "0.1", Units = "s"))
	float WavePeriod = 4.f;

	/** Reported every EventInterval seconds, with the number of events so far as value */
	UPROPERTY(EditAnywhere, Category = "Telemetry Test")
	FName EventName = TEXT("Test.Pulse");

	UPROPERTY(EditAnywhere, Category = "Telemetry Test", meta = (ClampMin = "0.1", Units = "s"))
	float EventInterval = 2.f;

protected:

	float Age = 0.f;
	float NextEventAge = 0.f;
	int32 EventCount = 0;
};
