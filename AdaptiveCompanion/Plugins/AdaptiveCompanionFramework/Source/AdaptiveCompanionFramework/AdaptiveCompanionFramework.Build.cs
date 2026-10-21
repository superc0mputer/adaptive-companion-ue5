using UnrealBuildTool;

public class AdaptiveCompanionFramework : ModuleRules
{
	public AdaptiveCompanionFramework(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// The framework must stay game-independent: never add the game module here.
		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"GameplayTags",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"DeveloperSettings",
			"Json",
			"JsonUtilities"
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"EngineSettings",
			"Projects"
		});
	}
}
