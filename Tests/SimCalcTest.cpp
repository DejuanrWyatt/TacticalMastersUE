// Checks what an ability does against its recorded baseline.
//
// Baselines/CalcTable.txt (first recorded from the Godot version): every built-in ability, used
// by each of the six classes against each of the six, from spots at different
// ground heights, with the target facing four ways and at two states of health.
// For each it wrote down the damage or healing, the chance of being evaded and
// the chance of a critical hit.
//
// The rows carry the ground levels rather than the map, because the map is not
// ported yet. That is the point: what an ability does can be checked now, and
// the map slice only has to get the levels right when it arrives.
//
// With --rebaseline the table is written again from the rules (Baseline.h):
// the classes' stats as the rules have them, and every result.
//
//   SimCalcTest [Baselines/CalcTable.txt] [--rebaseline]

#include "Baseline.h"
#include "SimAbility.h"
#include "SimBattle.h"

#include <algorithm>
#include <cmath>
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
	const std::vector<std::string> Args = TMBaseline::Paths(argc, argv);
	const std::string PathText = Args.empty() ? std::string("Baselines/CalcTable.txt") : Args[0];
	const char* Path = PathText.c_str();
	TMBaseline::FWriter Writer;
	Writer.bOn = TMBaseline::Asked(argc, argv);
	Writer.Path = PathText;
	std::ifstream File(Path);
	if (!File)
	{
		std::printf("could not open %s\n", Path);
		return 1;
	}

	FBattle Battle;
	std::map<std::string, FJobStats> TableStats;

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
		Writer.Read(Line);

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

		// A class's stats, as the baseline has them. Checking these against the table
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
			TableStats[First] = Stats;

			const FJobDef* Ported = FindJob(First);
			if (Writer.bOn && Ported)
			{
				// Written again: the rules' own stats, and the results below worked from them.
				std::string Row = "  " + First;
				const EStat Order[] = { EStat::Hp, EStat::AttDef, EStat::MagDef, EStat::AEva, EStat::MEva, EStat::Crit,
					EStat::Speed, EStat::Move, EStat::Patience, EStat::Sight };
				const char* Keys[] = { "hp", "attdef", "magdef", "aeva", "meva", "crit", "speed", "move", "patience", "sight" };
				bool bDiffers = false;
				for (int k = 0; k < 10; ++k)
				{
					Row += std::string(" ") + Keys[k] + "=" + std::to_string(Ported->Stats.Get(Order[k]));
					bDiffers = bDiffers || Ported->Stats.Get(Order[k]) != Stats.Get(Order[k]);
				}
				if (bDiffers)
				{
					Writer.Set(Row);
				}
				TableStats[First] = Ported->Stats;
				continue;
			}
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
						std::printf("  %s %s: the rules %d, the baseline %d\n", First.c_str(), StatName(Stat),
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
		const auto AttackerStats = TableStats.find(First);
		const auto TargetStats = TableStats.find(TargetJob);
		if (!Ability || AttackerStats == TableStats.end() || TargetStats == TableStats.end())
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

		// The odds the aim shows (2026-10-02): the four outcomes add up to 100,
		// they split the way the dice are thrown, and the knockout chance is the
		// sum of the outcomes that leave it on nothing.
		if (Ability->Effect == EEffect::Damage)
		{
			const FOdds Odds = Battle.OddsOf(User, *Ability, Target, GotValue);
			const double Sum = Odds.Hit + Odds.Crit + Odds.Graze + Odds.Dodge;
			const double Ko = (GotValue >= Hp ? Odds.Hit : 0.0)
				+ (std::max(1, RoundToInt(GotValue * Battle.Tuning.CritMultiplier)) >= Hp ? Odds.Crit : 0.0)
				+ (std::max(Combat::MinimumDamage, RoundToInt(GotValue * Combat::GrazeDamage)) >= Hp ? Odds.Graze : 0.0);
			if (std::fabs(Sum - 100.0) > 0.001 || std::fabs(Odds.Hit + Odds.Crit - (100.0 - GotEvade)) > 0.001
				|| std::fabs(Odds.Graze + Odds.Dodge - GotEvade) > 0.001 || std::fabs(Odds.Ko - Ko) > 0.001
				|| Odds.HitAmount != GotValue)
			{
				if (Failures < 10)
				{
					std::printf("  %s slot %d vs %s: odds hit %.2f crit %.2f graze %.2f dodge %.2f ko %.2f (want ko %.2f)\n",
						First.c_str(), Slot, TargetJob.c_str(), Odds.Hit, Odds.Crit, Odds.Graze, Odds.Dodge, Odds.Ko, Ko);
				}
				++Failures;
			}
		}

		if (Writer.bOn)
		{
			if (GotValue != WantValue || GotEvade != WantEvade || GotCrit != WantCrit)
			{
				// The situation as it was written, then the rules' three numbers.
				size_t Cut = Line.size();
				for (int k = 0; k < 3 && Cut > 0; ++k)
				{
					Cut = Line.find_last_of(' ', Cut - 1);
				}
				Writer.Set(Line.substr(0, Cut) + " " + std::to_string(GotValue) + " " + std::to_string(GotEvade) + " " + std::to_string(GotCrit));
			}
			continue;
		}
		if (GotValue != WantValue || GotEvade != WantEvade || GotCrit != WantCrit)
		{
			if (Failures < 10)
			{
				std::printf("  %s slot %d (%s) vs %s facing (%.0f,%.0f) levels %d->%d hp %d:\n"
					"      got value=%d evade=%d crit=%d, the baseline has value=%d evade=%d crit=%d\n",
					First.c_str(), Slot, Ability->Name.c_str(), TargetJob.c_str(), Fx, Fy,
					FromLevel, TargetLevel, Hp, GotValue, GotEvade, GotCrit, WantValue, WantEvade, WantCrit);
			}
			++Failures;
		}
	}

	std::printf("%d class stat blocks, %d ability results checked\n",
		static_cast<int>(TableStats.size()), Checked);
	if (Writer.bOn)
	{
		return Writer.Finish() ? 0 : 1;
	}
	if (StatMismatches > 0)
	{
		std::printf("%d stat(s) disagree with the baseline\n", StatMismatches);
	}
	if (Checked == 0)
	{
		std::printf("\nNOTHING WAS CHECKED\n");
		return 1;
	}
	std::printf("\n%s\n", (Failures == 0 && StatMismatches == 0)
		? "ABILITIES DO WHAT THEIR BASELINE SAYS"
		: "DIVERGED FROM THE BASELINE");
	return (Failures == 0 && StatMismatches == 0) ? 0 : 1;
}
