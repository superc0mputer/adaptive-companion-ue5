#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

/** Log category for everything inside the Adaptive Companion Framework plugin */
ADAPTIVECOMPANIONFRAMEWORK_API DECLARE_LOG_CATEGORY_EXTERN(LogAdaptiveCompanionFramework, Log, All);

class FAdaptiveCompanionFrameworkModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
