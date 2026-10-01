// Items: the files, what carrying one does, and what a battle with them does.
//
// Not Godot's (Docs/design/feat-neutral-camps.md, feat-items.md), so there is no
// Godot dump to measure against. What this holds the rules to instead:
//
//   - every item file in the catalog reads, and broken ones are refused with the
//     reason, as class files are;
//   - the worked numbers in the design come out exactly: damage and healing
//     raised flat-then-percent, the Turn Gauge filling faster, the caps;
//   - max HP, stats, floors and climbing include what a unit carries;
//   - a loadout is checked against the side's points, duplicates and what can't
//     be bought;
//   - the checksum sees what a unit carries;
//   - the computer picks items within its points, and a battle with items plays
//     legally and replays from its orders to the same checksum.
//
// Run with the items and maps folders: SimItemTest <Content/Data/Items> <Content/Data/Maps>.

#include "SimAI.h"
#include "SimBattle.h"
#include "SimItem.h"

#include <algorithm>
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
		if (Failures < 25)
		{
			std::printf("  %s\n", What.c_str());
		}
		++Failures;
	}

	void Expect(int Got, int Want, const std::string& What)
	{
		if (Got != Want)
		{
			Fail(What + ": got " + std::to_string(Got) + ", want " + std::to_string(Want));
		}
	}

	std::string ReadAll(const std::filesystem::path& Path)
	{
		std::ifstream In(Path, std::ios::binary);
		std::stringstream Buffer;
		Buffer << In.rdbuf();
		return Buffer.str();
	}

	std::vector<std::filesystem::path> FilesIn(const char* Folder, const std::string& Ending)
	{
		std::vector<std::filesystem::path> Paths;
		if (!Folder || !std::filesystem::exists(Folder))
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

	/** A test item with just these numbers, registered under a name of its own. */
	const FItemDef* TestItem(const std::string& Id, int DamagePercent, int DamageFlat = 0, int TgPercent = 0)
	{
		if (const FItemDef* Known = FindItem(Id))
		{
			return Known;
		}
		FItemDef Item;
		Item.Id = Id;
		Item.Name = Id;
		Item.Cost = 0;  // not for sale, so the computer's picks never meet it
		Item.DamagePercent = DamagePercent;
		Item.DamageFlat = DamageFlat;
		Item.TgPercent = TgPercent;
		RegisterItem(Item);
		return FindItem(Id);
	}

	/** Two sides of four on a map, as the director deals them, with these items on blue's first three. */
	void Deal(FBattle& Battle, const FMapDef& Map, uint64_t Seed, bool bItems)
	{
		const char* Roster[8] =
		{
			"knight", "archer", "black_mage", "white_mage",
			"knight", "archer", "black_mage", "white_mage"
		};
		Battle.Map.BuildMirrored(Map.Top);
		const FVec2 Size = Battle.Map.SizeMeters();
		Battle.SpawnPoints[0] = Map.Spawns[0];
		Battle.SpawnPoints[1] = FVec2(Size.X - Map.Spawns[0].X, Size.Y - Map.Spawns[0].Y);
		for (int Index = 0; Index < 8; ++Index)
		{
			FUnit Unit;
			Unit.Id = Index;
			Unit.Team = Index < 4 ? 0 : 1;
			Unit.Job = Roster[Index];
			const FVec2 Spot = Map.Spawns[static_cast<size_t>(Index % 4)];
			Unit.Pos = Index < 4 ? Spot : FVec2(Size.X - Spot.X, Size.Y - Spot.Y);
			if (bItems)
			{
				// Each side as the computer would kit it out with 6 points.
				const std::vector<std::string> Picks = SuggestLoadout(Unit.Job, Index % 4 == 0 ? 3 : 1);
				for (size_t i = 0; i < Picks.size() && i < 3; ++i)
				{
					Unit.Gear[i] = FindItem(Picks[i]);
				}
			}
			Battle.Units.push_back(Unit);
		}
		Battle.Tuning.ItemBudget = bItems ? 6.0 : 0.0;
		Battle.Start(Seed);
	}

	const FUnit* WaitingOn(const FBattle& Battle)
	{
		for (const FUnit& Unit : Battle.Units)
		{
			if (Unit.IsAlive() && Unit.bReady)
			{
				return &Unit;
			}
		}
		return nullptr;
	}
}

int main(int ArgCount, char** Args)
{
	const char* ItemFolder = ArgCount >= 2 ? Args[1] : nullptr;
	const char* MapFolder = ArgCount >= 3 ? Args[2] : nullptr;

	// Every file in the catalog reads.
	{
		const int Before = Failures;
		int Read = 0;
		int ByTier[4] = { 0, 0, 0, 0 };
		for (const auto& Path : FilesIn(ItemFolder, ".tmitem.json"))
		{
			const std::string Problems = LoadItemFile(ReadAll(Path));
			if (!Problems.empty())
			{
				Fail(Path.filename().string() + " was refused: " + Problems);
				continue;
			}
			++Read;
		}
		for (const FItemDef* Item : AllItems())
		{
			++ByTier[static_cast<int>(Item->Tier)];
		}
		if (Read < 20)
		{
			Fail("expected the catalog of at least 20 item files, read " + std::to_string(Read));
		}
		for (int t = 0; t < 4; ++t)
		{
			if (ByTier[t] == 0)
			{
				Fail(std::string("no ") + ItemTierName(static_cast<EItemTier>(t)) + " items in the catalog");
			}
		}
		if (FindItem("dragonheart") && FindItem("dragonheart")->Cost != 0)
		{
			Fail("an epic item should not be for sale");
		}
		if (Failures == Before)
		{
			std::printf("read %d item files: %d common, %d uncommon, %d rare, %d epic\n", Read, ByTier[0], ByTier[1], ByTier[2], ByTier[3]);
		}
	}

	// Broken files are refused, each with its reason.
	{
		const int Before = Failures;
		const std::string Head = "{\"format\": \"tactical-masters-item\", \"version\": 1, \"id\": \"broken\", \"name\": \"Broken\", ";
		const std::vector<std::pair<std::string, std::string>> Broken =
		{
			{ "not JSON", "{" },
			{ "wrong format", "{\"format\": \"tactical-masters-class\", \"version\": 1, \"id\": \"x\", \"name\": \"X\", \"tier\": \"common\"}" },
			{ "unknown key", Head + "\"tier\": \"common\", \"colour\": \"red\"}" },
			{ "no tier", Head + "\"stats\": {\"hp\": 5}}" },
			{ "bad tier", Head + "\"tier\": \"legendary\"}" },
			{ "unknown stat", Head + "\"tier\": \"common\", \"stats\": {\"luck\": 5}}" },
			{ "speed too high", Head + "\"tier\": \"rare\", \"stats\": {\"speed\": 3}}" },
			{ "fractional stat", Head + "\"tier\": \"common\", \"stats\": {\"hp\": 1.5}}" },
			{ "unknown bonus", Head + "\"tier\": \"common\", \"bonus\": {\"lifesteal\": 5}}" },
			{ "percent too high", Head + "\"tier\": \"rare\", \"bonus\": {\"damage_percent\": 80}}" },
			{ "bad scale", Head + "\"tier\": \"rare\", \"bonus\": {\"scale\": \"fire\"}}" },
			{ "bad id", "{\"format\": \"tactical-masters-item\", \"version\": 1, \"id\": \"Bad Id\", \"name\": \"X\", \"tier\": \"common\"}" },
		};
		int Refused = 0;
		for (const auto& Case : Broken)
		{
			FItemDef Item;
			if (ReadItemFile(Case.second, Item).empty())
			{
				Fail("a broken item file (" + Case.first + ") was read");
			}
			else
			{
				++Refused;
			}
		}
		if (!FilesIn(ItemFolder, ".tmitem.json").empty() && LoadItemFile(ReadAll(FilesIn(ItemFolder, ".tmitem.json").front())).empty())
		{
			Fail("the same item loaded twice should be refused");
		}
		if (Failures == Before)
		{
			std::printf("broken item files refused: %d of %d, each with a reason\n", Refused, static_cast<int>(Broken.size()));
		}
	}

	// The worked numbers (feat-neutral-camps.md 5.2): a damage ability of power
	// 34 into AttDef 4, level ground, from the front.
	{
		const int Before = Failures;
		FBattle Battle;
		Battle.Map.BuildMirrored(FindMap("highlands").Top);
		FAbility Rod;
		Rod.Effect = EEffect::Damage;
		Rod.Scale = EScale::Att;
		Rod.Power = 34.0f;
		static FJobStats TargetStats;
		TargetStats.Set(EStat::Hp, 100);
		TargetStats.Set(EStat::AttDef, 4);
		static FJobStats UserStats;
		UserStats.Set(EStat::Hp, 80);
		UserStats.Set(EStat::Speed, 10);
		FUnit Target;
		Target.Id = 1;
		Target.Stats = &TargetStats;
		Target.Hp = 100;
		Target.Pos = FVec2(1.0f, 0.0f);
		Target.Facing = FVec2(-1.0f, 0.0f);
		FUnit User;
		User.Id = 0;
		User.Stats = &UserStats;
		User.Pos = FVec2(0.0f, 0.0f);
		User.Facing = FVec2(1.0f, 0.0f);
		auto Hit = [&]() { return Battle.CalcAmount(User, Rod, User.Pos, Target, Target.Pos, 1, 1); };
		const std::pair<std::vector<const FItemDef*>, int> Table[] =
		{
			{ {}, 15 },
			{ { TestItem("t_pct10", 10) }, 17 },
			{ { TestItem("t_pct15", 15) }, 18 },
			{ { TestItem("t_pct10", 10), TestItem("t_pct15", 15) }, 20 },
			{ { TestItem("t_pct35", 35) }, 21 },
			{ { TestItem("t_pct35", 35), TestItem("t_pct35b", 35) }, 25 },  // 70 held to the cap of 60
			{ { TestItem("t_flat4", 0, 4) }, 17 },                        // feat-items 5.2: +4 flat
		};
		for (const auto& Row : Table)
		{
			for (int i = 0; i < 3; ++i)
			{
				User.Gear[i] = i < static_cast<int>(Row.first.size()) ? Row.first[static_cast<size_t>(i)] : nullptr;
			}
			Expect(Hit(), Row.second, "damage with " + std::to_string(Row.first.size()) + " items");
		}
		// A magic item doesn't raise a weapon's hit, and an "any" one does.
		FItemDef Magic;
		Magic.Id = "t_mag20";
		Magic.Name = "t_mag20";
		Magic.DamagePercent = 20;
		Magic.Scale = EItemScale::Mag;
		RegisterItem(Magic);
		User.Gear[0] = FindItem("t_mag20");
		User.Gear[1] = User.Gear[2] = nullptr;
		Expect(Hit(), 15, "a magic item on a weapon's hit");

		// Healing: power 30 heals 45 today; +15% (Censer of Mercy) 52.
		FAbility Mend;
		Mend.Effect = EEffect::Heal;
		Mend.Scale = EScale::Mag;
		Mend.Power = 30.0f;
		Target.Hp = 1;
		User.Gear[0] = nullptr;
		Expect(Battle.CalcAmount(User, Mend, User.Pos, Target, Target.Pos, 1, 1), 45, "healing with no items");
		User.Gear[0] = FindItem("censer_of_mercy");
		if (User.Gear[0])
		{
			Expect(Battle.CalcAmount(User, Mend, User.Pos, Target, Target.Pos, 1, 1), 52, "healing with the Censer of Mercy");
		}

		// The Turn Gauge at Speed 10: 20 a tick; +8% 22, +15% 23, the 30% cap 26.
		User.Gear[0] = nullptr;
		Expect(Battle.TgGain(User), 20, "gauge with no items");
		User.Gear[0] = TestItem("t_tg8", 0, 0, 8);
		Expect(Battle.TgGain(User), 22, "gauge +8%");
		User.Gear[0] = TestItem("t_tg15", 0, 0, 15);
		Expect(Battle.TgGain(User), 23, "gauge +15%");
		User.Gear[1] = TestItem("t_tg15b", 0, 0, 15);
		User.Gear[2] = TestItem("t_tg8", 0, 0, 8);
		Expect(Battle.TgGain(User), 26, "gauge held to +30%");
		if (Failures == Before)
		{
			std::printf("the design's numbers: damage 15, 17, 18, 20, 21, 25 (the cap), healing 45 and 52, gauge 20, 22, 23, 26 (the cap)\n");
		}
	}

	// Stats, max HP, floors, caps and climbing, on a unit in a battle.
	{
		const int Before = Failures;
		FBattle Battle;
		Battle.Map.BuildMirrored(FindMap("highlands").Top);
		FUnit Knight;
		Knight.Id = 0;
		Knight.Job = "knight";
		Knight.Pos = FVec2(2.75f, 4.75f);
		Knight.Gear[0] = FindItem("chain_vest");
		Knight.Gear[1] = FindItem("bulwark_plate");
		Knight.Gear[2] = FindItem("spring_greaves");
		Battle.Units.push_back(Knight);
		FUnit Mage;
		Mage.Id = 1;
		Mage.Team = 1;
		Mage.Job = "black_mage";
		Mage.Pos = FVec2(10.25f, 10.25f);
		Battle.Units.push_back(Mage);
		Battle.Start(1);
		const FUnit& K = Battle.Units[0];
		const FJobDef* KnightJob = FindJob("knight");
		if (!KnightJob || !K.Gear[0] || !K.Gear[1] || !K.Gear[2])
		{
			Fail("the knight's test items are missing");
		}
		else
		{
			const int Base = KnightJob->Stats.Get(EStat::Hp);
			Expect(K.MaxHp(), Base + 20 + 45, "max HP with Chain Vest and Bulwark Plate");
			Expect(K.Hp, K.MaxHp(), "a unit starts the battle at its max HP, items included");
			Expect(K.Stat(EStat::AttDef), KnightJob->Stats.Get(EStat::AttDef) + 2 + 4, "AttDef with both");
			Expect(K.Stat(EStat::Move), KnightJob->Stats.Get(EStat::Move) - 1, "Move with Bulwark Plate");
			Expect(K.ItemJump(), 1, "climbing one more level with Spring Greaves");
			Expect(Battle.Units[1].ItemJump(), 0, "climbing with nothing");
		}
		// Caps: three +4 crit items give +12, a fourth's worth would pass 20.
		FUnit Lucky = Battle.Units[1];
		Lucky.Gear[0] = FindItem("lucky_coin");
		Lucky.Gear[1] = FindItem("hunters_eye");
		Lucky.Gear[2] = FindItem("executioners_edge");
		Expect(Lucky.ItemStat(EStat::Crit), 14, "crit from three items");
		FItemDef Big;
		Big.Id = "t_crit15";
		Big.Name = "t_crit15";
		Big.Stats[static_cast<int>(EStat::Crit)] = 15;
		RegisterItem(Big);
		Lucky.Gear[0] = FindItem("t_crit15");
		Expect(Lucky.ItemStat(EStat::Crit), Items::ChanceCap, "crit from items held to the cap");
		// Floor: a Move of 1 with a -1 item stays 1.
		static FJobStats Slow;
		Slow.Set(EStat::Hp, 50);
		Slow.Set(EStat::Move, 1);
		Slow.Set(EStat::Speed, 5);
		Slow.Set(EStat::Sight, 5);
		FUnit Snail;
		Snail.Stats = &Slow;
		Snail.Gear[0] = FindItem("bulwark_plate");
		Expect(Snail.Stat(EStat::Move), 1, "Move held to its floor");
		if (Failures == Before)
		{
			std::printf("items add to max HP (and a unit starts full), stats, climbing; held to their caps and floors\n");
		}
	}

	// Loadouts: within the side's points, no item twice, nothing unbuyable.
	{
		const int Before = Failures;
		FBattle Battle;
		Battle.Tuning.ItemBudget = 6.0;
		FUnit A;
		A.Id = 0;
		A.Team = 0;
		A.Job = "knight";
		FUnit B = A;
		B.Id = 1;
		B.Team = 1;
		Battle.Units = { A, B };
		if (!Battle.LoadoutProblem().empty())
		{
			Fail("no items at all should be fine");
		}
		Battle.Units[0].Gear[0] = FindItem("quicksilver_charm");  // 3
		Battle.Units[0].Gear[1] = FindItem("chain_vest");         // 2
		Battle.Units[0].Gear[2] = FindItem("iron_charm");         // 1
		if (!Battle.LoadoutProblem().empty())
		{
			Fail("6 points of items with 6 to spend should be fine: " + Battle.LoadoutProblem());
		}
		Battle.Units[1].Gear[0] = FindItem("bulwark_plate");      // red: 3, fine
		Battle.Units[0].Gear[2] = FindItem("trail_boots");        // blue: 7
		if (Battle.LoadoutProblem().find("Blue") == std::string::npos)
		{
			Fail("7 points with 6 to spend should be refused, naming the side");
		}
		Battle.Units[0].Gear[2] = Battle.Units[0].Gear[1];
		if (Battle.LoadoutProblem().find("two") == std::string::npos)
		{
			Fail("the same item twice on one unit should be refused");
		}
		Battle.Units[0].Gear[1] = nullptr;
		Battle.Units[0].Gear[2] = FindItem("dragonheart");
		if (Battle.LoadoutProblem().find("found") == std::string::npos)
		{
			Fail("an epic item should not be buyable");
		}
		Battle.Units[0].Gear[2] = nullptr;
		Battle.Tuning.ItemBudget = 0.0;
		if (Battle.LoadoutProblem().empty())
		{
			Fail("any item with no points to spend should be refused");
		}
		if (Failures == Before)
		{
			std::printf("loadouts: held to the side's points, no item twice on a unit, epic items only found\n");
		}
	}

	// The computer's picks: within its points, never twice, and a knight armours up.
	{
		const int Before = Failures;
		for (const FJobDef* Job : AllJobs())
		{
			for (int Points = 0; Points <= 8; ++Points)
			{
				const std::vector<std::string> Picks = SuggestLoadout(Job->Id, Points);
				int Cost = 0;
				for (size_t i = 0; i < Picks.size(); ++i)
				{
					const FItemDef* Item = FindItem(Picks[i]);
					if (!Item)
					{
						Fail(Job->Id + ": picked an item that doesn't exist");
						continue;
					}
					Cost += Item->Cost;
					for (size_t j = 0; j < i; ++j)
					{
						if (Picks[j] == Picks[i])
						{
							Fail(Job->Id + ": picked the same item twice");
						}
					}
				}
				if (Cost > Points || Picks.size() > 3)
				{
					Fail(Job->Id + ": picked " + std::to_string(Cost) + " points of items with " + std::to_string(Points));
				}
			}
		}
		const std::vector<std::string> KnightPicks = SuggestLoadout("knight", 3);
		const FItemDef* First = KnightPicks.empty() ? nullptr : FindItem(KnightPicks[0]);
		if (!First || (First->Stats[static_cast<int>(EStat::Hp)] <= 0 && First->Stats[static_cast<int>(EStat::AttDef)] <= 0
			&& First->Stats[static_cast<int>(EStat::Speed)] <= 0))
		{
			Fail("a knight with 3 points should take armour or speed first");
		}
		if (Failures == Before)
		{
			std::printf("the computer's picks: %d classes, 0 to 8 points each, always within its points, never twice\n",
				static_cast<int>(AllJobs().size()));
		}
	}

	// The checksum sees what a unit carries.
	{
		const int Before = Failures;
		FBattle A;
		A.Map.BuildMirrored(FindMap("highlands").Top);
		FUnit U;
		U.Id = 0;
		U.Job = "archer";
		U.Pos = FVec2(2.75f, 4.75f);
		A.Units.push_back(U);
		A.Start(3);
		FBattle B = A;
		if (A.Checksum() != B.Checksum())
		{
			Fail("two copies of a battle should have the same checksum");
		}
		B.Units[0].Gear[1] = FindItem("lucky_coin");
		if (A.Checksum() == B.Checksum())
		{
			Fail("the checksum should notice an item");
		}
		FBattle C = A;
		C.Units[0].Gear[1] = FindItem("scouts_feather");
		if (B.Checksum() == C.Checksum())
		{
			Fail("the checksum should notice which item");
		}
		if (Failures == Before)
		{
			std::printf("the checksum notices an item, and which one\n");
		}
	}

	// A battle with items: the computer against itself, every order legal, and
	// the battle replays from its orders to the same checksum.
	{
		const int Before = Failures;
		std::vector<FMapDef> Maps = { FindMap("highlands") };
		for (const auto& Path : FilesIn(MapFolder, ".tmmap.json"))
		{
			FMapDef Def;
			if (ReadMapFile(ReadAll(Path), Def).empty())
			{
				Maps.push_back(Def);
			}
		}
		int Played = 0;
		int Wins[2] = { 0, 0 };
		for (const FMapDef& Map : Maps)
		{
			for (uint64_t Seed = 1; Seed <= 2; ++Seed)
			{
				FBattle Battle;
				Deal(Battle, Map, Seed, true);
				if (!Battle.LoadoutProblem().empty())
				{
					Fail(Map.Id + ": the dealt loadout was refused: " + Battle.LoadoutProblem());
				}
				FAIPlayer Computers[2] = { FAIPlayer("hard"), FAIPlayer("hard") };
				Computers[0].Rng.Seed(Seed);
				Computers[1].Rng.Seed(Seed + 1);
				std::vector<std::pair<int, FOrder>> Orders;
				int Refusals = 0;
				while (Battle.TickCount < 4000 && Battle.Winner < 0)
				{
					const FUnit* Unit = WaitingOn(Battle);
					if (!Unit)
					{
						FTickReport Report;
						Battle.Advance(1, Report);
						continue;
					}
					FOrder Order = Computers[Unit->Team].NextCommand(Battle, *Unit);
					if (!Battle.Validate(Order).empty())
					{
						++Refusals;
						Order = FOrder::MakeEndTurn(Unit->Id, Unit->Serial);
					}
					Orders.emplace_back(Battle.TickCount, Order);
					FTickReport Report;
					Battle.Apply(Order, Report);
				}
				++Played;
				if (Battle.Winner == 0 || Battle.Winner == 1)
				{
					++Wins[Battle.Winner];
				}
				if (Refusals > 0)
				{
					Fail(Map.Id + ": the computer gave " + std::to_string(Refusals) + " orders the rules refused");
				}
				FBattle Replay;
				Deal(Replay, Map, Seed, true);
				size_t Next = 0;
				while (Next < Orders.size() || (Replay.TickCount < Battle.TickCount && Replay.Winner < 0))
				{
					if (Next < Orders.size() && Orders[Next].first == Replay.TickCount)
					{
						FTickReport Report;
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
					FTickReport Report;
					Replay.Advance(1, Report);
				}
				if (Replay.Checksum() != Battle.Checksum())
				{
					Fail(Map.Id + ": a battle with items did not replay to the same checksum");
				}
			}
		}
		if (Failures == Before)
		{
			std::printf("the computer played %d battles with items: every order legal, blue won %d, red %d, each replayed to the same checksum\n",
				Played, Wins[0], Wins[1]);
		}
	}

	std::printf("\n%s\n", Failures == 0 ? "ITEMS DO WHAT THE DESIGN SAYS" : "ITEMS ARE WRONG");
	return Failures == 0 ? 0 : 1;
}
