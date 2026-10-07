// Ground zones (2026-10-04, Docs/design/feat-ground-zones.md): the twelve area
// denial abilities in the class files do what the design says -- ground that
// hurts, ground that hangs statuses, ground that sees and ground that hides --
// and battles where the computer plays the classes that carry them are legal
// and replay to the same checksum.
//
// Not Godot's: nothing in a Godot battle lays a zone, and nothing here runs
// unless one is laid, which the other suites (their baselines unchanged) hold.
//
// Run with the classes and maps folders: SimZoneTest <Content/Data/Classes> <Content/Data/Maps>.

#include "SimAI.h"
#include "SimBattle.h"
#include "SimClassFile.h"
#include "SimJson.h"
#include "SimMap.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
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
		if (Failures < 40)
		{
			std::printf("  %s\n", What.c_str());
		}
		++Failures;
	}

	void Check(bool bOk, const std::string& What)
	{
		if (!bOk)
		{
			Fail(What);
		}
	}

	std::string ReadAll(const std::filesystem::path& Path)
	{
		std::ifstream In(Path, std::ios::binary);
		std::stringstream Buffer;
		Buffer << In.rdbuf();
		return Buffer.str();
	}

	std::vector<std::filesystem::path> FilesIn(const std::filesystem::path& Folder, const std::string& Ending)
	{
		std::vector<std::filesystem::path> Paths;
		if (!std::filesystem::exists(Folder))
		{
			return Paths;
		}
		for (const auto& Entry : std::filesystem::directory_iterator(Folder))
		{
			const std::string Name = Entry.path().filename().string();
			if (Name.size() > Ending.size() && Name.substr(Name.size() - Ending.size()) == Ending)
			{
				Paths.push_back(Entry.path());
			}
		}
		std::sort(Paths.begin(), Paths.end());
		return Paths;
	}

	struct FSetup
	{
		std::string Job;
		int Team;
		FVec2 At;
	};

	/** Units of these classes on Highlands where given, exactly, no dodging and no crits. */
	void Deal(FBattle& Battle, const std::vector<FSetup>& Setups)
	{
		Battle.Map.BuildMirrored(HighlandsRows());
		for (size_t i = 0; i < Setups.size(); ++i)
		{
			FUnit Unit;
			Unit.Id = static_cast<int>(i);
			Unit.Team = Setups[i].Team;
			Unit.Job = Setups[i].Job;
			Unit.Pos = FMap::Snap(Setups[i].At);
			Battle.Units.push_back(Unit);
		}
		Battle.Tuning.EvadeMultiplier = 0.0;
		Battle.Tuning.CritChanceMultiplier = 0.0;
		Battle.Tuning.CastTimeMultiplier = 0.0;
		Battle.Start(99);
		for (FUnit& Unit : Battle.Units)
		{
			Unit.Tg = 0;
		}
	}

	/** Runs the clock until this unit's turn comes, every other unit ending its turns as they come. */
	void TurnOf(FBattle& Battle, int Id, FTickReport& Report)
	{
		for (int i = 0; i < 20000; ++i)
		{
			const FUnit* Unit = Battle.FindUnit(Id);
			if (!Unit || !Unit->IsAlive() || Unit->bReady)
			{
				return;
			}
			bool bOther = false;
			for (const FUnit& Each : Battle.Units)
			{
				if (Each.IsAlive() && Each.bReady && Each.Id != Id)
				{
					Battle.Apply(FOrder::MakeEndTurn(Each.Id, Each.Serial), Report);
					bOther = true;
					break;
				}
			}
			if (!bOther)
			{
				Battle.Tick(Report);
			}
		}
	}

	/** The unit's ability in slot Slot, aimed at Target, through Validate. */
	bool Use(FBattle& Battle, int Id, int Slot, const FVec2& Target, FTickReport& Report)
	{
		const FUnit* Unit = Battle.FindUnit(Id);
		const FOrder Order = FOrder::MakeUseAbility(Id, Unit->Serial, Slot, Target);
		const std::string Why = Battle.Validate(Order);
		if (!Why.empty())
		{
			Fail(Unit->Job + " can't use slot " + std::to_string(Slot) + ": " + Why);
			return false;
		}
		Battle.Apply(Order, Report);
		return true;
	}

	void EndTurn(FBattle& Battle, int Id, FTickReport& Report)
	{
		const FUnit* Unit = Battle.FindUnit(Id);
		if (Unit && Unit->bReady)
		{
			Battle.Apply(FOrder::MakeEndTurn(Id, Unit->Serial), Report);
		}
	}

	int Count(const FTickReport& Report, EEventKind Kind, int Unit = -1, const std::string& Id = "")
	{
		int N = 0;
		for (const FEvent& Event : Report.Events)
		{
			N += Event.Kind == Kind && (Unit < 0 || Event.Unit == Unit) && (Id.empty() || Event.Id == Id) ? 1 : 0;
		}
		return N;
	}

	int SlotOf(const std::string& Job, const std::string& AbilityId)
	{
		for (int Slot = 0; Slot < AbilitySlots; ++Slot)
		{
			const FAbility* Ability = JobAbility(Job, Slot);
			if (Ability && Ability->Id == AbilityId)
			{
				return Slot;
			}
		}
		return -1;
	}
}

int main(int ArgCount, char** Args)
{
	const std::filesystem::path ClassFolder = ArgCount >= 2 ? Args[1] : "";
	const std::filesystem::path MapFolder = ArgCount >= 3 ? Args[2] : "";
	for (const auto& Path : FilesIn(ClassFolder, ".tmclass.json"))
	{
		const std::string Problems = LoadClassFile(ReadAll(Path));
		if (!Problems.empty())
		{
			Fail(Path.filename().string() + " was refused: " + Problems);
		}
	}

	// The twelve, read from their files, each in the class it was given to.
	const std::pair<const char*, const char*> Twelve[] = {
		{ "flame_sorcerer", "flame_sorcerer_ember_field" }, { "stormcaller", "stormcaller_static_mire" },
		{ "necromancer", "necromancer_caustic_pool" }, { "cryomancer", "cryomancer_frost_patch" },
		{ "snare_hunter", "snare_hunter_tar_slick" }, { "druid", "druid_bramble_thicket" },
		{ "null_monk", "null_monk_hush_circle" }, { "sea_witch", "sea_witch_tide_pool" },
		{ "sun_archer", "sun_archer_scout_flare" }, { "oracle", "oracle_watchers_eye" },
		{ "lumimancer", "lumimancer_lantern_glow" }, { "shadow_stalker", "shadow_stalker_smoke_veil" } };
	{
		const int Before = Failures;
		for (const auto& Each : Twelve)
		{
			const int Slot = SlotOf(Each.first, Each.second);
			const FAbility* Ability = FindAbility(Each.second);
			Check(Slot >= 0 && Ability && Ability->LaysZone() && Ability->Special == "zone",
				std::string(Each.second) + " should be a zone ability of " + Each.first);
		}
		const FAbility* Tar = FindAbility("snare_hunter_tar_slick");
		Check(Tar && Tar->bZoneFlammable && Tar->ZoneStatus2 == "slow" && Tar->StatusId == "oiled" && ShapeOf(*Tar) == "line",
			"Tar Slick should be a flammable line that Oils and Slows");
		const FAbility* Flare = FindAbility("sun_archer_scout_flare");
		Check(Flare && Flare->ZoneSight == 4.0f && Flare->bZoneReveal, "Scout Flare should see 4 m and find what hides");
		// A zone without its "zone", and a zone with a wrong key, are refused.
		auto Refused = [](const std::string& Extra)
		{
			FJson Json;
			ParseJson("{\"id\": \"t_z\", \"name\": \"Z\", \"kind\": \"active\", \"effect\": \"support\", \"scale\": \"mag\", "
				"\"target\": \"enemy\"" + Extra + "}", Json);
			FAbility Probe;
			return !ReadAbilityObject(Json, "z", Probe).empty();
		};
		Check(Refused(", \"special\": \"zone\""), "\"special\": \"zone\" without a \"zone\" should be refused");
		Check(Refused(", \"special\": \"zone\", \"zone\": {\"turns\": 2, \"glow\": 1}"), "a zone with an unknown key should be refused");
		Check(Refused(", \"zone\": {\"turns\": 2}"), "a \"zone\" without \"special\": \"zone\" should be refused");
		Check(!Refused(", \"special\": \"zone\", \"zone\": {\"turns\": 2, \"percent\": 5}"), "a good zone should be read");
		if (Failures == Before)
		{
			std::printf("the twelve zone abilities read from their class files; broken zones are refused\n");
		}
	}

	// Ember Field: laid where aimed, touches nobody as it goes off; an enemy that
	// starts its turn in it loses 6% and Burns, once a turn; it counts down on the
	// caster's turns and goes.
	{
		const int Before = Failures;
		FBattle Battle;
		Deal(Battle, { { "flame_sorcerer", 0, FVec2(8.25f, 12.25f) }, { "knight", 1, FVec2(13.25f, 12.25f) } });
		const int Slot = SlotOf("flame_sorcerer", "flame_sorcerer_ember_field");
		FTickReport Report;
		TurnOf(Battle, 0, Report);
		const FVec2 Aim = Battle.Units[1].Pos;
		const int Hp = Battle.Units[1].Hp;
		FTickReport Cast;
		if (Use(Battle, 0, Slot, Aim, Cast))
		{
			Check(Battle.Zones.size() == 1 && Battle.Zones[0].Turns == 3 && Count(Cast, EEventKind::ZoneLaid) == 1,
				"Ember Field should lay one zone of 3 turns");
			Check(Battle.Units[1].Hp == Hp && Count(Cast, EEventKind::Hit) == 0, "laying a zone should touch nobody");
			EndTurn(Battle, 0, Cast);
			FTickReport Next;
			TurnOf(Battle, 1, Next);
			const int Lost = Hp - Battle.Units[1].Hp;
			const int Expected = RoundToInt(Battle.Units[1].MaxHp() * 0.06);
			Check(Lost == Expected, "an enemy starting its turn in Ember Field should lose 6% (" + std::to_string(Expected)
				+ "), lost " + std::to_string(Lost));
			Check(Battle.Units[1].HasStatus("burn"), "Ember Field should Burn");
			// Walking about inside it in the same turn: not touched again.
			const int After = Battle.Units[1].Hp;
			FTickReport Walk;
			const FVec2 Step = FMap::Snap(Aim + FVec2(0.5f, 0.0f));
			if (Battle.Validate(FOrder::MakeMove(1, Battle.Units[1].Serial, Step)).empty())
			{
				Battle.Apply(FOrder::MakeMove(1, Battle.Units[1].Serial, Step), Walk);
				Check(Battle.Units[1].Hp == After, "a zone should touch a unit only once a turn");
			}
			EndTurn(Battle, 1, Walk);
			// The caster's turns count it down: 3 laid, 2, 1, gone.
			for (int Turn = 0; Turn < 3; ++Turn)
			{
				FTickReport Round;
				TurnOf(Battle, 0, Round);
				EndTurn(Battle, 0, Round);
				if (Turn < 2)
				{
					Check(Battle.Zones.size() == 1 && Battle.Zones[0].Turns == 2 - Turn, "Ember Field should count down on its caster's turns");
				}
				else
				{
					Check(Battle.Zones.empty() && Count(Round, EEventKind::ZoneEnded) == 1, "Ember Field should be gone after 3 of its caster's turns");
				}
			}
		}
		if (Failures == Before)
		{
			std::printf("Ember Field touches nobody as it is laid, takes 6%% and Burns as a turn starts in it, once a turn, and lasts 3 of its caster's turns\n");
		}
	}

	// Stopping in one: a walk that ends in Bramble Thicket roots for the next turn;
	// the first time only. Recasting moves the zone.
	{
		const int Before = Failures;
		FBattle Battle;
		Deal(Battle, { { "druid", 0, FVec2(8.25f, 12.25f) }, { "knight", 1, FVec2(16.25f, 12.25f) } });
		const int Slot = SlotOf("druid", "druid_bramble_thicket");
		FTickReport Report;
		TurnOf(Battle, 0, Report);
		const FVec2 Aim(12.25f, 12.25f);
		if (Use(Battle, 0, Slot, Aim, Report))
		{
			EndTurn(Battle, 0, Report);
			TurnOf(Battle, 1, Report);
			FTickReport Walk;
			const FOrder Move = FOrder::MakeMove(1, Battle.Units[1].Serial, Aim);
			Check(Battle.Validate(Move).empty(), "the knight should be able to walk into the brambles");
			Battle.Apply(Move, Walk);
			Check(Battle.Units[1].HasStatus("root"), "stopping in Bramble Thicket should Root");
			EndTurn(Battle, 1, Walk);
			FTickReport Next;
			TurnOf(Battle, 1, Next);
			Check(Battle.Units[1].IsRooted(), "a Root from stopping in it should hold through the next turn");
			EndTurn(Battle, 1, Next);
			FTickReport Third;
			TurnOf(Battle, 1, Third);
			Check(!Battle.Units[1].IsRooted() || Battle.Zones.empty(), "Bramble Thicket should Root a unit only the first time");
		}
		if (Failures == Before)
		{
			std::printf("stopping in Bramble Thicket Roots for the next turn, the first time only\n");
		}
	}

	// Elements: Static Mire on a Wet unit takes its turn (and not the next);
	// Frost Patch freezes the Wet the same way; fire lights Tar Slick; water puts
	// burning ground out.
	{
		const int Before = Failures;
		{
			FBattle Battle;
			Deal(Battle, { { "stormcaller", 0, FVec2(8.25f, 12.25f) }, { "knight", 1, FVec2(13.25f, 12.25f) } });
			FTickReport Report;
			TurnOf(Battle, 0, Report);
			Battle.AddStatus(Battle.Units[1], "wet", 3);
			if (Use(Battle, 0, SlotOf("stormcaller", "stormcaller_static_mire"), Battle.Units[1].Pos, Report))
			{
				EndTurn(Battle, 0, Report);
				const int Serial = Battle.Units[1].Serial;
				FTickReport Next;
				for (int i = 0; i < 20000 && Battle.Units[1].Serial == Serial; ++i)
				{
					for (const FUnit& Each : Battle.Units)
					{
						if (Each.bReady && Each.Id != 1)
						{
							EndTurn(Battle, Each.Id, Next);
						}
					}
					Battle.Tick(Next);
				}
				Check(Count(Next, EEventKind::Reaction, 1, "shock") == 1, "Static Mire should shock a Wet unit as its turn starts");
				Check(!Battle.Units[1].bReady, "a shock as the turn starts should take that turn");
				TurnOf(Battle, 1, Next);
				Check(Battle.Units[1].bReady, "the turn after a shock should be its own again");
			}
		}
		{
			FBattle Battle;
			Deal(Battle, { { "cryomancer", 0, FVec2(8.25f, 12.25f) }, { "knight", 1, FVec2(13.25f, 12.25f) } });
			FTickReport Report;
			TurnOf(Battle, 0, Report);
			Battle.AddStatus(Battle.Units[1], "wet", 3);
			if (Use(Battle, 0, SlotOf("cryomancer", "cryomancer_frost_patch"), Battle.Units[1].Pos, Report))
			{
				EndTurn(Battle, 0, Report);
				FTickReport Next;
				const int Serial = Battle.Units[1].Serial;
				for (int i = 0; i < 20000 && Battle.Units[1].Serial == Serial; ++i)
				{
					for (const FUnit& Each : Battle.Units)
					{
						if (Each.bReady && Each.Id != 1)
						{
							EndTurn(Battle, Each.Id, Next);
						}
					}
					Battle.Tick(Next);
				}
				Check(Count(Next, EEventKind::Reaction, 1, "freeze") == 1, "Frost Patch should freeze a Wet unit");
				Check(!Battle.Units[1].bReady && !Battle.Units[1].HasStatus("freeze"),
					"a freeze as the turn starts should take that turn and be spent on it");
			}
		}
		{
			FBattle Battle;
			Deal(Battle, { { "snare_hunter", 0, FVec2(8.25f, 12.25f) }, { "flame_sorcerer", 0, FVec2(8.25f, 15.25f) },
				{ "sea_witch", 0, FVec2(8.25f, 9.25f) }, { "knight", 1, FVec2(20.25f, 12.25f) } });
			FTickReport Report;
			TurnOf(Battle, 0, Report);
			if (Use(Battle, 0, SlotOf("snare_hunter", "snare_hunter_tar_slick"), FVec2(13.25f, 12.25f), Report))
			{
				EndTurn(Battle, 0, Report);
				TurnOf(Battle, 1, Report);
				Check(!Battle.Zones.empty() && !Battle.Zones[0].bIgnited, "Tar Slick should lie unlit");
				FTickReport Fire;
				if (Use(Battle, 1, SlotOf("flame_sorcerer", "flame_sorcerer_ember_field"), FVec2(12.25f, 12.75f), Fire))
				{
					Check(Count(Fire, EEventKind::ZoneIgnited) == 1 && Battle.Zones.size() == 2 && Battle.Zones[0].bIgnited,
						"Ember Field laid over Tar Slick should light it");
					EndTurn(Battle, 1, Fire);
					TurnOf(Battle, 2, Fire);
					FTickReport Water;
					if (Use(Battle, 2, SlotOf("sea_witch", "sea_witch_tide_pool"), FVec2(12.25f, 12.75f), Water))
					{
						Check(Count(Water, EEventKind::ZoneEnded) == 2 && Battle.Zones.size() == 1,
							"Tide Pool should put out the Ember Field and the burning tar it reaches");
					}
				}
			}
		}
		if (Failures == Before)
		{
			std::printf("Static Mire shocks and Frost Patch freezes the Wet as their turn starts, taking only that turn; fire lights tar, water puts burning ground out\n");
		}
	}

	// Sight: Scout Flare thrown into the fog sees there and finds a unit in tall
	// grass; Smoke Veil hides its own side; a fallen caster's zones go.
	{
		const int Before = Failures;
		FBattle Battle;
		Deal(Battle, { { "sun_archer", 0, FVec2(4.25f, 12.25f) }, { "shadow_stalker", 1, FVec2(15.25f, 12.25f) },
			{ "knight", 1, FVec2(15.25f, 14.25f) } });
		Battle.Units[0].Buffs.push_back({ EStat::Sight, -9, 9 });  // short-sighted, so the flare has fog to light
		Battle.Map.Grass.assign(static_cast<size_t>(Battle.Map.TilesX) * Battle.Map.TilesY, 0);
		const FVec2 Hide = Battle.Units[2].Pos;
		Battle.Map.Grass[static_cast<size_t>(std::floor(Hide.Y / Ground::TileSize)) * Battle.Map.TilesX
			+ static_cast<size_t>(std::floor(Hide.X / Ground::TileSize))] = 1;
		FTickReport Report;
		TurnOf(Battle, 1, Report);
		if (Use(Battle, 1, SlotOf("shadow_stalker", "shadow_stalker_smoke_veil"), Battle.Units[1].Pos, Report))
		{
			Check(Battle.Hidden(0, Battle.Units[1]), "Smoke Veil should hide its own side in it");
			EndTurn(Battle, 1, Report);
		}
		TurnOf(Battle, 0, Report);
		Check(!Battle.CanSee(0, Battle.Units[2].Pos), "the archer should not see so far before the flare");
		Check(Battle.Hidden(0, Battle.Units[2]), "the knight should hide in the grass before the flare");
		if (Use(Battle, 0, SlotOf("sun_archer", "sun_archer_scout_flare"), FVec2(15.25f, 13.25f), Report))
		{
			Check(Battle.CanSee(0, Battle.Units[2].Pos), "Scout Flare should see round where it lands");
			Check(!Battle.Hidden(0, Battle.Units[2]) && !Battle.Hidden(0, Battle.Units[1]),
				"Scout Flare should find units in grass and smoke");
			Check(Battle.CanSeeUnit(0, Battle.Units[2]), "a unit the flare finds should be seen");
		}
		// The archer falls: its flare goes as the next turn begins.
		Battle.Units[0].Hp = 0;
		Battle.KnockOut(Battle.Units[0], Report);
		FTickReport Next;
		TurnOf(Battle, 2, Next);
		bool bFlare = false;
		for (const FBattle::FZone& Zone : Battle.Zones)
		{
			bFlare = bFlare || Zone.AbilityId == "sun_archer_scout_flare";
		}
		Check(!bFlare, "a fallen caster's zones should go");
		if (Failures == Before)
		{
			std::printf("Scout Flare thrown into the fog sees there and finds what hides in grass and smoke; Smoke Veil hides; a fallen caster's zones go\n");
		}
	}

	// Battles: the computer plays the twelve classes on every map, both sides.
	{
		const int Before = Failures;
		std::vector<FMapDef> Maps;
		for (const auto& Path : FilesIn(MapFolder, ".tmmap.json"))
		{
			FMapDef Def;
			if (ReadMapFile(ReadAll(Path), Def).empty())
			{
				Maps.push_back(Def);
			}
		}
		std::map<std::string, int> Laid;
		int Touches = 0;
		int Played = 0;
		for (int Round = 0; Round < 12 && !Maps.empty(); ++Round)
		{
			const FMapDef& Map = Maps[static_cast<size_t>(Round) % Maps.size()];
			const uint64_t Seed = 700 + static_cast<uint64_t>(Round);
			auto Build = [&](FBattle& Battle)
			{
				Battle.Map.BuildMirrored(Map.Top, Map.Grass);
				const FVec2 Size = Battle.Map.SizeMeters();
				Battle.SpawnPoints[0] = Map.Spawns[0];
				Battle.SpawnPoints[1] = FVec2(Size.X - Map.Spawns[0].X, Size.Y - Map.Spawns[0].Y);
				for (int Index = 0; Index < 8; ++Index)
				{
					FUnit Unit;
					Unit.Id = Index;
					Unit.Team = Index < 4 ? 0 : 1;
					Unit.Job = Twelve[static_cast<size_t>((Round * 5 + Index * 7) % 12)].first;
					const FVec2 Spot = Map.Spawns[static_cast<size_t>(Index % 4)];
					Unit.Pos = Index < 4 ? Spot : FVec2(Size.X - Spot.X, Size.Y - Spot.Y);
					Battle.Units.push_back(Unit);
				}
				Battle.Tuning = GameTuning();
				Battle.Tuning.Elements = Round % 2;
				Battle.Start(Seed);
			};
			FBattle Battle;
			Build(Battle);
			FAIPlayer Computers[2] = { FAIPlayer("hard"), FAIPlayer(Round % 3 == 0 ? "medium" : "hard") };
			Computers[0].Rng.Seed(Seed);
			Computers[1].Rng.Seed(Seed + 1);
			std::vector<std::pair<int, FOrder>> Orders;
			int Refusals = 0;
			while (Battle.TickCount < 6000 && Battle.Winner < 0)
			{
				const FUnit* Unit = nullptr;
				for (const FUnit& Each : Battle.Units)
				{
					if (Each.IsAlive() && Each.bReady)
					{
						Unit = &Each;
						break;
					}
				}
				FTickReport Report;
				if (!Unit)
				{
					Battle.Advance(1, Report);
				}
				else
				{
					FOrder Order = Computers[Unit->Team == 1 ? 1 : 0].NextCommand(Battle, *Unit);
					if (!Battle.Validate(Order).empty())
					{
						++Refusals;
						Order = FOrder::MakeEndTurn(Unit->Id, Unit->Serial);
					}
					Orders.emplace_back(Battle.TickCount, Order);
					Battle.Apply(Order, Report);
				}
				for (const FEvent& Event : Report.Events)
				{
					if (Event.Kind == EEventKind::ZoneLaid)
					{
						++Laid[Event.Id];
					}
					Touches += (Event.Kind == EEventKind::Hit && Event.Id == "zone") ? 1 : 0;
				}
			}
			++Played;
			if (Refusals > 0)
			{
				Fail(Map.Id + ": the computer gave " + std::to_string(Refusals) + " orders the rules refused");
			}
			FBattle Replay;
			Build(Replay);
			size_t Next = 0;
			while (Next < Orders.size() || (Replay.TickCount < Battle.TickCount && Replay.Winner < 0))
			{
				FTickReport Report;
				if (Next < Orders.size() && Orders[Next].first == Replay.TickCount)
				{
					if (!Replay.Validate(Orders[Next].second).empty())
					{
						Fail(Map.Id + ": the replay refused an order the battle took");
						break;
					}
					Replay.Apply(Orders[Next].second, Report);
					++Next;
					continue;
				}
				if (Replay.Winner >= 0)
				{
					break;
				}
				Replay.Advance(1, Report);
			}
			if (Replay.Checksum() != Battle.Checksum())
			{
				Fail(Map.Id + ": a battle with zones did not replay to the same checksum");
			}
		}
		std::string Seen;
		for (const auto& Pair : Laid)
		{
			Seen += " " + Pair.first + "=" + std::to_string(Pair.second);
		}
		if (Laid.size() < 9)
		{
			Fail("the computer laid only " + std::to_string(Laid.size()) + " of the 12 zones:" + Seen);
		}
		if (Failures == Before)
		{
			std::printf("the computer played %d battles with the twelve: every order legal, %d kinds of zone laid, %d zone hits; each replayed to the same checksum\n ",
				Played, static_cast<int>(Laid.size()), Touches);
			std::printf("%s\n", Seen.c_str());
		}
	}

	std::printf("\n%s\n", Failures == 0 ? "GROUND ZONES DO WHAT THE DESIGN SAYS" : "GROUND ZONES ARE WRONG");
	return Failures == 0 ? 0 : 1;
}
