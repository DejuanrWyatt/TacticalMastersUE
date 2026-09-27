// Checks walking against the Godot version: the ground, where a unit can get
// to, what it costs, and the way it goes.
//
// The last of those is the fussy one and the reason the pathfinder was ported
// as a transcription rather than rewritten. Two routes of the same length are
// equally correct, so a path only matches if the heap pops ties in the same
// order and the neighbours are tried in the same order. A replay that picks the
// other route is a replay that goes wrong, quietly, several seconds later.

#include "SimBattle.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace TMSim;

namespace
{
	int Failures = 0;

	void Fail(const std::string& What)
	{
		if (Failures < 12)
		{
			std::printf("  %s\n", What.c_str());
		}
		++Failures;
	}

	double ValueOf(const std::string& Line, const char* Key, double Fallback)
	{
		const std::string Needle = std::string(Key) + "=";
		const size_t At = Line.find(Needle);
		return At == std::string::npos ? Fallback : std::atof(Line.c_str() + At + Needle.size());
	}

	struct FSeedUnit
	{
		int Id = 0;
		int Team = 0;
		std::string Job;
		FVec2 Pos;
	};
}

int main(int argc, char** argv)
{
	const char* Path = argc > 1 ? argv[1] : "GodotMoveTable.txt";
	std::ifstream File(Path);
	if (!File)
	{
		std::printf("could not open %s\n", Path);
		return 1;
	}

	FBattle Battle;
	Battle.Map.BuildMirrored(HighlandsRows());

	std::vector<FSeedUnit> Seeds;
	std::map<std::string, FJobStats> JobStats;
	int Checked = 0;

	std::string Line;
	enum class ESection { None, Reach, Paths } Section = ESection::None;

	while (std::getline(File, Line))
	{
		if (!Line.empty() && Line.back() == '\r')
		{
			Line.pop_back();
		}

		if (Line.rfind("MAP ", 0) == 0)
		{
			const int TilesX = static_cast<int>(ValueOf(Line, "tiles", 0));
			if (Battle.Map.TilesX != 12 || Battle.Map.NavX != 48)
			{
				Fail("the ported map is " + std::to_string(Battle.Map.TilesX) + " tiles wide, Godot's is 12");
			}
			(void)TilesX;
			continue;
		}
		if (Line.rfind("HEIGHTS ", 0) == 0)
		{
			// The ground itself. If this is wrong nothing above it can be right.
			std::vector<int> Want;
			std::stringstream Stream(Line.substr(8));
			std::string Cell;
			while (std::getline(Stream, Cell, ','))
			{
				if (!Cell.empty())
				{
					Want.push_back(std::atoi(Cell.c_str()));
				}
			}
			if (Want != Battle.Map.Heights)
			{
				Fail("the ground does not match Godot's (" + std::to_string(Want.size())
					+ " tiles expected, " + std::to_string(Battle.Map.Heights.size()) + " built)");
				for (size_t i = 0; i < Want.size() && i < Battle.Map.Heights.size(); ++i)
				{
					if (Want[i] != Battle.Map.Heights[i])
					{
						Fail("  first difference at tile " + std::to_string(i) + ": ported "
							+ std::to_string(Battle.Map.Heights[i]) + ", Godot " + std::to_string(Want[i]));
						break;
					}
				}
			}
			else
			{
				std::printf("the ground matches: %d tiles\n", static_cast<int>(Want.size()));
			}
			++Checked;
			continue;
		}
		if (Line.rfind("TUNING", 0) == 0)
		{
			Battle.Tuning.MoveMultiplier = ValueOf(Line, "move_multiplier", 1.0);
			Battle.Tuning.SprintMultiplier = ValueOf(Line, "sprint_multiplier", 1.25);
			Battle.Tuning.EngageRadius = ValueOf(Line, "engage_radius", 1.8);
			Battle.Tuning.EngageCost = ValueOf(Line, "engage_cost", 1.0);
			continue;
		}
		if (Line.rfind("REACH", 0) == 0) { Section = ESection::Reach; continue; }
		if (Line.rfind("PATHS", 0) == 0) { Section = ESection::Paths; continue; }
		if (Line.rfind("UNITS", 0) == 0) { Section = ESection::None; continue; }

		std::istringstream Stream(Line);

		// A unit line: "0 knight team=0 move=6 pos=2.75,4.75"
		if (Section == ESection::None && Line.find("team=") != std::string::npos)
		{
			FSeedUnit Seed;
			std::string Job;
			if (!(Stream >> Seed.Id >> Job))
			{
				continue;
			}
			Seed.Job = Job;
			Seed.Team = static_cast<int>(ValueOf(Line, "team", 0));
			const size_t At = Line.find("pos=");
			if (At != std::string::npos)
			{
				float X = 0.0f, Y = 0.0f;
				std::sscanf(Line.c_str() + At + 4, "%f,%f", &X, &Y);
				Seed.Pos = FVec2(X, Y);
			}
			Seeds.push_back(Seed);

			if (const FJobDef* Job2 = FindJob(Seed.Job))
			{
				JobStats[Seed.Job] = Job2->Stats;
				if (Job2->Stats.Get(EStat::Move) != static_cast<int>(ValueOf(Line, "move", -1)))
				{
					Fail(Seed.Job + " walks " + std::to_string(Job2->Stats.Get(EStat::Move))
						+ " here and " + std::to_string(static_cast<int>(ValueOf(Line, "move", -1))) + " in Godot");
				}
			}
			else
			{
				Fail("no class registered called " + Seed.Job);
			}

			// Put the units on the board as Godot has them, so what follows is
			// about walking rather than about where anybody started.
			FUnit Unit;
			Unit.Id = Seed.Id;
			Unit.Team = Seed.Team;
			Unit.Job = Seed.Job;
			Unit.Pos = Seed.Pos;
			Battle.Units.push_back(Unit);
			for (FUnit& U : Battle.Units)
			{
				U.Stats = &JobStats[U.Job];
				U.Hp = U.MaxHp();
			}
			continue;
		}

		if (Section == ESection::Reach)
		{
			int Id = 0;
			std::string Mode;
			int Count = 0;
			if (!(Stream >> Id >> Mode >> Count))
			{
				continue;
			}
			const FUnit* Unit = Battle.FindUnit(Id);
			if (!Unit)
			{
				continue;
			}
			std::map<long long, double> Want;
			std::string Cell;
			while (Stream >> Cell)
			{
				int Nx = 0, Ny = 0;
				double C = 0.0;
				if (std::sscanf(Cell.c_str(), "%d:%d:%lf", &Nx, &Ny, &C) == 3)
				{
					Want[static_cast<long long>(Ny) * 1000 + Nx] = C;
				}
			}

			const auto Got = Battle.ReachableNodes(*Unit, Mode == "sprint");
			++Checked;
			if (static_cast<int>(Got.size()) != Count)
			{
				Fail("unit " + std::to_string(Id) + " " + Mode + ": reaches "
					+ std::to_string(Got.size()) + " spots, Godot reaches " + std::to_string(Count));
				continue;
			}
			for (const auto& Pair : Got)
			{
				const long long Key = static_cast<long long>(Pair.first.Y) * 1000 + Pair.first.X;
				const auto Found = Want.find(Key);
				if (Found == Want.end())
				{
					Fail("unit " + std::to_string(Id) + " " + Mode + ": reaches "
						+ std::to_string(Pair.first.X) + "," + std::to_string(Pair.first.Y)
						+ " and Godot does not");
					break;
				}
				if (std::abs(Found->second - Pair.second) > 0.001)
				{
					char Buffer[192];
					std::snprintf(Buffer, sizeof(Buffer),
						"unit %d %s: %d,%d costs %.4f here and %.4f in Godot",
						Id, Mode.c_str(), Pair.first.X, Pair.first.Y, Pair.second, Found->second);
					Fail(Buffer);
					break;
				}
			}
			continue;
		}

		if (Section == ESection::Paths)
		{
			int Id = 0;
			std::string Target;
			if (!(Stream >> Id >> Target))
			{
				continue;
			}
			int Nx = 0, Ny = 0;
			if (std::sscanf(Target.c_str(), "%d:%d", &Nx, &Ny) != 2)
			{
				continue;
			}
			const FUnit* Unit = Battle.FindUnit(Id);
			if (!Unit)
			{
				continue;
			}
			std::vector<FVec2> Want;
			std::string Cell;
			while (Stream >> Cell)
			{
				float X = 0.0f, Y = 0.0f;
				if (std::sscanf(Cell.c_str(), "%f,%f", &X, &Y) == 2)
				{
					Want.push_back(FVec2(X, Y));
				}
			}

			const auto Got = Battle.PathTo(*Unit, FNode{ Nx, Ny });
			++Checked;
			if (Got.size() != Want.size())
			{
				Fail("unit " + std::to_string(Id) + " to " + Target + ": path is "
					+ std::to_string(Got.size()) + " steps, Godot's is " + std::to_string(Want.size()));
				continue;
			}
			for (size_t i = 0; i < Got.size(); ++i)
			{
				if (std::abs(Got[i].X - Want[i].X) > 0.01f || std::abs(Got[i].Y - Want[i].Y) > 0.01f)
				{
					char Buffer[192];
					std::snprintf(Buffer, sizeof(Buffer),
						"unit %d to %s: step %d is %.2f,%.2f here and %.2f,%.2f in Godot",
						Id, Target.c_str(), static_cast<int>(i), Got[i].X, Got[i].Y, Want[i].X, Want[i].Y);
					Fail(Buffer);
					break;
				}
			}
			continue;
		}
	}

	std::printf("%d checks over %d units\n", Checked, static_cast<int>(Battle.Units.size()));
	if (Checked == 0)
	{
		std::printf("\nNOTHING WAS CHECKED\n");
		return 1;
	}
	std::printf("\n%s\n", Failures == 0
		? "WALKING GOES WHERE IT GOES IN GODOT"
		: "DIVERGED FROM GODOT");
	return Failures == 0 ? 0 : 1;
}
