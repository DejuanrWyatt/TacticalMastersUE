// The class lab: asks the rules about one class file, with no engine.
//
// The class creator (E:\TacticsClassCreator) makes classes and has to know two
// things only the rules can answer: whether a class is legal, and how strong it
// is. This asks the game's own rules, built on their own (Tools\ClassLab\Build.bat)
// the way the rules tests are.
//
//   TMClassLab check <class file> [--battle]
//   TMClassLab playtest <class file> [games] [--skill easy|medium|hard] [--rules game|classic] [--maps <dir>]
//   TMClassLab tournament <classes dir> [games] [--skill ...] [--rules ...] [--maps <dir>]
//   TMClassLab rating <classes dir> <role> [games a pair] [--sample K]
//   TMClassLab map <map file> [games]
//   TMClassLab looks <class file>
//
// --monsters <dir> reads the monster files too (Content/Data/Monsters); with
// --maps and no --monsters, the Monsters folder beside the maps folder. Pets
// are monster files (2026-10-02): without them a summoner's pet never comes.
//
// Each prints the lines the creator reads -- CLASSCHECK {json}, PLAYTEST {json},
// TOURNEY {json} per class then TOURNEYDONE {json}, MAPCHECK {json} -- and nothing
// else on stdout.
//
// The rules (2026-10-01): battles are fought on the rules the game plays
// (TMSim::GameTuning: Armor and Resist take a share, one Evasion), not the
// classic defaults the rules tests' baselines were recorded with; --rules
// classic measures the old way. The computer plays at "hard", the game's own computer opponent, and a
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
//
// The restructure (2026-10-04, Docs/design/feat-class-balance.md, "The lab"; the research report "Balancing
// tactical games"): one yardstick and one set of seeds made the verdicts partly luck and partly the yardstick's.
//   --teams standard (or "a,b,c,d;e,f,g,h") measures a class in four reference teams, not one; members that are
//     class files need --classes <dir>.
//   --seed-base N plays battles no earlier run has, to check a result on fresh seeds.
//   --ban <slot> makes one of the class's abilities inert: what the class is worth without it.
//   --skill search lets the class under test think ahead (several whole turns tried and played on), so a class
//     the hard computer plays badly shows it: compare its gain with other classes' (--search candidates,seconds).
//   playtest also reports a 95% interval, uses per game, and the abilities it almost never chose ("unused").
//   rating plays one role's classes against each other and fits Bradley-Terry ratings with standard errors.
// Tools/ClassLab/balance.py (Balance.bat) runs all of it as a pass: screen, shrink, confirm on fresh seeds.
// Without the new options every command gives exactly what it gave before.
//
// looks (2026-10-02, Cast Studio phase 2) writes each of the class's abilities'
// look today -- the one the game works out from what an ability is made of
// (Source/TMCast/Public/CastLegacy.h) -- as Cast Studio events, for the
// creator's "Import today's look": LOOKS {"abilities": {...}, "footprints": {...}}.

#include "SimAI.h"
#include "SimAbility.h"
#include "SimBattle.h"
#include "SimClassFile.h"
#include "SimOrderText.h"

#include "CastLegacy.h"
#include "CastLooks.h"

#include <algorithm>
#include <chrono>
#include <cmath>
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
	 * The reference teams (2026-10-04, the lab restructure; Docs/design/feat-class-balance.md, "The lab"). Each
	 * is four class ids in the stand-in order: damage, tank, special, support. A class is measured in each of them
	 * in turn, standing in for the member with its role, against that same team -- so one team's quirks (a strong
	 * Black Mage, a weak Archer) are not the whole verdict. By default only the classic team, as before; --teams
	 * standard gives four, --teams "a,b,c,d;e,f,g,h" any. Members that are class files need --classes.
	 */
	std::vector<std::vector<std::string>> Teams = { { "black_mage", "knight", "archer", "white_mage" } };

	/**
	 * The standard four (2026-10-04): the classic team, then three made of classes the 2026-10-03 tuning pass
	 * measured near the middle of their role at 200 games, so no single built-in sets the bar.
	 */
	const char* const StandardTeams = "black_mage,knight,archer,white_mage;"
		"dragoon,paladin,warlock,tide_cleric;"
		"geomancer,reef_guardian,tempest_hexer,cantor;"
		"cryomancer,earthshaker,exorcist,piper";

	/**
	 * Added to every seed (--seed-base N, as N x 1000003), so a check can be run on battles the tuning never saw.
	 * 0, the default, gives the battles the lab always gave.
	 */
	uint64_t SeedBase = 0;

	/** The class's ability slot made inert for a run (--ban 0-3): what the class is worth without it. -1 for none. */
	int Banned = -1;

	/**
	 * The search player (--skill search): the class under test thinks ahead, everyone else plays at hard.
	 * Candidates turns tried, and how many seconds of battle each is played on for, to be judged by.
	 */
	bool bSearch = false;
	int SearchCandidates = 8;
	int SearchSeconds = 15;
	/** Turns the search player thought about, and how many of them it played differently from the hard computer. */
	int SearchedTurns = 0;
	int SearchChanged = 0;

	/** Which reference team pair Pair plays in. With nine arenas and four teams the two cycle through 36 pairings. */
	size_t TeamFor(int Pair)
	{
		return static_cast<size_t>(Pair) % Teams.size();
	}

	/** Reads "a,b,c,d;e,f,g,h". "" and fills Out, or what is wrong. */
	std::string ParseTeams(const std::string& Text, std::vector<std::vector<std::string>>& Out)
	{
		Out.clear();
		std::stringstream TeamList(Text == "standard" ? std::string(StandardTeams) : Text);
		std::string Team;
		while (std::getline(TeamList, Team, ';'))
		{
			std::vector<std::string> Members;
			std::stringstream MemberList(Team);
			std::string Member;
			while (std::getline(MemberList, Member, ','))
			{
				if (!Member.empty())
				{
					Members.push_back(Member);
				}
			}
			if (Members.size() != 4)
			{
				return "a team is four class ids (damage, tank, special, support): " + Team;
			}
			Out.push_back(Members);
		}
		return Out.empty() ? std::string("no teams given") : std::string();
	}

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

	/** The rules battles are fought on: the game's, unless --rules classic. */
	FTuning Rules = GameTuning();
	std::string RulesName = "game";

	/** A board to fight on, and where blue's four stand (red stands turned about). */
	struct FArena
	{
		std::string Id;
		std::vector<std::string> Top;
		std::vector<FVec2> Spawns;
		std::vector<std::pair<int, int>> Grass;
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
	 * the class's team is four of it. On the game's rules (or the classic ones), on the
	 * arena given.
	 */
	void Deal(FBattle& Battle, const std::string& JobId, int Side, uint64_t Seed, bool bStack, const FArena& Arena,
		size_t TeamIndex = 0, const std::string& OtherId = std::string())
	{
		// With OtherId (the role round-robin), the other side has it in the same seat instead of the team's own.
		std::string Rosters[2][4];
		const int Stands = StandIn(JobId);
		const std::vector<std::string>& Team = Teams[TeamIndex % Teams.size()];
		for (int i = 0; i < 4; ++i)
		{
			Rosters[1 - Side][i] = !OtherId.empty() && i == Stands ? OtherId : Team[static_cast<size_t>(i)];
			Rosters[Side][i] = bStack || i == Stands ? JobId : Team[static_cast<size_t>(i)];
		}
		Battle.Tuning = Rules;
		Battle.Map.BuildMirrored(Arena.Top, Arena.Grass);
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
		Battle.Start(Seed + SeedBase * 1000003ULL);
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

	/** Applies an order, or ends the unit's turn if the rules refuse it. False if even that is refused. */
	bool ApplyOrEnd(FBattle& Battle, FOrder Order, const FUnit* Ready, FTickReport& Report)
	{
		if (!Battle.Validate(Order).empty())
		{
			if (!Ready)
			{
				return false;
			}
			Order = FOrder::MakeEndTurn(Ready->Id, Ready->Serial);
			if (!Battle.Validate(Order).empty())
			{
				return false;
			}
		}
		Battle.Apply(Order, Report);
		return true;
	}

	/**
	 * The search player (2026-10-04): is the hard computer playing this class badly? When the watched unit's turn
	 * comes, it tries several whole turns on copies of the battle -- the hard computer's own, and others from the
	 * medium and easy computers, which now and then take one of their next best options -- plays each on for
	 * SearchSeconds with the hard computer on both sides, and keeps the turn that left its side best off (its
	 * share of health against theirs, a win or a loss counting most). It changes nothing in the rules and reads
	 * nothing hidden: the copies are the battle as it stands. The others are played exactly as the real battle
	 * will play them (the same computer, its generator copied), so the turn kept is the turn the real battle gets.
	 */
	std::vector<FOrder> SearchTurn(const FBattle& Battle, const FAIPlayer& Computer, int UnitId, int Side)
	{
		std::vector<FOrder> Best;
		double BestValue = -1e9;
		int BestCandidate = -1;
		std::vector<std::string> Seen;
		for (int Candidate = 0; Candidate < SearchCandidates; ++Candidate)
		{
			FBattle Copy = Battle;
			FAIPlayer Rest = Computer;
			// The hard computer's own turn first; then turns from the medium and easy computers, which settle for one of
			// their best three or five options now and then, so the candidates differ.
			FAIPlayer Mine(Candidate == 0 ? "hard" : Candidate % 2 == 1 ? "medium" : "easy");
			Mine.Rng.Seed(9001 + static_cast<uint64_t>(Candidate) * 7919 + static_cast<uint64_t>(Battle.TickCount));
			const FUnit* Start = Copy.FindUnit(UnitId);
			if (!Start)
			{
				break;
			}
			const int Serial = Start->Serial;
			std::vector<FOrder> Turn;
			std::string Key;
			int Guard = 0;
			// The turn: the unit's own orders, and anyone else's that come in between, until it has ended.
			while (Copy.Winner == -1 && Guard++ < 400)
			{
				const FUnit* Me = Copy.FindUnit(UnitId);
				if (!Me || !Me->IsAlive() || Me->Serial != Serial || !Me->bReady)
				{
					break;
				}
				const FUnit* Ready = NextReady(Copy);
				FTickReport Report;
				if (Ready && Ready->Id == UnitId)
				{
					FOrder Order = Mine.NextCommand(Copy, *Ready);
					if (!Copy.Validate(Order).empty())
					{
						Order = FOrder::MakeEndTurn(Ready->Id, Ready->Serial);
					}
					Turn.push_back(Order);
					Key += OrderToText(Order) + "|";
					if (!ApplyOrEnd(Copy, Order, Ready, Report))
					{
						break;
					}
				}
				else if (!ApplyOrEnd(Copy, Ready ? Rest.NextCommand(Copy, *Ready) : FOrder::MakeAdvance(1), Ready, Report))
				{
					break;
				}
			}
			if (Turn.empty() || std::find(Seen.begin(), Seen.end(), Key) != Seen.end())
			{
				continue;
			}
			Seen.push_back(Key);
			// Then the battle played on, everyone at hard.
			const int Until = Copy.TickCount + SearchSeconds * Pace::TicksPerSecond;
			Guard = 0;
			while (Copy.Winner == -1 && Copy.TickCount < Until && Guard++ < 20000)
			{
				const FUnit* Ready = NextReady(Copy);
				FTickReport Report;
				if (!ApplyOrEnd(Copy, Ready ? Rest.NextCommand(Copy, *Ready) : FOrder::MakeAdvance(1), Ready, Report))
				{
					break;
				}
			}
			const double Value = HealthShare(Copy, Side) - HealthShare(Copy, 1 - Side)
				+ (Copy.Winner == Side ? 2.0 : Copy.Winner == 1 - Side ? -2.0 : 0.0);
			if (Value > BestValue)
			{
				BestValue = Value;
				Best = Turn;
				BestCandidate = Candidate;
			}
		}
		++SearchedTurns;
		if (BestCandidate > 0)
		{
			++SearchChanged;
		}
		return Best;
	}

	/** One battle through the order path, the class's unit watched throughout. */
	FFought Fight(const std::string& JobId, int Side, uint64_t Seed, uint64_t AiSeed, const char* Skill, int TickLimit,
		bool bStack = false, size_t ArenaIndex = 0, size_t TeamIndex = 0, const std::string& OtherId = std::string())
	{
		FBattle Battle;
		Deal(Battle, JobId, Side, Seed, bStack, Arenas[ArenaIndex % Arenas.size()], TeamIndex, OtherId);
		FAIPlayer Computer(bSearch ? "hard" : Skill);
		Computer.Rng.Seed(AiSeed + SeedBase * 1000003ULL);
		const int Watched = (Side == 0 ? 0 : 4) + (bStack ? 0 : StandIn(JobId));
		// In the round-robin the other class is watched too, so the search player plays both alike.
		const int OtherWatched = OtherId.empty() ? -1 : (Side == 0 ? 4 : 0) + StandIn(JobId);
		// A searched turn's orders, last first, per unit, and the turn they are for.
		std::vector<FOrder> Plans[8];
		int PlanSerial[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };
		int KnockedAt = -1;

		FFought Out;
		while (Battle.Winner == -1 && Battle.TickCount < TickLimit)
		{
			const FUnit* Ready = NextReady(Battle);
			FOrder Order = FOrder::MakeAdvance(1);
			if (Ready && bSearch && !bStack && (Ready->Id == Watched || Ready->Id == OtherWatched))
			{
				std::vector<FOrder>& Plan = Plans[Ready->Id & 7];
				if (PlanSerial[Ready->Id & 7] != Ready->Serial)
				{
					Plan = SearchTurn(Battle, Computer, Ready->Id, Ready->Team);
					std::reverse(Plan.begin(), Plan.end());
					PlanSerial[Ready->Id & 7] = Ready->Serial;
				}
				if (!Plan.empty())
				{
					Order = Plan.back();
					Plan.pop_back();
				}
				else
				{
					Order = Computer.NextCommand(Battle, *Ready);
				}
			}
			else if (Ready)
			{
				Order = Computer.NextCommand(Battle, *Ready);
			}
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

	/**
	 * Reads and registers the class. "" if it is in the game now. With bBan and --ban, that ability is made inert
	 * first: a passive with nothing in it, which nobody can use and which gives nothing.
	 */
	std::string Load(const char* Path, FJobDef& Job, std::vector<FAbility>& Abilities, bool bBan = false)
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
		if (bBan && Banned >= 0 && static_cast<size_t>(Banned) < Abilities.size())
		{
			FAbility& Inert = Abilities[static_cast<size_t>(Banned)];
			Inert.Kind = "passive";
			Inert.Buffs.clear();
			Inert.StatusId.clear();
			Inert.SelfStatusId.clear();
			Inert.Power = 0.0f;
			Inert.TgChange = 0;
		}
		return RegisterJob(Job, Abilities);
	}

	/** --classes: a folder of class files the reference teams may draw on. */
	std::string ClassesDir;

	/**
	 * Registers the class files in --classes, after the command's own, so a class being tested keeps its own
	 * version (a second file with its id is refused); then checks every team member is a class. "" if they are.
	 */
	std::string TeamsReady()
	{
		if (!ClassesDir.empty())
		{
			std::error_code Error;
			for (const auto& Entry : std::filesystem::directory_iterator(ClassesDir, Error))
			{
				const std::string Name = Entry.path().filename().string();
				if (Name.size() > 13 && Name.compare(Name.size() - 13, 13, ".tmclass.json") == 0)
				{
					LoadClassFile(ReadAll(Entry.path().string().c_str()));
				}
			}
		}
		for (const std::vector<std::string>& Team : Teams)
		{
			for (const std::string& Member : Team)
			{
				if (!FindJob(Member))
				{
					return "no class '" + Member + "' for a reference team" + std::string(ClassesDir.empty() ? " (is --classes given?)" : "");
				}
			}
		}
		return std::string();
	}

	/** The 95% Wilson interval of a win rate, in whole percent: [low, high]. */
	std::string Interval(double Wins, int Games)
	{
		if (Games <= 0)
		{
			return "[0,100]";
		}
		const double Z = 1.959964;
		const double P = Wins / Games;
		const double Denominator = 1.0 + Z * Z / Games;
		const double Centre = (P + Z * Z / (2.0 * Games)) / Denominator;
		const double Half = Z * std::sqrt(P * (1.0 - P) / Games + Z * Z / (4.0 * Games * Games)) / Denominator;
		return "[" + Number(static_cast<double>(RoundToInt(100.0 * std::max(0.0, Centre - Half)))) + ","
			+ Number(static_cast<double>(RoundToInt(100.0 * std::min(1.0, Centre + Half)))) + "]";
	}

	/** The teams as the output names them: ["a,b,c,d", ...]. */
	std::string TeamList()
	{
		std::string Out = "[";
		for (size_t t = 0; t < Teams.size(); ++t)
		{
			std::string Members;
			for (size_t i = 0; i < Teams[t].size(); ++i)
			{
				Members += (i ? "," : "") + Teams[t][i];
			}
			Out += std::string(t ? "," : "") + Quote(Members);
		}
		return Out + "]";
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
			const FFought Fought = Fight(JobId, Side, 5001 + Game / 2, 5001 + Game, Skill, 40000, true, static_cast<size_t>(Game / 2),
				TeamFor(Game / 2));
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
		const std::string Problems = Load(Path, Job, Abilities, true);
		if (!Problems.empty())
		{
			std::printf("PLAYTEST {\"ok\":false,\"errors\":%s}\n", ErrorList(Problems).c_str());
			return 1;
		}
		const std::string Missing = TeamsReady();
		if (!Missing.empty())
		{
			std::printf("PLAYTEST {\"ok\":false,\"errors\":%s}\n", ErrorList(Missing).c_str());
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
			const FFought Fought = Fight(Job.Id, Side, 1 + Game / 2, 1 + Game, Skill, 40000, false, static_cast<size_t>(Game / 2),
				TeamFor(Game / 2));
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
		std::string Out = "{\"ok\":true,\"id\":" + Quote(Job.Id) + ",\"games\":" + Number(Games)
			+ ",\"skill\":" + Quote(bSearch ? "search" : Skill);
		if (bSearch)
		{
			Out += ",\"searched\":" + Number(SearchedTurns) + ",\"searchChanged\":" + Number(SearchChanged);
		}
		Out += ",\"rules\":" + Quote(RulesName) + ",\"maps\":" + ArenaList();
		Out += ",\"standsIn\":" + Quote(Reference[StandIn(Job.Id)]);
		Out += ",\"teams\":" + TeamList() + ",\"seedBase\":" + Number(static_cast<double>(SeedBase));
		Out += ",\"banned\":" + Number(Banned);
		Out += ",\"stack\":" + Number(static_cast<double>(RoundToInt(100.0 * Stack)));
		Out += ",\"win\":" + Number(static_cast<double>(RoundToInt(100.0 * Wins / Games)));
		Out += ",\"wins\":" + Number(Wins) + ",\"ci\":" + Interval(Wins, Games);
		Out += ",\"margin\":" + Number(RoundToInt(Margin / Games * 10.0) / 10.0);
		Out += ",\"dealt\":" + Number(static_cast<double>(Dealt / Games));
		Out += ",\"lives\":" + Number(static_cast<double>(AliveTicks / Games / Pace::TicksPerSecond));
		Out += ",\"uses\":[" + Number(Uses[0]) + "," + Number(Uses[1]) + "," + Number(Uses[2]) + "," + Number(Uses[3]) + "]";
		// Uses a game, and the abilities a unit could choose (actives and channels) that it almost never did: the
		// first sign the computer is not playing the class as it is meant to be played.
		Out += ",\"perGame\":[";
		std::string Unused = "[";
		for (int Slot = 0; Slot < 4; ++Slot)
		{
			const double Each = static_cast<double>(Uses[Slot]) / Games;
			Out += std::string(Slot ? "," : "") + Number(RoundToInt(Each * 100.0) / 100.0);
			const bool bChosen = static_cast<size_t>(Slot) < Abilities.size()
				&& (Abilities[static_cast<size_t>(Slot)].Kind == "active" || Abilities[static_cast<size_t>(Slot)].Kind == "channeled");
			if (bChosen && Slot != Banned && Each < 0.1)
			{
				Unused += std::string(Unused.size() > 1 ? "," : "") + Number(Slot);
			}
		}
		Out += "],\"unused\":" + Unused + "]";
		Out += ",\"illegal\":" + Number(Illegal) + ",\"ms\":" + Number(static_cast<double>(Ms)) + "}";
		std::printf("PLAYTEST %s\n", Out.c_str());
		return 0;
	}

	/**
	 * Is a map legal, and how does it play? Read with the game's own checks
	 * (TMSim::ReadMapFile), then fought on: the classic starting four a side,
	 * computer against computer at hard, the seeds counting up. It says how
	 * often each side won -- a fair map is near half and half -- how long a
	 * battle takes and when the first blow lands, where units fell, and how
	 * often each tile was stood on, so ground nobody uses shows up.
	 */
	/** Today's look of each of a class's abilities, as Cast Studio events (CastLegacy.h). */
	int Looks(const char* Path)
	{
		FJobDef Job;
		std::vector<FAbility> Abilities;
		const std::string Problems = Load(Path, Job, Abilities);
		std::string Out = "{\"ok\":" + std::string(Problems.empty() ? "true" : "false") + ",\"errors\":" + ErrorList(Problems);
		if (Problems.empty())
		{
			Out += ",\"id\":" + Quote(Job.Id) + ",\"abilities\":{";
			for (size_t i = 0; i < Abilities.size(); ++i)
			{
				const FAbility& Ability = Abilities[i];
				const int Slot = static_cast<int>(i);
				const TMCast::FLegacyKind Kind = TMCast::LegacyKindOf(Ability);
				Out += std::string(i ? "," : "") + Quote(Ability.Id) + ":{\"slot\":" + Number(Slot)
					+ ",\"flavour\":" + Quote(TMCast::LegacyFlavour(Kind.Flavour).Name)
					+ ",\"motion\":" + Quote(MotionOf(Ability, Slot))
					+ ",\"look\":" + TMCast::LookJson(TMCast::LegacyLook(Ability, Slot)) + "}";
			}
			Out += "},\"footprints\":" + TMCast::LegacyFootprintsJson();
		}
		std::printf("LOOKS %s}\n", Out.c_str());
		return Problems.empty() ? 0 : 1;
	}

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
		Map.BuildMirrored(Def.Top, Def.Grass);
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
		const std::string Missing = TeamsReady();
		if (!Missing.empty())
		{
			std::printf("TOURNEYDONE {\"ok\":false,\"errors\":%s}\n", ErrorList(Missing).c_str());
			return 1;
		}
		for (const std::string& Id : Ids)
		{
			double Wins = 0.0;
			double Margin = 0.0;
			int Illegal = 0;
			for (int Game = 0; Game < Games; ++Game)
			{
				const int Side = Game % 2;
				const FFought Fought = Fight(Id, Side, 1 + Game / 2, 1 + Game, Skill, 40000, false, static_cast<size_t>(Game / 2),
					TeamFor(Game / 2));
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

	/**
	 * The role round-robin (2026-10-04): every class of one role against every other, each in the same seat of
	 * the reference teams, sides alternating, the pairs going round the arenas and teams. The results are fitted
	 * to Bradley-Terry strengths (Hunter's MM algorithm), shown as Elo-style ratings: 0 is the role's average, +100
	 * wins about 64% against it. Unlike playtest this places classes against each other, not against one yardstick,
	 * and the standard error says how far each is to be trusted. With --sample K each class plays only K others.
	 */
	int Rating(const char* Dir, const std::string& Role, int GamesPerPair, const char* Skill, int Sample)
	{
		const auto Start = std::chrono::steady_clock::now();
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
		std::vector<std::string> Ids;
		for (const std::filesystem::path& File : Files)
		{
			FJobDef Job;
			std::vector<FAbility> Abilities;
			if (Load(File.string().c_str(), Job, Abilities).empty() && !Job.Roles.empty() && Job.Roles.front() == Role)
			{
				Ids.push_back(Job.Id);
			}
		}
		for (const char* Builtin : { "squire", "knight", "archer", "monk", "black_mage", "white_mage" })
		{
			const FJobDef* Job = FindJob(Builtin);
			if (Job && !Job->Roles.empty() && Job->Roles.front() == Role)
			{
				Ids.push_back(Builtin);
			}
		}
		const std::string Missing = TeamsReady();
		if (!Missing.empty())
		{
			std::printf("RATINGDONE {\"ok\":false,\"errors\":%s}\n", ErrorList(Missing).c_str());
			return 1;
		}
		const size_t Count = Ids.size();
		if (Count < 2)
		{
			std::printf("RATINGDONE {\"ok\":false,\"errors\":[\"fewer than two classes with that first role\"]}\n");
			return 1;
		}
		// Who plays whom: everyone, or each class against Sample others picked by a fixed shuffle.
		std::vector<std::pair<size_t, size_t>> Pairs;
		std::vector<std::vector<bool>> Paired(Count, std::vector<bool>(Count, false));
		if (Sample <= 0 || static_cast<size_t>(Sample) >= Count - 1)
		{
			for (size_t a = 0; a < Count; ++a)
			{
				for (size_t b = a + 1; b < Count; ++b)
				{
					Pairs.emplace_back(a, b);
				}
			}
		}
		else
		{
			FSimRandom Shuffle;
			Shuffle.Seed(4242 + SeedBase);
			std::vector<int> Played(Count, 0);
			for (size_t a = 0; a < Count; ++a)
			{
				int Tries = 0;
				while (Played[a] < Sample && Tries++ < 1000)
				{
					const size_t b = static_cast<size_t>(Shuffle.RandiRange(0, static_cast<int>(Count) - 1));
					if (b == a || Paired[a][b])
					{
						continue;
					}
					Paired[a][b] = Paired[b][a] = true;
					++Played[a];
					++Played[b];
					Pairs.emplace_back(std::min(a, b), std::max(a, b));
				}
			}
		}
		std::vector<std::vector<double>> Won(Count, std::vector<double>(Count, 0.0));
		std::vector<std::vector<int>> Played(Count, std::vector<int>(Count, 0));
		int Illegal = 0;
		for (size_t p = 0; p < Pairs.size(); ++p)
		{
			const size_t a = Pairs[p].first;
			const size_t b = Pairs[p].second;
			for (int Game = 0; Game < GamesPerPair; ++Game)
			{
				const int Side = Game % 2;
				const uint64_t Seed = 70001 + static_cast<uint64_t>(p) * 1000 + static_cast<uint64_t>(Game / 2);
				const size_t Round = p * static_cast<size_t>((GamesPerPair + 1) / 2) + static_cast<size_t>(Game / 2);
				const FFought Fought = Fight(Ids[a], Side, Seed, Seed + static_cast<uint64_t>(Game), Skill, 40000, false,
					Round, TeamFor(static_cast<int>(Round)), Ids[b]);
				const double Score = Fought.Winner == Side ? 1.0 : Fought.Winner == -1 ? 0.5 : 0.0;
				Won[a][b] += Score;
				Won[b][a] += 1.0 - Score;
				++Played[a][b];
				++Played[b][a];
				Illegal += Fought.Illegal;
			}
		}
		// Bradley-Terry by MM, with one win and one loss against an average opponent (strength 1) for each class,
		// so a class that won or lost everything still gets a finite rating.
		std::vector<double> Strength(Count, 1.0);
		for (int Iteration = 0; Iteration < 2000; ++Iteration)
		{
			double Moved = 0.0;
			std::vector<double> Next(Count, 1.0);
			for (size_t i = 0; i < Count; ++i)
			{
				double Wins = 1.0;
				double Denominator = 2.0 / (Strength[i] + 1.0);
				for (size_t j = 0; j < Count; ++j)
				{
					if (j != i && Played[i][j] > 0)
					{
						Wins += Won[i][j];
						Denominator += Played[i][j] / (Strength[i] + Strength[j]);
					}
				}
				Next[i] = Wins / Denominator;
			}
			double LogMean = 0.0;
			for (const double S : Next)
			{
				LogMean += std::log(S);
			}
			LogMean /= static_cast<double>(Count);
			for (size_t i = 0; i < Count; ++i)
			{
				const double Normal = Next[i] / std::exp(LogMean);
				Moved = std::max(Moved, std::fabs(std::log(Normal) - std::log(Strength[i])));
				Strength[i] = Normal;
			}
			if (Moved < 1e-9)
			{
				break;
			}
		}
		const double EloPerLog = 400.0 / std::log(10.0);
		std::vector<size_t> Order(Count);
		for (size_t i = 0; i < Count; ++i)
		{
			Order[i] = i;
		}
		std::sort(Order.begin(), Order.end(), [&Strength](size_t x, size_t y) { return Strength[x] > Strength[y]; });
		for (const size_t i : Order)
		{
			double Information = 2.0 * Strength[i] / ((Strength[i] + 1.0) * (Strength[i] + 1.0));
			double Wins = 0.0;
			int Games = 0;
			for (size_t j = 0; j < Count; ++j)
			{
				if (j != i && Played[i][j] > 0)
				{
					Information += Played[i][j] * Strength[i] * Strength[j] / ((Strength[i] + Strength[j]) * (Strength[i] + Strength[j]));
					Wins += Won[i][j];
					Games += Played[i][j];
				}
			}
			std::string Out = "{\"id\":" + Quote(Ids[i]) + ",\"rating\":" + Number(RoundToInt(EloPerLog * std::log(Strength[i])));
			Out += ",\"se\":" + Number(RoundToInt(EloPerLog / std::sqrt(Information)));
			Out += ",\"vsAverage\":" + Number(RoundToInt(100.0 * Strength[i] / (Strength[i] + 1.0)));
			Out += ",\"games\":" + Number(Games) + ",\"wins\":" + Number(Wins) + "}";
			std::printf("RATING %s\n", Out.c_str());
		}
		const long long Ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - Start).count();
		std::printf("RATINGDONE {\"ok\":true,\"role\":%s,\"classes\":%d,\"pairs\":%d,\"gamesPerPair\":%d,\"skill\":%s,"
			"\"teams\":%s,\"seedBase\":%s,\"illegal\":%d,\"ms\":%s}\n", Quote(Role).c_str(), static_cast<int>(Count),
			static_cast<int>(Pairs.size()), GamesPerPair, Quote(Skill).c_str(), TeamList().c_str(),
			Number(static_cast<double>(SeedBase)).c_str(), Illegal, Number(static_cast<double>(Ms)).c_str());
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
			Arena.Grass = Def.Grass;
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
	std::string MonstersDir;
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
			if (RulesName != "game" && RulesName != "classic")
			{
				std::fprintf(stderr, "rules must be game or classic\n");
				return 2;
			}
			Rules = RulesName == "game" ? GameTuning() : FTuning();
		}
		else if (Arg == "--maps" && i + 1 < ArgCount)
		{
			MapsDir = Args[++i];
		}
		else if (Arg == "--monsters" && i + 1 < ArgCount)
		{
			MonstersDir = Args[++i];
		}
		else if (Arg == "--classes" && i + 1 < ArgCount)
		{
			ClassesDir = Args[++i];
		}
		else if (Arg == "--teams" && i + 1 < ArgCount)
		{
			const std::string Problem = ParseTeams(Args[++i], Teams);
			if (!Problem.empty())
			{
				std::fprintf(stderr, "%s\n", Problem.c_str());
				return 2;
			}
		}
		else if (Arg == "--seed-base" && i + 1 < ArgCount)
		{
			SeedBase = static_cast<uint64_t>(std::strtoull(Args[++i], nullptr, 10));
		}
		else if (Arg == "--ban" && i + 1 < ArgCount)
		{
			Banned = std::atoi(Args[++i]);
			if (Banned < 0 || Banned > 3)
			{
				std::fprintf(stderr, "--ban takes an ability slot, 0 to 3\n");
				return 2;
			}
		}
		else if (Arg == "--search" && i + 1 < ArgCount)
		{
			// "candidates,seconds", e.g. 6,15.
			const std::string Spec = Args[++i];
			const size_t Comma = Spec.find(',');
			SearchCandidates = std::max(2, std::min(20, std::atoi(Spec.substr(0, Comma).c_str())));
			if (Comma != std::string::npos)
			{
				SearchSeconds = std::max(1, std::min(120, std::atoi(Spec.substr(Comma + 1).c_str())));
			}
		}
		else
		{
			Rest.push_back(Arg);
		}
	}
	// The search player is the hard computer that thinks ahead for the class under test.
	if (std::string(Skill) == "search")
	{
		bSearch = true;
		Skill = "hard";
	}
	// On the game's rules, the game's built-ins too: its Knight and Archer.
	if (RulesName == "game")
	{
		ApplyGameBalance();
	}
	// The monsters, and with them the pets (2026-10-02).
	if (MonstersDir.empty() && MapsDir)
	{
		std::filesystem::path Maps(MapsDir);
		if (!Maps.has_filename())
		{
			Maps = Maps.parent_path();
		}
		const std::filesystem::path Beside = Maps.parent_path() / "Monsters";
		if (std::filesystem::is_directory(Beside))
		{
			MonstersDir = Beside.string();
		}
	}
	if (!MonstersDir.empty() && std::filesystem::is_directory(MonstersDir))
	{
		for (const auto& Entry : std::filesystem::directory_iterator(MonstersDir))
		{
			const std::string Name = Entry.path().filename().string();
			if (Name.size() > 13 && Name.substr(Name.size() - 13) == ".tmclass.json")
			{
				LoadClassFile(ReadAll(Entry.path().string().c_str()));
			}
		}
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
	if (Rest.size() >= 3 && Rest[0] == "rating")
	{
		const int Games = Rest.size() >= 4 ? std::atoi(Rest[3].c_str()) : 4;
		int Sample = 0;
		for (size_t i = 4; i + 1 < Rest.size(); ++i)
		{
			if (Rest[i] == "--sample")
			{
				Sample = std::atoi(Rest[i + 1].c_str());
			}
		}
		if (Games < 2 || Games > 200)
		{
			std::fprintf(stderr, "games a pair must be 2 to 200\n");
			return 2;
		}
		return Rating(Rest[1].c_str(), Rest[2], Games, Skill, Sample);
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
	if (ArgCount >= 3 && std::string(Args[1]) == "looks")
	{
		return Looks(Args[2]);
	}
	if (ArgCount >= 3 && std::string(Args[1]) == "check")
	{
		const bool bBattle = ArgCount >= 4 && std::string(Args[3]) == "--battle";
		return Check(Args[2], bBattle);
	}
	std::fprintf(stderr, "usage:\n  TMClassLab check <class file> [--battle]\n"
		"  TMClassLab playtest <class file> [games, 40] [--skill easy|medium|hard] [--rules game|classic] [--maps <dir>]\n"
		"  TMClassLab tournament <classes dir> [games, 20] [--skill ...] [--rules ...] [--maps <dir>]\n"
		"  TMClassLab rating <classes dir> <role> [games a pair, 4] [--sample K]\n"
		"options: --skill easy|medium|hard|search  --search candidates,seconds  --teams standard|\"a,b,c,d;...\"\n"
		"  --classes <dir>  --seed-base N  --ban <slot 0-3>  --rules game|classic  --maps <dir>  --monsters <dir>\n"
		"  TMClassLab map <map file> [games]\n"
		"  TMClassLab looks <class file>\n");
	return 2;
}
