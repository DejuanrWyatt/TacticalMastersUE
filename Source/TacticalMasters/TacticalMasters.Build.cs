using UnrealBuildTool;

public class TacticalMasters : ModuleRules
{
	public TacticalMasters(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "InputCore", "TMSim",
			// Cast Studio's published looks (Docs/CastStudio-Plan.md).
			"TMCast"
		});

		// Effects bought from Fab: Niagara systems (and Cascade ones, which are
		// part of Engine), played when an ability names one, and filmed for the
		// class creator by ATMVfxStudio.
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Niagara", "AssetRegistry", "ImageCore", "Json", "RenderCore", "RHI",
			// The board's ground as one smooth mesh (TMBattleDirectorGround.cpp).
			"ProceduralMeshComponent"
		});

		// Online play (Docs/tech/feat-online.md): one TCP connection between two
		// players, and HTTP to ask the router to open the port (UPnP). Slate for
		// typed text: the address to join, and chat.
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Sockets", "Networking", "HTTP", "Slate", "SlateCore", "ApplicationCore"
		});
	}
}
