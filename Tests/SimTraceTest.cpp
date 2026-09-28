// Whole battles, replayed from Godot's own orders.
//
// Every other parity test pins down one rule on a board set up for it. This one
// is the rules together, as a battle brings them about: statuses ticking as a
// turn begins, auras reaching whoever stands near, toggles switched on and off,
// casts landing, units falling, one after another over a few thousand ticks.
//
// Godot's computer player chose the orders (tests/dump_battle_trace.gd in the
// Godot project wrote them, with the state after each, into
// GodotBattleTrace.txt). The port replays exactly those orders rather than
// choosing its own, so the two cannot drift apart over a choice both consider
// equally good -- the one known way their computer players differ. Any
// difference here is a rule.
//
// Each order must be one the port's rules accept, and after each order and
// each run of ticks every unit must be in the state Godot printed: position,
// facing, health, gauge, turn, meter, countdown, statuses, buffs, casts,
// cooldowns, toggles -- and the dice and the time spent holding the middle
// must have reached the same point. Two of the battles are fought with the
// setup screen's other rules on (holding the middle, a time limit), so how a
// battle is won is compared as well as how it is fought. The
// first difference in a battle is reported with both lines, and that battle
// stops there, since everything after it would differ too.

#include "SimAbility.h"
#include "SimAI.h"
#include "SimBattle.h"
#include "SimClassFile.h"
#include "SimMap.h"
#include "SimOrder.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
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
		++Failures;
		std::printf("FAIL: %s\n", What.c_str());
	}

	std::string ReadAll(const std::filesystem::path& Path)
	{
		std::ifstream In(Path, std::ios::binary);
		std::stringstream Buffer;
		Buffer << In.rdbuf();
		return Buffer.str();
	}

	std::vector<std::string> Split(const std::string& Text, char By)
	{
		std::vector<std::string> Out;
		std::string Part;
		std::stringstream In(Text);
		while (std::getline(In, Part, By))
		{
			Out.push_back(Part);
		}
		return Out;
	}

	/** "key=value" words after the first, as a map; the first word is the line's kind. */
	std::map<std::string, std::string> Fields(const std::string& Line)
	{
		std::map<std::string, std::string> Out;
		for (const std::string& Word : Split(Line, ' '))
		{
			const size_t Equals = Word.find('=');
			if (Equals != std::string::npos)
			{
				Out[Word.substr(0, Equals)] = Word.substr(Equals + 1);
			}
		}
		return Out;
	}

	FVec2 VecOf(const std::string& Text)
	{
		const std::vector<std::string> Parts = Split(Text, ',');
		return Parts.size() == 2 ? FVec2(std::strtof(Parts[0].c_str(), nullptr), std::strtof(Parts[1].c_str(), nullptr)) : FVec2();
	}

	std::string Vec(const FVec2& V)
	{
		char Buffer[64];
		std::snprintf(Buffer, sizeof(Buffer), "%.6f,%.6f", static_cast<double>(V.X), static_cast<double>(V.Y));
		return Buffer;
	}

	/** One unit, in exactly the shape dump_battle_trace.gd's _unit_line prints it. */
	std::string UnitLine(const FUnit& Unit)
	{
		std::string Statuses;
		for (const FStatus& Status : Unit.Statuses)
		{
			Statuses += (Statuses.empty() ? "" : "|") + Status.Id + ":" + std::to_string(Status.Turns) + ":"
				+ std::to_string(Status.Amount) + ":" + std::to_string(Status.By);
		}
		std::string Buffs;
		for (const FBuff& Buff : Unit.Buffs)
		{
			Buffs += (Buffs.empty() ? "" : "|") + std::string(StatName(Buff.Stat)) + ":" + std::to_string(Buff.Amount) + ":"
				+ std::to_string(Buff.Turns) + ":" + (Buff.Aura.empty() ? "-" : Buff.Aura);
		}
		std::string Toggled;
		for (int Slot = 0; Slot < 4; ++Slot)
		{
			Toggled += Unit.Toggled[Slot] ? "1" : "0";
		}
		std::string Casting = "-";
		if (Unit.IsCasting())
		{
			Casting = std::to_string(Unit.Casting.Slot) + ":" + Vec(Unit.Casting.Target) + ":" + std::to_string(Unit.Casting.FollowId)
				+ ":" + std::to_string(Unit.Casting.Ticks) + ":" + std::to_string(Unit.Casting.Total);
		}
		std::string Channeling = "-";
		if (Unit.IsChanneling())
		{
			Channeling = std::to_string(Unit.Channeling.Slot) + ":" + Vec(Unit.Channeling.Target) + ":" + std::to_string(Unit.Channeling.Turns);
		}
		char Head[512];
		std::snprintf(Head, sizeof(Head),
			"U %d pos=%s facing=%s hp=%d tg=%d serial=%d ult=%d ko=%d clock=%d ready=%d moved=%d acted=%d hustling=%d unharmed=%d cd=%d,%d,%d,%d toggled=%s",
			Unit.Id, Vec(Unit.Pos).c_str(), Vec(Unit.Facing).c_str(), Unit.Hp, Unit.Tg, Unit.Serial, Unit.Ult, Unit.KoTicks, Unit.Clock,
			Unit.bReady ? 1 : 0, Unit.bMoved ? 1 : 0, Unit.bActed ? 1 : 0, Unit.bHustling ? 1 : 0, Unit.UnharmedTurns,
			Unit.Cooldowns[0], Unit.Cooldowns[1], Unit.Cooldowns[2], Unit.Cooldowns[3], Toggled.c_str());
		return std::string(Head) + " casting=" + Casting + " channeling=" + Channeling
			+ " statuses=" + (Statuses.empty() ? "-" : Statuses) + " buffs=" + (Buffs.empty() ? "-" : Buffs);
	}

	/**
	 * Whether two printed values agree. Numbers are compared as numbers, to the
	 * six places both sides print, so the two languages' ways of writing -0 or
	 * rounding the last digit cannot pass for a difference in the rules.
	 */
	bool Agree(const std::string& A, const std::string& B)
	{
		if (A == B)
		{
			return true;
		}
		const std::vector<std::string> PartsA = Split(A, ',');
		const std::vector<std::string> PartsB = Split(B, ',');
		if (PartsA.size() != PartsB.size())
		{
			return false;
		}
		for (size_t i = 0; i < PartsA.size(); ++i)
		{
			char* EndA = nullptr;
			char* EndB = nullptr;
			const double NumberA = std::strtod(PartsA[i].c_str(), &EndA);
			const double NumberB = std::strtod(PartsB[i].c_str(), &EndB);
			if (*EndA != '\0' || *EndB != '\0' || std::fabs(NumberA - NumberB) > 1.5e-6)
			{
				return false;
			}
		}
		return true;
	}

	/** The first field in which two unit lines differ, or empty when they agree. */
	std::string Differs(const std::string& Godot, const std::string& Port)
	{
		const std::map<std::string, std::string> Want = Fields(Godot);
		const std::map<std::string, std::string> Have = Fields(Port);
		for (const auto& Field : Want)
		{
			const auto Found = Have.find(Field.first);
			if (Found == Have.end() || !Agree(Field.second, Found->second))
			{
				return Field.first;
			}
		}
		return std::string();
	}

	const FVec2 BlueSpawns[4] = { FVec2(2.75f, 4.75f), FVec2(0.75f, 8.75f), FVec2(4.75f, 6.75f), FVec2(2.75f, 10.75f) };

	/** Two sides on the default map, as MapData.build deals them. */
	bool Deal(FBattle& Battle, const std::vector<std::string>& Blue, const std::vector<std::string>& Red, uint64_t Seed,
		double CaptureSeconds, double BattleSeconds)
	{
		Battle.Tuning.CaptureSeconds = CaptureSeconds;
		Battle.Tuning.BattleSeconds = BattleSeconds;
		Battle.Map.BuildMirrored(HighlandsRows());
		const FVec2 Size = Battle.Map.SizeMeters();
		for (int Index = 0; Index < 8; ++Index)
		{
			const std::vector<std::string>& Side = Index < 4 ? Blue : Red;
			if (Side.size() != 4 || !FindJob(Side[Index % 4]))
			{
				Fail("a class the trace names is not known: " + (Side.size() == 4 ? Side[Index % 4] : std::string("?")));
				return false;
			}
			FUnit Unit;
			Unit.Id = Index;
			Unit.Team = Index < 4 ? 0 : 1;
			Unit.Job = Side[Index % 4];
			Unit.Stats = &FindJob(Unit.Job)->Stats;
			const FVec2 Spot = BlueSpawns[Index % 4];
			Unit.Pos = Index < 4 ? Spot : FVec2(Size.X - Spot.X, Size.Y - Spot.Y);
			Battle.Units.push_back(Unit);
		}
		Battle.Start(Seed);
		return true;
	}

	FOrder OrderOf(const std::string& Line)
	{
		const std::vector<std::string> Words = Split(Line, ' ');
		const std::string Type = Words.size() > 1 ? Words[1] : std::string();
		std::map<std::string, std::string> F = Fields(Line);
		const int Unit = std::atoi(F["unit"].c_str());
		const int Serial = std::atoi(F["serial"].c_str());
		if (Type == "move")
		{
			return FOrder::MakeMove(Unit, Serial, VecOf(F["to"]), F["sprint"] == "1");
		}
		if (Type == "ability")
		{
			return FOrder::MakeUseAbility(Unit, Serial, std::atoi(F["slot"].c_str()), VecOf(F["target"]),
				F.count("follow") ? std::atoi(F["follow"].c_str()) : -1);
		}
		return FOrder::MakeEndTurn(Unit, Serial);
	}
}

int main(int ArgCount, char** Args)
{
	if (ArgCount < 3)
	{
		std::printf("usage: SimTraceTest <class dir> <GodotBattleTrace.txt>\n");
		return 2;
	}
	if (!std::filesystem::exists(Args[2]))
	{
		std::printf("NOT CHECKED AGAINST GODOT: GodotBattleTrace.txt is missing. Run the Godot project's\n"
			"  tests/dump_battle_trace.gd and put its output in Tests/ -- until then whole battles\n"
			"  (auras, toggles, statuses ticking) are only known to agree with themselves.\n");
		return 0;
	}

	// The classes the battles are fought with.
	for (const auto& Entry : std::filesystem::directory_iterator(Args[1]))
	{
		const std::string Name = Entry.path().filename().string();
		if (Name.size() > 13 && Name.compare(Name.size() - 13, 13, ".tmclass.json") == 0)
		{
			const std::string Problem = LoadClassFile(ReadAll(Entry.path()));
			if (!Problem.empty())
			{
				Fail(Name + ": " + Problem);
			}
		}
	}

	std::ifstream Trace(Args[2]);
	std::string Line;
	FBattle Battle;
	int BattleNumber = 0;
	bool bFollowing = false;   // still in step with Godot in this battle
	int Checked = 0;
	int Orders = 0;
	int Agreed = 0;
	int Battles = 0;
	std::string Version;
	std::string LastOrder;
	std::vector<std::string> Expected;  // the U lines of the STATE being read
	std::string ExpectedState;

	// Compares the port with the STATE block just read, once it is complete.
	auto Compare = [&]()
	{
		if (!bFollowing || ExpectedState.empty())
		{
			return;
		}
		++Checked;
		std::map<std::string, std::string> Want = Fields(ExpectedState);
		char Have[192];
		std::snprintf(Have, sizeof(Have), "tick=%d winner=%d capture=%d,%d rng=%lld", Battle.TickCount, Battle.Winner,
			Battle.CaptureTicks[0], Battle.CaptureTicks[1], static_cast<long long>(Battle.Rng.GetState()));
		const std::map<std::string, std::string> HaveFields = Fields(Have);
		std::string Wrong;
		for (const char* Key : { "tick", "winner", "capture", "rng" })
		{
			if (Want[Key] != HaveFields.at(Key))
			{
				Wrong = std::string(Key) + ": Godot " + Want[Key] + ", port " + HaveFields.at(Key);
				break;
			}
		}
		for (size_t i = 0; Wrong.empty() && i < Expected.size(); ++i)
		{
			const std::string Port = i < Battle.Units.size() ? UnitLine(Battle.Units[i]) : std::string("(no such unit)");
			const std::string Field = Differs(Expected[i], Port);
			if (!Field.empty())
			{
				Wrong = "unit " + std::to_string(i) + " " + Field + "\n    Godot " + Expected[i] + "\n    port  " + Port;
			}
		}
		if (!Wrong.empty())
		{
			Fail("battle " + std::to_string(BattleNumber) + " parts from Godot at tick " + Want["tick"]
				+ (LastOrder.empty() ? std::string() : " after " + LastOrder) + ": " + Wrong);
			bFollowing = false;
		}
		ExpectedState.clear();
		Expected.clear();
	};

	while (std::getline(Trace, Line))
	{
		if (!Line.empty() && Line.back() == '\r')
		{
			Line.pop_back();
		}
		if (Line.rfind("U ", 0) == 0)
		{
			Expected.push_back(Line);
			continue;
		}
		// Anything else ends a STATE block.
		Compare();
		if (Line.rfind("GODOT ", 0) == 0)
		{
			Version = Line.substr(6);
		}
		else if (Line.rfind("BATTLE ", 0) == 0)
		{
			std::map<std::string, std::string> F = Fields(Line);
			BattleNumber = std::atoi(Split(Line, ' ')[1].c_str());
			Battle = FBattle();
			bFollowing = Deal(Battle, Split(F["blue"], ','), Split(F["red"], ','), std::strtoull(F["seed"].c_str(), nullptr, 10),
				std::strtod(F["capture"].c_str(), nullptr), std::strtod(F["time"].c_str(), nullptr));
			LastOrder.clear();
			++Battles;
		}
		else if (Line.rfind("ADVANCE ", 0) == 0 && bFollowing)
		{
			// One tick at a time, as Godot's loop gave them.
			const int Ticks = std::atoi(Line.c_str() + 8);
			for (int i = 0; i < Ticks; ++i)
			{
				FTickReport Report;
				Battle.Apply(FOrder::MakeAdvance(1), Report);
			}
			LastOrder = Line;
		}
		else if (Line.rfind("ORDER ", 0) == 0 && bFollowing)
		{
			const FOrder Order = OrderOf(Line);
			const std::string Refused = Battle.Validate(Order);
			LastOrder = Line;
			++Orders;
			if (!Refused.empty())
			{
				Fail("battle " + std::to_string(BattleNumber) + ": the port refuses an order Godot carried out, at tick "
					+ std::to_string(Battle.TickCount) + ": " + Line + " -- \"" + Refused + "\"");
				bFollowing = false;
				continue;
			}
			FTickReport Report;
			Battle.Apply(Order, Report);
		}
		else if (Line.rfind("STATE ", 0) == 0)
		{
			ExpectedState = Line;
		}
		else if (Line.rfind("END ", 0) == 0 && bFollowing)
		{
			++Agreed;
		}
	}
	Compare();

	std::printf("%d battles from Godot %s: %d orders, %d states compared\n", Battles, Version.c_str(), Orders, Checked);
	std::printf("%d of %d battles agree with Godot from the first tick to the last\n", Agreed, Battles);
	if (Battles == 0)
	{
		Fail("the trace has no battles in it");
	}
	if (Failures == 0)
	{
		std::printf("WHOLE BATTLES PLAY IN UNREAL AS THEY PLAY IN GODOT\n");
	}
	return Failures == 0 ? 0 : 1;
}
