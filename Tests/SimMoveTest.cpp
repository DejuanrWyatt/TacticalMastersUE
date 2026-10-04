// Checks walking against its recorded baseline (Baselines/MoveTable.txt, first
// recorded from the Godot version): the ground, where a unit can get to, what
// it costs, and the way it goes. With --rebaseline the table is written again
// from the rules (Baseline.h).
//
//   SimMoveTest [Baselines/MoveTable.txt] [--rebaseline]
//
// The last of those is the fussy one and the reason the pathfinder was ported
// as a transcription rather than rewritten. Two routes of the same length are
// equally correct, so a path only matches if the heap pops ties in the same
// order and the neighbours are tried in the same order. A replay that picks the
// other route is a replay that goes wrong, quietly, several seconds later.

#include "Baseline.h"
#include "SimBattle.h"

#include <algorithm>
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

	/**
	 * Waypoints (2026-10-01; not Godot's): a walk by a spot on the way costs
	 * the legs it is made of, never less than the shortest way, never more than
	 * the unit's move; the order checks that and the walk goes by the spot.
	 */
	int CheckWaypoints(const FBattle& Original)
	{
		int Checks = 0;
		for (const FUnit& Each : Original.Units)
		{
			if (!Each.IsAlive())
			{
				continue;
			}
			FBattle Battle = Original;
			FUnit& Unit = *Battle.FindUnit(Each.Id);
			Unit.bReady = true;
			Unit.bMoved = false;
			Unit.bActed = false;
			const double Budget = Battle.MoveOf(Unit);
			const auto Plain = Battle.ReachableNodes(Unit);
			std::map<std::pair<int, int>, double> PlainCost;
			for (const auto& Pair : Plain)
			{
				PlainCost[{ Pair.first.X, Pair.first.Y }] = Pair.second;
			}
			// A waypoint about halfway out.
			const FNode Start = FMap::NodeOf(Unit.Pos);
			const std::pair<FNode, double>* Mid = nullptr;
			for (const auto& Pair : Plain)
			{
				if (!(Pair.first == Start) && (!Mid || std::abs(Pair.second - Budget * 0.5) < std::abs(Mid->second - Budget * 0.5)))
				{
					Mid = &Pair;
				}
			}
			if (!Mid)
			{
				continue;
			}
			const FVec2 Way = FMap::NodePos(Mid->first);
			const std::vector<FVec2> Via = { Way };
			double Left = 0.0;
			++Checks;
			if (!Battle.WalkVia(Unit, Via, false, Left) || std::abs((Budget - Left) - Mid->second) > 0.001)
			{
				Fail("unit " + std::to_string(Unit.Id) + ": the walk to its waypoint costs other than the shortest way there");
				continue;
			}
			const auto ByWay = Battle.ReachableVia(Unit, Via);
			const std::pair<FNode, double>* Far = nullptr;
			for (const auto& Pair : ByWay)
			{
				++Checks;
				if (Pair.second > Budget + 0.001)
				{
					Fail("unit " + std::to_string(Unit.Id) + ": a walk by a waypoint goes further than its move");
					break;
				}
				const auto Found = PlainCost.find({ Pair.first.X, Pair.first.Y });
				if (Found != PlainCost.end() && Pair.second < Found->second - 0.001)
				{
					Fail("unit " + std::to_string(Unit.Id) + ": a walk by a waypoint is shorter than the shortest way");
					break;
				}
				if (!Far || Pair.second > Far->second)
				{
					Far = &Pair;
				}
			}
			if (!Far)
			{
				continue;
			}
			const FVec2 To = FMap::NodePos(Far->first);
			++Checks;
			const std::string Refused = Battle.ValidateMove(Unit.Id, To, false, Via);
			if (!Refused.empty())
			{
				Fail("unit " + std::to_string(Unit.Id) + ": a walk by a waypoint it can make is refused: " + Refused);
				continue;
			}
			// Somewhere it can reach the short way but not by the waypoint.
			for (const auto& Pair : Plain)
			{
				bool bByWay = false;
				for (const auto& Other : ByWay)
				{
					bByWay = bByWay || Other.first == Pair.first;
				}
				if (!bByWay)
				{
					++Checks;
					if (Battle.ValidateMove(Unit.Id, FMap::NodePos(Pair.first), false, Via).empty())
					{
						Fail("unit " + std::to_string(Unit.Id) + ": a walk by a waypoint further than its move is allowed");
					}
					break;
				}
			}
			// Malformed waypoints.
			Checks += 3;
			if (Battle.ValidateMove(Unit.Id, To, false, { FVec2(Way.X + 0.1f, Way.Y) }).empty())
			{
				Fail("a waypoint off the node centres is allowed");
			}
			if (Battle.ValidateMove(Unit.Id, To, false, { Unit.Pos }).empty())
			{
				Fail("a waypoint where the unit stands is allowed");
			}
			if (Battle.ValidateMove(Unit.Id, To, false, { Way, To, Way, To, Way }).empty())
			{
				Fail("five waypoints are allowed");
			}
			// The way goes by it, from where it stands to where it ends.
			const std::vector<FVec2> Path = Battle.PathVia(Unit, Via, Far->first);
			bool bBy = false;
			for (const FVec2& Point : Path)
			{
				bBy = bBy || FMap::NodeOf(Point) == Mid->first;
			}
			++Checks;
			if (Path.empty() || !(FMap::NodeOf(Path.front()) == Start) || !(FMap::NodeOf(Path.back()) == Far->first) || !bBy)
			{
				Fail("unit " + std::to_string(Unit.Id) + ": the way by a waypoint does not go by it");
			}
			// Go To: the way to the farthest walkable spot on the map, however far.
			{
				FNode Far{ -1, -1 };
				double FarCost = -1.0;
				FBattle Probe = Battle;
				FUnit& Walker = *Probe.FindUnit(Unit.Id);
				// The farthest walkable node from it, by straight distance.
				for (int Ny = 0; Ny < Probe.Map.NavY; ++Ny)
				{
					for (int Nx = 0; Nx < Probe.Map.NavX; ++Nx)
					{
						const FNode N{ Nx, Ny };
						if (Probe.Map.NodeLevel(N) <= 0)
						{
							continue;
						}
						const double D = FMap::NodePos(N).DistanceTo(Walker.Pos);
						if (D > FarCost)
						{
							FarCost = D;
							Far = N;
						}
					}
				}
				double RouteCost = 0.0;
				const std::vector<FVec2> Route = Probe.RouteTo(Walker, {}, Far, &RouteCost);
				++Checks;
				if (!Route.empty())
				{
					bool bJoined = FMap::NodeOf(Route.front()) == Start && FMap::NodeOf(Route.back()) == Far;
					for (size_t i = 1; i < Route.size() && bJoined; ++i)
					{
						const FNode A = FMap::NodeOf(Route[i - 1]);
						const FNode B = FMap::NodeOf(Route[i]);
						bJoined = std::abs(A.X - B.X) <= 1 && std::abs(A.Y - B.Y) <= 1 && !(A == B);
					}
					if (!bJoined || RouteCost < FarCost - 0.001)
					{
						Fail("unit " + std::to_string(Unit.Id) + ": a long route is broken, or shorter than a straight line");
					}
					// Within reach, a route is as long as the walk there.
					const auto Near = Probe.ReachableNodes(Walker);
					if (!Near.empty())
					{
						double NearCost = 0.0;
						Probe.RouteTo(Walker, {}, Near.back().first, &NearCost);
						++Checks;
						if (std::abs(NearCost - Near.back().second) > 0.001)
						{
							Fail("unit " + std::to_string(Unit.Id) + ": a route within reach costs other than the walk there");
						}
					}
				}
			}
			// And walking it.
			const FVec2 From = Unit.Pos;
			FTickReport Report;
			++Checks;
			if (!Battle.ApplyMove(Unit.Id, To, false, Report, Via) || !(Unit.Pos == To) || !Unit.bMoved
				|| Unit.WalkVia.size() != 1 || !(Unit.WalkVia[0] == Way) || !(Unit.WalkFrom == From))
			{
				Fail("unit " + std::to_string(Unit.Id) + ": walking by a waypoint did not end where it was told");
			}
		}
		return Checks;
	}

	/**
	 * Facing on arrival (2026-10-03): a walk told which way to face ends facing
	 * exactly that way, every one of the eight; one not told faces its last
	 * step, as before; Off-Balance keeps it from turning either way; and a way
	 * that isn't one of the eight is refused.
	 */
	int CheckFacing(const FBattle& Original)
	{
		int Checks = 0;
		for (const FUnit& Each : Original.Units)
		{
			if (!Each.IsAlive())
			{
				continue;
			}
			FBattle Probe = Original;
			FUnit& Walker = *Probe.FindUnit(Each.Id);
			Walker.bReady = true;
			Walker.bMoved = false;
			Walker.bActed = false;
			const FNode Start = FMap::NodeOf(Walker.Pos);
			FNode Far = Start;
			double FarCost = -1.0;
			for (const auto& Pair : Probe.ReachableNodes(Walker))
			{
				if (!(Pair.first == Start) && Pair.second > FarCost)
				{
					Far = Pair.first;
					FarCost = Pair.second;
				}
			}
			if (FarCost < 0.0)
			{
				continue;
			}
			const FVec2 To = FMap::NodePos(Far);
			for (int Way = -1; Way < FacingWays; ++Way)
			{
				FBattle Battle = Probe;
				FUnit& Unit = *Battle.FindUnit(Each.Id);
				FOrder Order = FOrder::MakeMove(Unit.Id, -1, To, false, {}, Way);
				const std::string Refused = Battle.Validate(Order);
				FTickReport Report;
				++Checks;
				if (!Refused.empty() || !Battle.Apply(Order, Report))
				{
					Fail("unit " + std::to_string(Unit.Id) + ": a walk with a facing is refused: " + Refused);
					continue;
				}
				const bool bRight = Way >= 0
					? (Unit.Facing.X == FacingWay(Way).X && Unit.Facing.Y == FacingWay(Way).Y)
					: std::abs(Unit.Facing.Length() - 1.0f) < 0.001f;
				if (!(Unit.Pos == To) || !bRight)
				{
					Fail("unit " + std::to_string(Unit.Id) + ": a walk told to face way " + std::to_string(Way) + " faces otherwise");
				}
			}
			// Off-Balance: told to face a way, it still faces as it did.
			{
				FBattle Battle = Probe;
				FUnit& Unit = *Battle.FindUnit(Each.Id);
				FStatus Off;
				Off.Id = "offbalance";
				Off.Turns = 2;
				Unit.Statuses.push_back(Off);
				const FVec2 Before = Unit.Facing;
				FTickReport Report;
				++Checks;
				Battle.ApplyMove(Unit.Id, To, false, Report, {}, 2);
				if (!(Unit.Facing.X == Before.X && Unit.Facing.Y == Before.Y))
				{
					Fail("unit " + std::to_string(Unit.Id) + ": Off-Balance turned to face the way it was told");
				}
			}
			// Not one of the eight.
			for (const int Bad : { -2, FacingWays, 99 })
			{
				FBattle Battle = Probe;
				++Checks;
				if (Battle.Validate(FOrder::MakeMove(Each.Id, -1, To, false, {}, Bad)).empty())
				{
					Fail("unit " + std::to_string(Each.Id) + ": a facing of " + std::to_string(Bad) + " is allowed");
				}
			}
			break;  // one unit shows it; the rest walk the same way
		}
		return Checks;
	}

	// Zones of control as the HUD shows them (2026-10-02, "Zone of Control Mockups"
	// B and D): the ground an enemy tank takes away is what the walk can reach
	// without zones and cannot with them; and a tank stood in the way can cut an
	// enemy's way to the back line, and is back where it was afterwards.
	int CheckZones()
	{
		int Checks = 0;
		const FMapDef Def = FindMap("highlands");
		FBattle Battle;
		Battle.Map.BuildMirrored(Def.Top);
		const FVec2 Size = Battle.Map.SizeMeters();
		Battle.SpawnPoints[0] = Def.Spawns[0];
		Battle.SpawnPoints[1] = FVec2(Size.X - Def.Spawns[0].X, Size.Y - Def.Spawns[0].Y);
		const FVec2 Middle(Size.X * 0.5f, Size.Y * 0.5f);
		const char* Jobs[] = { "archer", "knight", "white_mage", "knight" };
		const int Teams[] = { 0, 1, 0, 0 };
		const FVec2 Spots[] = { Middle + FVec2(-4.0f, 0.0f), Middle, Middle + FVec2(-6.0f, 3.0f), Middle + FVec2(-3.0f, -4.0f) };
		for (int i = 0; i < 4; ++i)
		{
			FUnit Unit;
			Unit.Id = i;
			Unit.Team = Teams[i];
			Unit.Job = Jobs[i];
			Unit.Pos = FMap::Snap(Spots[i]);
			Battle.Units.push_back(Unit);
		}
		Battle.Start(3);
		for (FUnit& Unit : Battle.Units)
		{
			Unit.Pos = FMap::Snap(Spots[Unit.Id]);
		}
		Battle.Tuning.ZoneOfControl = 1.0;
		const FUnit& Archer = Battle.Units[0];

		++Checks;
		if (!FBattle::HoldsTheLine(Battle.Units[1]) || FBattle::HoldsTheLine(Archer))
		{
			Fail("zones: a knight holds the line and an archer does not");
		}
		const std::vector<FNode> Shadow = Battle.ZoneShadow(Archer, {});
		const std::vector<std::pair<FNode, double>> With = Battle.ReachableNodes(Archer);
		Battle.Tuning.ZoneOfControl = 0.0;
		const std::vector<std::pair<FNode, double>> Without = Battle.ReachableNodes(Archer);
		Battle.Tuning.ZoneOfControl = 1.0;
		++Checks;
		if (Shadow.empty() || With.size() + Shadow.size() != Without.size())
		{
			Fail("zones: the ground a tank takes away should be what the walk loses to it (" + std::to_string(Shadow.size()) + ")");
		}
		for (const FNode& Node : Shadow)
		{
			for (const std::pair<FNode, double>& Entry : With)
			{
				if (Entry.first == Node)
				{
					Fail("zones: ground taken away is still in the walk");
				}
			}
		}
		++Checks;
		if (!Shadow.empty())
		{
			const std::vector<FVec2> Ghost = Battle.PathIgnoringZones(Archer, {}, Shadow.back());
			if (Ghost.empty() || !Battle.PathVia(Archer, {}, Shadow.back()).empty())
			{
				Fail("zones: past a zone there is a way only when zones are ignored");
			}
		}
		Battle.Tuning.ZoneOfControl = 0.0;
		++Checks;
		if (!Battle.ZoneShadow(Archer, {}).empty())
		{
			Fail("zones: with the rule off nothing is taken away");
		}
		Battle.Tuning.ZoneOfControl = 1.0;

		// Somewhere the blue knight can stand cuts the red knight's way to the
		// archer or the white mage, and the knight is put back each time.
		const FVec2 Home = Battle.Units[3].Pos;
		bool bCut = false;
		bool bReached = false;
		for (const std::pair<FNode, double>& Entry : Battle.ReachableNodes(Battle.Units[3]))
		{
			const std::vector<FLane> Lanes = Battle.TankLanes(3, FMap::NodePos(Entry.first), { 1 }, { 0, 2 });
			for (const FLane& Lane : Lanes)
			{
				bReached = bReached || !Lane.Before.empty();
				bCut = bCut || Lane.After.size() < Lane.Before.size();
				if (Lane.TowardId < 0 || (!Lane.After.empty() && Lane.Path.empty()))
				{
					Fail("zones: a lane without its target or its walk");
				}
			}
			if (!(Battle.Units[3].Pos == Home))
			{
				Fail("zones: the tank was not put back where it stood");
				break;
			}
		}
		++Checks;
		if (!bReached || !bCut)
		{
			Fail("zones: the red knight should reach the back line, and a spot for the blue knight should cut it");
		}
		return Checks;
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
	const std::vector<std::string> Args = TMBaseline::Paths(argc, argv);
	const std::string PathText = Args.empty() ? std::string("Baselines/MoveTable.txt") : Args[0];
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
		Writer.Read(Line);

		if (Line.rfind("MAP ", 0) == 0)
		{
			const int TilesX = static_cast<int>(ValueOf(Line, "tiles", 0));
			if (Battle.Map.TilesX != 12 || Battle.Map.NavX != 48)
			{
				Fail("the map is " + std::to_string(Battle.Map.TilesX) + " tiles wide, the baseline's is 12");
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
			if (Writer.bOn)
			{
				if (Want != Battle.Map.Heights)
				{
					std::string Row = "HEIGHTS ";
					for (const int Height : Battle.Map.Heights)
					{
						Row += std::to_string(Height) + ",";
					}
					Row.pop_back();
					Writer.Set(Row);
				}
			}
			else if (Want != Battle.Map.Heights)
			{
				Fail("the ground does not match the baseline's (" + std::to_string(Want.size())
					+ " tiles expected, " + std::to_string(Battle.Map.Heights.size()) + " built)");
				for (size_t i = 0; i < Want.size() && i < Battle.Map.Heights.size(); ++i)
				{
					if (Want[i] != Battle.Map.Heights[i])
					{
						Fail("  first difference at tile " + std::to_string(i) + ": the rules "
							+ std::to_string(Battle.Map.Heights[i]) + ", the baseline " + std::to_string(Want[i]));
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
				if (Writer.bOn && Job2->Stats.Get(EStat::Move) != static_cast<int>(ValueOf(Line, "move", -1)))
				{
					const size_t MoveAt = Line.find("move=");
					const size_t MoveEnd = Line.find(' ', MoveAt);
					Writer.Set(Line.substr(0, MoveAt) + "move=" + std::to_string(Job2->Stats.Get(EStat::Move))
						+ (MoveEnd == std::string::npos ? std::string() : Line.substr(MoveEnd)));
				}
				else if (Job2->Stats.Get(EStat::Move) != static_cast<int>(ValueOf(Line, "move", -1)))
				{
					Fail(Seed.Job + " walks " + std::to_string(Job2->Stats.Get(EStat::Move))
						+ " here and " + std::to_string(static_cast<int>(ValueOf(Line, "move", -1))) + " in the baseline");
				}
			}
			else
			{
				Fail("no class registered called " + Seed.Job);
			}

			// Put the units on the board as the baseline has them, so what follows is
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
			if (Writer.bOn)
			{
				// Written in the table's order, row by row, if anything differs.
				auto Cells = Got;
				std::sort(Cells.begin(), Cells.end(), [](const auto& A, const auto& B)
					{ return A.first.Y != B.first.Y ? A.first.Y < B.first.Y : A.first.X < B.first.X; });
				bool bDiffers = static_cast<int>(Cells.size()) != Count;
				std::string Row = "  " + std::to_string(Id) + " " + Mode + " " + std::to_string(Cells.size());
				for (const auto& Pair : Cells)
				{
					char Written[48];
					std::snprintf(Written, sizeof(Written), " %d:%d:%.4f", Pair.first.X, Pair.first.Y, Pair.second);
					Row += Written;
					const auto Found = Want.find(static_cast<long long>(Pair.first.Y) * 1000 + Pair.first.X);
					bDiffers = bDiffers || Found == Want.end() || std::abs(Found->second - Pair.second) > 0.001;
				}
				if (bDiffers)
				{
					Writer.Set(Row);
				}
				continue;
			}
			if (static_cast<int>(Got.size()) != Count)
			{
				Fail("unit " + std::to_string(Id) + " " + Mode + ": reaches "
					+ std::to_string(Got.size()) + " spots, the baseline reaches " + std::to_string(Count));
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
						+ " and the baseline does not");
					break;
				}
				if (std::abs(Found->second - Pair.second) > 0.001)
				{
					char Buffer[192];
					std::snprintf(Buffer, sizeof(Buffer),
						"unit %d %s: %d,%d costs %.4f here and %.4f in the baseline",
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
			if (Writer.bOn)
			{
				bool bDiffers = Got.size() != Want.size();
				std::string Row = "  " + std::to_string(Id) + " " + Target;
				for (size_t i = 0; i < Got.size(); ++i)
				{
					char Written[48];
					std::snprintf(Written, sizeof(Written), " %.2f,%.2f", Got[i].X, Got[i].Y);
					Row += Written;
					bDiffers = bDiffers || std::abs(Got[i].X - Want[i].X) > 0.01f || std::abs(Got[i].Y - Want[i].Y) > 0.01f;
				}
				if (bDiffers)
				{
					Writer.Set(Row);
				}
				continue;
			}
			if (Got.size() != Want.size())
			{
				Fail("unit " + std::to_string(Id) + " to " + Target + ": path is "
					+ std::to_string(Got.size()) + " steps, the baseline's is " + std::to_string(Want.size()));
				continue;
			}
			for (size_t i = 0; i < Got.size(); ++i)
			{
				if (std::abs(Got[i].X - Want[i].X) > 0.01f || std::abs(Got[i].Y - Want[i].Y) > 0.01f)
				{
					char Buffer[192];
					std::snprintf(Buffer, sizeof(Buffer),
						"unit %d to %s: step %d is %.2f,%.2f here and %.2f,%.2f in the baseline",
						Id, Target.c_str(), static_cast<int>(i), Got[i].X, Got[i].Y, Want[i].X, Want[i].Y);
					Fail(Buffer);
					break;
				}
			}
			continue;
		}
	}

	std::printf("%d checks over %d units\n", Checked, static_cast<int>(Battle.Units.size()));
	if (Writer.bOn && !Writer.Finish())
	{
		return 1;
	}
	const int Before = Failures;
	const int WayChecks = CheckWaypoints(Battle);
	std::printf("waypoints: %d checks, %s\n", WayChecks, Failures == Before && WayChecks > 0 ? "as the rules say" : "WRONG");
	if (WayChecks == 0)
	{
		Fail("no waypoint was checked");
	}
	const int BeforeFacing = Failures;
	const int FacingChecks = CheckFacing(Battle);
	std::printf("facing on arrival: %d checks, %s\n", FacingChecks, Failures == BeforeFacing && FacingChecks > 0 ? "as the rules say" : "WRONG");
	if (FacingChecks == 0)
	{
		Fail("no facing was checked");
	}
	const int BeforeZones = Failures;
	const int ZoneChecks = CheckZones();
	std::printf("zones of control: %d checks, %s\n", ZoneChecks, Failures == BeforeZones ? "as the rules say" : "WRONG");
	if (Checked == 0)
	{
		std::printf("\nNOTHING WAS CHECKED\n");
		return 1;
	}
	std::printf("\n%s\n", Failures == 0
		? "WALKING GOES WHERE ITS BASELINE SAYS"
		: "DIVERGED FROM THE BASELINE");
	return Failures == 0 ? 0 : 1;
}
