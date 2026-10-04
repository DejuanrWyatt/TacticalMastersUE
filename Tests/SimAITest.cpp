// Checks the computer player against its recorded baseline (Baselines/AITable.txt,
// first recorded from the Godot version).
//
// It is tested the same way the clock and the pathfinder were, and for the same
// reason: choosing where to stand is a long chain of exact arithmetic over a
// couple of thousand reachable spots, settled by a strict less-than. Two spots
// a tenth of a point apart are not equally good, so a rounding difference in
// what high ground is worth sends a unit to the wrong hill -- which looks like
// a slightly odd opponent rather than like a bug, and would never be found by
// playing.
//
// Ties are the fussy part. The best spot is the first one to beat everything
// before it, so a match depends on walking the reachable spots in the same
// order the baseline walked them, which is the order the pathfinder settled them in.
//
// Four states, because one would not be enough, and each was added because a
// deliberately broken version of the chooser still passed without it. The
// opening has nobody in sight and only tests walking at the enemy's corner.
// "contact" has the two sides looking at each other over ground of three
// heights with two units hurt, so backing away, high ground and the standoff
// distance all count. "hazards" lays burning and healing ground over the whole
// map, because Highlands has none. "springs" lays healing ground against plain
// ground, which is the only state in which the extra value a hurt unit puts on
// it changes the spot it picks.
//
// What these four states do NOT pin down: the health at which a unit starts
// putting extra value on healing ground. Moving that line from 60% to 50% still
// passes, because no unit near the line ever has its choice of spot turned by
// it. Everything else here was checked by breaking it on purpose and watching
// this fail -- the tie order, the standoff distance, what high ground is worth,
// avoiding fire, valuing a spring when hurt, and leaving an ultimate out of the
// reckoning while its meter is empty.
//
// When the computer player is changed on purpose, write the table again from
// the rules with --rebaseline (Baseline.h).
//
//   SimAITest [Baselines/AITable.txt] [--rebaseline]

#include "Baseline.h"
#include "SimAI.h"
#include "SimBattle.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace TMSim;

namespace
{
	int Failures = 0;
	int Shown = 0;
	TMBaseline::FWriter Writer;

	void Fail(const std::string& What)
	{
		if (Shown < 14)
		{
			std::printf("  %s\n", What.c_str());
			++Shown;
		}
		++Failures;
	}

	/**
	 * Where "key=" starts, as a whole word. The boundary matters: looking for
	 * "hp=" without it finds the tail of "maxhp=" first, and then a unit the
	 * dump says is nearly dead is read as being at full health, which changes
	 * what it decides and looks like a fault in the port.
	 */
	size_t FindKey(const std::string& Line, const char* Key)
	{
		const std::string Needle = std::string(Key) + "=";
		size_t At = Line.find(Needle);
		while (At != std::string::npos && At > 0 && !std::isspace(static_cast<unsigned char>(Line[At - 1])))
		{
			At = Line.find(Needle, At + 1);
		}
		return At == std::string::npos ? At : At + Needle.size();
	}

	/** "key=value" as a whole word, or the fallback if it isn't there. */
	double ValueOf(const std::string& Line, const char* Key, double Fallback)
	{
		const size_t At = FindKey(Line, Key);
		return At == std::string::npos ? Fallback : std::atof(Line.c_str() + At);
	}

	/** "key=x,y", the way the dump writes a position. */
	FVec2 PointOf(const std::string& Line, const char* Key)
	{
		const size_t At = FindKey(Line, Key);
		if (At == std::string::npos)
		{
			return FVec2();
		}
		const char* From = Line.c_str() + At;
		const float X = static_cast<float>(std::atof(From));
		const char* Comma = std::strchr(From, ',');
		return FVec2(X, Comma ? static_cast<float>(std::atof(Comma + 1)) : 0.0f);
	}

	std::string Text(const FVec2& P)
	{
		char Buffer[64];
		std::snprintf(Buffer, sizeof(Buffer), "%.2f,%.2f", P.X, P.Y);
		return Buffer;
	}

	/** Positions sit on a half-metre grid, so they should match exactly. */
	bool Same(const FVec2& A, const FVec2& B)
	{
		return std::fabs(A.X - B.X) < 0.001f && std::fabs(A.Y - B.Y) < 0.001f;
	}

	void CheckNear(const std::string& Where, const char* What, double Got, double Want)
	{
		if (std::fabs(Got - Want) > 1e-9)
		{
			char Buffer[220];
			std::snprintf(Buffer, sizeof(Buffer), "%s: %s is %g, the baseline says %g",
				Where.c_str(), What, Got, Want);
			Fail(Buffer);
		}
	}

	/** One state of the board, and what the baseline says was decided about it. */
	struct FScenario
	{
		std::string Name;
		/** "tx,ty=kind" pairs laid over the map before anything is asked. */
		std::string Hazards;
		std::vector<std::string> Units;
		std::vector<std::string> Sight;
		std::vector<std::string> Spots;
		/** Where each of those lines is in the table, for --rebaseline. */
		std::vector<size_t> UnitsAt;
		std::vector<size_t> SightAt;
		std::vector<size_t> SpotsAt;
	};

	int Sights = 0;
	int Spots = 0;
	int Rosters = 0;

	void Run(const FScenario& Scene)
	{
		FBattle Battle;
		Battle.Map.BuildMirrored(HighlandsRows());

		// Ground laid down before anything is asked about it.
		if (!Scene.Hazards.empty())
		{
			std::istringstream Stream(Scene.Hazards);
			std::string Each;
			while (Stream >> Each)
			{
				const size_t Comma = Each.find(',');
				const size_t Equals = Each.find('=');
				if (Comma == std::string::npos || Equals == std::string::npos)
				{
					continue;
				}
				const int TileX = std::atoi(Each.c_str());
				const int TileY = std::atoi(Each.c_str() + Comma + 1);
				const int Kind = std::atoi(Each.c_str() + Equals + 1);
				Battle.Map.Hazards[TileY * Battle.Map.TilesX + TileX] = Kind;
			}
		}

		// Who is on the board, and where, is the state being asked about, so it
		// comes from the dump rather than being guessed at. What that state
		// implies -- each one's health and eyesight, what it can see, where it
		// would rather stand -- is worked out here, and is what is checked.
		for (const std::string& Line : Scene.Units)
		{
			std::istringstream Stream(Line);
			int Id = -1;
			std::string Job;
			Stream >> Id >> Job;
			FUnit Unit;
			Unit.Id = Id;
			Unit.Job = Job;
			Unit.Team = static_cast<int>(ValueOf(Line, "team", 0));
			Battle.Units.push_back(Unit);
		}
		Battle.Start(12345);

		for (size_t u = 0; u < Scene.Units.size(); ++u)
		{
			const std::string& Line = Scene.Units[u];
			int Id = -1;
			std::istringstream(Line) >> Id;
			FUnit* Unit = Battle.FindUnit(Id);
			if (!Unit)
			{
				Fail(Scene.Name + ": there is no unit " + std::to_string(Id));
				continue;
			}
			// Which class each one is was read above; what the class is worth --
			// how much health, how far it sees -- comes out of the ported job
			// table, and that is what these two check.
			const std::string Where = Scene.Name + " unit " + std::to_string(Id);
			if (Writer.bOn)
			{
				// The class's sight and health as the rules have them now.
				Writer.SetAt(Scene.UnitsAt[u], TMBaseline::WithValue(TMBaseline::WithValue(Line, "sight",
					std::to_string(Unit->Stat(EStat::Sight))), "maxhp", std::to_string(Unit->MaxHp())));
			}
			else
			{
				CheckNear(Where, "sight", Unit->Stat(EStat::Sight), ValueOf(Line, "sight", 0.0));
				CheckNear(Where, "max health", Unit->MaxHp(), ValueOf(Line, "maxhp", 0.0));
			}
			Unit->Pos = PointOf(Line, "pos");
			Unit->Hp = static_cast<int>(ValueOf(Line, "hp", Unit->MaxHp()));
			++Rosters;
		}

		// What each side has actually spotted. A unit only walks at enemies it
		// can see, so this decides everything below it.
		for (size_t s = 0; s < Scene.Sight.size(); ++s)
		{
			const std::string& Line = Scene.Sight[s];
			std::istringstream Stream(Line);
			int Id = -1;
			std::string Listed;
			Stream >> Id >> Listed;
			const FUnit* Unit = Battle.FindUnit(Id);
			if (!Unit)
			{
				continue;
			}
			if (Writer.bOn)
			{
				// Every enemy this side sees, as the rules see it now.
				std::string Seen;
				for (const FUnit* Enemy : Battle.TeamUnits(1 - Unit->Team))
				{
					if (Battle.CanSee(Unit->Team, Enemy->Pos))
					{
						Seen += (Seen.empty() ? "" : ",") + std::to_string(Enemy->Id);
					}
				}
				const std::string Now = Line.substr(0, Line.find(std::to_string(Id))) + std::to_string(Id) + " " + (Seen.empty() ? "-" : Seen);
				// Only the list matters: a list in another order is the same list.
				bool bSame = true;
				for (const FUnit* Enemy : Battle.TeamUnits(1 - Unit->Team))
				{
					const bool bIn = Listed != "-" && ("," + Listed + ",").find("," + std::to_string(Enemy->Id) + ",") != std::string::npos;
					bSame = bSame && bIn == Battle.CanSee(Unit->Team, Enemy->Pos);
				}
				if (!bSame)
				{
					Writer.SetAt(Scene.SightAt[s], Now);
				}
				Sights += static_cast<int>(Battle.TeamUnits(1 - Unit->Team).size());
				continue;
			}
			for (const FUnit* Enemy : Battle.TeamUnits(1 - Unit->Team))
			{
				const std::string Needle = std::to_string(Enemy->Id);
				const bool bGot = Battle.CanSee(Unit->Team, Enemy->Pos);
				const bool bWant = Listed != "-"
					&& ("," + Listed + ",").find("," + Needle + ",") != std::string::npos;
				if (bGot != bWant)
				{
					Fail(Scene.Name + ": side " + std::to_string(Unit->Team)
						+ (bGot ? " sees " : " cannot see ") + "unit " + Needle
						+ ", the baseline says otherwise");
				}
				++Sights;
			}
		}

		FAIPlayer Ai("hard");
		for (size_t p = 0; p < Scene.Spots.size(); ++p)
		{
			const std::string& Line = Scene.Spots[p];
			std::istringstream Stream(Line);
			int Id = -1;
			std::string Pace;
			Stream >> Id >> Pace;
			const FUnit* Unit = Battle.FindUnit(Id);
			if (!Unit)
			{
				continue;
			}
			const bool bSprint = Pace == "sprint";
			const std::string Where = Scene.Name + ": unit " + std::to_string(Id) + " would " + Pace;

			const FVec2 Approach = Ai.ApproachSpot(Battle, *Unit, bSprint);
			const FVec2 WantApproach = PointOf(Line, "approach");
			if (Writer.bOn)
			{
				const FVec2 Back = Ai.RetreatSpot(Battle, *Unit, bSprint);
				if (!Same(Approach, WantApproach) || !Same(Back, PointOf(Line, "retreat")))
				{
					Writer.SetAt(Scene.SpotsAt[p], TMBaseline::WithValue(TMBaseline::WithValue(Line, "approach", Text(Approach)),
						"retreat", Text(Back)));
				}
				Spots += 2;
				continue;
			}
			if (!Same(Approach, WantApproach))
			{
				Fail(Where + " to " + Text(Approach) + ", the baseline picks " + Text(WantApproach));
			}

			const FVec2 Retreat = Ai.RetreatSpot(Battle, *Unit, bSprint);
			const FVec2 WantRetreat = PointOf(Line, "retreat");
			if (!Same(Retreat, WantRetreat))
			{
				Fail(Where + " back off to " + Text(Retreat) + ", the baseline picks " + Text(WantRetreat));
			}
			Spots += 2;
		}
	}
}

int main(int argc, char** argv)
{
	const std::vector<std::string> Args = TMBaseline::Paths(argc, argv);
	const std::string Path = Args.empty() ? std::string("Baselines/AITable.txt") : Args[0];
	Writer.bOn = TMBaseline::Asked(argc, argv);
	Writer.Path = Path;
	std::ifstream File(Path);
	if (!File)
	{
		std::printf("Could not open %s\n", Path.c_str());
		return 1;
	}

	std::vector<FScenario> Scenes;
	std::string Pending;  // ground laid down, which belongs to the next state
	std::string Section;

	// The settings every decision below is made under.
	FBattle Header;
	Header.Map.BuildMirrored(HighlandsRows());
	Header.Start(12345);

	std::string Line;
	while (std::getline(File, Line))
	{
		if (!Line.empty() && Line.back() == '\r')
		{
			Line.pop_back();
		}
		const size_t At = Writer.Next();
		Writer.Read(Line);
		while (!Line.empty() && std::isspace(static_cast<unsigned char>(Line.back())))
		{
			Line.pop_back();
		}
		if (Line.empty())
		{
			continue;
		}

		// A difference in the settings would show up much further along, as a
		// unit standing in the wrong place for no visible reason.
		if (Line.rfind("TUNING", 0) == 0)
		{
			if (Writer.bOn)
			{
				if (std::fabs(Header.Tuning.SightMultiplier - ValueOf(Line, "sight_multiplier", 0.0)) > 1e-9
					|| std::fabs(Header.Tuning.HazardPercent - ValueOf(Line, "hazard_percent", 0.0)) > 1e-9)
				{
					Writer.SetAt(At, TMBaseline::WithValue(TMBaseline::WithValue(Line, "sight_multiplier",
						TMBaseline::Number(Header.Tuning.SightMultiplier)), "hazard_percent", TMBaseline::Number(Header.Tuning.HazardPercent)));
				}
				continue;
			}
			CheckNear("tuning", "sight multiplier", Header.Tuning.SightMultiplier,
				ValueOf(Line, "sight_multiplier", 0.0));
			CheckNear("tuning", "hazard percent", Header.Tuning.HazardPercent,
				ValueOf(Line, "hazard_percent", 0.0));
			continue;
		}
		if (Line.rfind("SPAWNS", 0) == 0)
		{
			// Where each side started: a unit that can see nobody walks at it.
			std::istringstream Parts(Line.substr(6));
			std::string Written;
			if (Writer.bOn)
			{
				const std::string Now = "SPAWNS " + Text(Header.SpawnPoints[0]) + " " + Text(Header.SpawnPoints[1]);
				std::string First;
				std::string Second;
				Parts >> First >> Second;
				const auto Point = [](const std::string& W) { const size_t C = W.find(','); return FVec2(static_cast<float>(std::atof(W.c_str())),
					static_cast<float>(std::atof(W.c_str() + (C == std::string::npos ? 0 : C + 1)))); };
				if (!Same(Point(First), Header.SpawnPoints[0]) || !Same(Point(Second), Header.SpawnPoints[1]))
				{
					Writer.SetAt(At, Now);
				}
				continue;
			}
			for (int Team = 0; Team < 2 && (Parts >> Written); ++Team)
			{
				const size_t Comma = Written.find(',');
				const FVec2 Wanted(static_cast<float>(std::atof(Written.c_str())),
					static_cast<float>(std::atof(Written.c_str() + Comma + 1)));
				if (!Same(Header.SpawnPoints[Team], Wanted))
				{
					Fail("team " + std::to_string(Team) + " starts at "
						+ Text(Header.SpawnPoints[Team]) + ", the baseline says " + Text(Wanted));
				}
			}
			continue;
		}
		if (Line.rfind("HAZARDS", 0) == 0)
		{
			Pending = Line.substr(7);
			continue;
		}
		if (Line.rfind("SCENARIO", 0) == 0)
		{
			FScenario Scene;
			Scene.Name = Line.substr(9);
			Scene.Hazards = Pending;
			Pending.clear();
			Scenes.push_back(Scene);
			Section.clear();
			continue;
		}
		if (Line == "UNITS" || Line == "SIGHT" || Line == "SPOTS")
		{
			Section = Line;
			continue;
		}
		if (Scenes.empty() || Section.empty())
		{
			continue;
		}
		FScenario& Scene = Scenes.back();
		if (Section == "UNITS") { Scene.Units.push_back(Line); Scene.UnitsAt.push_back(At); }
		else if (Section == "SIGHT") { Scene.Sight.push_back(Line); Scene.SightAt.push_back(At); }
		else if (Section == "SPOTS") { Scene.Spots.push_back(Line); Scene.SpotsAt.push_back(At); }
	}

	for (const FScenario& Scene : Scenes)
	{
		Run(Scene);
	}

	std::printf("%d states, %d units placed\n", static_cast<int>(Scenes.size()), Rosters);
	std::printf("%d sight checks, %d chosen spots\n", Sights, Spots);

	// A reference that had come out empty would pass every check in it.
	if (Scenes.size() < 4 || Rosters < 32 || Sights < 128 || Spots < 128)
	{
		std::printf("THE BASELINE IS SHORT -- it has lost situations; restore it from git\n");
		return 1;
	}
	if (Writer.bOn)
	{
		return Writer.Finish() ? 0 : 1;
	}
	if (Failures > 0)
	{
		std::printf("THE COMPUTER PLAYER DECIDES DIFFERENTLY (%d)\n", Failures);
		return 1;
	}
	std::printf("THE COMPUTER PLAYER STANDS WHERE ITS BASELINE SAYS\n");
	return 0;
}
