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

#include <string>
#include <utility>
#include <vector>

namespace TMSim
{
	/** The most waypoints one walk may have. */
	constexpr int MaxWaypoints = 4;

	enum class EOrderType : uint8_t
	{
		/** Time passing. The only one nobody issues. */
		Advance,
		Move,
		UseAbility,
		EndTurn,
		/** While planning: put a unit down somewhere in its side's spawn area. */
		Place,
		/** While planning: a side is done placing. Carries the side, not a unit. */
		Ready,
		/**
		 * Developer Tools: new rule numbers, by their index in TuningKeys(). An
		 * order like any other, so a battle whose rules were changed part way
		 * replays with the change at the same moment (game_state.gd:1043, 1164).
		 */
		Tune,
		/**
		 * Spends the unit's turn at a watchtower, towards its side taking it
		 * (Docs/design/feat-objectives.md). Not Godot's: last, so every older
		 * order keeps its number.
		 */
		Capture,
		/**
		 * Takes an item from a loot cache (Docs/design/feat-neutral-camps.md 3.4):
		 * the turn's action. Into GearSlot, or the first empty slot for -1; an
		 * item already there is left in the cache in its place.
		 */
		Take,
		/**
		 * Unequips the item in GearSlot into the side's stash. The unit's whole
		 * turn (2026-10-01: an item once worn comes off only when its wearer
		 * dies or spends a turn taking it off). A monster's still leaves it on
		 * the ground, free. Called "drop" in the order text, as it always was.
		 */
		Drop,
		/**
		 * Puts an item from the side's stash into an open slot of one of its
		 * units (GearSlot, or the first open one for -1). Any time, free, and
		 * not tied to a turn: no serial. Not Godot's; added last.
		 */
		Equip,
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
		/**
		 * Move: the spots it walks by on the way, in order, each a node centre
		 * (2026-10-01, waypoints). Empty for the shortest way, as every move was
		 * before. Still one walk: the whole length counts against its move.
		 */
		std::vector<FVec2> Via;

		/** UseAbility: which of the four, and where it is aimed. */
		int Slot = -1;
		FVec2 Target;
		/** The unit a cast follows, or -1 for a spot on the ground. */
		int Follow = -1;

		/** The side a Ready speaks for. */
		int Team = -1;

		/** For a Tune: which rule numbers, by index in TuningKeys(), and their new values. */
		std::vector<std::pair<int, double>> TuneValues;

		/** Advance: how many ticks. */
		int Ticks = 0;

		/** Capture: which watchtower, by its place in FBattle::Watchtowers. */
		int Tower = -1;

		/** Take: which cache, by its place in FBattle::Caches, and which of its items, by id. */
		int Cache = -1;
		std::string ItemId;
		/** Take and Drop: which of the unit's three item slots, or -1 (Take: the first empty one). */
		int GearSlot = -1;

		static FOrder MakeTake(int InUnitId, int InSerial, int InCache, const std::string& InItemId, int InGearSlot = -1)
		{
			FOrder Order;
			Order.Type = EOrderType::Take;
			Order.UnitId = InUnitId;
			Order.Serial = InSerial;
			Order.Cache = InCache;
			Order.ItemId = InItemId;
			Order.GearSlot = InGearSlot;
			return Order;
		}

		static FOrder MakeDrop(int InUnitId, int InSerial, int InGearSlot)
		{
			FOrder Order;
			Order.Type = EOrderType::Drop;
			Order.UnitId = InUnitId;
			Order.Serial = InSerial;
			Order.GearSlot = InGearSlot;
			return Order;
		}

		static FOrder MakeEquip(int InUnitId, const std::string& InItemId, int InGearSlot = -1)
		{
			FOrder Order;
			Order.Type = EOrderType::Equip;
			Order.UnitId = InUnitId;
			Order.ItemId = InItemId;
			Order.GearSlot = InGearSlot;
			return Order;
		}

		static FOrder MakeMove(int InUnitId, int InSerial, const FVec2& InTo, bool bInSprint = false,
			const std::vector<FVec2>& InVia = std::vector<FVec2>())
		{
			FOrder Order;
			Order.Type = EOrderType::Move;
			Order.UnitId = InUnitId;
			Order.Serial = InSerial;
			Order.To = InTo;
			Order.bSprint = bInSprint;
			Order.Via = InVia;
			return Order;
		}

		static FOrder MakeUseAbility(int InUnitId, int InSerial, int InSlot,
			const FVec2& InTarget, int InFollow = -1)
		{
			FOrder Order;
			Order.Type = EOrderType::UseAbility;
			Order.UnitId = InUnitId;
			Order.Serial = InSerial;
			Order.Slot = InSlot;
			Order.Target = InTarget;
			Order.Follow = InFollow;
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

		static FOrder MakePlace(int InUnitId, int InSerial, const FVec2& InTo)
		{
			FOrder Order;
			Order.Type = EOrderType::Place;
			Order.UnitId = InUnitId;
			Order.Serial = InSerial;
			Order.To = InTo;
			return Order;
		}

		static FOrder MakeTune(const std::vector<std::pair<int, double>>& InValues)
		{
			FOrder Order;
			Order.Type = EOrderType::Tune;
			Order.TuneValues = InValues;
			return Order;
		}

		static FOrder MakeReady(int InTeam)
		{
			FOrder Order;
			Order.Type = EOrderType::Ready;
			Order.Team = InTeam;
			return Order;
		}

		static FOrder MakeCapture(int InUnitId, int InSerial, int InTower)
		{
			FOrder Order;
			Order.Type = EOrderType::Capture;
			Order.UnitId = InUnitId;
			Order.Serial = InSerial;
			Order.Tower = InTower;
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
