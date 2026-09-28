// Colours and sizes the battle HUD shares between its files, taken from the
// Godot HUD (hud.gd:51-79). Private to the HUD: nothing else includes this.

#pragma once

#include "CoreMinimal.h"

#include "SimTypes.h"
#include "SimUnit.h"
#include "TMSettings.h"

namespace TMHudStyle
{
	inline const FLinearColor TextColour(0.92f, 0.94f, 1.0f);
	inline const FLinearColor Dim(0.92f, 0.94f, 1.0f, 0.55f);
	inline const FLinearColor Gold(1.0f, 0.82f, 0.35f);
	inline const FLinearColor Urgent(1.0f, 0.38f, 0.32f);
	inline const FLinearColor CastColour(0.75f, 0.45f, 1.0f);
	inline const FLinearColor PanelFill(0.06f, 0.08f, 0.12f, 0.78f);
	inline const FLinearColor Shadow(0.0f, 0.0f, 0.0f, 0.8f);

	// Red becomes orange with the colour-blind option on (settings.gd:16-17).
	inline FLinearColor TeamColour(int32 Team)
	{
		if (Team == 0)
		{
			return FLinearColor(0.47f, 0.7f, 1.0f);
		}
		return FTMSettings::Get().bColorblind ? FLinearColor(1.0f, 0.72f, 0.3f) : FLinearColor(1.0f, 0.51f, 0.47f);
	}

	inline FLinearColor TeamFill(int32 Team)
	{
		if (Team == 0)
		{
			return FLinearColor(0.1f, 0.2f, 0.42f, 0.95f);
		}
		return FTMSettings::Get().bColorblind ? FLinearColor(0.45f, 0.26f, 0.05f, 0.95f) : FLinearColor(0.42f, 0.12f, 0.1f, 0.95f);
	}

	// The turn order timeline (hud.gd:56-74).
	inline constexpr float TimelineSeconds = 30.0f;
	inline constexpr float RowHeight = 42.0f;
	inline constexpr float ChipSize = 36.0f;
	inline constexpr float ChipGap = 3.0f;
	inline constexpr float ChipMinScale = 0.75f;
	inline constexpr float MergeDistance = 4.0f;
	inline constexpr int32 ReadySlots = 4;
	inline constexpr float TrackStart = ReadySlots * (ChipSize + ChipGap) + 10.0f;
	/** Seconds either side of a turn during which a chip shows its time. */
	inline constexpr float ShowTimeSeconds = 3.0f;
	/** Room kept at the top right for the corner buttons (hud.gd:56). */
	inline constexpr float CornerWidth = 394.0f;
	inline constexpr float TickSeconds[] = { 0.0f, 1.0f, 3.0f, 5.0f, 10.0f, 20.0f, 30.0f };

	inline constexpr float Tps = static_cast<float>(TMSim::Pace::TicksPerSecond);

	/**
	 * The engine's fonts are drawn for a small screen. Every text scale in the HUD
	 * is written as a share of a readable size and multiplied up once, in Text.
	 */
	inline constexpr float FontBoost = 2.2f;

	/** Two letters for a chip, from the class name: "Black Mage" is BM, "Knight" Kn. */
	inline FString Initials(const FString& Name)
	{
		TArray<FString> Words;
		Name.ParseIntoArray(Words, TEXT(" "));
		if (Words.Num() >= 2)
		{
			return Words[0].Left(1) + Words[1].Left(1);
		}
		return Name.Left(2);
	}

	/** A stat's name as the HUD writes it. */
	inline const TCHAR* ShownStatName(TMSim::EStat Stat)
	{
		switch (Stat)
		{
		case TMSim::EStat::Hp: return TEXT("HP");
		case TMSim::EStat::AttDef: return TEXT("AttDef");
		case TMSim::EStat::MagDef: return TEXT("MagDef");
		case TMSim::EStat::AEva: return TEXT("A-Eva");
		case TMSim::EStat::MEva: return TEXT("M-Eva");
		case TMSim::EStat::Crit: return TEXT("Crit");
		case TMSim::EStat::Speed: return TEXT("Speed");
		case TMSim::EStat::Move: return TEXT("Move");
		case TMSim::EStat::Patience: return TEXT("Patience");
		case TMSim::EStat::Sight: return TEXT("Sight");
		default: return TEXT("?");
		}
	}
}