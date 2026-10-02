// Colours and sizes the battle HUD shares between its files, taken from the
// Godot HUD (hud.gd:51-79). Private to the HUD: nothing else includes this.

#pragma once

#include "CoreMinimal.h"

#include "SimTypes.h"
#include "SimUnit.h"
#include "TMSettings.h"

#include <string>

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

	// Allies blue and enemies red, from where the player stands rather than by
	// side, as Atlas Reactor does it. Red becomes orange with the colour-blind
	// option on, as TeamColour's does.
	inline FLinearColor SideColour(bool bFriend)
	{
		if (bFriend)
		{
			return FLinearColor(0.2f, 0.55f, 1.0f);
		}
		return FTMSettings::Get().bColorblind ? FLinearColor(1.0f, 0.6f, 0.1f) : FLinearColor(1.0f, 0.18f, 0.15f);
	}

	inline FLinearColor SideDark(bool bFriend)
	{
		return SideColour(bFriend) * FLinearColor(0.25f, 0.25f, 0.25f, 1.0f);
	}

	/** A status's colour and what it does, as the Godot game shows them (jobs.gd:90-134). */
	struct FStatusLook
	{
		const char* Id;
		FLinearColor Colour;
		const TCHAR* Desc;
	};

	inline const FStatusLook& StatusLook(const std::string& Id)
	{
		static const FStatusLook Looks[] =
		{
			{ "burn", FLinearColor(1.0f, 0.5f, 0.2f), TEXT("Loses 10% of max HP at the start of each of its turns.") },
			{ "bleed", FLinearColor(0.8f, 0.1f, 0.2f), TEXT("Loses 6% of max HP at the start of each of its turns.") },
			{ "regen", FLinearColor(0.45f, 1.0f, 0.55f), TEXT("Recovers 10% of max HP at the start of each of its turns.") },
			{ "slow", FLinearColor(0.5f, 0.75f, 1.0f), TEXT("Turn Gauge fills at half speed.") },
			{ "stun", FLinearColor(1.0f, 0.9f, 0.3f), TEXT("A unit caught in its own turn loses it, and is left partway to its next one instead of starting the gauge over.") },
			{ "shield", FLinearColor(0.6f, 0.85f, 1.0f), TEXT("Soaks up damage until it is used up (or its turns run out).") },
			{ "barrier", FLinearColor(0.75f, 0.9f, 1.0f), TEXT("Soaks up damage the way a Shield does, and the two stack.") },
			{ "root", FLinearColor(0.7f, 0.55f, 0.3f), TEXT("Can't walk, but can still use abilities.") },
			{ "crippled", FLinearColor(0.6f, 0.5f, 0.35f), TEXT("Walks only half as far.") },
			{ "stride", FLinearColor(0.5f, 0.95f, 0.8f), TEXT("Walks half again as far.") },
			{ "silence", FLinearColor(0.8f, 0.5f, 0.9f), TEXT("Can't use abilities, but can still walk.") },
			{ "blind", FLinearColor(0.45f, 0.4f, 0.5f), TEXT("Its attacks are 25% more likely to be evaded.") },
			{ "shred", FLinearColor(0.9f, 0.45f, 0.3f), TEXT("AttDef and MagDef cut to 60%, so everything hits it harder.") },
			{ "sleep", FLinearColor(0.55f, 0.65f, 1.0f), TEXT("Sleeps through its next turn, which is lost. Any damage wakes it at once.") },
			{ "freeze", FLinearColor(0.6f, 0.9f, 1.0f), TEXT("Locked in place and can do nothing, but AttDef and MagDef are tripled while it lasts.") },
			{ "knockdown", FLinearColor(0.8f, 0.7f, 0.45f), TEXT("Getting up takes something: it may walk or use an ability on its turn, not both.") },
			{ "doom", FLinearColor(0.7f, 0.2f, 0.45f), TEXT("When its count runs out the unit falls, however much health it has left.") },
			{ "taunt", FLinearColor(1.0f, 0.6f, 0.35f), TEXT("Must attack whoever taunted it while that unit is in reach.") },
			{ "fly", FLinearColor(0.75f, 0.85f, 1.0f), TEXT("Climbs any height, and melee abilities can't reach it.") },
			{ "immunity", FLinearColor(1.0f, 0.95f, 0.6f), TEXT("Clears every harmful status on it, and turns away new ones while it lasts.") },
			{ "invuln", FLinearColor(1.0f, 1.0f, 0.85f), TEXT("Takes no damage at all while it lasts.") },
			{ "relentless", FLinearColor(1.0f, 0.75f, 0.3f), TEXT("Takes another turn the moment this one ends, then wears off.") },
			// The camps' and items' own (Docs/design/feat-neutral-camps.md).
			{ "surge", FLinearColor(1.0f, 0.44f, 0.4f), TEXT("Its abilities do 35% more damage.") },
			{ "veil", FLinearColor(0.6f, 0.65f, 0.79f), TEXT("Unseen by the other side, unless an enemy comes within 3 m, until it deals or takes damage.") },
			{ "lured", FLinearColor(0.9f, 0.7f, 0.35f), TEXT("Monsters go for it first.") },
			{ "staggered", FLinearColor(0.9f, 0.6f, 0.4f), TEXT("Loses its next turn and its defences are cut by a quarter.") },
			// The second set (Docs/design/feat-status-effects.md).
			{ "marked", FLinearColor(1.0f, 0.35f, 0.35f), TEXT("The next hit on it deals 30% more, then the mark is spent. Monsters go for it.") },
			{ "offbalance", FLinearColor(1.0f, 0.65f, 0.3f), TEXT("Can't turn to face anything, and the next hit lands as if from behind.") },
			{ "wet", FLinearColor(0.3f, 0.65f, 1.0f), TEXT("Lightning stuns it (and the Wet near it), ice freezes it, fire dries it. Burn lasts half as long.") },
			{ "oiled", FLinearColor(0.7f, 0.54f, 0.3f), TEXT("Fire hits it 50% harder and sets it burning for twice as long, using the oil up.") },
			{ "chilled", FLinearColor(0.65f, 0.88f, 1.0f), TEXT("Turn Gauge and steps slowed by a fifth. Three layers freeze it; fire thaws it.") },
			{ "haste", FLinearColor(1.0f, 0.87f, 0.35f), TEXT("Turn Gauge fills 50% faster. Slow cancels it.") },
			{ "stop", FLinearColor(0.7f, 0.65f, 1.0f), TEXT("Its Turn Gauge doesn't fill at all. Counted in seconds, not turns.") },
			{ "suppressed", FLinearColor(0.9f, 0.36f, 0.25f), TEXT("If it walks, whoever suppressed it gets a free blow; its own attacks miss 30% more. Ends when the suppressor is hurt.") },
			{ "protect", FLinearColor(0.9f, 0.76f, 0.45f), TEXT("Takes a third less physical (AttDef) damage.") },
			{ "shell", FLinearColor(0.7f, 0.55f, 1.0f), TEXT("Takes a third less magic (MagDef) damage.") },
			{ "guarded", FLinearColor(0.55f, 0.78f, 0.9f), TEXT("Single-target hits on it land on its guardian instead, while the guardian stands within 3 m.") },
			{ "reraise", FLinearColor(1.0f, 0.85f, 0.55f), TEXT("Knocked out, it stands up again 3 seconds later with a quarter of its health.") },
			{ "reflect", FLinearColor(0.85f, 0.94f, 1.0f), TEXT("The next single-target spell the other side aims at it bounces back to the caster.") },
			{ "charmed", FLinearColor(1.0f, 0.5f, 0.76f), TEXT("Fights for the other side on its next turn. Any damage brings it back to its senses.") },
			{ "terrified", FLinearColor(0.65f, 0.5f, 0.85f), TEXT("On its turn it first runs its full move away from what it fears, then may act.") },
			{ "decay", FLinearColor(0.56f, 0.68f, 0.28f), TEXT("Healing on it deals damage instead; Regen and springs hurt it.") },
			{ "wounded", FLinearColor(0.75f, 0.32f, 0.36f), TEXT("Receives only half of any healing: abilities, Regen, springs, mending and lifesteal.") },
		};
		for (const FStatusLook& Look : Looks)
		{
			if (Id == Look.Id)
			{
				return Look;
			}
		}
		static const FStatusLook Unknown = { "", FLinearColor(0.8f, 0.8f, 0.85f), TEXT("") };
		return Unknown;
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

	/** A class's one Evasion, as the defense rules read it (FBattle::EvasionOf): the higher of its two. */
	inline int ClassEvasion(const TMSim::FJobStats& Stats)
	{
		return FMath::Max(Stats.Get(TMSim::EStat::AEva), Stats.Get(TMSim::EStat::MEva));
	}

	/** A stat's name as the HUD writes it. */
	inline const TCHAR* ShownStatName(TMSim::EStat Stat)
	{
		switch (Stat)
		{
		case TMSim::EStat::Hp: return TEXT("HP");
		// 2026-10-01 (Docs/design/feat-defense.md): Armor and Resist take a share
		// off physical and magic hits, and the two evasions are one Evasion.
		case TMSim::EStat::AttDef: return TEXT("Armor");
		case TMSim::EStat::MagDef: return TEXT("Resist");
		case TMSim::EStat::AEva: return TEXT("Evasion");
		case TMSim::EStat::MEva: return TEXT("Evasion");
		case TMSim::EStat::Crit: return TEXT("Crit");
		case TMSim::EStat::Speed: return TEXT("Speed");
		case TMSim::EStat::Move: return TEXT("Move");
		case TMSim::EStat::Patience: return TEXT("Patience");
		case TMSim::EStat::Sight: return TEXT("Sight");
		default: return TEXT("?");
		}
	}
}