#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Pipeline/AdaptivePipelineFrame.h"
#include "AdaptivePipelineStage.generated.h"

class UAdaptiveDebugSubsystem;
class UAdaptiveLogSubsystem;
class UAdaptivePipelineSubsystem;

/**
 *  Base class of the six pipeline stages. Each stage is a separate object, picked and configured
 *  in the pipeline config asset (UAdaptivePipelineConfig), so it can be replaced by a subclass
 *  without touching the other stages.
 *
 *  The objects in the config asset are templates: every session creates its own copies, so stages
 *  can keep state in member variables.
 */
UCLASS(Abstract, EditInlineNew, DefaultToInstanced, CollapseCategories)
class ADAPTIVECOMPANIONFRAMEWORK_API UAdaptivePipelineStage : public UObject
{
	GENERATED_BODY()

public:

	/** Called once when the session starts, before the first update */
	virtual void Initialize(UAdaptivePipelineSubsystem& InPipeline);

	/** Called once when the session ends */
	virtual void Deinitialize();

	/** Called at the pipeline's update rate, in stage order. Reads earlier stages' output from Frame and adds its own */
	virtual void Update(FAdaptivePipelineFrame& Frame);

	/** The pipeline this stage runs in, nullptr outside a session (e.g. in tests) */
	UAdaptivePipelineSubsystem* GetPipeline() const { return Pipeline; }

	virtual UWorld* GetWorld() const override;

protected:

	/** The session log, nullptr outside a session or if logging is disabled */
	UAdaptiveLogSubsystem* GetLog() const;

	/** The debug overlay, nullptr outside a session */
	UAdaptiveDebugSubsystem* GetDebug() const;

private:

	UPROPERTY(Transient)
	TObjectPtr<UAdaptivePipelineSubsystem> Pipeline;
};
