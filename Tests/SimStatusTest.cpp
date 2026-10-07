// The second set of statuses (Docs/design/feat-status-effects.md): each does
// what the design says, they meet each other and the elements as it says, and
// battles where every unit carries one of the items that give them play
// legally and replay to the same checksum.
//
// Not Godot's, so there is nothing to measure against but the design. The
// Godot tests (SimTraceTest and the rest) are what hold the rules to Godot:
// none of these statuses ever appears in a battle there.
//
// Run with the items and maps folders: SimStatusTest <Content/Data/Items> <Content/Data/Maps>.
// The monsters are read from the Monsters folder beside Items.

#include "SimAI.h"
#include "SimBattle.h"
#include "SimClassFile.h"
#include "SimItem.h"
#include "SimMap.h"

#include <algorithm>
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

	/** A test-only item with one ability, written as an item file. */
	void TestItem(const std::string& Id, const std::string& AbilityJson)
	{
		const std::string Text = "{\"format\": \"tactical-masters-item\", \"version\": 1, \"id\": \"" + Id + "\", \"name\": \"" + Id
			+ "\", \"tier\": \"epic\", \"ability\": " + AbilityJson + "}";
		const std::string Problems = LoadItemFile(Text);
		if (!Problems.empty())
		{
			Fail("test item " + Id + " was refused: " + Problems);
		}
	}

	std::string Ability(const std::string& Id, const std::string& Effect, const std::string& Scale, const std::string& Target,
		const std::string& Shape, int Power, double Range, const std::string& Extra = "")
	{
		return "{\"id\": \"" + Id + "\", \"name\": \"" + Id + "\", \"kind\": \"active\", \"effect\": \"" + Effect + "\", \"scale\": \""
			+ Scale + "\", \"target\": \"" + Target + "\", \"shape\": \"" + Shape + "\", \"power\": " + std::to_string(Power)
			+ ", \"min_range\": 0, \"max_range\": " + std::to_string(Range) + ", \"aoe\": 0, \"cooldown\": 0, \"cast\": 0" + Extra + "}";
	}

	/**
	 * Units of these classes on Highlands, blue first then red, stood where
	 * given (snapped to the nearest open spot), with no dodging and no crits so
	 * every number is exact.
	 */
	struct FSetup
	{
		std::string Job;
		int Team;
		FVec2 At;
		std::string Carry;
	};

	FVec2 OpenSpot(const FBattle& Battle, const FVec2& Near, const std::vector<FVec2>& Taken)
	{
		FVec2 Best = FMap::Snap(Near);
		double BestDistance = 1e9;
		for (int y = 0; y < Battle.Map.NavY; ++y)
		{
			for (int x = 0; x < Battle.Map.NavX; ++x)
			{
				const FNode Node{ x, y };
				if (!Battle.Map.NodeWalkable(Node))
				{
					continue;
				}
				const FVec2 Spot = FMap::NodePos(Node);
				bool bFree = true;
				for (const FVec2& Other : Taken)
				{
					bFree = bFree && Other.DistanceTo(Spot) > 0.9f;
				}
				const double Distance = Spot.DistanceTo(Near);
				if (bFree && Distance < BestDistance)
				{
					BestDistance = Distance;
					Best = Spot;
				}
			}
		}
		return Best;
	}

	void Deal(FBattle& Battle, const std::vector<FSetup>& Setups)
	{
		Battle.Map.BuildMirrored(HighlandsRows());
		std::vector<FVec2> Taken;
		for (size_t i = 0; i < Setups.size(); ++i)
		{
			FUnit Unit;
			Unit.Id = static_cast<int>(i);
			Unit.Team = Setups[i].Team;
			Unit.Job = Setups[i].Job;
			Unit.Pos = OpenSpot(Battle, Setups[i].At, Taken);
			Taken.push_back(Unit.Pos);
			if (!Setups[i].Carry.empty())
			{
				Unit.Gear[0] = FindItem(Setups[i].Carry);
			}
			Battle.Units.push_back(Unit);
		}
		Battle.Tuning.EvadeMultiplier = 0.0;
		Battle.Tuning.CritChanceMultiplier = 0.0;
		Battle.Start(99);
		for (FUnit& Unit : Battle.Units)
		{
			Unit.Tg = 0;
		}
	}

	int Level(const FBattle& Battle, const FUnit& Unit)
	{
		return Battle.LevelAt(Unit.Pos);
	}

	int Amount(const FBattle& Battle, const FUnit& User, int Slot, const FUnit& Target)
	{
		return Battle.CalcAmount(User, *User.Ability(Slot), User.Pos, Target, Target.Pos, Level(Battle, User), Level(Battle, Target));
	}

	int Count(const FTickReport& Report, EEventKind Kind, const std::string& Id = "")
	{
		int N = 0;
		for (const FEvent& Event : Report.Events)
		{
			N += Event.Kind == Kind && (Id.empty() || Event.Id == Id) ? 1 : 0;
		}
		return N;
	}

	/** Runs the clock until this unit's turn comes (or a limit). */
	void UntilReady(FBattle& Battle, int Id, FTickReport& Report)
	{
		for (int i = 0; i < 4000 && !Battle.FindUnit(Id)->bReady; ++i)
		{
			Battle.Tick(Report);
		}
	}
}

int main(int ArgCount, char** Args)
{
	const std::filesystem::path ItemFolder = ArgCount >= 2 ? Args[1] : "";
	const std::filesystem::path MapFolder = ArgCount >= 3 ? Args[2] : "";
	const std::filesystem::path MonsterFolder = ItemFolder.parent_path() / "Monsters";

	int Items = 0;
	for (const auto& Path : FilesIn(ItemFolder, ".tmitem.json"))
	{
		const std::string Problems = LoadItemFile(ReadAll(Path));
		if (!Problems.empty())
		{
			Fail(Path.filename().string() + " was refused: " + Problems);
		}
		++Items;
	}
	for (const auto& Path : FilesIn(ItemFolder.parent_path() / "Classes", ".tmclass.json"))
	{
		LoadClassFile(ReadAll(Path));
	}
	for (const auto& Path : FilesIn(MonsterFolder, ".tmclass.json"))
	{
		const std::string Problems = LoadClassFile(ReadAll(Path));
		if (!Problems.empty())
		{
			Fail(Path.filename().string() + " was refused: " + Problems);
		}
	}
	// Test abilities with an element each, and a plain single-target spell and heal.
	TestItem("t_spark", Ability("t_spark", "damage", "mag", "enemy", "unit", 10, 8, ", \"element\": \"lightning\""));
	TestItem("t_frost", Ability("t_frost", "damage", "mag", "enemy", "unit", 10, 8));
	TestItem("t_fire", Ability("t_fire", "damage", "mag", "enemy", "unit", 20, 8, ", \"element\": \"fire\""));
	TestItem("t_splash", Ability("t_splash", "damage", "mag", "enemy", "unit", 5, 8, ", \"element\": \"water\""));
	TestItem("t_bolt", Ability("t_bolt", "damage", "mag", "enemy", "unit", 20, 8, ", \"element\": \"none\""));
	TestItem("t_mend", Ability("t_mend", "heal", "mag", "ally", "unit", 20, 8));
	TestItem("t_burn", Ability("t_burn", "support", "mag", "enemy", "unit", 0, 8, ", \"status\": {\"id\": \"burn\", \"turns\": 4}, \"element\": \"none\""));

	// The table: all sixteen, the harmful ones turned away by Immunity.
	{
		const int Before = Failures;
		const char* Ids[] = { "marked", "offbalance", "wet", "oiled", "chilled", "haste", "stop", "suppressed", "protect", "shell",
			"guarded", "reraise", "reflect", "charmed", "terrified", "decay" };
		const char* Harmful[] = { "marked", "offbalance", "wet", "oiled", "chilled", "stop", "suppressed", "charmed", "terrified", "decay" };
		for (const char* Id : Ids)
		{
			Check(FindStatus(Id) != nullptr, std::string("status ") + Id + " is missing");
		}
		FBattle Battle;
		Deal(Battle, { { "knight", 0, FVec2(8, 8), "" }, { "knight", 1, FVec2(12, 12), "" } });
		FUnit& Target = Battle.Units[1];
		Battle.AddStatus(Target, "immunity", 3);
		for (const char* Id : Harmful)
		{
			Battle.AddStatus(Target, Id, 2, 0, 0);
			Check(!Target.HasStatus(Id), std::string("Immunity should turn away ") + Id);
		}
		auto ElementNamed = [](const char* Id) { const FAbility* Found = FindAbility(Id); return Found ? ElementOf(*Found) : std::string("?"); };
		Check(ElementNamed("t_spark") == "lightning" && ElementNamed("t_bolt").empty() && ElementNamed("frost_brawler_frost_jab") == "ice"
			&& ElementNamed("helix_prime_flare").empty() && ElementNamed("stormcaller_thunder_bolt") == "lightning"
			&& ElementNamed("cryomancer_frost_patch") == "ice" && ElementNamed("fire") == "fire",
			"an ability's element should come from its file, else the words of its id");
		if (Failures == Before)
		{
			std::printf("read %d item files; 16 new statuses, the harmful ten turned away by Immunity; elements read from files and ids\n", Items);
		}
	}

	// Marked, Off-Balance, Protect, Shell: what a hit is worth.
	{
		const int Before = Failures;
		FBattle Battle;
		Deal(Battle, { { "archer", 0, FVec2(6, 8), "t_bolt" }, { "knight", 1, FVec2(9, 8), "" } });
		FUnit& Archer = Battle.Units[0];
		FUnit& Knight = Battle.Units[1];
		const int Plain = Amount(Battle, Archer, 0, Knight);
		const int Spell = Amount(Battle, Archer, 4, Knight);
		Battle.AddStatus(Knight, "marked", 2);
		Check(Amount(Battle, Archer, 0, Knight) > Plain, "Marked should make a hit worth more");
		FTickReport Report;
		Battle.ResolveAbility(Archer, 0, Knight.Pos, Report);
		Check(!Knight.HasStatus("marked"), "Marked should be spent by the hit that lands");
		Battle.AddStatus(Knight, "protect", 3);
		Check(Amount(Battle, Archer, 0, Knight) < Plain && Amount(Battle, Archer, 4, Knight) == Spell, "Protect should cut physical damage only");
		Battle.Units[1].Statuses.clear();
		Battle.AddStatus(Knight, "shell", 3);
		Check(Amount(Battle, Archer, 4, Knight) < Spell && Amount(Battle, Archer, 0, Knight) == Plain, "Shell should cut magic damage only");
		Knight.Statuses.clear();
		Knight.Facing = (Archer.Pos - Knight.Pos).Normalized();
		const int Front = Amount(Battle, Archer, 0, Knight);
		Battle.AddStatus(Knight, "offbalance", 2);
		Check(Amount(Battle, Archer, 0, Knight) > Front, "Off-Balance should make a hit from the front land as if from behind");
		const FVec2 Facing = Knight.Facing;
		const std::vector<std::pair<FNode, double>> Reach = Battle.ReachableNodes(Knight);
		if (!Reach.empty())
		{
			Battle.ApplyMove(Knight.Id, FMap::NodePos(Reach.back().first), false, Report);
		}
		Check(Knight.Facing == Facing, "Off-Balance should lock facing while it walks");
		if (Failures == Before)
		{
			std::printf("Marked adds and is spent on the hit; Protect and Shell cut their kind; Off-Balance lands from behind and locks facing\n");
		}
	}

	// The elements: Wet with lightning and ice, Oiled with fire, Chilled's layers, Burn on the Wet.
	{
		const int Before = Failures;
		FBattle Battle;
		Deal(Battle, { { "black_mage", 0, FVec2(4, 8), "t_spark" }, { "knight", 1, FVec2(10, 8), "" }, { "knight", 1, FVec2(11, 8), "" },
			{ "knight", 1, FVec2(18, 16), "" }, { "black_mage", 0, FVec2(4, 10), "t_frost" }, { "black_mage", 0, FVec2(4, 12), "t_fire" } });
		FUnit& A = Battle.Units[1];
		FUnit& B = Battle.Units[2];
		FUnit& Far = Battle.Units[3];
		for (FUnit* Each : { &A, &B, &Far })
		{
			Battle.AddStatus(*Each, "wet", 2);
		}
		FTickReport Report;
		Battle.ResolveAbility(Battle.Units[0], 4, A.Pos, Report);
		Check(A.HasStatus("stun") && B.HasStatus("stun") && !Far.HasStatus("stun"), "lightning should stun the Wet target and the Wet near it, not the Wet far away");
		Check(Count(Report, EEventKind::Reaction, "shock") == 2, "two shocks should be told, not " + std::to_string(Count(Report, EEventKind::Reaction, "shock")));

		A.Statuses.clear();
		Battle.AddStatus(A, "wet", 2);
		Battle.ResolveAbility(Battle.Units[4], 4, A.Pos, Report);
		Check(A.HasStatus("freeze") && !A.HasStatus("wet"), "ice on a Wet unit should freeze it");

		B.Statuses.clear();
		const int Dry = Amount(Battle, Battle.Units[5], 4, B);
		Battle.AddStatus(B, "oiled", 3);
		Check(Amount(Battle, Battle.Units[5], 4, B) > Dry, "fire should hit the Oiled harder");
		Battle.ResolveAbility(Battle.Units[5], 4, B.Pos, Report);
		int BurnTurns = 0;
		for (const FStatus& Status : B.Statuses)
		{
			BurnTurns = Status.Id == "burn" ? Status.Turns : BurnTurns;
		}
		Check(BurnTurns == 4 && !B.HasStatus("oiled"), "fire on the Oiled should ignite it for twice as long, using the oil up");

		Far.Statuses.clear();
		Battle.AddStatus(Far, "wet", 2);
		Battle.AddStatus(Far, "burn", 4);
		BurnTurns = 0;
		for (const FStatus& Status : Far.Statuses)
		{
			BurnTurns = Status.Id == "burn" ? Status.Turns : BurnTurns;
		}
		Check(BurnTurns == 2 && !Far.HasStatus("wet"), "Burn on a Wet unit should last half as long and dry it");
		Battle.AddStatus(Far, "wet", 2);
		Check(!Far.HasStatus("burn"), "Wet should put Burn out");

		Far.Statuses.clear();
		const int Gain = Battle.TgGain(Far);
		Battle.AddStatus(Far, "chilled", 2, 1);
		Check(Battle.TgGain(Far) < Gain, "Chilled should slow the gauge");
		Battle.AddStatus(Far, "chilled", 2, 1);
		Check(Far.HasStatus("chilled") && !Far.HasStatus("freeze"), "two layers of Chilled should not freeze yet");
		Battle.AddStatus(Far, "chilled", 2, 1);
		Check(Far.HasStatus("freeze") && !Far.HasStatus("chilled"), "three layers of Chilled should freeze");

		// Element rules: water leaves Wet and ice Chills only when the setting is on.
		FBattle Rules;
		Deal(Rules, { { "black_mage", 0, FVec2(4, 8), "t_splash" }, { "knight", 1, FVec2(10, 8), "" }, { "black_mage", 0, FVec2(4, 10), "t_frost" } });
		Rules.ResolveAbility(Rules.Units[0], 4, Rules.Units[1].Pos, Report);
		Check(!Rules.Units[1].HasStatus("wet"), "water should leave nothing with the element rule off");
		Rules.Tuning.Elements = 1.0;
		Rules.ResolveAbility(Rules.Units[0], 4, Rules.Units[1].Pos, Report);
		Check(Rules.Units[1].HasStatus("wet"), "water should leave its target Wet with the element rule on");
		Rules.Units[1].Statuses.clear();
		Rules.ResolveAbility(Rules.Units[2], 4, Rules.Units[1].Pos, Report);
		Check(Rules.Units[1].HasStatus("chilled"), "ice should Chill with the element rule on");
		if (Failures == Before)
		{
			std::printf("lightning shocks the Wet within 2 m; ice freezes the Wet; fire ignites the Oiled for twice as long; Wet halves and puts out Burn; three Chills freeze; the element rule leaves Wet and Chilled\n");
		}
	}

	// The timeline: Haste, Stop, and the two against Slow.
	{
		const int Before = Failures;
		FBattle Battle;
		Deal(Battle, { { "knight", 0, FVec2(6, 8), "" }, { "knight", 1, FVec2(16, 16), "" } });
		FUnit& Knight = Battle.Units[0];
		const int Gain = Battle.TgGain(Knight);
		Battle.AddStatus(Knight, "haste", 2);
		Check(Battle.TgGain(Knight) > Gain, "Haste should fill the gauge faster");
		Battle.AddStatus(Knight, "slow", 2);
		Check(!Knight.HasStatus("haste") && !Knight.HasStatus("slow"), "Slow and Haste should cancel out");

		Battle.AddStatus(Knight, "stop", 2);
		const int Tg = Knight.Tg;
		FTickReport Report;
		Battle.Advance(3 * Pace::TicksPerSecond, Report);
		Check(Knight.Tg == Tg && Knight.HasStatus("stop"), "a Stopped gauge should not fill");
		Battle.Advance(2 * Pace::TicksPerSecond, Report);
		Check(!Knight.HasStatus("stop") && Knight.Tg > Tg, "Stop should last 4 seconds, then the gauge fills again");
		if (Failures == Before)
		{
			std::printf("Haste fills the gauge faster and cancels Slow; Stop holds the gauge still for 4 seconds\n");
		}
	}

	// Allies and the other side: Suppressed, Guarded, Reflect, Reraise, Decay.
	{
		const int Before = Failures;
		FBattle Battle;
		Deal(Battle, { { "archer", 0, FVec2(4, 8), "t_frost" }, { "knight", 1, FVec2(10, 8), "" }, { "white_mage", 1, FVec2(11, 9), "t_mend" },
			{ "knight", 1, FVec2(18, 18), "" } });
		FUnit& Archer = Battle.Units[0];
		FUnit& Knight = Battle.Units[1];
		FUnit& Mage = Battle.Units[2];
		FTickReport Report;

		Battle.AddStatus(Knight, "suppressed", 2, 0, Archer.Id);
		const int Hp = Knight.Hp;
		const std::vector<std::pair<FNode, double>> Reach = Battle.ReachableNodes(Knight);
		Battle.ApplyMove(Knight.Id, FMap::NodePos(Reach.back().first), false, Report);
		Check(Knight.Hp < Hp && !Knight.HasStatus("suppressed"), "walking while Suppressed should draw the suppressor's free blow");
		Battle.AddStatus(Knight, "suppressed", 2, 0, Archer.Id);
		Battle.Hurt(Archer, 1);
		Check(!Knight.HasStatus("suppressed"), "hurting the suppressor should free the suppressed");

		// Guarded: the knight stands by the mage and takes the arrow meant for her.
		Knight.Pos = OpenSpot(Battle, Mage.Pos + FVec2(1, 0), { Mage.Pos, Archer.Pos, Battle.Units[3].Pos });
		Knight.Hp = Knight.MaxHp();
		Battle.AddStatus(Mage, "guarded", 2, 0, Knight.Id);
		const int MageHp = Mage.Hp;
		Battle.ResolveAbility(Archer, 0, Mage.Pos, Report);
		Check(Mage.Hp == MageHp && Knight.Hp < Knight.MaxHp(), "a hit on the Guarded should land on its guardian");
		Check(Count(Report, EEventKind::Redirected, "guard") >= 1, "the guard should be told");
		Knight.Pos = Battle.Units[3].Pos + FVec2(0.0f, -2.0f);
		Knight.Pos = OpenSpot(Battle, Knight.Pos, { Battle.Units[3].Pos });
		Battle.ResolveAbility(Archer, 0, Mage.Pos, Report);
		Check(Mage.Hp < MageHp && !Mage.HasStatus("guarded"), "a guardian too far away can't step in, and the oath breaks");

		// Reflect: the frost bounces back to the archer.
		Mage.Statuses.clear();
		Battle.AddStatus(Mage, "reflect", 3);
		const int ArcherHp = Archer.Hp;
		const int MageBefore = Mage.Hp;
		Battle.ResolveAbility(Archer, 4, Mage.Pos, Report);
		Check(Archer.Hp < ArcherHp && Mage.Hp == MageBefore && !Mage.HasStatus("reflect"), "a spell on the Reflecting should bounce back once");
		Battle.ResolveAbility(Archer, 0, Mage.Pos, Report);
		Check(Mage.Hp < MageBefore, "Reflect should not turn arrows");

		// Decay: the mage's own heal rots the knight.
		Knight.Hp = Knight.MaxHp() / 2;
		Battle.AddStatus(Knight, "decay", 2);
		const int Rotting = Knight.Hp;
		Battle.ResolveAbility(Mage, 4, Knight.Pos, Report);
		Check(Knight.Hp < Rotting, "healing a Decaying unit should hurt it");

		// Wounded (2026-10-02): the same heal lands at half, and so does anything else that mends it.
		Battle.RemoveStatus(Knight, "decay");
		Knight.Hp = 1;
		const int FullHeal = Amount(Battle, Mage, 4, Knight);
		Battle.AddStatus(Knight, "wounded", 2);
		const int HalfHeal = Amount(Battle, Mage, 4, Knight);
		const int WoundedBefore = Knight.Hp;
		Battle.ResolveAbility(Mage, 4, Knight.Pos, Report);
		Check(FullHeal > 1 && HalfHeal == RoundToInt(FullHeal * 0.5) && Knight.Hp - WoundedBefore == HalfHeal,
			"a heal on the Wounded should land at half: " + std::to_string(FullHeal) + " -> " + std::to_string(Knight.Hp - WoundedBefore));
		Check(Knight.HealReceived(10) == 5 && Mage.HealReceived(10) == 10, "Wounded should halve Regen, springs and mending as well, and only on its holder");

		// Reraise: down, then up again three seconds later with a quarter of its health.
		FUnit& Far = Battle.Units[3];
		Battle.AddStatus(Far, "reraise", 3);
		Battle.Hurt(Far, Far.Hp);
		Battle.KnockOut(Far, Report);
		Check(Far.IsKo(), "a unit with Reraise should still fall");
		Battle.Advance(FBattle::ReraiseSeconds * Pace::TicksPerSecond + 1, Report);
		Check(Far.IsAlive() && Far.Hp == std::max(1, Far.MaxHp() / 4), "Reraise should stand it up with a quarter of its health");
		if (Failures == Before)
		{
			std::printf("Suppressed draws a free blow when it walks and ends when its suppressor is hurt; Guarded sends single-target hits to a guardian within 3 m; Reflect bounces one spell; Decay turns heals to harm; Wounded halves them; Reraise stands up once\n");
		}
	}

	// Minds: Charmed and Terrified.
	{
		const int Before = Failures;
		FBattle Battle;
		Deal(Battle, { { "knight", 0, FVec2(8, 8), "" }, { "knight", 1, FVec2(10, 8), "" } });
		FUnit& Blue = Battle.Units[0];
		FUnit& Red = Battle.Units[1];
		Battle.AddStatus(Red, "charmed", 1, 0, Blue.Id);
		Check(Red.Team == 0 && Red.HomeTeam() == 1, "Charmed should put it on the charmer's side");
		Battle.CheckWinner();
		Check(Battle.Winner == -1, "a side whose last unit is Charmed hasn't lost");
		FTickReport Report;
		UntilReady(Battle, Red.Id, Report);
		Check(Red.bReady && Red.Team == 0, "a Charmed unit's turn should be played for the charmer");
		Battle.Apply(FOrder::MakeEndTurn(Red.Id, Red.Serial), Report);
		Check(Red.Team == 1 && Red.CharmedFrom == -1 && !Red.HasStatus("charmed"), "Charmed should end with the turn it gives");
		Battle.AddStatus(Red, "charmed", 1, 0, Blue.Id);
		Battle.Hurt(Red, 1);
		Check(Red.Team == 1 && !Red.HasStatus("charmed"), "damage should bring a Charmed unit back to its senses");

		// Terrified: when its turn comes it runs first.
		Red.bReady = false;
		for (FUnit& Each : Battle.Units)
		{
			Each.Tg = 0;
			Each.bReady = false;
		}
		const double Near = Red.Pos.DistanceTo(Blue.Pos);
		Battle.AddStatus(Red, "terrified", 1, 0, Blue.Id);
		UntilReady(Battle, Red.Id, Report);
		Check(Red.bReady && Red.bMoved && Red.Pos.DistanceTo(Blue.Pos) > Near, "a Terrified unit should first run from what it fears");
		Check(!Red.bActed, "a Terrified unit may still act after running");

		// A boss shrugs off Stop, Charm and Terror.
		if (FindJob("helix_prime"))
		{
			FBattle Boss;
			Deal(Boss, { { "knight", 0, FVec2(8, 8), "" }, { "helix_prime", 2, FVec2(12, 12), "" } });
			Boss.Units[1].bMonster = true;
			for (const char* Id : { "stop", "charmed", "terrified" })
			{
				Boss.AddStatus(Boss.Units[1], Id, 1, 0, 0);
				Check(!Boss.Units[1].HasStatus(Id), std::string("a boss should shrug off ") + Id);
			}
		}
		if (Failures == Before)
		{
			std::printf("Charmed plays its next turn for the charmer, then goes home, and damage ends it early; Terrified runs before it acts; bosses shrug off Stop, Charm and Terror\n");
		}
	}

	// Battles: every unit carries one of the status items, the computer plays both sides.
	{
		const int Before = Failures;
		const char* Carried[] = { "marking_dart", "feint_gauntlet", "waterskin", "oil_flask", "frost_charm", "hastening_draught",
			"hourglass_shard", "suppressing_crossbow", "warding_talisman", "spellward_talisman", "guardians_oath", "rebirth_charm",
			"mirror_ward", "sirens_locket", "dread_mask", "rot_censer" };
		std::vector<FMapDef> Maps;
		for (const auto& Path : FilesIn(MapFolder, ".tmmap.json"))
		{
			FMapDef Def;
			if (ReadMapFile(ReadAll(Path), Def).empty())
			{
				Maps.push_back(Def);
			}
		}
		std::map<std::string, int> Applied;
		int Reactions = 0;
		int Played = 0;
		for (int Round = 0; Round < 16 && !Maps.empty(); ++Round)
		{
			const FMapDef& Map = Maps[static_cast<size_t>(Round) % Maps.size()];
			const uint64_t Seed = 500 + static_cast<uint64_t>(Round);
			auto Build = [&](FBattle& Battle)
			{
				const char* Roster[8] = { "knight", "archer", "black_mage", "white_mage", "knight", "archer", "black_mage", "white_mage" };
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
					Unit.Gear[0] = FindItem(Carried[(Round * 8 + Index) % 16]);
					Unit.Gear[1] = FindItem(Carried[(Round * 8 + Index + 5) % 16]);
					Battle.Units.push_back(Unit);
				}
				Battle.Tuning.Elements = Round % 2;
				Battle.Start(Seed);
			};
			FBattle Battle;
			Build(Battle);
			FAIPlayer Computers[2] = { FAIPlayer("hard"), FAIPlayer("hard") };
			Computers[0].Rng.Seed(Seed);
			Computers[1].Rng.Seed(Seed + 1);
			std::vector<std::pair<int, FOrder>> Orders;
			int Refusals = 0;
			while (Battle.TickCount < 5000 && Battle.Winner < 0)
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
					if (Event.Kind == EEventKind::StatusApplied)
					{
						++Applied[Event.Id];
					}
					Reactions += Event.Kind == EEventKind::Reaction || Event.Kind == EEventKind::Redirected || Event.Kind == EEventKind::Fled ? 1 : 0;
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
				Fail(Map.Id + ": a battle with status items did not replay to the same checksum");
			}
		}
		int Kinds = 0;
		for (const char* Id : { "marked", "offbalance", "wet", "oiled", "chilled", "haste", "stop", "suppressed", "protect", "shell",
			"guarded", "reraise", "reflect", "charmed", "terrified", "decay" })
		{
			Kinds += Applied[Id] > 0 ? 1 : 0;
		}
		if (Kinds < 12)
		{
			std::string Seen;
			for (const auto& Pair : Applied)
			{
				Seen += " " + Pair.first + "=" + std::to_string(Pair.second);
			}
			Fail("the computer used only " + std::to_string(Kinds) + " of the 16 new statuses:" + Seen);
		}
		if (Failures == Before)
		{
			std::printf("the computer played %d battles with the status items: every order legal, %d of the 16 statuses used, %d reactions, guards and flights; each replayed to the same checksum\n",
				Played, Kinds, Reactions);
		}
	}

	// Springs (wells, 2026-10-04): their own healing, and a rest once used.
	{
		const int Before = Failures;
		FBattle Battle;
		Deal(Battle, { { "knight", 0, FVec2(8, 8), "" }, { "knight", 1, FVec2(16, 8), "" } });
		Battle.Tuning.SpringPercent = 10.0;
		Battle.Tuning.HazardPercent = 30.0;
		Battle.Tuning.SpringRestTurns = 2.0;
		FUnit& Blue = Battle.Units[0];
		const int TileX = static_cast<int>(std::floor(Blue.Pos.X / Ground::TileSize));
		const int TileY = static_cast<int>(std::floor(Blue.Pos.Y / Ground::TileSize));
		Battle.Map.Hazards[TileY * Battle.Map.TilesX + TileX] = 1;
		std::vector<int> Healed;
		for (int Turn = 0; Turn < 5; ++Turn)
		{
			Blue.Hp = Blue.MaxHp() / 2;
			FTickReport Report;
			for (int i = 0; i < 4000 && !Blue.bReady; ++i)
			{
				Battle.Tick(Report);
				for (FUnit& Other : Battle.Units)
				{
					if (Other.Id != Blue.Id && Other.bReady)
					{
						Battle.Apply(FOrder::MakeEndTurn(Other.Id, Other.Serial), Report);
					}
				}
			}
			int Amount = 0;
			for (const FEvent& Event : Report.Events)
			{
				Amount += Event.Kind == EEventKind::Hit && Event.Id == "ground" && Event.Unit == Blue.Id ? Event.Amount : 0;
			}
			Healed.push_back(Amount);
			Check(Battle.SpringRestAt(Blue.Pos) == (Turn % 3 == 0 ? 2 : Turn % 3 == 1 ? 1 : 0),
				"a spring should count its rest down on its user's turns (turn " + std::to_string(Turn + 1) + ")");
			Battle.Apply(FOrder::MakeEndTurn(Blue.Id, Blue.Serial), Report);
		}
		const int Mend = RoundToInt(Blue.MaxHp() * 0.1);
		Check(Healed.size() == 5 && Healed[0] == Mend && Healed[1] == 0 && Healed[2] == 0 && Healed[3] == Mend && Healed[4] == 0,
			"a spring should mend its own share (spring_percent, not hazard_percent), then rest 2 turns, then mend again");
		if (Failures == Before)
		{
			std::printf("springs mend by their own number, then run dry for spring_rest_turns of their user's turns\n");
		}
	}

	// Tall grass (2026-10-04): hides who stands in it from an enemy not close by,
	// until it strikes out or is struck, and again from its next turn.
	{
		const int Before = Failures;
		FBattle Battle;
		Deal(Battle, { { "knight", 0, FVec2(8, 8), "" }, { "knight", 1, FVec2(14, 8), "" } });
		FUnit& Blue = Battle.Units[0];
		FUnit& Red = Battle.Units[1];
		const int TileX = static_cast<int>(std::floor(Blue.Pos.X / Ground::TileSize));
		const int TileY = static_cast<int>(std::floor(Blue.Pos.Y / Ground::TileSize));
		Battle.Map.Grass[static_cast<size_t>(TileY) * Battle.Map.TilesX + TileX] = 1;
		Check(Battle.Map.InGrass(Blue.Pos) && Battle.Hidden(Red.Team, Blue), "a unit in tall grass should be hidden from an enemy 6 m off");
		Check(!Battle.Hidden(Blue.Team, Red), "a unit out of the grass should not be hidden");
		const FVec2 Away = Red.Pos;
		Red.Pos = FVec2(Blue.Pos.X + 2.5f, Blue.Pos.Y);
		Check(!Battle.Hidden(Red.Team, Blue), "an enemy within 3 m should see into the grass");
		Red.Pos = Away;
		Blue.bSpotted = true;
		Check(!Battle.Hidden(Red.Team, Blue), "a unit that has struck out of the grass should be seen until its turn comes round");
		FTickReport Report;
		for (int i = 0; i < 4000 && !Blue.bReady; ++i)
		{
			Battle.Tick(Report);
			if (Red.bReady)
			{
				Battle.Apply(FOrder::MakeEndTurn(Red.Id, Red.Serial), Report);
			}
		}
		Check(Blue.bReady && !Blue.bSpotted && Battle.Hidden(Red.Team, Blue), "its next turn should hide it in the grass again");
		if (Failures == Before)
		{
			std::printf("tall grass hides a unit from enemies more than 3 m off until it strikes or is struck, and again from its next turn\n");
		}
	}

	std::printf("\n%s\n", Failures == 0 ? "STATUSES DO WHAT THE DESIGN SAYS" : "STATUSES ARE WRONG");
	return Failures == 0 ? 0 : 1;
}
