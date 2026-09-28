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

		// Effects bought from Fab: Niagara systems (and Cascade ones, which are
		// part of Engine), played when an ability names one, and filmed for the
		// class creator by ATMVfxStudio.
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Niagara", "AssetRegistry", "ImageCore", "Json"
		});
	}
}
