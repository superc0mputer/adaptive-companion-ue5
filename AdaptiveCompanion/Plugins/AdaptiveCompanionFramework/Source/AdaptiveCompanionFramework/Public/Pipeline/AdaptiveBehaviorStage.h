#pragma once

#include "CoreMinimal.h"
#include "Pipeline/AdaptivePipelineStage.h"
#include "AdaptiveBehaviorStage.generated.h"

/**
 *  6 Behavior execution. Hands the selected intervention to the game through
 *  UAdaptivePipelineSubsystem::OnIntervention, and records what the game reports back about it
 *  (UAdaptivePipelineSubsystem::ReportInterventionPhase). The framework doesn't know what an
 *  intervention does; the game's companion carries it out.
 *
 *  Gets status tracking (running, finished, interrupted) in #19.
 */
UCLASS(meta = (DisplayName = "Behavior Stage"))
class ADAPTIVECOMPANIONFRAMEWORK_API UAdaptiveBehaviorStage : public UAdaptivePipelineStage
{
	GENERATED_BODY()

public:

	virtual void Update(FAdaptivePipelineFrame& Frame) override;

	/** Logs a phase of an intervention reported by the game, e.g. "started" or "ended" */
	virtual void ReportPhase(FName Intervention, FName Phase);

protected:

	void LogPhase(FName Intervention, const FString& Phase, int32 Receivers = INDEX_NONE) const;
};
