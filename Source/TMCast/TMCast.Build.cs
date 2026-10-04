using UnrealBuildTool;

public class TMCast : ModuleRules
{
	public TMCast(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Cast Studio's published looks (Docs/CastStudio-Plan.md), read as plain
		// C++ so the standalone tests can check them as they check the rules.
		// It borrows the rules' small JSON reader and nothing else; the rules
		// never depend on this, so a look can never change a battle.
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "TMSim" });
	}
}
