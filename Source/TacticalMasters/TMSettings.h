// The player's settings and key bindings: how the game is played on this
// machine, not what the rules are. Saved to Saved/TacticalMasters/settings.json.
//
// Ported from the Godot game's Settings and Keybinds (autoload/settings.gd,
// autoload/keybinds.gd): the same actions with the same default keys, a key
// already used by another action swapped between the two so nothing is left
// without one. Sound volumes wait for the game to have sound.
//
// Rule numbers chosen in Developer Tools are kept here too, for the next
// battle, as Godot keeps them in GameConfig.tuning. They reach the rules only
// by order (FOrder::MakeTune), never by being read from here during a battle.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

/** Everything a key can do (keybinds.gd:15-38). */
enum class ETMAction : uint8
{
	Move,
	Sprint,
	Ability1,
	Ability2,
	Ability3,
	Ability4,
	EndTurn,
	Cancel,
	NextUnit,
	Pause,
	UnitGuide,
	Log,
	CenterCamera,
	CamForward,
	CamBack,
	CamLeft,
	CamRight,
	CamUp,
	CamDown,
	CamRotateLeft,
	CamRotateRight,
	/** Customise the screen, and lock it again: one press each. */
	EditLayout,
	/** Online: type a line to the other player. */
	Chat,
	/** Show or hide the health and status bars over the units (2026-10-01). */
	StatusBars,
	/** Turn the camera's slide to the next ready unit on or off. */
	AutoRecenter,
	/** Queued orders (2026-10-01): held while clicking, adds a waypoint to the walk. */
	Waypoint,
	/** Plan the selected unit's turn ahead; again carries the plan out (or puts it by). */
	PlanTurn,
	/** Takes back the plan's last step: a waypoint, its ability, then its walk. */
	PlanUndo,
	/** Held: the battle runs three times as fast while none of this machine's units is ready (2026-10-03). */
	FastForward,
	Count
};

struct FTMActionInfo
{
	const TCHAR* Id;
	const TCHAR* Label;
	TArray<FKey> Defaults;
};

class TACTICALMASTERS_API FTMSettings
{
public:
	static FTMSettings& Get();

	/** Multiplies how fast the camera pans and turns (settings.gd camera_speed). */
	float CameraSpeed = 1.0f;
	/** How big the HUD is drawn: 0.9, 1, 1.15 or 1.3 (settings.gd ui_scale). */
	float UiScale = 1.0f;
	bool bFullscreen = false;
	/** Blue and orange rather than blue and red, for red-green colour blindness. */
	bool bColorblind = false;

	/**
	 * The turn order as a fixed square per unit rather than chips sliding along
	 * two bars (settings.gd turn_icons). The default here: the squares.
	 */
	bool bTurnSquares = true;

	/** Your squad down the left edge: health, statuses and turns left, cooldowns (2026-10-02, "Squad Strip Mockups" C). */
	bool bSquadStrip = true;

	/** When a unit's turn ends, the camera slides to the next one ready (2026-10-01: optional, and a key toggles it). */
	bool bAutoRecenter = true;
	/**
	 * Camera rules (2026-10-03, "Camera Rules Mockups"): when the camera may
	 * follow to the next ready unit by itself. 0 Always (at once, but never in
	 * the middle of an order), 1 When I'm idle (the default: not within 1.5 s of
	 * a click or key either), 2 Never. bAutoRecenter is On unless this is Never.
	 */
	int32 CameraFollow = 1;
	/** The camera goes ahead of a walk to the edge of the screen (Options). */
	bool bLeadCamera = true;
	/** A small "Camera held" note while the camera waits for you to finish (Options). */
	bool bCameraHeldNote = true;

	/**
	 * Quick Cast, per ability key (2026-10-03): the key held aims the ability,
	 * and letting it go uses it where the pointer is -- no click. Off, a press
	 * aims and a click uses it, as before.
	 */
	bool bQuickCast[4] = { false, false, false, false };

	/** The battle runs fast while none of this machine's units is ready: the other side's turns, the waits (Options). */
	bool bFastEnemyTurns = false;
	/** A unit that has walked and has no ability it can use ends its turn by itself (Options). */
	bool bAutoEndTurn = false;
	/** The camera closes in on whoever uses an ultimate, for a moment (Options; any key or click skips it). */
	bool bCloseUps = true;
	/** The wheel zooms toward where the pointer is, not the middle of the screen (Options). */
	bool bZoomToCursor = true;
	/** The camera pans when the pointer rests at the edge of the window (Options). */
	bool bEdgePan = true;

	/** How big the name and health over each unit are drawn, and its status icons (Options). */
	float OverheadScale = 1.35f;
	float StatusIconScale = 1.875f;

	/** How loud the battle's sound effects and the heroes' voices are, 0 to 1 (Options). */
	float SfxVolume = 0.8f;
	float VoiceVolume = 0.8f;

	/** How big the damage and healing numbers rising off units are, times the director's FloaterSize (Options). */
	float DamageTextScale = 1.75f;

	/**
	 * Where each panel has been moved to in Edit layout, as how far it is
	 * nudged from where it normally sits, in 1080p pixels -- so it still
	 * follows its edge or corner when the window changes (layout_editor.gd:6-9).
	 */
	TMap<FString, FVector2D> Layout;
	/** The order each side's turn squares were dragged into, by unit id. */
	TArray<int32> CardOrder[2];
	/** How big each panel is drawn, as a share of its normal size; missing means 1. */
	TMap<FString, float> LayoutScale;
	float ScaleOf(const TCHAR* Id) const { const float* Found = LayoutScale.Find(Id); return Found ? *Found : 1.0f; }
	/** While arranging: a grid to line panels up on, and how far apart its lines are (1080p pixels). */
	bool bLayoutGrid = true;
	float GridSize = 20.0f;
	void ResetLayout();

	/** Rule numbers for the next battle, by tuning key; missing means the default. */
	TMap<FString, double> Tuning;
	/**
	 * The last battle set up on this machine (2026-10-01: "dev tools options save
	 * after closing game"): the setup screen's choices, by name, written as text.
	 * Empty until a battle has been set up. Read by ATMBattleDirector::OpenSetup.
	 */
	TMap<FString, FString> LastSetup;

	static const FTMActionInfo& Info(ETMAction Action);
	const TArray<FKey>& Keys(ETMAction Action) const { return Bound[static_cast<int32>(Action)]; }
	/** Whether this key does that action. */
	bool Is(const FKey& Key, ETMAction Action) const;
	/** The first key's name, for the HUD: "Space", "1", "Enter". */
	FString KeyName(ETMAction Action) const;
	/**
	 * Makes Key the action's main key. If another action had it, that one gets
	 * this action's old main key in exchange (keybinds.gd:98-115).
	 */
	void Rebind(ETMAction Action, const FKey& Key);
	void ResetKeys();
	void ResetOptions();

	/** Team colours, as the colour-blind option has them. */
	FLinearColor TeamColour(int32 Team) const;

	void Load();
	void Save() const;
	/** Puts the display settings into effect: fullscreen or windowed. */
	void Apply() const;

private:
	FTMSettings();
	TArray<FKey> Bound[static_cast<int32>(ETMAction::Count)];
};
