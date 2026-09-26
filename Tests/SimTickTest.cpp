// Replays a battle's clock against the Godot version, tick by tick.
//
// GodotTickTrace.txt was produced by the Godot game itself: eight units on
// Highlands, nobody giving an order, four hundred ticks. With no orders every
// turn is lost to the countdown, which runs the whole cycle over and over --
// the gauge filling, a unit becoming ready, the countdown draining, the turn
// ending and the gauge starting again.
//
// Every unit's gauge, ready flag and countdown is compared on every tick. If
// this passes, the port keeps the same time as the original; if it drifts, the
// tick it first drifts on is printed, which is usually enough to find why.

#include "SimBattle.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace TMSim;

namespace
{
	struct FExpected
	{
		int Tg = 0;
		int Ready = 0;
		int Clock = 0;
	};

	struct FTraceFile
	{
		std::vector<FUnit> Units;
		std::map<std::string, FJobStats> Jobs;
		FTuning Tuning;
		/** Per tick, then per unit id. */
		std::vector<std::map<int, FExpected>> Ticks;
	};

	/** "speed=6" -> 6, or Fallback when the key is not on the line. */
	int ValueOf(const std::string& Line, const char* Key, int Fallback = 0)
	{
		const std::string Needle = std::string(Key) + "=";
		const size_t At = Line.find(Needle);
		if (At == std::string::npos)
		{
			return Fallback;
		}
		return std::atoi(Line.c_str() + At + Needle.size());
	}

	double DoubleOf(const std::string& Line, const char* Key, double Fallback)
	{
		const std::string Needle = std::string(Key) + "=";
		const size_t At = Line.find(Needle);
		if (At == std::string::npos)
		{
			return Fallback;
		}
		return std::atof(Line.c_str() + At + Needle.size());
	}

	bool Load(const char* Path, FTraceFile& Out)
	{
		std::ifstream File(Path);
		if (!File)
		{
			std::printf("could not open %s\n", Path);
			return false;
		}

		std::string Line;
		enum class ESection { None, Units, Trace } Section = ESection::None;
		while (std::getline(File, Line))
		{
			if (!Line.empty() && Line.back() == '\r')
			{
				Line.pop_back();
			}
			if (Line.rfind("UNITS", 0) == 0) { Section = ESection::Units; continue; }
			if (Line.rfind("TRACE", 0) == 0) { Section = ESection::Trace; continue; }
			if (Line.rfind("TUNING", 0) == 0)
			{
				Out.Tuning.ClockBase = static_cast<float>(DoubleOf(Line, "clock_base", 8.0));
				Out.Tuning.PatienceMultiplier = static_cast<float>(DoubleOf(Line, "patience_multiplier", 2.0));
				Out.Tuning.SpeedMultiplier = static_cast<float>(DoubleOf(Line, "speed_multiplier", 1.0));
				Out.Tuning.HustleBonus = static_cast<float>(DoubleOf(Line, "hustle_bonus", 25.0));
				continue;
			}

			std::istringstream Stream(Line);
			if (Section == ESection::Units)
			{
				int Id = 0;
				std::string Job;
				if (!(Stream >> Id >> Job))
				{
					continue;
				}
				FJobStats Stats;
				Stats.Set(EStat::Speed, ValueOf(Line, "speed"));
				Stats.Set(EStat::Patience, ValueOf(Line, "patience"));
				Stats.Set(EStat::Hp, ValueOf(Line, "hp"));
				Out.Jobs[Job] = Stats;

				FUnit Unit;
				Unit.Id = Id;
				Unit.Job = Job;
				Unit.Team = Id < 4 ? 0 : 1;
				Out.Units.push_back(Unit);
			}
			else if (Section == ESection::Trace)
			{
				int Tick = 0;
				if (!(Stream >> Tick))
				{
					continue;
				}
				std::map<int, FExpected> Row;
				std::string Cell;
				while (Stream >> Cell)
				{
					int Id = 0, Tg = 0, Ready = 0, Clock = 0;
					if (std::sscanf(Cell.c_str(), "%d:%d:%d:%d", &Id, &Tg, &Ready, &Clock) == 4)
					{
						Row[Id] = { Tg, Ready, Clock };
					}
				}
				Out.Ticks.push_back(Row);
			}
		}
		return !Out.Units.empty() && !Out.Ticks.empty();
	}
}

int main(int argc, char** argv)
{
	const char* Path = argc > 1 ? argv[1] : "GodotTickTrace.txt";
	FTraceFile Trace;
	if (!Load(Path, Trace))
	{
		std::printf("FAILED: could not read the trace\n");
		return 1;
	}

	FBattle Battle;
	Battle.Tuning = Trace.Tuning;
	Battle.Units = Trace.Units;
	// The stats live in the map, so point each unit at its class's copy after
	// the vector has stopped moving around.
	for (FUnit& Unit : Battle.Units)
	{
		Unit.Stats = &Trace.Jobs[Unit.Job];
	}
	Battle.Start(12345);

	std::printf("%d units, %d ticks of Godot to match\n",
		static_cast<int>(Battle.Units.size()), static_cast<int>(Trace.Ticks.size()));

	// The head start each unit gets from its Speed, before any time passes.
	int Failures = 0;
	for (const FUnit& Unit : Battle.Units)
	{
		const int Want = Unit.Stat(EStat::Speed) * Pace::StartTgPerSpeed;
		if (Unit.Tg != Want)
		{
			std::printf("  unit %d starts on %d, Godot starts it on %d\n", Unit.Id, Unit.Tg, Want);
			++Failures;
		}
	}

	for (size_t i = 0; i < Trace.Ticks.size() && Failures < 5; ++i)
	{
		FTickReport Report;
		Battle.Tick(Report);

		for (const FUnit& Unit : Battle.Units)
		{
			const auto Found = Trace.Ticks[i].find(Unit.Id);
			if (Found == Trace.Ticks[i].end())
			{
				continue;
			}
			const FExpected& Want = Found->second;
			const int Ready = Unit.bReady ? 1 : 0;
			if (Unit.Tg != Want.Tg || Ready != Want.Ready || Unit.Clock != Want.Clock)
			{
				std::printf("  tick %d unit %d: got tg=%d ready=%d clock=%d, Godot had tg=%d ready=%d clock=%d\n",
					static_cast<int>(i) + 1, Unit.Id, Unit.Tg, Ready, Unit.Clock,
					Want.Tg, Want.Ready, Want.Clock);
				++Failures;
			}
		}
	}

	std::printf("\n%s\n", Failures == 0
		? "THE CLOCK KEEPS THE SAME TIME AS GODOT"
		: "DRIFTED FROM GODOT");
	return Failures == 0 ? 0 : 1;
}
