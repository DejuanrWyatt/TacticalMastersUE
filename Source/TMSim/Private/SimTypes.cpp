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
}
