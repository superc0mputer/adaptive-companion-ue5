#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayContextSettings.generated.h"

/**
 *  How the prototype decides which gameplay context it reports to the pipeline
 *  (Project Settings > Game > Gameplay Context)
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Gameplay Context"))
class ADAPTIVECOMPANION_API UGameplayContextSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:

	UGameplayContextSettings();

	/** The player is in combat while a living enemy is within this distance */
	UPROPERTY(Config, EditAnywhere, Category = "Combat", meta = (ClampMin = "0", Units = "cm"))
	float CombatRadius = 1500.0f;

	/** Combat ends only after no enemy has been within the radius for this long, so it doesn't flicker at the edge */
	UPROPERTY(Config, EditAnywhere, Category = "Combat", meta = (ClampMin = "0", Units = "s"))
	float CombatLingerTime = 3.0f;

	/** How often the context is checked */
	UPROPERTY(Config, EditAnywhere, Category = "Detection", meta = (ClampMin = "0.02", Units = "s"))
	float CheckInterval = 0.1f;
};
