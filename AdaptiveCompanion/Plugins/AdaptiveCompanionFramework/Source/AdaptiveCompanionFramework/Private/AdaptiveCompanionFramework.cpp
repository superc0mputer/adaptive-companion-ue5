#include "AdaptiveCompanionFramework.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogAdaptiveCompanionFramework);

void FAdaptiveCompanionFrameworkModule::StartupModule()
{
	UE_LOG(LogAdaptiveCompanionFramework, Log, TEXT("Adaptive Companion Framework module started."));
}

void FAdaptiveCompanionFrameworkModule::ShutdownModule()
{
	UE_LOG(LogAdaptiveCompanionFramework, Log, TEXT("Adaptive Companion Framework module shut down."));
}

IMPLEMENT_MODULE(FAdaptiveCompanionFrameworkModule, AdaptiveCompanionFramework)
