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
	/** One thing that happened. The kind says which of the fields below mean anything. */
	enum class EEventKind : uint8_t
	{
		BecameReady,
		TurnEnded,
		TimedOut,
		Gone,
		Moved,
		/** A cast begun, finished, or lost with the caster. */
		CastStarted,
		CastFinished,
		CastFizzled,
		/** An ability going off: Unit is the caster, Slot and Target say what and where. */
		Resolved,
		/** One target of it. Amount is the damage or the healing. */
		Hit,
		Evaded,
		Critical,
		/** Soaked by a Shield or a Barrier, or turned away by Invulnerable. */
		Absorbed,
		StatusApplied,
		GaugeChanged,
		Knocked,
		Revived,
		Won,
	};

	/**
	 * What happened during a tick or while an order was applied.
	 *
	 * Ids alone were enough while units only walked, but a fight has to be read:
	 * what was cast, at whom, for how much, whether it was dodged. So this is a
	 * list of events in the order they happened, which the view plays back and
	 * the log prints.
	 *
	 * It is presentation, not state. Nothing in the rules reads it back, and two
	 * machines in a match agree by comparing checksums rather than these -- which
	 * is what lets a host send them on for the other side to watch without any
	 * of it being authoritative.
	 */
	struct FEvent
	{
		EEventKind Kind = EEventKind::BecameReady;
		/** Whoever it happened to, or the caster for a Resolved. */
		int Unit = -1;
		/** The other party: the caster of a Hit, or who applied a status. */
		int By = -1;
		int Slot = -1;
		int Amount = 0;
		FVec2 Where;
		/** The ability or status it concerns. */
		std::string Id;
	};

	struct FTickReport
	{
		std::vector<FEvent> Events;

		void Say(EEventKind Kind, int Unit)
		{
			FEvent Event;
			Event.Kind = Kind;
			Event.Unit = Unit;
			Events.push_back(Event);
		}

		/** Every unit an event of this kind happened to, for the simple cases. */
		std::vector<int> WhoWas(EEventKind Kind) const
		{
			std::vector<int> Out;
			for (const FEvent& Event : Events)
			{
				if (Event.Kind == Kind)
				{
					Out.push_back(Event.Unit);
				}
			}
			return Out;
		}
	};

	struct FAbility;

	/**
	 * One unit an ability would reach, and what it would do to it. Worked out
	 * without touching anything, so the same answer serves the forecast a player
	 * reads, the hits that land, and the computer weighing its options.
	 */
	struct FHit
	{
		int UnitId = -1;
		/** Where it was judged from, which for the caster is where it cast. */
		FVec2 Where;
		/** Damage, healing, or the health a revive brings it back with. */
		int Amount = 0;
		/** How far from the aim point, which decides who a single shot hits. */
		float Distance = 0.0f;
		double Flank = 1.0;
	};

	/** "unit", "point", "circle", "self", "line", "cone", "global" or "vector". */
	TMSIM_API std::string ShapeOf(const FAbility& Ability);
	/** Anything reaching past arm's length has to see where it is going. */
	TMSIM_API bool NeedsLineOfSight(const FAbility& Ability);

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

		/** Whether a unit at TargetPos is caught by this aim. */
		TMSIM_API bool InShape(const FAbility& Ability, const FVec2& From, const FVec2& Target,
			const FVec2& TargetPos) const;

		/** Whether the aim point itself is somewhere this ability may be pointed. */
		TMSIM_API bool InAbilityRange(const FUnit& Unit, int Slot, const FVec2& From,
			const FVec2& Target) const;

		/**
		 * What this ability would do, used from From and aimed at Target, without
		 * doing any of it. Resolution walks exactly this list, so the order is
		 * part of the rules: it is unit id order, and that fixes the order the
		 * dice are rolled in.
		 */
		TMSIM_API std::vector<FHit> Preview(const FUnit& Unit, int Slot, const FVec2& From,
			const FVec2& Target) const;

		/** The nearest living unit within this far of a spot, or null. */
		TMSIM_API const FUnit* UnitNear(const FVec2& Point, float Radius) const;

		// ------------------------------------------------- what an ability does

		/**
		 * Sends an ability off at a spot. This is the only place in the rules the
		 * dice are thrown, so a match and a replay roll the same numbers in the
		 * same order.
		 */
		/**
		 * Commits to an ability: pays for it, then either sends it off now or
		 * starts casting it. Assumes the order already passed Validate.
		 */
		TMSIM_API void UseAbility(FUnit& User, int Slot, const FVec2& Target, int Follow,
			FTickReport& Report);

		/** Ticks of casting an ability takes, after the setting is applied. */
		TMSIM_API int CastTicks(const FAbility& Ability) const;

		TMSIM_API void ResolveAbility(FUnit& User, int Slot, const FVec2& Target, FTickReport& Report);

		/** Takes health off, and the only way health is ever lost. */
		TMSIM_API int Hurt(FUnit& Target, int Amount);

		/** Soaks what a Shield or Barrier can; returns what is left to hurt with. */
		TMSIM_API int TakeFromShield(FUnit& Target, int Amount, FTickReport& Report);

		/** Puts a status on a unit, with the rules about refreshing and immunity. */
		TMSIM_API void AddStatus(FUnit& Target, const std::string& StatusId, int Turns,
			int Amount = 0, int By = -1);

		/** Down, and revivable for as long as the setting allows. */
		TMSIM_API void KnockOut(FUnit& Target, FTickReport& Report);

		/** A Stun taking the turn a unit is in the middle of. */
		TMSIM_API void StunInterrupt(FUnit& Target, FTickReport& Report);

		/** Sets Winner if one side has nobody left standing. */
		TMSIM_API void CheckWinner();

		/**
		 * Why this unit cannot send that ability at that spot, or empty if it can.
		 * An entry point in its own right, so it repeats the checks Validate makes
		 * rather than assuming them.
		 */
		TMSIM_API std::string ValidateAbility(int UnitId, int Slot, const FVec2& Target, int Follow);

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
		/** -1 while it is still being fought, 0 or 1 for a side, Draw for neither. */
		int Winner = -1;
		static constexpr int Draw = 2;
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
		/** Statuses act on the unit's own turn, then count down. */
		void TickStatuses(FUnit& Unit, FTickReport& Report);
		/** Burning ground hurts, a spring heals, when its turn comes round. */
		void GroundEffect(FUnit& Unit, FTickReport& Report);
		/** Mends a unit that has been left alone long enough. */
		void UndamagedRegen(FUnit& Unit, FTickReport& Report);
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
