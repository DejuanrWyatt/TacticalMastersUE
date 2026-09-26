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
	const FJobDef* FindJob(const std::string& JobId);
	const FAbility* FindAbility(const std::string& AbilityId);

	/** The ability in one of a class's four slots, or null. */
	const FAbility* JobAbility(const std::string& JobId, int Slot);

	/** Every class the game knows, for listings. */
	const std::vector<const FJobDef*>& AllJobs();
}
