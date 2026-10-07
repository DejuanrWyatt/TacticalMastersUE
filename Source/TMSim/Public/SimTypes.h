// The vocabulary the battle rules are written in: stats, statuses, and the
// handful of numbers that set the pace of a fight.
//
// Plain C++ on purpose. The rules must run headless so a battle can be replayed
// and checked against the Godot version tick for tick, and nothing here should
// need the editor to be open.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Unreal builds each module as its own library, so anything used from another
// module has to be exported. UBT defines TMSIM_API when it compiles this; the
// fallback is for the standalone tests, which build these files with nothing
// but a compiler.
#ifndef TMSIM_API
#define TMSIM_API
#endif

namespace TMSim
{
	// --------------------------------------------------------------- vectors

	/**
	 * A spot on the ground, in meters. Single precision on purpose: Godot's
	 * Vector2 is float, and the flank test turns on the sign of a dot product,
	 * so widening it here could take a different branch from the original.
	 */
	struct FVec2
	{
		float X = 0.0f;
		float Y = 0.0f;

		FVec2() = default;
		FVec2(float InX, float InY) : X(InX), Y(InY) {}

		FVec2 operator-(const FVec2& Other) const { return FVec2(X - Other.X, Y - Other.Y); }
		FVec2 operator+(const FVec2& Other) const { return FVec2(X + Other.X, Y + Other.Y); }
		FVec2 operator*(float By) const { return FVec2(X * By, Y * By); }
		FVec2 operator/(float By) const { return FVec2(X / By, Y / By); }
		TMSIM_API float Length() const;
		TMSIM_API FVec2 Normalized() const;
		float Dot(const FVec2& Other) const { return X * Other.X + Y * Other.Y; }
		/** The 2D cross product: how far off the line the other one lies. */
		float Cross(const FVec2& Other) const { return X * Other.Y - Y * Other.X; }
		/** Signed angle to the other one, in radians, as Godot measures it. */
		TMSIM_API float AngleTo(const FVec2& Other) const;
		bool operator==(const FVec2& Other) const { return X == Other.X && Y == Other.Y; }
		bool operator!=(const FVec2& Other) const { return !(*this == Other); }
		float DistanceTo(const FVec2& Other) const { return (*this - Other).Length(); }
	};

	// ---------------------------------------------------------------- stats

	enum class EStat : uint8_t
	{
		Hp, AttDef, MagDef, AEva, MEva, Crit, Speed, Move, Patience, Sight, Count
	};

	inline constexpr int StatCount = static_cast<int>(EStat::Count);

	/**
	 * A unit's ability slots: its class's four (the fourth the ultimate), then
	 * one for each item it carries that gives an ability (SimItem.h).
	 */
	inline constexpr int AbilitySlots = 7;
	inline constexpr int ClassSlots = 4;

	/** The name a class file uses for a stat, for reading data and for messages. */
	TMSIM_API const char* StatName(EStat Stat);

	/** EStat::Count if the name belongs to no stat (a class file may carry its own). */
	TMSIM_API EStat StatFromName(const std::string& Name);

	// ------------------------------------------------------------- statuses

	/**
	 * What a status does. A status is data: it names the flags it needs and the
	 * rules read them, so adding one is a line in the table rather than a new
	 * branch in the middle of a fight.
	 *
	 * Ported from Jobs.STATUSES in the Godot version, which stays the source of
	 * truth until the rules live here. Only the parts the clock needs are acted
	 * on so far -- TgFactor and bNoOrders -- but the whole table is here so the
	 * two versions can be compared without translating as you read.
	 */
	struct FStatusDef
	{
		const char* Id = "";
		const char* Name = "";
		const char* Tag = "";

		/** Health gained (positive) or lost (negative) each turn, as a share of max HP. */
		float PerTurn = 0.0f;
		/** Multiplies how fast the Turn Gauge fills. Slow halves it. */
		float TgFactor = 1.0f;
		/** Multiplies how far the unit walks. */
		float MoveFactor = 1.0f;
		/** Multiplies AttDef and MagDef. Shred cuts them, Freeze multiplies them. */
		float DefenseFactor = 1.0f;
		/** Added to the chance the unit's own attacks are evaded. */
		int MissPercent = 0;

		bool bHarmful = false;      /** Immunity clears these and turns them away. */
		bool bNoOrders = false;     /** Takes the unit's turn away entirely. */
		bool bNoMove = false;
		bool bNoAbilities = false;
		bool bInterrupt = false;    /** Takes the turn a unit is caught in. */
		bool bAbsorbs = false;      /** Soaks damage before HP. */
		bool bTaunt = false;
		bool bOneAction = false;    /** May walk or act, not both. */
		bool bExtraTurn = false;    /** Comes straight back round. */
		bool bFly = false;
		bool bImmune = false;
		bool bInvulnerable = false;
		bool bCleanse = false;
		bool bWakesOnDamage = false;
		bool bDoom = false;         /** Falls when the count runs out. */
		// Not Godot's (Docs/design/feat-neutral-camps.md); last, so Godot's rows need no change.
		/** Added to the percent its holder's abilities do (Surge). */
		int DamagePercent = 0;
		/** Unseen by the other sides until it deals or takes damage (Vanish). */
		bool bHidden = false;
		/** Monsters go for it first (Scent Lure). */
		bool bLured = false;
		/** Remembers who put it there (FStatus::By): the suppressor, guardian, charmer, or what it fears. */
		bool bSourced = false;
		/** Added to the percent of healing its holder receives (Wounded: -50), from any source. */
		int HealTakenPercent = 0;
	};

	/** Every status, in the order the Godot table lists them. */
	TMSIM_API const FStatusDef* AllStatuses(int& OutCount);

	/** Null if no status has that id. */
	TMSIM_API const FStatusDef* FindStatus(const std::string& StatusId);

	// ----------------------------------------------------------------- time

	namespace Pace
	{
		/** The simulation steps this many times a second. */
		inline constexpr int TicksPerSecond = 10;
		/** A full Turn Gauge. Fine-grained so half speed stays whole for any Speed. */
		inline constexpr int TgMax = 4000;
		/** Gauge gained per tick per point of Speed. */
		inline constexpr int TgPerSpeed = 2;
		/** Head start at the beginning of a battle, per point of Speed. */
		inline constexpr int StartTgPerSpeed = 320;
		/** Gauge kept after a turn that only moved or only acted, and neither. */
		inline constexpr int TgKeepOne = 800;
		inline constexpr int TgKeepNone = 1600;
		/** A full ultimate meter. */
		inline constexpr int UltMax = 100;
		/** Most ticks one Advance order may ask for. */
		inline constexpr int MaxAdvance = 50;
	}

	/**
	 * The rule numbers a battle is played with. Defaults match GameState.TUNING;
	 * a battle carries its own copy so a match or a replay can be played with
	 * the numbers it was recorded under.
	 */
	struct FTuning  // doubles: GDScript has no 32-bit float
	{
		double SpeedMultiplier = 1.0;
		double ClockBase = 8.0;
		double PatienceMultiplier = 2.0;
		/** Percent faster the gauge fills for a unit that held its ability back. */
		double HustleBonus = 25.0;

		/** What every hit is multiplied by once defence has been taken off. */
		double DamageMultiplier = 0.5;
		double HealMultiplier = 1.0;
		/** Damage added per height level above the target, taken off below. */
		double HeightBonus = 0.1;
		double SideBonus = 1.1;
		double BackBonus = 1.25;
		/** What a critical hit multiplies damage by. */
		double CritMultiplier = 1.5;
		double EvadeMultiplier = 1.0;
		double CritChanceMultiplier = 1.0;

		/** Multiplier on how far every unit walks. */
		double MoveMultiplier = 1.0;
		/** How far a sprint goes, as a multiple of Move. A sprint costs the action. */
		double SprintMultiplier = 1.25;
		/** How close an enemy has to be to engage a unit. */
		double EngageRadius = 1.8;
		/** Movement spent stepping out of an enemy's reach. Walking in is free. */
		double EngageCost = 1.0;
		/** Multiplier on how far every unit can see. */
		double SightMultiplier = 1.0;
		/** Health lost on burning ground per turn (and, before 2026-10-04, gained on a spring). */
		double HazardPercent = 8.0;
		/** Health a spring mends, % of max HP, when a unit's turn comes round on it (v19 play test: its own number). */
		double SpringPercent = 8.0;
		/**
		 * Once a spring has mended a unit it runs dry for this many of that unit's
		 * turns (anyone's, once that unit has fallen). 0, Godot's: it never does.
		 */
		double SpringRestTurns = 0.0;
		/**
		 * Tile movement (v20 play test, 2026-10-04, "Tile-based movement" A and B):
		 * 0, free walking on the half-metre grid, as always; 1, from tile to tile
		 * (2 m) four ways, each step 2 m of Move; 2, eight ways, a diagonal step 3 m
		 * (one and a half tiles). A walk ends on a tile's spot (FBattle::TileSpot).
		 */
		double TileMove = 0.0;
		/**
		 * A boss's reach (v21 play test, 2026-10-04: "increase boss aggro radius"):
		 * how far off it notices someone, and how far it chases, times this. 1, as
		 * its monster file says; the game plays at 1.5 (GameTuning).
		 */
		double BossAggro = 1.0;

		/** Multiplier on every ability's cast time. */
		double CastTimeMultiplier = 1.0;
		/** Seconds a knocked-out unit can still be revived. */
		double KoSeconds = 12.0;
		/** Ultimate meter gained per ability used, and per turn taken. */
		double UltPerAction = 20.0;
		double UltPerTurn = 5.0;
		/**
		 * Gauge a unit keeps after a Stun takes its turn. Higher is a weaker
		 * Stun: it loses the turn but is left most of the way to the next one
		 * rather than starting the fill again.
		 */
		double StunTgPercent = 75.0;
		/** Health mended each turn once a unit has been left alone; 0 is off. */
		double RegenPercent = 5.0;
		/** Turns it must go unhurt before that starts. */
		double RegenAfterTurns = 2.0;
		/**
		 * Seconds before the battle is called for the healthier side; 0 is no
		 * limit. The Godot setup screen offers 3, 5 and 10 minutes.
		 */
		double BattleSeconds = 0.0;
		/**
		 * Seconds a side must stand alone in the middle of the map to win; 0 is
		 * off. The Godot setup screen offers 30 and 60.
		 */
		double CaptureSeconds = 0.0;
		/**
		 * Seconds before the fighting in which each side places its units in its
		 * own spawn area; 0 is none. The Godot setup offers 30, 60 and 90.
		 */
		double PlanningSeconds = 0.0;

		// Watchtowers (Docs/design/feat-objectives.md). Not in Godot: a battle
		// with none is exactly Godot's, which is why the count defaults to 0.

		/**
		 * How many watchtowers the battle starts with, placed at random in
		 * mirrored pairs (an odd one stands in the middle). Chosen on the setup
		 * screen; read only when the battle starts.
		 */
		double WatchtowerCount = 0.0;
		/** Turns a side must spend at a watchtower to take it. */
		double WatchtowerTurns = 1.0;
		/** Metres a held watchtower lets its side see, from the top of the tower. */
		double WatchtowerSight = 28.0;

		/**
		 * Points each side may spend on items on the setup screen
		 * (Docs/design/feat-neutral-camps.md); 0 is none. Not in Godot, which
		 * has no items: a battle nobody spends on is exactly Godot's.
		 */
		double ItemBudget = 0.0;

		/**
		 * Neutral camps (Docs/design/feat-neutral-camps.md): 0 off, 1 light,
		 * 2 standard, 3 wild. Off by default, so a battle is Godot's unless asked.
		 */
		double CampLevel = 0.0;
		/** 1: the boss camp holds a boss drawn at random instead of the map's own. */
		double RandomBoss = 0.0;
		/** 1: element hits leave their mark (water makes a unit Wet, ice Chills it). 0 in a Godot battle. */
		double Elements = 0.0;
		/**
		 * 1: an area blow meant for the enemy hurts the caster's own side too,
		 * whoever stands in it -- a cone, a lane, a charge, a blast (the human's
		 * ask, 2026-10-01). Never the caster itself, and never a single-target
		 * blow. 0 in a Godot battle.
		 */
		double FriendlyFire = 0.0;
		/** 1: a cleared camp wakes again after its respawn time; 0 (the default, 2026-10-01): once cleared, gone. */
		double CampRespawn = 0.0;
		/**
		 * How defense works (2026-10-01, Docs/design/feat-defense.md). 0, Godot's
		 * and the default here: AttDef or MagDef is taken off the hit, and A-Eva
		 * or M-Eva is the chance it misses. 1, the game's: Armor (AttDef) or
		 * Resist (MagDef) takes a share, Scale / (Scale + it), off every hit;
		 * one Evasion, the higher of A-Eva and M-Eva plus whatever is added to
		 * either, is the chance a hit is evaded -- and an evaded hit is dodged
		 * outright one time in ten, grazed for half the other nine.
		 */
		double DefenseModel = 0.0;
		/** The defense at which a hit is halved, in model 1. */
		double DefenseScale = 30.0;
		/**
		 * Zone of control (2026-10-01, "Class Rebalance Mockups" 3): 1, an enemy
		 * that walks within EngageRadius of a unit whose first role is tank must
		 * stop there. 0, Godot's and the default here: no such stop.
		 */
		double ZoneOfControl = 0.0;
		/** 1: a boss hunts whoever has hurt it most ("Camps and Bosses Mockups" C). A setup option, off by default. */
		double BossHunt = 0.0;
		/**
		 * 1: the side landing a boss's last blow takes its boon, and the other side,
		 * if it dealt 30% of the boss's health, a rare item (D). A setup option, off by default.
		 */
		double BossClaim = 0.0;
	};

	/**
	 * One of the rule numbers Developer Tools can change: its name in Godot,
	 * what it is called and what it does, its range and step, and the field
	 * it sets (game_state.gd:136-176, TUNING -- the text is Godot's own).
	 */
	struct FTuningKey
	{
		const char* Key;
		const char* Label;
		const char* Desc;
		double Low;
		double High;
		double Step;
		double FTuning::* Member;
	};

	/** Every rule number that can be tuned, in Godot's order. */
	TMSIM_API const std::vector<FTuningKey>& TuningKeys();

	/**
	 * The rules the game plays: Godot's defaults, which stay the defaults here so
	 * the port can be checked against Godot, with the newer rules on top
	 * (2026-10-01: defense rules 1, Docs/design/feat-defense.md). One place, read
	 * by the game (ATMBattleDirector::GameTuning) and by the class lab
	 * (Tools/ClassLab), so a class is measured on the rules it is played on.
	 */
	TMSIM_API FTuning GameTuning();

	namespace Combat
	{
		/** An ability's power is the damage; this is the scale it is read at. */
		inline constexpr double DamageScale = 1.0;
		inline constexpr double HealScale = 1.5;
		/** Height counts for at most this many levels either way. */
		inline constexpr int MaxHeightLevels = 3;
		/** However good the defence, a hit still lands for this much. */
		inline constexpr int MinimumDamage = 1;
		/**
		 * Ultimate meter a unit gains for being hurt, per percent of its own max
		 * health lost. Taking a beating earns a comeback.
		 */
		inline constexpr double UltFromDamage = 0.5;
		/** Defense model 1: of the hits evaded, the share dodged outright (the rest are grazed), and what a graze lands. */
		inline constexpr int DodgeOneIn = 10;
		inline constexpr double GrazeDamage = 0.5;
		/** What an evasion is worth on average, in model 1: a tenth dodged whole, nine tenths halved. */
		inline constexpr double EvadedShare = 1.0 / DodgeOneIn + (1.0 - 1.0 / DodgeOneIn) * (1.0 - GrazeDamage);
	}

	/** Godot's roundi(): halves go away from zero, which is what the rules assume. */
	TMSIM_API int RoundToInt(double Value);
}
