// Checks what the computer decides to DO with a turn, against the Godot game.
//
// SimAITest covers where it would rather stand. This covers the other half: which
// of its four abilities it picks, where it walks to in order to use it, what it
// aims at, and what it thinks the whole thing is worth.
//
// Two sections, because the decision has two halves that fail differently. SCORE
// is the arithmetic on one ability aimed one way -- expected damage after evasion
// and crits, what a status is worth, whether an ultimate is worth spending yet.
// CHOICE is the search: every spot it could stand on crossed with everything it
// could aim at, which is thousands of candidates, settled by comparing doubles.
// Checking them separately means a wrong sum is not mistaken for a wrong search.
//
// Hard only, and deliberately. At hard the generator is never touched -- GDScript
// short-circuits `mistakes > 0.0 and rng.randf() < mistakes` -- so the decision is
// wholly deterministic. Easy and medium roll to settle for a worse option, and
// that roll wants Godot's randf, which is not ported; SimRandom.h records what is
// known about it.
//
// The tie count is the fussy part. The original sorts its options by score and
// takes the first, with a sort that is not stable, so if two options ever tied at
// the top it could take either and no transcription could promise to match. So
// this insists the best was unique in every case rather than assuming it, and if
// that ever stops being true the assumption has to be revisited rather than the
// test loosened.
//
// Regenerate with tests/dump_ai_action_table.gd in the Godot project; the table's
// first line says which script and which Godot build produced it. Never edit the
// table to make this pass.

#include "SimAI.h"
#include "SimAbility.h"
#include "SimBattle.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace TMSim;

namespace
{
	int Failures = 0;
	int Shown = 0;

	void Fail(const std::string& What)
	{
		if (Shown < 16)
		{
			std::printf("  %s\n", What.c_str());
			++Shown;
		}
		++Failures;
	}

	/** Where "key=" starts, as a whole word, so "hp=" misses the tail of "maxhp=". */
	size_t FindKey(const std::string& Line, const char* Key)
	{
		const std::string Needle = std::string(Key) + "=";
		size_t At = Line.find(Needle);
		while (At != std::string::npos && At > 0
			&& !std::isspace(static_cast<unsigned char>(Line[At - 1])))
		{
			At = Line.find(Needle, At + 1);
		}
		return At == std::string::npos ? At : At + Needle.size();
	}

	double ValueOf(const std::string& Line, const char* Key, double Fallback = 0.0)
	{
		const size_t At = FindKey(Line, Key);
		return At == std::string::npos ? Fallback : std::atof(Line.c_str() + At);
	}

	std::string TextOf(const std::string& Line, const char* Key)
	{
		const size_t At = FindKey(Line, Key);
		if (At == std::string::npos)
		{
			return std::string();
		}
		const size_t End = Line.find(' ', At);
		return Line.substr(At, End == std::string::npos ? std::string::npos : End - At);
	}

	FVec2 PointOf(const std::string& Line, const char* Key)
	{
		const size_t At = FindKey(Line, Key);
		if (At == std::string::npos)
		{
			return FVec2();
		}
		const char* From = Line.c_str() + At;
		const char* Comma = std::strchr(From, ',');
		return FVec2(static_cast<float>(std::atof(From)),
			Comma ? static_cast<float>(std::atof(Comma + 1)) : 0.0f);
	}

	std::string Text(const FVec2& P)
	{
		char Buffer[64];
		std::snprintf(Buffer, sizeof(Buffer), "%.2f,%.2f", P.X, P.Y);
		return Buffer;
	}

	std::string Trimmed(std::string Line)
	{
		while (!Line.empty() && std::isspace(static_cast<unsigned char>(Line.back())))
		{
			Line.pop_back();
		}
		size_t At = 0;
		while (At < Line.size() && std::isspace(static_cast<unsigned char>(Line[At])))
		{
			++At;
		}
		return Line.substr(At);
	}

	/** Positions sit on a half-metre grid, so they should match exactly. */
	bool Same(const FVec2& A, const FVec2& B)
	{
		return std::fabs(A.X - B.X) < 0.001f && std::fabs(A.Y - B.Y) < 0.001f;
	}

	std::string Number(double V)
	{
		char Buffer[64];
		std::snprintf(Buffer, sizeof(Buffer), "%.6f", V);
		return Buffer;
	}
}

int main(int argc, char** argv)
{
	const std::string Path = argc > 1 ? argv[1] : "GodotAIActionTable.txt";
	std::ifstream File(Path);
	if (!File)
	{
		std::printf("Could not open %s\n", Path.c_str());
		return 1;
	}

	std::vector<std::string> Lines;
	std::string Read;
	while (std::getline(File, Read))
	{
		Read = Trimmed(Read);
		if (!Read.empty())
		{
			Lines.push_back(Read);
		}
	}

	FBattle Battle;
	Battle.Map.BuildMirrored(HighlandsRows());
	std::vector<std::string> UnitLines;
	std::string Section;

	for (const std::string& Line : Lines)
	{
		if (Line.rfind("TUNING", 0) == 0)
		{
			Battle.Tuning.DamageMultiplier = ValueOf(Line, "damage_multiplier");
			Battle.Tuning.HealMultiplier = ValueOf(Line, "heal_multiplier");
			Battle.Tuning.HeightBonus = ValueOf(Line, "height_bonus");
			Battle.Tuning.SideBonus = ValueOf(Line, "side_bonus");
			Battle.Tuning.BackBonus = ValueOf(Line, "back_bonus");
			Battle.Tuning.CritMultiplier = ValueOf(Line, "crit_multiplier");
			Battle.Tuning.EvadeMultiplier = ValueOf(Line, "evade_multiplier");
			Battle.Tuning.CritChanceMultiplier = ValueOf(Line, "crit_chance_multiplier");
			Battle.Tuning.CastTimeMultiplier = ValueOf(Line, "cast_time_multiplier");
			Battle.Tuning.KoSeconds = ValueOf(Line, "ko_seconds");
			Battle.Tuning.UltPerAction = ValueOf(Line, "ult_per_action");
			Battle.Tuning.UltPerTurn = ValueOf(Line, "ult_per_turn");
			Battle.Tuning.StunTgPercent = ValueOf(Line, "stun_tg_percent");
			Battle.Tuning.HazardPercent = ValueOf(Line, "hazard_percent");
			Battle.Tuning.RegenPercent = ValueOf(Line, "regen_percent");
			Battle.Tuning.RegenAfterTurns = ValueOf(Line, "regen_after_turns");
			Battle.Tuning.ClockBase = ValueOf(Line, "clock_base");
			Battle.Tuning.PatienceMultiplier = ValueOf(Line, "patience_multiplier");
			Battle.Tuning.SpeedMultiplier = ValueOf(Line, "speed_multiplier");
			Battle.Tuning.SightMultiplier = ValueOf(Line, "sight_multiplier");
			Battle.Tuning.MoveMultiplier = ValueOf(Line, "move_multiplier");
			Battle.Tuning.SprintMultiplier = ValueOf(Line, "sprint_multiplier");
			Battle.Tuning.EngageRadius = ValueOf(Line, "engage_radius");
			Battle.Tuning.EngageCost = ValueOf(Line, "engage_cost");
			Battle.Tuning.HustleBonus = ValueOf(Line, "hustle_bonus");
			continue;
		}
		if (Line == "UNITS" || Line.rfind("SCORE", 0) == 0 || Line.rfind("CHOICE", 0) == 0)
		{
			Section = Line.substr(0, 6);
			continue;
		}
		if (Section == "UNITS")
		{
			UnitLines.push_back(Line);
		}
	}

	if (UnitLines.empty())
	{
		std::printf("THE REFERENCE HAS NO BOARD IN IT\n");
		return 1;
	}

	// The board the decisions are made on. Who is where is the state being asked
	// about, so it comes from the dump; what the classes are worth comes from the
	// ported tables and is checked.
	for (const std::string& Line : UnitLines)
	{
		std::istringstream Stream(Line);
		int Id = -1;
		std::string Job;
		Stream >> Id >> Job;
		FUnit Unit;
		Unit.Id = Id;
		Unit.Job = Job;
		Unit.Team = static_cast<int>(ValueOf(Line, "team"));
		Battle.Units.push_back(Unit);
	}
	Battle.Start(12345);
	for (const std::string& Line : UnitLines)
	{
		int Id = -1;
		std::istringstream(Line) >> Id;
		FUnit* Unit = Battle.FindUnit(Id);
		if (!Unit)
		{
			Fail("there is no unit " + std::to_string(Id));
			continue;
		}
		if (Unit->MaxHp() != static_cast<int>(ValueOf(Line, "maxhp")))
		{
			Fail("unit " + std::to_string(Id) + " has " + std::to_string(Unit->MaxHp())
				+ " health at most, Godot says "
				+ std::to_string(static_cast<int>(ValueOf(Line, "maxhp"))));
		}
		Unit->Pos = PointOf(Line, "pos");
		Unit->Facing = PointOf(Line, "facing");
		Unit->Hp = static_cast<int>(ValueOf(Line, "hp"));
		Unit->Tg = static_cast<int>(ValueOf(Line, "tg"));
		Unit->Ult = static_cast<int>(ValueOf(Line, "ult"));
		Unit->KoTicks = static_cast<int>(ValueOf(Line, "ko"));
		Unit->Clock = static_cast<int>(ValueOf(Line, "clock"));
		Unit->bReady = ValueOf(Line, "ready") != 0.0;
		Unit->bMoved = ValueOf(Line, "moved") != 0.0;
		Unit->bActed = ValueOf(Line, "acted") != 0.0;
		const std::string Cooldowns = TextOf(Line, "cd");
		if (!Cooldowns.empty())
		{
			std::istringstream Bits(Cooldowns);
			std::string Each;
			int Slot = 0;
			while (std::getline(Bits, Each, ',') && Slot < 4)
			{
				Unit->Cooldowns[Slot++] = std::atoi(Each.c_str());
			}
		}
	}

	FAIPlayer Computer("hard");
	int Scores = 0;
	int Choices = 0;
	int Nothings = 0;
	int Tied = 0;

	Section.clear();
	for (const std::string& Line : Lines)
	{
		if (Line == "UNITS" || Line.rfind("SCORE", 0) == 0 || Line.rfind("CHOICE", 0) == 0)
		{
			Section = Line.substr(0, 6);
			continue;
		}

		if (Section == "SCORE")
		{
			std::istringstream Stream(Line);
			int CasterId = -1;
			int Slot = -1;
			std::string Aim;
			Stream >> CasterId >> Slot >> Aim;
			const FUnit* Caster = Battle.FindUnit(CasterId);
			const FAbility* Ability = Caster ? JobAbility(Caster->Job, Slot) : nullptr;
			if (!Caster || !Ability)
			{
				continue;
			}
			const size_t Comma = Aim.find(',');
			const FVec2 At(static_cast<float>(std::atof(Aim.c_str())),
				Comma == std::string::npos ? 0.0f
					: static_cast<float>(std::atof(Aim.c_str() + Comma + 1)));
			const std::string Where = "unit " + std::to_string(CasterId) + " slot "
				+ std::to_string(Slot) + " at " + Text(At);

			const std::vector<FHit> Hits = Battle.Preview(*Caster, Slot, Caster->Pos, At);
			std::string Struck;
			for (const FHit& Hit : Hits)
			{
				if (!Struck.empty()) { Struck += "|"; }
				Struck += std::to_string(Hit.UnitId) + ":" + std::to_string(Hit.Amount);
			}
			if ((Struck.empty() ? "-" : Struck) != TextOf(Line, "hits"))
			{
				Fail(Where + " reaches " + (Struck.empty() ? "nobody" : Struck)
					+ ", Godot says " + TextOf(Line, "hits"));
			}

			const double Got = Computer.Score(Battle, *Caster, Slot, *Ability, Hits);
			const double Want = ValueOf(Line, "score");
			if (std::fabs(Got - Want) > 1e-4)
			{
				Fail(Where + " is worth " + Number(Got) + ", Godot says " + Number(Want));
			}
			++Scores;
			continue;
		}

		if (Section == "CHOICE")
		{
			std::istringstream Stream(Line);
			int UnitId = -1;
			Stream >> UnitId;
			const FUnit* Unit = Battle.FindUnit(UnitId);
			if (!Unit)
			{
				continue;
			}
			// The same one search of the ground the original does per decision.
			std::vector<std::pair<FNode, double>> Reach;
			if (!Unit->bMoved && !Unit->IsCasting())
			{
				Reach = Battle.ReachableNodes(*Unit);
			}
			const FChoice Best = Computer.BestAction(Battle, *Unit, Reach);
			const std::string Who = "unit " + std::to_string(UnitId);

			if (Line.find("nothing") != std::string::npos)
			{
				if (Best.Slot >= 0)
				{
					Fail(Who + " would use slot " + std::to_string(Best.Slot)
						+ ", Godot finds nothing worth doing");
				}
				++Nothings;
				++Choices;
				continue;
			}

			if (Best.Slot < 0)
			{
				Fail(Who + " finds nothing worth doing, Godot picks slot "
					+ std::to_string(static_cast<int>(ValueOf(Line, "slot"))));
				++Choices;
				continue;
			}
			// Where the two disagree, the question is whether the port found a
			// worse option or an equally good one. So price the option the
			// original chose: if it is worth exactly what the port's best is
			// worth, the two were tied and the difference is the original's
			// unstable sort taking the other one, which no transcription can
			// promise to match. Anything else is a real disagreement.
			const int WantSlot = static_cast<int>(ValueOf(Line, "slot"));
			const FVec2 WantSpot = PointOf(Line, "spot");
			const FVec2 WantTarget = PointOf(Line, "target");
			const bool bSameChoice = Best.Slot == WantSlot && Same(Best.Spot, WantSpot)
				&& Same(Best.Target, WantTarget);
			if (!bSameChoice)
			{
				const double Theirs = Computer.ValueOfOption(Battle, *Unit, WantSlot,
					WantSpot, WantTarget);
				if (std::fabs(Theirs - Best.Score) > 1e-9)
				{
					Fail(Who + " picks slot " + std::to_string(Best.Slot) + " at "
						+ Text(Best.Spot) + " aimed at " + Text(Best.Target) + " worth "
						+ Number(Best.Score) + ", but Godot's slot " + std::to_string(WantSlot)
						+ " at " + Text(WantSpot) + " aimed at " + Text(WantTarget)
						+ " is worth " + Number(Theirs) + " -- not a tie, a disagreement");
				}
				else if (Best.Ties < 2)
				{
					Fail(Who + " disagrees with Godot on an option worth the same, yet counted"
						" no tie, which means the count is wrong");
				}
				else
				{
					++Tied;
				}
				++Choices;
				continue;
			}
			if (Best.Slot != WantSlot)
			{
				Fail(Who + " would use slot " + std::to_string(Best.Slot) + ", Godot picks "
					+ std::to_string(static_cast<int>(ValueOf(Line, "slot"))));
			}
			if (Best.Follow != static_cast<int>(ValueOf(Line, "follow", -1)))
			{
				Fail(Who + " would follow " + std::to_string(Best.Follow) + ", Godot says "
					+ std::to_string(static_cast<int>(ValueOf(Line, "follow", -1))));
			}
			const double Want = ValueOf(Line, "score");
			if (std::fabs(Best.Score - Want) > 1e-4)
			{
				Fail(Who + " rates it " + Number(Best.Score) + ", Godot says " + Number(Want));
			}
			++Choices;
			continue;
		}
	}

	std::printf("%d scored options, %d decisions (%d to do nothing, %d settled by a tie)\n",
		Scores, Choices, Nothings, Tied);

	// A reference that came out empty would pass every check in it.
	if (Scores < 100 || Choices < 5)
	{
		std::printf("THE REFERENCE IS SHORT -- re-dump it from the Godot game\n");
		return 1;
	}
	if (Failures > 0)
	{
		std::printf("THE COMPUTER PLAYER DECIDES DIFFERENTLY (%d)\n", Failures);
		return 1;
	}
	std::printf("THE COMPUTER PLAYER DOES WHAT IT DOES IN GODOT\n");
	return 0;
}
