#include "SimTypes.h"

#include <cmath>
#include <cstring>

namespace TMSim
{
	float FVec2::Length() const
	{
		return std::sqrt(X * X + Y * Y);
	}

	FVec2 FVec2::Normalized() const
	{
		const float Len = Length();
		return Len > 0.0f ? FVec2(X / Len, Y / Len) : FVec2();
	}

	float FVec2::AngleTo(const FVec2& Other) const
	{
		// atan2 of the cross over the dot, which is how Godot measures it: signed,
		// and correct all the way round rather than only within a right angle.
		return std::atan2(Cross(Other), Dot(Other));
	}

	static const char* const GStatNames[StatCount] =
	{
		"hp", "attdef", "magdef", "aeva", "meva", "crit", "speed", "move", "patience", "sight"
	};

	const char* StatName(EStat Stat)
	{
		const int Index = static_cast<int>(Stat);
		return (Index >= 0 && Index < StatCount) ? GStatNames[Index] : "";
	}

	EStat StatFromName(const std::string& Name)
	{
		for (int i = 0; i < StatCount; ++i)
		{
			if (Name == GStatNames[i])
			{
				return static_cast<EStat>(i);
			}
		}
		return EStat::Count;
	}

	// Ported from Jobs.STATUSES. Kept in the same order so the two can be read
	// side by side. Power is gone as a stat, so nothing here refers to it.
	static const FStatusDef GStatuses[] =
	{
		{ "burn",       "Burn",          "BRN", -0.10f, 1.0f, 1.0f, 1.0f, 0,  true,  false, false, false, false, false, false, false, false, false, false, false, false, false, false },
		{ "bleed",      "Bleed",         "BLE", -0.06f, 1.0f, 1.0f, 1.0f, 0,  true,  false, false, false, false, false, false, false, false, false, false, false, false, false, false },
		{ "regen",      "Regen",         "RGN",  0.10f, 1.0f, 1.0f, 1.0f, 0,  false, false, false, false, false, false, false, false, false, false, false, false, false, false, false },
		{ "slow",       "Slow",          "SLW",  0.0f,  0.5f, 1.0f, 1.0f, 0,  true,  false, false, false, false, false, false, false, false, false, false, false, false, false, false },
		{ "stun",       "Stun",          "STN",  0.0f,  1.0f, 1.0f, 1.0f, 0,  true,  false, false, false, true,  false, false, false, false, false, false, false, false, false, false },
		{ "shield",     "Shield",        "SHD",  0.0f,  1.0f, 1.0f, 1.0f, 0,  false, false, false, false, false, true,  false, false, false, false, false, false, false, false, false },
		{ "barrier",    "Barrier",       "BAR",  0.0f,  1.0f, 1.0f, 1.0f, 0,  false, false, false, false, false, true,  false, false, false, false, false, false, false, false, false },
		{ "root",       "Root",          "ROT",  0.0f,  1.0f, 1.0f, 1.0f, 0,  true,  false, true,  false, false, false, false, false, false, false, false, false, false, false, false },
		{ "crippled",   "Crippled",      "CRP",  0.0f,  1.0f, 0.5f, 1.0f, 0,  true,  false, false, false, false, false, false, false, false, false, false, false, false, false, false },
		{ "stride",     "Stride",        "STR",  0.0f,  1.0f, 1.5f, 1.0f, 0,  false, false, false, false, false, false, false, false, false, false, false, false, false, false, false },
		{ "silence",    "Silence",       "SIL",  0.0f,  1.0f, 1.0f, 1.0f, 0,  true,  false, false, true,  false, false, false, false, false, false, false, false, false, false, false },
		{ "blind",      "Blind",         "BLN",  0.0f,  1.0f, 1.0f, 1.0f, 25, true,  false, false, false, false, false, false, false, false, false, false, false, false, false, false },
		{ "shred",      "Shred",         "SHR",  0.0f,  1.0f, 1.0f, 0.6f, 0,  true,  false, false, false, false, false, false, false, false, false, false, false, false, false, false },
		{ "sleep",      "Sleep",         "SLP",  0.0f,  1.0f, 1.0f, 1.0f, 0,  true,  true,  false, false, false, false, false, false, false, false, false, false, false, true,  false },
		{ "freeze",     "Freeze",        "FRZ",  0.0f,  1.0f, 1.0f, 3.0f, 0,  true,  false, true,  true,  false, false, false, false, false, false, false, false, false, false, false },
		{ "knockdown",  "Knockdown",     "KND",  0.0f,  1.0f, 1.0f, 1.0f, 0,  true,  false, false, false, false, false, false, true,  false, false, false, false, false, false, false },
		{ "doom",       "Doom",          "DOM",  0.0f,  1.0f, 1.0f, 1.0f, 0,  true,  false, false, false, false, false, false, false, false, false, false, false, false, false, true  },
		{ "taunt",      "Taunt",         "TNT",  0.0f,  1.0f, 1.0f, 1.0f, 0,  true,  false, false, false, false, false, true,  false, false, false, false, false, false, false, false },
		{ "fly",        "Fly",           "FLY",  0.0f,  1.0f, 1.0f, 1.0f, 0,  false, false, false, false, false, false, false, false, false, true,  false, false, false, false, false },
		{ "immunity",   "Immunity",      "IMM",  0.0f,  1.0f, 1.0f, 1.0f, 0,  false, false, false, false, false, false, false, false, false, false, true,  false, true,  false, false },
		{ "invuln",     "Invulnerable",  "INV",  0.0f,  1.0f, 1.0f, 1.0f, 0,  false, false, false, false, false, false, false, false, false, false, false, true,  false, false, false },
		{ "relentless", "Relentless",    "RLN",  0.0f,  1.0f, 1.0f, 1.0f, 0,  false, false, false, false, false, false, false, false, true,  false, false, false, false, false, false },
	};

	const FStatusDef* AllStatuses(int& OutCount)
	{
		OutCount = static_cast<int>(sizeof(GStatuses) / sizeof(GStatuses[0]));
		return GStatuses;
	}

	const FStatusDef* FindStatus(const std::string& StatusId)
	{
		int Count = 0;
		const FStatusDef* Statuses = AllStatuses(Count);
		for (int i = 0; i < Count; ++i)
		{
			if (StatusId == Statuses[i].Id)
			{
				return &Statuses[i];
			}
		}
		return nullptr;
	}

	int RoundToInt(double Value)
	{
		// std::round takes halves away from zero, which is what Godot's roundi()
		// does. Worth being exact about: the gauge and the countdown are whole
		// numbers derived from these, and a rounding that disagrees by one would
		// send a battle down a different path a few seconds later.
		return static_cast<int>(std::round(Value));
	}

	const std::vector<FTuningKey>& TuningKeys()
	{
		static const std::vector<FTuningKey> Keys =
		{
			{ "speed_multiplier", "Speed multiplier", "How fast Turn Gauges fill (x Speed).", 0.25, 3.0, 0.05, &FTuning::SpeedMultiplier },
			{ "clock_base", "Countdown base (s)", "Seconds every READY unit gets, before Patience.", 2.0, 30.0, 0.5, &FTuning::ClockBase },
			{ "patience_multiplier", "Patience multiplier (s)", "Extra countdown seconds per point of Patience.", 0.0, 6.0, 0.25, &FTuning::PatienceMultiplier },
			{ "damage_multiplier", "Damage multiplier", "Final multiplier on all damage (after defense).", 0.1, 2.0, 0.05, &FTuning::DamageMultiplier },
			{ "heal_multiplier", "Healing multiplier", "Multiplier on all healing.", 0.1, 3.0, 0.05, &FTuning::HealMultiplier },
			{ "height_bonus", "Height bonus per level", "Damage bonus per level above the target (penalty below).", 0.0, 0.5, 0.01, &FTuning::HeightBonus },
			{ "side_bonus", "Side attack multiplier", "Damage multiplier for hits from the side.", 1.0, 2.0, 0.05, &FTuning::SideBonus },
			{ "back_bonus", "Back attack multiplier", "Damage multiplier for hits from behind.", 1.0, 3.0, 0.05, &FTuning::BackBonus },
			{ "ko_seconds", "Knock-out time (s)", "How long a knocked-out unit can still be revived.", 0.0, 60.0, 1.0, &FTuning::KoSeconds },
			{ "ult_per_action", "Ultimate per action", "Ultimate meter gained per ability used.", 0.0, 100.0, 1.0, &FTuning::UltPerAction },
			{ "ult_per_turn", "Ultimate per turn", "Ultimate meter gained each time a unit becomes READY.", 0.0, 50.0, 1.0, &FTuning::UltPerTurn },
			{ "move_multiplier", "Move multiplier", "Multiplier on every unit's Move distance.", 0.25, 3.0, 0.05, &FTuning::MoveMultiplier },
			{ "sight_multiplier", "Sight multiplier", "Multiplier on every unit's Sight radius.", 0.25, 3.0, 0.05, &FTuning::SightMultiplier },
			{ "cast_time_multiplier", "Cast time multiplier", "Multiplier on every ability's cast time.", 0.0, 3.0, 0.05, &FTuning::CastTimeMultiplier },
			{ "crit_multiplier", "Critical hit multiplier", "What a critical hit multiplies damage by.", 1.0, 3.0, 0.05, &FTuning::CritMultiplier },
			{ "evade_multiplier", "Evasion multiplier", "Multiplier on every unit's A-Eva and M-Eva.", 0.0, 3.0, 0.05, &FTuning::EvadeMultiplier },
			{ "crit_chance_multiplier", "Crit chance multiplier", "Multiplier on every unit's Crit chance.", 0.0, 3.0, 0.05, &FTuning::CritChanceMultiplier },
			{ "planning_seconds", "Planning time (s)", "0 = off. Before the fighting starts, each side may place its units anywhere in its own spawn area for this long.", 0.0, 180.0, 15.0, &FTuning::PlanningSeconds },
			{ "capture_seconds", "Hold the middle to win (s)", "0 = off. A side that stands alone in the middle of the map for this long wins.", 0.0, 180.0, 5.0, &FTuning::CaptureSeconds },
			{ "sprint_multiplier", "Sprint distance (x Move)", "How far a Sprint goes, as a multiple of Move. A Sprint uses the unit's action as well as its move.", 1.0, 2.0, 0.05, &FTuning::SprintMultiplier },
			{ "engage_radius", "Engagement radius (m)", "How close an enemy has to be to engage a unit. Walking in is free; breaking away costs extra movement.", 0.0, 6.0, 0.2, &FTuning::EngageRadius },
			{ "engage_cost", "Breaking away costs (m)", "Movement spent to step out of an enemy's engagement radius.", 0.0, 6.0, 0.5, &FTuning::EngageCost },
			{ "hustle_bonus", "Held-back turn bonus (%)", "How much faster the Turn Gauge fills for a unit that ended its turn without using an ability.", 0.0, 100.0, 5.0, &FTuning::HustleBonus },
			{ "hazard_percent", "Hazard ground (% max HP)", "Health lost on burning ground, or gained on a spring, when a unit's turn comes round.", 0.0, 40.0, 1.0, &FTuning::HazardPercent },
			{ "stun_tg_percent", "Stun gauge kept (%)", "Turn Gauge a unit is left with after a Stun takes its turn. Higher is a weaker Stun.", 0.0, 100.0, 5.0, &FTuning::StunTgPercent },
			{ "regen_percent", "Undamaged regen (% max HP)", "0 = off. Health a unit regains at the start of each of its turns once it has gone long enough without being hurt.", 0.0, 25.0, 1.0, &FTuning::RegenPercent },
			{ "regen_after_turns", "Regen after (turns)", "How many of its own turns a unit must go through without taking damage before it starts mending.", 1.0, 10.0, 1.0, &FTuning::RegenAfterTurns },
			{ "battle_seconds", "Battle time limit (s)", "0 = no limit. When it runs out, the side with more of its health left wins; level shares draw.", 0.0, 600.0, 15.0, &FTuning::BattleSeconds },
		};
		return Keys;
	}
}
