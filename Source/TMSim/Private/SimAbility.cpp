#include "SimAbility.h"

#include <map>

namespace TMSim
{
	namespace
	{
		FAbility MakeAbility(const char* Id, const char* Name, EEffect Effect, EScale Scale,
			ETargetSide Target, float Power, float MinRange, float MaxRange, float Aoe,
			int Cooldown, float Cast)
		{
			FAbility Ability;
			Ability.Id = Id;
			Ability.Name = Name;
			Ability.Effect = Effect;
			Ability.Scale = Scale;
			Ability.Target = Target;
			Ability.Power = Power;
			Ability.MinRange = MinRange;
			Ability.MaxRange = MaxRange;
			Ability.Aoe = Aoe;
			Ability.Cooldown = Cooldown;
			Ability.Cast = Cast;
			return Ability;
		}

		// The built-in abilities, as jobs.gd has them. The powers already carry
		// what the old Power stat used to add, so an ability's number is the
		// whole of what it does.
		std::map<std::string, FAbility> BuildAbilities()
		{
			using E = EEffect;
			using S = EScale;
			using T = ETargetSide;
			std::map<std::string, FAbility> Out;

			auto Add = [&Out](FAbility Ability) { Out[Ability.Id] = Ability; };

			// Squire
			Add(MakeAbility("attack", "Attack", E::Damage, S::Att, T::Enemy, 31, 0.0f, 1.8f, 0.0f, 0, 0.0f));
			Add(MakeAbility("throw_stone", "Throw Stone", E::Damage, S::Att, T::Enemy, 20, 2.0f, 7.0f, 0.0f, 1, 0.0f));
			{
				FAbility Focus = MakeAbility("focus", "Focus", E::Support, S::Att, T::Ally, 0, 0.0f, 0.0f, 0.0f, 3, 0.0f);
				Focus.Buffs.push_back({ EStat::Crit, 12, 2 });
				Add(Focus);
			}
			Add(MakeAbility("brave_slash", "Brave Slash", E::Damage, S::Att, T::Enemy, 73, 0.0f, 1.8f, 0.0f, 0, 0.0f));

			// Knight
			{
				FAbility Bash = MakeAbility("shield_bash", "Shield Bash", E::Damage, S::Att, T::Enemy, 42, 0.0f, 1.8f, 0.0f, 2, 0.0f);
				Bash.TgChange = -30;
				Bash.StatusId = "stun";
				Bash.StatusTurns = 1;
				Add(Bash);
			}
			{
				FAbility Guard = MakeAbility("guard", "Guard", E::Support, S::Att, T::Ally, 0, 0.0f, 0.0f, 0.0f, 3, 0.0f);
				Guard.Buffs.push_back({ EStat::AttDef, 8, 2 });
				Guard.Buffs.push_back({ EStat::MagDef, 6, 2 });
				Add(Guard);
			}
			Add(MakeAbility("holy_blade", "Holy Blade", E::Damage, S::Att, T::Enemy, 64, 0.0f, 3.0f, 1.5f, 0, 1.0f));

			// Archer
			Add(MakeAbility("bow_shot", "Bow Shot", E::Damage, S::Att, T::Enemy, 30, 3.0f, 10.0f, 0.0f, 0, 0.0f));
			Add(MakeAbility("aimed_shot", "Aimed Shot", E::Damage, S::Att, T::Enemy, 48, 4.0f, 12.0f, 0.0f, 2, 1.0f));
			{
				FAbility Pin = MakeAbility("pin_shot", "Pin Shot", E::Damage, S::Att, T::Enemy, 21, 3.0f, 10.0f, 0.0f, 2, 0.0f);
				Pin.TgChange = -40;
				Add(Pin);
			}
			Add(MakeAbility("arrow_rain", "Arrow Rain", E::Damage, S::Att, T::Enemy, 39, 4.0f, 13.0f, 2.5f, 0, 2.0f));

			// Monk
			Add(MakeAbility("punch", "Punch", E::Damage, S::Att, T::Enemy, 41, 0.0f, 1.8f, 0.0f, 0, 0.0f));
			Add(MakeAbility("wave_fist", "Wave Fist", E::Damage, S::Att, T::Enemy, 34, 2.0f, 5.0f, 0.0f, 1, 0.0f));
			{
				FAbility Chakra = MakeAbility("chakra", "Chakra", E::Heal, S::Att, T::Ally, 25, 0.0f, 0.0f, 2.5f, 3, 0.0f);
				Chakra.StatusId = "regen";
				Chakra.StatusTurns = 2;
				Add(Chakra);
			}
			Add(MakeAbility("earth_slash", "Earth Slash", E::Damage, S::Att, T::Enemy, 61, 0.0f, 0.0f, 3.5f, 0, 1.0f));

			// Black Mage
			Add(MakeAbility("staff", "Staff Strike", E::Damage, S::Att, T::Enemy, 23, 0.0f, 1.8f, 0.0f, 0, 0.0f));
			{
				FAbility Fire = MakeAbility("fire", "Fire", E::Damage, S::Mag, T::Enemy, 50, 2.0f, 8.0f, 0.0f, 0, 1.0f);
				Fire.StatusId = "burn";
				Fire.StatusTurns = 3;
				Add(Fire);
			}
			{
				FAbility Blizzard = MakeAbility("blizzard", "Blizzard", E::Damage, S::Mag, T::Enemy, 36, 2.0f, 8.0f, 2.0f, 2, 2.0f);
				Blizzard.StatusId = "slow";
				Blizzard.StatusTurns = 2;
				Add(Blizzard);
			}
			Add(MakeAbility("meteor", "Meteor", E::Damage, S::Mag, T::Enemy, 79, 3.0f, 10.0f, 3.5f, 0, 4.0f));

			// White Mage
			Add(MakeAbility("cure", "Cure", E::Heal, S::Mag, T::Ally, 24, 0.0f, 6.0f, 0.0f, 0, 1.0f));
			{
				FAbility Haste = MakeAbility("haste", "Haste", E::Support, S::Mag, T::Ally, 0, 0.0f, 6.0f, 0.0f, 3, 1.5f);
				Haste.TgChange = 50;
				Add(Haste);
			}
			{
				FAbility Sanctuary = MakeAbility("sanctuary", "Sanctuary", E::Heal, S::Mag, T::Ally, 30, 0.0f, 8.0f, 3.5f, 0, 3.0f);
				Sanctuary.StatusId = "regen";
				Sanctuary.StatusTurns = 3;
				Add(Sanctuary);
			}
			// A revive's power is a share of the target's max HP, not damage.
			Add(MakeAbility("raise", "Raise", E::Revive, S::Mag, T::KoAlly, 0.3f, 0.0f, 5.0f, 0.0f, 4, 2.0f));

			return Out;
		}

		FJobDef MakeJob(const char* Id, const char* Name, int Hp, int AttDef, int MagDef,
			int AEva, int MEva, int Crit, int Speed, int Move, int Patience, int Sight,
			const char* A0, const char* A1, const char* A2, const char* A3,
			const char* Role0, const char* Role1 = nullptr)
		{
			FJobDef Job;
			Job.Id = Id;
			Job.Name = Name;
			Job.Roles.push_back(Role0);
			if (Role1)
			{
				Job.Roles.push_back(Role1);
			}
			Job.Stats.Set(EStat::Hp, Hp);
			Job.Stats.Set(EStat::AttDef, AttDef);
			Job.Stats.Set(EStat::MagDef, MagDef);
			Job.Stats.Set(EStat::AEva, AEva);
			Job.Stats.Set(EStat::MEva, MEva);
			Job.Stats.Set(EStat::Crit, Crit);
			Job.Stats.Set(EStat::Speed, Speed);
			Job.Stats.Set(EStat::Move, Move);
			Job.Stats.Set(EStat::Patience, Patience);
			Job.Stats.Set(EStat::Sight, Sight);
			Job.AbilityIds[0] = A0;
			Job.AbilityIds[1] = A1;
			Job.AbilityIds[2] = A2;
			Job.AbilityIds[3] = A3;
			return Job;
		}

		std::map<std::string, FJobDef> BuildJobs()
		{
			std::map<std::string, FJobDef> Out;
			auto Add = [&Out](FJobDef Job) { Out[Job.Id] = Job; };
			//                 id            name          hp  ad  md  ae  me  cr  sp  mv  pa  si
			Add(MakeJob("squire",     "Squire",      75,  8,  6,  8,  5,  8, 10,  7,  6,  9, "attack", "throw_stone", "focus", "brave_slash", "damage"));
			Add(MakeJob("knight",     "Knight",     105, 12,  6,  5,  5,  5,  6,  6,  7,  8, "attack", "shield_bash", "guard", "holy_blade", "tank", "damage"));
			Add(MakeJob("archer",     "Archer",      60,  6,  7, 15,  8, 15, 12,  7,  6, 13, "bow_shot", "aimed_shot", "pin_shot", "arrow_rain", "damage"));
			Add(MakeJob("monk",       "Monk",        80,  8,  5, 18,  8, 12, 12,  8,  5,  9, "punch", "wave_fist", "chakra", "earth_slash", "damage", "support"));
			Add(MakeJob("black_mage", "Black Mage",  60,  4, 12,  5, 12, 10,  8,  6,  8, 10, "staff", "fire", "blizzard", "meteor", "damage"));
			Add(MakeJob("white_mage", "White Mage",  65,  5, 13,  5, 15,  5,  8,  6,  8, 10, "raise", "cure", "haste", "sanctuary", "support"));
			return Out;
		}

		const std::map<std::string, FAbility>& Abilities()
		{
			static const std::map<std::string, FAbility> Table = BuildAbilities();
			return Table;
		}

		const std::map<std::string, FJobDef>& Jobs()
		{
			static const std::map<std::string, FJobDef> Table = BuildJobs();
			return Table;
		}
	}

	const FJobDef* FindJob(const std::string& JobId)
	{
		const auto Found = Jobs().find(JobId);
		return Found == Jobs().end() ? nullptr : &Found->second;
	}

	const FAbility* FindAbility(const std::string& AbilityId)
	{
		const auto Found = Abilities().find(AbilityId);
		return Found == Abilities().end() ? nullptr : &Found->second;
	}

	bool JobHasRole(const std::string& JobId, const std::string& Role)
	{
		const FJobDef* Job = FindJob(JobId);
		if (!Job)
		{
			return false;
		}
		for (const std::string& Each : Job->Roles)
		{
			if (Each == Role)
			{
				return true;
			}
		}
		return false;
	}

	const FAbility* JobAbility(const std::string& JobId, int Slot)
	{
		const FJobDef* Job = FindJob(JobId);
		if (!Job || Slot < 0 || Slot > 3)
		{
			return nullptr;
		}
		return FindAbility(Job->AbilityIds[Slot]);
	}

	const std::vector<const FJobDef*>& AllJobs()
	{
		static const std::vector<const FJobDef*> List = []
		{
			std::vector<const FJobDef*> Out;
			for (const auto& Pair : Jobs())
			{
				Out.push_back(&Pair.second);
			}
			return Out;
		}();
		return List;
	}
}
