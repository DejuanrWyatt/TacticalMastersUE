// Maps: the one Godot built in, and the ones written as files.
//
// Highlands is Godot's (map_data.gd) and the parity tests stand on it, so it is
// checked here against Godot's own rows and spawns. The rest are the port's
// own: every file in Content/Data/Maps must be accepted, a set of broken ones
// must each be refused for the reason given, and a whole battle is played on
// every map, computer against computer through the order path, to see it is
// decided with no order refused.
//
//   SimMapTest <Content/Data/Maps folder>

#include "SimAI.h"
#include "SimBattle.h"
#include "SimMap.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace TMSim;

namespace
{
	int Failures = 0;

	void Fail(const std::string& What)
	{
		std::printf("FAIL: %s\n", What.c_str());
		++Failures;
	}

	std::string ReadAll(const std::filesystem::path& Path)
	{
		std::ifstream In(Path, std::ios::binary);
		std::stringstream Out;
		Out << In.rdbuf();
		return Out.str();
	}

	/** A map file with one part changed, to see it refused. */
	std::string MapText(const std::string& Top, const std::string& Spawns, const std::string& Extra = "")
	{
		return "{\"format\": \"tactical-masters-map\", \"version\": 1, \"id\": \"probe\", \"name\": \"Probe\", "
			"\"top\": " + Top + ", \"spawns\": " + Spawns + Extra + "}";
	}

	const char* const GoodTop = "[\"111111111111\", \"111111111111\", \"111111111111\", \"111111111111\", \"111111111111\", \"111111111111\"]";
	const char* const GoodSpawns = "[[2.75, 4.75], [0.75, 8.75], [4.75, 6.75], [2.75, 10.75]]";

	/** A whole battle on a map, computer against computer, hard, through the order path. */
	void PlayOn(const FMapDef& Def)
	{
		FBattle Battle;
		Battle.Map.BuildMirrored(Def.Top);
		const FVec2 Size = Battle.Map.SizeMeters();
		const char* const Roster[4] = { "knight", "archer", "black_mage", "white_mage" };
		for (int Index = 0; Index < 8; ++Index)
		{
			FUnit Unit;
			Unit.Id = Index;
			Unit.Team = Index < 4 ? 0 : 1;
			Unit.Job = Roster[Index % 4];
			const FVec2 Spot = Def.Spawns[Index % 4];
			Unit.Pos = Index < 4 ? Spot : FVec2(Size.X - Spot.X, Size.Y - Spot.Y);
			Battle.Units.push_back(Unit);
		}
		Battle.SpawnPoints[0] = Def.Spawns[0];
		Battle.SpawnPoints[1] = FVec2(Size.X - Def.Spawns[0].X, Size.Y - Def.Spawns[0].Y);
		Battle.Start(777);
		FAIPlayer Computer("hard");
		Computer.Rng.Seed(777);

		// Ten minutes of battle: a bigger map takes longer to cross.
		const int TickLimit = 6000;
		int Orders = 0;
		int Refused = 0;
		int FirstHit = -1;
		while (Battle.TickCount < TickLimit && Battle.Winner < 0)
		{
			const FUnit* Ready = nullptr;
			for (const FUnit& Unit : Battle.Units)
			{
				if (Unit.IsAlive() && Unit.bReady)
				{
					Ready = &Unit;
					break;
				}
			}
			FTickReport Report;
			if (!Ready)
			{
				Battle.Advance(1, Report);
			}
			else
			{
				FOrder Order = Computer.NextCommand(Battle, *Ready);
				if (!Battle.Validate(Order).empty())
				{
					++Refused;
					Order = FOrder::MakeEndTurn(Ready->Id, Ready->Serial);
				}
				if (!Battle.Apply(Order, Report))
				{
					Fail(Def.Id + ": an order passed the rules and then did nothing");
					return;
				}
				++Orders;
			}
			for (const FEvent& Event : Report.Events)
			{
				if (FirstHit < 0 && Event.Kind == EEventKind::Hit && Event.By >= 0)
				{
					FirstHit = Battle.TickCount;
				}
			}
		}
		std::printf("  %s (%dx%d tiles): %d orders, first blow at %.1fs, %s after %.1fs\n", Def.Id.c_str(),
			Battle.Map.TilesX, Battle.Map.TilesY, Orders, FirstHit / 10.0,
			Battle.Winner < 0 ? "UNDECIDED" : (Battle.Winner == 0 ? "blue won" : "red won"), Battle.TickCount / 10.0);
		if (Refused > 0)
		{
			Fail(Def.Id + ": the computer asked for " + std::to_string(Refused) + " orders the rules refused");
		}
		if (Battle.Winner < 0)
		{
			Fail(Def.Id + ": not decided in ten minutes");
		}
	}
}

int main(int ArgCount, char** Args)
{
	// Highlands is Godot's: map_data.gd's rows and WEST_SPAWNS, exactly.
	const FMapDef& Highlands = FindMap("highlands");
	const std::vector<std::string> GodotRows = { "112233211111", "112233211111", "111222111111", "111111111111", "122111121111", "123111121111" };
	if (Highlands.Top != GodotRows)
	{
		Fail("Highlands is not Godot's map");
	}
	const float GodotSpawns[4][2] = { { 2.75f, 4.75f }, { 0.75f, 8.75f }, { 4.75f, 6.75f }, { 2.75f, 10.75f } };
	for (int i = 0; i < 4; ++i)
	{
		if (Highlands.Spawns.size() != 4 || Highlands.Spawns[i].X != GodotSpawns[i][0] || Highlands.Spawns[i].Y != GodotSpawns[i][1])
		{
			Fail("Highlands' spawns are not Godot's WEST_SPAWNS");
		}
	}
	if (!CheckMap(Highlands).empty())
	{
		Fail("Highlands fails its own checks: " + CheckMap(Highlands));
	}
	if (FindMap("no_such_map").Id != "highlands")
	{
		Fail("an unknown map should fall back to Highlands");
	}

	// Broken maps, each refused for its own reason.
	struct FProbe
	{
		const char* What;
		std::string Text;
		const char* Expect;
	};
	const std::vector<FProbe> Probes =
	{
		{ "a good one", MapText(GoodTop, GoodSpawns), "" },
		{ "not ground", MapText("[\"11111111111Q\", \"111111111111\", \"111111111111\"]", GoodSpawns), "is not ground" },
		{ "uneven rows", MapText("[\"111111111111\", \"11111111111\", \"111111111111\"]", GoodSpawns), "not as wide" },
		{ "too narrow", MapText("[\"1111111\", \"1111111\", \"1111111\"]", GoodSpawns), "8 to 40" },
		{ "three spawns", MapText(GoodTop, "[[2.75, 4.75], [0.75, 8.75], [4.75, 6.75]]"), "four" },
		{ "a spawn in rock", MapText("[\"111111111111\", \"111111111111\", \"1#1111111111\", \"111111111111\", \"111111111111\", \"111111111111\"]", GoodSpawns), "not on ground" },
		{ "a spawn off the map", MapText(GoodTop, "[[2.75, 4.75], [0.75, 8.75], [4.75, 6.75], [99.0, 1.0]]"), "not on ground" },
		{ "spawns on top of each other", MapText(GoodTop, "[[2.75, 4.75], [2.75, 4.75], [4.75, 6.75], [2.75, 10.75]]"), "too close" },
		{ "walled in by water", MapText("[\"111111111111\", \"111111111111\", \"111111111111\", \"111111111111\", \"111111111111\", \"~~~~~~~~~~~~\"]", "[[2.75, 2.75], [0.75, 4.75], [4.75, 6.75], [2.75, 8.75]]"), "cannot walk" },
		{ "walled in by cliffs", MapText("[\"111111111111\", \"111111111111\", \"111111111111\", \"111111111111\", \"111111111111\", \"555555555555\"]", GoodSpawns), "cannot walk" },
		{ "an unknown key", MapText(GoodTop, GoodSpawns, ", \"weather\": \"rain\""), "unknown key" },
		{ "another format", "{\"format\": \"something\", \"version\": 1}", "format" },
	};
	int Refused = 0;
	for (const FProbe& Probe : Probes)
	{
		FMapDef Def;
		const std::string Problems = ReadMapFile(Probe.Text, Def);
		if (*Probe.Expect == '\0')
		{
			if (!Problems.empty())
			{
				Fail(std::string(Probe.What) + " was refused: " + Problems);
			}
		}
		else if (Problems.find(Probe.Expect) == std::string::npos)
		{
			Fail(std::string(Probe.What) + " should be refused (" + Probe.Expect + "), said: " + Problems);
		}
		else
		{
			++Refused;
		}
	}
	std::printf("broken maps refused: %d of %d\n", Refused, static_cast<int>(Probes.size()) - 1);
	FMapDef Imposter = Highlands;
	if (RegisterMap(Imposter).empty())
	{
		Fail("a file may not replace Highlands");
	}

	// Every map the game ships.
	if (ArgCount >= 2 && std::filesystem::exists(Args[1]))
	{
		int Loaded = 0;
		for (const auto& Entry : std::filesystem::directory_iterator(Args[1]))
		{
			const std::string Name = Entry.path().filename().string();
			if (Name.size() < 11 || Name.substr(Name.size() - 11) != ".tmmap.json")
			{
				continue;
			}
			FMapDef Def;
			const std::string Problems = ReadMapFile(ReadAll(Entry.path()), Def);
			if (!Problems.empty())
			{
				Fail(Name + " refused: " + Problems);
				continue;
			}
			if (Def.Id + ".tmmap.json" != Name)
			{
				Fail(Name + " should be named for its id, " + Def.Id);
			}
			if (!RegisterMap(Def).empty())
			{
				Fail(Name + " could not be added");
			}
			++Loaded;
		}
		std::printf("map files loaded: %d\n", Loaded);
	}
	else
	{
		Fail("no map folder given");
	}

	std::printf("a battle on every map:\n");
	for (const FMapDef& Def : AllMaps())
	{
		PlayOn(Def);
	}

	if (Failures > 0)
	{
		std::printf("%d FAILED\n", Failures);
		return 1;
	}
	std::printf("ALL TESTS PASSED\n");
	return 0;
}
