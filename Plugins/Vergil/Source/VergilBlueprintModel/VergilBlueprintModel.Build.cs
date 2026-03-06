using UnrealBuildTool;

public class VergilBlueprintModel : ModuleRules
{
	public VergilBlueprintModel(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"VergilCore"
			});
	}
}
