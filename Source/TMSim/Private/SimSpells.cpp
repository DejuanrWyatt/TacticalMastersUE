// The unique and mobility spells (2026-10-05; Docs/design/feat-new-spells.md).
//
// Thirty abilities the human asked for after the area denial zones: fifteen that
// do something no ability did before (a disease that spreads, two enemies
// sharing pain, a bomb on a countdown...) and fifteen that move units about
// (dash, grapple, hook, shove, gates...). Most are a status with a rule of its
// own, read where it matters (TickStatuses, EndTurn, Hurt, KnockOut,
// ResolveAbility); the rest are an ability's "special", done here. None of it
// runs unless one of these statuses or specials is in the battle, so every older
// battle plays exactly as before.

#include "SimAbility.h"
#include "SimBattle.h"

#include <algorithm>
#include <cmath>

namespace TMSim
{
	namespace
	{
		/** A boss is too heavy to drag, push or trade places with. */
		bool IsBoss(const FUnit& Unit)
		{
			const FMonsterInfo* Info = Unit.bMonster ? Unit.MonsterInfo() : nullptr;
			return Info && Info->Tier >= 3;
		}

		FVec2 Toward(const FVec2& From, const FVec2& To)
		{
			const FVec2 Along = To - From;
			const float Length = Along.Length();
			return Length > 0.001f ? Along / Length : FVec2(0.0f, 0.0f);
		}

		/** Contagion: how near an ally catches it, and how many times it passes on from the first. */
		constexpr float PlagueReach = 2.0f;
		constexpr int PlagueJumps = 2;
		/** Time Bomb: how far its blast reaches, and what it does if the ability gave no number. */
		constexpr float BombReach = 4.0f;
		constexpr int BombDefault = 45;
		/** Soul Link: how near the second enemy must stand (the pair share within 8 m: SimResolve.cpp). */
		constexpr float LinkPick = 5.0f;
		/** Purge Transfer, Chain Mend: how far to the enemy that takes the ills, and between leaps, and how many. */
		constexpr float TransferReach = 6.0f;
		constexpr float ChainReach = 3.0f;
		constexpr int ChainLeaps = 2;
		constexpr double ChainFalloff = 0.75;
		/** Gravity Well, Shove, Disengage: metres moved. */
		constexpr float GravityPull = 2.0f;
		constexpr float ShoveDistance = 4.0f;
		constexpr float DisengageDistance = 4.0f;
		/** Blood Pact: the share of the user's health it costs. */
		constexpr double PactShare = 0.15;
	}

	// ------------------------------------------------------------ places

	bool FBattle::SpotNear(const FVec2& Centre, const FVec2& Want, float Reach, float Closest, int IgnoreId, FVec2& OutSpot) const
	{
		const FNode Middle = FMap::NodeOf(Centre);
		const int Span = std::min(8, static_cast<int>(std::ceil(Reach / 0.5f)) + 1);
		bool bFound = false;
		float Best = 0.0f;
		for (int DY = -Span; DY <= Span; ++DY)
		{
			for (int DX = -Span; DX <= Span; ++DX)
			{
				const FNode Node{ Middle.X + DX, Middle.Y + DY };
				const FVec2 Spot = FMap::NodePos(Node);
				const float FromCentre = Spot.DistanceTo(Centre);
				if (FromCentre > Reach + 0.001f || FromCentre < Closest || !InBounds(Spot) || !Map.NodeWalkable(Node) || IsCover(Spot))
				{
					continue;
				}
				bool bTaken = false;
				for (const FUnit& Other : Units)
				{
					bTaken = bTaken || (Other.IsAlive() && !Other.bOffBoard && Other.Id != IgnoreId
						&& Other.Pos.DistanceTo(Spot) < Ground::UnitSpacing);
				}
				if (bTaken)
				{
					continue;
				}
				const float Score = Spot.DistanceTo(Want);
				if (!bFound || Score < Best)
				{
					bFound = true;
					Best = Score;
					OutSpot = Spot;
				}
			}
		}
		return bFound;
	}

	FVec2 FBattle::SlideEnd(const FUnit& Mover, const FVec2& Dir, float Max, bool bPassAllies) const
	{
		const FVec2 Start = Mover.Pos;
		FVec2 Last = Start;
		FVec2 Previous = Start;
		const int Jump = JumpOf(Mover);
		for (float Along = 0.5f; Along <= Max + 0.001f; Along += 0.5f)
		{
			const FVec2 Point = FMap::Snap(Start + Dir * Along);
			if (Point == Previous)
			{
				continue;
			}
			if (!InBounds(Point) || !Map.NodeWalkable(FMap::NodeOf(Point)) || IsCover(Point))
			{
				break;
			}
			if (!Mover.Flies() && std::abs(LevelAt(Point) - LevelAt(Previous)) > Jump)
			{
				break;
			}
			const FUnit* Blocking = nullptr;
			for (const FUnit& Other : Units)
			{
				if (!Blocking && Other.IsAlive() && !Other.bOffBoard && Other.Id != Mover.Id && Other.Pos.DistanceTo(Point) < Ground::UnitSpacing)
				{
					Blocking = &Other;
				}
			}
			Previous = Point;
			if (Blocking)
			{
				if (bPassAllies && Blocking->Team == Mover.Team)
				{
					continue;  // through, but not stopped on
				}
				break;
			}
			Last = Point;
		}
		return Last;
	}

	bool FBattle::LandingFor(const FUnit& User, const FAbility& Ability, const FVec2& Target, FVec2& OutSpot) const
	{
		return LandingFrom(User, Ability, User.Pos, Target, OutSpot);
	}

	bool FBattle::LandingFrom(const FUnit& User, const FAbility& Ability, const FVec2& From, const FVec2& Target, FVec2& OutSpot) const
	{
		// Leap: as near the aim point as can be, within 1.5 m of it (or the
		// ability's area, if wider). Behind: round the unit aimed at, as near as
		// can be to the spot a metre behind it, not on top of it. Vault: the far
		// side of the unit aimed at, from where the user stands; Charge: the near
		// side, where the run ends.
		if (Ability.Special == "vault" || Ability.Special == "charge")
		{
			const FUnit* Victim = UnitNear(Target, Ground::HitRadius);
			if (!Victim || Victim->Id == User.Id)
			{
				return false;
			}
			const FVec2 Want = Ability.Special == "vault" ? Victim->Pos + Toward(From, Victim->Pos) * 1.0f
				: Victim->Pos + Toward(Victim->Pos, From) * 1.0f;
			return SpotNear(Victim->Pos, Want, 1.6f, 0.6f, User.Id, OutSpot);
		}
		// Leap and behind (2026-10-03), as they always were.
		FVec2 Centre = FMap::Snap(Target);
		FVec2 Want = Centre;
		float Reach = std::max(1.5f, Ability.Aoe);
		float Closest = 0.0f;
		if (Ability.Special == "behind")
		{
			const FUnit* Victim = UnitNear(Target, Ground::HitRadius);
			if (!Victim || Victim->Id == User.Id)
			{
				return false;
			}
			Centre = Victim->Pos;
			Want = Victim->Pos - Victim->Facing * 1.0f;
			Reach = 1.6f;
			Closest = 0.6f;
		}
		else if (Ability.Special != "leap")
		{
			return false;
		}
		const FNode Middle = FMap::NodeOf(Centre);
		bool bFound = false;
		float Best = 0.0f;
		// Rows then columns, nearest-first by distance, the first of equals kept:
		// the same spot on every machine.
		for (int DY = -4; DY <= 4; ++DY)
		{
			for (int DX = -4; DX <= 4; ++DX)
			{
				const FNode Node{ Middle.X + DX, Middle.Y + DY };
				const FVec2 Spot = FMap::NodePos(Node);
				const float FromCentre = Spot.DistanceTo(Centre);
				if (FromCentre > Reach + 0.001f || FromCentre < Closest || !InBounds(Spot) || !Map.NodeWalkable(Node))
				{
					continue;
				}
				const FUnit* Other = UnitNear(Spot, Ground::UnitSpacing);
				if (Other && Other->Id != User.Id)
				{
					continue;
				}
				const float Score = Spot.DistanceTo(Want);
				if (!bFound || Score < Best)
				{
					bFound = true;
					Best = Score;
					OutSpot = Spot;
				}
			}
		}
		return bFound;
	}

	std::string FBattle::SpecialProblem(const FUnit& User, const FAbility& Ability, const FVec2& From, const FVec2& Target) const
	{
		const std::string& Special = Ability.Special;
		if (Special.empty())
		{
			return std::string();
		}
		FVec2 Landing;
		if (Special == "leap" || Special == "behind" || Special == "vault" || Special == "charge")
		{
			if (!LandingFrom(User, Ability, From, Target, Landing))
			{
				return Special == "behind" ? "There's no room behind that target."
					: Special == "vault" ? "There's no room on the far side."
					: Special == "charge" ? "There's no room to stop beside that target." : "There's nowhere to land there.";
			}
			return std::string();
		}
		if (Special == "shadowhop")
		{
			// Into tall grass, or its own side's smoke, where nobody stands.
			const FVec2 Spot = FMap::Snap(Target);
			if (!InBounds(Spot) || !Map.NodeWalkable(FMap::NodeOf(Spot)) || IsCover(Spot))
			{
				return "There's nowhere to land there.";
			}
			for (const FUnit& Other : Units)
			{
				if (Other.IsAlive() && !Other.bOffBoard && Other.Id != User.Id && Other.Pos.DistanceTo(Spot) < Ground::UnitSpacing)
				{
					return "Somebody is standing there.";
				}
			}
			bool bCover = !Map.Grass.empty() && Map.InGrass(Spot);
			for (const FZone& Zone : Zones)
			{
				const FAbility* Laid = ZoneAbility(Zone);
				bCover = bCover || (Laid && Laid->bZoneHide && Zone.Team == User.Team && ZoneLive(Zone) && ZoneCovers(Zone, Spot));
			}
			return bCover ? std::string() : std::string("Shadow Hop lands only in tall grass or your side's smoke.");
		}
		if (Special == "dash")
		{
			const FVec2 Dir = Toward(User.Pos, Target);
			const float Length = std::min(Ability.MaxRange, User.Pos.DistanceTo(Target));
			if (Dir.Length() < 0.5f || SlideEnd(User, Dir, Length, true) == User.Pos)
			{
				return "There's no room to dash that way.";
			}
			return std::string();
		}
		if (Special == "hook" || Special == "riptide")
		{
			const FUnit* Victim = UnitNear(Target, Ground::HitRadius);
			if (Special == "hook" && Victim && IsBoss(*Victim))
			{
				return "That is too heavy to drag.";
			}
		}
		return std::string();
	}

	void FBattle::SpellMoved(FUnit& Unit, const FVec2& To, const char* How, FTickReport& Report)
	{
		Unit.Pos = To;
		FEvent Event;
		Event.Kind = EEventKind::Teleported;
		Event.Unit = Unit.Id;
		Event.Where = To;
		Event.Id = How;
		Report.Events.push_back(Event);
	}

	// ----------------------------------------------- moving the user

	void FBattle::MoveForSpell(FUnit& User, int Slot, const FAbility& Ability, const FVec2& Target, FTickReport& Report)
	{
		const std::string& Special = Ability.Special;
		if (Special == "dash")
		{
			// Straight at the aim point, up to its range: through allies, never
			// through enemies or rock, and off the ground of anyone's zone of control.
			const FVec2 Dir = Toward(User.Pos, Target);
			const FVec2 End = SlideEnd(User, Dir, std::min(Ability.MaxRange, User.Pos.DistanceTo(Target)), true);
			if (End != User.Pos)
			{
				if (!User.HasStatus("offbalance"))
				{
					User.Facing = Dir;
				}
				SpellMoved(User, End, "dash", Report);
			}
		}
		else if (Special == "disengage")
		{
			// Straight away from the nearest enemy.
			const FUnit* Nearest = nullptr;
			for (const FUnit& Other : Units)
			{
				if (Other.IsAlive() && !Other.bOffBoard && Other.Team != User.Team
					&& (!Nearest || Other.Pos.DistanceTo(User.Pos) < Nearest->Pos.DistanceTo(User.Pos)))
				{
					Nearest = &Other;
				}
			}
			FVec2 Dir = Nearest ? Toward(Nearest->Pos, User.Pos) : User.Facing * -1.0f;
			if (Dir.Length() < 0.5f)
			{
				Dir = User.Facing * -1.0f;
			}
			const FVec2 End = SlideEnd(User, Dir, DisengageDistance, false);
			if (End != User.Pos)
			{
				SpellMoved(User, End, "dash", Report);
			}
		}
		else if (Special == "shadowhop")
		{
			SpellMoved(User, FMap::Snap(Target), "shadowhop", Report);
			User.bSpotted = false;
		}
		else if (Special == "recall")
		{
			// Cast once, it marks the spot (a zone that lasts); cast again while the
			// mark lasts, the user is back there and the mark is gone.
			for (size_t i = 0; i < Zones.size(); ++i)
			{
				if (Zones[i].Owner == User.Id && Zones[i].AbilityId == Ability.Id)
				{
					FVec2 Spot;
					if (SpotNear(Zones[i].Target, Zones[i].Target, 1.6f, 0.0f, User.Id, Spot))
					{
						SpellMoved(User, Spot, "recall", Report);
					}
					FEvent Ended;
					Ended.Kind = EEventKind::ZoneEnded;
					Ended.Unit = User.Id;
					Ended.Slot = Zones[i].Slot;
					Ended.Where = Zones[i].Target;
					Ended.Id = Ability.Id;
					Report.Events.push_back(Ended);
					Zones.erase(Zones.begin() + static_cast<std::ptrdiff_t>(i));
					return;
				}
			}
			LayZone(User, Slot, Ability, User.Pos, Report);
		}
	}

	// ------------------------------------------- on each unit reached

	void FBattle::ApplySpellSpecial(FUnit& User, const FAbility& Ability, const FVec2& Target, FUnit& Struck, FTickReport& Report)
	{
		const std::string& Special = Ability.Special;
		auto Says = [&Report, &User](const FUnit& On, const std::string& StatusId)
		{
			FEvent Event;
			Event.Kind = EEventKind::StatusApplied;
			Event.Unit = On.Id;
			Event.By = User.Id;
			Event.Where = On.Pos;
			Event.Id = StatusId;
			Report.Events.push_back(Event);
		};
		if (Special == "link")
		{
			// The one struck and the nearest of its side to it, each holding the other.
			if (Struck.Id == User.Id)
			{
				return;
			}
			FUnit* Partner = nullptr;
			for (FUnit& Other : Units)
			{
				if (Other.IsAlive() && !Other.bOffBoard && Other.Team == Struck.Team && Other.Id != Struck.Id && Other.Id != User.Id
					&& Other.Pos.DistanceTo(Struck.Pos) <= LinkPick
					&& (!Partner || Other.Pos.DistanceTo(Struck.Pos) < Partner->Pos.DistanceTo(Struck.Pos)))
				{
					Partner = &Other;
				}
			}
			if (!Partner)
			{
				return;
			}
			AddStatus(Struck, "linked", 4, 0, Partner->Id);
			AddStatus(*Partner, "linked", 4, 0, Struck.Id);
			if (Struck.HasStatus("linked"))
			{
				Says(Struck, "linked");
			}
			if (Partner->HasStatus("linked"))
			{
				Says(*Partner, "linked");
			}
		}
		else if (Special == "gravity")
		{
			if (Struck.Id == User.Id || IsBoss(Struck))
			{
				return;
			}
			const float Gap = Struck.Pos.DistanceTo(Target);
			if (Gap < 1.0f)
			{
				return;
			}
			const FVec2 End = SlideEnd(Struck, Toward(Struck.Pos, Target), std::min(GravityPull, Gap - 0.9f), false);
			if (End != Struck.Pos)
			{
				SpellMoved(Struck, End, "pull", Report);
			}
		}
		else if (Special == "transfer")
		{
			// Off the ally, every ill but a charm; onto the nearest enemy within reach of it.
			// What is tied to a unit (a taunt, a tether, a link...) is only lifted, but
			// for a plague or a bomb, which go on as they were.
			if (Struck.Team != User.Team)
			{
				return;
			}
			std::vector<FStatus> Ills;
			std::vector<FStatus> Kept;
			for (const FStatus& Status : Struck.Statuses)
			{
				const FStatusDef* Def = FindStatus(Status.Id);
				if (Def && Def->bHarmful && Status.Id != "charmed" && Status.Id != "hunted")
				{
					Ills.push_back(Status);
				}
				else
				{
					Kept.push_back(Status);
				}
			}
			Struck.Statuses.swap(Kept);
			FUnit* Enemy = nullptr;
			for (FUnit& Other : Units)
			{
				if (Other.IsAlive() && !Other.bOffBoard && Other.Team != Struck.Team && Other.Pos.DistanceTo(Struck.Pos) <= TransferReach
					&& (!Enemy || Other.Pos.DistanceTo(Struck.Pos) < Enemy->Pos.DistanceTo(Struck.Pos)))
				{
					Enemy = &Other;
				}
			}
			for (const FStatus& Ill : Ills)
			{
				const FStatusDef* IllDef = FindStatus(Ill.Id);
				const bool bTied = IllDef && IllDef->bSourced && Ill.Id != "plague" && Ill.Id != "bomb";
				if (Enemy && Enemy->IsAlive() && !bTied)
				{
					AddStatus(*Enemy, Ill.Id, std::max(1, Ill.Turns), Ill.Amount, Ill.By);
					if (Enemy->HasStatus(Ill.Id))
					{
						Says(*Enemy, Ill.Id);
					}
				}
			}
		}
		else if (Special == "pact")
		{
			// Health for an ally's cooldowns: never the last of it.
			if (Struck.Id == User.Id || Struck.Team != User.Team)
			{
				return;
			}
			const int Price = std::min(RoundToInt(User.MaxHp() * PactShare), User.Hp - 1);
			if (Price > 0)
			{
				Hurt(User, Price);
				FEvent Paid;
				Paid.Kind = EEventKind::Hit;
				Paid.Unit = User.Id;
				Paid.Amount = Price;
				Paid.Where = User.Pos;
				Paid.Id = "pact";
				Report.Events.push_back(Paid);
			}
			for (int Slot = 0; Slot < AbilitySlots; ++Slot)
			{
				if (Slot != 3)
				{
					Struck.Cooldowns[Slot] = 0;
				}
			}
		}
		else if (Special == "spiritswap")
		{
			// Trade shares of health: 90% and 20% become 20% and 90%.
			if (Struck.Id == User.Id || !Struck.IsAlive())
			{
				return;
			}
			const int UserNow = std::max(1, RoundToInt(static_cast<double>(Struck.Hp) / std::max(1, Struck.MaxHp()) * User.MaxHp()));
			const int StruckNow = std::max(1, RoundToInt(static_cast<double>(User.Hp) / std::max(1, User.MaxHp()) * Struck.MaxHp()));
			for (const std::pair<FUnit*, int>& Change : { std::make_pair(&User, UserNow), std::make_pair(&Struck, StruckNow) })
			{
				FUnit& Who = *Change.first;
				const int Difference = Change.second - Who.Hp;
				Who.Hp = Change.second;
				if (Difference != 0)
				{
					FEvent Event;
					Event.Kind = EEventKind::Hit;
					Event.Unit = Who.Id;
					Event.Amount = std::abs(Difference);
					Event.Where = Who.Pos;
					Event.Id = Difference > 0 ? "spirit_up" : "spirit_down";
					Report.Events.push_back(Event);
				}
			}
		}
		else if (Special == "rally")
		{
			if (Struck.Id == User.Id || Struck.Team != User.Team || IsBoss(Struck))
			{
				return;
			}
			FVec2 Spot;
			if (SpotNear(User.Pos, User.Pos + Toward(User.Pos, Struck.Pos) * 1.0f, 1.6f, 0.6f, Struck.Id, Spot))
			{
				SpellMoved(Struck, Spot, "rally", Report);
			}
		}
		else if (Special == "hook")
		{
			if (Struck.Id == User.Id || IsBoss(Struck))
			{
				return;
			}
			FVec2 Spot;
			if (SpotNear(User.Pos, User.Pos + Toward(User.Pos, Struck.Pos) * 1.0f, 1.6f, 0.6f, Struck.Id, Spot))
			{
				SpellMoved(Struck, Spot, "hook", Report);
			}
		}
		else if (Special == "shove")
		{
			// Two tiles straight back; stopped short by rock, a cliff or a unit, it is stunned.
			if (Struck.Id == User.Id || IsBoss(Struck))
			{
				return;
			}
			const FVec2 Start = Struck.Pos;
			const FVec2 End = SlideEnd(Struck, Toward(User.Pos, Struck.Pos), ShoveDistance, false);
			if (End != Start)
			{
				SpellMoved(Struck, End, "shove", Report);
			}
			if (End.DistanceTo(Start) < ShoveDistance - 0.6f)
			{
				AddStatus(Struck, "stun", 1);
				if (Struck.HasStatus("stun"))
				{
					Says(Struck, "stun");
					StunInterrupt(Struck, Report);
				}
			}
		}
	}

	// ------------------------------------------- once everything is hit

	void FBattle::AfterSpell(FUnit& User, int Slot, const FAbility& Ability, const FVec2& Target, const FSpellAfter& After, FTickReport& Report)
	{
		(void)Target;
		// Shield Toss (2026-10-06, Cire's Spell Codex): on from the last struck to the
		// nearest enemy within 5 m not yet struck, twice, a fifth weaker each time;
		// no dice (as Chain Mend's leaps), and each takes the status.
		if (Ability.Special == "ricochet" && Ability.Effect == EEffect::Damage && !After.Struck.empty() && User.IsAlive())
		{
			std::vector<int> Done = After.Struck;
			const FUnit* First = FindUnit(Done.back());
			FVec2 From = First ? First->Pos : Target;
			int Previous = Done.back();
			double Share = 1.0;
			for (int Bounce = 0; Bounce < RicochetBounces && User.IsAlive(); ++Bounce)
			{
				FUnit* Next = nullptr;
				for (FUnit& Other : Units)
				{
					if (Other.IsAlive() && !Other.bOffBoard && Other.Team != User.Team
						&& std::find(Done.begin(), Done.end(), Other.Id) == Done.end()
						&& Other.Pos.DistanceTo(From) <= RicochetReach
						&& (!Next || Other.Pos.DistanceTo(From) < Next->Pos.DistanceTo(From)))
					{
						Next = &Other;
					}
				}
				if (!Next)
				{
					break;
				}
				Share *= RicochetFalloff;
				Done.push_back(Next->Id);
				FEvent Leap;
				Leap.Kind = EEventKind::Reaction;
				Leap.Unit = Next->Id;
				Leap.By = Previous;
				Leap.Where = From;
				Leap.Id = "ricochet";
				Report.Events.push_back(Leap);
				int Amount = RoundToInt(CalcAmount(User, Ability, From, *Next, Next->Pos, LevelAt(From), LevelAt(Next->Pos)) * Share);
				Amount = TakeFromShield(*Next, std::max(1, Amount), Report);
				Amount = Hurt(*Next, Amount);
				if (Next->bMonster && Amount > 0)
				{
					BossHurtBy(*Next, User, Amount);
				}
				FEvent Event;
				Event.Kind = EEventKind::Hit;
				Event.Unit = Next->Id;
				Event.By = User.Id;
				Event.Slot = Slot;
				Event.Amount = Amount;
				Event.Where = Next->Pos;
				Event.Id = Ability.Id;
				Report.Events.push_back(Event);
				From = Next->Pos;
				Previous = Next->Id;
				if (!Next->IsAlive())
				{
					KnockOut(*Next, Report);
					continue;
				}
				if (Ability.HasStatus())
				{
					const FStatusDef* Def = FindStatus(Ability.StatusId);
					AddStatus(*Next, Ability.StatusId, Ability.StatusTurns, 0, Def && (Def->bTaunt || Def->bSourced) ? User.Id : -1);
					if (Next->HasStatus(Ability.StatusId))
					{
						FEvent Given;
						Given.Kind = EEventKind::StatusApplied;
						Given.Unit = Next->Id;
						Given.By = User.Id;
						Given.Where = Next->Pos;
						Given.Id = Ability.StatusId;
						Report.Events.push_back(Given);
					}
				}
			}
		}
		// Soul Link: half of what each linked unit took, to its partner.
		for (const std::pair<int, int>& Share : After.Shared)
		{
			FUnit* Partner = FindUnit(Share.first);
			if (!Partner || !Partner->IsAlive())
			{
				continue;
			}
			const int Taken = Hurt(*Partner, Share.second);
			if (Taken <= 0)
			{
				continue;
			}
			FEvent Event;
			Event.Kind = EEventKind::Hit;
			Event.Unit = Partner->Id;
			Event.By = User.Id;
			Event.Amount = Taken;
			Event.Where = Partner->Pos;
			Event.Id = "link";
			Report.Events.push_back(Event);
			if (!Partner->IsAlive())
			{
				KnockOut(*Partner, Report);
			}
		}
		// Chain Mend: on from the last healed to the nearest ally not yet healed, a quarter less each time.
		if (Ability.Special == "chain" && Ability.Effect == EEffect::Heal && !After.Healed.empty() && User.IsAlive())
		{
			std::vector<int> Done = After.Healed;
			double Share = 1.0;
			for (int Leap = 0; Leap < ChainLeaps; ++Leap)
			{
				const FUnit* Last = FindUnit(Done.back());
				if (!Last)
				{
					break;
				}
				FUnit* Next = nullptr;
				for (FUnit& Other : Units)
				{
					if (Other.IsAlive() && !Other.bOffBoard && Other.Team == User.Team
						&& std::find(Done.begin(), Done.end(), Other.Id) == Done.end()
						&& Other.Pos.DistanceTo(Last->Pos) <= ChainReach
						&& (!Next || Other.Pos.DistanceTo(Last->Pos) < Next->Pos.DistanceTo(Last->Pos)))
					{
						Next = &Other;
					}
				}
				if (!Next)
				{
					break;
				}
				Share *= ChainFalloff;
				Done.push_back(Next->Id);
				const int Amount = RoundToInt(CalcAmount(User, Ability, User.Pos, *Next, Next->Pos, LevelAt(User.Pos), LevelAt(Next->Pos)) * Share);
				if (Amount <= 0)
				{
					continue;
				}
				FEvent Event;
				Event.Kind = EEventKind::Hit;
				Event.Unit = Next->Id;
				Event.By = User.Id;
				Event.Slot = Slot;
				Event.Where = Next->Pos;
				if (Next->HasStatus("decay"))
				{
					Event.Amount = Hurt(*Next, Amount);
					Event.Id = "decay";
					Report.Events.push_back(Event);
					if (!Next->IsAlive())
					{
						KnockOut(*Next, Report);
					}
					continue;
				}
				Next->Hp += Amount;
				Event.Amount = Amount;
				Event.Id = Ability.Id;
				Report.Events.push_back(Event);
			}
		}
		// Riptide: places traded with the furthest of the enemies the line struck.
		if (Ability.Special == "riptide" && User.IsAlive())
		{
			FUnit* Furthest = nullptr;
			for (const int Id : After.Struck)
			{
				FUnit* Each = FindUnit(Id);
				if (Each && Each->IsAlive() && Each->Id != User.Id && Each->Team != User.Team && !IsBoss(*Each)
					&& (!Furthest || Each->Pos.DistanceTo(User.Pos) > Furthest->Pos.DistanceTo(User.Pos)))
				{
					Furthest = Each;
				}
			}
			if (Furthest)
			{
				const FVec2 Mine = User.Pos;
				SpellMoved(User, Furthest->Pos, "riptide", Report);
				SpellMoved(*Furthest, Mine, "riptide", Report);
			}
		}
		// Retribution: whoever struck the warded ally is stunned.
		if (After.bRetribution && User.IsAlive())
		{
			AddStatus(User, "stun", 1);
			if (User.HasStatus("stun"))
			{
				FEvent Event;
				Event.Kind = EEventKind::Reaction;
				Event.Unit = User.Id;
				Event.Where = User.Pos;
				Event.Id = "retribution";
				Report.Events.push_back(Event);
				StunInterrupt(User, Report);
			}
		}
	}

	// ------------------------------------------- statuses with rules

	void FBattle::SpreadPlague(FUnit& Unit, FTickReport& Report)
	{
		int Turns = 0;
		int Jumps = 0;
		int By = -1;
		for (const FStatus& Status : Unit.Statuses)
		{
			if (Status.Id == "plague")
			{
				Turns = Status.Turns;
				Jumps = Status.Amount;
				By = Status.By;
			}
		}
		if (Turns <= 0 || Jumps >= PlagueJumps)
		{
			return;
		}
		for (FUnit& Other : Units)
		{
			if (Other.IsAlive() && !Other.bOffBoard && Other.Id != Unit.Id && Other.Team == Unit.Team
				&& !Other.HasStatus("plague") && Other.Pos.DistanceTo(Unit.Pos) <= PlagueReach)
			{
				AddStatus(Other, "plague", Turns, Jumps + 1, By);
				if (Other.HasStatus("plague"))
				{
					FEvent Event;
					Event.Kind = EEventKind::Reaction;
					Event.Unit = Other.Id;
					Event.By = Unit.Id;
					Event.Where = Other.Pos;
					Event.Id = "spread";
					Report.Events.push_back(Event);
				}
			}
		}
	}

	void FBattle::Detonate(FUnit& Carrier, int Amount, int By, FTickReport& Report)
	{
		const FVec2 Centre = Carrier.Pos;
		const int Blast = Amount > 0 ? Amount : BombDefault;
		FEvent Boom;
		Boom.Kind = EEventKind::Reaction;
		Boom.Unit = Carrier.Id;
		Boom.By = By;
		Boom.Where = Centre;
		Boom.Id = "bomb";
		Report.Events.push_back(Boom);
		for (FUnit& Each : Units)
		{
			if (!Each.IsAlive() || Each.bOffBoard || Each.Pos.DistanceTo(Centre) > BombReach)
			{
				continue;
			}
			const int Taken = Hurt(Each, Blast);
			if (Taken <= 0)
			{
				continue;
			}
			FEvent Event;
			Event.Kind = EEventKind::Hit;
			Event.Unit = Each.Id;
			Event.By = By;
			Event.Amount = Taken;
			Event.Where = Each.Pos;
			Event.Id = "bomb";
			Report.Events.push_back(Event);
			if (!Each.IsAlive())
			{
				KnockOut(Each, Report);
			}
		}
		CheckWinner();
	}
}
