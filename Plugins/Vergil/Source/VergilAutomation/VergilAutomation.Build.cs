using UnrealBuildTool;

public class VergilAutomation : ModuleRules
{
	public VergilAutomation(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"AIGraph",
				"AIModule",
				"AnimGraph",
				"AnimGraphRuntime",
				"BlueprintGraph",
				"Core",
				"CoreUObject",
				"Engine",
				"UnrealEd",
				"VergilCore",
				"VergilBlueprintModel",
				"VergilBlueprintCompiler",
				"VergilEditor",
				"VergilAgent"
			});
	}
}
