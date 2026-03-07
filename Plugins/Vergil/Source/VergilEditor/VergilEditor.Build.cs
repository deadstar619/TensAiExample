using UnrealBuildTool;

public class VergilEditor : ModuleRules
{
	public VergilEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"DeveloperSettings",
				"EditorSubsystem",
				"Engine",
				"VergilCore",
				"VergilBlueprintModel",
				"VergilBlueprintCompiler"
			});

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"AIGraph",
				"AIModule",
				"AnimGraph",
				"AnimGraphRuntime",
				"BlueprintGraph",
				"Kismet",
				"Projects",
				"Slate",
				"SlateCore",
				"UnrealEd"
			});
	}
}
