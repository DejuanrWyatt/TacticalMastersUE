// The battle's clock.
//
// Time runs continuously rather than in rounds: every unit's Turn Gauge fills
// by its Speed, and the moment one is full that unit becomes READY and has a
// countdown to act in. Let the countdown run out and the turn is lost.
//
// Ported from game_state.gd. This is the timing half -- the gauge, readiness,
// the countdown and the end of a turn. Orders, abilities, pathfinding, statuses
// acting and the ground itself come with later slices; where the original does
// something at a point this does not yet, the comment says so rather than
// leaving a silent gap.

#pragma once

#include "SimRandom.h"
#include "SimTypes.h"
#include "SimUnit.h"

#include <vector>

namespace TMSim
{
	/** What happened during a tick, for the presentation layer to play back. */
	struct FTickReport
	{
		std::vector<int> BecameReady;
		std::vector<int> TurnEnded;
		std::vector<int> TimedOut;
		std::vector<int> Gone;
	};

	class FBattle
	{
	public:
		/** Places units with the head start their Speed earns them. */
		void Start(uint64_t InSeed);

		/** One step of the clock, TicksPerSecond of these to the second. */
		void Tick(FTickReport& Report);

		/** Runs the clock on, for tests and for catching a replay up. */
		void Advance(int Ticks, FTickReport& Report);

		// ----------------------------------------------------------- timing

		/** Gauge per tick before statuses: Speed x TgPerSpeed x the multiplier. */
		int BaseTgGain(const FUnit& Unit) const;
		/** The same, after Slow and after a held-back turn. */
		int TgGain(const FUnit& Unit) const;
		/** Faster until its next turn, for a unit that kept its ability back. */
		float HustleFactor(const FUnit& Unit) const;
		/** Ticks a READY unit has to act in, from its Patience. */
		int ClockTicks(const FUnit& Unit) const;
		/** Ticks until this unit's turn comes (0 if it is ready now). */
		int TicksToReady(const FUnit& Unit) const;

		// ------------------------------------------------------------ state

		std::vector<FUnit> Units;
		FTuning Tuning;
		FSimRandom Rng;
		int TickCount = 0;
		int Winner = -1;
		/** While the sides are still placing units, nothing else happens. */
		int PlanningTicks = 0;

		FUnit* FindUnit(int Id);

	private:
		/** Its turn has come: the gauge is full and the countdown starts. */
		void BecomeReady(FUnit& Unit, FTickReport& Report);
		/** Its turn is over, by choice or because the countdown ran out. */
		void EndTurn(FUnit& Unit, bool bTimedOut, FTickReport& Report);
	};
}
