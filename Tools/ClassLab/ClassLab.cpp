// The class lab: asks the rules about one class file, with no engine and no Godot.
//
// The class creator (E:\TacticsClassCreator) makes classes and has to know two
// things only the rules can answer: whether a class is legal, and how strong it
// is. It used to ask the Godot game (tests/check_class.gd and tests/balance.gd).
// This asks the Unreal port's rules instead, built on their own (Tools\ClassLab\Build.bat) the way the
// parity tests are, so making a class needs neither Godot nor Astra.
//
//   TMClassLab check <class file> [--battle]
//   TMClassLab playtest <class file> [games] [--skill easy|medium|hard]
//
// Each prints one line the creator reads -- CLASSCHECK {json} or PLAYTEST {json}
// -- and nothing else on stdout.
//
// check reads the file with the rules' own strict reader (SimClassFile.cpp) and
// reports what the rules understood; with --battle it also fights one battle
// through the order path and counts the orders the rules refused, as
// check_class.gd did. A class can read perfectly and still be unplayable.
//
// playtest is balance.gd: the class in a team with a knight, an archer and a
// white mage, against a black mage, a knight, an archer and a white mage,
// computer against computer, sides alternating, each pair of games on its own
// seed. It reports the margin -- how much more of its health the class's team
// kept than the other side, +100 to -100 -- which is the number to compare
// classes by; the win rate swings by 25 points between runs at eight games.
// Measured with the port's "medium", as balance.gd measures with Godot's. The
// port's medium thinks as Godot's does but does not yet make its random
// mistakes (Randf is not ported), so margins here are close to Godot's, not
// equal to them.

#include "SimAI.h"
#include "SimBattle.h"
#include "SimClassFile.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace TMSim;

namespace
{
	// ------------------------------------------------------------ writing JSON

	std::string Quote(const std::string& Text)
	{
		std::string Out = "\"";
		for (const char C : Text)
		{
			switch (C)
			{
			case '"': Out += "\\\""; break;
			case '\\': Out += "\\\\"; break;
			case '\n': Out += "\\n"; break;
			case '\r': Out += "\\r"; break;
			case '\t': Out += "\\t"; break;
			default:
				if (static_cast<unsigned char>(C) < 0x20)
				{
					char Escaped[8];
					std::snprintf(Escaped, sizeof(Escaped), "\\u%04x", static_cast<unsigned char>(C));
					Out += Escaped;
				}
				else
				{
					Out += C;
				}
			}
		}
		return Out + "\"";
	}

	std::string Number(double Value)
	{
		char Buffer[64];
		std::snprintf(Buffer, sizeof(Buffer), "%.9g", Value);
		return Buffer;
	}

	std::string ReadAll(const char* Path)
	{
		std::ifstream In(Path, std::ios::binary);
		if (!In)
		{
			return std::string();
		}
		std::stringstream Buffer;
		Buffer << In.rdbuf();
		return Buffer.str();
	}

	// ----------------------------------------------------------------- battles

	const char* const Reference[4] = { "black_mage", "knight", "archer", "white_mage" };
	const char* const Mates[3] = { "knight", "archer", "white_mage" };

	/** The two sides, the class's team on Side, as balance.gd deals them. */
	void Deal(FBattle& Battle, const std::string& JobId, int Side, uint64_t Seed)
	{
		std::string Rosters[2][4];
		for (int i = 0; i < 4; ++i)
		{
			Rosters[1 - Side][i] = Reference[i];
		}
		Rosters[Side][0] = JobId;
		for (int i = 0; i < 3; ++i)
		{
			Rosters[Side][i + 1] = Mates[i];
		}
		const FVec2 Blue[4] = { FVec2(2.75f, 4.75f), FVec2(0.75f, 8.75f), FVec2(4.75f, 6.75f), FVec2(2.75f, 10.75f) };
		Battle.Map.BuildMirrored(HighlandsRows());
		const FVec2 Size = Battle.Map.SizeMeters();
		for (int Index = 0; Index < 8; ++Index)
		{
			FUnit Unit;
			Unit.Id = Index;
			Unit.Team = Index < 4 ? 0 : 1;
			Unit.Job = Rosters[Unit.Team][Index % 4];
			Unit.Stats = &FindJob(Unit.Job)->Stats;
			const FVec2 Spot = Blue[Index % 4];
			Unit.Pos = Index < 4 ? Spot : FVec2(Size.X - Spot.X, Size.Y - Spot.Y);
			Battle.Units.push_back(Unit);
		}
		Battle.Start(Seed);
	}

	/** The first unit ready to be ordered, as orderable_units() offers them. */
	const FUnit* NextReady(const FBattle& Battle)
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

	/** Its share of its team's health still standing (game_state.gd:1464-1471). */
	double HealthShare(const FBattle& Battle, int Team)
	{
		double Alive = 0.0;
		double Total = 0.0;
		for (const FUnit& Unit : Battle.Units)
		{
			if (Unit.Team == Team)
			{
				Total += Unit.MaxHp();
				Alive += Unit.Hp > 0 ? Unit.Hp : 0;
			}
		}
		return Total > 0.0 ? Alive / Total : 0.0;
	}

	struct FFought
	{
		int Orders = 0;
		int Illegal = 0;
		std::vector<std::string> Refusals;
		int Ticks = 0;
		int Winner = -1;
		int Uses[4] = { 0, 0, 0, 0 };
		int Dealt = 0;
		int AliveTicks = 0;
		double Margin = 0.0;
	};

	/** One battle through the order path, the class's unit watched throughout. */
	FFought Fight(const std::string& JobId, int Side, uint64_t Seed, uint64_t AiSeed, const char* Skill, int TickLimit)
	{
		FBattle Battle;
		Deal(Battle, JobId, Side, Seed);
		FAIPlayer Computer(Skill);
		Computer.Rng.Seed(AiSeed);
		const int Watched = Side == 0 ? 0 : 4;
		int KnockedAt = -1;

		FFought Out;
		while (Battle.Winner == -1 && Battle.TickCount < TickLimit)
		{
			const FUnit* Ready = NextReady(Battle);
			FOrder Order = Ready ? Computer.NextCommand(Battle, *Ready) : FOrder::MakeAdvance(1);
			if (Ready)
			{
				++Out.Orders;
			}
			const std::string Refused = Battle.Validate(Order);
			if (!Refused.empty())
			{
				++Out.Illegal;
				if (Out.Refusals.size() < 5)
				{
					Out.Refusals.push_back(Refused);
				}
				if (!Ready)
				{
					break;
				}
				Order = FOrder::MakeEndTurn(Ready->Id, Ready->Serial);
				if (!Battle.Validate(Order).empty())
				{
					Out.Refusals.push_back("stuck: a ready unit could not even end its turn");
					break;
				}
			}
			FTickReport Report;
			Battle.Apply(Order, Report);
			for (const FEvent& Event : Report.Events)
			{
				if (Event.Kind == EEventKind::Resolved && Event.Unit == Watched && Event.Slot >= 0 && Event.Slot < 4)
				{
					++Out.Uses[Event.Slot];
				}
				if (Event.Kind == EEventKind::Hit && Event.By == Watched)
				{
					const FUnit* Struck = Battle.FindUnit(Event.Unit);
					if (!Struck || Struck->Team != Side)
					{
						Out.Dealt += Event.Amount;
					}
				}
				if (Event.Kind == EEventKind::Knocked && Event.Unit == Watched && KnockedAt < 0)
				{
					KnockedAt = Battle.TickCount;
				}
			}
		}
		const FUnit* Me = Battle.FindUnit(Watched);
		Out.AliveTicks = Me && Me->IsAlive() ? Battle.TickCount : (KnockedAt >= 0 ? KnockedAt : Battle.TickCount);
		Out.Margin = 100.0 * (HealthShare(Battle, Side) - HealthShare(Battle, 1 - Side));
		Out.Ticks = Battle.TickCount;
		Out.Winner = Battle.Winner;
		return Out;
	}

	// ---------------------------------------------------------------- commands

	std::string AbilityJson(const FAbility& A, int Slot)
	{
		std::string Out = "{\"id\":" + Quote(A.Id) + ",\"name\":" + Quote(A.Name) + ",\"kind\":" + Quote(A.Kind);
		const char* Effects[] = { "damage", "heal", "revive", "support" };
		const char* Targets[] = { "enemy", "ally", "ko_ally" };
		Out += ",\"effect\":" + Quote(Effects[static_cast<int>(A.Effect)]);
		Out += ",\"scale\":" + Quote(A.Scale == EScale::Att ? "att" : "mag");
		Out += ",\"target\":" + Quote(Targets[static_cast<int>(A.Target)]);
		Out += ",\"shape\":" + Quote(ShapeOf(A));
		Out += ",\"power\":" + Number(A.Power) + ",\"min_range\":" + Number(A.MinRange) + ",\"max_range\":" + Number(A.MaxRange);
		Out += ",\"aoe\":" + Number(A.Aoe) + ",\"cooldown\":" + Number(A.Cooldown) + ",\"cast\":" + Number(A.Cast);
		Out += ",\"tg\":" + Number(A.TgChange);
		// How the body moves when it goes off, as the game works it out: what the
		// creator shows, and checks its own reckoning against.
		Out += ",\"motion\":" + Quote(MotionOf(A, Slot));
		if (!A.StatusId.empty())
		{
			Out += ",\"status\":{\"id\":" + Quote(A.StatusId) + ",\"turns\":" + Number(A.StatusTurns) + "}";
		}
		Out += ",\"buffs\":[";
		for (size_t i = 0; i < A.Buffs.size(); ++i)
		{
			Out += std::string(i ? "," : "") + "{\"stat\":" + Quote(StatName(A.Buffs[i].Stat)) + ",\"amount\":"
				+ Number(A.Buffs[i].Amount) + ",\"turns\":" + Number(A.Buffs[i].Turns) + "}";
		}
		return Out + "]}";
	}

	/** Reads and registers the class. "" if it is in the game now. */
	std::string Load(const char* Path, FJobDef& Job, std::vector<FAbility>& Abilities)
	{
		const std::string Text = ReadAll(Path);
		if (Text.empty())
		{
			return std::string("could not read ") + Path;
		}
		std::string Problems = ReadClassFile(Text, Job, Abilities);
		if (!Problems.empty())
		{
			return Problems;
		}
		return RegisterJob(Job, Abilities);
	}

	std::string ErrorList(const std::string& Problems)
	{
		std::string Out = "[";
		std::stringstream Lines(Problems);
		std::string Line;
		bool bFirst = true;
		while (std::getline(Lines, Line))
		{
			if (!Line.empty())
			{
				Out += std::string(bFirst ? "" : ",") + Quote(Line);
				bFirst = false;
			}
		}
		return Out + "]";
	}

	int Check(const char* Path, bool bBattle)
	{
		FJobDef Job;
		std::vector<FAbility> Abilities;
		const std::string Problems = Load(Path, Job, Abilities);
		std::string Out = "{\"ok\":" + std::string(Problems.empty() ? "true" : "false") + ",\"errors\":" + ErrorList(Problems);
		Out += ",\"file\":" + Quote(Path);
		if (Problems.empty())
		{
			Out += ",\"id\":" + Quote(Job.Id) + ",\"name\":" + Quote(Job.Name) + ",\"look\":" + Quote(Job.Look)
				+ ",\"color\":" + Quote(Job.Color);
			Out += ",\"roles\":[";
			for (size_t i = 0; i < Job.Roles.size(); ++i)
			{
				Out += std::string(i ? "," : "") + Quote(Job.Roles[i]);
			}
			Out += "],\"stats\":{";
			for (int i = 0; i < StatCount; ++i)
			{
				const EStat Which = static_cast<EStat>(i);
				Out += std::string(i ? "," : "") + Quote(StatName(Which)) + ":" + Number(Job.Stats.Get(Which));
			}
			Out += "},\"abilities\":[";
			for (size_t i = 0; i < Abilities.size(); ++i)
			{
				Out += std::string(i ? "," : "") + AbilityJson(Abilities[i], static_cast<int>(i));
			}
			Out += "]";
			if (bBattle)
			{
				// As check_class.gd: one battle, long enough to see whether its
				// orders are legal.
				const FFought Fought = Fight(Job.Id, 0, 1, 1, "hard", 12000);
				Out += ",\"battle\":{\"orders\":" + Number(Fought.Orders) + ",\"illegal\":" + Number(Fought.Illegal)
					+ ",\"refusals\":[";
				for (size_t i = 0; i < Fought.Refusals.size(); ++i)
				{
					Out += std::string(i ? "," : "") + Quote(Fought.Refusals[i]);
				}
				Out += "],\"ticks\":" + Number(Fought.Ticks) + ",\"winner\":" + Number(Fought.Winner) + "}";
			}
		}
		std::printf("CLASSCHECK %s}\n", Out.c_str());
		return Problems.empty() ? 0 : 1;
	}

	int Playtest(const char* Path, int Games, const char* Skill)
	{
		FJobDef Job;
		std::vector<FAbility> Abilities;
		const std::string Problems = Load(Path, Job, Abilities);
		if (!Problems.empty())
		{
			std::printf("PLAYTEST {\"ok\":false,\"errors\":%s}\n", ErrorList(Problems).c_str());
			return 1;
		}
		const auto Start = std::chrono::steady_clock::now();
		double Wins = 0.0;
		double Margin = 0.0;
		long long Dealt = 0;
		long long AliveTicks = 0;
		int Uses[4] = { 0, 0, 0, 0 };
		int Illegal = 0;
		for (int Game = 0; Game < Games; ++Game)
		{
			// balance.gd: sides alternate; each pair of games has its own seed,
			// and the computer is seeded from the game.
			const int Side = Game % 2;
			const FFought Fought = Fight(Job.Id, Side, 1 + Game / 2, 1 + Game, Skill, 40000);
			Margin += Fought.Margin;
			Dealt += Fought.Dealt;
			AliveTicks += Fought.AliveTicks;
			Illegal += Fought.Illegal;
			for (int Slot = 0; Slot < 4; ++Slot)
			{
				Uses[Slot] += Fought.Uses[Slot];
			}
			if (Fought.Winner == Side)
			{
				Wins += 1.0;
			}
			else if (Fought.Winner == -1)
			{
				Wins += 0.5;
			}
		}
		const long long Ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - Start).count();
		std::string Out = "{\"ok\":true,\"id\":" + Quote(Job.Id) + ",\"games\":" + Number(Games) + ",\"skill\":" + Quote(Skill);
		Out += ",\"win\":" + Number(static_cast<double>(RoundToInt(100.0 * Wins / Games)));
		Out += ",\"margin\":" + Number(RoundToInt(Margin / Games * 10.0) / 10.0);
		Out += ",\"dealt\":" + Number(static_cast<double>(Dealt / Games));
		Out += ",\"lives\":" + Number(static_cast<double>(AliveTicks / Games / Pace::TicksPerSecond));
		Out += ",\"uses\":[" + Number(Uses[0]) + "," + Number(Uses[1]) + "," + Number(Uses[2]) + "," + Number(Uses[3]) + "]";
		Out += ",\"illegal\":" + Number(Illegal) + ",\"ms\":" + Number(static_cast<double>(Ms)) + "}";
		std::printf("PLAYTEST %s\n", Out.c_str());
		return 0;
	}
}

int main(int ArgCount, char** Args)
{
	if (ArgCount >= 3 && std::string(Args[1]) == "check")
	{
		const bool bBattle = ArgCount >= 4 && std::string(Args[3]) == "--battle";
		return Check(Args[2], bBattle);
	}
	if (ArgCount >= 3 && std::string(Args[1]) == "playtest")
	{
		int Games = 8;
		const char* Skill = "medium";
		for (int i = 3; i < ArgCount; ++i)
		{
			const std::string Arg = Args[i];
			if (Arg == "--skill" && i + 1 < ArgCount)
			{
				Skill = Args[++i];
			}
			else
			{
				Games = std::atoi(Args[i]);
			}
		}
		if (Games < 1 || Games > 200)
		{
			std::fprintf(stderr, "games must be 1 to 200\n");
			return 2;
		}
		return Playtest(Args[2], Games, Skill);
	}
	std::fprintf(stderr, "usage:\n  TMClassLab check <class file> [--battle]\n  TMClassLab playtest <class file> [games] [--skill easy|medium|hard]\n");
	return 2;
}
