// Whole battles, replayed from their recorded orders (Baselines/BattleTrace.txt).
//
// Every other rules test pins down one rule on a board set up for it. This one
// is the rules together, as a battle brings them about: statuses ticking as a
// turn begins, auras reaching whoever stands near, toggles switched on and off,
// casts landing, units falling, one after another over a few thousand ticks.
//
// The computer player is asked for every order too, before the recorded one
// is applied, and must ask for the same one -- at hard, and at medium and easy,
// whose mistakes are random draws from their own generator.
//
// The recorded battles (first recorded from the Godot version, which the rules
// were ported from) go on with the recorded order, whatever the computer player
// asked for, so a difference in a rule and a difference in a choice are told
// apart: each is reported as what it is.
//
// Each order must be one the rules accept, and after each order and each run of
// ticks every unit must be in the state recorded: position,
// facing, health, gauge, turn, meter, countdown, statuses, buffs, casts,
// cooldowns, toggles -- and the dice and the time spent holding the middle
// must have reached the same point. Two of the battles are fought with the
// setup screen's other rules on (holding the middle, a time limit), so how a
// battle is won is compared as well as how it is fought. The
// first difference in a battle is reported with both lines, and that battle
// stops there, since everything after it would differ too.
//
// With --rebaseline every battle is played again from its first line -- the
// same sides, seed and setup, the same scripted placings -- with the rules and
// the computer player as they are now, and the trace is written again
// (Baseline.h). A line the rules still agree with is kept as it was written.
//
//   SimTraceTest <class dir> [Baselines/BattleTrace.txt] [--rebaseline]
//   (the classes come from Baselines/TraceClasses.txt beside the trace; the class
//   directory is used only when that file is missing)

#include "Baseline.h"
#include "SimAbility.h"
#include "SimAI.h"
#include "SimBattle.h"
#include "SimClassFile.h"
#include "SimMap.h"
#include "SimOrder.h"

#include <algorithm>
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

	/** One unit, in exactly the shape the trace writes one. */
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
	std::string Differs(const std::string& Was, const std::string& Port)
	{
		const std::map<std::string, std::string> Want = Fields(Was);
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
		double CaptureSeconds, double BattleSeconds, double PlanningSeconds)
	{
		Battle.Tuning.CaptureSeconds = CaptureSeconds;
		Battle.Tuning.BattleSeconds = BattleSeconds;
		Battle.Tuning.PlanningSeconds = PlanningSeconds;
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

	/** An order, in the shape the trace writes one (after the first word). */
	std::string OrderText(const FOrder& Order)
	{
		const char* Type = Order.Type == EOrderType::Move ? "move"
			: Order.Type == EOrderType::UseAbility ? "ability" : "end_turn";
		std::string Out = std::string(Type) + " unit=" + std::to_string(Order.UnitId) + " serial=" + std::to_string(Order.Serial);
		if (Order.Type == EOrderType::Move)
		{
			Out += " to=" + Vec(Order.To) + " sprint=" + (Order.bSprint ? "1" : "0");
		}
		if (Order.Type == EOrderType::UseAbility)
		{
			Out += " slot=" + std::to_string(Order.Slot) + " target=" + Vec(Order.Target) + " follow=" + std::to_string(Order.Follow);
		}
		return Out;
	}

	/**
	 * Whether the computer player asked for the recorded order. Recorded
	 * orders leave out what they do not need (a walk only says sprint when it
	 * might be one), so each field recorded must agree, and a sprint the record
	 * does not mention must not be one.
	 */
	bool SameOrder(const std::string& Was, const FOrder& Port)
	{
		const std::string Mine = OrderText(Port);
		if (Split(Was, ' ').size() < 2 || Split(Was, ' ')[1] != Split(Mine, ' ')[0])
		{
			return false;
		}
		std::map<std::string, std::string> Want = Fields(Was);
		const std::map<std::string, std::string> Have = Fields(Mine);
		Want.erase("refused");
		if (!Want.count("sprint") && Port.bSprint)
		{
			return false;
		}
		for (const auto& Field : Want)
		{
			const auto Found = Have.find(Field.first);
			if (Found == Have.end() || !Agree(Field.second, Found->second))
			{
				return false;
			}
		}
		return true;
	}

	/** The first unit waiting on an order: alive, ready and not stunned, in id order. */
	const FUnit* FirstOrderable(const FBattle& Battle)
	{
		for (const FUnit& Unit : Battle.Units)
		{
			if (Unit.IsAlive() && Unit.bReady && !Unit.IsStunned())
			{
				return &Unit;
			}
		}
		return nullptr;
	}

	FOrder OrderOf(const std::string& Line)
	{
		const std::vector<std::string> Words = Split(Line, ' ');
		const std::string Type = Words.size() > 1 ? Words[1] : std::string();
		std::map<std::string, std::string> F = Fields(Line);
		const int Unit = std::atoi(F["unit"].c_str());
		const int Serial = std::atoi(F["serial"].c_str());
		if (Type == "place")
		{
			return FOrder::MakePlace(Unit, Serial, VecOf(F["to"]));
		}
		if (Type == "ready")
		{
			return FOrder::MakeReady(std::atoi(F["team"].c_str()));
		}
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

namespace
{
	/**
	 * A row of Baselines/TraceClasses.txt as a class file: each class in the file
	 * format's own words, compactly, with three differences -- the file it came
	 * from and the role as written, which a class file does not have, and its
	 * colour without the leading #. An empty icon is the class's own.
	 */
	std::string TableRowAsClassFile(std::string Json)
	{
		auto Drop = [&Json](const std::string& Key, bool bOnlyIfEmpty)
		{
			const std::string Needle = "\"" + Key + "\":\"";
			const size_t At = Json.find(Needle);
			if (At == std::string::npos)
			{
				return;
			}
			const size_t Close = Json.find('"', At + Needle.size());
			if (Close == std::string::npos || (bOnlyIfEmpty && Close != At + Needle.size()))
			{
				return;
			}
			size_t Stop = Close + 1;
			if (Stop < Json.size() && Json[Stop] == ',')
			{
				++Stop;
			}
			else if (At > 0 && Json[At - 1] == ',')
			{
				Json.erase(At - 1, Stop - At + 1);
				return;
			}
			Json.erase(At, Stop - At);
		};
		Drop("file", false);
		Drop("role_written", false);
		Drop("icon", true);
		const std::string Colour = "\"color\":\"";
		const size_t At = Json.find(Colour);
		if (At != std::string::npos && Json.compare(At + Colour.size(), 1, "#") != 0)
		{
			Json.insert(At + Colour.size(), "#");
		}
		return "{\"format\":\"tactical-masters-class\",\"version\":1," + Json.substr(1);
	}
}

namespace
{
	// ----------------------------------------------------- playing a battle again
	// (--rebaseline): the loop the trace records, with the rules and the computer
	// player as they are now.

	/** An order as the trace writes one: a walk says sprint only when it is one. */
	std::string TraceOrderText(const FOrder& Order)
	{
		std::string Out = OrderText(Order);
		const std::string NoSprint = " sprint=0";
		const size_t At = Out.find(NoSprint);
		if (At != std::string::npos)
		{
			Out.erase(At, NoSprint.size());
		}
		return Out;
	}

	/** The STATE line, then a U line for every unit. */
	void WriteState(const FBattle& Battle, std::vector<std::string>& Out)
	{
		char Head[256];
		std::snprintf(Head, sizeof(Head), "STATE tick=%d winner=%d capture=%d,%d planning=%d done=%d,%d rng=%lld", Battle.TickCount, Battle.Winner,
			Battle.CaptureTicks[0], Battle.CaptureTicks[1], Battle.PlanningTicks, Battle.PlanningDone[0] ? 1 : 0,
			Battle.PlanningDone[1] ? 1 : 0, static_cast<long long>(Battle.Rng.GetState()));
		Out.push_back(Head);
		for (const FUnit& Unit : Battle.Units)
		{
			Out.push_back(UnitLine(Unit));
		}
	}

	/**
	 * One battle played again from its BATTLE line and the scripted orders it
	 * recorded (the placings and readying of a planning phase): every chosen
	 * order is the computer player's now. False, and said, if the rules refuse
	 * a scripted order.
	 */
	bool PlayAgain(const std::string& Header, const std::vector<std::string>& Scripted, FAIPlayer& Computer, std::vector<std::string>& Out)
	{
		std::map<std::string, std::string> F = Fields(Header);
		FBattle Battle;
		Computer.SetDifficulty((F.count("ai") ? F["ai"] : std::string("hard")).c_str());
		Computer.Rng.Seed(std::strtoull(F["seed"].c_str(), nullptr, 10));
		if (!Deal(Battle, Split(F["blue"], ','), Split(F["red"], ','), std::strtoull(F["seed"].c_str(), nullptr, 10),
			std::strtod(F["capture"].c_str(), nullptr), std::strtod(F["time"].c_str(), nullptr), std::strtod(F["planning"].c_str(), nullptr)))
		{
			return false;
		}
		Out.push_back(Header);
		WriteState(Battle, Out);
		for (const std::string& Line : Scripted)
		{
			const FOrder Order = OrderOf(Line);
			if (Order.Type == EOrderType::Place)
			{
				std::string Nodes;
				if (const FUnit* Unit = Battle.FindUnit(Order.UnitId))
				{
					for (const FNode& Node : Battle.PlaceableNodes(*Unit))
					{
						Nodes += (Nodes.empty() ? "" : "|") + std::to_string(Node.X) + "," + std::to_string(Node.Y);
					}
				}
				Out.push_back("PLACEABLE unit=" + std::to_string(Order.UnitId) + " nodes=" + (Nodes.empty() ? std::string("-") : Nodes));
			}
			Out.push_back(Line);
			const std::string Refused = Battle.Validate(Order);
			if (!Refused.empty())
			{
				Fail(Header + ": the rules now refuse the scripted " + Line + " -- \"" + Refused + "\"");
				return false;
			}
			FTickReport Report;
			Battle.Apply(Order, Report);
			WriteState(Battle, Out);
		}
		// A battle that runs this long has stopped meaning anything.
		const int Limit = 200000;
		while (Battle.Winner == -1 && Battle.TickCount < Limit)
		{
			if (const FUnit* Ready = FirstOrderable(Battle))
			{
				FOrder Order = Computer.NextCommand(Battle, *Ready);
				if (!Battle.Validate(Order).empty())
				{
					// Asked for something the rules refuse: the turn is ended instead.
					Out.push_back("WANTED ORDER " + TraceOrderText(Order));
					Order = FOrder::MakeEndTurn(Ready->Id, Ready->Serial);
					Out.push_back("ORDER " + TraceOrderText(Order) + " refused=1");
				}
				else
				{
					Out.push_back("ORDER " + TraceOrderText(Order));
				}
				FTickReport Report;
				Battle.Apply(Order, Report);
				WriteState(Battle, Out);
				continue;
			}
			int Ticks = 0;
			while (Battle.Winner == -1 && !FirstOrderable(Battle) && Battle.TickCount < Limit)
			{
				FTickReport Report;
				Battle.Apply(FOrder::MakeAdvance(1), Report);
				++Ticks;
			}
			Out.push_back("ADVANCE " + std::to_string(Ticks));
			WriteState(Battle, Out);
		}
		Out.push_back("END ticks=" + std::to_string(Battle.TickCount) + " winner=" + std::to_string(Battle.Winner));
		return true;
	}

	/** Whether two lines of the trace say the same thing, however each was written. */
	bool SameLine(const std::string& A, const std::string& B)
	{
		if (A == B)
		{
			return true;
		}
		const std::vector<std::string> WordsA = Split(A, ' ');
		const std::vector<std::string> WordsB = Split(B, ' ');
		if (WordsA.empty() || WordsB.empty() || WordsA[0] != WordsB[0])
		{
			return false;
		}
		if (WordsA[0] == "ORDER" && (WordsA.size() < 2 || WordsB.size() < 2 || WordsA[1] != WordsB[1]))
		{
			return false;
		}
		std::map<std::string, std::string> FieldsA = Fields(A);
		std::map<std::string, std::string> FieldsB = Fields(B);
		// A walk that is not a sprint may be written with sprint=0 or without it.
		if (WordsA[0] == "ORDER")
		{
			FieldsA.emplace("sprint", "0");
			FieldsB.emplace("sprint", "0");
		}
		if (FieldsA.size() != FieldsB.size())
		{
			return false;
		}
		for (const auto& Field : FieldsA)
		{
			const auto Found = FieldsB.find(Field.first);
			if (Found == FieldsB.end() || !Agree(Field.second, Found->second))
			{
				return false;
			}
		}
		return WordsA[0] != "U" || WordsA[1] == WordsB[1];
	}

	/**
	 * A battle as written before and as played now: the old lines while the two
	 * agree, so a rule that changed nothing changes no line, then the new ones.
	 */
	int Merge(const std::vector<std::string>& Was, const std::vector<std::string>& Now, std::vector<std::string>& Out)
	{
		size_t i = 0;
		while (i < Was.size() && i < Now.size() && SameLine(Was[i], Now[i]))
		{
			Out.push_back(Was[i]);
			++i;
		}
		const int Written = static_cast<int>(Now.size() - i);
		for (; i < Now.size(); ++i)
		{
			Out.push_back(Now[i]);
		}
		return Written;
	}
}

int main(int ArgCount, char** Args)
{
	const std::vector<std::string> Paths = TMBaseline::Paths(ArgCount, Args);
	if (Paths.empty())
	{
		std::printf("usage: SimTraceTest <class dir> [Baselines/BattleTrace.txt] [--rebaseline]\n");
		return 2;
	}
	const std::string TracePath = Paths.size() > 1 ? Paths[1] : std::string("Baselines/BattleTrace.txt");
	if (!std::filesystem::exists(TracePath))
	{
		std::printf("FAILED: %s is missing. Restore it from git.\n", TracePath.c_str());
		return 1;
	}

	// The classes the battles are fought with, as the trace was recorded with
	// them (Baselines/TraceClasses.txt beside the trace), so the game's classes
	// can be rebalanced without these battles changing (2026-10-02). Only
	// without that table are the class files used.
	const std::filesystem::path ClassTable = std::filesystem::path(TracePath).parent_path() / "TraceClasses.txt";
	int FromTable = 0;
	if (std::filesystem::exists(ClassTable))
	{
		std::ifstream Table(ClassTable);
		std::string Row;
		while (std::getline(Table, Row))
		{
			if (!Row.empty() && Row.back() == '\r')
			{
				Row.pop_back();
			}
			if (Row.rfind("CLASS ", 0) != 0)
			{
				continue;
			}
			const std::string Problem = LoadClassFile(TableRowAsClassFile(Row.substr(6)));
			if (!Problem.empty())
			{
				Fail("TraceClasses.txt: " + Problem);
			}
			++FromTable;
		}
		std::printf("%d classes as the trace was recorded with them (TraceClasses.txt)\n", FromTable);
	}
	else
	{
		for (const auto& Entry : std::filesystem::directory_iterator(Paths[0]))
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
	}

	if (TMBaseline::Asked(ArgCount, Args))
	{
		// Every battle played again, from its first line and its scripted orders.
		std::vector<std::string> Was;
		{
			std::ifstream In(TracePath);
			std::string Read;
			while (std::getline(In, Read))
			{
				if (!Read.empty() && Read.back() == '\r')
				{
					Read.pop_back();
				}
				Was.push_back(Read);
			}
		}
		TMBaseline::FWriter Writer;
		Writer.bOn = true;
		Writer.Path = TracePath;
		FAIPlayer Again("hard");
		size_t i = 0;
		while (i < Was.size() && Was[i].rfind("BATTLE ", 0) != 0)
		{
			Writer.Lines.push_back(Was[i++]);
		}
		int Played = 0;
		while (i < Was.size())
		{
			std::vector<std::string> Block;
			std::vector<std::string> Scripted;
			Block.push_back(Was[i++]);
			while (i < Was.size() && Was[i].rfind("BATTLE ", 0) != 0)
			{
				if (Was[i].rfind("ORDER ", 0) == 0 && Was[i].find(" scripted=1") != std::string::npos)
				{
					Scripted.push_back(Was[i]);
				}
				Block.push_back(Was[i++]);
			}
			std::vector<std::string> Now;
			if (!PlayAgain(Block[0], Scripted, Again, Now))
			{
				return 1;
			}
			Writer.Changed += Merge(Block, Now, Writer.Lines);
			++Played;
		}
		std::printf("%d battles played again\n", Played);
		return Failures == 0 && Writer.Finish() ? 0 : 1;
	}

	std::ifstream Trace(TracePath);
	std::string Line;
	FBattle Battle;
	int BattleNumber = 0;
	bool bFollowing = false;   // still in step with the trace in this battle
	int Checked = 0;
	int Orders = 0;
	int Agreed = 0;
	int Battles = 0;
	std::string Version;
	std::string LastOrder;
	// The computer player, seeded as the trace says, asked for every order
	// before the recorded one is applied: it must ask for the same one.
	FAIPlayer Computer("hard");
	std::string Wanted;
	std::string Level;
	int Decisions = 0;
	int DecisionsAgreed = 0;
	std::map<std::string, int> LevelDecisions;
	int PlaceChecks = 0;
	std::vector<std::string> Expected;  // the U lines of the STATE being read
	std::string ExpectedState;

	// Compares the port with the STATE block just read, once it is complete.
	auto Compare = [&]()
	{
		if (!bFollowing || ExpectedState.empty())
		{
			// A battle already parted from the trace, or nothing read yet: whatever
			// was read belongs to no comparison and must not reach the next one.
			ExpectedState.clear();
			Expected.clear();
			return;
		}
		++Checked;
		std::map<std::string, std::string> Want = Fields(ExpectedState);
		char Have[256];
		std::snprintf(Have, sizeof(Have), "tick=%d winner=%d capture=%d,%d planning=%d done=%d,%d rng=%lld", Battle.TickCount, Battle.Winner,
			Battle.CaptureTicks[0], Battle.CaptureTicks[1], Battle.PlanningTicks, Battle.PlanningDone[0] ? 1 : 0,
			Battle.PlanningDone[1] ? 1 : 0, static_cast<long long>(Battle.Rng.GetState()));
		const std::map<std::string, std::string> HaveFields = Fields(Have);
		std::string Wrong;
		for (const char* Key : { "tick", "winner", "capture", "planning", "done", "rng" })
		{
			if (Want[Key] != HaveFields.at(Key))
			{
				Wrong = std::string(Key) + ": recorded " + Want[Key] + ", now " + HaveFields.at(Key);
				break;
			}
		}
		for (size_t i = 0; Wrong.empty() && i < Expected.size(); ++i)
		{
			const std::string Port = i < Battle.Units.size() ? UnitLine(Battle.Units[i]) : std::string("(no such unit)");
			const std::string Field = Differs(Expected[i], Port);
			if (!Field.empty())
			{
				Wrong = "unit " + std::to_string(i) + " " + Field + "\n    recorded " + Expected[i] + "\n    now      " + Port;
			}
		}
		if (!Wrong.empty())
		{
			Fail("battle " + std::to_string(BattleNumber) + " parts from the trace at tick " + Want["tick"]
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
			Level = F.count("ai") ? F["ai"] : std::string("hard");
			Computer.SetDifficulty(Level.c_str());
			Computer.Rng.Seed(std::strtoull(F["seed"].c_str(), nullptr, 10));
			Wanted.clear();
			bFollowing = Deal(Battle, Split(F["blue"], ','), Split(F["red"], ','), std::strtoull(F["seed"].c_str(), nullptr, 10),
				std::strtod(F["capture"].c_str(), nullptr), std::strtod(F["time"].c_str(), nullptr),
				std::strtod(F["planning"].c_str(), nullptr));
			LastOrder.clear();
			++Battles;
		}
		else if (Line.rfind("ADVANCE ", 0) == 0 && bFollowing)
		{
			// One tick at a time, as the recording gave them.
			const int Ticks = std::atoi(Line.c_str() + 8);
			for (int i = 0; i < Ticks; ++i)
			{
				FTickReport Report;
				Battle.Apply(FOrder::MakeAdvance(1), Report);
			}
			LastOrder = Line;
		}
		else if (Line.rfind("WANTED ", 0) == 0)
		{
			Wanted = Line;
		}
		else if (Line.rfind("PLACEABLE ", 0) == 0 && bFollowing)
		{
			// Every spot the recording would let this unit be put down on, in its order.
			std::map<std::string, std::string> F = Fields(Line);
			const FUnit* Unit = Battle.FindUnit(std::atoi(F["unit"].c_str()));
			std::string Mine;
			if (Unit)
			{
				for (const FNode& Node : Battle.PlaceableNodes(*Unit))
				{
					Mine += (Mine.empty() ? "" : "|") + std::to_string(Node.X) + "," + std::to_string(Node.Y);
				}
			}
			++PlaceChecks;
			if ((Mine.empty() ? std::string("-") : Mine) != F["nodes"])
			{
				Fail("battle " + std::to_string(BattleNumber) + ": the spots unit " + F["unit"]
					+ " may be placed on differ from the trace's");
				bFollowing = false;
			}
		}
		else if (Line.rfind("ORDER ", 0) == 0 && bFollowing && Line.find(" scripted=1") != std::string::npos)
		{
			// Placing is scripted, not chosen: nobody's computer player places,
			// so it is only applied, and must be accepted.
			const FOrder Order = OrderOf(Line);
			const std::string Refused = Battle.Validate(Order);
			LastOrder = Line;
			if (!Refused.empty())
			{
				Fail("battle " + std::to_string(BattleNumber) + ": the rules refuse " + Line + " -- \"" + Refused + "\"");
				bFollowing = false;
				continue;
			}
			FTickReport Report;
			Battle.Apply(Order, Report);
		}
		else if (Line.rfind("ORDER ", 0) == 0 && bFollowing)
		{
			// First what the computer player would do here.
			const bool bRefused = Line.find(" refused=1") != std::string::npos;
			const std::string Recorded = bRefused ? Wanted : Line;
			Wanted.clear();
			const FUnit* Ready = FirstOrderable(Battle);
			if (!Ready)
			{
				Fail("battle " + std::to_string(BattleNumber) + ": the trace gives an order at tick " + std::to_string(Battle.TickCount)
					+ " while nobody is waiting on one");
				bFollowing = false;
				continue;
			}
			const FOrder Mine = Computer.NextCommand(Battle, *Ready);
			++Decisions;
			++LevelDecisions[Level];
			if (!SameOrder(Recorded, Mine))
			{
				Fail("battle " + std::to_string(BattleNumber) + " (" + Level + "): the computer player chooses differently at tick "
					+ std::to_string(Battle.TickCount) + "\n    recorded " + Recorded + "\n    now      ORDER " + OrderText(Mine));
				bFollowing = false;
				continue;
			}
			++DecisionsAgreed;
			const FOrder Order = OrderOf(Line);
			const std::string Refused = Battle.Validate(Order);
			LastOrder = Line;
			++Orders;
			if (!Refused.empty())
			{
				Fail("battle " + std::to_string(BattleNumber) + ": the rules refuse a recorded order, at tick "
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

	std::printf("%d recorded battles: %d orders, %d states compared\n", Battles, Orders, Checked);
	std::printf("%d of %d battles agree with the trace from the first tick to the last\n", Agreed, Battles);
	std::printf("every spot a unit may be placed on agrees for %d units\n", PlaceChecks);
	std::printf("the computer player chose as recorded %d times out of %d (", DecisionsAgreed, Decisions);
	bool bFirst = true;
	for (const auto& Each : LevelDecisions)
	{
		std::printf("%s%d at %s", bFirst ? "" : ", ", Each.second, Each.first.c_str());
		bFirst = false;
	}
	std::printf(")\n");
	if (Battles == 0)
	{
		Fail("the trace has no battles in it");
	}
	if (Failures == 0)
	{
		std::printf("WHOLE BATTLES PLAY AS THEY WERE RECORDED\n");
	}
	return Failures == 0 ? 0 : 1;
}
