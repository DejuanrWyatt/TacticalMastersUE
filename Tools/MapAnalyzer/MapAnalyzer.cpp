// The map analyser: what a battle map will play like, worked out from the rules
// themselves before anybody plays it.
//
// A map is only rows of characters, and whether it is any good is decided by the
// rules that read them: how far a unit walks, which heights it can climb, what it
// can see over. So this does not have its own idea of any of that. It builds the
// map with FMap::BuildMirrored exactly as a battle does, and asks FBattle the
// questions -- DistanceFrom for walking, HasLineOfSight for seeing, MoveOf and
// BaseTgGain for how quickly a class gets anywhere -- so its answers change when
// the rules do, and never disagree with the game.
//
//   TMMapAnalyzer <map file>... [--battles N] [--skill easy|medium|hard]
//                               [--capture SECONDS] [--limit SECONDS]
//
// What it reports, per map:
//   - the ground: the whole map as the rules build it, and what it is made of
//   - getting about: ground only fliers can reach, the walk between the two
//     starts and to the middle, and how many turns each built-in class needs
//   - routes: every place the middle line can be crossed on foot, how much
//     longer each is than the best, and what lies along it
//   - sight: how exposed each tile is, whether the starting areas can see each
//     other, what overlooks the middle, where to hide
//   - height: cliffs, and each raised plateau with its ways up
//   - hazards: every ember and spring, who reaches it first and who can see it
//   - with --battles: the computer against itself on this map, the same four
//     classes a side, and where units stood and fell
//   - warnings: the things above that usually make a map play badly
//
// It changes nothing in the rules and nothing about a battle. It only asks.
//
// ---------------------------------------------------------------- map files
//
// Plain text. Rows use the map characters (SimMap.cpp): a digit is a height
// level, '#' rock (blocks sight and movement), '~' water, 'x' embers, '+' a
// spring. By default the rows are the top half and the rest is that half turned
// around, as BuildMirrored makes every map. Other lines:
//
//   // a comment            ('#' is rock, so comments use //)
//   name Crown
//   spawn 2.75 4.75         a blue unit's starting spot in metres, up to four;
//                           red starts at the same spots turned around. The
//                           first is the side's spawn point, which placement and
//                           the computer player go by. Leave them out for the
//                           four the battle director and the class lab use.
//   mirror off              the rows are the whole map, not half of it
//
// ------------------------------------------------------ what it can't tell you
//
// Whether a map is fun. Everything here is a measurement, and a warning is a
// prompt to look, not a verdict: a crossing nobody takes can be a deliberate
// trap. The battles are the computer's opinion of the map, which is a player
// that thinks about where to stand but has no plan; people will find routes it
// never does. Turn counts assume a unit only walks, and the times are the
// clock's arithmetic, not a battle's.

#include "SimAbility.h"
#include "SimAI.h"
#include "SimBattle.h"
#include "SimMap.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

using namespace TMSim;

namespace
{
	constexpr double Infinity = std::numeric_limits<double>::infinity();

	/** Where the battle director and the class lab put blue's four units. */
	const FVec2 DefaultSpawns[4] =
	{
		FVec2(2.75f, 4.75f), FVec2(0.75f, 8.75f), FVec2(4.75f, 6.75f), FVec2(2.75f, 10.75f)
	};

	/** The six built-in classes, for the tempo table. */
	const char* const Classes[6] = { "squire", "knight", "archer", "monk", "black_mage", "white_mage" };

	/** The roster the class lab measures against, used by both sides here. */
	const char* const Roster[4] = { "black_mage", "knight", "archer", "white_mage" };

	constexpr int DirX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
	constexpr int DirY[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };

	// ------------------------------------------------------------- reading

	struct FMapFile
	{
		std::string Path;
		std::string Name;
		std::vector<std::string> Rows;
		std::vector<FVec2> Spawns;
		bool bMirror = true;
		std::vector<std::string> Problems;
	};

	bool IsMapChar(char Ch)
	{
		return (Ch >= '0' && Ch <= '9') || Ch == '~' || Ch == '#' || Ch == 'x' || Ch == '+';
	}

	std::string Trim(const std::string& Text)
	{
		size_t Start = 0;
		size_t End = Text.size();
		while (Start < End && (Text[Start] == ' ' || Text[Start] == '\t' || Text[Start] == '\r'))
		{
			++Start;
		}
		while (End > Start && (Text[End - 1] == ' ' || Text[End - 1] == '\t' || Text[End - 1] == '\r'))
		{
			--End;
		}
		return Text.substr(Start, End - Start);
	}

	FMapFile ReadMapFile(const std::string& Path)
	{
		FMapFile File;
		File.Path = Path;
		File.Name = Path;
		std::ifstream In(Path);
		if (!In)
		{
			File.Problems.push_back("could not open " + Path);
			return File;
		}
		std::string Line;
		int LineNumber = 0;
		while (std::getline(In, Line))
		{
			++LineNumber;
			const std::string Text = Trim(Line);
			if (Text.empty() || Text.rfind("//", 0) == 0)
			{
				continue;
			}
			if (Text.rfind("name ", 0) == 0)
			{
				File.Name = Trim(Text.substr(5));
				continue;
			}
			if (Text.rfind("mirror ", 0) == 0)
			{
				File.bMirror = Trim(Text.substr(7)) != "off";
				continue;
			}
			if (Text.rfind("spawn ", 0) == 0)
			{
				float X = 0.0f;
				float Y = 0.0f;
				if (std::sscanf(Text.c_str() + 6, "%f %f", &X, &Y) == 2)
				{
					File.Spawns.push_back(FVec2(X, Y));
				}
				else
				{
					File.Problems.push_back("line " + std::to_string(LineNumber) + ": spawn needs two numbers in metres");
				}
				continue;
			}
			for (const char Ch : Text)
			{
				if (!IsMapChar(Ch))
				{
					File.Problems.push_back("line " + std::to_string(LineNumber) + ": '" + std::string(1, Ch)
						+ "' is not a map character (digits, # ~ x +)");
					break;
				}
			}
			File.Rows.push_back(Text);
		}
		if (File.Rows.empty())
		{
			File.Problems.push_back("no map rows");
		}
		for (size_t i = 1; i < File.Rows.size(); ++i)
		{
			if (File.Rows[i].size() != File.Rows[0].size())
			{
				File.Problems.push_back("row " + std::to_string(i + 1) + " is " + std::to_string(File.Rows[i].size())
					+ " wide; the first is " + std::to_string(File.Rows[0].size()));
			}
		}
		if (File.Spawns.size() > 4)
		{
			File.Problems.push_back("at most four spawn lines, one per unit");
		}
		if (File.Spawns.empty())
		{
			File.Spawns.assign(DefaultSpawns, DefaultSpawns + 4);
		}
		return File;
	}

	// ------------------------------------------------------------ the map

	/** One map, built as a battle builds it, and the answers worked out about it. */
	struct FStudy
	{
		FMapFile File;
		FBattle Battle;
		int TilesX = 0;
		int TilesY = 0;
		std::vector<FVec2> Starts[2];

		/** Metres walked from the nearest of a side's starts, per navigation node. */
		std::vector<double> Walk[2];

		/** Per tile: how many tiles can see it, and how many it can see. */
		std::vector<int> Exposure;
		std::vector<int> Sees;
		/** Per tile: height levels it stands over the tiles it sees, each counted up to the bonus cap. */
		std::vector<int> Command;
		double SightRange = 0.0;

		std::vector<std::string> Warnings;

		int Tile(int X, int Y) const { return Y * TilesX + X; }
		/** The tile a point is on, or -1 off the map. */
		int TileAt(const FVec2& Point) const
		{
			const int X = static_cast<int>(std::floor(Point.X / Ground::TileSize));
			const int Y = static_cast<int>(std::floor(Point.Y / Ground::TileSize));
			return X >= 0 && Y >= 0 && X < TilesX && Y < TilesY ? Tile(X, Y) : -1;
		}
		const FMap& Map() const { return Battle.Map; }
		int Level(int X, int Y) const { return Battle.Map.TileLevel(X, Y); }
		bool Walkable(int X, int Y) const { return Level(X, Y) > 0; }
		bool IsRock(int X, int Y) const { return Battle.Map.Covers[Tile(X, Y)] == 1; }
		int Hazard(int X, int Y) const { return Battle.Map.Hazards[Tile(X, Y)]; }

		static FVec2 Centre(int X, int Y)
		{
			return FVec2((static_cast<float>(X) + 0.5f) * Ground::TileSize, (static_cast<float>(Y) + 0.5f) * Ground::TileSize);
		}

		/** The fewest metres a side walks to any spot on this tile. */
		double TileWalk(int Side, int X, int Y) const
		{
			double Best = Infinity;
			for (int Ny = 0; Ny < Ground::NodesPerTile; ++Ny)
			{
				for (int Nx = 0; Nx < Ground::NodesPerTile; ++Nx)
				{
					const FNode Node{ X * Ground::NodesPerTile + Nx, Y * Ground::NodesPerTile + Ny };
					Best = std::min(Best, Walk[Side][Map().NodeIndex(Node)]);
				}
			}
			return Best;
		}

		double WalkAt(int Side, const FVec2& Point) const
		{
			return Walk[Side][Map().NodeIndex(FMap::NodeOf(Point))];
		}

		char GroundChar(int X, int Y) const
		{
			if (IsRock(X, Y))
			{
				return '#';
			}
			if (!Walkable(X, Y))
			{
				return '~';
			}
			if (Hazard(X, Y) < 0)
			{
				return 'x';
			}
			if (Hazard(X, Y) > 0)
			{
				return '+';
			}
			return static_cast<char>('0' + std::min(Level(X, Y), 9));
		}
	};

	std::string Metres(double Value)
	{
		if (std::isinf(Value))
		{
			return "unreachable";
		}
		char Buffer[32];
		std::snprintf(Buffer, sizeof(Buffer), "%.1f m", Value);
		return Buffer;
	}

	std::string TileName(int X, int Y)
	{
		return "(" + std::to_string(X) + "," + std::to_string(Y) + ")";
	}

	/** A digit 0-9 for a value against the largest, '.' for none at all. */
	char Scale(double Value, double Largest)
	{
		if (Value <= 0.0 || Largest <= 0.0)
		{
			return '.';
		}
		const int Digit = static_cast<int>(std::lround(9.0 * Value / Largest));
		return static_cast<char>('0' + std::max(1, std::min(9, Digit)));
	}

	/** Prints a grid of tiles, one character each, with rock and water drawn as themselves. */
	template <typename FCell>
	void PrintGrid(const FStudy& Study, const char* Indent, FCell Cell)
	{
		std::printf("%s    ", Indent);
		for (int X = 0; X < Study.TilesX; ++X)
		{
			std::printf("%d", X % 10);
		}
		std::printf("\n");
		for (int Y = 0; Y < Study.TilesY; ++Y)
		{
			if (Y == Study.TilesY / 2 && Study.File.bMirror)
			{
				std::printf("%s    %s  <- middle line\n", Indent, std::string(Study.TilesX, '-').c_str());
			}
			std::printf("%s%3d ", Indent, Y);
			for (int X = 0; X < Study.TilesX; ++X)
			{
				if (Study.IsRock(X, Y))
				{
					std::printf("#");
				}
				else if (!Study.Walkable(X, Y))
				{
					std::printf("~");
				}
				else
				{
					std::printf("%c", Cell(X, Y));
				}
			}
			std::printf("\n");
		}
	}

	bool Build(FStudy& Study)
	{
		const FMapFile& File = Study.File;
		if (File.bMirror)
		{
			Study.Battle.Map.BuildMirrored(File.Rows);
		}
		else
		{
			Study.Battle.Map.Build(File.Rows);
		}
		Study.TilesX = Study.Battle.Map.TilesX;
		Study.TilesY = Study.Battle.Map.TilesY;
		const FVec2 Size = Study.Battle.Map.SizeMeters();

		for (const FVec2& Spot : File.Spawns)
		{
			Study.Starts[0].push_back(Spot);
			Study.Starts[1].push_back(FVec2(Size.X - Spot.X, Size.Y - Spot.Y));
		}
		Study.Battle.SpawnPoints[0] = Study.Starts[0][0];
		Study.Battle.SpawnPoints[1] = Study.Starts[1][0];

		bool bOk = true;
		for (int Side = 0; Side < 2; ++Side)
		{
			for (const FVec2& Spot : Study.Starts[Side])
			{
				if (!Study.Battle.InBounds(Spot) || !Study.Battle.Map.NodeWalkable(FMap::NodeOf(Spot)))
				{
					std::printf("  ERROR: %s start at %.2f, %.2f is not on walkable ground.\n",
						Side == 0 ? "blue" : "red", Spot.X, Spot.Y);
					bOk = false;
				}
			}
		}
		if (!bOk)
		{
			return false;
		}

		// Walking distance from each side's starts, as the pathfinder measures
		// it: units ignored, the ordinary climb. Copied, because DistanceFrom
		// hands back a reference into a cache that a later call can empty.
		for (int Side = 0; Side < 2; ++Side)
		{
			Study.Walk[Side].assign(Study.Battle.Map.NavX * Study.Battle.Map.NavY, Infinity);
			for (const FVec2& Spot : Study.Starts[Side])
			{
				const std::vector<double> Field = Study.Battle.DistanceFrom(Spot);
				for (size_t i = 0; i < Field.size(); ++i)
				{
					Study.Walk[Side][i] = std::min(Study.Walk[Side][i], Field[i]);
				}
			}
		}
		return true;
	}

	// --------------------------------------------------------------- ground

	void ReportGround(FStudy& Study)
	{
		std::printf("\nTHE GROUND  %d x %d tiles, %.0f x %.0f m%s\n", Study.TilesX, Study.TilesY,
			static_cast<double>(Study.TilesX) * Ground::TileSize, static_cast<double>(Study.TilesY) * Ground::TileSize,
			Study.File.bMirror ? ", the far half the near half turned around" : "");
		PrintGrid(Study, "  ", [&](int X, int Y) { return Study.GroundChar(X, Y); });

		int Walkable = 0;
		int Rock = 0;
		int Water = 0;
		int Embers = 0;
		int Springs = 0;
		int ByLevel[10] = { 0 };
		for (int Y = 0; Y < Study.TilesY; ++Y)
		{
			for (int X = 0; X < Study.TilesX; ++X)
			{
				if (Study.IsRock(X, Y))
				{
					++Rock;
				}
				else if (!Study.Walkable(X, Y))
				{
					++Water;
				}
				else
				{
					++Walkable;
					++ByLevel[std::min(Study.Level(X, Y), 9)];
					Embers += Study.Hazard(X, Y) < 0 ? 1 : 0;
					Springs += Study.Hazard(X, Y) > 0 ? 1 : 0;
				}
			}
		}
		const double All = Study.TilesX * Study.TilesY;
		std::printf("  walkable %d%%  rock %d%%  water %d%%  |  embers %d tiles, springs %d tiles\n",
			static_cast<int>(std::lround(100.0 * Walkable / All)), static_cast<int>(std::lround(100.0 * Rock / All)),
			static_cast<int>(std::lround(100.0 * Water / All)), Embers, Springs);
		std::printf("  height:");
		for (int Level = 1; Level < 10; ++Level)
		{
			if (ByLevel[Level] > 0)
			{
				std::printf("  level %d: %d%%", Level, static_cast<int>(std::lround(100.0 * ByLevel[Level] / Walkable)));
			}
		}
		std::printf("\n  starts: blue");
		for (const FVec2& Spot : Study.Starts[0])
		{
			std::printf(" %s", TileName(static_cast<int>(Spot.X / Ground::TileSize), static_cast<int>(Spot.Y / Ground::TileSize)).c_str());
		}
		std::printf(", red");
		for (const FVec2& Spot : Study.Starts[1])
		{
			std::printf(" %s", TileName(static_cast<int>(Spot.X / Ground::TileSize), static_cast<int>(Spot.Y / Ground::TileSize)).c_str());
		}
		std::printf("   (tiles are (column,row) from 0)\n");
		for (const FVec2& Spot : Study.Starts[0])
		{
			if (Study.Battle.HazardAt(Spot) < 0)
			{
				Study.Warnings.push_back("A unit starts on embers at " + TileName(static_cast<int>(Spot.X / Ground::TileSize),
					static_cast<int>(Spot.Y / Ground::TileSize)) + ": it burns before it has done anything.");
			}
		}

		if (Rock * 100 > Walkable * 35)
		{
			Study.Warnings.push_back("A lot of rock: close quarters everywhere, so ranged classes will struggle.");
		}
		if (Rock == 0)
		{
			Study.Warnings.push_back("No rock at all: nothing breaks line of sight but hills, which favours ranged classes.");
		}
	}

	// ---------------------------------------------------------- getting about

	void ReportTempo(FStudy& Study)
	{
		std::printf("\nGETTING ABOUT\n");

		// Ground only a flier gets to: walkable, but no walk reaches it from either start.
		std::vector<std::string> FlierOnly;
		for (int Y = 0; Y < Study.TilesY; ++Y)
		{
			for (int X = 0; X < Study.TilesX; ++X)
			{
				if (Study.Walkable(X, Y) && std::isinf(Study.TileWalk(0, X, Y)) && std::isinf(Study.TileWalk(1, X, Y)))
				{
					FlierOnly.push_back(TileName(X, Y));
				}
			}
		}
		if (FlierOnly.empty())
		{
			std::printf("  every walkable tile can be walked to\n");
		}
		else
		{
			std::printf("  %d tile(s) only fliers can reach:", static_cast<int>(FlierOnly.size()));
			for (size_t i = 0; i < FlierOnly.size() && i < 12; ++i)
			{
				std::printf(" %s", FlierOnly[i].c_str());
			}
			std::printf("%s\n", FlierOnly.size() > 12 ? " ..." : "");
		}

		double ToEnemy = Infinity;
		double Straight = Infinity;
		for (const FVec2& Theirs : Study.Starts[1])
		{
			ToEnemy = std::min(ToEnemy, Study.WalkAt(0, Theirs));
			for (const FVec2& Ours : Study.Starts[0])
			{
				Straight = std::min(Straight, static_cast<double>(Ours.DistanceTo(Theirs)));
			}
		}
		// The middle as hold-the-middle counts it: anywhere within CaptureRadius
		// of the centre, so the nearest walkable spot of it, not the centre itself
		// (which may be water or rock).
		const FVec2 Middle = Study.Battle.CapturePoint();
		double ToMiddle = Infinity;
		for (int i = 0; i < Study.Map().NavX * Study.Map().NavY; ++i)
		{
			const FVec2 Spot = FMap::NodePos(Study.Map().NodeAt(i));
			if (Study.Map().NavLevels[i] > 0 && static_cast<double>(Spot.DistanceTo(Middle)) <= FBattle::CaptureRadius)
			{
				ToMiddle = std::min(ToMiddle, Study.Walk[0][i]);
			}
		}
		if (std::isinf(ToEnemy))
		{
			Study.Warnings.push_back("The two sides cannot walk to each other at all.");
		}
		std::printf("  nearest start to nearest enemy start: %s walking, %s straight (%.2fx)\n",
			Metres(ToEnemy).c_str(), Metres(Straight).c_str(), std::isinf(ToEnemy) ? 0.0 : ToEnemy / Straight);
		std::printf("  nearest start to the middle of the map (%.0f m round the centre): %s walking\n",
			FBattle::CaptureRadius, Metres(ToMiddle).c_str());
		if (std::isinf(ToMiddle))
		{
			Study.Warnings.push_back("Nobody can walk into the middle: hold-the-middle can never be won on this map.");
		}

		// How many turns each class needs, walking and nothing else. A turn spent
		// only walking keeps TgKeepOne of the gauge, so the next one comes sooner
		// than a full turn would; the first comes from the head start Speed gives.
		std::printf("\n  %-11s %5s %6s   %-18s %-18s\n", "class", "move", "sight", "to the middle", "to the enemy start");
		int FastestToEnemy = 99;
		for (const char* Id : Classes)
		{
			const FJobDef* Job = FindJob(Id);
			if (!Job)
			{
				continue;
			}
			FUnit Unit;
			Unit.Job = Id;
			Unit.Stats = &Job->Stats;
			const double Move = Study.Battle.MoveOf(Unit);
			const double Gain = std::max(1, Study.Battle.BaseTgGain(Unit));
			const double First = std::max(0.0, Pace::TgMax - static_cast<double>(Unit.Stat(EStat::Speed) * Pace::StartTgPerSpeed)) / Gain;
			const double Between = (Pace::TgMax - Pace::TgKeepOne) / Gain;

			auto Describe = [&](double Distance, int& OutTurns) -> std::string
			{
				if (std::isinf(Distance) || Move <= 0.0)
				{
					OutTurns = 99;
					return "never";
				}
				if (Distance <= 0.0)
				{
					OutTurns = 0;
					return "starts there";
				}
				OutTurns = std::max(1, static_cast<int>(std::ceil(Distance / Move - 1e-9)));
				const double Seconds = (First + (OutTurns - 1) * Between) / Pace::TicksPerSecond;
				char Buffer[48];
				std::snprintf(Buffer, sizeof(Buffer), "%d turn%s, ~%.0f s", OutTurns, OutTurns == 1 ? "" : "s", Seconds);
				return Buffer;
			};
			int TurnsToMiddle = 0;
			int TurnsToEnemy = 0;
			const std::string A = Describe(ToMiddle, TurnsToMiddle);
			const std::string B = Describe(ToEnemy, TurnsToEnemy);
			FastestToEnemy = std::min(FastestToEnemy, TurnsToEnemy);
			std::printf("  %-11s %4.0fm %5.0fm   %-18s %-18s\n", Id, Move, Study.Battle.SightOf(Unit), A.c_str(), B.c_str());
		}
		std::printf("  (walking only; a turn spent only walking keeps part of the gauge, so later turns come sooner)\n");
		if (FastestToEnemy <= 2)
		{
			Study.Warnings.push_back("The fastest class can walk onto the enemy start by its second turn: little time to position.");
		}
	}

	// ---------------------------------------------------------------- routes
	//
	// Every route from one side to the other has to cross the front: the ground
	// both sides can reach in the same walk. On open ground the front is one long
	// line; rock, water and cliffs break it into pieces, and each piece is a way
	// through. So the routes are the pieces, and each is measured by the best way
	// through it, start to start.

	struct FCrossing
	{
		std::vector<int> Nodes;
		double Best = Infinity;
		int BestNode = -1;
		int Embers = 0;
		int Springs = 0;
		int HighestLevel = 0;
		int LowestLevel = 99;
		/** Tiles the best way through passes over, start to start. */
		std::vector<int> Tiles;
		/** Navigation nodes of that way, start to start. */
		std::vector<int> Path;
		/**
		 * Another piece the best way through this one has to cross first, or -1.
		 * Such a piece is a pocket of the front behind that one, not a way through
		 * of its own.
		 */
		int Behind = -1;
		/** Where its way through crosses the front a second time, or -1. */
		int Flip = -1;
	};

	bool CanStep(const FMap& Map, int From, int To)
	{
		return Map.NavLevels[To] > 0 && std::abs(Map.NavLevels[To] - Map.NavLevels[From]) <= Ground::Jump;
	}

	/** The way back to the nearest start, following the walk downhill. */
	std::vector<int> Descend(const FStudy& Study, int Side, int Start)
	{
		const FMap& Map = Study.Map();
		std::vector<int> Path;
		int Here = Start;
		for (int Steps = 0; Steps < Map.NavX * Map.NavY; ++Steps)
		{
			Path.push_back(Here);
			const double Cost = Study.Walk[Side][Here];
			if (Cost <= 0.0 || std::isinf(Cost))
			{
				break;
			}
			const int X = Here % Map.NavX;
			const int Y = Here / Map.NavX;
			int Next = -1;
			double NextCost = Cost;
			for (int k = 0; k < 8; ++k)
			{
				const int Mx = X + DirX[k];
				const int My = Y + DirY[k];
				if (Mx < 0 || My < 0 || Mx >= Map.NavX || My >= Map.NavY)
				{
					continue;
				}
				const int M = My * Map.NavX + Mx;
				if (CanStep(Map, Here, M) && Study.Walk[Side][M] < NextCost)
				{
					Next = M;
					NextCost = Study.Walk[Side][M];
				}
			}
			if (Next < 0)
			{
				break;
			}
			Here = Next;
		}
		return Path;
	}

	std::vector<FCrossing> ReportRoutes(FStudy& Study)
	{
		std::printf("\nROUTES  (every route crosses the front, where both sides arrive in the same walk;\n"
			"         rock, water and cliffs break the front into separate ways through)\n");
		const FMap& Map = Study.Map();
		const int Count = Map.NavX * Map.NavY;
		const std::vector<double>& Blue = Study.Walk[0];
		const std::vector<double>& Red = Study.Walk[1];

		// Front nodes: blue's side of the line, with a step to red's side of it.
		std::vector<uint8_t> Front(Count, 0);
		for (int i = 0; i < Count; ++i)
		{
			if (Map.NavLevels[i] <= 0 || std::isinf(Blue[i]) || std::isinf(Red[i]) || Blue[i] > Red[i])
			{
				continue;
			}
			const int X = i % Map.NavX;
			const int Y = i / Map.NavX;
			for (int k = 0; k < 8 && !Front[i]; ++k)
			{
				const int Mx = X + DirX[k];
				const int My = Y + DirY[k];
				if (Mx >= 0 && My >= 0 && Mx < Map.NavX && My < Map.NavY)
				{
					const int M = My * Map.NavX + Mx;
					if (CanStep(Map, i, M) && !std::isinf(Blue[M]) && Blue[M] > Red[M])
					{
						Front[i] = 1;
					}
				}
			}
		}

		// Pieces of the front: front nodes that touch, and could be walked between.
		std::vector<FCrossing> Crossings;
		std::vector<int> Piece(Count, -1);
		for (int i = 0; i < Count; ++i)
		{
			if (!Front[i] || Piece[i] >= 0)
			{
				continue;
			}
			FCrossing Crossing;
			std::vector<int> Stack{ i };
			Piece[i] = static_cast<int>(Crossings.size());
			while (!Stack.empty())
			{
				const int N = Stack.back();
				Stack.pop_back();
				Crossing.Nodes.push_back(N);
				const double Via = Blue[N] + Red[N];
				if (Via < Crossing.Best)
				{
					Crossing.Best = Via;
					Crossing.BestNode = N;
				}
				const int X = N % Map.NavX;
				const int Y = N / Map.NavX;
				for (int k = 0; k < 8; ++k)
				{
					const int Mx = X + DirX[k];
					const int My = Y + DirY[k];
					if (Mx >= 0 && My >= 0 && Mx < Map.NavX && My < Map.NavY)
					{
						const int M = My * Map.NavX + Mx;
						if (Front[M] && Piece[M] < 0 && CanStep(Map, N, M))
						{
							Piece[M] = Piece[i];
							Stack.push_back(M);
						}
					}
				}
			}
			Crossings.push_back(Crossing);
		}

		// A single node on its own is the grid's jaggedness, not a way through.
		if (Crossings.size() > 1)
		{
			Crossings.erase(std::remove_if(Crossings.begin(), Crossings.end(),
				[](const FCrossing& Crossing) { return Crossing.Nodes.size() < 2; }), Crossings.end());
		}
		if (Crossings.empty())
		{
			std::printf("  none: the two sides cannot reach each other\n");
			return Crossings;
		}
		// Longest first: the biggest openings are the ones worth naming A, B, C.
		std::stable_sort(Crossings.begin(), Crossings.end(), [](const FCrossing& A, const FCrossing& B)
		{
			return A.Nodes.size() > B.Nodes.size();
		});

		double Best = Infinity;
		for (const FCrossing& Crossing : Crossings)
		{
			Best = std::min(Best, Crossing.Best);
		}

		// What lies along the best way through each piece, start to start.
		for (FCrossing& Crossing : Crossings)
		{
			std::vector<int> Nodes = Descend(Study, 0, Crossing.BestNode);
			const std::vector<int> Theirs = Descend(Study, 1, Crossing.BestNode);
			// A true way through crosses the front once, here. If blue's half of
			// the way ever stands where red arrives first, or red's half comes back
			// to where blue arrives first, it crossed somewhere else as well, and
			// this piece is only a pocket behind that crossing.
			for (const int Node : Nodes)
			{
				if (Blue[Node] > Red[Node])
				{
					Crossing.Flip = Node;
					break;
				}
			}
			for (size_t k = 1; k < Theirs.size() && Crossing.Flip < 0; ++k)
			{
				if (Blue[Theirs[k]] <= Red[Theirs[k]])
				{
					Crossing.Flip = Theirs[k];
				}
			}
			Nodes.insert(Nodes.end(), Theirs.begin(), Theirs.end());
			Crossing.Path = Nodes;
			for (const int Node : Nodes)
			{
				const int TileX = (Node % Map.NavX) / Ground::NodesPerTile;
				const int TileY = (Node / Map.NavX) / Ground::NodesPerTile;
				const int Tile = Study.Tile(TileX, TileY);
				if (std::find(Crossing.Tiles.begin(), Crossing.Tiles.end(), Tile) != Crossing.Tiles.end())
				{
					continue;
				}
				Crossing.Tiles.push_back(Tile);
				Crossing.Embers += Study.Hazard(TileX, TileY) < 0 ? 1 : 0;
				Crossing.Springs += Study.Hazard(TileX, TileY) > 0 ? 1 : 0;
				Crossing.HighestLevel = std::max(Crossing.HighestLevel, Study.Level(TileX, TileY));
				Crossing.LowestLevel = std::min(Crossing.LowestLevel, Study.Level(TileX, TileY));
			}
		}

		// A piece whose best way through crosses the front twice is behind another.
		int Ways = 0;
		for (size_t c = 0; c < Crossings.size(); ++c)
		{
			FCrossing& Crossing = Crossings[c];
			if (Crossing.Flip >= 0)
			{
				// Behind whichever other piece is nearest where it crossed again.
				double Nearest = Infinity;
				const FVec2 At = FMap::NodePos(Map.NodeAt(Crossing.Flip));
				for (size_t o = 0; o < Crossings.size(); ++o)
				{
					if (o == c)
					{
						continue;
					}
					for (const int Node : Crossings[o].Nodes)
					{
						const double Distance = static_cast<double>(At.DistanceTo(FMap::NodePos(Map.NodeAt(Node))));
						if (Distance < Nearest)
						{
							Nearest = Distance;
							Crossing.Behind = static_cast<int>(o);
						}
					}
				}
				if (Crossing.Behind < 0)
				{
					Crossing.Behind = static_cast<int>(c);
				}
			}
			Ways += Crossing.Behind < 0 ? 1 : 0;
		}

		// The front drawn on the map: each piece's letter on the tiles it runs through.
		auto Letter = [](size_t Index) { return static_cast<char>(Index < 26 ? 'A' + Index : '?'); };
		std::vector<char> Marks(Study.TilesX * Study.TilesY, 0);
		for (size_t c = Crossings.size(); c-- > 0;)
		{
			for (const int Node : Crossings[c].Nodes)
			{
				Marks[Study.Tile((Node % Map.NavX) / Ground::NodesPerTile, (Node / Map.NavX) / Ground::NodesPerTile)] = Letter(c);
			}
		}
		PrintGrid(Study, "  ", [&](int X, int Y)
		{
			const char Mark = Marks[Study.Tile(X, Y)];
			return Mark ? Mark : '.';
		});

		int Rare = 0;
		for (size_t i = 0; i < Crossings.size(); ++i)
		{
			const FCrossing& Crossing = Crossings[i];
			const double Length = static_cast<double>(Crossing.Nodes.size()) * Ground::NavStep;
			const double Extra = 100.0 * (Crossing.Best / Best - 1.0);
			const int BestX = (Crossing.BestNode % Map.NavX) / Ground::NodesPerTile;
			const int BestY = (Crossing.BestNode / Map.NavX) / Ground::NodesPerTile;
			if (Crossing.Behind >= 0)
			{
				std::printf("  %c  front %4.1f m long: reached only by crossing the front at %c first, so not a separate way through\n",
					Letter(i), Length, Letter(static_cast<size_t>(Crossing.Behind)));
				continue;
			}
			std::printf("  %c  front %4.1f m long, best through %s: start to start %s", Letter(i), Length,
				TileName(BestX, BestY).c_str(), Metres(Crossing.Best).c_str());
			std::printf(Extra < 0.5 ? "  (shortest)" : "  (+%.0f%%)", Extra);
			std::printf("  levels %d-%d", Crossing.LowestLevel, Crossing.HighestLevel);
			if (Crossing.Embers > 0)
			{
				std::printf(", %d ember tile(s)", Crossing.Embers);
			}
			if (Crossing.Springs > 0)
			{
				std::printf(", %d spring tile(s)", Crossing.Springs);
			}
			std::printf("\n");
			if (Extra > 60.0)
			{
				++Rare;
			}
			if (Length < Ground::UnitSpacing * 2.0 && Ways > 1)
			{
				Study.Warnings.push_back(std::string("Way through ") + Letter(i)
					+ " is narrower than two units side by side: one unit can hold it alone.");
			}
		}
		std::printf("  (turned-around maps have each way through twice, once per side, unless it runs through the centre)\n");

		const double MapLength = static_cast<double>(std::max(Map.NavX, Map.NavY)) * Ground::NavStep;
		if (Ways == 1)
		{
			size_t Only = 0;
			while (Crossings[Only].Behind >= 0)
			{
				++Only;
			}
			const double Length = static_cast<double>(Crossings[Only].Nodes.size()) * Ground::NavStep;
			if (Length * 2.0 >= MapLength)
			{
				Study.Warnings.push_back("The front is open ground end to end: no chokepoints, so routes differ only by the hills.");
			}
			else
			{
				Study.Warnings.push_back("Only one way through: no route choice at all.");
			}
		}
		if (Rare > 0)
		{
			Study.Warnings.push_back(std::to_string(Rare)
				+ " way(s) through are over 60% longer than the shortest, so they will rarely be worth taking.");
		}
		return Crossings;
	}

	// ----------------------------------------------------------------- sight

	void MeasureSight(FStudy& Study)
	{
		// The longest sight among the built-in classes: what the best-sighted
		// unit on the map can take in from each tile.
		for (const char* Id : Classes)
		{
			if (const FJobDef* Job = FindJob(Id))
			{
				FUnit Unit;
				Unit.Job = Id;
				Unit.Stats = &Job->Stats;
				Study.SightRange = std::max(Study.SightRange, Study.Battle.SightOf(Unit));
			}
		}
		const int Count = Study.TilesX * Study.TilesY;
		Study.Exposure.assign(Count, 0);
		Study.Sees.assign(Count, 0);
		Study.Command.assign(Count, 0);
		for (int Ay = 0; Ay < Study.TilesY; ++Ay)
		{
			for (int Ax = 0; Ax < Study.TilesX; ++Ax)
			{
				if (!Study.Walkable(Ax, Ay))
				{
					continue;
				}
				const FVec2 From = FStudy::Centre(Ax, Ay);
				for (int By = 0; By < Study.TilesY; ++By)
				{
					for (int Bx = 0; Bx < Study.TilesX; ++Bx)
					{
						if ((Ax == Bx && Ay == By) || !Study.Walkable(Bx, By))
						{
							continue;
						}
						const FVec2 To = FStudy::Centre(Bx, By);
						if (From.DistanceTo(To) <= Study.SightRange && Study.Battle.HasLineOfSight(From, To))
						{
							++Study.Sees[Study.Tile(Ax, Ay)];
							++Study.Exposure[Study.Tile(Bx, By)];
							const int Over = Study.Level(Ax, Ay) - Study.Level(Bx, By);
							Study.Command[Study.Tile(Ax, Ay)] += std::max(0, std::min(Combat::MaxHeightLevels, Over));
						}
					}
				}
			}
		}
	}

	void ReportSight(FStudy& Study)
	{
		MeasureSight(Study);
		std::printf("\nSIGHT  (tile centre to tile centre, %.0f m: the longest sight of the built-in classes)\n", Study.SightRange);

		int Most = 0;
		for (const int Value : Study.Exposure)
		{
			Most = std::max(Most, Value);
		}
		std::printf("  how exposed each tile is: 9 = seen from the most places, 1 = from few, . = hidden\n");
		PrintGrid(Study, "  ", [&](int X, int Y) { return Scale(Study.Exposure[Study.Tile(X, Y)], Most); });

		// Whether anywhere a side may place a unit can see anywhere the other may.
		auto InStartArea = [&](int Side, int X, int Y)
		{
			return static_cast<double>(FStudy::Centre(X, Y).DistanceTo(Study.Battle.SpawnPoints[Side])) <= FBattle::PlanningRadius;
		};
		int Pairs = 0;
		double Closest = Infinity;
		for (int Ay = 0; Ay < Study.TilesY; ++Ay)
		{
			for (int Ax = 0; Ax < Study.TilesX; ++Ax)
			{
				if (!Study.Walkable(Ax, Ay) || !InStartArea(0, Ax, Ay))
				{
					continue;
				}
				for (int By = 0; By < Study.TilesY; ++By)
				{
					for (int Bx = 0; Bx < Study.TilesX; ++Bx)
					{
						if (!Study.Walkable(Bx, By) || !InStartArea(1, Bx, By))
						{
							continue;
						}
						const FVec2 A = FStudy::Centre(Ax, Ay);
						const FVec2 B = FStudy::Centre(Bx, By);
						if (A.DistanceTo(B) <= Study.SightRange && Study.Battle.HasLineOfSight(A, B))
						{
							++Pairs;
							Closest = std::min(Closest, static_cast<double>(A.DistanceTo(B)));
						}
					}
				}
			}
		}
		if (Pairs == 0)
		{
			std::printf("  the starting areas (%.0f m round each spawn point) cannot see each other\n", FBattle::PlanningRadius);
		}
		else
		{
			std::printf("  the starting areas can see each other from %d pair(s) of tiles, the closest %s apart\n", Pairs, Metres(Closest).c_str());
			Study.Warnings.push_back("The starting areas can see each other: the long-sighted can open fire before anyone has moved.");
		}

		// What overlooks the middle: tiles outside it that see most of it.
		const FVec2 Middle = Study.Battle.CapturePoint();
		std::vector<int> MiddleTiles;
		int RockInMiddle = 0;
		for (int Y = 0; Y < Study.TilesY; ++Y)
		{
			for (int X = 0; X < Study.TilesX; ++X)
			{
				if (static_cast<double>(FStudy::Centre(X, Y).DistanceTo(Middle)) <= FBattle::CaptureRadius)
				{
					if (Study.Walkable(X, Y))
					{
						MiddleTiles.push_back(Study.Tile(X, Y));
					}
					RockInMiddle += Study.IsRock(X, Y) ? 1 : 0;
				}
			}
		}
		struct FSpot
		{
			int X = 0;
			int Y = 0;
			int Seen = 0;
		};
		std::vector<FSpot> Overlooks;
		for (int Y = 0; Y < Study.TilesY; ++Y)
		{
			for (int X = 0; X < Study.TilesX; ++X)
			{
				if (!Study.Walkable(X, Y)
					|| std::find(MiddleTiles.begin(), MiddleTiles.end(), Study.Tile(X, Y)) != MiddleTiles.end())
				{
					continue;
				}
				FSpot Spot{ X, Y, 0 };
				for (const int Target : MiddleTiles)
				{
					const FVec2 To = FStudy::Centre(Target % Study.TilesX, Target / Study.TilesX);
					if (FStudy::Centre(X, Y).DistanceTo(To) <= Study.SightRange && Study.Battle.HasLineOfSight(FStudy::Centre(X, Y), To))
					{
						++Spot.Seen;
					}
				}
				if (Spot.Seen * 2 >= static_cast<int>(MiddleTiles.size()) && !MiddleTiles.empty())
				{
					Overlooks.push_back(Spot);
				}
			}
		}
		std::sort(Overlooks.begin(), Overlooks.end(), [&](const FSpot& A, const FSpot& B)
		{
			if (Study.Level(A.X, A.Y) != Study.Level(B.X, B.Y))
			{
				return Study.Level(A.X, A.Y) > Study.Level(B.X, B.Y);
			}
			return A.Seen > B.Seen;
		});
		std::printf("  the middle (%.0f m round the centre): %d walkable tile(s), %d rock\n",
			FBattle::CaptureRadius, static_cast<int>(MiddleTiles.size()), RockInMiddle);
		std::printf("  %d tile(s) outside it see at least half of it; the highest:", static_cast<int>(Overlooks.size()));
		for (size_t i = 0; i < Overlooks.size() && i < 6; ++i)
		{
			std::printf(" %s L%d", TileName(Overlooks[i].X, Overlooks[i].Y).c_str(), Study.Level(Overlooks[i].X, Overlooks[i].Y));
		}
		std::printf("\n");

		// Places to hide: walkable, and seen from almost nowhere.
		std::vector<std::string> Hidden;
		for (int Y = 0; Y < Study.TilesY; ++Y)
		{
			for (int X = 0; X < Study.TilesX; ++X)
			{
				if (Study.Walkable(X, Y) && Study.Exposure[Study.Tile(X, Y)] * 7 <= Most)
				{
					Hidden.push_back(TileName(X, Y));
				}
			}
		}
		std::printf("  %d well-hidden tile(s) (seen from a seventh as many places as the most exposed)", static_cast<int>(Hidden.size()));
		for (size_t i = 0; i < Hidden.size() && i < 10; ++i)
		{
			std::printf("%s%s", i == 0 ? ": " : " ", Hidden[i].c_str());
		}
		std::printf("%s\n", Hidden.size() > 10 ? " ..." : "");

		// Strong positions: the ones that look down on the most ground. Seeing is
		// nearly always mutual here, so what makes a position strong is the height
		// bonus over what it can see -- each level up to the cap, added up.
		struct FPower
		{
			int X = 0;
			int Y = 0;
			int Command = 0;
		};
		std::vector<FPower> Powers;
		for (int Y = 0; Y < Study.TilesY; ++Y)
		{
			for (int X = 0; X < Study.TilesX; ++X)
			{
				if (Study.Walkable(X, Y) && Study.Command[Study.Tile(X, Y)] > 0)
				{
					Powers.push_back(FPower{ X, Y, Study.Command[Study.Tile(X, Y)] });
				}
			}
		}
		std::stable_sort(Powers.begin(), Powers.end(), [](const FPower& A, const FPower& B) { return A.Command > B.Command; });
		if (Powers.empty())
		{
			std::printf("  no tile looks down on another: the ground is flat\n");
		}
		else
		{
			std::printf("  strongest positions (height levels over everything they can see, each capped at %d):\n   ",
				Combat::MaxHeightLevels);
			for (size_t i = 0; i < Powers.size() && i < 6; ++i)
			{
				const FPower& P = Powers[i];
				std::printf(" %s L%d: %d;", TileName(P.X, P.Y).c_str(), Study.Level(P.X, P.Y), P.Command);
			}
			std::printf("\n");
		}
		// Mirrored maps give every position a twin, so compare the best with the
		// best that is not its twin.
		if (Powers.size() > 2 && Powers[0].Command > 2 * Powers[2].Command)
		{
			Study.Warnings.push_back("One position (and its mirror twin) looks down on far more ground than anywhere else: "
				+ TileName(Powers[0].X, Powers[0].Y) + ". Check it isn't a spot that wins the map on its own.");
		}
	}

	// ---------------------------------------------------------------- height

	void ReportHeight(FStudy& Study)
	{
		std::printf("\nHEIGHT  (a unit climbs %d levels a step; each level above a target adds %.0f%%, up to %d levels)\n",
			Ground::Jump, 100.0 * Study.Battle.Tuning.HeightBonus, Combat::MaxHeightLevels);

		int Cliffs = 0;
		for (int Y = 0; Y < Study.TilesY; ++Y)
		{
			for (int X = 0; X < Study.TilesX; ++X)
			{
				if (!Study.Walkable(X, Y))
				{
					continue;
				}
				if (X + 1 < Study.TilesX && Study.Walkable(X + 1, Y) && std::abs(Study.Level(X, Y) - Study.Level(X + 1, Y)) > Ground::Jump)
				{
					++Cliffs;
				}
				if (Y + 1 < Study.TilesY && Study.Walkable(X, Y + 1) && std::abs(Study.Level(X, Y) - Study.Level(X, Y + 1)) > Ground::Jump)
				{
					++Cliffs;
				}
			}
		}
		std::printf("  %d cliff edge(s) between tiles (too high to climb in one step; fliers ignore them)\n", Cliffs);

		// Plateaus: connected tiles of one level, standing two or more above the
		// commonest level. Each with its ways up and how exposed its top is.
		int ByLevel[10] = { 0 };
		for (int Y = 0; Y < Study.TilesY; ++Y)
		{
			for (int X = 0; X < Study.TilesX; ++X)
			{
				if (Study.Walkable(X, Y))
				{
					++ByLevel[std::min(Study.Level(X, Y), 9)];
				}
			}
		}
		const int Common = static_cast<int>(std::max_element(ByLevel + 1, ByLevel + 10) - ByLevel);
		std::vector<int> Group(Study.TilesX * Study.TilesY, -1);
		int Groups = 0;
		int Most = 0;
		for (const int Value : Study.Exposure)
		{
			Most = std::max(Most, Value);
		}
		bool bAny = false;
		for (int Y = 0; Y < Study.TilesY; ++Y)
		{
			for (int X = 0; X < Study.TilesX; ++X)
			{
				const int Level = Study.Level(X, Y);
				if (!Study.Walkable(X, Y) || Level < Common + 2 || Group[Study.Tile(X, Y)] >= 0)
				{
					continue;
				}
				std::vector<int> Stack{ Study.Tile(X, Y) };
				std::vector<int> Members;
				Group[Study.Tile(X, Y)] = Groups;
				while (!Stack.empty())
				{
					const int T = Stack.back();
					Stack.pop_back();
					Members.push_back(T);
					const int Tx = T % Study.TilesX;
					const int Ty = T / Study.TilesX;
					for (int k = 0; k < 4; ++k)
					{
						const int Nx = Tx + DirX[k];
						const int Ny = Ty + DirY[k];
						if (Nx >= 0 && Ny >= 0 && Nx < Study.TilesX && Ny < Study.TilesY && Group[Study.Tile(Nx, Ny)] < 0
							&& Study.Walkable(Nx, Ny) && Study.Level(Nx, Ny) == Level)
						{
							Group[Study.Tile(Nx, Ny)] = Groups;
							Stack.push_back(Study.Tile(Nx, Ny));
						}
					}
				}
				// Ways up: lower neighbouring tiles a unit can step up from.
				int WaysUp = 0;
				double Exposure = 0.0;
				bool bWalkedTo = false;
				for (const int T : Members)
				{
					const int Tx = T % Study.TilesX;
					const int Ty = T / Study.TilesX;
					Exposure += Study.Exposure[T];
					bWalkedTo = bWalkedTo || !std::isinf(Study.TileWalk(0, Tx, Ty));
					for (int k = 0; k < 4; ++k)
					{
						const int Nx = Tx + DirX[k];
						const int Ny = Ty + DirY[k];
						if (Nx >= 0 && Ny >= 0 && Nx < Study.TilesX && Ny < Study.TilesY && Study.Walkable(Nx, Ny)
							&& Study.Level(Nx, Ny) < Level && Level - Study.Level(Nx, Ny) <= Ground::Jump)
						{
							++WaysUp;
						}
					}
				}
				Exposure /= static_cast<double>(Members.size());
				const int Tx = Members.front() % Study.TilesX;
				const int Ty = Members.front() / Study.TilesX;
				std::printf("  plateau at %s: level %d (%d over the common level %d), %d tile(s), %d way(s) up, top exposure %c/9%s\n",
					TileName(Tx, Ty).c_str(), Level, Level - Common, Common, static_cast<int>(Members.size()), WaysUp,
					Scale(Exposure, Most), bWalkedTo ? "" : ", fliers only");
				bAny = true;
				if (bWalkedTo && WaysUp <= 1 && Level - Common >= Combat::MaxHeightLevels && Exposure * 3 < Most)
				{
					Study.Warnings.push_back("The plateau at " + TileName(Tx, Ty)
						+ " has the full height bonus, one way up and is hard to see: it may be too easy to hold.");
				}
				++Groups;
			}
		}
		if (!bAny)
		{
			std::printf("  no plateaus: nothing stands two levels over the common level %d\n", Common);
			Study.Warnings.push_back("No real high ground: the height bonus will hardly come into it.");
		}
	}

	// --------------------------------------------------------------- hazards

	void ReportHazards(FStudy& Study, const std::vector<FCrossing>& Crossings)
	{
		std::printf("\nHAZARDS  (embers burn and springs heal %.0f%% of max health when a unit's turn comes round on them)\n",
			Study.Battle.Tuning.HazardPercent);
		int Most = 0;
		for (const int Value : Study.Exposure)
		{
			Most = std::max(Most, Value);
		}
		bool bAny = false;
		for (int Y = 0; Y < Study.TilesY; ++Y)
		{
			for (int X = 0; X < Study.TilesX; ++X)
			{
				if (!Study.Walkable(X, Y) || Study.Hazard(X, Y) == 0)
				{
					continue;
				}
				bAny = true;
				const bool bSpring = Study.Hazard(X, Y) > 0;
				const double Blue = Study.TileWalk(0, X, Y);
				const double Red = Study.TileWalk(1, X, Y);
				bool bOnRoute = false;
				for (const FCrossing& Crossing : Crossings)
				{
					bOnRoute = bOnRoute || std::find(Crossing.Tiles.begin(), Crossing.Tiles.end(), Study.Tile(X, Y)) != Crossing.Tiles.end();
				}
				std::printf("  %-6s %s level %d: blue walks %s, red walks %s, exposure %c/9%s\n", bSpring ? "spring" : "embers",
					TileName(X, Y).c_str(), Study.Level(X, Y), Metres(Blue).c_str(), Metres(Red).c_str(),
					Scale(Study.Exposure[Study.Tile(X, Y)], Most), bOnRoute ? ", on a route" : "");
				if (bSpring)
				{
					for (int Side = 0; Side < 2; ++Side)
					{
						if (static_cast<double>(FStudy::Centre(X, Y).DistanceTo(Study.Battle.SpawnPoints[Side])) <= FBattle::PlanningRadius + Ground::TileSize)
						{
							Study.Warnings.push_back("The spring at " + TileName(X, Y) + " is next to a starting area: free healing with no risk.");
						}
					}
					if (Study.Exposure[Study.Tile(X, Y)] * 7 <= Most)
					{
						Study.Warnings.push_back("The spring at " + TileName(X, Y) + " is well hidden: healing there costs nothing.");
					}
				}
			}
		}
		if (!bAny)
		{
			std::printf("  none\n");
		}
	}

	// --------------------------------------------------------------- battles

	struct FBattleTally
	{
		int Games = 0;
		int Wins[2] = { 0, 0 };
		int Draws = 0;
		int Unfinished = 0;
		int HeldMiddle = 0;
		int Illegal = 0;
		double Seconds = 0.0;
		double FirstHit = 0.0;
		int FirstHits = 0;
		std::vector<double> Stood;
		std::vector<int> Fell;
	};

	/** The walkable node nearest a spot with nobody standing too close to it. */
	FVec2 FreeSpotNear(const FBattle& Battle, const FVec2& Spot)
	{
		const FMap& Map = Battle.Map;
		FVec2 Best = Spot;
		double BestDistance = Infinity;
		for (int i = 0; i < Map.NavX * Map.NavY; ++i)
		{
			if (Map.NavLevels[i] <= 0)
			{
				continue;
			}
			const FVec2 Here = FMap::NodePos(Map.NodeAt(i));
			bool bFree = true;
			for (const FUnit& Other : Battle.Units)
			{
				bFree = bFree && Other.Pos.DistanceTo(Here) >= Ground::UnitSpacing;
			}
			const double Distance = static_cast<double>(Here.DistanceTo(Spot));
			if (bFree && Distance < BestDistance)
			{
				Best = Here;
				BestDistance = Distance;
			}
		}
		return Best;
	}

	const FUnit* NextReady(const FBattle& Battle)
	{
		for (const FUnit& Unit : Battle.Units)
		{
			if (Unit.IsAlive() && Unit.bReady && !Unit.IsStunned())
			{
				return &Unit;
			}
		}
		return nullptr;
	}

	void ReportBattles(FStudy& Study, int Games, const char* Skill, double CaptureSeconds, double LimitSeconds)
	{
		std::printf("\nBATTLES  %d game(s), the computer (%s) on both sides, each side", Games, Skill);
		for (const char* Id : Roster)
		{
			std::printf(" %s", Id);
		}
		std::printf("\n  hold-the-middle %s, time limit %.0f s\n",
			CaptureSeconds > 0.0 ? (std::to_string(static_cast<int>(CaptureSeconds)) + " s").c_str() : "off", LimitSeconds);

		FBattleTally Tally;
		Tally.Stood.assign(Study.TilesX * Study.TilesY, 0.0);
		Tally.Fell.assign(Study.TilesX * Study.TilesY, 0);
		const int TickLimit = static_cast<int>(LimitSeconds * Pace::TicksPerSecond);
		const auto Began = std::chrono::steady_clock::now();

		for (int Game = 0; Game < Games; ++Game)
		{
			FBattle Battle;
			if (Study.File.bMirror)
			{
				Battle.Map.BuildMirrored(Study.File.Rows);
			}
			else
			{
				Battle.Map.Build(Study.File.Rows);
			}
			Battle.SpawnPoints[0] = Study.Starts[0][0];
			Battle.SpawnPoints[1] = Study.Starts[1][0];
			Battle.Tuning.CaptureSeconds = CaptureSeconds;
			for (int Index = 0; Index < 8; ++Index)
			{
				// Every other game the red units come first. The rules take units in
				// id order, so whoever is listed first acts first when two are ready
				// on the same tick; alternating keeps that from favouring a colour.
				const bool bRedFirst = Game % 2 == 1;
				const int Side = (Index < 4) != bRedFirst ? 0 : 1;
				const std::vector<FVec2>& Starts = Study.Starts[Side];
				FUnit Unit;
				Unit.Id = Index;
				Unit.Team = Side;
				Unit.Job = Roster[Index % 4];
				Unit.Stats = &FindJob(Unit.Job)->Stats;
				if (Index % 4 < static_cast<int>(Starts.size()))
				{
					Unit.Pos = Starts[Index % 4];
				}
				else
				{
					// Fewer starts than units: the nearest free ground to the first.
					Unit.Pos = FreeSpotNear(Battle, Starts[0]);
				}
				Battle.Units.push_back(Unit);
			}
			Battle.Start(static_cast<uint64_t>(Game + 1));
			FAIPlayer Computer(Skill);
			Computer.Rng.Seed(static_cast<uint64_t>(Game + 1));

			int FirstHit = -1;
			while (Battle.Winner == -1 && Battle.TickCount < TickLimit)
			{
				const FUnit* Ready = NextReady(Battle);
				FOrder Order = Ready ? Computer.NextCommand(Battle, *Ready) : FOrder::MakeAdvance(1);
				if (!Battle.Validate(Order).empty())
				{
					++Tally.Illegal;
					if (!Ready)
					{
						break;
					}
					Order = FOrder::MakeEndTurn(Ready->Id, Ready->Serial);
					if (!Battle.Validate(Order).empty())
					{
						break;
					}
				}
				const int Before = Battle.TickCount;
				FTickReport Report;
				Battle.Apply(Order, Report);
				for (const FEvent& Event : Report.Events)
				{
					if (Event.Kind == EEventKind::Hit && FirstHit < 0)
					{
						FirstHit = Battle.TickCount;
					}
					if (Event.Kind == EEventKind::Knocked)
					{
						if (const FUnit* Down = Battle.FindUnit(Event.Unit))
						{
							const int Tile = Study.TileAt(Down->Pos);
							if (Tile >= 0)
							{
								++Tally.Fell[Tile];
							}
						}
					}
				}
				// Where everybody stands, once a second of battle.
				for (int Tick = Before; Tick < Battle.TickCount; ++Tick)
				{
					if ((Tick + 1) % Pace::TicksPerSecond != 0)
					{
						continue;
					}
					for (const FUnit& Unit : Battle.Units)
					{
						if (Unit.IsAlive())
						{
							const int Tile = Study.TileAt(Unit.Pos);
							if (Tile >= 0)
							{
								Tally.Stood[Tile] += 1.0;
							}
						}
					}
				}
			}
			++Tally.Games;
			Tally.Seconds += static_cast<double>(Battle.TickCount) / Pace::TicksPerSecond;
			if (FirstHit >= 0)
			{
				Tally.FirstHit += static_cast<double>(FirstHit) / Pace::TicksPerSecond;
				++Tally.FirstHits;
			}
			if (Battle.Winner == 0 || Battle.Winner == 1)
			{
				++Tally.Wins[Battle.Winner];
				// Won with the other side still standing: it held the middle.
				Tally.HeldMiddle += Battle.TeamUnits(1 - Battle.Winner).empty() ? 0 : 1;
			}
			else if (Battle.Winner == FBattle::Draw)
			{
				++Tally.Draws;
			}
			else
			{
				++Tally.Unfinished;
			}
		}
		const double Elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - Began).count();

		std::printf("  (the side listed first alternates each game, since units are taken in id order)\n");
		std::printf("  blue won %d, red won %d, drawn %d, unfinished %d   (%.1f s to play them)\n",
			Tally.Wins[0], Tally.Wins[1], Tally.Draws, Tally.Unfinished, Elapsed);
		if (CaptureSeconds > 0.0)
		{
			std::printf("  %d won by holding the middle, the rest by knocking the other side out\n", Tally.HeldMiddle);
		}
		std::printf("  battles lasted %.0f s on average; first blood after %s\n", Tally.Seconds / std::max(1, Tally.Games),
			Tally.FirstHits > 0 ? (std::to_string(static_cast<int>(std::lround(Tally.FirstHit / Tally.FirstHits))) + " s on average").c_str() : "never");
		if (Tally.Illegal > 0)
		{
			std::printf("  %d order(s) were refused by the rules and replaced with ending the turn\n", Tally.Illegal);
		}

		double MostStood = 0.0;
		int MostFell = 0;
		int Visited = 0;
		int Reachable = 0;
		for (int Y = 0; Y < Study.TilesY; ++Y)
		{
			for (int X = 0; X < Study.TilesX; ++X)
			{
				const int T = Study.Tile(X, Y);
				MostStood = std::max(MostStood, Tally.Stood[T]);
				MostFell = std::max(MostFell, Tally.Fell[T]);
				if (Study.Walkable(X, Y) && !std::isinf(Study.TileWalk(0, X, Y)))
				{
					++Reachable;
					Visited += Tally.Stood[T] > 0.0 ? 1 : 0;
				}
			}
		}
		std::printf("\n  where units stood (9 = most time, . = never)      where units fell (count, * for 10+)\n");
		std::printf("      ");
		for (int X = 0; X < Study.TilesX; ++X)
		{
			std::printf("%d", X % 10);
		}
		std::printf("%*s", 45 - Study.TilesX, "");
		for (int X = 0; X < Study.TilesX; ++X)
		{
			std::printf("%d", X % 10);
		}
		std::printf("\n");
		for (int Y = 0; Y < Study.TilesY; ++Y)
		{
			std::printf("  %3d ", Y);
			for (int X = 0; X < Study.TilesX; ++X)
			{
				std::printf("%c", Study.IsRock(X, Y) ? '#' : !Study.Walkable(X, Y) ? '~' : Scale(Tally.Stood[Study.Tile(X, Y)], MostStood));
			}
			std::printf("%*s", 45 - Study.TilesX, "");
			for (int X = 0; X < Study.TilesX; ++X)
			{
				const int Fell = Tally.Fell[Study.Tile(X, Y)];
				std::printf("%c", Study.IsRock(X, Y) ? '#' : !Study.Walkable(X, Y) ? '~'
					: Fell == 0 ? '.' : Fell >= 10 ? '*' : static_cast<char>('0' + Fell));
			}
			std::printf("\n");
		}
		const int Unused = Reachable - Visited;
		std::printf("  %d of %d reachable tiles were never stood on\n", Unused, Reachable);

		// Identical teams on identical halves should split the wins evenly. How far
		// from even chance alone takes it is half the square root of the games, so
		// past two and a half of those it is very unlikely to be chance.
		const int Decided = Tally.Wins[0] + Tally.Wins[1];
		const double Spread = 0.5 * std::sqrt(static_cast<double>(Decided));
		const double Off = std::fabs(static_cast<double>(Tally.Wins[0]) - 0.5 * Decided);
		if (Decided >= 20 && Off > 2.5 * Spread)
		{
			Study.Warnings.push_back(std::string(Tally.Wins[0] > Tally.Wins[1] ? "Blue" : "Red") + " won "
				+ std::to_string(std::max(Tally.Wins[0], Tally.Wins[1])) + " of " + std::to_string(Decided)
				+ " decided games with identical teams, more than chance gives. The halves, the teams and who is listed"
				" first are all even, so the lean comes from the rules or the computer player treating the two sides"
				" differently (taking ties in grid order, say), not from the ground itself.");
		}
		else if (Decided < 20)
		{
			std::printf("  (fewer than 20 decided games: too few to judge whether either side is favoured)\n");
		}
		if (Tally.Unfinished * 2 > Tally.Games)
		{
			Study.Warnings.push_back("Most battles hit the time limit: the map may be too easy to turtle on.");
		}
		if (Reachable > 0 && Unused * 3 > Reachable)
		{
			Study.Warnings.push_back(std::to_string(Unused) + " of " + std::to_string(Reachable)
				+ " reachable tiles were never stood on in the battles: over a third of the map is dead ground to the computer.");
		}
	}

	// ------------------------------------------------------------------ main

	struct FOptions
	{
		std::vector<std::string> Files;
		int Battles = 0;
		std::string Skill = "hard";
		double CaptureSeconds = 0.0;
		double LimitSeconds = 600.0;
	};

	void PrintUsage()
	{
		std::printf("usage: TMMapAnalyzer <map file>... [--battles N] [--skill easy|medium|hard]\n"
			"                     [--capture SECONDS] [--limit SECONDS]\n"
			"  --battles N      also play N battles, computer against computer (default 0)\n"
			"  --skill          how well the computer plays (default hard)\n"
			"  --capture S      turn on hold-the-middle: S seconds alone in the middle wins\n"
			"  --limit S        stop a battle after S seconds of battle time (default 600)\n"
			"See the top of Tools/MapAnalyzer/MapAnalyzer.cpp for the map file format.\n");
	}

	int Analyse(const std::string& Path, const FOptions& Options)
	{
		FStudy Study;
		Study.File = ReadMapFile(Path);
		std::printf("==============================================================================\n");
		std::printf("MAP  %s   (%s)\n", Study.File.Name.c_str(), Path.c_str());
		if (!Study.File.Problems.empty())
		{
			for (const std::string& Problem : Study.File.Problems)
			{
				std::printf("  ERROR: %s\n", Problem.c_str());
			}
			return 1;
		}
		if (!Build(Study))
		{
			return 1;
		}
		ReportGround(Study);
		ReportTempo(Study);
		const std::vector<FCrossing> Crossings = ReportRoutes(Study);
		ReportSight(Study);
		ReportHeight(Study);
		ReportHazards(Study, Crossings);
		if (Options.Battles > 0)
		{
			ReportBattles(Study, Options.Battles, Options.Skill.c_str(), Options.CaptureSeconds, Options.LimitSeconds);
		}

		std::printf("\nWARNINGS  (prompts to look, not verdicts)\n");
		if (Study.Warnings.empty())
		{
			std::printf("  none\n");
		}
		for (const std::string& Warning : Study.Warnings)
		{
			std::printf("  - %s\n", Warning.c_str());
		}
		std::printf("\n");
		return 0;
	}
}

int main(int ArgCount, char** Args)
{
	FOptions Options;
	for (int i = 1; i < ArgCount; ++i)
	{
		const std::string Arg = Args[i];
		const bool bHasValue = i + 1 < ArgCount;
		if (Arg == "--battles" && bHasValue)
		{
			Options.Battles = std::max(0, std::atoi(Args[++i]));
		}
		else if (Arg == "--skill" && bHasValue)
		{
			Options.Skill = Args[++i];
		}
		else if (Arg == "--capture" && bHasValue)
		{
			Options.CaptureSeconds = std::max(0.0, std::atof(Args[++i]));
		}
		else if (Arg == "--limit" && bHasValue)
		{
			Options.LimitSeconds = std::max(1.0, std::atof(Args[++i]));
		}
		else if (Arg == "--help" || Arg == "-h" || Arg.rfind("--", 0) == 0)
		{
			PrintUsage();
			return Arg == "--help" || Arg == "-h" ? 0 : 2;
		}
		else
		{
			Options.Files.push_back(Arg);
		}
	}
	if (Options.Files.empty())
	{
		PrintUsage();
		return 2;
	}
	int Failed = 0;
	for (const std::string& File : Options.Files)
	{
		Failed += Analyse(File, Options) != 0 ? 1 : 0;
	}
	return Failed > 0 ? 1 : 0;
}
