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
// No winner is possible yet: nothing can deal damage until abilities are wired
// to orders, so the battle is units manoeuvring. That does not weaken what is
// being checked here, which is the order path rather than the fighting -- but it
// does mean one thing this cannot yet check. Seeding the replay differently on
// purpose still passes, because nothing in a battle consumes the dice until
// there are attacks to miss and crits to roll. Once abilities arrive, that probe
// should start failing, and if it does not, something has stopped using the
// seeded generator.

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
	int Refusals = 0;
	int Stuck = 0;

	while (Battle.TickCount < TickLimit && Battle.Winner < 0)
	{
		const FUnit* Unit = WaitingOn(Battle);
		if (!Unit)
		{
			FTickReport Report;
			Battle.Advance(1, Report);
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

	// Now the replay: the same seed, the same orders, nothing else. Nobody
	// decides anything this time -- the list is read off and applied.
	FBattle Replay;
	Deal(Replay);
	size_t Next = 0;
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

	std::printf("replayed %d of %d orders\n", static_cast<int>(Next), static_cast<int>(Recorded.size()));

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
		auto Notices = [&Blind, &Battle](const char* Field, void (*Change)(FBattle&))
		{
			FBattle Copy = Battle;
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
		std::printf("the checksum notices %d kinds of change\n", 18 - Blind);
	}

	// A battle where nothing ever happened would pass everything above.
	if (Recorded.size() < 50 || Battle.TickCount < TickLimit / 2)
	{
		std::printf("THE BATTLE BARELY HAPPENED -- %d orders, %d ticks\n",
			static_cast<int>(Recorded.size()), Battle.TickCount);
		return 1;
	}
	// And so would one where every order was refused.
	if (Refusals * 2 > static_cast<int>(Recorded.size()))
	{
		std::printf("THE RULES REFUSED MOST OF WHAT THE COMPUTER ASKED FOR (%d of %d)\n",
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
