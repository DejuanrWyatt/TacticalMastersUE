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

		bool HasStatus() const { return !StatusId.empty(); }
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

	/** Every class the game knows, for listings: the built-in six, then any loaded, each in id order. */
	TMSIM_API const std::vector<const FJobDef*>& AllJobs();

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
}
