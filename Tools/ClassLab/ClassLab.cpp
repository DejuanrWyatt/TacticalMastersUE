// The class lab: asks the rules about one class file, with no engine and no Godot.
//
// The class creator (E:\TacticsClassCreator) makes classes and has to know two
// things only the rules can answer: whether a class is legal, and how strong it
// is. It used to ask the Godot game (tests/check_class.gd and tests/balance.gd).
// This asks the Unreal port's rules instead, built on their own (Tools\ClassLab\Build.bat) the way the
// parity tests are, so making a class needs neither Godot nor Astra.
//
//   TMClassLab check <class file> [--battle]
//   TMClassLab playtest <class file> [games] [--skill easy|medium|hard] [--rules game|godot] [--maps <dir>]
//   TMClassLab tournament <classes dir> [games] [--skill ...] [--rules ...] [--maps <dir>]
//   TMClassLab map <map file> [games]
//
// Each prints the lines the creator reads -- CLASSCHECK {json}, PLAYTEST {json},
// TOURNEY {json} per class then TOURNEYDONE {json}, MAPCHECK {json} -- and nothing
// else on stdout.
//
// The rules (2026-10-01): battles are fought on the rules the game plays
// (TMSim::GameTuning: Armor and Resist take a share, one Evasion), not the
// defaults the rules keep for checking against Godot; --rules godot measures the
// old way. The computer plays at "hard", the game's own computer opponent, and a
// playtest is 40 games, not 8, since the win rate swung by 25 points between runs
// at eight. With --maps, games go round the Highlands and every map in that
// folder (Content/Data/Maps), so a class is not tuned to one board.
//
// check reads the file with the rules' own strict reader (SimClassFile.cpp) and
// reports what the rules understood; with --battle it also fights one battle
// through the order path and counts the orders the rules refused, as
// check_class.gd did. A class can read perfectly and still be unplayable.
//
// playtest is balance.gd: the reference team -- a black mage, a knight, an
// archer and a white mage -- with the class standing in for the one that shares
// its first role (tank: knight, support: white mage, special: archer, damage:
// black mage), against the reference team, computer against computer, sides
// alternating, each pair of games on its own seed. It reports the win rate
// ("solo"; a class as good at its job as the one it replaces wins half)
// and the margin -- how much more of its health the class's team kept than the
// other side, +100 to -100. It also plays four of the class against the same
// team ("stack"), which shows what a class does on its own rather than what it
// adds to a sound team.
//
// tournament plays every class in a folder, and the six built in, the same
// way, so a class can be read against all the others: the creator's Balance tab.

#include "SimAI.h"
#include "SimAbility.h"
#include "SimBattle.h"
#include "SimClassFile.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
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

	/**
	 * Which of the reference four the class stands in for: the one with its
	 * first role (2026-10-01). A tank replaces the Knight, a support the White
	 * Mage, a special the Archer, damage the Black Mage -- so a class is judged
	 * at its own job, and a built-in class standing in for itself wins half.
	 * It used to replace the Black Mage whatever it was for, which made every
	 * tank and healer look broken.
	 */
	int StandIn(const std::string& JobId)
	{
		const FJobDef* Job = FindJob(JobId);
		const std::string Role = Job && !Job->Roles.empty() ? Job->Roles.front() : std::string("damage");
		return Role == "tank" ? 1 : Role == "special" ? 2 : Role == "support" ? 3 : 0;
	}

	/** The rules battles are fought on: the game's, unless --rules godot. */
	FTuning Rules = GameTuning();
	std::string RulesName = "game";

	/** A board to fight on, and where blue's four stand (red stands turned about). */
	struct FArena
	{
		std::string Id;
		std::vector<std::string> Top;
		std::vector<FVec2> Spawns;
	};

	/** The Highlands, always; and with --maps, every map in that folder. */
	std::vector<FArena> Arenas;

	void AddHighlands()
	{
		FArena Highlands;
		Highlands.Id = "highlands";
		Highlands.Top = HighlandsRows();
		Highlands.Spawns = { FVec2(2.75f, 4.75f), FVec2(0.75f, 8.75f), FVec2(4.75f, 6.75f), FVec2(2.75f, 10.75f) };
		Arenas.push_back(Highlands);
	}

	/**
	 * The two sides, the class's team on Side, as balance.gd deals them; stacked,
	 * the class's team is four of it. On the game's rules (or Godot's), on the
	 * arena given.
	 */
	void Deal(FBattle& Battle, const std::string& JobId, int Side, uint64_t Seed, bool bStack, const FArena& Arena)
	{
		std::string Rosters[2][4];
		const int Stands = StandIn(JobId);
		for (int i = 0; i < 4; ++i)
		{
			Rosters[1 - Side][i] = Reference[i];
			Rosters[Side][i] = bStack || i == Stands ? JobId : std::string(Reference[i]);
		}
		Battle.Tuning = Rules;
		Battle.Map.BuildMirrored(Arena.Top);
		const FVec2 Size = Battle.Map.SizeMeters();
		for (int Index = 0; Index < 8; ++Index)
		{
			FUnit Unit;
			Unit.Id = Index;
			Unit.Team = Index < 4 ? 0 : 1;
			Unit.Job = Rosters[Unit.Team][Index % 4];
			Unit.Stats = &FindJob(Unit.Job)->Stats;
			const FVec2 Spot = Arena.Spawns[static_cast<size_t>(Index % 4) % Arena.Spawns.size()];
			Unit.Pos = Index < 4 ? Spot : FVec2(Size.X - Spot.X, Size.Y - Spot.Y);
			Battle.Units.push_back(Unit);
		}
		Battle.SpawnPoints[0] = Arena.Spawns[0];
		Battle.SpawnPoints[1] = FVec2(Size.X - Arena.Spawns[0].X, Size.Y - Arena.Spawns[0].Y);
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
	FFought Fight(const std::string& JobId, int Side, uint64_t Seed, uint64_t AiSeed, const char* Skill, int TickLimit,
		bool bStack = false, size_t ArenaIndex = 0)
	{
		FBattle Battle;
		Deal(Battle, JobId, Side, Seed, bStack, Arenas[ArenaIndex % Arenas.size()]);
		FAIPlayer Computer(Skill);
		Computer.Rng.Seed(AiSeed);
		const int Watched = (Side == 0 ? 0 : 4) + (bStack ? 0 : StandIn(JobId));
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

	/** Four of the class against the reference team: its win rate, a draw a half. */
	double StackWins(const std::string& JobId, int Games, const char* Skill)
	{
		double Wins = 0.0;
		for (int Game = 0; Game < Games; ++Game)
		{
			const int Side = Game % 2;
			const FFought Fought = Fight(JobId, Side, 5001 + Game / 2, 5001 + Game, Skill, 40000, true, static_cast<size_t>(Game / 2));
			Wins += Fought.Winner == Side ? 1.0 : Fought.Winner == -1 ? 0.5 : 0.0;
		}
		return Wins / Games;
	}

	std::string ArenaList()
	{
		std::string Out = "[";
		for (size_t i = 0; i < Arenas.size(); ++i)
		{
			Out += std::string(i ? "," : "") + Quote(Arenas[i].Id);
		}
		return Out + "]";
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
			// Each pair of games on the next arena, so both sides play every board.
			const FFought Fought = Fight(Job.Id, Side, 1 + Game / 2, 1 + Game, Skill, 40000, false, static_cast<size_t>(Game / 2));
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
		const double Stack = StackWins(Job.Id, Games, Skill);
		const long long Ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - Start).count();
		std::string Out = "{\"ok\":true,\"id\":" + Quote(Job.Id) + ",\"games\":" + Number(Games) + ",\"skill\":" + Quote(Skill);
		Out += ",\"rules\":" + Quote(RulesName) + ",\"maps\":" + ArenaList();
		Out += ",\"standsIn\":" + Quote(Reference[StandIn(Job.Id)]);
		Out += ",\"stack\":" + Number(static_cast<double>(RoundToInt(100.0 * Stack)));
		Out += ",\"win\":" + Number(static_cast<double>(RoundToInt(100.0 * Wins / Games)));
		Out += ",\"margin\":" + Number(RoundToInt(Margin / Games * 10.0) / 10.0);
		Out += ",\"dealt\":" + Number(static_cast<double>(Dealt / Games));
		Out += ",\"lives\":" + Number(static_cast<double>(AliveTicks / Games / Pace::TicksPerSecond));
		Out += ",\"uses\":[" + Number(Uses[0]) + "," + Number(Uses[1]) + "," + Number(Uses[2]) + "," + Number(Uses[3]) + "]";
		Out += ",\"illegal\":" + Number(Illegal) + ",\"ms\":" + Number(static_cast<double>(Ms)) + "}";
		std::printf("PLAYTEST %s\n", Out.c_str());
		return 0;
	}

	/**
	 * Is a map legal, and how does it play? Read with the game's own checks
	 * (TMSim::ReadMapFile), then fought on: the Godot starting four a side,
	 * computer against computer at hard, the seeds counting up. It says how
	 * often each side won -- a fair map is near half and half -- how long a
	 * battle takes and when the first blow lands, where units fell, and how
	 * often each tile was stood on, so ground nobody uses shows up.
	 */
	int MapCheck(const char* Path, int Games)
	{
		FMapDef Def;
		const std::string Text = ReadAll(Path);
		const std::string Problems = Text.empty() ? std::string("could not read ") + Path : ReadMapFile(Text, Def);
		if (!Problems.empty())
		{
			std::printf("MAPCHECK {\"ok\":false,\"errors\":%s}\n", ErrorList(Problems).c_str());
			return 1;
		}
		FMap Map;
		Map.BuildMirrored(Def.Top);
		const FVec2 Size = Map.SizeMeters();
		const char* const Roster[4] = { "knight", "archer", "black_mage", "white_mage" };
		int Wins[2] = { 0, 0 };
		int Undecided = 0;
		int Illegal = 0;
		double Seconds = 0.0;
		double FirstBlow = 0.0;
		int Blows = 0;
		std::vector<int> Trodden(static_cast<size_t>(Map.TilesX) * Map.TilesY, 0);
		std::vector<int> Falls(Trodden.size(), 0);
		auto TileAt = [&Map](const FVec2& Point)
		{
			const FNode Node = FMap::NodeOf(Point);
			const int X = std::min(Map.TilesX - 1, std::max(0, Node.X / Ground::NodesPerTile));
			const int Y = std::min(Map.TilesY - 1, std::max(0, Node.Y / Ground::NodesPerTile));
			return static_cast<size_t>(Y * Map.TilesX + X);
		};
		for (int Game = 0; Game < Games; ++Game)
		{
			FBattle Battle;
			Battle.Tuning = Rules;
			Battle.Map = Map;
			for (int Index = 0; Index < 8; ++Index)
			{
				FUnit Unit;
				Unit.Id = Index;
				Unit.Team = Index < 4 ? 0 : 1;
				Unit.Job = Roster[Index % 4];
				Unit.Stats = &FindJob(Unit.Job)->Stats;
				const FVec2 Spot = Def.Spawns[Index % 4];
				Unit.Pos = Index < 4 ? Spot : FVec2(Size.X - Spot.X, Size.Y - Spot.Y);
				Battle.Units.push_back(Unit);
			}
			Battle.SpawnPoints[0] = Def.Spawns[0];
			Battle.SpawnPoints[1] = FVec2(Size.X - Def.Spawns[0].X, Size.Y - Def.Spawns[0].Y);
			Battle.Start(1000 + Game);
			FAIPlayer Computer("hard");
			Computer.Rng.Seed(2000 + Game);
			int Hit = -1;
			while (Battle.Winner == -1 && Battle.TickCount < 6000)
			{
				const FUnit* Ready = NextReady(Battle);
				FOrder Order = Ready ? Computer.NextCommand(Battle, *Ready) : FOrder::MakeAdvance(1);
				if (!Battle.Validate(Order).empty())
				{
					++Illegal;
					if (!Ready)
					{
						break;
					}
					Order = FOrder::MakeEndTurn(Ready->Id, Ready->Serial);
				}
				FTickReport Report;
				Battle.Apply(Order, Report);
				for (const FEvent& Event : Report.Events)
				{
					if (Hit < 0 && Event.Kind == EEventKind::Hit && Event.By >= 0)
					{
						Hit = Battle.TickCount;
					}
					if (Event.Kind == EEventKind::Knocked)
					{
						if (const FUnit* Fallen = Battle.FindUnit(Event.Unit))
						{
							++Falls[TileAt(Fallen->Pos)];
						}
					}
				}
				// Where everyone stands, once a second of battle.
				if (Battle.TickCount % Pace::TicksPerSecond == 0 && !Ready)
				{
					for (const FUnit& Unit : Battle.Units)
					{
						if (Unit.IsAlive())
						{
							++Trodden[TileAt(Unit.Pos)];
						}
					}
				}
			}
			if (Battle.Winner == 0 || Battle.Winner == 1)
			{
				++Wins[Battle.Winner];
			}
			else
			{
				++Undecided;
			}
			Seconds += Battle.TickCount / static_cast<double>(Pace::TicksPerSecond);
			if (Hit >= 0)
			{
				FirstBlow += Hit / static_cast<double>(Pace::TicksPerSecond);
				++Blows;
			}
		}
		auto List = [](const std::vector<int>& Values)
		{
			std::string Out = "[";
			for (size_t i = 0; i < Values.size(); ++i)
			{
				Out += (i ? "," : "") + std::to_string(Values[i]);
			}
			return Out + "]";
		};
		std::printf("MAPCHECK {\"ok\":true,\"errors\":[],\"id\":%s,\"name\":%s,\"tiles\":[%d,%d],\"games\":%d,\"blue\":%d,\"red\":%d,"
			"\"undecided\":%d,\"illegal\":%d,\"seconds\":%s,\"firstBlow\":%s,\"trodden\":%s,\"falls\":%s}\n",
			Quote(Def.Id).c_str(), Quote(Def.Name).c_str(), Map.TilesX, Map.TilesY, Games, Wins[0], Wins[1], Undecided, Illegal,
			Number(Seconds / Games).c_str(), Number(Blows ? FirstBlow / Blows : -1.0).c_str(), List(Trodden).c_str(), List(Falls).c_str());
		return Illegal == 0 ? 0 : 1;
	}
}

namespace
{
	/**
	 * Every class in a folder, and the six built in, measured as playtest does:
	 * one TOURNEY line each, then TOURNEYDONE. The creator's Balance tab reads
	 * these to place a class among all the others.
	 */
	int Tournament(const char* Dir, int Games, const char* Skill)
	{
		const auto Start = std::chrono::steady_clock::now();
		std::vector<std::string> Ids;
		std::vector<std::string> Refused;
		std::vector<std::filesystem::path> Files;
		std::error_code Error;
		for (const auto& Entry : std::filesystem::directory_iterator(Dir, Error))
		{
			const std::string Name = Entry.path().filename().string();
			if (Name.size() > 13 && Name.compare(Name.size() - 13, 13, ".tmclass.json") == 0)
			{
				Files.push_back(Entry.path());
			}
		}
		std::sort(Files.begin(), Files.end());
		for (const std::filesystem::path& File : Files)
		{
			FJobDef Job;
			std::vector<FAbility> Abilities;
			const std::string Problems = Load(File.string().c_str(), Job, Abilities);
			if (Problems.empty())
			{
				Ids.push_back(Job.Id);
			}
			else
			{
				Refused.push_back(File.filename().string() + ": " + Problems);
			}
		}
		for (const char* Builtin : { "squire", "knight", "archer", "monk", "black_mage", "white_mage" })
		{
			Ids.push_back(Builtin);
		}
		for (const std::string& Id : Ids)
		{
			double Wins = 0.0;
			double Margin = 0.0;
			int Illegal = 0;
			for (int Game = 0; Game < Games; ++Game)
			{
				const int Side = Game % 2;
				const FFought Fought = Fight(Id, Side, 1 + Game / 2, 1 + Game, Skill, 40000, false, static_cast<size_t>(Game / 2));
				Wins += Fought.Winner == Side ? 1.0 : Fought.Winner == -1 ? 0.5 : 0.0;
				Margin += Fought.Margin;
				Illegal += Fought.Illegal;
			}
			const double Stack = StackWins(Id, Games, Skill);
			const FJobDef* Job = FindJob(Id);
			std::string Out = "{\"id\":" + Quote(Id) + ",\"name\":" + Quote(Job ? Job->Name : Id);
			Out += ",\"builtin\":" + std::string(&Id >= &Ids[Ids.size() - 6] ? "true" : "false");
			Out += ",\"standsIn\":" + Quote(Reference[StandIn(Id)]);
			Out += ",\"solo\":" + Number(static_cast<double>(RoundToInt(100.0 * Wins / Games)));
			Out += ",\"margin\":" + Number(RoundToInt(Margin / Games * 10.0) / 10.0);
			Out += ",\"stack\":" + Number(static_cast<double>(RoundToInt(100.0 * Stack)));
			Out += ",\"illegal\":" + Number(Illegal) + "}";
			std::printf("TOURNEY %s\n", Out.c_str());
			std::fflush(stdout);
		}
		const long long Ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - Start).count();
		std::string Done = "{\"ok\":true,\"classes\":" + Number(static_cast<double>(Ids.size())) + ",\"games\":" + Number(Games)
			+ ",\"skill\":" + Quote(Skill) + ",\"rules\":" + Quote(RulesName) + ",\"maps\":" + ArenaList() + ",\"refused\":[";
		for (size_t i = 0; i < Refused.size(); ++i)
		{
			Done += std::string(i ? "," : "") + Quote(Refused[i]);
		}
		Done += "],\"ms\":" + Number(static_cast<double>(Ms)) + "}";
		std::printf("TOURNEYDONE %s\n", Done.c_str());
		return 0;
	}

	/** The Highlands, then, with --maps, every map file in the folder, in name order. */
	std::string LoadArenas(const char* Dir)
	{
		AddHighlands();
		if (!Dir)
		{
			return std::string();
		}
		std::vector<std::filesystem::path> Files;
		std::error_code Error;
		for (const auto& Entry : std::filesystem::directory_iterator(Dir, Error))
		{
			const std::string Name = Entry.path().filename().string();
			if (Name.size() > 11 && Name.compare(Name.size() - 11, 11, ".tmmap.json") == 0)
			{
				Files.push_back(Entry.path());
			}
		}
		if (Error)
		{
			return std::string("could not read the maps folder ") + Dir;
		}
		std::sort(Files.begin(), Files.end());
		for (const std::filesystem::path& File : Files)
		{
			FMapDef Def;
			const std::string Text = ReadAll(File.string().c_str());
			if (Text.empty() || !ReadMapFile(Text, Def).empty() || Def.Spawns.empty())
			{
				continue;
			}
			FArena Arena;
			Arena.Id = Def.Id;
			Arena.Top = Def.Top;
			Arena.Spawns = Def.Spawns;
			Arenas.push_back(Arena);
		}
		return std::string();
	}
}

int main(int ArgCount, char** Args)
{
	// The options every battle reads: --skill, --rules, --maps; anything else is
	// the command's own. The game's rules, the game's computer, the Highlands.
	const char* Skill = "hard";
	const char* MapsDir = nullptr;
	std::vector<std::string> Rest;
	for (int i = 1; i < ArgCount; ++i)
	{
		const std::string Arg = Args[i];
		if (Arg == "--skill" && i + 1 < ArgCount)
		{
			Skill = Args[++i];
		}
		else if (Arg == "--rules" && i + 1 < ArgCount)
		{
			RulesName = Args[++i];
			if (RulesName != "game" && RulesName != "godot")
			{
				std::fprintf(stderr, "rules must be game or godot\n");
				return 2;
			}
			Rules = RulesName == "game" ? GameTuning() : FTuning();
		}
		else if (Arg == "--maps" && i + 1 < ArgCount)
		{
			MapsDir = Args[++i];
		}
		else
		{
			Rest.push_back(Arg);
		}
	}
	// On the game's rules, the game's built-ins too: its Knight and Archer.
	if (RulesName == "game")
	{
		ApplyGameBalance();
	}
	const std::string Problem = LoadArenas(MapsDir);
	if (!Problem.empty())
	{
		std::fprintf(stderr, "%s\n", Problem.c_str());
		return 2;
	}
	if (Rest.size() >= 2 && Rest[0] == "tournament")
	{
		const int Games = Rest.size() >= 3 ? std::atoi(Rest[2].c_str()) : 20;
		if (Games < 2 || Games > 200)
		{
			std::fprintf(stderr, "games must be 2 to 200\n");
			return 2;
		}
		return Tournament(Rest[1].c_str(), Games, Skill);
	}
	if (Rest.size() >= 2 && Rest[0] == "playtest")
	{
		const int Games = Rest.size() >= 3 ? std::atoi(Rest[2].c_str()) : 40;
		if (Games < 1 || Games > 200)
		{
			std::fprintf(stderr, "games must be 1 to 200\n");
			return 2;
		}
		return Playtest(Rest[1].c_str(), Games, Skill);
	}
	// The older commands read their own arguments, the options taken out.
	std::vector<char*> Kept = { Args[0] };
	for (std::string& Arg : Rest)
	{
		Kept.push_back(Arg.data());
	}
	ArgCount = static_cast<int>(Kept.size());
	Args = Kept.data();
	if (ArgCount >= 3 && std::string(Args[1]) == "map")
	{
		const int Games = ArgCount >= 4 ? std::atoi(Args[3]) : 8;
		if (Games < 1 || Games > 64)
		{
			std::fprintf(stderr, "games must be 1 to 64\n");
			return 2;
		}
		return MapCheck(Args[2], Games);
	}
	if (ArgCount >= 3 && std::string(Args[1]) == "check")
	{
		const bool bBattle = ArgCount >= 4 && std::string(Args[3]) == "--battle";
		return Check(Args[2], bBattle);
	}
	std::fprintf(stderr, "usage:\n  TMClassLab check <class file> [--battle]\n"
		"  TMClassLab playtest <class file> [games, 40] [--skill easy|medium|hard] [--rules game|godot] [--maps <dir>]\n"
		"  TMClassLab tournament <classes dir> [games, 20] [--skill ...] [--rules ...] [--maps <dir>]\n"
		"  TMClassLab map <map file> [games]\n");
	return 2;
}
