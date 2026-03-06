using UnrealBuildTool;

public class VergilAutomation : ModuleRules
{
	public VergilAutomation(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
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
