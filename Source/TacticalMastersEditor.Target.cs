using UnrealBuildTool;

public class TacticalMastersEditorTarget : TargetRules
{
	public TacticalMastersEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		// V7 matches what the installed engine was built with. An installed engine
		// shares build products with UnrealEditor, so a target that changes warning
		// levels is refused outright.
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "TacticalMasters", "TMSim", "TMCast" });
	}
}
