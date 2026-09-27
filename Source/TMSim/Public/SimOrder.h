// An order: the one way anything changes a battle.
//
// This matters more for a game played against other people than it looks. A
// player's order arrives over the network, the computer's comes out of the
// thing in SimAI, and a replay reads its orders off a list -- and all three go
// through the same validate-then-apply. So there is nothing the computer can do
// that a player could not, and nothing the host accepts from one that it would
// refuse from the other.
//
// It also means the host can check an order it is handed rather than trusting
// it, and that a battle is exactly the list of orders that were applied to it:
// same orders, same seed, same battle, which is what a replay and a desync
// check both rest on.

#pragma once

#include "SimTypes.h"

namespace TMSim
{
	enum class EOrderType : uint8_t
	{
		/** Time passing. The only one nobody issues. */
		Advance,
		Move,
		UseAbility,
		EndTurn,
	};

	struct FOrder
	{
		EOrderType Type = EOrderType::EndTurn;

		/** Who it is for. */
		int UnitId = -1;
		/**
		 * Which turn it was given on. A unit's serial goes up every time it
		 * becomes ready, so an order written for an earlier turn -- held up on
		 * the network, or replayed out of order -- is refused rather than
		 * applied to the wrong turn.
		 */
		int Serial = -1;

		/** Move: where to walk to, in metres. */
		FVec2 To;
		bool bSprint = false;

		/** UseAbility: which of the four, and where it is aimed. */
		int Slot = -1;
		FVec2 Target;
		/** The unit a cast follows, or -1 for a spot on the ground. */
		int Follow = -1;

		/** Advance: how many ticks. */
		int Ticks = 0;

		static FOrder MakeMove(int InUnitId, int InSerial, const FVec2& InTo, bool bInSprint = false)
		{
			FOrder Order;
			Order.Type = EOrderType::Move;
			Order.UnitId = InUnitId;
			Order.Serial = InSerial;
			Order.To = InTo;
			Order.bSprint = bInSprint;
			return Order;
		}

		static FOrder MakeEndTurn(int InUnitId, int InSerial)
		{
			FOrder Order;
			Order.Type = EOrderType::EndTurn;
			Order.UnitId = InUnitId;
			Order.Serial = InSerial;
			return Order;
		}

		static FOrder MakeAdvance(int InTicks)
		{
			FOrder Order;
			Order.Type = EOrderType::Advance;
			Order.Ticks = InTicks;
			return Order;
		}
	};
}
