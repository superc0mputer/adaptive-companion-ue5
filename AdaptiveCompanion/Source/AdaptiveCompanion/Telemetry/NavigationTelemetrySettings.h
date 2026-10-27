#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "NavigationTelemetrySettings.generated.h"

/**
 *  Tuning of the navigation signals UNavigationTelemetrySubsystem reports
 *  (Project Settings > Game > Navigation Telemetry)
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Navigation Telemetry"))
class ADAPTIVECOMPANION_API UNavigationTelemetrySettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:

	UNavigationTelemetrySettings();

	/** Size of the grid cells used to detect revisited areas */
	UPROPERTY(Config, EditAnywhere, Category = "Revisits", meta = (ClampMin = "100", Units = "cm"))
	float CellSize = 500.0f;

	/** A cell only counts as revisited if the player was out of it for at least this long, so walking along a cell border doesn't count */
	UPROPERTY(Config, EditAnywhere, Category = "Revisits", meta = (ClampMin = "0", Units = "s"))
	float RevisitMinAbsence = 5.0f;

	/** Below this horizontal speed the player counts as standing still, and the heading alignment is 0 */
	UPROPERTY(Config, EditAnywhere, Category = "Heading", meta = (ClampMin = "0", Units = "cm/s"))
	float MinMovingSpeed = 50.0f;
};
