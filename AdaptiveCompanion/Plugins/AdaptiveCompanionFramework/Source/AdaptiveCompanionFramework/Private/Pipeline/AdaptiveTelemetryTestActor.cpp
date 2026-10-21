#include "Pipeline/AdaptiveTelemetryTestActor.h"
#include "Pipeline/AdaptivePipelineSubsystem.h"
#include "Components/SceneComponent.h"

AAdaptiveTelemetryTestActor::AAdaptiveTelemetryTestActor()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AAdaptiveTelemetryTestActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	Age += DeltaSeconds;

	UAdaptivePipelineSubsystem* Pipeline = UAdaptivePipelineSubsystem::Get(this);
	if (!Pipeline)
	{
		return;
	}

	// reported every frame on purpose: the pipeline only samples it at its own rate
	Pipeline->ReportValue(ValueName, 0.5 + 0.5 * FMath::Sin(UE_TWO_PI * Age / WavePeriod));

	if (Age >= NextEventAge)
	{
		NextEventAge = Age + EventInterval;
		Pipeline->ReportEvent(EventName, ++EventCount);
	}
}
