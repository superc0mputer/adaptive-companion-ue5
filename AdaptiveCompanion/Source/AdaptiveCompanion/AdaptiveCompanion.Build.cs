// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class AdaptiveCompanion : ModuleRules
{
	public AdaptiveCompanion(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate",
			"GameplayTags",
			"NavigationSystem",
			"DeveloperSettings",
			"AdaptiveCompanionFramework"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"AdaptiveCompanion",
			"AdaptiveCompanion/Companion",
			"AdaptiveCompanion/Context",
			"AdaptiveCompanion/Navigation",
			"AdaptiveCompanion/Telemetry",
			"AdaptiveCompanion/Variant_Combat",
			"AdaptiveCompanion/Variant_Combat/AI",
			"AdaptiveCompanion/Variant_Combat/Animation",
			"AdaptiveCompanion/Variant_Combat/Gameplay",
			"AdaptiveCompanion/Variant_Combat/Interfaces",
			"AdaptiveCompanion/Variant_Combat/UI",
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
