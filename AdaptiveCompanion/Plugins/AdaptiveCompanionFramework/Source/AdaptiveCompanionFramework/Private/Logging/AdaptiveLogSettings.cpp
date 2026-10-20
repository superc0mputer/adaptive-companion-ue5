#include "Logging/AdaptiveLogSettings.h"
#include "Misc/Paths.h"

UAdaptiveLogSettings::UAdaptiveLogSettings()
{
	CategoryName = TEXT("Plugins");
}

FString UAdaptiveLogSettings::GetLogDirectoryPath() const
{
	const FString Directory = LogDirectory.IsEmpty() ? TEXT("AdaptiveLogs") : LogDirectory;
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir(), Directory);
}
