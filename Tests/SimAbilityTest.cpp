// Checks what abilities do against the Godot game.
//
// Three sections, because an ability can be wrong in three different ways.
//
// PREVIEW is who it would reach and for how much, worked out without doing any
// of it. That part has no dice in it, so it is checked over a wide matrix: every
// class, every slot, aimed at every unit and at patches of empty ground.
//
// RESOLVE is what actually happens, which is a different question because of the
// dice. Every case starts from the same board with the generator put in a known
// place, so this only passes if the port draws the same numbers in the same
// order. That order is the thing most likely to be wrong and the hardest to
// notice: a port that rolls the crit before the evade agrees about very nearly
// everything, and then desyncs a match ten seconds after the mistake.
//
// TRACE is the clock. A cast is begun and lands several ticks later, and where it
// lands is decided when it lands rather than when it was cast, so a spell aimed
// at somebody follows them and one aimed at the ground does not. Tick by tick,
// because a cast resolving one tick early is invisible in anything coarser.
//
// Regenerate with tests/dump_ability_table.gd in the Godot project; the table's
// first line records which script and which Godot build produced it. Never edit
// the table to make this pass -- if the two disagree, one of them is wrong about
// the rules, and the table is the one that was measured.

#include "SimAbility.h"
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

	void Fail(const std::string& What)
	{
		if (Shown < 16)
		{
			std::printf("  %s\n", What.c_str());
			++Shown;
		}
		++Failures;
	}

	/**
	 * Where "key=" starts, as a whole word. The boundary matters: looking for
	 * "hp=" without it finds the tail of "maxhp=" first, and a unit the dump says
	 * is nearly dead is then read as being at full health.
	 */
	size_t FindKey(const std::string& Line, const char* Key)
	{
		const std::string Needle = std::string(Key) + "=";
		size_t At = Line.find(Needle);
		while (At != std::string::npos && At > 0
			&& !std::isspace(static_cast<unsigned char>(Line[At - 1])))
		{
			At = Line.find(Needle, At + 1);
		}
		return At == std::string::npos ? At : At + Needle.size();
	}

	double ValueOf(const std::string& Line, const char* Key, double Fallback = 0.0)
	{
		const size_t At = FindKey(Line, Key);
		return At == std::string::npos ? Fallback : std::atof(Line.c_str() + At);
	}

	std::string TextOf(const std::string& Line, const char* Key)
	{
		const size_t At = FindKey(Line, Key);
		if (At == std::string::npos)
		{
			return std::string();
		}
		const size_t End = Line.find(' ', At);
		return Line.substr(At, End == std::string::npos ? std::string::npos : End - At);
	}

	FVec2 PointOf(const std::string& Line, const char* Key)
	{
		const size_t At = FindKey(Line, Key);
		if (At == std::string::npos)
		{
			return FVec2();
		}
		const char* From = Line.c_str() + At;
		const char* Comma = std::strchr(From, ',');
		return FVec2(static_cast<float>(std::atof(From)),
			Comma ? static_cast<float>(std::atof(Comma + 1)) : 0.0f);
	}

	std::string Text(const FVec2& P)
	{
		char Buffer[64];
		std::snprintf(Buffer, sizeof(Buffer), "%.2f,%.2f", P.X, P.Y);
		return Buffer;
	}

	/** Splits "a|b|c" the way the dump joins lists; "-" means nothing. */
	std::vector<std::string> Parts(const std::string& Joined)
	{
		std::vector<std::string> Out;
		if (Joined.empty() || Joined == "-")
		{
			return Out;
		}
		size_t At = 0;
		while (At <= Joined.size())
		{
			const size_t Bar = Joined.find('|', At);
			Out.push_back(Joined.substr(At, Bar == std::string::npos ? std::string::npos : Bar - At));
			if (Bar == std::string::npos)
			{
				break;
			}
			At = Bar + 1;
		}
		return Out;
	}

	std::string Trimmed(std::string Line)
	{
		while (!Line.empty() && std::isspace(static_cast<unsigned char>(Line.back())))
		{
			Line.pop_back();
		}
		size_t At = 0;
		while (At < Line.size() && std::isspace(static_cast<unsigned char>(Line[At])))
		{
			++At;
		}
		return Line.substr(At);
	}

	/** How the port sees a unit, in the same shape the dump writes it. */
	std::string StateOf(const FUnit& Unit)
	{
		std::string Statuses;
		for (const FStatus& Status : Unit.Statuses)
		{
			if (!Statuses.empty())
			{
				Statuses += "|";
			}
			Statuses += Status.Id + ":" + std::to_string(Status.Turns) + ":"
				+ std::to_string(Status.Amount) + ":" + std::to_string(Status.By);
		}
		std::string Buffs;
		for (const FBuff& Buff : Unit.Buffs)
		{
			if (!Buffs.empty())
			{
				Buffs += "|";
			}
			Buffs += std::string(StatName(Buff.Stat)) + ":" + std::to_string(Buff.Amount)
				+ ":" + std::to_string(Buff.Turns);
		}
		char Buffer[320];
		std::snprintf(Buffer, sizeof(Buffer),
			"hp=%d tg=%d ult=%d ready=%d moved=%d acted=%d cd=%d,%d,%d,%d statuses=%s buffs=%s",
			Unit.Hp, Unit.Tg, Unit.Ult, Unit.bReady ? 1 : 0, Unit.bMoved ? 1 : 0,
			Unit.bActed ? 1 : 0, Unit.Cooldowns[0], Unit.Cooldowns[1], Unit.Cooldowns[2],
			Unit.Cooldowns[3], Statuses.empty() ? "-" : Statuses.c_str(),
			Buffs.empty() ? "-" : Buffs.c_str());
		return Buffer;
	}

	/** Everything the dump says about the starting board. */
	struct FBoard
	{
		std::vector<std::string> UnitLines;
		std::vector<std::string> Tuning;
	};

	void Furnish(FBattle& Battle, const FBoard& Board)
	{
		Battle.Units.clear();
		Battle.Map.BuildMirrored(HighlandsRows());
		for (const std::string& Line : Board.UnitLines)
		{
			std::istringstream Stream(Line);
			int Id = -1;
			std::string Job;
			Stream >> Id >> Job;
			FUnit Unit;
			Unit.Id = Id;
			Unit.Job = Job;
			Unit.Team = static_cast<int>(ValueOf(Line, "team"));
			Battle.Units.push_back(Unit);
		}
		Battle.Start(12345);
		for (const std::string& Line : Board.UnitLines)
		{
			int Id = -1;
			std::istringstream(Line) >> Id;
			FUnit* Unit = Battle.FindUnit(Id);
			if (!Unit)
			{
				continue;
			}
			Unit->Pos = PointOf(Line, "pos");
			Unit->Facing = PointOf(Line, "facing");
			Unit->Hp = static_cast<int>(ValueOf(Line, "hp"));
			Unit->Tg = static_cast<int>(ValueOf(Line, "tg"));
			Unit->Ult = static_cast<int>(ValueOf(Line, "ult"));
			Unit->KoTicks = static_cast<int>(ValueOf(Line, "ko"));
			Unit->Clock = static_cast<int>(ValueOf(Line, "clock"));
			Unit->bReady = ValueOf(Line, "ready") != 0.0;
			Unit->Statuses.clear();
			for (const std::string& Each : Parts(TextOf(Line, "statuses")))
			{
				FStatus Status;
				std::istringstream Bits(Each);
				std::string Field;
				std::vector<std::string> Fields;
				while (std::getline(Bits, Field, ':'))
				{
					Fields.push_back(Field);
				}
				if (Fields.size() >= 4)
				{
					Status.Id = Fields[0];
					Status.Turns = std::atoi(Fields[1].c_str());
					Status.Amount = std::atoi(Fields[2].c_str());
					Status.By = std::atoi(Fields[3].c_str());
					Unit->Statuses.push_back(Status);
				}
			}
		}
	}

	int Previews = 0;
	int Resolves = 0;
	int Ticks = 0;
	int Uses = 0;
}

int main(int argc, char** argv)
{
	const std::string Path = argc > 1 ? argv[1] : "GodotAbilityTable.txt";
	std::ifstream File(Path);
	if (!File)
	{
		std::printf("Could not open %s\n", Path.c_str());
		return 1;
	}

	std::vector<std::string> Lines;
	std::string Read;
	while (std::getline(File, Read))
	{
		Read = Trimmed(Read);
		if (!Read.empty())
		{
			Lines.push_back(Read);
		}
	}

	// ------------------------------------------------ the settings and the board
	FBoard Board;
	FBattle Base;
	std::string Section;
	for (const std::string& Line : Lines)
	{
		if (Line.rfind("TUNING", 0) == 0)
		{
			Base.Tuning.DamageMultiplier = ValueOf(Line, "damage_multiplier");
			Base.Tuning.HealMultiplier = ValueOf(Line, "heal_multiplier");
			Base.Tuning.HeightBonus = ValueOf(Line, "height_bonus");
			Base.Tuning.SideBonus = ValueOf(Line, "side_bonus");
			Base.Tuning.BackBonus = ValueOf(Line, "back_bonus");
			Base.Tuning.CritMultiplier = ValueOf(Line, "crit_multiplier");
			Base.Tuning.EvadeMultiplier = ValueOf(Line, "evade_multiplier");
			Base.Tuning.CritChanceMultiplier = ValueOf(Line, "crit_chance_multiplier");
			Base.Tuning.CastTimeMultiplier = ValueOf(Line, "cast_time_multiplier");
			Base.Tuning.KoSeconds = ValueOf(Line, "ko_seconds");
			Base.Tuning.UltPerAction = ValueOf(Line, "ult_per_action");
			Base.Tuning.UltPerTurn = ValueOf(Line, "ult_per_turn");
			Base.Tuning.StunTgPercent = ValueOf(Line, "stun_tg_percent");
			Base.Tuning.HazardPercent = ValueOf(Line, "hazard_percent");
			Base.Tuning.RegenPercent = ValueOf(Line, "regen_percent");
			Base.Tuning.RegenAfterTurns = ValueOf(Line, "regen_after_turns");
			Base.Tuning.ClockBase = ValueOf(Line, "clock_base");
			Base.Tuning.PatienceMultiplier = ValueOf(Line, "patience_multiplier");
			Base.Tuning.SpeedMultiplier = ValueOf(Line, "speed_multiplier");
			continue;
		}
		if (Line.rfind("CONSTANTS", 0) == 0)
		{
			// Numbers baked into both versions. A difference here would make every
			// case below wrong in a way that looks like arithmetic.
			if (Pace::TgMax != static_cast<int>(ValueOf(Line, "tg_max")))
			{
				Fail("a full gauge is a different number here");
			}
			if (Pace::UltMax != static_cast<int>(ValueOf(Line, "ult_max")))
			{
				Fail("a full ultimate meter is a different number here");
			}
			if (std::fabs(Ground::HitRadius - ValueOf(Line, "hit_radius")) > 0.001)
			{
				Fail("the radius a shot catches things in differs");
			}
			if (std::fabs(Ground::MeleeRange - ValueOf(Line, "melee_range")) > 0.001)
			{
				Fail("arm's length is a different distance here");
			}
			if (Pace::TicksPerSecond != static_cast<int>(ValueOf(Line, "ticks_per_second")))
			{
				Fail("the clock runs at a different rate");
			}
			continue;
		}
		if (Line == "UNITS" || Line == "PREVIEW" || Line == "RESOLVE" || Line == "USE" || Line == "TRACE")
		{
			Section = Line;
			continue;
		}
		if (Section == "UNITS")
		{
			Board.UnitLines.push_back(Line);
		}
	}

	if (Board.UnitLines.empty())
	{
		std::printf("THE REFERENCE HAS NO BOARD IN IT\n");
		return 1;
	}

	Furnish(Base, Board);
	for (const std::string& Line : Board.UnitLines)
	{
		int Id = -1;
		std::istringstream(Line) >> Id;
		const FUnit* Unit = Base.FindUnit(Id);
		if (!Unit)
		{
			Fail("there is no unit " + std::to_string(Id));
			continue;
		}
		// The class each one is came out of the dump; what the class is worth comes
		// out of the ported job table, so that is what is checked.
		if (Unit->MaxHp() != static_cast<int>(ValueOf(Line, "maxhp")))
		{
			Fail("unit " + std::to_string(Id) + " has " + std::to_string(Unit->MaxHp())
				+ " health at most, Godot says " + std::to_string(static_cast<int>(ValueOf(Line, "maxhp"))));
		}
	}

	// -------------------------------------------------------------- the forecast
	Section.clear();
	for (const std::string& Line : Lines)
	{
		if (Line == "UNITS" || Line == "PREVIEW" || Line == "RESOLVE" || Line == "USE" || Line == "TRACE")
		{
			Section = Line;
			continue;
		}
		if (Section != "PREVIEW" || Line.rfind("PREVIEW CASES", 0) == 0)
		{
			continue;
		}
		std::istringstream Stream(Line);
		int CasterId = -1;
		int Slot = -1;
		std::string Aim;
		Stream >> CasterId >> Slot >> Aim;
		const FUnit* Caster = Base.FindUnit(CasterId);
		if (!Caster)
		{
			continue;
		}
		const size_t Comma = Aim.find(',');
		const FVec2 At(static_cast<float>(std::atof(Aim.c_str())),
			Comma == std::string::npos ? 0.0f : static_cast<float>(std::atof(Aim.c_str() + Comma + 1)));

		const std::string Where = "unit " + std::to_string(CasterId) + " slot "
			+ std::to_string(Slot) + " at " + Text(At);

		const bool bInRange = Base.InAbilityRange(*Caster, Slot, Caster->Pos, At);
		if (bInRange != (ValueOf(Line, "inrange") != 0.0))
		{
			Fail(Where + (bInRange ? " is in range" : " is out of range") + ", Godot disagrees");
		}
		const bool bSees = Base.HasLineOfSight(Caster->Pos, At);
		if (bSees != (ValueOf(Line, "los") != 0.0))
		{
			Fail(Where + (bSees ? " has a clear line" : " has no line") + ", Godot disagrees");
		}

		// Who it reaches, for how much, and from which side -- in order, because
		// resolution rolls the dice walking this same list.
		std::string Got;
		for (const FHit& Hit : Base.Preview(*Caster, Slot, Caster->Pos, At))
		{
			if (!Got.empty())
			{
				Got += "|";
			}
			char Buffer[64];
			std::snprintf(Buffer, sizeof(Buffer), "%d:%d:%.2f", Hit.UnitId, Hit.Amount, Hit.Flank);
			Got += Buffer;
		}
		const std::string Want = TextOf(Line, "hits");
		if ((Got.empty() ? "-" : Got) != Want)
		{
			Fail(Where + " reaches " + (Got.empty() ? "nobody" : Got) + ", Godot says " + Want);
		}
		++Previews;
	}

	// ----------------------------------------------------------- what it actually does
	Section.clear();
	int CaseCaster = -1;
	int CaseSlot = -1;
	FVec2 CaseAim;
	int CaseSeed = 0;
	int CaseNumber = -1;
	FBattle Work;
	bool bInCase = false;
	for (const std::string& Line : Lines)
	{
		if (Line == "UNITS" || Line == "PREVIEW" || Line == "RESOLVE" || Line == "USE" || Line == "TRACE")
		{
			Section = Line;
			bInCase = false;
			continue;
		}
		if (Section != "RESOLVE")
		{
			continue;
		}

		if (Line.rfind("case=", 0) == 0)
		{
			CaseNumber = static_cast<int>(ValueOf(Line, "case"));
			CaseCaster = static_cast<int>(ValueOf(Line, "caster"));
			CaseSlot = static_cast<int>(ValueOf(Line, "slot"));
			CaseAim = PointOf(Line, "aim");
			CaseSeed = static_cast<int>(ValueOf(Line, "seed"));

			// A clean board each time, and the generator put exactly where Godot
			// put it, so the case answers for itself.
			Work = Base;
			Work.Rng.Seed(static_cast<uint64_t>(CaseSeed));
			bInCase = true;
			continue;
		}
		if (!bInCase)
		{
			continue;
		}

		if (Line.rfind("struck=", 0) == 0)
		{
			FUnit* Caster = Work.FindUnit(CaseCaster);
			if (!Caster)
			{
				bInCase = false;
				continue;
			}
			// Godot skipped the cases it would refuse; so does this.
			if (!Work.AbilityBlockedReason(*Caster, CaseSlot).empty())
			{
				bInCase = false;
				continue;
			}
			FTickReport Report;
			Work.ResolveAbility(*Caster, CaseSlot, CaseAim, Report);

			const std::string Where = "case " + std::to_string(CaseNumber) + " (unit "
				+ std::to_string(CaseCaster) + " slot " + std::to_string(CaseSlot) + ")";

			std::string Struck;
			std::string Evaded;
			std::string Crits;
			// One pass, in the order the events came, because that is the order the
			// targets were taken in and the order the dice were thrown in. Godot
			// lists a target that dodged among the struck with nothing taken off,
			// in its own place rather than at either end.
			for (const FEvent& Event : Report.Events)
			{
				if (Event.Kind == EEventKind::Hit && Event.By == CaseCaster)
				{
					if (!Struck.empty()) { Struck += "|"; }
					Struck += std::to_string(Event.Unit) + ":" + std::to_string(Event.Amount);
				}
				else if (Event.Kind == EEventKind::Evaded)
				{
					if (!Struck.empty()) { Struck += "|"; }
					Struck += std::to_string(Event.Unit) + ":0";
					if (!Evaded.empty()) { Evaded += "|"; }
					Evaded += std::to_string(Event.Unit);
				}
				else if (Event.Kind == EEventKind::Critical)
				{
					if (!Crits.empty()) { Crits += "|"; }
					Crits += std::to_string(Event.Unit);
				}
			}

			if ((Struck.empty() ? "-" : Struck) != TextOf(Line, "struck"))
			{
				Fail(Where + " strikes " + (Struck.empty() ? "nobody" : Struck)
					+ ", Godot says " + TextOf(Line, "struck"));
			}
			if ((Evaded.empty() ? "-" : Evaded) != TextOf(Line, "evaded"))
			{
				Fail(Where + ": dodged by " + (Evaded.empty() ? "nobody" : Evaded)
					+ ", Godot says " + TextOf(Line, "evaded"));
			}
			if ((Crits.empty() ? "-" : Crits) != TextOf(Line, "crits"))
			{
				Fail(Where + ": critical on " + (Crits.empty() ? "nobody" : Crits)
					+ ", Godot says " + TextOf(Line, "crits"));
			}
			if (Work.Winner != static_cast<int>(ValueOf(Line, "winner", -1)))
			{
				Fail(Where + " ends with winner " + std::to_string(Work.Winner)
					+ ", Godot says " + std::to_string(static_cast<int>(ValueOf(Line, "winner", -1))));
			}
			++Resolves;
			continue;
		}

		// The state of every unit afterwards: health, gauge, meter, cooldowns,
		// statuses and buffs, all of which an ability can change.
		int Id = -1;
		std::istringstream Stream(Line);
		if (Stream >> Id)
		{
			const FUnit* Unit = Work.FindUnit(Id);
			if (!Unit)
			{
				continue;
			}
			const size_t Space = Line.find(' ');
			const std::string Want = Space == std::string::npos ? "" : Line.substr(Space + 1);
			const std::string Got = StateOf(*Unit);
			if (Got != Want)
			{
				Fail("case " + std::to_string(CaseNumber) + " leaves unit " + std::to_string(Id)
					+ "\n      as " + Got + "\n      Godot says " + Want);
			}
		}
	}

	// -------------------------------------------------------- what it costs to use
	// Paying for an ability is a separate question from what it does, and none of
	// it shows up above: the resolution cases call the resolver directly, exactly
	// as the original does, so the meter, the cooldown, the unit's action and which
	// way it ends up facing are only measured here.
	Section.clear();
	FBattle Using;
	bool bUsing = false;
	int UseCaster = -1;
	int UseSlot = -1;
	int UseNumber = -1;
	for (const std::string& Line : Lines)
	{
		if (Line == "UNITS" || Line == "PREVIEW" || Line == "RESOLVE" || Line == "USE" || Line == "TRACE")
		{
			Section = Line;
			bUsing = false;
			continue;
		}
		if (Section != "USE" || Line.rfind("USE CASES", 0) == 0)
		{
			continue;
		}

		if (Line.rfind("case=", 0) == 0)
		{
			UseNumber = static_cast<int>(ValueOf(Line, "case"));
			UseCaster = static_cast<int>(ValueOf(Line, "caster"));
			UseSlot = static_cast<int>(ValueOf(Line, "slot"));
			const FVec2 Aim = PointOf(Line, "aim");
			const bool bWantBlocked = TextOf(Line, "blocked") == "yes";

			Using = Base;
			Using.Rng.Seed(static_cast<uint64_t>(5000 + UseNumber));
			FUnit* Caster = Using.FindUnit(UseCaster);
			bUsing = false;
			if (!Caster)
			{
				continue;
			}
			const bool bBlocked = !Using.AbilityBlockedReason(*Caster, UseSlot).empty();
			if (bBlocked != bWantBlocked)
			{
				Fail("case " + std::to_string(UseNumber) + ": unit " + std::to_string(UseCaster)
					+ " slot " + std::to_string(UseSlot)
					+ (bBlocked ? " is refused" : " is allowed") + ", Godot disagrees");
			}
			if (bBlocked)
			{
				continue;
			}
			FTickReport Report;
			Using.UseAbility(*Caster, UseSlot, Aim, -1, Report);
			bUsing = true;
			++Uses;
			continue;
		}
		if (!bUsing)
		{
			continue;
		}
		if (Line.rfind("casting=", 0) == 0)
		{
			const FUnit* Caster = Using.FindUnit(UseCaster);
			const std::string Where = "case " + std::to_string(UseNumber) + " (unit "
				+ std::to_string(UseCaster) + " slot " + std::to_string(UseSlot) + ")";
			if ((Caster->IsCasting() ? 1 : 0) != static_cast<int>(ValueOf(Line, "casting")))
			{
				Fail(Where + (Caster->IsCasting() ? " is casting" : " is not casting")
					+ ", Godot disagrees");
			}
			const int Ticks_ = Caster->IsCasting() ? Caster->Casting.Ticks : 0;
			if (Ticks_ != static_cast<int>(ValueOf(Line, "castticks")))
			{
				Fail(Where + " casts for " + std::to_string(Ticks_) + " ticks, Godot says "
					+ std::to_string(static_cast<int>(ValueOf(Line, "castticks"))));
			}
			if ((Caster->IsChanneling() ? 1 : 0) != static_cast<int>(ValueOf(Line, "channeling")))
			{
				Fail(Where + (Caster->IsChanneling() ? " is channelling" : " is not channelling")
					+ ", Godot disagrees");
			}
			const FVec2 Facing = PointOf(Line, "facing");
			if (std::fabs(Caster->Facing.X - Facing.X) > 0.01f
				|| std::fabs(Caster->Facing.Y - Facing.Y) > 0.01f)
			{
				Fail(Where + " faces " + Text(Caster->Facing) + ", Godot says " + Text(Facing));
			}
			if ((Caster->Toggled[UseSlot] ? 1 : 0) != static_cast<int>(ValueOf(Line, "toggled")))
			{
				Fail(Where + ": the toggle is the other way round from Godot's");
			}
			continue;
		}
		{
			int Id = -1;
			std::istringstream Stream(Line);
			if (Stream >> Id)
			{
				const FUnit* Unit = Using.FindUnit(Id);
				if (!Unit)
				{
					continue;
				}
				const size_t Space = Line.find(' ');
				const std::string Want = Space == std::string::npos ? "" : Line.substr(Space + 1);
				const std::string Got = StateOf(*Unit);
				if (Got != Want)
				{
					Fail("case " + std::to_string(UseNumber) + " leaves the caster"
						+ "\n      as " + Got + "\n      Godot says " + Want);
				}
			}
		}
	}

	// ------------------------------------------------------------------ the clock
	Section.clear();
	FBattle Scene;
	bool bScene = false;
	int SceneCaster = -1;
	std::string SceneName;
	for (size_t i = 0; i < Lines.size(); ++i)
	{
		const std::string& Line = Lines[i];
		if (Line == "UNITS" || Line == "PREVIEW" || Line == "RESOLVE" || Line == "USE" || Line == "TRACE")
		{
			Section = Line;
			continue;
		}
		if (Section != "TRACE")
		{
			continue;
		}

		if (Line.rfind("SCENE", 0) == 0)
		{
			SceneName = TextOf(Line, "");
			std::istringstream Stream(Line.substr(6));
			Stream >> SceneName;
			SceneCaster = static_cast<int>(ValueOf(Line, "caster"));
			const int Slot = static_cast<int>(ValueOf(Line, "slot"));
			const int Follow = static_cast<int>(ValueOf(Line, "follow", -1));
			const FVec2 Aim = PointOf(Line, "aim");

			Scene = Base;
			Scene.Rng.Seed(777);
			// The trace is about the clock, so nothing is left on anybody that
			// would soak or turn away what lands.
			for (FUnit& Unit : Scene.Units)
			{
				Unit.Statuses.clear();
			}
			if (FUnit* Six = Scene.FindUnit(6))
			{
				Six->Hp = Six->MaxHp();
				Six->KoTicks = 0;
			}
			FUnit* Caster = Scene.FindUnit(SceneCaster);
			bScene = Caster != nullptr;
			if (!bScene)
			{
				continue;
			}
			FTickReport Report;
			Scene.UseAbility(*Caster, Slot, Aim, Follow, Report);
			continue;
		}
		if (!bScene)
		{
			continue;
		}
		if (Line.rfind("BLOCKED", 0) == 0)
		{
			bScene = false;
			continue;
		}
		if (Line.rfind("started casting=", 0) == 0)
		{
			const FUnit* Caster = Scene.FindUnit(SceneCaster);
			const bool bCasting = Caster && Caster->IsCasting();
			if (bCasting != (ValueOf(Line, "casting") != 0.0))
			{
				Fail(SceneName + ": the caster " + (bCasting ? "is" : "is not")
					+ " casting, Godot disagrees");
			}
			const int Left = bCasting ? Caster->Casting.Ticks : 0;
			if (Left != static_cast<int>(ValueOf(Line, "ticks")))
			{
				Fail(SceneName + ": the cast takes " + std::to_string(Left)
					+ " ticks, Godot says " + std::to_string(static_cast<int>(ValueOf(Line, "ticks"))));
			}
			continue;
		}
		if (Line.rfind("unit", 0) == 0 && Line.find(" walks to ") != std::string::npos)
		{
			// Whoever is being followed steps aside, to prove the spell tracks them.
			const int Who = std::atoi(Line.c_str() + 5);
			const size_t At = Line.find(" walks to ") + 10;
			const char* From = Line.c_str() + At;
			const char* Comma = std::strchr(From, ',');
			if (FUnit* Runner = Scene.FindUnit(Who))
			{
				Runner->Pos = FVec2(static_cast<float>(std::atof(From)),
					Comma ? static_cast<float>(std::atof(Comma + 1)) : 0.0f);
			}
			continue;
		}
		if (Line.rfind("unit", 0) == 0 && Line.find(" is struck down") != std::string::npos)
		{
			const int Who = std::atoi(Line.c_str() + 5);
			if (FUnit* Doomed = Scene.FindUnit(Who))
			{
				FTickReport Report;
				Doomed->Hp = 0;
				Scene.KnockOut(*Doomed, Report);
			}
			continue;
		}
		if (Line.rfind("t=", 0) == 0)
		{
			FTickReport Report;
			Scene.Tick(Report);

			const FUnit* Caster = Scene.FindUnit(SceneCaster);
			const int Casting = (Caster && Caster->IsCasting()) ? Caster->Casting.Ticks : -1;
			if (Casting != static_cast<int>(ValueOf(Line, "cast", -1)))
			{
				Fail(SceneName + " at tick " + std::to_string(Scene.TickCount) + ": cast has "
					+ std::to_string(Casting) + " left, Godot says "
					+ std::to_string(static_cast<int>(ValueOf(Line, "cast", -1))));
			}
			int Resolved = 0;
			for (const FEvent& Event : Report.Events)
			{
				if (Event.Kind == EEventKind::Resolved)
				{
					++Resolved;
				}
			}
			if (Resolved != static_cast<int>(ValueOf(Line, "resolved")))
			{
				Fail(SceneName + " at tick " + std::to_string(Scene.TickCount) + ": "
					+ std::to_string(Resolved) + " abilities went off, Godot says "
					+ std::to_string(static_cast<int>(ValueOf(Line, "resolved"))));
			}
			std::string Health;
			for (const FUnit& Unit : Scene.Units)
			{
				if (!Health.empty()) { Health += ","; }
				Health += std::to_string(Unit.Hp);
			}
			const std::string WantHealth = TextOf(Line, "hp");
			if (Health != WantHealth)
			{
				Fail(SceneName + " at tick " + std::to_string(Scene.TickCount) + ": health is "
					+ Health + ", Godot says " + WantHealth);
			}
			++Ticks;
			continue;
		}
	}

	std::printf("%d forecasts, %d resolved, %d paid for, %d ticks of casting\n",
		Previews, Resolves, Uses, Ticks);

	// A reference that came out empty would pass every check in it.
	if (Previews < 300 || Resolves < 100 || Uses < 20 || Ticks < 100)
	{
		std::printf("THE REFERENCE IS SHORT -- re-dump it from the Godot game\n");
		return 1;
	}
	if (Failures > 0)
	{
		std::printf("ABILITIES DO SOMETHING ELSE HERE (%d)\n", Failures);
		return 1;
	}
	std::printf("ABILITIES DO IN UNREAL WHAT THEY DO IN GODOT\n");
	return 0;
}
