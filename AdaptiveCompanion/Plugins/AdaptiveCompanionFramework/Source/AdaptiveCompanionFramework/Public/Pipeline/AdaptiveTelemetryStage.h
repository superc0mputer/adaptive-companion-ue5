#pragma once

#include "CoreMinimal.h"
#include "Pipeline/AdaptivePipelineStage.h"
#include "AdaptiveTelemetryStage.generated.h"

/**
 *  1 Telemetry: collects what the game reports through UAdaptivePipelineSubsystem::ReportEvent /
 *  ReportValue and hands it to the next stages once per update.
 *
 *  - Events are logged as signal records the moment they are reported, and passed on in the next
 *    update's Frame.Events.
 *  - Values are sampled: only the latest value per name counts. Frame.Values holds all of them
 *    every update; the log gets a record whenever a value changed (or every update, see
 *    bLogEveryUpdate).
 */
UCLASS(meta = (DisplayName = "Telemetry Stage"))
class ADAPTIVECOMPANIONFRAMEWORK_API UAdaptiveTelemetryStage : public UAdaptivePipelineStage
{
	GENERATED_BODY()

public:

	/** If true, every value is logged at every update; otherwise only when it changed since it was last logged */
	UPROPERTY(EditAnywhere, Category = "Telemetry")
	bool bLogEveryUpdate = false;

	virtual void ReportEvent(FName Name, double Value);
	virtual void ReportValue(FName Name, double Value);

	virtual void Update(FAdaptivePipelineFrame& Frame) override;

protected:

	void LogSignal(const TCHAR* Kind, FName Name, double Value) const;

	/** Events reported since the last update */
	TArray<FAdaptiveTelemetryEvent> PendingEvents;

	TMap<FName, double> Values;

	/** Value of each name when it was last logged */
	TMap<FName, double> LoggedValues;

	/** Events per name since the session started, for the overlay */
	TMap<FName, int32> EventCounts;
};
