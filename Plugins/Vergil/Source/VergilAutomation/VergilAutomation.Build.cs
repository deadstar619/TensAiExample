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
				"CQTest",
				"Core",
				"CoreUObject",
				"Engine",
				"Json",
				"UnrealEd",
				"VergilCore",
				"VergilBlueprintModel",
				"VergilBlueprintCompiler",
				"VergilEditor",
				"VergilAgent"
			});
	}
}
