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

	/** How big the name and health over each unit are drawn, and its status icons (Options). */
	float OverheadScale = 1.35f;
	float StatusIconScale = 1.5f;

	/** How loud the battle's sound effects and the heroes' voices are, 0 to 1 (Options). */
	float SfxVolume = 0.8f;
	float VoiceVolume = 0.8f;

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
