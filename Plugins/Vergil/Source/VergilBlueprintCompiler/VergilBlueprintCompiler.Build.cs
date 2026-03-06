using UnrealBuildTool;

public class VergilBlueprintCompiler : ModuleRules
{
	public VergilBlueprintCompiler(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"VergilCore",
				"VergilBlueprintModel"
			});

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"BlueprintGraph",
				"Kismet",
				"KismetCompiler",
				"Projects",
				"UnrealEd"
			});
	}
}
