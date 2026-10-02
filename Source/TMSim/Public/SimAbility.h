// What a class is and what its abilities do.
//
// The six built-in classes are written out here, as they are in jobs.gd. The
// other 81 come from the class files in Content/Data/Classes and arrive when
// the importer is ported; this registry is where they will land, so nothing
// above it needs to know which kind a class is.

#pragma once

#include "SimTypes.h"
#include "SimUnit.h"

#include <string>
#include <vector>

namespace TMSim
{
	enum class EEffect : uint8_t { Damage, Heal, Revive, Support };
	/** Which defence resists it, and which evasion dodges it. */
	enum class EScale : uint8_t { Att, Mag };
	enum class ETargetSide : uint8_t { Enemy, Ally, KoAlly };

	struct FAbility
	{
		std::string Id;
		std::string Name;
		/**
		 * One of "active", "passive", "toggle", "channeled", "active_passive"
		 * or "aura". Only an active is something a unit chooses to do on its
		 * turn and be done with; a passive or an aura is never an order at all,
		 * a toggle is a free switch, and a channelled one repeats on each of
		 * the caster's next turns while it can do nothing else.
		 *
		 * None of the six built-in classes is anything but active. The class
		 * files waiting on the importer hold 81 passives, 18 auras, 10 toggles
		 * and 6 channelled, so the words matter even though nothing says them
		 * yet.
		 */
		std::string Kind = "active";
		EEffect Effect = EEffect::Damage;
		EScale Scale = EScale::Att;
		ETargetSide Target = ETargetSide::Enemy;

		/**
		 * What the ability does, and the whole of it: no stat of the user's is
		 * added. For a revive this is a share of the target's max HP instead.
		 */
		float Power = 0.0f;

		float MinRange = 0.0f;
		/** 0 means centred on the user. */
		float MaxRange = 0.0f;
		/** Radius hit around the target point; 0 is a single unit. */
		float Aoe = 0.0f;
		/**
		 * One of "unit", "point", "circle", "self", "line", "cone", "global" or
		 * "vector"; empty means work it out from the ranges. Aoe is the radius
		 * of a circle, the half-width of a line, and unused by a cone.
		 */
		std::string Shape;
		/** Cone spread in degrees. */
		float Angle = 60.0f;
		/** Turns a channelled ability lasts. */
		int Channel = 2;
		int Cooldown = 0;
		/** Seconds of casting before it goes off. */
		float Cast = 0.0f;
		/** Change to the target's Turn Gauge, as a percentage. */
		int TgChange = 0;

		std::string StatusId;
		int StatusTurns = 0;
		std::vector<FBuff> Buffs;

		/** What it does, in words, for the guide and tooltips. Never read by the rules. */
		std::string Desc;
		/** Which built-in ability's animation it borrows. Never read by the rules. */
		std::string Fx;
		/**
		 * A particle effect from the project -- one bought from Fab, say -- played
		 * when the ability goes off. VfxSystem is its object path under /Game;
		 * VfxAt is where it plays ("user", "point" or "targets") and VfxScale how
		 * big. Empty for none. Never read by the rules: an effect is something to
		 * watch, and a battle is the same with or without it.
		 */
		std::string VfxSystem;
		std::string VfxAt;
		/**
		 * How the user's body moves when it is used -- a motion such as "melee" or
		 * "bolt" (AnimMotions()), which each body's animation set turns into a
		 * clip. Empty means the motion worked out from what the ability is
		 * (MotionOf). Never read by the rules.
		 */
		std::string Anim;
		float VfxScale = 1.0f;

		/**
		 * Something beyond damage, healing and statuses, done to whoever it
		 * reaches (Docs/design/feat-neutral-camps.md; none of Godot's has one):
		 * "blink" moves the user to the aim point, "swap" trades places with the
		 * ally hit, "tame" brings a hurt monster over to the user's side,
		 * "summon" wakes the user's camp's reserves, "rewind" gives back the
		 * last hit the user took. Empty for none.
		 */
		std::string Special;
		/**
		 * "fire", "ice", "lightning" or "water" for the reactions with Wet,
		 * Oiled and Chilled (Docs/design/feat-status-effects.md); empty to be
		 * worked out from the ability's id and status (ElementOf).
		 */
		std::string Element;

		bool HasStatus() const { return !StatusId.empty(); }
	};

	/**
	 * An ability's element: as written, else from the words in its id ("flame",
	 * "frost", "thunder", "tide"...) or the status it gives (Burn is fire,
	 * Freeze ice, Wet water). "" for none. Only Wet, Oiled and Chilled react to
	 * it, and nothing in a Godot battle carries those, so its answer there is
	 * never read.
	 */
	TMSIM_API const std::string& ElementOf(const FAbility& Ability);

	/** How a neutral monster behaves: when it starts a fight (Docs/design/feat-neutral-camps.md 14). */
	enum class ETemperament : uint8_t { Docile, Skittish, Provoked, Territorial, Aggressive, GuardPlace, GuardUnit, Patrol };

	/** What bends how a monster fights. */
	namespace MonsterTrait
	{
		enum : uint32_t
		{
			/** Unseen until a unit comes within Monster::AmbushReach. */
			Ambush = 1,
			/** Goes for whoever has the fewest allies near. */
			PackHunter = 2,
			/** Goes for caches and item carriers; takes items. */
			Scavenger = 4,
			/** Seeing someone wakes the nearest waiting camp. */
			Lookout = 8,
			/** Can't be stunned, slept, frozen or knocked down; slows and damage over time last half as long. */
			Unstoppable = 16,
			/** Hits from behind fill a bar; full, it loses a turn and takes more damage. */
			Stagger = 32,
			/** Below half health it does a quarter more damage. */
			Enrage = 64,
		};
	}

	/** A boss's next set of abilities, once its health falls below a share. */
	struct FMonsterPhase
	{
		int BelowPercent = 0;
		std::string AbilityIds[4];
	};

	/** What makes a class a neutral monster. bMonster false for every other class. */
	struct FMonsterInfo
	{
		bool bMonster = false;
		/** 0 easy, 1 medium, 2 hard, 3 epic (a boss). */
		int Tier = 0;
		ETemperament Temperament = ETemperament::Provoked;
		uint32_t Traits = 0;
		/** Metres round its home (or its ward) that set it off: Territorial, Aggressive, a guardian. */
		float Ring = 4.0f;
		/** Metres from home (or its ward, or its route) it will chase before going back. */
		float Leash = 8.0f;
		/** A boss's later phases, highest share first. */
		std::vector<FMonsterPhase> Phases;

		bool Has(uint32_t Trait) const { return (Traits & Trait) != 0; }
	};

	struct FJobDef
	{
		std::string Id;
		std::string Name;
		FJobStats Stats;
		std::string AbilityIds[4];
		/**
		 * What the class is for: "tank", "damage", "support" or "special", and a
		 * class may be two things. The computer hits the other side's supports
		 * and specials first, so this is part of how it chooses.
		 *
		 * Every built-in class says what it is. The original can also work a role
		 * out from a class's numbers and abilities when none is written down,
		 * which the imported classes will need; that arrives with the importer.
		 */
		std::vector<std::string> Roles;

		// How it looks. None of these is read by the rules.

		/** Which of the six built-in bodies it wears. */
		std::string Look;
		/** Its colour, as #rrggbb. */
		std::string Color;
		/** Its icon's name, or empty for the class id's own. */
		std::string Icon;
		/** True for a class loaded from a file rather than written in code. */
		bool bFromFile = false;
		/** A neutral monster's own rules; bMonster false for a class a side can field. */
		FMonsterInfo Monster;
	};

	/** Null if nothing is registered under that id. */
	TMSIM_API const FJobDef* FindJob(const std::string& JobId);
	TMSIM_API const FAbility* FindAbility(const std::string& AbilityId);

	/** Every motion an ability can name, for a class file's "anim" and the creator's list. */
	TMSIM_API const std::vector<std::string>& AnimMotions();
	/**
	 * The motion an ability plays: the one its class file names, or else one
	 * worked out from what it is -- a weapon at arm's length swings, a weapon at
	 * range shoots, a spell at one target is a bolt, a spell over an area is
	 * cast wide, healing and raising and blessing each have their own, a
	 * channelled spell channels, a charge dashes. The ultimate (slot 3) of a
	 * weapon user swings heavy. Presentation only: the rules never ask.
	 */
	TMSIM_API std::string MotionOf(const FAbility& Ability, int Slot);

	/** The ability in one of a class's four slots, or null. */
	TMSIM_API const FAbility* JobAbility(const std::string& JobId, int Slot);

	/** Whether this class counts as that role. */
	TMSIM_API bool JobHasRole(const std::string& JobId, const std::string& Role);

	/**
	 * The game's balance changes to the built-in classes (2026-10-01, "Class
	 * Rebalance Mockups" 2A: the Knight and the Archer). The built-ins stay as
	 * Godot has them unless this is called, so the Godot comparisons hold; the
	 * game calls it once at startup, as the class lab does when it measures on
	 * the game's rules. Calling it again does nothing.
	 */
	TMSIM_API void ApplyGameBalance();

	/**
	 * Every class a side can field, for listings: the built-in six, then any
	 * loaded, each in id order. Neutral monsters are not among them.
	 */
	TMSIM_API const std::vector<const FJobDef*>& AllJobs();
	/** Every neutral monster loaded (Docs/design/feat-neutral-camps.md), in id order. */
	TMSIM_API const std::vector<const FJobDef*>& AllMonsters();

	/**
	 * Adds a class, and its four abilities, to what the game knows. "" when it is
	 * added, otherwise why not: a class may not take a built-in's id or one
	 * already loaded, and its abilities may not take ids already in use.
	 *
	 * Loading classes is part of setting up a game, not part of a battle. Every
	 * machine in a match must have loaded the same classes before one starts,
	 * because a battle's checksum covers the units, not the definitions of their
	 * classes; two machines holding different numbers under the same class id
	 * would come apart and only notice a step later.
	 */
	TMSIM_API std::string RegisterJob(const FJobDef& Job, const std::vector<FAbility>& JobAbilities);

	/** Forgets every loaded class, leaving the built-in six. For tests. */
	TMSIM_API void ForgetLoadedJobs();

	/** Adds one ability outside any class (an item's). "" when added, otherwise why not. */
	TMSIM_API std::string RegisterAbility(const FAbility& Ability);
	/** Takes back an ability RegisterAbility added. */
	TMSIM_API void ForgetAbility(const std::string& AbilityId);
}
