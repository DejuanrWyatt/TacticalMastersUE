// Picks from Cire's Spell Codex (2026-10-06, Docs/design/feat-codex-picks.md): the
// thirteen abilities in the class files do what the design says -- nine with
// what the rules already had, four with a rule of their own ("warned",
// "ricochet", "execute", "crowd") -- and battles where the computer plays the
// classes that carry them are legal and replay to the same checksum.
//
// Not Godot's: none of these is in a Godot battle, which the other suites hold.
//
// Run with the classes and maps folders: SimCodexTest <Content/Data/Classes> <Content/Data/Maps>.

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

	const std::pair<const char*, const char*> Picks[] = {
		{ "dust_hexer", "dust_hexer_blight_sigil" }, { "hexblade", "hexblade_crimson_crystals" }, { "thunder_fist", "thunder_fist_storm_slash" },
		{ "night_hunter", "night_hunter_night_spear" }, { "sylvan_muse", "sylvan_muse_thornweave" }, { "roc_caller", "roc_caller_owl_scout" },
		{ "cantor", "cantor_beacon_of_return" }, { "frost_brawler", "frost_brawler_frost_pirouette" }, { "mountain_sentinel", "mountain_sentinel_stone_henge" },
		{ "earthshaker", "earthshaker_faultline" }, { "bastion", "bastion_shield_toss" }, { "inquisitor", "inquisitor_verdict" },
		{ "stone_brawler", "stone_brawler_echo_slam" } };
	constexpr int PickCount = static_cast<int>(sizeof(Picks) / sizeof(Picks[0]));
	{
		const int Before = Failures;
		for (const auto& Each : Picks)
		{
			Check(SlotOf(Each.first, Each.second) >= 0, std::string(Each.second) + " should be one of " + Each.first + "'s abilities");
		}
		if (Failures == Before)
		{
			std::printf("the thirteen codex picks read from their class files\n");
		}
	}

	// Data only: Blight Sigil wounds, Night Spear silences down its line, Thornweave's thorns hurt and slow.
	{
		const int Before = Failures;
		FBattle Battle;
		Deal(Battle, { { "dust_hexer", 0, FVec2(8.25f, 12.25f) }, { "knight", 1, FVec2(13.25f, 12.25f) }, { "knight", 1, FVec2(14.25f, 13.25f) } });
		FTickReport Report;
		TurnOf(Battle, 0, Report);
		if (Use(Battle, 0, SlotOf("dust_hexer", "dust_hexer_blight_sigil"), FVec2(13.75f, 12.75f), Report))
		{
			Check(Battle.Units[1].HasStatus("wounded") && Battle.Units[2].HasStatus("wounded"), "Blight Sigil should leave both knights Wounded");
		}
		FBattle Spear;
		Deal(Spear, { { "night_hunter", 0, FVec2(6.25f, 12.25f) }, { "knight", 1, FVec2(10.25f, 12.25f) }, { "knight", 1, FVec2(14.25f, 12.25f) } });
		FTickReport Throw;
		TurnOf(Spear, 0, Throw);
		if (Use(Spear, 0, SlotOf("night_hunter", "night_hunter_night_spear"), FVec2(17.25f, 12.25f), Throw))
		{
			Check(Spear.Units[1].HasStatus("silence") && Spear.Units[2].HasStatus("silence"), "Night Spear should Silence everyone on its line");
		}
		FBattle Thorns;
		Deal(Thorns, { { "sylvan_muse", 0, FVec2(6.25f, 12.25f) }, { "knight", 1, FVec2(10.25f, 12.25f) } });
		FTickReport Weave;
		TurnOf(Thorns, 0, Weave);
		if (Use(Thorns, 0, SlotOf("sylvan_muse", "sylvan_muse_thornweave"), FVec2(14.25f, 12.25f), Weave))
		{
			Check(Thorns.Zones.size() == 1, "Thornweave should lay its line of thorns");
			EndTurn(Thorns, 0, Weave);
			const int Hp = Thorns.Units[1].Hp;
			FTickReport Start;
			TurnOf(Thorns, 1, Start);
			Check(Thorns.Units[1].Hp < Hp && Thorns.Units[1].HasStatus("slow"), "an enemy starting its turn on the thorns should be hurt and Slowed");
		}
		if (Failures == Before)
		{
			std::printf("Blight Sigil wounds, Night Spear silences its whole line, Thornweave's thorns hurt and slow\n");
		}
	}

	// Faultline (warned): drawn now, nothing hurt; lands at the start of the caster's next turn on whoever is still there.
	{
		const int Before = Failures;
		FBattle Battle;
		Deal(Battle, { { "earthshaker", 0, FVec2(6.25f, 12.25f) }, { "knight", 1, FVec2(10.25f, 12.25f) }, { "knight", 1, FVec2(12.25f, 12.25f) } });
		FTickReport Report;
		TurnOf(Battle, 0, Report);
		const int Slot = SlotOf("earthshaker", "earthshaker_faultline");
		const int Stays = Battle.Units[1].Hp;
		if (Use(Battle, 0, Slot, FVec2(14.25f, 12.25f), Report))
		{
			Check(Count(Report, EEventKind::Hit, 1) == 0 && Count(Report, EEventKind::Hit, 2) == 0, "Faultline should hurt nobody as it is drawn");
			Check(Battle.Zones.size() == 1 && Battle.ZoneIsWarning(Battle.Zones[0]), "Faultline should be drawn on the ground");
			EndTurn(Battle, 0, Report);
			// One knight steps off the line, the other stays.
			FTickReport Walk;
			TurnOf(Battle, 2, Walk);
			const FOrder Away = FOrder::MakeMove(2, Battle.Units[2].Serial, FVec2(12.25f, 14.75f));
			Check(Battle.Validate(Away).empty(), "the knight should be able to step off the line: " + Battle.Validate(Away));
			Battle.Apply(Away, Walk);
			EndTurn(Battle, 2, Walk);
			const int Left = Battle.Units[2].Hp;
			FTickReport Land;
			TurnOf(Battle, 0, Land);
			Check(Battle.Units[1].Hp < Stays && Battle.Units[1].HasStatus("stun"), "Faultline should land on the knight still on it, and Stun it");
			Check(Battle.Units[2].Hp == Left, "Faultline should miss the knight that stepped off");
			Check(Battle.Zones.empty(), "Faultline's mark should be gone once it lands");
			Check(Battle.Units[0].bReady, "the earthshaker's turn should go on as usual after it lands");
		}
		if (Failures == Before)
		{
			std::printf("Faultline is drawn first and lands on the next turn on whoever stayed on it\n");
		}
	}

	// Shield Toss (ricochet): the first, then two more within 5 m, each weaker, each taunted.
	{
		const int Before = Failures;
		FBattle Battle;
		Deal(Battle, { { "bastion", 0, FVec2(6.25f, 12.25f) }, { "knight", 1, FVec2(11.25f, 12.25f) }, { "knight", 1, FVec2(14.25f, 12.25f) },
			{ "knight", 1, FVec2(17.25f, 12.25f) }, { "knight", 1, FVec2(24.25f, 12.25f) } });
		FTickReport Report;
		TurnOf(Battle, 0, Report);
		if (Use(Battle, 0, SlotOf("bastion", "bastion_shield_toss"), Battle.Units[1].Pos, Report))
		{
			int Amounts[5] = { 0, 0, 0, 0, 0 };
			for (const FEvent& Event : Report.Events)
			{
				if (Event.Kind == EEventKind::Hit && Event.Id == "bastion_shield_toss" && Event.Unit >= 1 && Event.Unit <= 4)
				{
					Amounts[Event.Unit] = Event.Amount;
				}
			}
			Check(Amounts[1] > 0 && Amounts[2] > 0 && Amounts[3] > 0, "Shield Toss should strike the first knight and bounce to the next two");
			Check(Amounts[4] == 0, "Shield Toss should bounce only twice, and only within 5 m");
			Check(Amounts[2] < Amounts[1] && Amounts[3] < Amounts[2], "each bounce should land weaker");
			Check(Battle.Units[1].HasStatus("taunt") && Battle.Units[2].HasStatus("taunt") && Battle.Units[3].HasStatus("taunt"), "each knight struck should be taunted");
			Check(Count(Report, EEventKind::Reaction, -1, "ricochet") == 2, "each bounce should be said");
		}
		if (Failures == Before)
		{
			std::printf("Shield Toss bounces twice within 5 m, a fifth weaker each time, taunting each\n");
		}
	}

	// Verdict (execute): harder on the hurt; a kill gives back half the gauge.
	{
		const int Before = Failures;
		auto Strike = [&](int Hp, int& Dealt, int& UltAfter)
		{
			FBattle Battle;
			Deal(Battle, { { "inquisitor", 0, FVec2(8.25f, 12.25f) }, { "knight", 1, FVec2(9.25f, 12.25f) } });
			FTickReport Report;
			TurnOf(Battle, 0, Report);
			Battle.Units[0].Ult = Pace::UltMax;
			Battle.Units[1].Hp = Hp < 0 ? Battle.Units[1].MaxHp() : Hp;
			const int Was = Battle.Units[1].Hp;
			Dealt = 0;
			UltAfter = -1;
			if (Use(Battle, 0, SlotOf("inquisitor", "inquisitor_verdict"), Battle.Units[1].Pos, Report))
			{
				Dealt = Was - Battle.Units[1].Hp;
				UltAfter = Battle.Units[0].Ult;
			}
			return Battle.Units[1].MaxHp();
		};
		int Whole = 0, WholeUlt = 0, Half = 0, HalfUlt = 0, Kill = 0, KillUlt = 0;
		const int Max = Strike(-1, Whole, WholeUlt);
		Strike(Max / 2, Half, HalfUlt);
		Check(Half >= Whole + (Max - Max / 2) / 4 - 1, "Verdict should add a quarter of the missing health (" + std::to_string(Whole) + " whole, " + std::to_string(Half) + " at half)");
		Check(WholeUlt == 0, "Verdict without a kill should spend the whole gauge");
		Strike(5, Kill, KillUlt);
		Check(KillUlt == Pace::UltMax / 2, "a kill with Verdict should give back half the gauge (" + std::to_string(KillUlt) + ")");
		if (Failures == Before)
		{
			std::printf("Verdict adds a quarter of the missing health, and a kill gives back half the gauge\n");
		}
	}

	// Echo Slam (crowd): a fifth harder for each enemy beyond the first.
	{
		const int Before = Failures;
		auto Slam = [&](int Enemies)
		{
			std::vector<FSetup> Setup = { { "stone_brawler", 0, FVec2(10.25f, 12.25f) } };
			const FVec2 Spots[3] = { FVec2(11.75f, 12.25f), FVec2(8.75f, 12.25f), FVec2(10.25f, 13.75f) };
			for (int i = 0; i < Enemies; ++i)
			{
				Setup.push_back({ "knight", 1, Spots[i] });
			}
			Setup.push_back({ "knight", 1, FVec2(24.25f, 12.25f) });
			FBattle Battle;
			Deal(Battle, Setup);
			FTickReport Report;
			TurnOf(Battle, 0, Report);
			const int Was = Battle.Units[1].Hp;
			if (!Use(Battle, 0, SlotOf("stone_brawler", "stone_brawler_echo_slam"), Battle.Units[0].Pos, Report))
			{
				return 0;
			}
			return Was - Battle.Units[1].Hp;
		};
		const int One = Slam(1);
		const int Three = Slam(3);
		Check(One > 0 && std::abs(Three - One * 1.4) <= 2.0, "Echo Slam on three should land 40% harder than on one (" + std::to_string(One) + ", " + std::to_string(Three) + ")");
		if (Failures == Before)
		{
			std::printf("Echo Slam lands a fifth harder for each enemy beyond the first\n");
		}
	}

	// Battles: the computer plays the thirteen classes on every map, both sides.
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
		std::map<std::string, int> Used;
		int Played = 0;
		for (int Round = 0; Round < 13 && !Maps.empty(); ++Round)
		{
			const FMapDef& Map = Maps[static_cast<size_t>(Round) % Maps.size()];
			const uint64_t Seed = 1300 + static_cast<uint64_t>(Round);
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
					Unit.Job = Picks[static_cast<size_t>((Round * 8 + Index) % PickCount)].first;
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
			std::string FirstRefusal;
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
					const std::string Why = Battle.Validate(Order);
					if (!Why.empty())
					{
						if (FirstRefusal.empty())
						{
							FirstRefusal = Unit->Job + ": " + Why;
						}
						++Refusals;
						Order = FOrder::MakeEndTurn(Unit->Id, Unit->Serial);
					}
					Orders.emplace_back(Battle.TickCount, Order);
					Battle.Apply(Order, Report);
				}
				for (const FEvent& Event : Report.Events)
				{
					// A warned blow counts once, as it lands.
					if (Event.Kind == EEventKind::Resolved && Event.Amount != 2)
					{
						for (const auto& Each : Picks)
						{
							Used[Event.Id] += Event.Id == Each.second ? 1 : 0;
						}
					}
				}
			}
			++Played;
			if (Refusals > 0)
			{
				Fail(Map.Id + ": the computer gave " + std::to_string(Refusals) + " orders the rules refused, first " + FirstRefusal);
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
				Fail(Map.Id + ": a battle with the codex picks did not replay to the same checksum");
			}
		}
		int Kinds = 0;
		std::string Seen;
		for (const auto& Pair : Used)
		{
			Kinds += Pair.second > 0 ? 1 : 0;
			Seen += Pair.second > 0 ? " " + Pair.first + "=" + std::to_string(Pair.second) : "";
		}
		if (Kinds < 9)
		{
			Fail("the computer used only " + std::to_string(Kinds) + " of the thirteen:" + Seen);
		}
		if (Failures == Before)
		{
			std::printf("the computer played %d battles with the thirteen: every order legal, %d of them used; each replayed to the same checksum\n ", Played, Kinds);
			std::printf("%s\n", Seen.c_str());
		}
	}

	std::printf("\n%s\n", Failures == 0 ? "THE CODEX PICKS DO WHAT THE DESIGN SAYS" : "THE CODEX PICKS ARE WRONG");
	return Failures == 0 ? 0 : 1;
}
