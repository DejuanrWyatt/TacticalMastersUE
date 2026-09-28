// The classes, read from Tactical Masters' own class files.
//
// Four things are checked, in the order they would go wrong.
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
// MATCH: each class is exactly what the Godot game's own importer made of the
// Astra file it was converted from, as Godot printed it (GodotClassTable.txt,
// from tests/dump_class_table.gd). Numbers are compared after the same narrowing
// to float the rules apply, so "equal" means the rules see the same number.
// Until that table exists this part says so loudly and is not counted as passed.
//
// PLAY: every class fights one battle through the order path -- in a team with
// a knight, an archer and a white mage, against a black mage, a knight, an
// archer and a white mage, as the Godot game's balance tests arrange it -- and
// the rules must refuse none of the computer's orders. A class can read
// perfectly and still be unplayable, an ability nothing can legally aim, and
// that shows up only here.
//
//   SimClassTest <class dir> [GodotClassTable.txt]

#include "SimAI.h"
#include "SimBattle.h"
#include "SimClassFile.h"
#include "SimJson.h"

#include <algorithm>
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

	const char* ShapeOrEmpty(const FAbility& Ability) { return Ability.Shape.c_str(); }

	std::string EffectName(EEffect Effect)
	{
		switch (Effect)
		{
		case EEffect::Damage: return "damage";
		case EEffect::Heal: return "heal";
		case EEffect::Revive: return "revive";
		default: return "support";
		}
	}

	std::string TargetName(ETargetSide Target)
	{
		switch (Target)
		{
		case ETargetSide::Enemy: return "enemy";
		case ETargetSide::Ally: return "ally";
		default: return "ko_ally";
		}
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

	// ------------------------------------------------------------ MATCH

	void CompareWithGodot(const FJson& Godot)
	{
		const FJson* IdJson = Godot.Find("id");
		const std::string Id = IdJson ? IdJson->String : "?";
		const FJobDef* Job = FindJob(Id);
		if (!Job)
		{
			Fail(Id + ": Godot has this class, the files do not");
			return;
		}
		auto Text = [&Godot](const char* Key)
		{
			const FJson* Value = Godot.Find(Key);
			return Value && Value->IsString() ? Value->String : std::string();
		};
		if (Job->Name != Text("name")) { Fail(Id + ".name: " + Job->Name + " vs Godot's " + Text("name")); }
		if (Job->Color != Text("color")) { Fail(Id + ".color: " + Job->Color + " vs Godot's " + Text("color")); }
		if (Job->Look != Text("look")) { Fail(Id + ".look: " + Job->Look + " vs Godot's " + Text("look")); }
		if (Job->Icon != Text("icon")) { Fail(Id + ".icon: " + Job->Icon + " vs Godot's " + Text("icon")); }

		const FJson* Roles = Godot.Find("roles");
		std::vector<std::string> GodotRoles;
		if (Roles)
		{
			for (const FJson& Role : Roles->Array)
			{
				GodotRoles.push_back(Role.String);
			}
		}
		if (GodotRoles != Job->Roles) { Fail(Id + ".roles differ from Godot's"); }

		const FJson* Stats = Godot.Find("stats");
		for (int i = 0; Stats && i < StatCount; ++i)
		{
			const EStat Which = static_cast<EStat>(i);
			const FJson* Value = Stats->Find(StatName(Which));
			if (!Value || static_cast<int>(Value->Number) != Job->Stats.Get(Which))
			{
				Fail(Id + ".stats." + StatName(Which) + " differs from Godot's");
			}
		}

		const FJson* Abilities = Godot.Find("abilities");
		if (!Abilities || Abilities->Array.size() != 4)
		{
			Fail(Id + ": Godot's line has no four abilities");
			return;
		}
		for (int Slot = 0; Slot < 4; ++Slot)
		{
			const FJson& G = Abilities->Array[Slot];
			const FAbility* A = FindAbility(Job->AbilityIds[Slot]);
			const std::string Where = Id + ".abilities[" + std::to_string(Slot) + "]";
			if (!A)
			{
				Fail(Where + ": missing");
				continue;
			}
			auto GText = [&G](const char* Key, const char* Default)
			{
				const FJson* Value = G.Find(Key);
				return Value && Value->IsString() ? Value->String : std::string(Default);
			};
			auto GNumber = [&G](const char* Key, double Default)
			{
				const FJson* Value = G.Find(Key);
				return Value && Value->IsNumber() ? Value->Number : Default;
			};
			// Each number is compared as the rules will hold it.
			auto SameFloat = [](float Ours, double Theirs) { return Ours == static_cast<float>(Theirs); };

			if (A->Id != GText("id", "")) { Fail(Where + ".id: " + A->Id + " vs Godot's " + GText("id", "")); }
			if (A->Name != GText("name", "")) { Fail(Where + ".name differs"); }
			if (A->Desc != GText("desc", "")) { Fail(Where + ".desc differs"); }
			if (A->Kind != GText("kind", "active")) { Fail(Where + ".kind differs"); }
			if (EffectName(A->Effect) != GText("effect", "")) { Fail(Where + ".effect differs"); }
			if ((A->Scale == EScale::Att ? "att" : "mag") != GText("scale", "")) { Fail(Where + ".scale differs"); }
			if (TargetName(A->Target) != GText("target", "")) { Fail(Where + ".target differs"); }
			if (std::string(ShapeOrEmpty(*A)) != GText("shape", "")) { Fail(Where + ".shape differs"); }
			if (A->Fx != GText("fx", "")) { Fail(Where + ".fx differs"); }
			if (!SameFloat(A->Power, GNumber("power", 0.0))) { Fail(Where + ".power differs"); }
			if (!SameFloat(A->MinRange, GNumber("min_range", 0.0))) { Fail(Where + ".min_range differs"); }
			if (!SameFloat(A->MaxRange, GNumber("max_range", 0.0))) { Fail(Where + ".max_range differs"); }
			if (!SameFloat(A->Aoe, GNumber("aoe", 0.0))) { Fail(Where + ".aoe differs"); }
			if (!SameFloat(A->Angle, GNumber("angle", 60.0))) { Fail(Where + ".angle differs"); }
			if (!SameFloat(A->Cast, GNumber("cast", 0.0))) { Fail(Where + ".cast differs"); }
			if (A->Channel != static_cast<int>(GNumber("channel", 2.0))) { Fail(Where + ".channel differs"); }
			if (A->Cooldown != static_cast<int>(GNumber("cooldown", 0.0))) { Fail(Where + ".cooldown differs"); }
			if (A->TgChange != static_cast<int>(GNumber("tg", 0.0))) { Fail(Where + ".tg differs"); }

			const FJson* Status = G.Find("status");
			const std::string GodotStatus = Status && Status->Find("id") ? Status->Find("id")->String : std::string();
			const int GodotTurns = Status && Status->Find("turns") ? static_cast<int>(Status->Find("turns")->Number) : 0;
			if (A->StatusId != GodotStatus || A->StatusTurns != GodotTurns) { Fail(Where + ".status differs"); }

			const FJson* Buffs = G.Find("buffs");
			const size_t GodotBuffs = Buffs ? Buffs->Array.size() : 0;
			if (A->Buffs.size() != GodotBuffs)
			{
				Fail(Where + ".buffs differ in number");
			}
			else
			{
				for (size_t b = 0; b < GodotBuffs; ++b)
				{
					const FJson& GB = Buffs->Array[b];
					const FBuff& Ours = A->Buffs[b];
					if (StatName(Ours.Stat) != GB.Find("stat")->String
						|| Ours.Amount != static_cast<int>(GB.Find("amount")->Number)
						|| Ours.Turns != static_cast<int>(GB.Find("turns")->Number))
					{
						Fail(Where + ".buffs[" + std::to_string(b) + "] differs");
					}
				}
			}
		}
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
		// The class in a mixed team against the reference, as check_class.gd and
		// balance.gd arrange it.
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

int main(int ArgCount, char** Args)
{
	if (ArgCount < 2)
	{
		std::printf("usage: SimClassTest <class dir> [GodotClassTable.txt]\n");
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
	}

	// -------------------------------------------------------------- MATCH
	int Matched = 0;
	bool bHaveTable = false;
	if (ArgCount >= 3 && std::filesystem::exists(Args[2]))
	{
		bHaveTable = true;
		std::ifstream Table(Args[2]);
		std::string Line;
		const int Before = Failures;
		while (std::getline(Table, Line))
		{
			if (!Line.empty() && Line.back() == '\r')
			{
				Line.pop_back();
			}
			if (Line.rfind("CLASS ", 0) != 0)
			{
				continue;
			}
			FJson Godot;
			const std::string Bad = ParseJson(Line.substr(6), Godot);
			if (!Bad.empty())
			{
				Fail("a line of the Godot table is not JSON: " + Bad);
				continue;
			}
			CompareWithGodot(Godot);
			++Matched;
		}
		if (Matched != static_cast<int>(Loaded.size()))
		{
			Fail("the Godot table has " + std::to_string(Matched) + " classes, the files " + std::to_string(Loaded.size()));
		}
		std::printf("compared %d classes with what Godot made of them: %s\n", Matched,
			Failures == Before ? "every field agrees" : "they differ");
	}
	else
	{
		std::printf("NOT CHECKED AGAINST GODOT: GodotClassTable.txt is missing. Run the Godot project's\n"
			"  tests/dump_class_table.gd and put its output in Tests/ -- until then these classes\n"
			"  are only known to be well formed, not known to be the classes Godot plays.\n");
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

	ForgetLoadedJobs();
	if (Failures > 0)
	{
		std::printf("CLASS FILES FAILED (%d problems)\n", Failures);
		return 1;
	}
	std::printf(bHaveTable ? "THE CLASS FILES ARE THE CLASSES GODOT PLAYS\n"
		: "THE CLASS FILES READ AND PLAY (not yet compared with Godot)\n");
	return 0;
}
