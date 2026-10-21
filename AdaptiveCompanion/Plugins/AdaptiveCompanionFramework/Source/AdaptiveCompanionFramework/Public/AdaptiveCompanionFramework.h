#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Stats/Stats.h"

/** Log category for everything inside the Adaptive Companion Framework plugin */
ADAPTIVECOMPANIONFRAMEWORK_API DECLARE_LOG_CATEGORY_EXTERN(LogAdaptiveCompanionFramework, Log, All);

/** Stat group for the framework's per-frame cost: stat AdaptiveCompanion */
DECLARE_STATS_GROUP(TEXT("AdaptiveCompanion"), STATGROUP_AdaptiveCompanion, STATCAT_Advanced);

class FAdaptiveCompanionFrameworkModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
