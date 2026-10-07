// The unique and mobility spells (2026-10-05, Docs/design/feat-new-spells.md): the
// thirty abilities in the class files do what the design says, and battles where
// the computer plays the classes that carry them are legal and replay to the same
// checksum.
//
// Not Godot's: none of these statuses or specials is in a Godot battle, which the
// other suites (their baselines unchanged) hold.
//
// Run with the classes and maps folders: SimSpellTest <Content/Data/Classes> <Content/Data/Maps>.

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

	const std::pair<const char*, const char*> Thirty[] = {
		{ "lich_caller", "lich_caller_contagion" }, { "hexblade", "hexblade_soul_link" }, { "arc_warlock", "arc_warlock_time_bomb" },
		{ "time_mage", "time_mage_gravity_well" }, { "warlock", "warlock_life_tether" }, { "bard", "bard_echo" },
		{ "paladin", "paladin_retribution" }, { "exorcist", "exorcist_purge_transfer" }, { "war_drummer", "war_drummer_overcharge" },
		{ "dread_knight", "dread_knight_blood_pact" }, { "tide_cleric", "tide_cleric_chain_mend" }, { "holy_guardian", "holy_guardian_undying" },
		{ "sylvan_muse", "sylvan_muse_spirit_swap" }, { "berserker", "berserker_reckoning" }, { "shadow_assassin", "shadow_assassin_death_mark" },
		{ "wind_dancer", "wind_dancer_dash" }, { "sky_lancer", "sky_lancer_grapple" }, { "war_marshal", "war_marshal_rally_call" },
		{ "siren", "siren_lure" }, { "tide_brawler", "tide_brawler_hook" }, { "stone_fist", "stone_fist_shove" },
		{ "gale_dancer", "gale_dancer_vault" }, { "crusader", "crusader_charge" }, { "steel_ranger", "steel_ranger_disengage" },
		{ "aeromancer", "aeromancer_fair_winds" }, { "summoner", "summoner_rift_gate" }, { "chrono_sage", "chrono_sage_recall" },
		{ "frost_stalker", "frost_stalker_ice_slide" }, { "leviathan_caller", "leviathan_caller_riptide" }, { "night_hunter", "night_hunter_shadow_hop" } };
	{
		const int Before = Failures;
		for (const auto& Each : Thirty)
		{
			Check(SlotOf(Each.first, Each.second) >= 0, std::string(Each.second) + " should be one of " + Each.first + "'s abilities");
		}
		if (Failures == Before)
		{
			std::printf("the thirty spells read from their class files\n");
		}
	}

	// Contagion: a plague that hurts each turn and passes to allies within 2 m as the carrier's turn ends.
	{
		const int Before = Failures;
		FBattle Battle;
		Deal(Battle, { { "lich_caller", 0, FVec2(8.25f, 12.25f) }, { "knight", 1, FVec2(13.25f, 12.25f) }, { "knight", 1, FVec2(14.75f, 12.25f) } });
		FTickReport Report;
		TurnOf(Battle, 0, Report);
		if (Use(Battle, 0, SlotOf("lich_caller", "lich_caller_contagion"), Battle.Units[1].Pos, Report))
		{
			Check(Battle.Units[1].HasStatus("plague"), "Contagion should give the Plague");
			EndTurn(Battle, 0, Report);
			FTickReport Next;
			TurnOf(Battle, 1, Next);
			const int Hp = Battle.Units[1].Hp;
			(void)Hp;
			Check(Count(Next, EEventKind::Hit, 1, "plague") == 1, "the Plague should hurt as its holder's turn begins");
			EndTurn(Battle, 1, Next);
			Check(Battle.Units[2].HasStatus("plague"), "the Plague should pass to an ally within 2 m as the carrier's turn ends");
		}
		if (Failures == Before)
		{
			std::printf("Contagion hurts each turn and spreads to allies within 2 m\n");
		}
	}

	// Soul Link: half of a blow on one lands on the other. Time Bomb: goes off on the carrier's second turn.
	{
		const int Before = Failures;
		FBattle Battle;
		Deal(Battle, { { "hexblade", 0, FVec2(8.25f, 12.25f) }, { "archer", 0, FVec2(8.25f, 14.25f) },
			{ "knight", 1, FVec2(13.25f, 12.25f) }, { "knight", 1, FVec2(15.25f, 12.25f) } });
		FTickReport Report;
		TurnOf(Battle, 0, Report);
		if (Use(Battle, 0, SlotOf("hexblade", "hexblade_soul_link"), Battle.Units[2].Pos, Report))
		{
			Check(Battle.Units[2].HasStatus("linked") && Battle.Units[3].HasStatus("linked"), "Soul Link should bind the two nearest enemies");
			EndTurn(Battle, 0, Report);
			TurnOf(Battle, 1, Report);
			FTickReport Shot;
			const int Partner = Battle.Units[3].Hp;
			if (Use(Battle, 1, 0, Battle.Units[2].Pos, Shot))
			{
				Check(Count(Shot, EEventKind::Hit, 3, "link") == 1 && Battle.Units[3].Hp < Partner, "a blow on one linked unit should hurt the other");
			}
		}
		FBattle Bomb;
		Deal(Bomb, { { "arc_warlock", 0, FVec2(8.25f, 12.25f) }, { "knight", 1, FVec2(13.25f, 12.25f) }, { "knight", 1, FVec2(15.25f, 12.25f) } });
		FTickReport Plant;
		TurnOf(Bomb, 0, Plant);
		if (Use(Bomb, 0, SlotOf("arc_warlock", "arc_warlock_time_bomb"), Bomb.Units[1].Pos, Plant))
		{
			EndTurn(Bomb, 0, Plant);
			const int Neighbour = Bomb.Units[2].Hp;
			FTickReport Wait;
			TurnOf(Bomb, 1, Wait);
			Check(Count(Wait, EEventKind::Hit, -1, "bomb") == 0, "the bomb should wait through its carrier's first turn");
			EndTurn(Bomb, 1, Wait);
			FTickReport Boom;
			for (int i = 0; i < 20000 && Count(Boom, EEventKind::Hit, -1, "bomb") == 0; ++i)
			{
				for (const FUnit& Each : Bomb.Units)
				{
					if (Each.bReady)
					{
						EndTurn(Bomb, Each.Id, Boom);
					}
				}
				Bomb.Tick(Boom);
			}
			Check(Count(Boom, EEventKind::Hit, 2, "bomb") == 1 && Bomb.Units[2].Hp < Neighbour, "the bomb should go off on its carrier's second turn and hurt its ally within 4 m");
		}
		if (Failures == Before)
		{
			std::printf("Soul Link shares half a blow; Time Bomb goes off on the carrier's second turn, catching its allies\n");
		}
	}

	// Undying, Chain Mend, Gravity Well.
	{
		const int Before = Failures;
		FBattle Battle;
		Deal(Battle, { { "holy_guardian", 0, FVec2(8.25f, 12.25f) }, { "tide_cleric", 0, FVec2(8.25f, 14.25f) },
			{ "archer", 0, FVec2(10.25f, 12.25f) }, { "knight", 0, FVec2(11.75f, 12.25f) }, { "knight", 1, FVec2(20.25f, 12.25f) } });
		FTickReport Report;
		TurnOf(Battle, 0, Report);
		if (Use(Battle, 0, SlotOf("holy_guardian", "holy_guardian_undying"), Battle.Units[2].Pos, Report))
		{
			Battle.Hurt(Battle.Units[2], 9999);
			Check(Battle.Units[2].Hp == 1, "Undying should leave 1 health");
		}
		EndTurn(Battle, 0, Report);
		Battle.Units[3].Hp = Battle.Units[3].MaxHp() / 2;
		TurnOf(Battle, 1, Report);
		FTickReport Mend;
		const int Knight = Battle.Units[3].Hp;
		if (Use(Battle, 1, SlotOf("tide_cleric", "tide_cleric_chain_mend"), Battle.Units[2].Pos, Mend))
		{
			Check(Battle.Units[2].Hp > 1 && Battle.Units[3].Hp > Knight, "Chain Mend should leap to the next ally within 3 m");
		}
		FBattle Well;
		Deal(Well, { { "time_mage", 0, FVec2(6.25f, 12.25f) }, { "knight", 1, FVec2(10.25f, 12.25f) }, { "knight", 1, FVec2(14.25f, 12.25f) } });
		FTickReport Pull;
		TurnOf(Well, 0, Pull);
		const FVec2 Middle(12.25f, 12.25f);
		const float A = Well.Units[1].Pos.DistanceTo(Middle);
		const float B = Well.Units[2].Pos.DistanceTo(Middle);
		if (Use(Well, 0, SlotOf("time_mage", "time_mage_gravity_well"), Middle, Pull))
		{
			Check(Well.Units[1].Pos.DistanceTo(Middle) < A - 0.4f && Well.Units[2].Pos.DistanceTo(Middle) < B - 0.4f,
				"Gravity Well should drag the enemies toward its middle");
		}
		if (Failures == Before)
		{
			std::printf("Undying leaves 1 health; Chain Mend leaps; Gravity Well drags enemies in\n");
		}
	}

	// Mobility: Dash, Hook, Shove, Lure (swap), Vault, Charge, Rally Call, Grapple.
	{
		const int Before = Failures;
		auto Mover = [&](const char* Job, const char* AbilityId, const std::vector<FSetup>& Setups, const FVec2& Aim, FBattle& Battle, FTickReport& Report)
		{
			Deal(Battle, Setups);
			TurnOf(Battle, 0, Report);
			return Use(Battle, 0, SlotOf(Job, AbilityId), Aim, Report);
		};
		{
			FBattle Battle;
			FTickReport Report;
			const FVec2 From = FMap::Snap(FVec2(6.25f, 12.25f));
			if (Mover("wind_dancer", "wind_dancer_dash", { { "wind_dancer", 0, From }, { "knight", 1, FVec2(20.25f, 4.25f) } }, FVec2(12.25f, 12.25f), Battle, Report))
			{
				Check(Battle.Units[0].Pos.DistanceTo(From) > 4.5f, "Dash should carry the dancer up to 6 m");
			}
		}
		{
			FBattle Battle;
			FTickReport Report;
			if (Mover("tide_brawler", "tide_brawler_hook", { { "tide_brawler", 0, FVec2(6.25f, 12.25f) }, { "knight", 1, FVec2(12.25f, 12.25f) } }, FVec2(12.25f, 12.25f), Battle, Report))
			{
				Check(Battle.Units[1].Pos.DistanceTo(Battle.Units[0].Pos) < 2.0f, "Hook should drag the enemy beside the brawler");
			}
		}
		{
			FBattle Battle;
			FTickReport Report;
			if (Mover("stone_fist", "stone_fist_shove", { { "stone_fist", 0, FVec2(8.25f, 12.25f) }, { "knight", 1, FVec2(9.25f, 12.25f) }, { "knight", 1, FVec2(11.25f, 12.25f) } },
				FVec2(9.25f, 12.25f), Battle, Report))
			{
				Check(Battle.Units[1].HasStatus("stun"), "Shove stopped short by a unit should stun");
			}
		}
		{
			FBattle Battle;
			FTickReport Report;
			const FVec2 Mine = FMap::Snap(FVec2(8.25f, 12.25f));
			const FVec2 Theirs = FMap::Snap(FVec2(12.25f, 12.25f));
			if (Mover("siren", "siren_lure", { { "siren", 0, Mine }, { "knight", 1, Theirs } }, Theirs, Battle, Report))
			{
				Check(Battle.Units[0].Pos == Theirs && Battle.Units[1].Pos == Mine, "Lure should trade places with the enemy");
			}
		}
		{
			FBattle Battle;
			FTickReport Report;
			if (Mover("gale_dancer", "gale_dancer_vault", { { "gale_dancer", 0, FVec2(8.25f, 12.25f) }, { "knight", 1, FVec2(9.25f, 12.25f) } }, FVec2(9.25f, 12.25f), Battle, Report))
			{
				Check(Battle.Units[0].Pos.X > Battle.Units[1].Pos.X, "Vault should land on the far side");
			}
		}
		{
			FBattle Battle;
			FTickReport Report;
			if (Mover("crusader", "crusader_charge", { { "crusader", 0, FVec2(6.25f, 12.25f) }, { "knight", 1, FVec2(12.25f, 12.25f) } }, FVec2(12.25f, 12.25f), Battle, Report))
			{
				Check(Battle.Units[0].Pos.DistanceTo(Battle.Units[1].Pos) < 2.0f && Count(Report, EEventKind::Hit, 1) >= 1, "Charge should run to the enemy and strike");
			}
		}
		{
			FBattle Battle;
			FTickReport Report;
			if (Mover("war_marshal", "war_marshal_rally_call", { { "war_marshal", 0, FVec2(6.25f, 12.25f) }, { "archer", 0, FVec2(12.25f, 12.25f) }, { "knight", 1, FVec2(20.25f, 4.25f) } },
				FVec2(12.25f, 12.25f), Battle, Report))
			{
				Check(Battle.Units[1].Pos.DistanceTo(Battle.Units[0].Pos) < 2.0f, "Rally Call should bring the ally beside the marshal");
			}
		}
		{
			FBattle Battle;
			FTickReport Report;
			if (Mover("sky_lancer", "sky_lancer_grapple", { { "sky_lancer", 0, FVec2(6.25f, 12.25f) }, { "archer", 0, FVec2(12.25f, 12.25f) }, { "knight", 1, FVec2(20.25f, 4.25f) } },
				FVec2(12.25f, 12.25f), Battle, Report))
			{
				Check(Battle.Units[0].Pos.DistanceTo(Battle.Units[1].Pos) < 2.0f, "Grapple should land the lancer beside the ally");
			}
		}
		if (Failures == Before)
		{
			std::printf("Dash, Hook, Shove (stunned when stopped), Lure, Vault, Charge, Rally Call and Grapple move who they should\n");
		}
	}

	// Recall and Rift Gate.
	{
		const int Before = Failures;
		FBattle Battle;
		Deal(Battle, { { "chrono_sage", 0, FVec2(6.25f, 12.25f) }, { "summoner", 0, FVec2(6.25f, 14.25f) }, { "knight", 1, FVec2(22.25f, 4.25f) } });
		FTickReport Report;
		TurnOf(Battle, 0, Report);
		const FVec2 Home = Battle.Units[0].Pos;
		const int Recall = SlotOf("chrono_sage", "chrono_sage_recall");
		if (Use(Battle, 0, Recall, Home, Report))
		{
			Check(Battle.Zones.size() == 1, "Recall should leave a mark");
			EndTurn(Battle, 0, Report);
			Battle.Units[0].Pos = FMap::Snap(FVec2(10.25f, 12.25f));
			TurnOf(Battle, 0, Report);
			FTickReport Back;
			if (Use(Battle, 0, Recall, Battle.Units[0].Pos, Back))
			{
				Check(Battle.Units[0].Pos.DistanceTo(Home) < 1.0f && Battle.Zones.empty(), "Recall used again should bring the sage back and spend the mark");
			}
			EndTurn(Battle, 0, Report);
		}
		TurnOf(Battle, 1, Report);
		const FVec2 Far = FMap::Snap(FVec2(14.25f, 14.25f));
		const FVec2 Mouth = Battle.Units[1].Pos;
		if (Use(Battle, 1, SlotOf("summoner", "summoner_rift_gate"), Far, Report))
		{
			EndTurn(Battle, 1, Report);
			TurnOf(Battle, 0, Report);
			FTickReport Walk;
			const FVec2 Step = FMap::Snap(Mouth + FVec2(1.0f, 0.0f));
			const FOrder Move = FOrder::MakeMove(0, Battle.Units[0].Serial, Step);
			if (Battle.Validate(Move).empty())
			{
				Battle.Apply(Move, Walk);
				Check(Battle.Units[0].Pos.DistanceTo(Far) < 2.0f, "a walk ending at a Rift Gate should come out of the other");
			}
			else
			{
				Fail("couldn't walk to the gate: " + Battle.Validate(Move));
			}
		}
		if (Failures == Before)
		{
			std::printf("Recall marks a spot and returns to it; Rift Gate carries a walk from one mouth to the other\n");
		}
	}

	// Echo and Death Mark.
	{
		const int Before = Failures;
		FBattle Battle;
		Deal(Battle, { { "bard", 0, FVec2(6.25f, 12.25f) }, { "archer", 0, FVec2(8.25f, 12.25f) }, { "shadow_assassin", 0, FVec2(8.25f, 14.25f) },
			{ "knight", 1, FVec2(13.25f, 12.25f) } });
		FTickReport Report;
		TurnOf(Battle, 0, Report);
		if (Use(Battle, 0, SlotOf("bard", "bard_echo"), Battle.Units[1].Pos, Report))
		{
			EndTurn(Battle, 0, Report);
			TurnOf(Battle, 1, Report);
			FTickReport Shot;
			if (Use(Battle, 1, 0, Battle.Units[3].Pos, Shot))
			{
				Check(Count(Shot, EEventKind::Resolved, 1) == 2 && !Battle.Units[1].HasStatus("echo"), "an echoed ability should go off twice, once");
			}
			EndTurn(Battle, 1, Shot);
		}
		TurnOf(Battle, 2, Report);
		if (Use(Battle, 2, SlotOf("shadow_assassin", "shadow_assassin_death_mark"), Battle.Units[3].Pos, Report))
		{
			EndTurn(Battle, 2, Report);
			const int Gauge = Battle.Units[0].Tg;
			FTickReport Fall;
			Battle.Units[3].Hp = 0;
			Battle.KnockOut(Battle.Units[3], Fall);
			Check(Battle.Units[0].Tg > Gauge, "a marked enemy falling should pay the marker's side gauge");
		}
		if (Failures == Before)
		{
			std::printf("Echo repeats an ability once; Death Mark pays gauge when the marked falls\n");
		}
	}

	// Purge Transfer, Spirit Swap, Disengage, Riptide, Shadow Hop.
	{
		const int Before = Failures;
		{
			FBattle Battle;
			Deal(Battle, { { "exorcist", 0, FVec2(6.25f, 12.25f) }, { "archer", 0, FVec2(8.25f, 12.25f) }, { "knight", 1, FVec2(11.25f, 12.25f) } });
			FTickReport Report;
			TurnOf(Battle, 0, Report);
			Battle.AddStatus(Battle.Units[1], "burn", 3);
			if (Use(Battle, 0, SlotOf("exorcist", "exorcist_purge_transfer"), Battle.Units[1].Pos, Report))
			{
				Check(!Battle.Units[1].HasStatus("burn") && Battle.Units[2].HasStatus("burn"), "Purge Transfer should move the ally's Burn to the enemy");
			}
		}
		{
			FBattle Battle;
			Deal(Battle, { { "sylvan_muse", 0, FVec2(6.25f, 12.25f) }, { "archer", 0, FVec2(8.25f, 12.25f) }, { "knight", 1, FVec2(20.25f, 4.25f) } });
			FTickReport Report;
			TurnOf(Battle, 0, Report);
			Battle.Units[1].Hp = Battle.Units[1].MaxHp() / 5;
			if (Use(Battle, 0, SlotOf("sylvan_muse", "sylvan_muse_spirit_swap"), Battle.Units[1].Pos, Report))
			{
				Check(Battle.Units[1].Hp == Battle.Units[1].MaxHp() && Battle.Units[0].Hp < Battle.Units[0].MaxHp() / 4,
					"Spirit Swap should trade health shares");
			}
		}
		{
			FBattle Battle;
			const FVec2 From = FMap::Snap(FVec2(10.25f, 12.25f));
			Deal(Battle, { { "steel_ranger", 0, From }, { "knight", 1, FVec2(11.25f, 12.25f) } });
			FTickReport Report;
			TurnOf(Battle, 0, Report);
			if (Use(Battle, 0, SlotOf("steel_ranger", "steel_ranger_disengage"), From, Report))
			{
				Check(Battle.Units[0].Pos.X < From.X - 2.0f, "Disengage should leap away from the enemy");
			}
		}
		{
			FBattle Battle;
			Deal(Battle, { { "leviathan_caller", 0, FVec2(6.25f, 12.25f) }, { "knight", 1, FVec2(8.75f, 12.25f) }, { "knight", 1, FVec2(11.75f, 12.25f) } });
			FTickReport Report;
			TurnOf(Battle, 0, Report);
			const FVec2 Far = Battle.Units[2].Pos;
			if (Use(Battle, 0, SlotOf("leviathan_caller", "leviathan_caller_riptide"), FVec2(12.25f, 12.25f), Report))
			{
				Check(Battle.Units[0].Pos == Far, "Riptide should trade places with the furthest enemy struck");
			}
		}
		{
			FBattle Battle;
			Deal(Battle, { { "night_hunter", 0, FVec2(6.25f, 12.25f) }, { "knight", 1, FVec2(22.25f, 4.25f) } });
			Battle.Map.Grass.assign(static_cast<size_t>(Battle.Map.TilesX) * Battle.Map.TilesY, 0);
			const FVec2 Hide = FMap::Snap(FVec2(11.25f, 12.25f));
			Battle.Map.Grass[static_cast<size_t>(std::floor(Hide.Y / Ground::TileSize)) * Battle.Map.TilesX
				+ static_cast<size_t>(std::floor(Hide.X / Ground::TileSize))] = 1;
			FTickReport Report;
			TurnOf(Battle, 0, Report);
			const int Slot = SlotOf("night_hunter", "night_hunter_shadow_hop");
			Check(!Battle.Validate(FOrder::MakeUseAbility(0, Battle.Units[0].Serial, Slot, FVec2(8.25f, 14.25f))).empty(), "Shadow Hop should refuse open ground");
			if (Use(Battle, 0, Slot, Hide, Report))
			{
				Check(Battle.Units[0].Pos == Hide && Battle.Hidden(1, Battle.Units[0]), "Shadow Hop should land hidden in the grass");
			}
		}
		if (Failures == Before)
		{
			std::printf("Purge Transfer, Spirit Swap, Disengage, Riptide and Shadow Hop do what they say\n");
		}
	}

	// Battles: the computer plays the thirty classes on every map, both sides.
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
		for (int Round = 0; Round < 15 && !Maps.empty(); ++Round)
		{
			const FMapDef& Map = Maps[static_cast<size_t>(Round) % Maps.size()];
			const uint64_t Seed = 900 + static_cast<uint64_t>(Round);
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
					Unit.Job = Thirty[static_cast<size_t>((Round * 8 + Index) % 30)].first;
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
					if (Event.Kind == EEventKind::Resolved)
					{
						for (const auto& Each : Thirty)
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
				Fail(Map.Id + ": a battle with the new spells did not replay to the same checksum");
			}
		}
		int Kinds = 0;
		std::string Seen;
		for (const auto& Pair : Used)
		{
			Kinds += Pair.second > 0 ? 1 : 0;
			Seen += Pair.second > 0 ? " " + Pair.first.substr(Pair.first.rfind('_') + 1) + "=" + std::to_string(Pair.second) : "";
		}
		if (Kinds < 12)
		{
			Fail("the computer used only " + std::to_string(Kinds) + " of the thirty:" + Seen);
		}
		if (Failures == Before)
		{
			std::printf("the computer played %d battles with the thirty: every order legal, %d of them used; each replayed to the same checksum\n ", Played, Kinds);
			std::printf("%s\n", Seen.c_str());
		}
	}

	std::printf("\n%s\n", Failures == 0 ? "THE NEW SPELLS DO WHAT THE DESIGN SAYS" : "THE NEW SPELLS ARE WRONG");
	return Failures == 0 ? 0 : 1;
}
