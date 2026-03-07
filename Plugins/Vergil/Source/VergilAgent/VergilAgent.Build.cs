using UnrealBuildTool;

public class VergilAgent : ModuleRules
{
	public VergilAgent(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"EditorSubsystem",
				"Engine",
				"Json",
				"JsonUtilities",
				"VergilCore",
				"VergilBlueprintModel",
				"VergilBlueprintCompiler"
			});

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Projects",
				"UnrealEd",
				"VergilEditor"
			});
	}
}
