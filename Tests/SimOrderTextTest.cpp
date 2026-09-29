// Orders as text: how they travel between two machines in an online match.
//
// An online match is two machines playing the same battle from the same
// orders (Docs/design/feat-online.md), and every order crosses between them as
// a line of text. So a line has to bring back exactly the order that was
// written -- a position a hair off is a unit standing somewhere else, and the
// two games quietly part. This checks three things:
//
//   - every kind of order comes back bit for bit, including the awkward
//     numbers (negative zero, the smallest float there is, 0.1);
//   - a line that is not exactly an order is refused, not half-read, since it
//     comes from another machine;
//   - a whole battle, the computer playing both sides, replayed from its
//     orders after each has been written out and read back, ends with the
//     same checksum as the battle itself.

#include "SimAI.h"
#include "SimBattle.h"
#include "SimOrderText.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace TMSim;

namespace
{
	int Failures = 0;

	void Fail(const std::string& What)
	{
		if (Failures < 20)
		{
			std::printf("  %s\n", What.c_str());
		}
		++Failures;
	}

	bool SameBits(float A, float B)
	{
		return std::memcmp(&A, &B, sizeof(A)) == 0;
	}

	bool SameBits(double A, double B)
	{
		return std::memcmp(&A, &B, sizeof(A)) == 0;
	}

	bool Same(const FOrder& A, const FOrder& B)
	{
		if (A.Type != B.Type || A.UnitId != B.UnitId || A.Serial != B.Serial || A.bSprint != B.bSprint
			|| A.Slot != B.Slot || A.Follow != B.Follow || A.Team != B.Team || A.Ticks != B.Ticks
			|| !SameBits(A.To.X, B.To.X) || !SameBits(A.To.Y, B.To.Y)
			|| !SameBits(A.Target.X, B.Target.X) || !SameBits(A.Target.Y, B.Target.Y)
			|| A.TuneValues.size() != B.TuneValues.size())
		{
			return false;
		}
		for (size_t i = 0; i < A.TuneValues.size(); ++i)
		{
			if (A.TuneValues[i].first != B.TuneValues[i].first || !SameBits(A.TuneValues[i].second, B.TuneValues[i].second))
			{
				return false;
			}
		}
		return true;
	}

	/** Written and read back, it must be the same order. */
	FOrder RoundTrip(const FOrder& Order)
	{
		const std::string Text = OrderToText(Order);
		FOrder Back;
		const std::string Refused = OrderFromText(Text, Back);
		if (!Refused.empty())
		{
			Fail("\"" + Text + "\" was refused: " + Refused);
		}
		else if (!Same(Order, Back))
		{
			Fail("\"" + Text + "\" came back a different order");
		}
		return Back;
	}

	void CheckRefused(const std::string& Text)
	{
		FOrder Out;
		if (OrderFromText(Text, Out).empty())
		{
			Fail("\"" + Text + "\" should have been refused");
		}
	}

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
		Battle.Start(777);
	}
}

int main()
{
	// Every kind, with numbers that are easy to get wrong.
	float Smallest = 0.0f;
	const unsigned int One = 1;
	std::memcpy(&Smallest, &One, sizeof(Smallest));
	RoundTrip(FOrder::MakeAdvance(1));
	RoundTrip(FOrder::MakeAdvance(0));
	RoundTrip(FOrder::MakeMove(3, 7, FVec2(10.25f, 5.0f)));
	RoundTrip(FOrder::MakeMove(0, 0, FVec2(-0.0f, 0.1f), true));
	RoundTrip(FOrder::MakeMove(7, 123456, FVec2(Smallest, 3.4e38f)));
	RoundTrip(FOrder::MakeUseAbility(2, 9, 3, FVec2(12.3456789f, 0.3333333f), 5));
	RoundTrip(FOrder::MakeUseAbility(2, 9, 0, FVec2(1.0f, 2.0f), -1));
	RoundTrip(FOrder::MakeEndTurn(6, 42));
	RoundTrip(FOrder::MakePlace(1, 0, FVec2(2.75f, 4.75f)));
	RoundTrip(FOrder::MakeReady(0));
	RoundTrip(FOrder::MakeReady(1));
	RoundTrip(FOrder::MakeTune({}));
	RoundTrip(FOrder::MakeTune({ { 0, 1.0 }, { 1, 0.1 }, { 2, -0.0 }, { 3, 4.9406564584124654e-324 } }));
	const int Kinds = Failures;
	std::printf("every kind of order: %s\n", Kinds == 0 ? "comes back bit for bit" : "WRONG");

	// Anything else is refused.
	const std::vector<std::string> Bad =
	{
		"", "jump 1 2", "move", "move 1 2 41200000", "move 1 2 41200000 40a00000 0 9",
		"move 1 2 41200000 40a00000 2", "move -1 2 41200000 40a00000 0", "move 1 2 7fc00000 40a00000 0",
		"move 1 2 7f800000 40a00000 0", "move 1 2 141200000 40a00000 0", "move 1 2 4120000g 40a00000 0",
		"move 1  2 41200000 40a00000 0", "move 1 2 41200000 40a00000 0 ", " move 1 2 41200000 40a00000 0",
		"ability 1 2 4 0 0 -1", "ability 1 2 -1 0 0 -1", "ability 1 2 0 0 0 -2", "end 1", "end 1 2 3",
		"ready 2", "ready -1", "advance -5", "advance 1.5", "advance 0x10", "tune 1 0", "tune 2 0 3ff0000000000000",
		"tune 1 999 3ff0000000000000", "tune 1 0 7ff0000000000000", "place 1 2 0", "end 99999999999999999999 1",
		std::string(5000, 'a'),
	};
	const int Before = Failures;
	for (const std::string& Line : Bad)
	{
		CheckRefused(Line);
	}
	std::printf("%d malformed lines: %s\n", static_cast<int>(Bad.size()), Failures == Before ? "all refused" : "SOME READ");

	// A whole battle, its orders sent through text.
	FBattle Battle;
	Deal(Battle);
	FAIPlayer Computer("hard");
	Computer.Rng.Seed(777);
	std::vector<std::string> Lines;
	while (Battle.TickCount < 3000 && Battle.Winner < 0)
	{
		const FUnit* Waiting = nullptr;
		for (const FUnit& Unit : Battle.Units)
		{
			if (Unit.IsAlive() && Unit.bReady)
			{
				Waiting = &Unit;
				break;
			}
		}
		FOrder Order = Waiting ? Computer.NextCommand(Battle, *Waiting) : FOrder::MakeAdvance(1);
		if (!Battle.Validate(Order).empty())
		{
			Order = FOrder::MakeEndTurn(Waiting->Id, Waiting->Serial);
		}
		FTickReport Report;
		Battle.Apply(Order, Report);
		Lines.push_back(OrderToText(Order));
	}

	FBattle Replay;
	Deal(Replay);
	int Applied = 0;
	for (const std::string& Line : Lines)
	{
		FOrder Order;
		const std::string Refused = OrderFromText(Line, Order);
		if (!Refused.empty())
		{
			Fail("the battle's own \"" + Line + "\" was refused: " + Refused);
			break;
		}
		const std::string Invalid = Replay.Validate(Order);
		if (!Invalid.empty())
		{
			Fail("\"" + Line + "\" read back and was then refused by the rules: " + Invalid);
			break;
		}
		FTickReport Report;
		Replay.Apply(Order, Report);
		++Applied;
	}
	const bool bSame = Replay.Checksum() == Battle.Checksum() && Replay.TickCount == Battle.TickCount;
	if (!bSame)
	{
		Fail("the battle replayed through text ended differently");
	}
	std::printf("a battle of %d orders (%d ticks, winner %d), sent through text: %s\n",
		static_cast<int>(Lines.size()), Battle.TickCount, Battle.Winner, bSame ? "the same battle" : "A DIFFERENT BATTLE");

	if (Lines.size() < 100 || Battle.Winner < 0)
	{
		Fail("the battle barely happened, which proves nothing");
	}
	if (Failures > 0)
	{
		std::printf("ORDERS AS TEXT: %d FAILURES\n", Failures);
		return 1;
	}
	std::printf("ORDERS SURVIVE THE TRIP BETWEEN MACHINES\n");
	return 0;
}
