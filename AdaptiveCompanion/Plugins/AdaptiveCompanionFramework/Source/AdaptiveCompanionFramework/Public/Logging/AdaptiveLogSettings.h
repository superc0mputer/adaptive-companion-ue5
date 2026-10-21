#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "AdaptiveLogSettings.generated.h"

/**
 *  Session log settings (Project Settings > Plugins > Adaptive Companion Logging)
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Adaptive Companion Logging"))
class ADAPTIVECOMPANIONFRAMEWORK_API UAdaptiveLogSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:

	UAdaptiveLogSettings();

	/** If true, every play session writes one JSON Lines file */
	UPROPERTY(Config, EditAnywhere, Category = "Session Log")
	bool bEnabled = true;

	/** Folder for the log files. Relative paths are relative to the project's Saved folder */
	UPROPERTY(Config, EditAnywhere, Category = "Session Log", meta = (EditCondition = "bEnabled"))
	FString LogDirectory = TEXT("AdaptiveLogs");

	/** Participant ID written into every record. Overridden by -ParticipantId=<id> on the command line */
	UPROPERTY(Config, EditAnywhere, Category = "Session Log", meta = (EditCondition = "bEnabled"))
	FString DefaultParticipantId = TEXT("none");

	/** Pending records are handed to a background write at this interval */
	UPROPERTY(Config, EditAnywhere, Category = "Session Log", meta = (EditCondition = "bEnabled", ClampMin = "0.1", Units = "s"))
	float FlushInterval = 1.0f;

	/** Pending records are also written as soon as they exceed this size, to bound memory */
	UPROPERTY(Config, EditAnywhere, Category = "Session Log", meta = (EditCondition = "bEnabled", ClampMin = "1", Units = "KB"))
	int32 MaxPendingKB = 256;

	/** Absolute folder the log files are written to */
	FString GetLogDirectoryPath() const;
};
