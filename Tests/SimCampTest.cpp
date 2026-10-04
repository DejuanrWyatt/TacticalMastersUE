// Neutral camps: monsters, their moods, their loot, the bosses, and the items
// that came with them.
//
// Not Godot's (Docs/design/feat-neutral-camps.md and the bestiary), so there
// is no Godot dump to measure against. What this holds the rules to:
//
//   - a battle that asks for no camps has none, makes no monsters, and its dice
//     are untouched by asking for them;
//   - camps stand where the design says: mirrored pairs in their bands, apart,
//     the boss in the middle; the same seed puts them in the same places; the
//     map's own boss, or a random one when the setting says so;
//   - each temperament is set off by what the design says, and only that, with
//     a turn's warning; monsters give up and go home, mend, and never fight
//     each other;
//   - a cleared camp leaves loot of its tier and comes back later somewhere
//     new; Take and Drop are refused for every reason the design gives; a unit
//     finished off drops what it carried; the Treasure Runner escapes;
//   - bosses change phase as their health falls, summon their adds, shrug off
//     stuns, and stagger;
//   - noise from fights wakes a waiting camp early, at the loudest side, and a
//     monster killed whole in one blow pays its killer gauge; a stagger breaks a
//     boss's wind-up; with the setup options on, a boss hunts whoever hurt it
//     most and loses the scent, and its last blow claims it (2026-10-02);
//   - the new items do what they say, and Tamer's Collar tames;
//   - the computer and the monsters' own player play whole battles with every
//     camp and boss, every order legal, and each battle replays from its orders
//     to the same checksum.
//
// Run with the items and maps folders: SimCampTest <Content/Data/Items> <Content/Data/Maps>.
// The monsters are read from Content/Data/Monsters beside them.

#include "SimAI.h"
#include "SimBattle.h"
#include "SimClassFile.h"
#include "SimOrderText.h"

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
		if (Failures < 30)
		{
			std::printf("  %s\n", What.c_str());
		}
		++Failures;
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
		std::vector<std::filesystem::path> Out;
		if (!std::filesystem::exists(Folder))
		{
			return Out;
		}
		for (const auto& Entry : std::filesystem::directory_iterator(Folder))
		{
			const std::string Name = Entry.path().filename().string();
			if (Name.size() > Ending.size() && Name.substr(Name.size() - Ending.size()) == Ending)
			{
				Out.push_back(Entry.path());
			}
		}
		std::sort(Out.begin(), Out.end());
		return Out;
	}

	void Deal(FBattle& Battle, const FMapDef& Map, uint64_t Seed, const std::vector<std::string>& Roster = {})
	{
		const char* Default[8] = { "knight", "archer", "black_mage", "white_mage", "knight", "archer", "black_mage", "white_mage" };
		Battle.Map.BuildMirrored(Map.Top);
		const FVec2 Size = Battle.Map.SizeMeters();
		Battle.SpawnPoints[0] = Map.Spawns[0];
		Battle.SpawnPoints[1] = FVec2(Size.X - Map.Spawns[0].X, Size.Y - Map.Spawns[0].Y);
		for (int Index = 0; Index < 8; ++Index)
		{
			FUnit Unit;
			Unit.Id = Index;
			Unit.Team = Index < 4 ? 0 : 1;
			Unit.Job = Index < static_cast<int>(Roster.size()) ? Roster[static_cast<size_t>(Index)] : Default[Index];
			const FVec2 Spot = Map.Spawns[static_cast<size_t>(Index % 4)];
			Unit.Pos = Index < 4 ? Spot : FVec2(Size.X - Spot.X, Size.Y - Spot.Y);
			Battle.Units.push_back(Unit);
		}
		Battle.Start(Seed);
	}

	/** A camp of this kind, woken now, and its first member. */
	int FindCampOfKind(const FBattle& Battle, const std::string& Kind)
	{
		for (int i = 0; i < static_cast<int>(Battle.Camps.size()); ++i)
		{
			const FCamp& Camp_ = Battle.Camps[static_cast<size_t>(i)];
			if (Camp_.Kind >= 0 && CampKinds()[static_cast<size_t>(Camp_.Kind)].Id == Kind)
			{
				return i;
			}
		}
		return -1;
	}

	/** A small test board: a battle whose camps are replaced by one of our choosing. */
	struct FArena
	{
		FBattle Battle;
		FTickReport Report;

		/** Two sides and one camp of this kind (or boss) at a spot, woken. */
		int Setup(const FMapDef& Map, const std::string& KindId, const std::string& Boss = std::string())
		{
			Battle.Tuning.CampLevel = 0.0;
			Deal(Battle, Map, 11);
			// Hand-built camp: exactly one, where we say.
			const FVec2 Size = Battle.Map.SizeMeters();
			FVec2 Spot = FMap::Snap(FVec2(Size.X * 0.5f, Size.Y * 0.5f));
			for (int Try = 0; Try < 200 && !Battle.Map.NodeWalkable(FMap::NodeOf(Spot)); ++Try)
			{
				Spot = FMap::Snap(FVec2(Spot.X + 0.5f, Spot.Y));
			}
			FCamp Made;
			Made.Spot = Spot;
			Made.State = ECampState::Waiting;
			Made.Timer = 1;
			std::vector<std::string> Members;
			std::vector<std::string> Reserves;
			if (!Boss.empty())
			{
				Made.Tier = 3;
				Members = { Boss };
				if (Boss == "helix_prime")
				{
					Reserves = { "camp_scrapper", "camp_scrapper", "camp_scrapper" };
				}
			}
			else
			{
				for (int k = 0; k < static_cast<int>(CampKinds().size()); ++k)
				{
					if (CampKinds()[static_cast<size_t>(k)].Id == KindId)
					{
						Made.Kind = k;
						Made.Tier = CampKinds()[static_cast<size_t>(k)].Tier;
						Made.bShrine = CampKinds()[static_cast<size_t>(k)].bShrine;
						Members = CampKinds()[static_cast<size_t>(k)].Members;
					}
				}
			}
			int NextId = 100;
			auto Make = [&](const std::string& Job)
			{
				FUnit Monster;
				Monster.Id = NextId++;
				Monster.Team = 2;
				Monster.Job = Job;
				Monster.Stats = &FindJob(Job)->Stats;
				Monster.bMonster = true;
				Monster.bOffBoard = true;
				Monster.Camp = 0;
				Battle.Units.push_back(Monster);
				return Monster.Id;
			};
			for (const std::string& Job : Members)
			{
				Made.Members.push_back(Make(Job));
			}
			for (const std::string& Job : Reserves)
			{
				Made.Reserves.push_back(Make(Job));
			}
			Battle.Camps.push_back(Made);
			Battle.Advance(2, Report);
			return 0;
		}

		FUnit& Unit(int Id) { return *Battle.FindUnit(Id); }
		FUnit& Member(int i) { return Unit(Battle.Camps[0].Members[static_cast<size_t>(i)]); }

		/** Moves a side's unit to a spot, off everyone else. */
		void Put(FUnit& Who, const FVec2& Where)
		{
			FVec2 Spot = FMap::Snap(Where);
			for (int Try = 0; Try < 40; ++Try)
			{
				const FUnit* There = Battle.UnitNear(Spot, Ground::UnitSpacing);
				if (Battle.Map.NodeWalkable(FMap::NodeOf(Spot)) && (!There || There->Id == Who.Id))
				{
					break;
				}
				Spot = FMap::Snap(FVec2(Spot.X + 0.5f, Spot.Y + (Try % 2 == 0 ? 0.0f : 0.5f)));
			}
			Who.Pos = Spot;
		}

		/** Makes this unit's turn begin now, as the clock would. */
		void TurnFor(FUnit& Who)
		{
			for (FUnit& Each : Battle.Units)
			{
				if (Each.bReady)
				{
					Battle.Apply(FOrder::MakeEndTurn(Each.Id, Each.Serial), Report);
				}
				if (&Each != &Who && Each.IsAlive())
				{
					Each.Tg = 0;
				}
			}
			Who.Tg = Pace::TgMax - 1;
			for (int Tick = 0; Tick < 3 && !Who.bReady; ++Tick)
			{
				Battle.Advance(1, Report);
			}
		}

		/** The side's units off to their spawn, out of everyone's way. */
		void Clear()
		{
			for (FUnit& Each : Battle.Units)
			{
				if (!Each.bMonster)
				{
					Each.Pos = Each.Team == 0 ? Battle.SpawnPoints[0] : Battle.SpawnPoints[1];
				}
			}
			// Spread them so none stand on each other.
			int n = 0;
			for (FUnit& Each : Battle.Units)
			{
				if (!Each.bMonster)
				{
					Put(Each, FVec2(Each.Pos.X + static_cast<float>(n % 4), Each.Pos.Y));
					++n;
				}
			}
		}

		bool Said(EEventKind Kind, int Unit_ = -1) const
		{
			for (const FEvent& Event : Report.Events)
			{
				if (Event.Kind == Kind && (Unit_ < 0 || Event.Unit == Unit_))
				{
					return true;
				}
			}
			return false;
		}
	};

	/** Plays a battle out with the computer on both sides and the monsters' own player; the orders, for a replay. */
	struct FPlayed
	{
		std::vector<std::pair<int, FOrder>> Orders;
		int Refusals = 0;
		int Cleared = 0;
		int Taken = 0;
		int MonstersFell = 0;
		int Alerts = 0;
		int Escapes = 0;
		int Phases = 0;
		std::string FirstRefusal;
	};

	FPlayed Play(FBattle& Battle, uint64_t Seed, int Ticks)
	{
		FPlayed Out;
		FAIPlayer Computers[2] = { FAIPlayer("hard"), FAIPlayer("hard") };
		Computers[0].Rng.Seed(Seed);
		Computers[1].Rng.Seed(Seed + 1);
		FNeutralPlayer Monsters;
		int Guard = 0;
		while (Battle.TickCount < Ticks && Battle.Winner < 0 && ++Guard < 200000)
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
			if (!Unit)
			{
				FTickReport Report;
				Battle.Advance(1, Report);
				for (const FEvent& Event : Report.Events)
				{
					Out.Cleared += Event.Kind == EEventKind::CampCleared ? 1 : 0;
					Out.Alerts += Event.Kind == EEventKind::MonsterAlert ? 1 : 0;
					if (Event.Kind == EEventKind::Gone)
					{
						const FUnit* Gone = Battle.FindUnit(Event.Unit);
						Out.MonstersFell += Gone && Gone->bMonster ? 1 : 0;
					}
				}
				continue;
			}
			FOrder Order = Unit->Team == 2 ? Monsters.NextCommand(Battle, *Unit) : Computers[Unit->Team].NextCommand(Battle, *Unit);
			const std::string Why = Battle.Validate(Order);
			if (!Why.empty())
			{
				if (Out.FirstRefusal.empty())
				{
					Out.FirstRefusal = Unit->Job + ": " + OrderToText(Order) + ": " + Why;
				}
				++Out.Refusals;
				Order = FOrder::MakeEndTurn(Unit->Id, Unit->Serial);
			}
			// Through text, as a match sends it.
			FOrder Sent;
			if (!OrderFromText(OrderToText(Order), Sent).empty())
			{
				Fail("an order did not survive being written as text: " + OrderToText(Order));
			}
			Out.Orders.emplace_back(Battle.TickCount, Sent);
			FTickReport Report;
			Battle.Apply(Sent, Report);
			for (const FEvent& Event : Report.Events)
			{
				Out.Cleared += Event.Kind == EEventKind::CampCleared ? 1 : 0;
				Out.Taken += Event.Kind == EEventKind::ItemTaken ? 1 : 0;
				Out.Alerts += Event.Kind == EEventKind::MonsterAlert ? 1 : 0;
				Out.Escapes += Event.Kind == EEventKind::Escaped ? 1 : 0;
				Out.Phases += Event.Kind == EEventKind::PhaseChanged ? 1 : 0;
				if (Event.Kind == EEventKind::Gone)
				{
					const FUnit* Gone = Battle.FindUnit(Event.Unit);
					Out.MonstersFell += Gone && Gone->bMonster ? 1 : 0;
				}
			}
		}
		return Out;
	}

	bool Replays(const FBattle& Original, const FPlayed& Played, FBattle& Replay)
	{
		size_t Next = 0;
		while (Next < Played.Orders.size() || (Replay.TickCount < Original.TickCount && Replay.Winner < 0))
		{
			if (Next < Played.Orders.size() && Played.Orders[Next].first == Replay.TickCount)
			{
				FTickReport Report;
				if (!Replay.Validate(Played.Orders[Next].second).empty())
				{
					return false;
				}
				Replay.Apply(Played.Orders[Next].second, Report);
				++Next;
				continue;
			}
			if (Replay.Winner >= 0)
			{
				break;
			}
			FTickReport Report;
			Replay.Advance(1, Report);
		}
		return Replay.Checksum() == Original.Checksum();
	}
}

int main(int ArgCount, char** Args)
{
	const std::filesystem::path ItemFolder = ArgCount >= 2 ? Args[1] : "Content/Data/Items";
	const std::filesystem::path MapFolder = ArgCount >= 3 ? Args[2] : "Content/Data/Maps";
	const std::filesystem::path MonsterFolder = ItemFolder.parent_path() / "Monsters";

	// Every monster and item file reads.
	{
		const int Before = Failures;
		int Monsters = 0;
		int Bosses = 0;
		for (const auto& Path : FilesIn(MonsterFolder, ".tmclass.json"))
		{
			const std::string Problems = LoadClassFile(ReadAll(Path));
			if (!Problems.empty())
			{
				Fail(Path.filename().string() + " was refused: " + Problems);
				continue;
			}
			++Monsters;
		}
		for (const FJobDef* Job : AllMonsters())
		{
			Bosses += Job->Monster.bMonster && Job->Monster.Tier == 3 ? 1 : 0;
		}
		for (const FJobDef* Job : AllJobs())
		{
			if (Job->Monster.bMonster)
			{
				Fail("a monster should not be listed among the classes a side can field: " + Job->Id);
			}
		}
		int Items_ = 0;
		int WithAbility = 0;
		for (const auto& Path : FilesIn(ItemFolder, ".tmitem.json"))
		{
			const std::string Problems = LoadItemFile(ReadAll(Path));
			if (!Problems.empty())
			{
				Fail(Path.filename().string() + " was refused: " + Problems);
				continue;
			}
			++Items_;
		}
		for (const FItemDef* Item : AllItems())
		{
			WithAbility += !Item->AbilityId.empty() && FindAbility(Item->AbilityId) ? 1 : 0;
		}
		if (Monsters < 18 || Bosses != 3)
		{
			Fail("expected 15 monsters and 3 bosses, read " + std::to_string(Monsters) + " with " + std::to_string(Bosses) + " bosses");
		}
		for (const FCampKind& Kind : CampKinds())
		{
			for (const std::string& Job : Kind.Members)
			{
				if (!FindJob(Job) || !FindJob(Job)->Monster.bMonster)
				{
					Fail(Kind.Id + ": no monster called " + Job);
				}
			}
		}
		// A broken monster file is refused with its reasons.
		FJobDef Job;
		std::vector<FAbility> Abilities;
		const std::string Good = ReadAll(MonsterFolder / "tar_skink.tmclass.json");
		auto Broken = [&](const std::string& From, const std::string& To)
		{
			std::string Text = Good;
			const size_t At = Text.find(From);
			if (At == std::string::npos)
			{
				Fail("test fault: no " + From);
				return;
			}
			Text.replace(At, From.size(), To);
			if (ReadClassFile(Text, Job, Abilities).empty())
			{
				Fail("a monster file with " + To + " should be refused");
			}
		};
		Broken("\"territorial\"", "\"grumpy\"");
		Broken("\"easy\"", "\"legendary\"");
		Broken("\"ambush\"", "\"invisible\"");
		Broken("\"ring\": 3", "\"ring\": 30");
		if (Failures == Before)
		{
			std::printf("read %d monster files (%d bosses) and %d item files, %d with an ability; broken monster files refused\n",
				Monsters, Bosses, Items_, WithAbility);
		}
	}

	std::vector<FMapDef> Maps;
	for (const auto& Path : FilesIn(MapFolder, ".tmmap.json"))
	{
		FMapDef Def;
		if (ReadMapFile(ReadAll(Path), Def).empty())
		{
			Maps.push_back(Def);
		}
	}
	const FMapDef* Big = nullptr;
	for (const FMapDef& Map : Maps)
	{
		Big = Map.Id == "verdant_crossing" ? &Map : Big;
	}
	if (!Big)
	{
		std::printf("no verdant_crossing map\n");
		return 1;
	}

	// No camps unless asked for, and the dice untouched by asking.
	{
		const int Before = Failures;
		FBattle Plain;
		Deal(Plain, *Big, 777);
		FBattle Camped;
		Camped.Tuning.CampLevel = 3.0;
		Deal(Camped, *Big, 777);
		if (!Plain.Camps.empty() || Plain.Units.size() != 8)
		{
			Fail("a battle that asks for no camps should have none, and no monsters");
		}
		if (Camped.Rng.GetState() != Plain.Rng.GetState())
		{
			Fail("placing camps should not touch the battle's dice");
		}
		FTickReport A;
		FTickReport B;
		Plain.Advance(30, A);
		Camped.Advance(30, B);
		for (int i = 0; i < 8; ++i)
		{
			if (Plain.Units[static_cast<size_t>(i)].Tg != Camped.Units[static_cast<size_t>(i)].Tg)
			{
				Fail("camps should not change a side's clock");
				break;
			}
		}
		if (Failures == Before)
		{
			std::printf("no camps unless asked for, and the battle's dice and clock are the same either way\n");
		}
	}

	// Where camps stand.
	{
		const int Before = Failures;
		int Checked = 0;
		for (const FMapDef& Map : Maps)
		{
			const FVec2 Size = [&] { FMap M; M.BuildMirrored(Map.Top); return M.SizeMeters(); }();
			if (Size.X < 60.0f)
			{
				continue;  // the small maps have little room; their camps are checked by playing
			}
			for (int Level = 1; Level <= 3; ++Level)
			{
				for (uint64_t Seed = 1; Seed <= 6; ++Seed)
				{
					FBattle Battle;
					Battle.Tuning.CampLevel = Level;
					Battle.BossJob = "helix_prime";
					Deal(Battle, Map, Seed);
					FBattle Again;
					Again.Tuning.CampLevel = Level;
					Again.BossJob = "helix_prime";
					Deal(Again, Map, Seed);
					++Checked;
					if (Again.Camps.size() != Battle.Camps.size())
					{
						Fail(Map.Id + ": the same seed should give the same camps");
						continue;
					}
					int Boss = 0;
					for (size_t i = 0; i < Battle.Camps.size(); ++i)
					{
						const FCamp& Camp_ = Battle.Camps[i];
						if (!(Camp_.Spot == Again.Camps[i].Spot) || Camp_.Kind != Again.Camps[i].Kind)
						{
							Fail(Map.Id + ": the same seed should put camps in the same places");
						}
						if (Camp_.Tier == 3)
						{
							++Boss;
							const FUnit* Monster = Battle.FindUnit(Camp_.Members[0]);
							if (!Monster || Monster->Job != "helix_prime" || Camp_.Reserves.size() != 3)
							{
								Fail(Map.Id + ": the map's boss should hold the boss camp, with its adds in reserve");
							}
							if (Camp_.Spot.DistanceTo(FVec2(Size.X * 0.5f, Size.Y * 0.5f)) > 4.0f)
							{
								Fail(Map.Id + ": the boss should stand at the middle");
							}
							continue;
						}
						// Its twin, turned about.
						const FCamp& Twin = Battle.Camps[i % 2 == 0 ? i + 1 : i - 1];
						if (Twin.Spot.DistanceTo(FVec2(Size.X - Camp_.Spot.X, Size.Y - Camp_.Spot.Y)) > 0.01f || Twin.Kind != Camp_.Kind)
						{
							Fail(Map.Id + ": camps should come in mirrored pairs of one kind");
						}
						for (size_t j = 0; j < Battle.Camps.size(); ++j)
						{
							if (j != i && Battle.Camps[j].Spot.DistanceTo(Camp_.Spot) < Camp::Apart)
							{
								Fail(Map.Id + ": two camps closer than 8 m");
							}
						}
						for (const int Id : Camp_.Members)
						{
							const FUnit* Monster = Battle.FindUnit(Id);
							if (!Monster || !Monster->bMonster || !Monster->bOffBoard || Monster->IsAlive() || Monster->Team != 2)
							{
								Fail(Map.Id + ": a camp's monsters should wait off the board, on team 2");
							}
						}
					}
					const int Expected = Level == 1 ? 4 : Level == 2 ? 9 : 13;
					if (static_cast<int>(Battle.Camps.size()) != Expected || Boss != (Level >= 2 ? 1 : 0))
					{
						Fail(Map.Id + ": level " + std::to_string(Level) + " should give " + std::to_string(Expected) + " camps, gave "
							+ std::to_string(Battle.Camps.size()));
					}
				}
			}
		}
		// A random boss when asked: over some seeds, more than one of the three.
		std::map<std::string, int> Drawn;
		for (uint64_t Seed = 1; Seed <= 12; ++Seed)
		{
			FBattle Battle;
			Battle.Tuning.CampLevel = 2;
			Battle.Tuning.RandomBoss = 1;
			Battle.BossJob = "helix_prime";
			Deal(Battle, *Big, Seed);
			for (const FCamp& Camp_ : Battle.Camps)
			{
				if (Camp_.Tier == 3)
				{
					++Drawn[Battle.FindUnit(Camp_.Members[0])->Job];
				}
			}
		}
		if (Drawn.size() < 2)
		{
			Fail("the random boss setting should draw different bosses over different battles");
		}
		if (Failures == Before)
		{
			std::printf("%d layouts on the big maps: mirrored pairs of one kind, 8 m apart, the map's boss in the middle, monsters waiting off the board; the same seed, the same camps; a random boss drew %zu different bosses in 12 battles\n",
				Checked, Drawn.size());
		}
	}

	// Waking, and the warnings before.
	{
		const int Before = Failures;
		FBattle Battle;
		Battle.Tuning.CampLevel = 2;
		Battle.BossJob = "chronos";
		Deal(Battle, *Big, 3);
		int Warned = 0;
		int Woke = 0;
		int FirstMedium = -1;
		for (int Tick = 0; Tick < 42 * Pace::TicksPerSecond; ++Tick)
		{
			FTickReport Report;
			Battle.Advance(1, Report);
			// Nobody plays: every ready unit gives its turn up.
			for (FUnit& Unit : Battle.Units)
			{
				if (Unit.bReady)
				{
					FTickReport Skip;
					Battle.Apply(FOrder::MakeEndTurn(Unit.Id, Unit.Serial), Skip);
				}
			}
			for (const FEvent& Event : Report.Events)
			{
				Warned += Event.Kind == EEventKind::CampWarning ? 1 : 0;
				if (Event.Kind == EEventKind::CampAwake)
				{
					++Woke;
					if (Battle.Camps[static_cast<size_t>(Event.Slot)].Tier == 1 && FirstMedium < 0)
					{
						FirstMedium = Battle.TickCount;
					}
				}
			}
		}
		int Awake = 0;
		for (const FCamp& Camp_ : Battle.Camps)
		{
			if (Camp_.State == ECampState::Awake)
			{
				++Awake;
				for (const int Id : Camp_.Members)
				{
					const FUnit* Monster = Battle.FindUnit(Id);
					if (!Monster->IsAlive() || Monster->bOffBoard || Monster->Pos.DistanceTo(Camp_.Spot) > 4.0f)
					{
						Fail("a woken camp's monsters should stand round its spot");
					}
				}
			}
		}
		if (Awake != 6 || FirstMedium != 40 * Pace::TicksPerSecond || Warned < 2)
		{
			Fail("by 42 s the easy (0 s) and medium (40 s) camps should be awake, with warnings: " + std::to_string(Awake)
				+ " awake, medium at tick " + std::to_string(FirstMedium) + ", " + std::to_string(Warned) + " warnings");
		}
		if (Failures == Before)
		{
			std::printf("camps wake on time (easy at once, medium at 40 s, after a warning), their monsters round the spot\n");
		}
	}

	// The temperaments, one by one, on a board of our own.
	{
		const int Before = Failures;
		// Territorial: set off by stepping inside its ring, not outside it; a turn's warning first.
		{
			FArena A;
			A.Setup(*Big, "skink_den");
			A.Clear();
			FUnit& Skink = A.Member(0);
			FUnit& Knight = A.Unit(0);
			A.Put(Knight, FVec2(Skink.Home.X + 6.0f, Skink.Home.Y));
			A.TurnFor(Skink);
			if (Skink.Mind != EMind::Resting)
			{
				Fail("a territorial monster should not be set off by someone outside its ring");
			}
			A.Put(Knight, FVec2(Skink.Home.X + 2.0f, Skink.Home.Y));
			A.Report = FTickReport();
			A.TurnFor(Skink);
			if (Skink.Mind != EMind::Alert || !A.Said(EEventKind::MonsterAlert, Skink.Id) || Skink.bReady)
			{
				Fail("a territorial monster should be set off by someone inside its ring, and spend that turn warning");
			}
			// An ambusher is hidden until someone is right on it.
			A.Put(Knight, FVec2(Skink.Pos.X + 5.0f, Skink.Pos.Y));
			Skink.Mind = EMind::Resting;
			if (A.Battle.CanSeeUnit(0, Skink) && A.Battle.CanSee(0, Skink.Pos))
			{
				Fail("an ambusher lying in wait should be hidden from a side that sees its spot");
			}
			A.Put(Knight, FVec2(Skink.Pos.X + 1.0f, Skink.Pos.Y));
			if (!A.Battle.CanSeeUnit(0, Skink))
			{
				Fail("an ambusher should show to someone right on it");
			}
			Skink.Mind = EMind::Alert;
			A.TurnFor(Skink);
			if (Skink.Mind != EMind::Fighting || !Skink.bReady)
			{
				Fail("set off, a monster should fight from its next turn");
			}
			// It fights (perhaps stepping round first), then with nobody in its
			// leash it goes home and mends.
			FNeutralPlayer Player;
			bool bAttacked = false;
			for (int Step = 0; Step < 3 && Skink.bReady && !bAttacked; ++Step)
			{
				const FOrder Order = Player.NextCommand(A.Battle, Skink);
				if (!A.Battle.Validate(Order).empty())
				{
					Fail("the monster's order was refused: " + OrderToText(Order) + ": " + A.Battle.Validate(Order));
					break;
				}
				bAttacked = Order.Type == EOrderType::UseAbility;
				A.Battle.Apply(Order, A.Report);
			}
			if (!bAttacked)
			{
				Fail("a fighting monster with an enemy beside it should attack it");
			}
			if (Skink.bReady)
			{
				A.Battle.Apply(FOrder::MakeEndTurn(Skink.Id, Skink.Serial), A.Report);
			}
			A.Clear();
			Skink.Hp = 10;
			Skink.Grudge = -1;
			Skink.GrudgeTurns = 0;
			A.TurnFor(Skink);
			if (Skink.Mind != EMind::Returning)
			{
				Fail("a monster with nobody left in its leash should go home");
			}
			const FOrder Home = Player.NextCommand(A.Battle, Skink);
			A.Battle.Apply(Home, A.Report);
			A.Battle.Apply(FOrder::MakeEndTurn(Skink.Id, Skink.Serial), A.Report);
			Skink.Pos = Skink.Home;
			A.TurnFor(Skink);
			if (Skink.Mind != EMind::Resting || Skink.Hp != Skink.MaxHp())
			{
				Fail("home again, a monster should rest and mend to full");
			}
		}
		// Provoked: only a hit sets it off, and its whole pack with it. Docile runs.
		{
			FArena A;
			A.Setup(*Big, "scrapper_gang");
			A.Clear();
			FUnit& First = A.Member(0);
			FUnit& Knight = A.Unit(0);
			A.Put(Knight, FVec2(First.Pos.X + 1.0f, First.Pos.Y));
			A.TurnFor(First);
			if (First.Mind != EMind::Resting)
			{
				Fail("a provoked monster should not be set off by someone merely near");
			}
			A.Battle.Apply(FOrder::MakeEndTurn(First.Id, First.Serial), A.Report);
			A.TurnFor(Knight);
			const FOrder Hit = FOrder::MakeUseAbility(Knight.Id, Knight.Serial, 0, First.Pos, First.Id);
			const std::string Why = A.Battle.Validate(Hit);
			if (!Why.empty())
			{
				Fail("the knight should be able to hit the scrapper: " + Why);
			}
			A.Report = FTickReport();
			// Hit until it lands (it may be dodged).
			for (int Try = 0; Try < 8 && First.Mind == EMind::Resting; ++Try)
			{
				A.TurnFor(Knight);
				A.Battle.Apply(FOrder::MakeUseAbility(Knight.Id, Knight.Serial, 0, First.Pos, First.Id), A.Report);
			}
			bool bPack = true;
			for (int i = 0; i < 3; ++i)
			{
				const FUnit& Each = A.Member(i);
				bPack = bPack && (!Each.IsAlive() || Each.Mind == EMind::Alert);
			}
			if (!bPack || First.Grudge != Knight.Id)
			{
				Fail("hitting one of a provoked pack should set the whole pack off, holding it against the one who hit");
			}
		}
		{
			FArena A;
			A.Setup(*Big, "qilin_herd");
			A.Clear();
			FUnit& Qilin = A.Member(0);
			FUnit& Grazer = A.Member(1);
			FUnit& Knight = A.Unit(0);
			A.Put(Knight, FVec2(Grazer.Pos.X + 1.0f, Grazer.Pos.Y));
			// Near a ward: its guardian is set off.
			A.TurnFor(Qilin);
			if (Qilin.Mind != EMind::Alert)
			{
				Fail("a guardian should be set off by someone standing at its ward");
			}
			Qilin.Mind = EMind::Resting;
			A.Put(Knight, FVec2(Grazer.Pos.X + 8.0f, Grazer.Pos.Y));
			A.TurnFor(Knight);
			A.Put(Knight, FVec2(Grazer.Pos.X + 1.0f, Grazer.Pos.Y));
			for (int Try = 0; Try < 8 && Grazer.Mind == EMind::Resting; ++Try)
			{
				A.TurnFor(Knight);
				A.Battle.Apply(FOrder::MakeUseAbility(Knight.Id, Knight.Serial, 0, Grazer.Pos, Grazer.Id), A.Report);
			}
			if (Grazer.Mind != EMind::Fleeing || Qilin.Mind != EMind::Alert)
			{
				Fail("a hit docile grazer should run, and bring its guardian");
			}
		}
		// Aggressive: sees someone near and comes; monsters never fight each other.
		{
			FArena A;
			A.Setup(*Big, "stalker_pair");
			A.Clear();
			FUnit& Stalker = A.Member(0);
			FUnit& Other = A.Member(1);
			A.TurnFor(Stalker);
			if (Stalker.Mind != EMind::Resting)
			{
				Fail("an aggressive monster with nobody near should rest");
			}
			Stalker.Mind = EMind::Fighting;
			A.Battle.Apply(FOrder::MakeEndTurn(Stalker.Id, Stalker.Serial), A.Report);
			A.TurnFor(Stalker);
			FNeutralPlayer Player;
			const FOrder Order = Player.NextCommand(A.Battle, Stalker);
			if (Order.Type == EOrderType::UseAbility)
			{
				Fail("a monster should never attack another monster");
			}
			const std::vector<FHit> Hits = A.Battle.Preview(Stalker, 0, Stalker.Pos, Other.Pos);
			if (!Hits.empty())
			{
				Fail("a monster's attack should not reach its campmates");
			}
		}
		// Patrol: walks its route.
		{
			FArena A;
			A.Setup(*Big, "deserter_patrol");
			A.Clear();
			FUnit& Deserter = A.Member(0);
			if (A.Battle.Camps[0].Route.size() < 3)
			{
				Fail("a patrol camp should have a route");
			}
			FNeutralPlayer Player;
			A.TurnFor(Deserter);
			Deserter.RouteStep = 1;
			const FOrder Walk = Player.NextCommand(A.Battle, Deserter);
			if (Walk.Type != EOrderType::Move || !A.Battle.Validate(Walk).empty())
			{
				Fail("a resting patrol should walk its route: " + OrderToText(Walk));
			}
		}
		// Skittish: the runner flees, and escapes at the edge with its prize.
		{
			FArena A;
			A.Setup(*Big, "runner_warden");
			A.Clear();
			FUnit& Runner = A.Member(0);
			if (!Runner.Gear[0] || Runner.Gear[0]->Tier != EItemTier::Rare)
			{
				Fail("the Treasure Runner should carry a rare item");
			}
			FUnit& Knight = A.Unit(0);
			A.Put(Knight, FVec2(Runner.Pos.X + 3.0f, Runner.Pos.Y));
			A.TurnFor(Runner);
			if (Runner.Mind != EMind::Fleeing)
			{
				Fail("a skittish monster should run from anyone it sees");
			}
			A.Battle.Apply(FOrder::MakeEndTurn(Runner.Id, Runner.Serial), A.Report);
			// Onto ground at the board's edge.
			for (int Y = 0; Y < A.Battle.Map.NavY; ++Y)
			{
				const FNode Node{ 1, Y };
				if (A.Battle.Map.NodeWalkable(Node) && !A.Battle.UnitNear(FMap::NodePos(Node), Ground::UnitSpacing))
				{
					Runner.Pos = FMap::NodePos(Node);
					break;
				}
			}
			A.Report = FTickReport();
			A.TurnFor(Runner);
			if (!A.Said(EEventKind::Escaped, Runner.Id) || Runner.IsAlive() || Runner.Gear[0])
			{
				Fail("a runner at the board's edge should escape, and its prize with it");
			}
		}
		if (Failures == Before)
		{
			std::printf("each temperament is set off by what the design says (territorial ring, provoked pack, guardian's ward, aggressive sight, patrol route, skittish flight), with a turn's warning; ambushers hide; monsters go home and mend, and never fight each other; the runner escapes with its prize\n");
		}
	}

	// With respawns off (the default), a cleared camp stays cleared.
	{
		FArena Off;
		Off.Setup(*Big, "skink_den");
		Off.Clear();
		FUnit& Skink = Off.Member(0);
		FUnit& Knight = Off.Unit(0);
		Off.Put(Knight, FVec2(Skink.Pos.X + 1.0f, Skink.Pos.Y));
		Skink.Hp = 1;
		for (int Try = 0; Try < 10 && Skink.IsAlive(); ++Try)
		{
			Off.TurnFor(Knight);
			Off.Battle.Apply(FOrder::MakeUseAbility(Knight.Id, Knight.Serial, 0, Skink.Pos, Skink.Id), Off.Report);
		}
		Off.Battle.Advance(1, Off.Report);
		if (Skink.IsAlive() || Off.Battle.Camps[0].Timer < 1000000)
		{
			Fail("with camp respawns off, a cleared camp should never wake again");
		}
	}
	// Clearing, loot, Take and Drop, and dropping on death.
	{
		const int Before = Failures;
		FArena A;
		A.Setup(*Big, "skink_den");
		A.Battle.Tuning.CampRespawn = 1.0;
		A.Clear();
		FUnit& Skink = A.Member(0);
		FUnit& Knight = A.Unit(0);
		A.Put(Knight, FVec2(Skink.Pos.X + 1.0f, Skink.Pos.Y));
		A.Report = FTickReport();
		Skink.Hp = 1;
		for (int Try = 0; Try < 10 && Skink.IsAlive(); ++Try)
		{
			A.TurnFor(Knight);
			A.Battle.Apply(FOrder::MakeUseAbility(Knight.Id, Knight.Serial, 0, Skink.Pos, Skink.Id), A.Report);
		}
		A.Battle.Advance(1, A.Report);
		if (Skink.IsAlive() || !Skink.bOffBoard || A.Battle.Camps[0].State != ECampState::Waiting || A.Battle.Caches.empty())
		{
			Fail("a camp whose last monster falls should be cleared, leave a cache, and wait to wake again");
		}
		else
		{
			const FCache& Cache = A.Battle.Caches[0];
			if (Cache.Items.size() != 1 || Cache.Items[0]->Tier != EItemTier::Common)
			{
				Fail("an easy camp should leave one common item");
			}
			if (A.Battle.Camps[0].Timer != Camp::Respawn[0] * Pace::TicksPerSecond - 1 && A.Battle.Camps[0].Timer != Camp::Respawn[0] * Pace::TicksPerSecond)
			{
				Fail("a cleared camp should wake again after its respawn time");
			}
			const std::string Id = Cache.Items[0]->Id;
			A.Clear();
			FUnit& Archer = A.Unit(1);
			A.TurnFor(Archer);
			Archer.bActed = false;
			A.Put(Archer, FVec2(Cache.Pos.X + 6.0f, Cache.Pos.Y));
			if (A.Battle.ValidateTake(Archer.Id, 0, Id, -1) != "Too far: stand next to the items.")
			{
				Fail("taking from too far should be refused");
			}
			A.Put(Archer, FVec2(Cache.Pos.X + 1.0f, Cache.Pos.Y));
			if (A.Battle.ValidateTake(Archer.Id, 0, "no_such_item", -1) != "That item isn't there.")
			{
				Fail("taking an item not in the cache should be refused");
			}
			if (A.Battle.ValidateTake(Archer.Id, 7, Id, -1) != "Nothing lies there.")
			{
				Fail("taking from a cache that isn't there should be refused");
			}
			// Taking puts it in the side's stash (2026-10-01): free, whatever the unit wears.
			Archer.Gear[0] = FindItem("iron_charm");
			Archer.Gear[1] = FindItem("lucky_coin");
			Archer.Gear[2] = FindItem("worry_beads");
			const int Side = Archer.Team;
			const FOrder Take = FOrder::MakeTake(Archer.Id, Archer.Serial, 0, Id, -1);
			if (!A.Battle.Validate(Take).empty())
			{
				Fail("a take into the stash should be allowed with full slots: " + A.Battle.Validate(Take));
			}
			A.Battle.Apply(Take, A.Report);
			if (A.Battle.Stash[Side].size() != 1 || A.Battle.Stash[Side][0].Item->Id != Id || !A.Battle.Caches[0].Items.empty()
				|| Archer.bActed || Archer.Gear[1]->Id != "lucky_coin")
			{
				Fail("taking should move the item to the side's stash, free, leaving what the unit wears");
			}
			// Equipping: only into an open slot.
			if (Id != "iron_charm" && Id != "worry_beads")
			{
				if (A.Battle.ValidateEquip(Archer.Id, Id, -1) != "Its slots are full.")
				{
					Fail("equipping a unit with no open slot should be refused");
				}
				if (A.Battle.ValidateEquip(Archer.Id, Id, 1).empty())
				{
					Fail("equipping into a slot already worn should be refused");
				}
				Archer.Gear[1] = nullptr;
				const FOrder Equip = FOrder::MakeEquip(Archer.Id, Id, 1);
				if (!A.Battle.Validate(Equip).empty())
				{
					Fail("equipping from the stash into an open slot should be allowed: " + A.Battle.Validate(Equip));
				}
				A.Battle.Apply(Equip, A.Report);
				if (!Archer.Gear[1] || Archer.Gear[1]->Id != Id || !A.Battle.Stash[Side].empty())
				{
					Fail("equipping should move the item from the stash into the slot");
				}
				if (A.Battle.ValidateEquip(Archer.Id, Id, -1) != "That item isn't in the stash.")
				{
					Fail("equipping an item the stash doesn't hold should be refused");
				}
			}
			// Unequipping is the unit's whole turn; a Health item's health goes with it.
			A.Battle.Stash[Side].clear();
			A.TurnFor(Archer);
			Archer.bActed = false;
			Archer.bMoved = true;
			if (A.Battle.ValidateDrop(Archer.Id, 0).empty())
			{
				Fail("unequipping after moving should be refused: it takes the whole turn");
			}
			Archer.bMoved = false;
			const int Hp = Archer.Hp;
			const int Max = Archer.MaxHp();
			const FOrder Drop = FOrder::MakeDrop(Archer.Id, Archer.Serial, 0);
			if (!A.Battle.Validate(Drop).empty())
			{
				Fail("unequipping at the start of a turn should be allowed: " + A.Battle.Validate(Drop));
			}
			A.Battle.Apply(Drop, A.Report);
			if (Archer.Gear[0] || Archer.MaxHp() != Max - 12 || Archer.Hp > Archer.MaxHp() || Hp < Archer.Hp)
			{
				Fail("unequipping the Iron Charm should take its 12 health off the most, never adding health");
			}
			if (Archer.bReady || A.Battle.Stash[Side].size() != 1 || A.Battle.Stash[Side][0].Item->Id != "iron_charm")
			{
				Fail("unequipping should put the item in the stash and end the turn");
			}
			// Finished off, a side's unit's items go back to its side's stash.
			const size_t Lying = A.Battle.Caches.size();
			Archer.Gear[2] = FindItem("worry_beads");
			Archer.Hp = 0;
			A.Battle.KnockOut(Archer, A.Report);
			for (int Tick = 0; Tick < 200 && Archer.IsKo(); ++Tick)
			{
				A.Battle.Advance(1, A.Report);
			}
			bool bStashed = false;
			for (const FBattle::FStashed& Held : A.Battle.Stash[Side])
			{
				bStashed = bStashed || Held.Item->Id == "worry_beads";
			}
			if (!bStashed || Archer.HasItems() || A.Battle.Caches.size() != Lying)
			{
				Fail("a side's unit finished off should send what it wore to its side's stash");
			}
		}
		// Walking onto items puts all of them in the side's stash: free.
		{
			FArena W;
			W.Setup(*Big, "skink_den");
			W.Clear();
			FUnit& Walker = W.Unit(0);
			W.TurnFor(Walker);
			Walker.bActed = false;
			Walker.bMoved = false;
			Walker.Gear[0] = FindItem("iron_charm");
			Walker.Gear[1] = nullptr;
			Walker.Gear[2] = nullptr;
			FVec2 To = Walker.Pos;
			for (const std::pair<FNode, double>& Each : W.Battle.ReachableNodes(Walker))
			{
				const FVec2 Spot = FMap::NodePos(Each.first);
				To = Spot.DistanceTo(Walker.Pos) > To.DistanceTo(Walker.Pos) && Spot.DistanceTo(Walker.Pos) < 3.0f ? Spot : To;
			}
			FCache Lying;
			Lying.Pos = To;
			for (const char* Id : { "lucky_coin", "iron_charm", "worry_beads", "blink_stone" })
			{
				Lying.Items.push_back(FindItem(Id));
				Lying.Cooldowns.push_back(0);
			}
			W.Battle.Caches.clear();
			W.Battle.Caches.push_back(Lying);
			W.Battle.Stash[Walker.Team].clear();
			const FOrder Walk = FOrder::MakeMove(Walker.Id, Walker.Serial, To);
			if (To == Walker.Pos || !W.Battle.Validate(Walk).empty())
			{
				Fail("the pick-up test found no spot to walk to");
			}
			else
			{
				W.Report = FTickReport();
				W.Battle.Apply(Walk, W.Report);
				int Taken = 0;
				for (const FEvent& Event : W.Report.Events)
				{
					Taken += Event.Kind == EEventKind::ItemTaken ? 1 : 0;
				}
				if (Taken != 4 || !W.Battle.Caches[0].Items.empty() || W.Battle.Stash[Walker.Team].size() != 4 || Walker.bActed
					|| Walker.Gear[1] || Walker.Gear[2])
				{
					Fail("walking onto items should put all of them in the side's stash, free, and none on the unit");
				}
			}
		}
		// Take, Drop and Equip as text.
		for (const FOrder& Order : { FOrder::MakeTake(3, 9, 2, "blink_stone", -1), FOrder::MakeTake(3, 9, 0, "iron_charm", 2), FOrder::MakeDrop(5, 1, 2),
			FOrder::MakeEquip(4, "lucky_coin", -1), FOrder::MakeEquip(6, "iron_charm", 2) })
		{
			FOrder Back;
			if (!OrderFromText(OrderToText(Order), Back).empty() || Back.Type != Order.Type || Back.Cache != Order.Cache
				|| Back.ItemId != Order.ItemId || Back.GearSlot != Order.GearSlot || Back.UnitId != Order.UnitId)
			{
				Fail("a take or drop should survive being written as text: " + OrderToText(Order));
			}
		}
		for (const char* Bad : { "take 3 9 2", "take 3 9 2 Blink 0", "take 3 9 2 blink_stone 3", "drop 3 9", "drop 3 9 -1", "equip 4", "equip 4 Coin 0", "equip 4 lucky_coin 3" })
		{
			FOrder Ignored;
			if (OrderFromText(Bad, Ignored).empty())
			{
				Fail(std::string("a malformed take or drop should be refused: ") + Bad);
			}
		}
		if (Failures == Before)
		{
			std::printf("a cleared camp leaves loot of its tier and waits to wake; taking and walking onto items fill the side's stash, free; equipping only into an open slot; unequipping is the whole turn; health follows the item; the finished-off send what they wore to the stash; take, drop and equip survive the trip as text\n");
		}
	}

	// Bosses: phases, summons, stuns shrugged off, stagger.
	{
		const int Before = Failures;
		{
			FArena A;
			A.Setup(*Big, "", "helix_prime");
			FUnit& Boss = A.Member(0);
			const std::string First = Boss.Ability(3)->Id;
			FUnit& Knight = A.Unit(0);
			A.Put(Knight, FVec2(Boss.Pos.X + 1.0f, Boss.Pos.Y));
			Boss.Hp = static_cast<int>(Boss.MaxHp() * 0.7) - 1;
			for (int Try = 0; Try < 10 && Boss.Phase == 0; ++Try)
			{
				A.TurnFor(Knight);
				A.Battle.Apply(FOrder::MakeUseAbility(Knight.Id, Knight.Serial, 0, Boss.Pos, Boss.Id), A.Report);
			}
			if (Boss.Phase != 1 || Boss.Ability(3)->Id == First || Boss.Ability(3)->Special != "summon")
			{
				Fail("the Helix Prime below two thirds of its health should move to its second phase and its summon");
			}
			A.Battle.AddStatus(Boss, "stun", 2);
			A.Battle.AddStatus(Boss, "sleep", 2);
			if (Boss.HasStatus("stun") || Boss.HasStatus("sleep"))
			{
				Fail("a boss should shrug off stuns and sleep");
			}
			// Its summon wakes its reserves.
			A.TurnFor(Boss);
			Boss.Mind = EMind::Fighting;
			Boss.Ult = Pace::UltMax;
			const std::string Why = A.Battle.ValidateAbility(Boss.Id, 3, Boss.Pos, -1);
			if (!Why.empty())
			{
				Fail("the boss should be able to summon: " + Why);
			}
			A.Battle.UseAbility(Boss, 3, Boss.Pos, -1, A.Report);
			int Woken = 0;
			for (const int Id : A.Battle.Camps[0].Reserves)
			{
				Woken += A.Unit(Id).IsAlive() && !A.Unit(Id).bOffBoard ? 1 : 0;
			}
			if (Woken != 3)
			{
				Fail("the summon should bring the boss's three scrappers onto the board");
			}
		}
		{
			FArena A;
			A.Setup(*Big, "", "magma_colossus");
			FUnit& Boss = A.Member(0);
			FUnit& Knight = A.Unit(0);
			// Behind it.
			A.Put(Knight, FVec2(Boss.Pos.X - Boss.Facing.X * 1.2f, Boss.Pos.Y - Boss.Facing.Y * 1.2f));
			int Staggered = 0;
			for (int Try = 0; Try < 12 && Staggered == 0; ++Try)
			{
				A.TurnFor(Knight);
				A.Report = FTickReport();
				A.Battle.Apply(FOrder::MakeUseAbility(Knight.Id, Knight.Serial, 0, Boss.Pos, Boss.Id), A.Report);
				Staggered += A.Said(EEventKind::Staggered, Boss.Id) ? 1 : 0;
				Boss.Facing = (Boss.Pos - Knight.Pos).Normalized();  // it keeps its back turned
			}
			if (Staggered == 0 || !Boss.HasStatus("staggered"))
			{
				Fail("three hits from behind should stagger the Colossus");
			}
		}
		{
			FArena A;
			A.Setup(*Big, "", "chronos");
			FUnit& Boss = A.Member(0);
			Boss.Hp = Boss.MaxHp() / 2 - 1;
			FUnit& Knight = A.Unit(0);
			A.Put(Knight, FVec2(Boss.Pos.X + 1.0f, Boss.Pos.Y));
			for (int Try = 0; Try < 10 && Boss.Phase == 0; ++Try)
			{
				A.TurnFor(Knight);
				A.Battle.Apply(FOrder::MakeUseAbility(Knight.Id, Knight.Serial, 0, Boss.Pos, Boss.Id), A.Report);
			}
			if (Boss.Phase != 1 || Boss.Ability(3)->Id != "chronos_rewind")
			{
				Fail("Chronos below half its health should move to its second phase, with Rewind");
			}
		}
		if (Failures == Before)
		{
			std::printf("bosses change phase as their health falls (Helix Prime to its summon, Chronos to Rewind), the summon wakes the adds, stuns and sleep are shrugged off, and three hits from behind stagger the Colossus\n");
		}
	}

	// Camps and bosses (2026-10-02, "Camps and Bosses Mockups" A to D).
	{
		const int Before = Failures;
		const int Tps = Pace::TicksPerSecond;
		// A: noise. A waiting camp hears area blows and fire near it; full, it warns,
		// wakes, and goes for whoever was loudest.
		{
			FArena A;
			A.Setup(*Big, "stalker_pair");
			FCamp& Held = A.Battle.Camps[0];
			for (const int Id : Held.Members)
			{
				A.Unit(Id).bOffBoard = true;
				A.Unit(Id).Hp = 0;
			}
			Held.State = ECampState::Waiting;
			Held.Timer = 200 * Tps;
			Held.Wakes = 0;
			A.Clear();
			FUnit& Mage = A.Unit(2);
			int Slot = -1;
			for (int s = 0; s < ClassSlots && Slot < 0; ++s)
			{
				const FAbility* Ability = Mage.Ability(s);
				if (Ability && Ability->Effect == EEffect::Damage
					&& (Ability->Aoe > 0.0f || ShapeOf(*Ability) == "circle" || ShapeOf(*Ability) == "cone" || ShapeOf(*Ability) == "line"))
				{
					Slot = s;
				}
			}
			A.Put(Mage, FVec2(Held.Spot.X + 6.0f, Held.Spot.Y));
			if (Slot < 0)
			{
				Fail("the black mage should have an area blow to make noise with");
			}
			else
			{
				int Uses = 0;
				while (!Held.bNoiseWake && Uses < 6)
				{
					A.Report = FTickReport();
					A.Battle.UseAbility(Mage, Slot, FVec2(Held.Spot.X + 4.0f, Held.Spot.Y), -1, A.Report);
					for (int Tick = 0; Tick < 20 * Tps && Mage.IsCasting(); ++Tick)
					{
						A.Battle.Advance(1, A.Report);
					}
					++Uses;
					if (!A.Said(EEventKind::CampNoise))
					{
						Fail("an area blow 6 m from a waiting camp should make noise");
						break;
					}
				}
				if (!Held.bNoiseWake || Held.Noise != Camp::NoiseFull || Held.Timer != Camp::Warning[Held.Tier] * Tps + 1)
				{
					Fail("full of noise, a camp should give its warning now and wake when it runs out");
				}
				A.Report = FTickReport();
				A.Battle.Advance(Camp::Warning[Held.Tier] * Tps + 2, A.Report);
				if (Held.State != ECampState::Awake || !A.Said(EEventKind::CampWarning))
				{
					Fail("a camp full of noise should warn, then wake");
				}
				int Hunting = 0;
				for (const int Id : Held.Members)
				{
					Hunting += A.Unit(Id).Grudge == Mage.Id && A.Unit(Id).Mind == EMind::Alert ? 1 : 0;
				}
				if (Hunting == 0)
				{
					Fail("woken by noise, the camp should be set off at the loudest unit");
				}
				if (Held.Noise != 0 || Held.bNoiseWake)
				{
					Fail("a camp's noise should be spent when it wakes");
				}
			}
			// Far away it hears nothing; quiet, its noise fades.
			FArena B2;
			B2.Setup(*Big, "stalker_pair");
			FCamp& Far = B2.Battle.Camps[0];
			Far.State = ECampState::Waiting;
			Far.Timer = 200 * Tps;
			Far.Wakes = 0;
			for (const int Id : Far.Members)
			{
				B2.Unit(Id).bOffBoard = true;
				B2.Unit(Id).Hp = 0;
			}
			B2.Clear();
			FUnit& Mage2 = B2.Unit(2);
			if (Slot >= 0)
			{
				B2.Put(Mage2, FVec2(Far.Spot.X + 20.0f, Far.Spot.Y));
				B2.Battle.UseAbility(Mage2, Slot, FVec2(Far.Spot.X + 18.0f, Far.Spot.Y), -1, B2.Report);
				for (int Tick = 0; Tick < 20 * Tps && Mage2.IsCasting(); ++Tick)
				{
					B2.Battle.Advance(1, B2.Report);
				}
				if (Far.Noise != 0)
				{
					Fail("a blow 18 m from a camp should not reach it");
				}
				B2.Put(Mage2, FVec2(Far.Spot.X + 6.0f, Far.Spot.Y));
				B2.Battle.UseAbility(Mage2, Slot, FVec2(Far.Spot.X + 4.0f, Far.Spot.Y), -1, B2.Report);
				for (int Tick = 0; Tick < 20 * Tps && Mage2.IsCasting(); ++Tick)
				{
					B2.Battle.Advance(1, B2.Report);
				}
				const int Heard = Far.Noise;
				B2.Battle.Advance(Camp::NoiseQuietSeconds * Tps * Camp::NoiseFull + 4, B2.Report);
				if (Heard == 0 || Far.Noise != 0 || Far.State != ECampState::Waiting)
				{
					Fail("a camp's noise should fade with quiet, and not wake it");
				}
			}
		}
		// A: a monster killed whole in one blow pays its killer 20% of a gauge; one already hurt, nothing.
		{
			FArena A;
			A.Setup(*Big, "grazer_herd");
			A.Clear();
			FUnit& Grazer = A.Member(0);
			FUnit& Knight = A.Unit(0);
			static FJobStats Frail = *Grazer.Stats;
			Frail.Set(EStat::Hp, 3);
			Frail.Set(EStat::AEva, 0);
			Frail.Set(EStat::MEva, 0);
			Grazer.Stats = &Frail;
			A.Put(Knight, FVec2(Grazer.Pos.X + 1.0f, Grazer.Pos.Y));
			int Paid = -1;
			for (int Try = 0; Try < 12 && Paid < 0; ++Try)
			{
				Grazer.Hp = Grazer.MaxHp();
				A.TurnFor(Knight);
				const int Owed = Knight.KillTgPercent;
				A.Battle.Apply(FOrder::MakeUseAbility(Knight.Id, Knight.Serial, 0, Grazer.Pos, Grazer.Id), A.Report);
				if (!Grazer.IsAlive())
				{
					Paid = Knight.KillTgPercent - Owed;
				}
			}
			if (Paid != Camp::CleanKillTgPercent)
			{
				Fail("a monster killed whole in one blow should pay its killer 20% of a gauge");
			}
			FUnit& Other = A.Member(1);
			Other.Stats = &Frail;
			A.Put(Knight, FVec2(Other.Pos.X + 1.0f, Other.Pos.Y));
			Paid = -1;
			for (int Try = 0; Try < 12 && Paid < 0; ++Try)
			{
				Other.Hp = Other.MaxHp() - 1;
				A.TurnFor(Knight);
				const int Owed = Knight.KillTgPercent;
				A.Battle.Apply(FOrder::MakeUseAbility(Knight.Id, Knight.Serial, 0, Other.Pos, Other.Id), A.Report);
				if (!Other.IsAlive())
				{
					Paid = Knight.KillTgPercent - Owed;
				}
			}
			if (Paid != 0)
			{
				Fail("a monster already hurt pays nothing for the kill");
			}
		}
		// B: three hits from behind break a boss's wind-up.
		{
			FArena A;
			A.Setup(*Big, "", "chronos");
			A.Clear();
			FUnit& Boss = A.Member(0);
			FUnit& Knight = A.Unit(0);
			int Sweep = -1;
			for (int s = 0; s < ClassSlots; ++s)
			{
				Sweep = Boss.Ability(s) && Boss.Ability(s)->Cast > 0.0f ? s : Sweep;
			}
			A.Put(Knight, FVec2(Boss.Pos.X - Boss.Facing.X * 1.2f, Boss.Pos.Y - Boss.Facing.Y * 1.2f));
			A.TurnFor(Boss);
			Boss.Mind = EMind::Fighting;
			const FVec2 Ahead(Boss.Pos.X + Boss.Facing.X * 3.0f, Boss.Pos.Y + Boss.Facing.Y * 3.0f);
			A.Battle.UseAbility(Boss, Sweep, Ahead, -1, A.Report);
			if (Sweep < 0 || !Boss.IsCasting())
			{
				Fail("Chronos should wind up its Sweep");
			}
			bool bBroken = false;
			for (int Try = 0; Try < 10 && !bBroken && Boss.IsAlive(); ++Try)
			{
				Boss.Facing = (Boss.Pos - Knight.Pos).Normalized();
				A.TurnFor(Knight);
				A.Report = FTickReport();
				Boss.Facing = (Boss.Pos - Knight.Pos).Normalized();
				A.Battle.Apply(FOrder::MakeUseAbility(Knight.Id, Knight.Serial, 0, Boss.Pos, Boss.Id), A.Report);
				bBroken = A.Said(EEventKind::CastFizzled, Boss.Id);
			}
			if (!bBroken || Boss.IsCasting() || !Boss.HasStatus("staggered"))
			{
				Fail("three hits from behind should break a boss's wind-up and stagger it");
			}
		}
		// C: the hunt, a setup option.
		{
			FArena A;
			A.Battle.Tuning.BossHunt = 1.0;
			A.Setup(*Big, "", "helix_prime");
			A.Battle.Tuning.BossHunt = 1.0;
			A.Clear();
			FUnit& Boss = A.Member(0);
			FUnit& Knight = A.Unit(0);
			FUnit& Archer = A.Unit(1);
			A.Put(Knight, FVec2(Boss.Pos.X + 1.2f, Boss.Pos.Y));
			A.Put(Archer, FVec2(Boss.Pos.X - 3.0f, Boss.Pos.Y));
			Boss.Mind = EMind::Fighting;
			Boss.Wrath = { { Knight.Id, 40 }, { Archer.Id, 120 } };
			A.Report = FTickReport();
			A.TurnFor(Boss);
			if (Boss.HuntTarget != Archer.Id || !Archer.HasStatus("hunted") || !A.Said(EEventKind::Hunting, Boss.Id))
			{
				Fail("a boss should hunt whoever has hurt it most, and mark them Hunted");
			}
			// Out of its sight for three of its turns: it lets go, and turns to the next.
			A.Put(Archer, A.Battle.SpawnPoints[0]);
			for (int Turn = 0; Turn < Camp::ScentTurns; ++Turn)
			{
				A.TurnFor(Boss);
			}
			if (Boss.HuntTarget != Knight.Id || Archer.HasStatus("hunted") || !Knight.HasStatus("hunted"))
			{
				Fail("a boss out of sight of its prey for three of its turns should lose the scent and hunt the next");
			}
			// Off (the default): nobody is hunted.
			FArena Off;
			Off.Setup(*Big, "", "helix_prime");
			Off.Clear();
			FUnit& Calm = Off.Member(0);
			Calm.Mind = EMind::Fighting;
			Calm.Wrath = { { 0, 99 } };
			Off.TurnFor(Calm);
			if (Calm.HuntTarget >= 0 || Off.Unit(0).HasStatus("hunted"))
			{
				Fail("with the hunt off, a boss should hunt nobody");
			}
		}
		// D: the claim, a setup option.
		{
			for (int Pass = 0; Pass < 2; ++Pass)
			{
				FArena A;
				A.Setup(*Big, "", "chronos");
				A.Battle.Tuning.BossClaim = Pass == 0 ? 1.0 : 0.0;
				A.Clear();
				FUnit& Boss = A.Member(0);
				FUnit& Knight = A.Unit(0);
				A.Put(Knight, FVec2(Boss.Pos.X + 1.2f, Boss.Pos.Y));
				Boss.Claim[1] = Boss.MaxHp() * 2 / 5;
				const size_t RedStash = A.Battle.Stash[1].size();
				for (int Try = 0; Try < 12 && Boss.IsAlive(); ++Try)
				{
					Boss.Hp = 1;
					A.TurnFor(Knight);
					A.Report = FTickReport();
					A.Battle.Apply(FOrder::MakeUseAbility(Knight.Id, Knight.Serial, 0, Boss.Pos, Boss.Id), A.Report);
				}
				int Boons = 0;
				for (const FUnit& Each : A.Battle.Units)
				{
					Boons += Each.Team == 0 && Each.IsAlive() && Each.HasStatus("boon") ? 1 : 0;
				}
				if (Pass == 0 && (Boss.IsAlive() || Boons != 4 || !A.Said(EEventKind::BossClaimed, Boss.Id)
					|| A.Battle.Stash[1].size() != RedStash + 1 || !A.Said(EEventKind::ClaimShare, Boss.Id)))
				{
					Fail("the last blow should give its side the boon, and the side that dealt 40% a rare item in its stash");
				}
				if (Pass == 1 && (Boons != 0 || A.Battle.Stash[1].size() != RedStash))
				{
					Fail("with the claim off, a boss's last blow should give no boon and no share");
				}
			}
		}
		if (Failures == Before)
		{
			std::printf("noise wakes a waiting camp early (area blows, fire, kills within 12 m; it fades with quiet), with its warning, at the loudest; a monster killed whole in one blow pays 20%% of a gauge; three hits from behind break a boss's wind-up; with the setup options on, a boss hunts whoever hurt it most and loses the scent out of sight, and its last blow claims a boon, with a rare item for a big enough share\n");
		}
	}

	// Items that act: blink, tame, surge, lifesteal, phoenix, the situational ones.
	{
		const int Before = Failures;
		FArena A;
		A.Setup(*Big, "scrapper_gang");
		A.Clear();
		FUnit& Knight = A.Unit(0);
		FUnit& Archer = A.Unit(1);
		// Blink: to a seen spot within 4 m, free and walkable.
		Knight.Gear[0] = FindItem("blink_stone");
		A.TurnFor(Knight);
		FVec2 To = Knight.Pos;
		for (int Dy = -8; Dy <= 8 && To == Knight.Pos; ++Dy)
		{
			for (int Dx = -8; Dx <= 8; ++Dx)
			{
				const FVec2 Spot = FMap::Snap(FVec2(Knight.Pos.X + Dx * 0.5f, Knight.Pos.Y + Dy * 0.5f));
				const float Distance = Spot.DistanceTo(Knight.Pos);
				if (Distance > 2.5f && Distance < 3.8f && A.Battle.InBounds(Spot) && A.Battle.Map.NodeWalkable(FMap::NodeOf(Spot))
					&& !A.Battle.UnitNear(Spot, Ground::UnitSpacing * 1.5f) && A.Battle.CanSee(0, Spot))
				{
					To = Spot;
					break;
				}
			}
		}
		const std::string Why = A.Battle.ValidateAbility(Knight.Id, ClassSlots, To, -1);
		if (!Why.empty())
		{
			Fail("the Blink Stone's ability should be usable from slot 5: " + Why);
		}
		else
		{
			A.Battle.UseAbility(Knight, ClassSlots, To, -1, A.Report);
			if (!(Knight.Pos == To) || Knight.Cooldowns[ClassSlots] != 4)
			{
				Fail("Blink should put the unit at the aim point, and recharge");
			}
		}
		Knight.Gear[0] = nullptr;
		// Tame: a hurt scrapper fights for the side for 3 of its turns.
		FUnit& Scrapper = A.Member(0);
		Knight.Gear[1] = FindItem("tamers_collar");
		A.Put(Knight, FVec2(Scrapper.Pos.X + 1.5f, Scrapper.Pos.Y));
		A.TurnFor(Knight);
		const std::string Healthy = A.Battle.ValidateAbility(Knight.Id, ClassSlots + 1, Scrapper.Pos, Scrapper.Id);
		A.Battle.UseAbility(Knight, ClassSlots + 1, Scrapper.Pos, Scrapper.Id, A.Report);
		if (Scrapper.Team != 2)
		{
			Fail("a monster at full health should not be tamed");
		}
		A.Battle.Apply(FOrder::MakeEndTurn(Knight.Id, Knight.Serial), A.Report);
		Scrapper.Hp = 5;
		Knight.Cooldowns[ClassSlots + 1] = 0;
		A.TurnFor(Knight);
		A.Battle.UseAbility(Knight, ClassSlots + 1, Scrapper.Pos, Scrapper.Id, A.Report);
		if (Scrapper.Team != 0)
		{
			Fail("a hurt scrapper should be tamed onto the knight's side" + (Healthy.empty() ? std::string() : ": " + Healthy));
		}
		int Turns = 0;
		for (; Turns < 6 && Scrapper.Team == 0; ++Turns)
		{
			A.TurnFor(Scrapper);
			if (Scrapper.bReady)
			{
				A.Battle.Apply(FOrder::MakeEndTurn(Scrapper.Id, Scrapper.Serial), A.Report);
			}
		}
		if (Turns != 4 || Scrapper.Team != 2)
		{
			Fail("a tamed monster should serve three turns, then go wild again (served " + std::to_string(Turns - 1) + ")");
		}
		// A side can't win on a tamed monster's legs.
		Scrapper.Team = 1;
		for (FUnit& Each : A.Battle.Units)
		{
			if (Each.Team == 1 && !Each.bMonster)
			{
				Each.Hp = 0;
			}
		}
		A.Battle.CheckWinner();
		if (A.Battle.Winner != 0)
		{
			Fail("a side with only a tamed monster left should have lost");
		}
		A.Battle.Winner = -1;
		Scrapper.Team = 2;
		for (FUnit& Each : A.Battle.Units)
		{
			if (Each.Team == 1 && !Each.bMonster)
			{
				Each.Hp = Each.MaxHp();
			}
		}
		// Surge and the situational numbers, read off CalcAmount.
		const FAbility* Attack = Archer.Ability(0);
		FUnit& Target = A.Unit(4);
		const int Plain = A.Battle.CalcAmount(Archer, *Attack, Archer.Pos, Target, Target.Pos, 1, 1);
		A.Battle.AddStatus(Archer, "surge", 2);
		const int Surged = A.Battle.CalcAmount(Archer, *Attack, Archer.Pos, Target, Target.Pos, 1, 1);
		Archer.Statuses.clear();
		Archer.Gear[0] = FindItem("hunters_mark");
		const int VsUnit = A.Battle.CalcAmount(Archer, *Attack, Archer.Pos, Target, Target.Pos, 1, 1);
		const int VsMonster = A.Battle.CalcAmount(Archer, *Attack, Archer.Pos, A.Member(1), A.Member(1).Pos, 1, 1);
		const int PlainMonster = [&] { Archer.Gear[0] = nullptr; return A.Battle.CalcAmount(Archer, *Attack, Archer.Pos, A.Member(1), A.Member(1).Pos, 1, 1); }();
		Archer.Gear[0] = FindItem("high_ground_sash");
		const int Level = A.Battle.CalcAmount(Archer, *Attack, Archer.Pos, Target, Target.Pos, 1, 1);
		const int Above = A.Battle.CalcAmount(Archer, *Attack, Archer.Pos, Target, Target.Pos, 3, 1);
		Archer.Gear[0] = nullptr;
		const int PlainAbove = A.Battle.CalcAmount(Archer, *Attack, Archer.Pos, Target, Target.Pos, 3, 1);
		if (Surged <= Plain || VsUnit != Plain || VsMonster <= PlainMonster || Level != Plain || Above <= PlainAbove)
		{
			Fail("Surge, Hunter's Mark and the High Ground Sash should add only when they say: " + std::to_string(Plain) + " "
				+ std::to_string(Surged) + " " + std::to_string(VsUnit) + " " + std::to_string(VsMonster) + "/" + std::to_string(PlainMonster)
				+ " " + std::to_string(Level) + " " + std::to_string(Above) + "/" + std::to_string(PlainAbove));
		}
		// Phoenix Feather: once.
		Target.Gear[0] = FindItem("phoenix_feather");
		A.Battle.Hurt(Target, 999);
		const bool bSaved = Target.Hp == 1;
		A.Battle.Hurt(Target, 999);
		if (!bSaved || Target.Hp != 0)
		{
			Fail("the Phoenix Feather should save its holder once, and only once");
		}
		if (Failures == Before)
		{
			std::printf("item abilities work from slots 5 to 7: Blink moves and recharges, the Tamer's Collar tames only the hurt, for three turns, and a tamed monster wins nobody a battle; Surge, Hunter's Mark and the High Ground Sash add only when they say; the Phoenix Feather saves once\n");
		}
	}

	// Whole battles: the computer on both sides, the monsters' player, every camp
	// and each boss; every order legal, and every battle replays.
	{
		const int Before = Failures;
		int Played = 0;
		FPlayed Total;
		const char* BossesByMap[3] = { "helix_prime", "chronos", "magma_colossus" };
		int b = 0;
		for (const FMapDef& Map : Maps)
		{
			FMap Probe;
			Probe.BuildMirrored(Map.Top);
			if (Probe.SizeMeters().X < 60.0f)
			{
				continue;
			}
			for (uint64_t Seed = 1; Seed <= 3; ++Seed)
			{
				FBattle Battle;
				Battle.Tuning.CampLevel = 3;
				Battle.Tuning.BattleSeconds = 480;
				// The boss's hunt and claim (setup options) on in two battles of three.
				Battle.Tuning.BossHunt = Seed >= 2 ? 1.0 : 0.0;
				Battle.Tuning.BossClaim = Seed >= 2 ? 1.0 : 0.0;
				Battle.BossJob = BossesByMap[b++ % 3];
				Deal(Battle, Map, Seed);
				const FPlayed Result = Play(Battle, Seed, 4800);
				++Played;
				Total.Refusals += Result.Refusals;
				Total.Cleared += Result.Cleared;
				Total.Taken += Result.Taken;
				Total.MonstersFell += Result.MonstersFell;
				Total.Alerts += Result.Alerts;
				Total.Phases += Result.Phases;
				Total.Escapes += Result.Escapes;
				if (Result.Refusals > 0)
				{
					Fail(Map.Id + ": " + std::to_string(Result.Refusals) + " orders refused, the first: " + Result.FirstRefusal);
				}
				FBattle Replay;
				Replay.Tuning.CampLevel = 3;
				Replay.Tuning.BattleSeconds = 480;
				Replay.Tuning.BossHunt = Battle.Tuning.BossHunt;
				Replay.Tuning.BossClaim = Battle.Tuning.BossClaim;
				Replay.BossJob = Battle.BossJob;
				Deal(Replay, Map, Seed);
				if (!Replays(Battle, Result, Replay))
				{
					Fail(Map.Id + ": a battle with camps did not replay to the same checksum");
				}
			}
		}
		if (Total.Alerts == 0 || Total.MonstersFell == 0 || Total.Cleared == 0 || Total.Taken == 0)
		{
			Fail("in whole battles monsters should be set off, fall, camps be cleared and loot taken: " + std::to_string(Total.Alerts) + " alerts, "
				+ std::to_string(Total.MonstersFell) + " fell, " + std::to_string(Total.Cleared) + " cleared, " + std::to_string(Total.Taken) + " taken");
		}
		if (Failures == Before)
		{
			std::printf("the computer and the monsters played %d battles with every camp (the boss's hunt and claim on in two of three): every order legal, %d monsters set off, %d fell, %d camps cleared, %d items taken, %d boss phases, %d runners escaped; each replayed to the same checksum\n",
				Played, Total.Alerts, Total.MonstersFell, Total.Cleared, Total.Taken, Total.Phases, Total.Escapes);
		}
	}

	// Pets (2026-10-02): a summoner's summon calls up a pet, a monster file played
	// by the computer for the summoner's side, for the turns the ability says;
	// it leaves when they run out, nothing raises it, it never counts towards a
	// win, and the battle replays from its orders.
	{
		const int Before = Failures;
		const std::filesystem::path ClassFolder = ItemFolder.parent_path() / "Classes";
		for (const char* Caller : { "golem_master", "summoner", "seraph_caller" })
		{
			const std::string Problems = LoadClassFile(ReadAll(ClassFolder / (std::string(Caller) + ".tmclass.json")));
			if (!Problems.empty())
			{
				Fail(std::string(Caller) + " was refused: " + Problems);
			}
		}
		int Called = 0;
		int Left = 0;
		int PetOrders = 0;
		int Played = 0;
		for (uint64_t Seed = 1; Seed <= 4; ++Seed)
		{
			const FMapDef& Map = Maps[static_cast<size_t>(Seed) % Maps.size()];
			const std::vector<std::string> Roster = { "knight", "golem_master", "summoner", "white_mage", "knight", "archer", "seraph_caller", "white_mage" };
			FBattle Battle;
			Battle.Tuning = GameTuning();
			Battle.Tuning.BattleSeconds = 480;
			Deal(Battle, Map, Seed, Roster);
			int Pets = 0;
			for (const FUnit& Unit : Battle.Units)
			{
				if (Unit.PetOf >= 0)
				{
					++Pets;
					const FUnit* Owner = Battle.FindUnit(Unit.PetOf);
					if (!Unit.bOffBoard || Unit.IsAlive() || !Owner || Owner->Team != Unit.Team || Unit.bMonster)
					{
						Fail("a pet should wait off the board, on its caller's side, from the start");
					}
				}
			}
			if (Pets != 3)
			{
				Fail("three callers should have three pets waiting, not " + std::to_string(Pets));
			}
			// Played as Play does, counting the pets' comings, goings and orders.
			FAIPlayer Computers[2] = { FAIPlayer("hard"), FAIPlayer("hard") };
			Computers[0].Rng.Seed(Seed);
			Computers[1].Rng.Seed(Seed + 1);
			FNeutralPlayer Monsters;
			std::vector<std::pair<int, FOrder>> Orders;
			std::map<int, int> TurnsOut;
			int Guard = 0;
			while (Battle.TickCount < 30000 && Battle.Winner < 0 && ++Guard < 200000)
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
					FOrder Order = Unit->Team == 2 ? Monsters.NextCommand(Battle, *Unit) : Computers[Unit->Team].NextCommand(Battle, *Unit);
					if (!Battle.Validate(Order).empty())
					{
						Fail("the computer gave a refused order for " + Unit->Job + ": " + OrderToText(Order) + ": " + Battle.Validate(Order));
						Order = FOrder::MakeEndTurn(Unit->Id, Unit->Serial);
					}
					if (Unit->PetOf >= 0)
					{
						++PetOrders;
						if (Order.Type == EOrderType::EndTurn || Order.Type == EOrderType::Move || Order.Type == EOrderType::UseAbility)
						{
							TurnsOut[Unit->Id] += Order.Type == EOrderType::EndTurn ? 1 : 0;
						}
					}
					Orders.emplace_back(Battle.TickCount, Order);
					Battle.Apply(Order, Report);
				}
				for (const FEvent& Event : Report.Events)
				{
					const FUnit* Who = Battle.FindUnit(Event.Unit);
					if (Event.Kind == EEventKind::Teleported && Event.Id == "pet")
					{
						++Called;
						const FAbility* Summon = Battle.FindUnit(Event.By) ? Battle.FindUnit(Event.By)->Ability(1) : nullptr;
						if (!Who || !Who->IsAlive() || Who->bOffBoard || Who->PetOf != Event.By || !Summon || Who->PetTurns != Summon->PetTurns + 1)
						{
							Fail("a called pet should stand on the board, whole, with its turns to come");
						}
					}
					if (Event.Kind == EEventKind::Gone && Who && Who->PetOf >= 0)
					{
						++Left;
						if (!Who->bOffBoard || Who->IsAlive() || Who->IsKo())
						{
							Fail("a pet that leaves or falls should be off the board, with nothing to raise");
						}
					}
				}
			}
			++Played;
			// Pets never keep a side in the battle: a side down to its pets has lost.
			for (int Team = 0; Team < 2; ++Team)
			{
				bool bOwn = false;
				for (const FUnit& Unit : Battle.Units)
				{
					bOwn = bOwn || (Unit.HomeTeam() == Team && Unit.PetOf < 0 && !Unit.bMonster && Unit.IsAlive());
				}
				if (!bOwn && Battle.Winner == Team)
				{
					Fail("a side with only its pets left won");
				}
			}
			FBattle Replay;
			Replay.Tuning = Battle.Tuning;
			Deal(Replay, Map, Seed, Roster);
			FPlayed Recorded;
			Recorded.Orders = Orders;
			if (!Replays(Battle, Recorded, Replay))
			{
				Fail("a battle with pets did not replay to the same checksum");
			}
		}
		if (Called == 0 || Left == 0 || PetOrders == 0)
		{
			Fail("in whole battles pets should be called up, take orders and leave: " + std::to_string(Called) + " called, "
				+ std::to_string(PetOrders) + " orders, " + std::to_string(Left) + " gone");
		}
		if (Failures == Before)
		{
			std::printf("pets: %d battles, %d called up, %d of their orders legal, %d gone (time up or fallen); each replayed to the same checksum\n",
				Played, Called, PetOrders, Left);
		}
	}

	std::printf("\n%s\n", Failures == 0 ? "CAMPS WAKE, FIGHT, FALL AND PAY OUT AS THE DESIGN SAYS" : "CAMPS ARE WRONG");
	return Failures == 0 ? 0 : 1;
}
