#include "Pipeline/AdaptivePipelineSettings.h"
#include "Pipeline/AdaptivePipelineConfig.h"
#include "AdaptiveCompanionFramework.h"

UAdaptivePipelineSettings::UAdaptivePipelineSettings()
{
	CategoryName = TEXT("Plugins");
}

const UAdaptivePipelineConfig* UAdaptivePipelineSettings::LoadConfig() const
{
	if (Config.IsNull())
	{
		UE_LOG(LogAdaptiveCompanionFramework, Display, TEXT("No pipeline config selected in the project settings, using the defaults."));
		return GetDefault<UAdaptivePipelineConfig>();
	}

	if (const UAdaptivePipelineConfig* Loaded = Config.LoadSynchronous())
	{
		return Loaded;
	}

	UE_LOG(LogAdaptiveCompanionFramework, Warning, TEXT("Pipeline config '%s' could not be loaded, using the defaults."), *Config.ToString());
	return GetDefault<UAdaptivePipelineConfig>();
}
