#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "Pipeline/AdaptivePipelineFrame.h"
#include "AdaptivePipelineSubsystem.generated.h"

class UAdaptiveBehaviorStage;
class UAdaptiveContextStage;
class UAdaptiveDebugSubsystem;
class UAdaptiveLogSubsystem;
class UAdaptivePipelineConfig;
class UAdaptivePipelineStage;
class UAdaptiveTelemetryStage;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAdaptiveInterventionDelegate, FName, Intervention);

/**
 *  The framework's core loop. Runs the six pipeline stages in order at a fixed rate (not every
 *  frame), and is the API the game reports telemetry through:
 *
 *    ReportEvent("Combat.DamageTaken", 25)   something happened, optionally with a magnitude
 *    ReportValue("Player.Health", 0.4)       current value of something continuous
 *
 *    SetContext("Context.Combat", true)      what the player is currently doing, several at once
 *    SetObjective("Objective.2", Location)   where the player currently has to go
 *
 *  The selected interventions come back to the game through OnIntervention; the game carries them
 *  out and reports their phases with ReportInterventionPhase.
 *
 *  Names are plain FNames chosen by the game, so the framework needs no game types. Update rate
 *  and stage classes come from the config asset selected in UAdaptivePipelineSettings.
 *  One pipeline per game instance; it keeps running across level changes.
 */
UCLASS()
class ADAPTIVECOMPANIONFRAMEWORK_API UAdaptivePipelineSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:

	/** Returns the pipeline of the context object's game instance, or nullptr if the pipeline is disabled */
	static UAdaptivePipelineSubsystem* Get(const UObject* WorldContextObject);

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Reports a discrete event, e.g. damage taken (Value = damage) or a death (Value = 1). Logged right away */
	UFUNCTION(BlueprintCallable, Category = "Adaptive Companion|Telemetry")
	void ReportEvent(FName Name, double Value = 1.0);

	/** Reports the current value of a continuous signal, e.g. health. Only the latest value per update counts, so calling it every frame is fine */
	UFUNCTION(BlueprintCallable, Category = "Adaptive Companion|Telemetry")
	void ReportValue(FName Name, double Value);

	/** Activates or deactivates a gameplay context, e.g. "Context.Combat" while enemies are near. Several can be active at once. Logged right away; repeated calls with the same state are ignored */
	UFUNCTION(BlueprintCallable, Category = "Adaptive Companion|Context")
	void SetContext(FName Name, bool bActive);

	/** True if the game reported the context as active */
	UFUNCTION(BlueprintPure, Category = "Adaptive Companion|Context")
	bool IsContextActive(FName Name) const;

	/** Sets the objective the player currently works toward, e.g. the next place to reach. Logged right away; repeated calls with the same name and location are ignored */
	UFUNCTION(BlueprintCallable, Category = "Adaptive Companion|Context")
	void SetObjective(FName Name, FVector Location);

	/** The player has no objective (any more), e.g. after reaching the last one */
	UFUNCTION(BlueprintCallable, Category = "Adaptive Companion|Context")
	void ClearObjective();

	/** The objective the game reported last. Returns false if there is none */
	UFUNCTION(BlueprintPure, Category = "Adaptive Companion|Context")
	bool GetCurrentObjective(FName& Name, FVector& Location) const;

	/** Called when the pipeline selected an intervention. The game carries it out, e.g. by sending its companion an event */
	UPROPERTY(BlueprintAssignable, Category = "Adaptive Companion|Behavior")
	FAdaptiveInterventionDelegate OnIntervention;

	/** Reports a phase of an intervention received through OnIntervention, e.g. "started" or "ended". Logged as an intervention record */
	UFUNCTION(BlueprintCallable, Category = "Adaptive Companion|Behavior")
	void ReportInterventionPhase(FName Intervention, FName Phase);

	/** Runs all stages once, now. Normally called at the configured rate */
	void Update();

	/** Session time in seconds, the same clock as "t" in the session log */
	double GetTime() const;

	UFUNCTION(BlueprintPure, Category = "Adaptive Companion|Pipeline")
	float GetUpdateRate() const;

	const UAdaptivePipelineConfig* GetConfig() const { return Config; }

	/** The stages in update order */
	const TArray<TObjectPtr<UAdaptivePipelineStage>>& GetStages() const { return Stages; }

	/** The frame of the most recent update */
	const FAdaptivePipelineFrame& GetLastFrame() const { return Frame; }

	/** nullptr if logging is disabled */
	UAdaptiveLogSubsystem* GetLog() const { return Log; }
	UAdaptiveDebugSubsystem* GetDebug() const { return Debug; }

protected:

	bool Tick(float DeltaTime);

	template <typename TStage>
	TStage* CreateStage(TStage* Template);

	UPROPERTY()
	TObjectPtr<const UAdaptivePipelineConfig> Config;

	UPROPERTY()
	TArray<TObjectPtr<UAdaptivePipelineStage>> Stages;

	UPROPERTY()
	TObjectPtr<UAdaptiveTelemetryStage> Telemetry;

	UPROPERTY()
	TObjectPtr<UAdaptiveContextStage> Context;

	UPROPERTY()
	TObjectPtr<UAdaptiveBehaviorStage> Behavior;

	UPROPERTY()
	TObjectPtr<UAdaptiveLogSubsystem> Log;

	UPROPERTY()
	TObjectPtr<UAdaptiveDebugSubsystem> Debug;

	FAdaptivePipelineFrame Frame;
	FAdaptiveFixedRate Rate;

	int64 UpdateCount = 0;
	double LastUpdateTime = 0.0;

	/** FPlatformTime::Seconds() at start, only used when logging is disabled */
	double StartSeconds = 0.0;

	FTSTicker::FDelegateHandle TickerHandle;
};
