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
#include "SimItem.h"
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
		/**
		 * A turn spent at a watchtower: Unit captured, Slot is the tower, Amount
		 * the turns its side has put in, By the turns it needs. Not Godot's.
		 */
		Capturing,
		/** A watchtower changed hands: Unit took it, Slot is the tower, By the side now holding it. */
		Captured,

		// Neutral camps (Docs/design/feat-neutral-camps.md). Not Godot's.

		/** A camp (Slot) wakes in Amount seconds, at Where. */
		CampWarning,
		/** A camp (Slot) woke at Where. */
		CampAwake,
		/** A camp's (Slot) last monster fell or fled; Amount is the cache its loot is in, or -1. */
		CampCleared,
		/** Items are on the ground: Slot is the cache, at Where. */
		CacheAppeared,
		/** Unit took item Id from cache Slot. */
		ItemTaken,
		/** Unit left item Id in cache Slot. */
		ItemDropped,
		/** A monster (Unit) is set off, and fights from its next turn. By is who set it off, or -1. */
		MonsterAlert,
		/** A monster (Unit) ran off the board with what it carried. */
		Escaped,
		/** A monster (Unit) joined By's side (Amount: turns), or went back (Amount 0). */
		Tamed,
		/** A boss (Unit) moved to phase Amount. */
		PhaseChanged,
		/** Unit was staggered by hits from behind. */
		Staggered,
		/** Unit drew power from the shrine of camp Slot. */
		ShrineUsed,
		/** Unit moved to Where by an ability (Blink, Swap). */
		Teleported,
		/** Unit was spared a knock-out by an item (Phoenix Feather) or healed by Rewind. */
		Saved,
		// The second set of statuses (Docs/design/feat-status-effects.md).
		/** Two things met on Unit: Id is what came of it ("shock", "freeze", "ignite", "douse", "thaw"). By is who caused it. */
		Reaction,
		/** A hit meant for By landed on Unit instead: Id "guard" (a guardian stepped in) or "reflect" (it bounced back). */
		Redirected,
		/** Unit (Terrified) ran from By to Where. */
		Fled,
		/** Unit is back on its own side (Charmed wore off). */
		CharmEnded,
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

	/**
	 * A watchtower (Docs/design/feat-objectives.md): a spot on the board a side
	 * takes by spending turns standing at it, and then sees from. Not Godot's;
	 * a battle has none unless its rules ask for some.
	 */
	struct FWatchtower
	{
		/** Where it stands, in metres: the middle of a tile. */
		FVec2 Pos;
		/** The side holding it, or -1 for nobody yet. */
		int Owner = -1;
		/** The side part-way to taking it, or -1; and how many turns it has put in. */
		int Capturer = -1;
		int Progress = 0;
	};

	namespace Watchtower
	{
		/** How near a unit must stand to capture one, in metres. */
		inline constexpr double Reach = 2.5;
		/** How high above its ground its side looks out from, in metres. */
		inline constexpr float EyeHeight = 4.0f;
		/** Closest a tower may be to where either side starts, in metres. */
		inline constexpr double AwayFromStart = 12.0;
		/** Closest two towers may stand to each other (a pair's two included), in metres. */
		inline constexpr double Apart = 10.0;
		/** Salt for the placing generator, so it never shares a sequence with the battle's dice. */
		inline constexpr uint64_t Salt = 0x5741544348544F57ull;  // "WATCHTOW"
	}

	/** Neutral camps (Docs/design/feat-neutral-camps.md). */
	enum class ECampState : uint8_t
	{
		/** Its monsters are off the board; Timer counts down to them waking. */
		Waiting,
		/** Its monsters are on the board. */
		Awake,
	};

	/** One kind of camp: who is in it (by class id), and what it leaves behind. */
	struct FCampKind
	{
		std::string Id;
		std::string Name;
		/** 0 easy, 1 medium, 2 hard, 3 epic. */
		int Tier = 0;
		/** The monsters that wake, in order. */
		std::vector<std::string> Members;
		/** Held back until one of them summons them (a boss's adds). */
		std::vector<std::string> Reserves;
		/** The tier of the item its cache holds (0 common .. 3 epic), or -1 for none; and how many. */
		int LootTier = 0;
		int LootCount = 1;
		/** An epic camp's cache also holds one item of this tier, or -1. */
		int BonusTier = -1;
		/** Its first member carries an item of this tier while it lives (the Treasure Runner), or -1. */
		int CarriedTier = -1;
		/** A shrine stands at the camp: power for whoever stands on it (Crag Brute). */
		bool bShrine = false;
	};

	/** Every kind of camp, easy first. Bosses are made from the map's boss class, not listed here. */
	TMSIM_API const std::vector<FCampKind>& CampKinds();

	struct FCamp
	{
		/** Its kind, by place in CampKinds(), or -1 for the boss camp. */
		int Kind = -1;
		int Tier = 0;
		/** Where it stands: the middle of a navigation node. */
		FVec2 Spot;
		ECampState State = ECampState::Waiting;
		/** Waiting: ticks until it wakes. */
		int Timer = 0;
		/** On red's half of the board (its route is turned about to match its twin's). */
		bool bRed = false;
		/** Its monsters by unit id, in its kind's order; then any held in reserve. */
		std::vector<int> Members;
		std::vector<int> Reserves;
		/** A patrol's waypoints. */
		std::vector<FVec2> Route;
		/** A shrine at the camp, and ticks until it gives power again. */
		bool bShrine = false;
		int ShrineRest = 0;
		/** Times it has woken. */
		int Wakes = 0;
	};

	/** Items on the ground, for anybody standing near to take. Never removed, so an index stays good. */
	struct FCache
	{
		FVec2 Pos;
		std::vector<const FItemDef*> Items;
		/** Each item's ability cooldown, which stays with the item. */
		std::vector<int> Cooldowns;
	};

	namespace Camp
	{
		/** Salts for the camps' and the loot's own generators. */
		inline constexpr uint64_t Salt = 0x43414D5053504F54ull;      // "CAMPSPOT"
		inline constexpr uint64_t LootSalt = 0x4C4F4F5452414E44ull;  // "LOOTRAND"
		/** Closest a camp may be to another, a watchtower or a start, in metres. */
		inline constexpr double Apart = 8.0;
		/** How near a unit must stand to take from a cache, in metres. */
		inline constexpr double TakeReach = 1.5;
		/** How near an ambusher lets anyone come before it shows, in metres. */
		inline constexpr double AmbushReach = 3.0;
		/** How near a guardian lets anyone come to its ward, and how far from it it goes, in metres. */
		inline constexpr double WardReach = 2.0;
		inline constexpr double WardLeash = 3.0;
		/** How far a lookout's cry carries to a waiting camp, in metres. */
		inline constexpr double LookoutReach = 15.0;
		/** How near the shrine a unit must stand, and its rest, in seconds. */
		inline constexpr double ShrineReach = 1.5;
		inline constexpr int ShrineRestSeconds = 45;
		/** Turns a monster holds a grudge; turns a hurt docile one runs. */
		inline constexpr int GrudgeTurns = 3;
		inline constexpr int DocileFleeTurns = 1;
		/** Hits from behind that stagger; a tamed monster's turns. */
		inline constexpr int StaggerHits = 3;
		inline constexpr int TameTurns = 3;
		/** Seconds before a tier first wakes, comes back after it is cleared, and warns. Easy to epic. */
		inline constexpr int FirstWake[4] = { 0, 40, 100, 180 };
		inline constexpr int Respawn[4] = { 60, 90, 150, 300 };
		inline constexpr int Warning[4] = { 5, 10, 10, 15 };
	}

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

		// ---------------- the second set of statuses (SimStatuses.cpp, Docs/design/feat-status-effects.md)

		/** Takes a status off, if it is there. True if it was. */
		TMSIM_API bool RemoveStatus(FUnit& Unit, const std::string& StatusId);
		/** Back on its own side, if Charmed has gone from it. Report may be null. */
		TMSIM_API void ReleaseCharm(FUnit& Unit, FTickReport* Report);
		/**
		 * A single-target ability meeting Reflect (bounces to the caster) or
		 * Guarded (the guardian takes it): Struck and Amount become the new
		 * target and what it would take. Before the dice, so they roll the same.
		 */
		TMSIM_API void Redirect(FUnit& User, const FAbility& Ability, FUnit*& Struck, int& Amount, FTickReport& Report);
		/** What an element does to a unit that is Wet, Oiled or Chilled, and what water and ice leave behind. */
		TMSIM_API void ElementReactions(FUnit& User, const FAbility& Ability, FUnit& Struck, FTickReport& Report);
		/** Suppressed and it walked: the unit that suppressed it gets a free blow. */
		TMSIM_API void SuppressedMoved(FUnit& Unit, FTickReport& Report);
		/** Terrified: its full move away from what it fears, before it may act. */
		TMSIM_API void Flee(FUnit& Unit, int From, FTickReport& Report);
		/** Stop counts in ticks, not the holder's turns (it has none while stopped). */
		TMSIM_API void TickStops(FUnit& Unit);
		/** Reraise: seconds between falling and standing again. */
		static constexpr int ReraiseSeconds = 3;
		/** Guarded: how far the guardian may stand from its ward, in metres. */
		static constexpr float GuardReach = 3.0f;
		/** Lightning on a Wet unit shocks the Wet within this many metres of it. */
		static constexpr float ShockReach = 2.0f;

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

		/** Whether this side can see that spot at all: through its units' eyes, or a watchtower it holds. */
		TMSIM_API bool CanSee(int Team, const FVec2& Point, int ExcludeId = -1) const;
		TMSIM_API double SightOf(const FUnit& Unit) const;
		/** Whether the ground between two points is low enough to see over. */
		TMSIM_API bool HasLineOfSight(const FVec2& A, const FVec2& B) const;
		/** The same, looking out from this many metres above the ground at A instead of a unit's eyes. */
		TMSIM_API bool HasLineOfSightFrom(const FVec2& A, float EyeAboveGround, const FVec2& B) const;

		// ------------------------------------------------------------ items

		/** Why the units' items can't be taken into this battle, or empty if they can (budget, duplicates). */
		TMSIM_API std::string LoadoutProblem() const;

		// ------------------------------------------------------ watchtowers

		/** Why this unit cannot spend its turn capturing that tower now, or empty if it can. */
		TMSIM_API std::string ValidateCapture(int UnitId, int Tower) const;
		/** Whether the watchtower sees this spot for the side holding it. */
		TMSIM_API bool TowerSees(const FWatchtower& Tower, const FVec2& Point) const;
		/** Turns a side must spend at a tower to take it. */
		TMSIM_API int CaptureTurnsNeeded() const;
		/** The tower within reach of this spot that is nearest to it, or -1. */
		TMSIM_API int TowerNear(const FVec2& Point) const;

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
		/** Which sides have said they are done placing (game_state.gd:201). */
		bool PlanningDone[2] = { false, false };
		/** How far from its spawn point a side may place a unit, in metres. */
		static constexpr double PlanningRadius = 6.0;

		bool IsPlanning() const { return PlanningTicks > 0; }
		/** Whether this unit may be put down here while planning (game_state.gd:1484-1496). */
		TMSIM_API bool CanPlace(const FUnit& Unit, const FVec2& Point) const;
		/** Every node it may be put down on, on the one-metre lattice (game_state.gd:1499-1512). */
		TMSIM_API std::vector<FNode> PlaceableNodes(const FUnit& Unit) const;
		/**
		 * Ticks each side has stood alone in the middle. Kept when a side is
		 * pushed off: holding the middle is won by adding up, not in one go.
		 */
		int CaptureTicks[2] = { 0, 0 };
		/**
		 * The battle's watchtowers, placed when it starts (PlaceWatchtowers):
		 * the one in the middle first if there is one, then each pair, blue's
		 * before red's.
		 */
		std::vector<FWatchtower> Watchtowers;

		// ------------------------------------------------- neutral camps

		/** The map's boss class (the map file's "boss"), or empty. Set before Start. */
		std::string BossJob;
		/** The camps (PlaceCamps): pairs, blue's before red's, easy first; the boss last. */
		std::vector<FCamp> Camps;
		/** Items lying on the ground. */
		std::vector<FCache> Caches;
		/** The camps' own generators: where they stand, and what they drop. The battle's dice never move for them. */
		FSimRandom CampRng;
		FSimRandom LootRng;

		/** Whether this side sees that unit: its spot, and it isn't vanished or lying in ambush. */
		TMSIM_API bool CanSeeUnit(int Team, const FUnit& Unit) const;
		/** Vanished, or an ambusher lying in wait, with nobody of this side right on top of it. */
		TMSIM_API bool Hidden(int Team, const FUnit& Unit) const;
		/** Why this unit cannot take that item from that cache now, or empty if it can. */
		TMSIM_API std::string ValidateTake(int UnitId, int Cache, const std::string& ItemId, int GearSlot) const;
		/** Why it cannot drop what is in that slot now, or empty. */
		TMSIM_API std::string ValidateDrop(int UnitId, int GearSlot) const;
		/** The cache within reach of this spot nearest to it that holds anything, or -1. */
		TMSIM_API int CacheNear(const FVec2& Point) const;
		/** A unit that is not a monster, or a monster on a side's side (tamed): someone a wild monster fights. */
		bool IsQuarry(const FUnit& Unit) const { return Unit.IsAlive() && Unit.Team != 2; }
		/** Where a monster measures its leash from: its ward, its route, or its home. */
		TMSIM_API FVec2 LeashAnchor(const FUnit& Monster) const;
		/** No ally of this unit's within Items::AloneReach of that spot. */
		TMSIM_API bool IsAlone(const FUnit& Unit, const FVec2& At) const;
		/** Whether a wild monster has anyone it may fight within its leash. */
		TMSIM_API bool HasQuarry(const FUnit& Monster) const;

		/** How far the middle reaches, in metres (game_state.gd:75). */
		static constexpr double CaptureRadius = 4.0;

		/** The middle of the map, which holding the middle is fought over. */
		TMSIM_API FVec2 CapturePoint() const;
		/** How much of its health a side has left, as a share of what it started with. */
		TMSIM_API double HealthShare(int Team) const;

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
		/** A side standing alone in the middle long enough wins. */
		void TickCapture(FTickReport& Report);
		/** Puts up the watchtowers the rules ask for, where a generator of their own says. */
		void PlaceWatchtowers(uint64_t InSeed);
		/** A turn spent at a tower: its side's progress, and the tower if that is enough. */
		void ApplyCapture(FUnit& Unit, int Tower, FTickReport& Report);
		/** Puts the camps the rules ask for on the board, their monsters waiting off it. */
		void PlaceCamps(uint64_t InSeed);
		/** Counts camps down, wakes them, clears them, rests shrines. */
		void TickCamps(FTickReport& Report);
		void WakeCamp(int Index, FTickReport& Report);
		/** Where a camp of this tier may stand on blue's half; FVec2(-1,-1) if nowhere. */
		FVec2 CampSpot(int Tier, int SkipCamp);
		/** A patrol route round a spot, turned about for red. */
		std::vector<FVec2> PatrolRoute(const FVec2& Spot, bool bRed);
		/** Draws items of a tier nobody has yet; a boss's own only for that boss. */
		void RollLoot(int Tier, int Count, std::vector<const FItemDef*>& Into, const std::string& Boss = std::string());
		/** Puts items on the ground here; returns the cache. */
		int DropItems(const FVec2& Where, const std::vector<const FItemDef*>& Items, const std::vector<int>& Cooldowns, FTickReport& Report);
		/** A unit gone for good: what it carried falls where it was; a monster leaves the board. */
		void OnGone(FUnit& Unit, FTickReport& Report);
		/** A monster's turn begins: what it has seen and suffered decides its mood. True if that took the turn. */
		bool MonsterTurnStarts(FUnit& Unit, FTickReport& Report);
		/** Whether anything sets this resting monster off now. */
		bool MonsterTriggered(const FUnit& Monster, int& OutBy) const;
		/** Sets a monster (and its bonded campmates) off. */
		void AlertMonster(FUnit& Monster, int By, FTickReport& Report);
		/** A monster was hurt by Attacker: grudges, flight, guardians, stagger, phases. */
		void MonsterHurt(FUnit& Monster, const FUnit& Attacker, double Flank, FTickReport& Report);
		/** A shrine under a unit starting its turn. */
		void UseShrine(FUnit& Unit, FTickReport& Report);
		void ApplyTake(FUnit& Unit, int Cache, const std::string& ItemId, int GearSlot, FTickReport& Report);
		/** A side's unit that ends a move by items takes, free, what fits its empty slots: best tier first. */
		void PickUpAt(FUnit& Unit, FTickReport& Report);
		void ApplyDrop(FUnit& Unit, int GearSlot, FTickReport& Report);
		/** Sets an item into a slot, with what it does to health. */
		void PutInSlot(FUnit& Unit, int Slot, const FItemDef* Item, int Cooldown);
		/** An ability's special (FAbility::Special) on whoever it reached. */
		void ApplySpecial(FUnit& User, const FAbility& Ability, const FVec2& Target, FUnit* Struck, FTickReport& Report);

		/** The time limit ran out: the healthier side wins, level shares draw. */
		void FinishOnTime(FTickReport& Report);
		/** The auras of every living unit that reach this one, refreshed as its turn begins. */
		void ApplyAuras(FUnit& Unit);
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
