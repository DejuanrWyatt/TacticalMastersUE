// Plays a whole battle through the order path, then replays it.
//
// The other tests each check one piece against the Godot game. This one checks
// the thing the pieces are for: that a battle is exactly the list of orders
// that were applied to it. It is the claim the whole design rests on, and it is
// the one that cannot be checked by reading the code.
//
// Ultimately this is a game of people against other people. Both machines step
// the same rules over the same orders and compare notes, and a replay re-runs a
// recorded match. So "the same orders and the same seed give the same battle"
// is not a nicety -- it is the difference between a match and two machines
// quietly playing different games. This test plays one out with the computer on
// both sides, writes down every order and the tick it landed on, replays the
// list into a fresh battle, and compares the two after every single step.
//
// It also watches for the two ways the order path could be wrong. An order the
// rules accept and then do not carry out means the checking half and the doing
// half disagree. And a unit that is ready, is refused every order, and so stays
// ready forever would hang a real match on the spot.
//
// The orders all come from the computer player, which chooses abilities as well
// as where to stand, so this is the real thing playing itself rather than a
// stand-in policy written for the test.
//
// The units fight now, which is what makes the replay check mean anything:
// seeding the replay differently on purpose fails, because rolling to evade and
// to crit is the only thing in a battle that touches the dice. While nothing
// could deal damage that probe passed, and the check could not tell a right seed
// from a wrong one. If it ever starts passing again, something has stopped using
// the seeded generator and the replay guarantee is hollow.

#include "SimAI.h"
#include "SimBattle.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace TMSim;

namespace
{
	int Failures = 0;

	void Fail(const std::string& What)
	{
		if (Failures < 10)
		{
			std::printf("  %s\n", What.c_str());
		}
		++Failures;
	}

	/** Two sides of four, as the director builds them. */
	void Deal(FBattle& Battle)
	{
		const char* Roster[8] =
		{
			"knight", "archer", "black_mage", "white_mage",
			"knight", "archer", "black_mage", "white_mage"
		};
		const FVec2 Blue[4] =
		{
			FVec2(2.75f, 4.75f), FVec2(0.75f, 8.75f),
			FVec2(4.75f, 6.75f), FVec2(2.75f, 10.75f)
		};
		Battle.Map.BuildMirrored(HighlandsRows());
		const FVec2 Size = Battle.Map.SizeMeters();
		for (int Index = 0; Index < 8; ++Index)
		{
			FUnit Unit;
			Unit.Id = Index;
			Unit.Team = Index < 4 ? 0 : 1;
			Unit.Job = Roster[Index];
			const FVec2 Spot = Blue[Index % 4];
			Unit.Pos = Index < 4 ? Spot : FVec2(Size.X - Spot.X, Size.Y - Spot.Y);
			Battle.Units.push_back(Unit);
		}
		Battle.Start(12345);
	}

	/** The unit the battle is waiting on, as the director asks it. */
	const FUnit* WaitingOn(const FBattle& Battle)
	{
		for (const FUnit& Unit : Battle.Units)
		{
			if (Unit.IsAlive() && Unit.bReady)
			{
				return &Unit;
			}
		}
		return nullptr;
	}

	/** One order that got through, and the tick it got through on. */
	struct FRecorded
	{
		int Tick = 0;
		FOrder Order;
	};

}

int main()
{
	const int TickLimit = 3000;  // five minutes of battle, which is plenty

	FBattle Battle;
	Deal(Battle);
	FAIPlayer Computer("hard");
	Computer.Rng.Seed(12345);

	std::vector<FRecorded> Recorded;
	/** The checksum after each tick, so a divergence is caught where it happens. */
	struct FMark { int Tick; uint64_t Sum; };
	std::vector<FMark> Marks;
	int Refusals = 0;
	int Stuck = 0;

	while (Battle.TickCount < TickLimit && Battle.Winner < 0)
	{
		const FUnit* Unit = WaitingOn(Battle);
		if (!Unit)
		{
			FTickReport Report;
			Battle.Advance(1, Report);
			Marks.push_back({ Battle.TickCount, Battle.Checksum() });
			continue;
		}

		const int UnitId = Unit->Id;
		const int Serial = Unit->Serial;
		FOrder Order = Computer.NextCommand(Battle, *Unit);

		// Checked, then applied, and never the other way round.
		const std::string Refused = Battle.Validate(Order);
		if (!Refused.empty())
		{
			++Refusals;
			// It cannot have this turn, so it gives the turn up. A unit that
			// could neither act nor stop being ready would hang the match.
			Order = FOrder::MakeEndTurn(UnitId, Serial);
			if (!Battle.Validate(Order).empty())
			{
				++Stuck;
				Fail("unit " + std::to_string(UnitId)
					+ " is ready and cannot even end its turn, so the battle is stuck");
				break;
			}
		}

		FTickReport Report;
		if (!Battle.Apply(Order, Report))
		{
			Fail("an order for unit " + std::to_string(UnitId)
				+ " passed the rules and then did nothing");
			break;
		}
		Recorded.push_back({ Battle.TickCount, Order });

		// Still ready and still not moved means nothing happened, and asking
		// again would ask forever.
		if (Order.Type == EOrderType::EndTurn)
		{
			const FUnit* After = Battle.FindUnit(UnitId);
			if (After && After->bReady)
			{
				Fail("unit " + std::to_string(UnitId) + " ended its turn and is still ready");
				break;
			}
		}
	}

	std::printf("%d orders over %d ticks\n", static_cast<int>(Recorded.size()), Battle.TickCount);
	std::printf("%d refused, %d stuck\n", Refusals, Stuck);

	// What the fight came to. Orders alone would not say whether any of them did
	// anything: a battle where every blow missed would look identical from here.
	int Standing[2] = { 0, 0 };
	int HealthLost = 0;
	for (const FUnit& Unit : Battle.Units)
	{
		if (Unit.IsAlive())
		{
			++Standing[Unit.Team];
		}
		HealthLost += Unit.MaxHp() - Unit.Hp;
	}
	std::printf("%d left standing to %d, %d health between them, winner %d\n",
		Standing[0], Standing[1], HealthLost, Battle.Winner);

	// Now the replay: the same seed, the same orders, nothing else. Nobody
	// decides anything this time -- the list is read off and applied.
	FBattle Replay;
	Deal(Replay);
	size_t Next = 0;
	size_t Marked = 0;
	int Mismatches = 0;
	while (Replay.TickCount < Battle.TickCount)
	{
		while (Next < Recorded.size() && Recorded[Next].Tick == Replay.TickCount)
		{
			const FOrder& Order = Recorded[Next].Order;
			const std::string Refused = Replay.Validate(Order);
			if (!Refused.empty())
			{
				Fail("the replay refused a recorded order for unit "
					+ std::to_string(Order.UnitId) + ": " + Refused);
				++Mismatches;
			}
			else
			{
				FTickReport Report;
				Replay.Apply(Order, Report);
			}
			++Next;
		}
		FTickReport Report;
		Replay.Advance(1, Report);

		// Compared here rather than only at the end. Comparing once tells you the
		// replay went wrong; comparing every tick tells you which tick, and that
		// is the difference between a bug you can find and one you cannot. It is
		// also what two machines in a match do, so this is the same check.
		if (Marked < Marks.size() && Marks[Marked].Tick == Replay.TickCount)
		{
			if (Marks[Marked].Sum != Replay.Checksum() && Mismatches == 0)
			{
				Fail("the replay parts company with the battle at tick "
					+ std::to_string(Replay.TickCount));
				++Mismatches;
			}
			++Marked;
		}
	}
	// Anything the first battle did on its very last tick.
	while (Next < Recorded.size() && Recorded[Next].Tick == Replay.TickCount)
	{
		FTickReport Report;
		const FOrder& Order = Recorded[Next].Order;
		if (Replay.Validate(Order).empty())
		{
			Replay.Apply(Order, Report);
		}
		++Next;
	}

	std::printf("replayed %d of %d orders, checked at %d of %d ticks\n",
		static_cast<int>(Next), static_cast<int>(Recorded.size()),
		static_cast<int>(Marked), static_cast<int>(Marks.size()));

	// Every tick has to have been compared. If the two ran out of step the marks
	// stop lining up and the comparison quietly stops happening, which would leave
	// the rest of the replay unchecked while still reporting success.
	if (Marked != Marks.size())
	{
		Fail("the replay only stayed in step for " + std::to_string(Marked) + " of "
			+ std::to_string(Marks.size()) + " ticks");
	}
	if (Battle.Checksum() != Replay.Checksum())
	{
		Fail("the replay ended in a different state from the battle it replayed");
		for (size_t i = 0; i < Battle.Units.size() && i < Replay.Units.size(); ++i)
		{
			const FUnit& A = Battle.Units[i];
			const FUnit& B = Replay.Units[i];
			if (A.Pos != B.Pos || A.Hp != B.Hp || A.Tg != B.Tg || A.Serial != B.Serial)
			{
				char Line[220];
				std::snprintf(Line, sizeof(Line),
					"    unit %d: %.2f,%.2f hp %d tg %d turn %d -- replay has %.2f,%.2f hp %d tg %d turn %d",
					A.Id, A.Pos.X, A.Pos.Y, A.Hp, A.Tg, A.Serial,
					B.Pos.X, B.Pos.Y, B.Hp, B.Tg, B.Serial);
				std::printf("%s\n", Line);
			}
		}
	}

	// And the checksum itself, field by field. A checksum that quietly ignores
	// something is worse than none: two machines would drift apart in that field
	// and keep playing, each sure the other agreed. So change one thing at a
	// time and insist the number moves. This is checked here rather than on a
	// fresh battle because a played-out one has statuses, cooldowns and spent
	// gauges on it, which an untouched one does not.
	{
		int Blind = 0;
		// Setup, when given, runs before the checksum is taken, so the change can
		// be to something that has to exist first.
		auto Notices = [&Blind, &Battle](const char* Field, void (*Change)(FBattle&), void (*Setup)(FBattle&) = nullptr)
		{
			FBattle Copy = Battle;
			if (Setup)
			{
				Setup(Copy);
			}
			const uint64_t Before = Copy.Checksum();
			Change(Copy);
			if (Copy.Checksum() == Before)
			{
				Fail(std::string("the checksum does not notice ") + Field);
				++Blind;
			}
		};

		Notices("the clock", [](FBattle& B) { B.TickCount += 1; });
		Notices("who won", [](FBattle& B) { B.Winner = 1 - B.Winner; });
		Notices("the planning time", [](FBattle& B) { B.PlanningTicks += 1; });
		Notices("where a unit stands, across", [](FBattle& B) { B.Units[3].Pos.X += 0.5f; });
		Notices("where a unit stands, along", [](FBattle& B) { B.Units[3].Pos.Y += 0.5f; });
		Notices("which way a unit faces", [](FBattle& B) { B.Units[2].Facing.X += 1.0f; });
		Notices("a unit's health", [](FBattle& B) { B.Units[5].Hp -= 1; });
		Notices("a unit's gauge", [](FBattle& B) { B.Units[1].Tg += 1; });
		Notices("which turn a unit is on", [](FBattle& B) { B.Units[0].Serial += 1; });
		Notices("a unit's ultimate meter", [](FBattle& B) { B.Units[4].Ult += 1; });
		Notices("whether a unit is ready", [](FBattle& B) { B.Units[6].bReady = !B.Units[6].bReady; });
		Notices("whether a unit has walked", [](FBattle& B) { B.Units[6].bMoved = !B.Units[6].bMoved; });
		Notices("whether a unit has acted", [](FBattle& B) { B.Units[6].bActed = !B.Units[6].bActed; });
		Notices("a unit's turn countdown", [](FBattle& B) { B.Units[7].Clock += 1; });
		Notices("a recharging ability", [](FBattle& B) { B.Units[2].Cooldowns[1] += 1; });
		Notices("a switched-on toggle", [](FBattle& B) { B.Units[3].Toggled[2] = !B.Units[3].Toggled[2]; });
		Notices("a toggle already flipped this turn", [](FBattle& B)
			{ B.Units[3].ToggledTurn[2] = !B.Units[3].ToggledTurn[2]; });
		Notices("a spell part-way out", [](FBattle& B) { B.Units[2].Casting.Slot = 1; });
		Notices("how much longer a cast has", [](FBattle& B)
			{
				B.Units[2].Casting.Slot = 1;
				B.Units[2].Casting.Ticks = 7;
			});
		Notices("where a cast is aimed", [](FBattle& B)
			{
				B.Units[2].Casting.Slot = 1;
				B.Units[2].Casting.Target.X += 1.0f;
			});
		Notices("who a cast is following", [](FBattle& B)
			{
				B.Units[2].Casting.Slot = 1;
				B.Units[2].Casting.FollowId = 5;
			});
		Notices("a channelled ability", [](FBattle& B) { B.Units[6].Channeling.Slot = 3; });
		Notices("where the dice have got to", [](FBattle& B) { B.Rng.Randi(); });
		Notices("how long a fallen unit can still be raised", [](FBattle& B) { B.Units[4].KoTicks += 1; });
		Notices("a gauge owed for holding back", [](FBattle& B) { B.Units[1].bHustling = !B.Units[1].bHustling; });
		Notices("how long a unit has gone unhurt", [](FBattle& B) { B.Units[3].UnharmedTurns += 1; });
		Notices("how many turns a channel has left", [](FBattle& B)
			{
				B.Units[6].Channeling.Slot = 3;
				B.Units[6].Channeling.Turns = 2;
			});
		Notices("a status on a unit", [](FBattle& B)
			{
				FStatus Status;
				Status.Id = "poison";
				Status.Turns = 2;
				B.Units[0].Statuses.push_back(Status);
			});
		Notices("how long a status has left", [](FBattle& B)
			{
				FStatus Status;
				Status.Id = "poison";
				Status.Turns = 2;
				B.Units[0].Statuses.push_back(Status);
				B.Units[0].Statuses.back().Turns = 3;
			});
		Notices("a buff on a unit", [](FBattle& B)
			{
				FBuff Buff;
				Buff.Stat = EStat::AttDef;
				Buff.Amount = 3;
				Buff.Turns = 2;
				B.Units[1].Buffs.push_back(Buff);
			});
		Notices("which aura keeps a buff topped up",
			[](FBattle& B) { B.Units[1].Buffs.back().Aura = "Warcry"; },
			[](FBattle& B)
			{
				FBuff Buff;
				Buff.Stat = EStat::AttDef;
				Buff.Amount = 3;
				Buff.Turns = 2;
				B.Units[1].Buffs.push_back(Buff);
			});
		Notices("how long blue has held the middle", [](FBattle& B) { B.CaptureTicks[0] += 1; });
		Notices("how long red has held the middle", [](FBattle& B) { B.CaptureTicks[1] += 1; });
		std::printf("the checksum notices %d kinds of change\n", 34 - Blind);
	}

	// How a battle is won besides by wiping out the other side: the time limit
	// and holding the middle. The same five checks as the Godot project's own
	// smoke_test.gd (_test_victory_conditions and the capture checks), on the
	// same board; GodotBattleTrace.txt then measures them in whole battles.
	{
		const int Before = Failures;
		auto Run = [](FBattle& B, int Ticks)
		{
			for (int i = 0; i < Ticks && B.Winner == -1; ++i)
			{
				FTickReport Report;
				B.Apply(FOrder::MakeAdvance(1), Report);
			}
		};

		FBattle Timed;
		Timed.Tuning.BattleSeconds = 5.0;
		Deal(Timed);
		Timed.Units[4].Hp = 10;  // red is hurt, so blue should win on health
		Run(Timed, 200);
		if (Timed.Winner != 0 || Timed.TickCount != 50)
		{
			Fail("the time limit should end the battle at tick 50 for the healthier side (winner "
				+ std::to_string(Timed.Winner) + " at " + std::to_string(Timed.TickCount) + ")");
		}

		FBattle Level;
		Level.Tuning.BattleSeconds = 3.0;
		Deal(Level);
		Run(Level, 200);
		if (Level.Winner != FBattle::Draw)
		{
			Fail("an even battle at the time limit should be a draw");
		}

		FBattle Endless;
		Deal(Endless);
		Run(Endless, 50);
		if (Endless.Winner != -1)
		{
			Fail("without a limit a battle should keep going");
		}

		FBattle Hold;
		Hold.Tuning.CaptureSeconds = 3.0;
		Deal(Hold);
		for (FUnit& Unit : Hold.Units)
		{
			Unit.Pos = Unit.Team == 0 ? Hold.CapturePoint() : Hold.CapturePoint() + FVec2(30.0f, 30.0f);
		}
		Run(Hold, 40);
		if (Hold.Winner != 0)
		{
			Fail("holding the middle alone should win the battle");
		}

		FBattle Contested;
		Contested.Tuning.CaptureSeconds = 3.0;
		Deal(Contested);
		for (FUnit& Unit : Contested.Units)
		{
			Unit.Pos = Contested.CapturePoint() + FVec2(0.5f * Unit.Team, 0.0f);
		}
		Run(Contested, 40);
		if (Contested.CaptureTicks[0] != 0 || Contested.CaptureTicks[1] != 0)
		{
			Fail("a contested middle should count for neither side");
		}
		if (Failures == Before)
		{
			std::printf("a battle is called on time for the healthier side, and won by holding the middle\n");
		}
	}

	// A battle where nothing ever happened would pass everything above, and so
	// would one where every blow missed. It has to have been a fight: somebody
	// lost health, and it either reached a decision or ran the clock out trying.
	if (Recorded.size() < 50)
	{
		std::printf("THE BATTLE BARELY HAPPENED -- only %d orders\n", static_cast<int>(Recorded.size()));
		return 1;
	}
	if (HealthLost < 100)
	{
		std::printf("NOBODY REALLY FOUGHT -- %d health lost between eight units\n", HealthLost);
		return 1;
	}
	if (Battle.Winner < 0 && Battle.TickCount < TickLimit)
	{
		std::printf("THE BATTLE STOPPED EARLY WITH NOBODY WINNING -- %d ticks\n", Battle.TickCount);
		return 1;
	}
	// And one where the rules refused most of what was asked for.
	if (Refusals * 2 > static_cast<int>(Recorded.size()))
	{
		std::printf("THE RULES REFUSED MOST OF WHAT WAS ASKED FOR (%d of %d)\n",
			Refusals, static_cast<int>(Recorded.size()));
		return 1;
	}
	if (Failures > 0)
	{
		std::printf("THE BATTLE DOES NOT REPLAY (%d)\n", Failures);
		return 1;
	}
	std::printf("THE SAME ORDERS GIVE THE SAME BATTLE\n");
	return 0;
}
