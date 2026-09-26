// Checks what an ability does against the Godot version.
//
// GodotCalcTable.txt came out of the Godot game: every built-in ability, used
// by each of the six classes against each of the six, from spots at different
// ground heights, with the target facing four ways and at two states of health.
// For each it wrote down the damage or healing, the chance of being evaded and
// the chance of a critical hit.
//
// The rows carry the ground levels rather than the map, because the map is not
// ported yet. That is the point: what an ability does can be checked now, and
// the map slice only has to get the levels right when it arrives.

#include "SimAbility.h"
#include "SimBattle.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

using namespace TMSim;

namespace
{
	double ValueOf(const std::string& Line, const char* Key, double Fallback)
	{
		const std::string Needle = std::string(Key) + "=";
		const size_t At = Line.find(Needle);
		return At == std::string::npos ? Fallback : std::atof(Line.c_str() + At + Needle.size());
	}
}

int main(int argc, char** argv)
{
	const char* Path = argc > 1 ? argv[1] : "GodotCalcTable.txt";
	std::ifstream File(Path);
	if (!File)
	{
		std::printf("could not open %s\n", Path);
		return 1;
	}

	FBattle Battle;
	std::map<std::string, FJobStats> GodotStats;

	int Checked = 0;
	int Failures = 0;
	int StatMismatches = 0;
	std::string Line;

	while (std::getline(File, Line))
	{
		if (!Line.empty() && Line.back() == '\r')
		{
			Line.pop_back();
		}

		if (Line.rfind("TUNING", 0) == 0)
		{
			FTuning& T = Battle.Tuning;
			T.DamageMultiplier = ValueOf(Line, "damage_multiplier", 0.5);
			T.HealMultiplier = ValueOf(Line, "heal_multiplier", 1.0);
			T.HeightBonus = ValueOf(Line, "height_bonus", 0.1);
			T.SideBonus = ValueOf(Line, "side_bonus", 1.1);
			T.BackBonus = ValueOf(Line, "back_bonus", 1.25);
			T.CritMultiplier = ValueOf(Line, "crit_multiplier", 1.5);
			T.EvadeMultiplier = ValueOf(Line, "evade_multiplier", 1.0);
			T.CritChanceMultiplier = ValueOf(Line, "crit_chance_multiplier", 1.0);
			continue;
		}

		std::istringstream Stream(Line);
		std::string First;
		if (!(Stream >> First))
		{
			continue;
		}

		// A class's stats, as Godot has them. Checking these against the table
		// ported into C++ is half the point: a wrong stat would otherwise show up
		// as a wrong damage number and look like a broken formula.
		if (Line.find("attdef=") != std::string::npos && Line.find("sight=") != std::string::npos)
		{
			FJobStats Stats;
			Stats.Set(EStat::Hp, static_cast<int>(ValueOf(Line, "hp", 0)));
			Stats.Set(EStat::AttDef, static_cast<int>(ValueOf(Line, "attdef", 0)));
			Stats.Set(EStat::MagDef, static_cast<int>(ValueOf(Line, "magdef", 0)));
			Stats.Set(EStat::AEva, static_cast<int>(ValueOf(Line, "aeva", 0)));
			Stats.Set(EStat::MEva, static_cast<int>(ValueOf(Line, "meva", 0)));
			Stats.Set(EStat::Crit, static_cast<int>(ValueOf(Line, "crit", 0)));
			Stats.Set(EStat::Speed, static_cast<int>(ValueOf(Line, "speed", 0)));
			Stats.Set(EStat::Move, static_cast<int>(ValueOf(Line, "move", 0)));
			Stats.Set(EStat::Patience, static_cast<int>(ValueOf(Line, "patience", 0)));
			Stats.Set(EStat::Sight, static_cast<int>(ValueOf(Line, "sight", 0)));
			GodotStats[First] = Stats;

			const FJobDef* Ported = FindJob(First);
			if (!Ported)
			{
				std::printf("  no class called %s is registered\n", First.c_str());
				++StatMismatches;
			}
			else
			{
				for (int i = 0; i < StatCount; ++i)
				{
					const EStat Stat = static_cast<EStat>(i);
					if (Ported->Stats.Get(Stat) != Stats.Get(Stat))
					{
						std::printf("  %s %s: ported %d, Godot %d\n", First.c_str(), StatName(Stat),
							Ported->Stats.Get(Stat), Stats.Get(Stat));
						++StatMismatches;
					}
				}
			}
			continue;
		}

		// attacker slot target fx fy fromx fromy tx ty fromlvl tlvl hp value evade crit
		int Slot = 0, FromLevel = 0, TargetLevel = 0, Hp = 0;
		int WantValue = 0, WantEvade = 0, WantCrit = 0;
		float Fx = 0, Fy = 0, FromX = 0, FromY = 0, TargetX = 0, TargetY = 0;
		std::string TargetJob;
		if (!(Stream >> Slot >> TargetJob >> Fx >> Fy >> FromX >> FromY >> TargetX >> TargetY
			>> FromLevel >> TargetLevel >> Hp >> WantValue >> WantEvade >> WantCrit))
		{
			continue;
		}

		const FAbility* Ability = JobAbility(First, Slot);
		const auto AttackerStats = GodotStats.find(First);
		const auto TargetStats = GodotStats.find(TargetJob);
		if (!Ability || AttackerStats == GodotStats.end() || TargetStats == GodotStats.end())
		{
			std::printf("  missing data for %s slot %d against %s\n", First.c_str(), Slot, TargetJob.c_str());
			++Failures;
			continue;
		}

		FUnit User;
		User.Job = First;
		User.Stats = &AttackerStats->second;
		User.Hp = User.MaxHp();

		FUnit Target;
		Target.Job = TargetJob;
		Target.Stats = &TargetStats->second;
		Target.Hp = Hp;
		Target.Facing = FVec2(Fx, Fy);
		Target.Pos = FVec2(TargetX, TargetY);

		const FVec2 From(FromX, FromY);
		const int GotValue = Battle.CalcAmount(User, *Ability, From, Target, Target.Pos, FromLevel, TargetLevel);
		const int GotEvade = Battle.EvadeChance(Target, *Ability, &User);
		const int GotCrit = Battle.CritChance(User);
		++Checked;

		if (GotValue != WantValue || GotEvade != WantEvade || GotCrit != WantCrit)
		{
			if (Failures < 10)
			{
				std::printf("  %s slot %d (%s) vs %s facing (%.0f,%.0f) levels %d->%d hp %d:\n"
					"      got value=%d evade=%d crit=%d, Godot had value=%d evade=%d crit=%d\n",
					First.c_str(), Slot, Ability->Name.c_str(), TargetJob.c_str(), Fx, Fy,
					FromLevel, TargetLevel, Hp, GotValue, GotEvade, GotCrit, WantValue, WantEvade, WantCrit);
			}
			++Failures;
		}
	}

	std::printf("%d class stat blocks, %d ability results checked\n",
		static_cast<int>(GodotStats.size()), Checked);
	if (StatMismatches > 0)
	{
		std::printf("%d stat(s) disagree with Godot\n", StatMismatches);
	}
	if (Checked == 0)
	{
		std::printf("\nNOTHING WAS CHECKED\n");
		return 1;
	}
	std::printf("\n%s\n", (Failures == 0 && StatMismatches == 0)
		? "ABILITIES DO WHAT THEY DO IN GODOT"
		: "DIVERGED FROM GODOT");
	return (Failures == 0 && StatMismatches == 0) ? 0 : 1;
}
