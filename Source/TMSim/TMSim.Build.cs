using UnrealBuildTool;

public class TMSim : ModuleRules
{
	public TMSim(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Core only, and nothing from Engine. The battle rules are plain C++ so
		// they can be run headless, replayed, and checked against the Godot
		// version tick for tick. Anything that needs the engine belongs in the
		// TacticalMasters module instead.
		PublicDependencyModuleNames.AddRange(new string[] { "Core" });
	}
}
