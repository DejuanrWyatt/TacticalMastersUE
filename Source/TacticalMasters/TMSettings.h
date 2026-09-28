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
