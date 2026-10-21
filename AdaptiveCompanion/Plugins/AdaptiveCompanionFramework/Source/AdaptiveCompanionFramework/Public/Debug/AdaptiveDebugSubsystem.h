#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "Debug/AdaptiveDebugData.h"
#include "AdaptiveDebugSubsystem.generated.h"

class APlayerController;
class UCanvas;

/**
 *  Debug overlay that shows the pipeline's live values while playing, one section per stage.
 *  Toggled with the console variable ac.Debug (0/1). Stages push their values every update with
 *  SetBar / SetNumber / SetText; values in [0, 1] are drawn as bars.
 *
 *  The overlay only draws on the debug canvas: it spawns no actors, takes no input and changes no
 *  game state, so hiding it has no effect on gameplay. Values are stored while it is hidden too,
 *  so it shows the latest state as soon as it is turned on.
 */
UCLASS()
class ADAPTIVECOMPANIONFRAMEWORK_API UAdaptiveDebugSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:

	/** Returns the overlay of the context object's game instance, or nullptr */
	static UAdaptiveDebugSubsystem* Get(const UObject* WorldContextObject);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Shows a value in [0, 1] (clamped) as a bar */
	UFUNCTION(BlueprintCallable, Category = "Adaptive Companion|Debug")
	void SetBar(EAdaptiveDebugSection Section, const FString& Name, double Value);

	/** Shows any number as text */
	UFUNCTION(BlueprintCallable, Category = "Adaptive Companion|Debug")
	void SetNumber(EAdaptiveDebugSection Section, const FString& Name, double Value);

	UFUNCTION(BlueprintCallable, Category = "Adaptive Companion|Debug")
	void SetText(EAdaptiveDebugSection Section, const FString& Name, const FString& Text);

	UFUNCTION(BlueprintCallable, Category = "Adaptive Companion|Debug")
	void RemoveValue(EAdaptiveDebugSection Section, const FString& Name);

	UFUNCTION(BlueprintCallable, Category = "Adaptive Companion|Debug")
	void ClearSection(EAdaptiveDebugSection Section);

	UFUNCTION(BlueprintCallable, Category = "Adaptive Companion|Debug")
	void ClearAll();

	/** True while ac.Debug is on. Stages can skip work that only feeds the overlay */
	UFUNCTION(BlueprintPure, Category = "Adaptive Companion|Debug")
	static bool IsOverlayVisible();

	/** Sets ac.Debug, which applies to every game instance */
	UFUNCTION(BlueprintCallable, Category = "Adaptive Companion|Debug")
	static void SetOverlayVisible(bool bVisible);

	/** Fills every section with animated test values (ac.Debug.Demo) */
	void SetDemoRunning(bool bRunning);
	bool IsDemoRunning() const { return DemoTickerHandle.IsValid(); }

	const FAdaptiveDebugData& GetData() const { return Data; }

	/** Parses a section from its name ("Features", case-insensitive) or number (1-6). Public for tests */
	static bool ParseSection(const FString& Text, EAdaptiveDebugSection& OutSection);

	/** Header text of a section, e.g. "2 Features" */
	static const TCHAR* GetSectionTitle(EAdaptiveDebugSection Section);

protected:

	void DrawOverlay(UCanvas* Canvas, APlayerController* PlayerController);

	bool DemoTick(float DeltaTime);

	FAdaptiveDebugData Data;

	FDelegateHandle DrawHandle;
	FTSTicker::FDelegateHandle DemoTickerHandle;
	double DemoStartSeconds = 0.0;
};
