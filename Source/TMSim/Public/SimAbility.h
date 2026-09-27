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

		bool HasStatus() const { return !StatusId.empty(); }
	};

	struct FJobDef
	{
		std::string Id;
		std::string Name;
		FJobStats Stats;
		std::string AbilityIds[4];
	};

	/** Null if nothing is registered under that id. */
	TMSIM_API const FJobDef* FindJob(const std::string& JobId);
	TMSIM_API const FAbility* FindAbility(const std::string& AbilityId);

	/** The ability in one of a class's four slots, or null. */
	TMSIM_API const FAbility* JobAbility(const std::string& JobId, int Slot);

	/** Every class the game knows, for listings. */
	TMSIM_API const std::vector<const FJobDef*>& AllJobs();
}
