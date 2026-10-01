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

	// Clearing, loot, Take and Drop, and dropping on death.
	{
		const int Before = Failures;
		FArena A;
		A.Setup(*Big, "skink_den");
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
			Archer.Gear[0] = FindItem("iron_charm");
			Archer.Gear[1] = FindItem("lucky_coin");
			Archer.Gear[2] = FindItem("worry_beads");
			if (A.Battle.ValidateTake(Archer.Id, 0, Id, -1) != "Its slots are full: say which item to leave." && Id != "iron_charm"
				&& Id != "lucky_coin" && Id != "worry_beads")
			{
				Fail("with full slots and none named, a take should be refused");
			}
			const FOrder Take = FOrder::MakeTake(Archer.Id, Archer.Serial, 0, Id, 1);
			if (Id != "iron_charm" && Id != "lucky_coin" && Id != "worry_beads")
			{
				if (!A.Battle.Validate(Take).empty())
				{
					Fail("a take naming a slot to empty should be allowed: " + A.Battle.Validate(Take));
				}
				A.Battle.Apply(Take, A.Report);
				if (Archer.Gear[1]->Id != Id || A.Battle.Caches[0].Items.size() != 1 || A.Battle.Caches[0].Items[0]->Id != "lucky_coin"
					|| !Archer.bActed)
				{
					Fail("taking into a full slot should leave the old item in the cache, and use the action");
				}
				if (A.Battle.ValidateTake(Archer.Id, 0, "lucky_coin", 1) != "Already used its action this turn.")
				{
					Fail("a second take in a turn should be refused");
				}
			}
			// Drop is free; a Health item's health goes with it.
			const int Hp = Archer.Hp;
			const int Max = Archer.MaxHp();
			const FOrder Drop = FOrder::MakeDrop(Archer.Id, Archer.Serial, 0);
			if (!A.Battle.Validate(Drop).empty())
			{
				Fail("dropping should be free: " + A.Battle.Validate(Drop));
			}
			A.Battle.Apply(Drop, A.Report);
			if (Archer.Gear[0] || Archer.MaxHp() != Max - 12 || Archer.Hp > Archer.MaxHp() || Hp < Archer.Hp)
			{
				Fail("dropping the Iron Charm should take its 12 health off the most, never adding health");
			}
			// Finished off, a unit leaves what it carried where it fell.
			const size_t Lying = A.Battle.Caches.size();
			Archer.Hp = 0;
			A.Battle.KnockOut(Archer, A.Report);
			for (int Tick = 0; Tick < 200 && Archer.IsKo(); ++Tick)
			{
				A.Battle.Advance(1, A.Report);
			}
			bool bDropped = false;
			for (const FCache& Each : A.Battle.Caches)
			{
				bDropped = bDropped || (Each.Pos.DistanceTo(Archer.Pos) < 0.5f
					&& std::find_if(Each.Items.begin(), Each.Items.end(), [](const FItemDef* I) { return I->Id == "worry_beads"; }) != Each.Items.end());
			}
			if (!bDropped || Archer.HasItems() || A.Battle.Caches.size() < Lying)
			{
				Fail("a unit finished off should drop everything it carried where it fell");
			}
		}
		// Walking onto items picks them up: free, into empty slots, best tier first.
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
				const FCache& Left = W.Battle.Caches[0];
				if (!Walker.Gear[1] || !Walker.Gear[2] || Taken != 2 || Left.Items.size() != 2 || Walker.bActed)
				{
					Fail("walking onto items should fill the empty slots, free, leaving the rest");
				}
				else if (Walker.Gear[1]->Tier < Left.Items[0]->Tier || Walker.Gear[2]->Tier < Left.Items[1]->Tier
					|| Walker.Gear[1]->Id == "iron_charm" || Walker.Gear[2]->Id == "iron_charm")
				{
					Fail("walking onto items should take the best tiers first, and never a second of one it carries");
				}
				else if (!W.Battle.Validate(FOrder::MakeTake(Walker.Id, Walker.Serial, 0, Left.Items[0]->Id == "iron_charm" ? Left.Items[1]->Id : Left.Items[0]->Id, 0)).empty())
				{
					Fail("after walking onto items a swap should still be a Take");
				}
			}
		}
		// Take and Drop as text.
		for (const FOrder& Order : { FOrder::MakeTake(3, 9, 2, "blink_stone", -1), FOrder::MakeTake(3, 9, 0, "iron_charm", 2), FOrder::MakeDrop(5, 1, 2) })
		{
			FOrder Back;
			if (!OrderFromText(OrderToText(Order), Back).empty() || Back.Type != Order.Type || Back.Cache != Order.Cache
				|| Back.ItemId != Order.ItemId || Back.GearSlot != Order.GearSlot || Back.UnitId != Order.UnitId)
			{
				Fail("a take or drop should survive being written as text: " + OrderToText(Order));
			}
		}
		for (const char* Bad : { "take 3 9 2", "take 3 9 2 Blink 0", "take 3 9 2 blink_stone 3", "drop 3 9", "drop 3 9 -1" })
		{
			FOrder Ignored;
			if (OrderFromText(Bad, Ignored).empty())
			{
				Fail(std::string("a malformed take or drop should be refused: ") + Bad);
			}
		}
		if (Failures == Before)
		{
			std::printf("a cleared camp leaves loot of its tier and waits to wake; take refused when too far, not there, full without a slot, or a second time; drop is free; health follows the item; the finished-off drop what they carry; take and drop survive the trip as text\n");
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
			std::printf("the computer and the monsters played %d battles with every camp: every order legal, %d monsters set off, %d fell, %d camps cleared, %d items taken, %d boss phases, %d runners escaped; each replayed to the same checksum\n",
				Played, Total.Alerts, Total.MonstersFell, Total.Cleared, Total.Taken, Total.Phases, Total.Escapes);
		}
	}

	std::printf("\n%s\n", Failures == 0 ? "CAMPS WAKE, FIGHT, FALL AND PAY OUT AS THE DESIGN SAYS" : "CAMPS ARE WRONG");
	return Failures == 0 ? 0 : 1;
}
