using UnrealBuildTool;

public class TacticalMasters : ModuleRules
{
	public TacticalMasters(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "InputCore", "TMSim"
		});
	}
}
