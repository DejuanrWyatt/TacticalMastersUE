// The classes, read from Tactical Masters' own class files.
//
// Three things are checked, in the order they would go wrong.
//
// READ: every file in Content/Data/Classes loads through the rules' own reader
// and registers, and the game then knows the six built-in classes plus every one
// of those.
//
// REFUSE: the reader is strict. A key the format does not have, a number out of
// range, an unknown status, a class id already taken -- each is refused with a
// reason rather than skipped. Forgiving reading is what made the Astra files
// dangerous; this proves it did not come back.
//
// PLAY: every class fights one battle through the order path -- in a team with
// a knight, an archer and a white mage, against a black mage, a knight, an
// archer and a white mage -- and the rules must refuse none of the computer's
// orders. A class can read perfectly and still be unplayable, an ability
// nothing can legally aim, and that shows up only here.
//
// What each class's numbers should be is the class creator's business now
// (E:\TacticsClassCreator), where every class is designed and measured.
//
//   SimClassTest <class dir>

#include "SimAI.h"
#include "SimBattle.h"
#include "SimClassFile.h"

#include <algorithm>
#include <map>
#include <chrono>
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

	std::string ReadAll(const std::filesystem::path& Path)
	{
		std::ifstream In(Path, std::ios::binary);
		std::stringstream Buffer;
		Buffer << In.rdbuf();
		return Buffer.str();
	}

	/** A test file: a valid class with one thing changed, which must be refused. */
	std::string Mutated(const std::string& Valid, const std::string& Find, const std::string& Replace)
	{
		const size_t At = Valid.find(Find);
		if (At == std::string::npos)
		{
			return std::string();
		}
		std::string Out = Valid;
		Out.replace(At, Find.size(), Replace);
		return Out;
	}

	// ------------------------------------------------------------- PLAY

	struct FPlayed
	{
		int Orders = 0;
		int Refused = 0;
		int Ticks = 0;
		int Winner = -1;
		std::string FirstRefusal;
	};

	FPlayed PlayOne(const std::string& JobId)
	{
		// The class in a mixed team against the reference team.
		const std::string Roster[8] =
		{
			JobId, "knight", "archer", "white_mage",
			"black_mage", "knight", "archer", "white_mage"
		};
		const FVec2 Blue[4] = { FVec2(2.75f, 4.75f), FVec2(0.75f, 8.75f), FVec2(4.75f, 6.75f), FVec2(2.75f, 10.75f) };
		FBattle Battle;
		Battle.Map.BuildMirrored(HighlandsRows());
		const FVec2 Size = Battle.Map.SizeMeters();
		for (int Index = 0; Index < 8; ++Index)
		{
			FUnit Unit;
			Unit.Id = Index;
			Unit.Team = Index < 4 ? 0 : 1;
			Unit.Job = Roster[Index];
			Unit.Stats = &FindJob(Unit.Job)->Stats;
			const FVec2 Spot = Blue[Index % 4];
			Unit.Pos = Index < 4 ? Spot : FVec2(Size.X - Spot.X, Size.Y - Spot.Y);
			Battle.Units.push_back(Unit);
		}
		Battle.Start(1);
		FAIPlayer Computers[2] = { FAIPlayer("hard"), FAIPlayer("hard") };
		Computers[0].Rng.Seed(1);
		Computers[1].Rng.Seed(2);

		FPlayed Out;
		const int TickLimit = 6000;
		while (Battle.Winner < 0 && Battle.TickCount < TickLimit)
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
				Battle.Apply(FOrder::MakeAdvance(1), Report);
				continue;
			}
			FOrder Order = Computers[Ready->Team].NextCommand(Battle, *Ready);
			++Out.Orders;
			const std::string Refused = Battle.Validate(Order);
			if (!Refused.empty())
			{
				++Out.Refused;
				if (Out.FirstRefusal.empty())
				{
					Out.FirstRefusal = Refused;
				}
				Order = FOrder::MakeEndTurn(Ready->Id, Ready->Serial);
				if (!Battle.Validate(Order).empty())
				{
					Out.FirstRefusal = "stuck: " + Refused;
					break;
				}
			}
			Battle.Apply(Order, Report);
		}
		Out.Ticks = Battle.TickCount;
		Out.Winner = Battle.Winner;
		return Out;
	}
}

namespace
{
	// ----------------------------------------------------------- MOVE
	// Movement skills (2026-10-03): the Berserker's Leap Smash lands where it
	// smashes, the Ninja's Shadow Step lands behind its target and strikes its
	// back, and its Smoke Bomb leaves it Vanished. Only when those classes are
	// among the files read.

	/** A ready unit of a class, and an enemy archer before it, on the highlands. */
	FBattle Duel(const std::string& JobId, const FVec2& At, const FVec2& Enemy)
	{
		FBattle Battle;
		Battle.Map.BuildMirrored(HighlandsRows());
		const char* Jobs[2] = { JobId.c_str(), "archer" };
		const FVec2 Spots[2] = { At, Enemy };
		for (int Index = 0; Index < 2; ++Index)
		{
			FUnit Unit;
			Unit.Id = Index;
			Unit.Team = Index;
			Unit.Job = Jobs[Index];
			Unit.Stats = &FindJob(Unit.Job)->Stats;
			Unit.Pos = Spots[Index];
			Battle.Units.push_back(Unit);
		}
		Battle.Start(5);
		for (FUnit& Unit : Battle.Units)
		{
			Unit.bReady = Unit.Id == 0;
			Unit.Clock = 1000;
		}
		// The archer looks away from the Ninja's side, so "behind" is the far side.
		Battle.Units[1].Facing = FVec2(1.0f, 0.0f);
		return Battle;
	}

	/** The slot of a class's ability by id, or -1. */
	int SlotOf(const FBattle& Battle, const std::string& AbilityId)
	{
		const FUnit& Unit = Battle.Units[0];
		for (int Slot = 0; Slot < AbilitySlots; ++Slot)
		{
			const FAbility* Ability = Unit.Ability(Slot);
			if (Ability && Ability->Id == AbilityId)
			{
				return Slot;
			}
		}
		return -1;
	}

	/** Uses it, then lets two seconds pass for a cast to go off. */
	std::string UseAndWait(FBattle& Battle, int Slot, const FVec2& Target, int Follow)
	{
		const FOrder Order = FOrder::MakeUseAbility(0, Battle.Units[0].Serial, Slot, Target, Follow);
		const std::string Refused = Battle.Validate(Order);
		if (!Refused.empty())
		{
			return Refused;
		}
		FTickReport Report;
		Battle.Apply(Order, Report);
		Battle.Apply(FOrder::MakeAdvance(20), Report);
		return std::string();
	}

	int CheckMovementSkills()
	{
		int Checks = 0;
		const FVec2 Home(4.75f, 8.75f);
		const FVec2 Foe(8.75f, 8.75f);
		if (FindJob("berserker"))
		{
			FBattle Battle = Duel("berserker", Home, Foe);
			const int Slot = SlotOf(Battle, "berserker_leap_smash");
			const FVec2 Aim(8.25f, 8.75f);
			++Checks;
			const std::string Refused = Slot < 0 ? std::string("no Leap Smash") : UseAndWait(Battle, Slot, Aim, -1);
			const FVec2 Landed = Battle.Units[0].Pos;
			if (!Refused.empty())
			{
				Fail("Leap Smash refused: " + Refused);
			}
			else if (Landed.DistanceTo(Home) < 2.0f || Landed.DistanceTo(Aim) > 1.6f || Landed.DistanceTo(Foe) < 0.4f)
			{
				Fail("Leap Smash did not land the Berserker by where it smashed");
			}
		}
		if (FindJob("ninja"))
		{
			FBattle Battle = Duel("ninja", Home, Foe);
			const int Step = SlotOf(Battle, "ninja_shadow_step");
			++Checks;
			const std::string Refused = Step < 0 ? std::string("no Shadow Step") : UseAndWait(Battle, Step, Foe, 1);
			const FVec2 Landed = Battle.Units[0].Pos;
			const FVec2 Away = Landed - Battle.Units[1].Pos;
			if (!Refused.empty())
			{
				Fail("Shadow Step refused: " + Refused);
			}
			else if (Away.Length() > 1.7f || Away.Length() < 0.5f || Away.Dot(Battle.Units[1].Facing) > -0.5f)
			{
				Fail("Shadow Step did not land the Ninja behind its target");
			}

			FBattle Smoke = Duel("ninja", Home, Foe);
			const int Bomb = SlotOf(Smoke, "ninja_smoke_bomb");
			++Checks;
			const std::string BombRefused = Bomb < 0 ? std::string("no Smoke Bomb") : UseAndWait(Smoke, Bomb, Home, 0);
			if (!BombRefused.empty())
			{
				Fail("Smoke Bomb refused: " + BombRefused);
			}
			else if (!Smoke.Units[0].HasStatus("veil") || Smoke.CanSeeUnit(1, Smoke.Units[0]))
			{
				Fail("Smoke Bomb did not leave the Ninja Vanished, unseen by the other side");
			}
		}
		return Checks;
	}
}

int main(int ArgCount, char** Args)
{
	if (ArgCount < 2)
	{
		std::printf("usage: SimClassTest <class dir>\n");
		return 2;
	}
	const std::filesystem::path Dir = Args[1];

	// --------------------------------------------------------------- READ
	std::vector<std::filesystem::path> Files;
	for (const auto& Entry : std::filesystem::directory_iterator(Dir))
	{
		const std::string Name = Entry.path().filename().string();
		if (Name.size() > 13 && Name.compare(Name.size() - 13, 13, ".tmclass.json") == 0)
		{
			Files.push_back(Entry.path());
		}
	}
	std::sort(Files.begin(), Files.end());
	std::vector<std::string> Loaded;
	std::string AnyValid;
	for (const std::filesystem::path& File : Files)
	{
		const std::string Text = ReadAll(File);
		FJobDef Job;
		std::vector<FAbility> Abilities;
		const std::string Problems = ReadClassFile(Text, Job, Abilities);
		if (!Problems.empty())
		{
			Fail(File.filename().string() + ": " + Problems);
			continue;
		}
		const std::string Refused = RegisterJob(Job, Abilities);
		if (!Refused.empty())
		{
			Fail(File.filename().string() + ": " + Refused);
			continue;
		}
		Loaded.push_back(Job.Id);
		if (AnyValid.empty())
		{
			AnyValid = Text;
		}
	}
	std::printf("read %zu class files; the game now knows %zu classes\n", Loaded.size(), AllJobs().size());
	if (Loaded.size() != Files.size() || Files.empty())
	{
		Fail("not every class file loaded");
	}
	if (AllJobs().size() != 6 + Loaded.size())
	{
		Fail("the listing should be the six built-in classes plus every loaded one");
	}

	// ------------------------------------------------------------- REFUSE
	if (!AnyValid.empty())
	{
		struct FProbe
		{
			const char* What;
			std::string Text;
		};
		// Each of these is a valid file with one thing broken, loaded under a
		// fresh id so that being refused cannot be put down to the id.
		const std::string Fresh = Mutated(AnyValid, "\"id\": \"" + Loaded.front() + "\"", "\"id\": \"probe_class\"");
		const FProbe Probes[] =
		{
			{ "an unknown key", Mutated(Fresh, "\"look\"", "\"lok\"") },
			{ "a stat out of range", Mutated(Fresh, "\"hp\": ", "\"hp\": 999, \"was_hp\": ") },
			{ "the wrong format", Mutated(Fresh, "tactical-masters-class", "astra") },
			{ "a newer version", Mutated(Fresh, "\"version\": 1", "\"version\": 2") },
			{ "not JSON at all", Fresh.substr(0, Fresh.size() / 2) },
			{ "an id already taken", AnyValid },
			// The particle effect an ability may name is read strictly too: the game
			// hands the path to Unreal's asset loader, so only a path under /Game.
			{ "an effect outside /Game", Mutated(Fresh, "\"kind\": ", "\"vfx\": {\"system\": \"/Game/../Engine/X.X\"}, \"kind\": ") },
			{ "an effect with an unknown key", Mutated(Fresh, "\"kind\": ", "\"vfx\": {\"system\": \"/Game/A/B.B\", \"colour\": \"red\"}, \"kind\": ") },
			{ "an effect played nowhere", Mutated(Fresh, "\"kind\": ", "\"vfx\": {\"system\": \"/Game/A/B.B\", \"at\": \"sky\"}, \"kind\": ") },
			{ "an effect far too big", Mutated(Fresh, "\"kind\": ", "\"vfx\": {\"system\": \"/Game/A/B.B\", \"scale\": 50}, \"kind\": ") },
			{ "a motion no body knows", Mutated(Fresh, "\"kind\": ", "\"anim\": \"backflip\", \"kind\": ") },
		};
		int Refused = 0;
		for (const FProbe& Probe : Probes)
		{
			if (Probe.Text.empty())
			{
				Fail(std::string("could not build the probe for ") + Probe.What);
				continue;
			}
			if (LoadClassFile(Probe.Text).empty())
			{
				Fail(std::string("a class file with ") + Probe.What + " was accepted");
				ForgetLoadedJobs();
				return 1;
			}
			++Refused;
		}
		std::printf("refused %d of %zu broken class files, each with a reason\n", Refused, sizeof(Probes) / sizeof(Probes[0]));

		// And a well-formed effect is read as written, with where and how big
		// defaulting to on the targets at full size.
		const std::string WithVfx = Mutated(Fresh, "\"kind\": ",
			"\"vfx\": {\"system\": \"/Game/FreeParticle_SoftTofu/Niagara/NS_leaf.NS_leaf\"}, \"kind\": ");
		FJobDef VfxJob;
		std::vector<FAbility> VfxAbilities;
		const std::string VfxProblems = ReadClassFile(WithVfx, VfxJob, VfxAbilities);
		if (!VfxProblems.empty() || VfxAbilities.empty()
			|| VfxAbilities[0].VfxSystem != "/Game/FreeParticle_SoftTofu/Niagara/NS_leaf.NS_leaf"
			|| VfxAbilities[0].VfxAt != "targets" || VfxAbilities[0].VfxScale != 1.0f)
		{
			Fail("a class file naming a particle effect was not read as written: " + VfxProblems);
		}
		else
		{
			std::printf("read an ability's particle effect as written\n");
		}

		// A motion named in the file is the one played; every other ability of
		// every class gets one worked out from what it is.
		FJobDef AnimJob;
		std::vector<FAbility> AnimAbilities;
		const std::string AnimProblems = ReadClassFile(Mutated(Fresh, "\"kind\": ", "\"anim\": \"heavy\", \"kind\": "), AnimJob, AnimAbilities);
		if (!AnimProblems.empty() || AnimAbilities.empty() || MotionOf(AnimAbilities[0], 0) != "heavy")
		{
			Fail("a class file naming a motion was not read as written: " + AnimProblems);
		}
		std::map<std::string, int> Motions;
		for (const FJobDef* Job : AllJobs())
		{
			for (int Slot = 0; Slot < 4; ++Slot)
			{
				const FAbility* Ability = JobAbility(Job->Id, Slot);
				const std::string Motion = Ability ? MotionOf(*Ability, Slot) : std::string("?");
				if (std::find(AnimMotions().begin(), AnimMotions().end(), Motion) == AnimMotions().end())
				{
					Fail(Job->Id + " slot " + std::to_string(Slot) + " has no motion");
				}
				++Motions[Motion];
			}
		}
		std::printf("every ability of every class has a motion:");
		for (const auto& Each : Motions)
		{
			std::printf(" %s %d", Each.first.c_str(), Each.second);
		}
		std::printf("\n");
	}

	// --------------------------------------------------------------- PLAY
	const auto Start = std::chrono::steady_clock::now();
	int Clean = 0;
	for (const std::string& Id : Loaded)
	{
		const FPlayed Played = PlayOne(Id);
		if (Played.Refused > 0)
		{
			Fail(Id + ": the rules refused " + std::to_string(Played.Refused) + " of " + std::to_string(Played.Orders)
				+ " orders, first: " + Played.FirstRefusal);
		}
		else
		{
			++Clean;
		}
	}
	const double Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - Start).count();
	std::printf("played %zu classes, one battle each, in %.1fs: %d with every order legal\n", Loaded.size(), Seconds, Clean);

	const int BeforeMoves = Failures;
	const int MoveChecks = CheckMovementSkills();
	std::printf("movement skills: %d checks, %s\n", MoveChecks, Failures == BeforeMoves ? "as the rules say" : "WRONG");

	ForgetLoadedJobs();
	if (Failures > 0)
	{
		std::printf("CLASS FILES FAILED (%d problems)\n", Failures);
		return 1;
	}
	std::printf("THE CLASS FILES READ, REFUSE WHAT IS BROKEN, AND PLAY\n");
	return 0;
}
