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

#include "SimAbility.h"
#include "SimMap.h"
#include "SimOrder.h"
#include "SimRandom.h"
#include "SimTypes.h"
#include "SimUnit.h"

#include <map>
#include <string>
#include <utility>
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
		std::vector<int> Moved;
	};

	class FBattle
	{
	public:
		/** Places units with the head start their Speed earns them. */
		TMSIM_API void Start(uint64_t InSeed);

		/** One step of the clock, TicksPerSecond of these to the second. */
		TMSIM_API void Tick(FTickReport& Report);

		/** Runs the clock on, for tests and for catching a replay up. */
		TMSIM_API void Advance(int Ticks, FTickReport& Report);

		// ----------------------------------------------------------- timing

		/** Gauge per tick before statuses: Speed x TgPerSpeed x the multiplier. */
		TMSIM_API int BaseTgGain(const FUnit& Unit) const;
		/** The same, after Slow and after a held-back turn. */
		TMSIM_API int TgGain(const FUnit& Unit) const;
		/** Faster until its next turn, for a unit that kept its ability back. */
		TMSIM_API double HustleFactor(const FUnit& Unit) const;
		/** Ticks a READY unit has to act in, from its Patience. */
		TMSIM_API int ClockTicks(const FUnit& Unit) const;
		/** Ticks until this unit's turn comes (0 if it is ready now). */
		TMSIM_API int TicksToReady(const FUnit& Unit) const;

		// --------------------------------------------------------- what it does

		/**
		 * What an ability does to one target: damage, healing, or the health a
		 * revive brings it back with. The ground levels come in rather than being
		 * looked up, because the map is not ported yet -- and because it keeps
		 * this answerable without one.
		 */
		TMSIM_API int CalcAmount(const FUnit& User, const FAbility& Ability, FVec2 From,
			const FUnit& Target, FVec2 TargetPos, int FromLevel, int TargetLevel) const;

		/** Hits from the side and from behind land harder. */
		TMSIM_API double FlankBonus(const FUnit& Target, FVec2 TargetPos, FVec2 From) const;

		/** Chance in % the target gets out of the way. Friendly abilities never are. */
		TMSIM_API int EvadeChance(const FUnit& Target, const FAbility& Ability, const FUnit* Attacker) const;

		/** Chance in % that this unit's abilities land a critical hit. */
		TMSIM_API int CritChance(const FUnit& User) const;

		// --------------------------------------------------------- walking

		/** Metres this unit walks in a turn. */
		TMSIM_API double MoveOf(const FUnit& Unit, bool bSprint = false) const;

		/**
		 * Every node the unit could stand on this turn, and the metres walked to
		 * reach each. Allies can be walked through but not stood on; enemies
		 * block, and stepping out of an enemy's reach costs extra.
		 */
		TMSIM_API std::vector<std::pair<FNode, double>> ReachableNodes(const FUnit& Unit, bool bSprint = false);

		/** The way there, both ends included, or empty if there is no way. */
		TMSIM_API std::vector<FVec2> PathTo(const FUnit& Unit, const FNode& To, bool bSprint = false);

		/** "" if the unit may walk there now, otherwise why not. */
		TMSIM_API std::string ValidateMove(int UnitId, const FVec2& To, bool bSprint = false);

		/** Walks the unit there. It faces the way it last stepped. */
		TMSIM_API bool ApplyMove(int UnitId, const FVec2& To, bool bSprint, FTickReport& Report);

		/**
		 * Ends a unit's turn the way giving no further orders would. What it
		 * keeps of its gauge depends on what it did with the turn, so this is
		 * the rules' business rather than the caller's.
		 */
		TMSIM_API void EndTurnFor(FUnit& Unit, bool bTimedOut, FTickReport& Report);

		// ------------------------------------------------- orders and sight

		/** "" if the order may be applied now, otherwise why not. */
		/**
		 * Why this unit cannot use the ability in that slot right now, or empty
		 * if it can. Part of the rules rather than of the computer player,
		 * because a player clicking the button has to be told the same thing.
		 */
		TMSIM_API std::string AbilityBlockedReason(const FUnit& Unit, int Slot) const;

		TMSIM_API std::string Validate(const FOrder& Order);

		/**
		 * Applies an order that has passed Validate. Everything that changes a
		 * battle comes through here -- a player's order, the computer's, or a
		 * replay's -- so a battle is exactly the orders applied to it.
		 */
		TMSIM_API bool Apply(const FOrder& Order, FTickReport& Report);

		/** Whether this side can see that spot at all. */
		TMSIM_API bool CanSee(int Team, const FVec2& Point, int ExcludeId = -1) const;
		TMSIM_API double SightOf(const FUnit& Unit) const;
		/** Whether the ground between two points is low enough to see over. */
		TMSIM_API bool HasLineOfSight(const FVec2& A, const FVec2& B) const;

		TMSIM_API int LevelAt(const FVec2& Point) const;
		TMSIM_API double GroundHeight(const FVec2& Point) const;
		TMSIM_API bool IsCover(const FVec2& Point) const;
		/** -1 burns a unit that starts its turn here, +1 heals. */
		TMSIM_API int HazardAt(const FVec2& Point) const;
		TMSIM_API bool InBounds(const FVec2& Point) const;

		/** Walking distance from a spot to every node, ignoring units. */
		TMSIM_API const std::vector<double>& DistanceFrom(const FVec2& Goal);
		TMSIM_API double DistanceToNearest(const std::vector<FVec2>& Goals, const FNode& Node);

		TMSIM_API std::vector<const FUnit*> TeamUnits(int Team) const;

		/** Where each side starts, used when it cannot see anybody to walk at. */
		FVec2 SpawnPoints[2];

		FMap Map;

		// ------------------------------------------------------------ state

		std::vector<FUnit> Units;
		FTuning Tuning;
		FSimRandom Rng;
		int TickCount = 0;
		int Winner = -1;
		/** While the sides are still placing units, nothing else happens. */
		int PlanningTicks = 0;

		TMSIM_API FUnit* FindUnit(int Id);

		/**
		 * A number standing for everything about this battle that the rules
		 * decide. Two machines stepping the same orders from the same seed must
		 * agree on it after every step; the moment they do not, the match has
		 * come apart and carrying on would only make it worse.
		 *
		 * Deliberately not the same number the Godot game produces. Godot's is
		 * the hash of a printed string, so matching it would tie this to how
		 * Godot chooses to format a vector and to order a dictionary -- a very
		 * long way from anything about the rules. What has to agree here is two
		 * copies of this build, and Godot remains the reference for the rules
		 * themselves, which is what the other tests measure.
		 */
		TMSIM_API uint64_t Checksum() const;

	private:
		/** Its turn has come: the gauge is full and the countdown starts. */
		void BecomeReady(FUnit& Unit, FTickReport& Report);
		/** Its turn is over, by choice or because the countdown ran out. */
		void EndTurn(FUnit& Unit, bool bTimedOut, FTickReport& Report);

		/** Height levels this unit may cross in one step. */
		int JumpOf(const FUnit& Unit) const;

		/**
		 * Dijkstra over the navigation grid. Costs land in Cost and the way back
		 * in Parent, both indexed by node. Kept as one flat pass over arrays
		 * because the computer player asks for this constantly.
		 */
		void RunDijkstra(const std::vector<FNode>& Starts, double MaxCost, int Team, int Jump);

		/** Marks every node within a radius of a point. */
		void MarkBlocked(const FVec2& Point, double Radius, std::vector<uint8_t>& Flags) const;

		void HeapPush(double Cost, int Node);
		void HeapPop();
		void HeapSwap(int A, int B);

		/** Terrain never changes, so a distance field is worth keeping. */
		std::map<long long, std::vector<double>> DistanceCache;

		std::vector<double> Cost;
		std::vector<int> Parent;
		std::vector<double> HeapCost;
		std::vector<int> HeapNode;
	};
}
