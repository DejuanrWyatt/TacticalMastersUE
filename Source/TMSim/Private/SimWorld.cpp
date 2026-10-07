// The ground as the rules ask about it: how high it is, what is on it, what can
// be seen across it, and how far it is to walk somewhere.
//
// Also the one door every order comes through. Ported from game_state.gd.

#include "SimAbility.h"
#include "SimBattle.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace TMSim
{
	namespace
	{
		/** How far apart the line of sight is sampled, in metres. */
		constexpr float LosStep = 0.5f;
		/** Eyes are this far above the ground a unit stands on. */
		constexpr float EyeHeight = 1.2f;
		/** What is being looked at is this far above its own ground. */
		constexpr float TargetHeight = 1.0f;
		constexpr double Infinity = std::numeric_limits<double>::infinity();

		float Lerp(float A, float B, float T) { return A + (B - A) * T; }
	}

	bool FBattle::CanPlace(const FUnit& Unit, const FVec2& Point) const
	{
		if (!IsPlanning() || !(Point == FMap::Snap(Point)) || !InBounds(Point))
		{
			return false;
		}
		// Godot measures in float and compares against the radius as a double.
		if (static_cast<double>(Point.DistanceTo(SpawnPoints[Unit.Team])) > PlanningRadius)
		{
			return false;
		}
		if (!Map.NodeWalkable(FMap::NodeOf(Point)))
		{
			return false;
		}
		for (const FUnit& Other : Units)
		{
			if (&Other != &Unit && Other.IsAlive() && Other.Pos.DistanceTo(Point) < Ground::UnitSpacing)
			{
				return false;
			}
		}
		return true;
	}

	std::vector<FNode> FBattle::PlaceableNodes(const FUnit& Unit) const
	{
		std::vector<FNode> Out;
		if (!IsPlanning())
		{
			return Out;
		}
		const int Reach = static_cast<int>(std::ceil(PlanningRadius / Ground::NavStep));
		const FNode Middle = FMap::NodeOf(SpawnPoints[Unit.Team]);
		for (int Dy = -Reach; Dy <= Reach; ++Dy)
		{
			for (int Dx = -Reach; Dx <= Reach; ++Dx)
			{
				FNode Node;
				Node.X = Middle.X + Dx;
				Node.Y = Middle.Y + Dy;
				// GDScript's % keeps the sign, as C++'s does.
				if (Node.X % 2 == 0 && Node.Y % 2 == 0 && CanPlace(Unit, FMap::NodePos(Node)))
				{
					Out.push_back(Node);
				}
			}
		}
		return Out;
	}

	bool FBattle::InBounds(const FVec2& Point) const
	{
		const FVec2 Size = Map.SizeMeters();
		return Point.X >= 0.0f && Point.Y >= 0.0f && Point.X < Size.X && Point.Y < Size.Y;
	}

	int FBattle::LevelAt(const FVec2& Point) const
	{
		if (!InBounds(Point))
		{
			return 0;
		}
		return Map.TileLevel(
			static_cast<int>(std::floor(Point.X / Ground::TileSize)),
			static_cast<int>(std::floor(Point.Y / Ground::TileSize)));
	}

	double FBattle::GroundHeight(const FVec2& Point) const
	{
		return LevelAt(Point) * Ground::LevelHeight;
	}

	bool FBattle::IsCover(const FVec2& Point) const
	{
		if (!InBounds(Point))
		{
			return false;
		}
		const int TileX = static_cast<int>(std::floor(Point.X / Ground::TileSize));
		const int TileY = static_cast<int>(std::floor(Point.Y / Ground::TileSize));
		return Map.Covers[TileY * Map.TilesX + TileX] == 1;
	}

	int FBattle::HazardAt(const FVec2& Point) const
	{
		if (!InBounds(Point))
		{
			return 0;
		}
		const int TileX = static_cast<int>(std::floor(Point.X / Ground::TileSize));
		const int TileY = static_cast<int>(std::floor(Point.Y / Ground::TileSize));
		return Map.Hazards[TileY * Map.TilesX + TileX];
	}

	double FBattle::SightOf(const FUnit& Unit) const
	{
		return Unit.Stat(EStat::Sight) * Tuning.SightMultiplier;
	}

	bool FBattle::HasLineOfSight(const FVec2& A, const FVec2& B) const
	{
		return HasLineOfSightFrom(A, EyeHeight, B);
	}

	bool FBattle::HasLineOfSightFrom(const FVec2& A, float EyeAboveGround, const FVec2& B) const
	{
		const float Distance = A.DistanceTo(B);
		if (Distance <= LosStep * 2.0f)
		{
			return true;
		}
		// A line from eye height over there to chest height over here: a hill in
		// between blocks the view only if it rises above that line. A unit's eyes
		// are EyeHeight up; a watchtower looks out from much higher, which is the
		// point of holding one. The sum is the same float sum either way, so a
		// unit's sight is exactly what it was before towers existed.
		const float FromH = static_cast<float>(GroundHeight(A)) + EyeAboveGround;
		const float ToH = static_cast<float>(GroundHeight(B)) + TargetHeight;
		const int Steps = static_cast<int>(Distance / LosStep);
		for (int i = 1; i < Steps; ++i)
		{
			const float T = static_cast<float>(i) / Steps;
			const FVec2 P(Lerp(A.X, B.X, T), Lerp(A.Y, B.Y, T));
			if (IsCover(P))
			{
				return false;
			}
			if (GroundHeight(P) > Lerp(FromH, ToH, T))
			{
				return false;
			}
		}
		return true;
	}

	bool FBattle::CanSee(int Team, const FVec2& Point, int ExcludeId) const
	{
		for (const FUnit& Unit : Units)
		{
			if (Unit.IsAlive() && Unit.Team == Team && Unit.Id != ExcludeId
				&& Unit.Pos.DistanceTo(Point) <= SightOf(Unit)
				&& HasLineOfSight(Unit.Pos, Point))
			{
				return true;
			}
		}
		// A watchtower this side holds sees for it too. None exist unless the
		// battle asked for them, so without them this is Godot's sight exactly.
		for (const FWatchtower& Tower : Watchtowers)
		{
			if (Tower.Owner == Team && TowerSees(Tower, Point))
			{
				return true;
			}
		}
		// And a zone that sees (2026-10-04): a flare, a ward, a lantern.
		return !Zones.empty() && ZoneSees(Team, Point);
	}

	bool FBattle::TowerSees(const FWatchtower& Tower, const FVec2& Point) const
	{
		// Everything within its sight, nothing in the way: from the top of the
		// tower nothing on the ground blocks the view (the human's ask, 2026-09-30;
		// online protocol 7). It used to be line of sight from its eye height.
		return static_cast<double>(Tower.Pos.DistanceTo(Point)) <= Tuning.WatchtowerSight;
	}

	int FBattle::CaptureTurnsNeeded() const
	{
		return std::max(1, RoundToInt(Tuning.WatchtowerTurns));
	}

	int FBattle::TowerNear(const FVec2& Point) const
	{
		int Best = -1;
		double BestDistance = Watchtower::Reach;
		for (int i = 0; i < static_cast<int>(Watchtowers.size()); ++i)
		{
			const double Distance = Watchtowers[static_cast<size_t>(i)].Pos.DistanceTo(Point);
			// Strictly nearer, so of two at the same distance the first listed wins.
			if (Distance <= BestDistance && (Best == -1 || Distance < BestDistance))
			{
				Best = i;
				BestDistance = Distance;
			}
		}
		return Best;
	}

	std::string FBattle::ValidateCapture(int UnitId, int Tower) const
	{
		// Checked in full, like ValidateAbility: the HUD asks it before offering
		// the button, and Validate asks it about an order off the network.
		const FUnit* Unit = nullptr;
		for (const FUnit& Each : Units)
		{
			Unit = Each.Id == UnitId ? &Each : Unit;
		}
		if (!Unit || !Unit->IsAlive())
		{
			return "No such unit.";
		}
		if (Tower < 0 || Tower >= static_cast<int>(Watchtowers.size()))
		{
			return "No such watchtower.";
		}
		if (!Unit->bReady)
		{
			return "It isn't ready.";
		}
		if (Unit->IsStunned())
		{
			return "It can't act.";
		}
		// Capturing is the turn's action and then the end of the turn, so a unit
		// that has already acted has nothing left to spend on it. It may walk
		// there first: walk, then capture, is one turn.
		if (Unit->bActed || Unit->IsCasting())
		{
			return "Already used its action this turn.";
		}
		const FWatchtower& Held = Watchtowers[static_cast<size_t>(Tower)];
		if (Held.Owner == Unit->Team)
		{
			return "Your side already holds this watchtower.";
		}
		// Next to it, and on ground a unit could step up or down to from the
		// tower's own: not at the foot of a cliff it stands on top of.
		if (static_cast<double>(Unit->Pos.DistanceTo(Held.Pos)) > Watchtower::Reach
			|| std::abs(LevelAt(Unit->Pos) - LevelAt(Held.Pos)) > Ground::Jump)
		{
			return "Too far from the watchtower: stand next to it.";
		}
		// Contested: nobody takes a tower out from under an enemy standing at it.
		for (const FUnit& Other : Units)
		{
			if (Other.IsAlive() && Other.Team != Unit->Team
				&& static_cast<double>(Other.Pos.DistanceTo(Held.Pos)) <= Watchtower::Reach)
			{
				return "An enemy is standing at the watchtower.";
			}
		}
		return std::string();
	}

	const FUnit* FBattle::UnitNear(const FVec2& Point, float Radius) const
	{
		const FUnit* Best = nullptr;
		for (const FUnit& Unit : Units)
		{
			if (!Unit.IsAlive() || Unit.Pos.DistanceTo(Point) > Radius)
			{
				continue;
			}
			if (Best == nullptr || Unit.Pos.DistanceTo(Point) < Best->Pos.DistanceTo(Point))
			{
				Best = &Unit;
			}
		}
		return Best;
	}

	std::vector<const FUnit*> FBattle::TeamUnits(int Team) const
	{
		std::vector<const FUnit*> Out;
		for (const FUnit& Unit : Units)
		{
			if (Unit.IsAlive() && Unit.Team == Team)
			{
				Out.push_back(&Unit);
			}
		}
		return Out;
	}

	const std::vector<double>& FBattle::DistanceFrom(const FVec2& Goal)
	{
		// Terrain never changes, so this is worth keeping: the computer player
		// asks for it constantly while deciding where to stand.
		const FNode GoalNode = FMap::NodeOf(Goal);
		const long long Key = static_cast<long long>(GoalNode.Y) * 100000 + GoalNode.X;
		const auto Found = DistanceCache.find(Key);
		if (Found != DistanceCache.end())
		{
			return Found->second;
		}

		// Team -1 means units are ignored, which is what makes it cacheable.
		// The ground's own distances, walked node to node whatever the movement
		// rule (v23 play test, 2026-10-05): with tile movement on, a tile-stepping
		// field reaches only tiles' spots, so every other node read as unreachable
		// and the watchtowers and camps (placed by this) found nowhere to stand.
		const double Tiles = Tuning.TileMove;
		Tuning.TileMove = 0.0;
		RunDijkstra({ GoalNode }, Infinity, -1, Ground::Jump);
		Tuning.TileMove = Tiles;
		if (DistanceCache.size() >= 256)
		{
			DistanceCache.clear();
		}
		return DistanceCache.emplace(Key, Cost).first->second;
	}

	double FBattle::DistanceToNearest(const std::vector<FVec2>& Goals, const FNode& Node)
	{
		double Best = Infinity;
		const int Index = Map.NodeIndex(Node);
		for (const FVec2& Goal : Goals)
		{
			const std::vector<double>& Field = DistanceFrom(Goal);
			if (Index >= 0 && Index < static_cast<int>(Field.size()))
			{
				Best = std::min(Best, Field[Index]);
			}
		}
		return Best;
	}

	uint64_t FBattle::Checksum() const
	{
		// FNV-1a, written out rather than pulled in, because the exact arithmetic
		// is part of what the two machines are agreeing on.
		uint64_t Hash = 14695981039346656037ull;
		auto Mix = [&Hash](uint64_t Value)
		{
			for (int Byte = 0; Byte < 8; ++Byte)
			{
				Hash ^= (Value >> (Byte * 8)) & 0xff;
				Hash *= 1099511628211ull;
			}
		};
		// A position is hashed by its bits, not rounded first: two machines that
		// disagree in the last bit of a coordinate have already gone wrong, and
		// the point of this is to say so at once rather than in ten seconds.
		auto MixFloat = [&Mix](float Value)
		{
			uint32_t Bits = 0;
			std::memcpy(&Bits, &Value, sizeof(Bits));
			Mix(Bits);
		};

		Mix(static_cast<uint64_t>(TickCount));
		Mix(static_cast<uint64_t>(Winner + 1));
		Mix(static_cast<uint64_t>(PlanningTicks));
		Mix(static_cast<uint64_t>(PlanningDone[0] ? 1 : 0));
		// The rule numbers, since Developer Tools can change them mid-battle by
		// order: two machines on different numbers would drift apart unseen.
		for (const FTuningKey& Key : TuningKeys())
		{
			uint64_t Bits = 0;
			const double Value = Tuning.*Key.Member;
			std::memcpy(&Bits, &Value, sizeof(Bits));
			Mix(Bits);
		}
		Mix(static_cast<uint64_t>(PlanningDone[1] ? 1 : 0));
		Mix(static_cast<uint64_t>(CaptureTicks[0]));
		Mix(static_cast<uint64_t>(CaptureTicks[1]));
		// Where the watchtowers stand, who holds each and who is part-way to
		// taking it. Two machines that disagree about one would see different
		// ground through the fog from then on.
		Mix(static_cast<uint64_t>(Watchtowers.size()));
		for (const FWatchtower& Tower : Watchtowers)
		{
			MixFloat(Tower.Pos.X);
			MixFloat(Tower.Pos.Y);
			Mix(static_cast<uint64_t>(Tower.Owner + 1));
			Mix(static_cast<uint64_t>(Tower.Capturer + 1));
			Mix(static_cast<uint64_t>(Tower.Progress));
		}
		// Dry springs: which, for whom, how long.
		for (const FSpringRest& Rest : SpringRests)
		{
			Mix(static_cast<uint64_t>(Rest.Tile));
			Mix(static_cast<uint64_t>(Rest.UnitId + 1));
			Mix(static_cast<uint64_t>(Rest.Turns));
		}
		// Ground zones (2026-10-04), only when there are any, so a battle without
		// them sums as it always did.
		for (const FZone& Zone : Zones)
		{
			Mix(0x5A);
			Mix(static_cast<uint64_t>(Zone.Owner + 1));
			Mix(static_cast<uint64_t>(Zone.Team + 1));
			Mix(static_cast<uint64_t>(Zone.Slot + 1));
			for (const char Letter : Zone.AbilityId)
			{
				Mix(static_cast<uint64_t>(static_cast<unsigned char>(Letter)));
			}
			MixFloat(Zone.From.X);
			MixFloat(Zone.From.Y);
			MixFloat(Zone.Target.X);
			MixFloat(Zone.Target.Y);
			Mix(static_cast<uint64_t>(Zone.Turns));
			Mix(static_cast<uint64_t>(Zone.bIgnited ? 1 : 0));
			for (const std::pair<int, int>& Touch : Zone.Touched)
			{
				Mix(static_cast<uint64_t>(Touch.first + 1));
				Mix(static_cast<uint64_t>(Touch.second));
			}
			for (const int Once : Zone.Once)
			{
				Mix(static_cast<uint64_t>(Once + 1));
			}
		}
		// The dice themselves. Two machines that have drawn a different number of
		// times still agree about the board for a while, and then disagree about
		// the very next thing anyone rolls for -- so the generator's own position
		// has to be part of what they compare, not merely its consequences.
		Mix(Rng.GetState());

		for (const FUnit& Unit : Units)
		{
			Mix(static_cast<uint64_t>(Unit.Id));
			MixFloat(Unit.Pos.X);
			MixFloat(Unit.Pos.Y);
			MixFloat(Unit.Facing.X);
			MixFloat(Unit.Facing.Y);
			Mix(static_cast<uint64_t>(Unit.Hp));
			Mix(static_cast<uint64_t>(Unit.Tg));
			Mix(static_cast<uint64_t>(Unit.Serial));
			Mix(static_cast<uint64_t>(Unit.Ult));
			// What it did with the turn, which decides when its next one comes.
			Mix(static_cast<uint64_t>(Unit.bReady ? 1 : 0));
			Mix(static_cast<uint64_t>(Unit.bMoved ? 2 : 0));
			Mix(static_cast<uint64_t>(Unit.bActed ? 4 : 0));
			Mix(static_cast<uint64_t>(Unit.Clock));
			// How long a fallen unit can still be raised, whether it is owed a
			// faster gauge for holding its action back, and how long it has gone
			// unhurt. All three decide something later, so all three must agree.
			Mix(static_cast<uint64_t>(Unit.KoTicks));
			Mix(static_cast<uint64_t>(Unit.bHustling ? 1 : 0));
			Mix(static_cast<uint64_t>(Unit.UnharmedTurns));
			Mix(static_cast<uint64_t>(Unit.bSpotted ? 1 : 0));

			for (const FStatus& Status : Unit.Statuses)
			{
				for (const char Letter : Status.Id)
				{
					Mix(static_cast<uint64_t>(static_cast<unsigned char>(Letter)));
				}
				Mix(static_cast<uint64_t>(Status.Turns));
				Mix(static_cast<uint64_t>(Status.Amount));
				Mix(static_cast<uint64_t>(Status.By + 1));
			}
			for (const FBuff& Buff : Unit.Buffs)
			{
				Mix(static_cast<uint64_t>(Buff.Stat));
				Mix(static_cast<uint64_t>(Buff.Amount));
				Mix(static_cast<uint64_t>(Buff.Turns));
				for (const char Letter : Buff.Aura)
				{
					Mix(static_cast<uint64_t>(static_cast<unsigned char>(Letter)));
				}
				Mix(0xA0);
			}
			for (int Slot = 0; Slot < AbilitySlots; ++Slot)
			{
				Mix(static_cast<uint64_t>(Unit.Cooldowns[Slot]));
				Mix(static_cast<uint64_t>(Unit.Toggled[Slot] ? 1 : 0));
				Mix(static_cast<uint64_t>(Unit.ToggledTurn[Slot] ? 1 : 0));
			}

			// A spell part-way out. Two machines that disagree about what is in
			// flight, or about how much longer it has, will agree about
			// everything else right up until it lands.
			Mix(static_cast<uint64_t>(Unit.Casting.Slot + 1));
			MixFloat(Unit.Casting.Target.X);
			MixFloat(Unit.Casting.Target.Y);
			Mix(static_cast<uint64_t>(Unit.Casting.FollowId + 1));
			Mix(static_cast<uint64_t>(Unit.Casting.Ticks));
			Mix(static_cast<uint64_t>(Unit.Casting.Total));

			Mix(static_cast<uint64_t>(Unit.Channeling.Slot + 1));
			MixFloat(Unit.Channeling.Target.X);
			MixFloat(Unit.Channeling.Target.Y);
			Mix(static_cast<uint64_t>(Unit.Channeling.Turns));

			// What it carries, by id: the items' numbers come from files every
			// machine loads, as classes' do.
			for (const FItemDef* Item : Unit.Gear)
			{
				Mix(0x17u);
				if (Item)
				{
					for (const char C : Item->Id)
					{
						Mix(static_cast<uint64_t>(static_cast<unsigned char>(C)));
					}
				}
			}
			// The items' own memory, and a monster's mind. All at rest outside camps.
			Mix(static_cast<uint64_t>(Unit.LastHurt));
			Mix(static_cast<uint64_t>((Unit.bRewindUsed ? 1 : 0) | (Unit.bFirstStrikeUsed ? 2 : 0) | (Unit.bPhoenixUsed ? 4 : 0)
				| (Unit.bStillLastTurn ? 8 : 0) | (Unit.bUnseenAtStart ? 16 : 0) | (Unit.bMonster ? 32 : 0) | (Unit.bOffBoard ? 64 : 0)));
			Mix(static_cast<uint64_t>(Unit.KillTgPercent));
			Mix(static_cast<uint64_t>(Unit.Team));
			Mix(static_cast<uint64_t>(Unit.CharmedFrom + 1));
			Mix(static_cast<uint64_t>(Unit.ReraiseTicks));
			Mix(static_cast<uint64_t>(Unit.PetOf + 1));
			Mix(static_cast<uint64_t>(Unit.PetTurns));
			if (Unit.bMonster)
			{
				Mix(static_cast<uint64_t>(Unit.Camp + 1));
				Mix(static_cast<uint64_t>(Unit.Mind));
				MixFloat(Unit.Home.X);
				MixFloat(Unit.Home.Y);
				Mix(static_cast<uint64_t>(Unit.Grudge + 1));
				Mix(static_cast<uint64_t>(Unit.GrudgeTurns));
				Mix(static_cast<uint64_t>(Unit.FleeTurns));
				Mix(static_cast<uint64_t>(Unit.RouteStep));
				Mix(static_cast<uint64_t>(Unit.Phase));
				Mix(static_cast<uint64_t>(Unit.Stagger));
				Mix(static_cast<uint64_t>(Unit.TamedTurns));
				for (const std::pair<int, int>& Entry : Unit.Wrath)
				{
					Mix(static_cast<uint64_t>(Entry.first + 1));
					Mix(static_cast<uint64_t>(Entry.second));
				}
				Mix(static_cast<uint64_t>(Unit.HuntTarget + 1));
				Mix(static_cast<uint64_t>(Unit.HuntLost));
				Mix(static_cast<uint64_t>(Unit.Claim[0]));
				Mix(static_cast<uint64_t>(Unit.Claim[1]));
			}
		}
		// The camps, the items on the ground, and the two generators that place
		// and fill them.
		Mix(static_cast<uint64_t>(Camps.size()));
		for (const FCamp& Held : Camps)
		{
			Mix(static_cast<uint64_t>(Held.Kind + 1));
			MixFloat(Held.Spot.X);
			MixFloat(Held.Spot.Y);
			Mix(static_cast<uint64_t>(Held.State));
			Mix(static_cast<uint64_t>(Held.Timer));
			Mix(static_cast<uint64_t>(Held.ShrineRest));
			Mix(static_cast<uint64_t>(Held.Wakes));
			Mix(static_cast<uint64_t>(Held.Route.size()));
			Mix(static_cast<uint64_t>(Held.Noise));
			Mix(static_cast<uint64_t>(Held.NoiseQuiet));
			Mix(static_cast<uint64_t>(Held.NoiseBy[0]));
			Mix(static_cast<uint64_t>(Held.NoiseBy[1]));
			Mix(static_cast<uint64_t>(Held.LoudUnit[0] + 1));
			Mix(static_cast<uint64_t>(Held.LoudUnit[1] + 1));
			Mix(static_cast<uint64_t>(Held.LastLoudSide));
			Mix(static_cast<uint64_t>(Held.bNoiseWake ? 1 : 0));
		}
		for (int Team = 0; Team < 2; ++Team)
		{
			Mix(static_cast<uint64_t>(Stash[Team].size()));
			for (const FStashed& Held : Stash[Team])
			{
				Mix(0x53u);
				for (const char C : Held.Item ? Held.Item->Id : std::string())
				{
					Mix(static_cast<uint64_t>(static_cast<unsigned char>(C)));
				}
				Mix(static_cast<uint64_t>(Held.Cooldown));
			}
		}
		Mix(static_cast<uint64_t>(Caches.size()));
		for (const FCache& Cache : Caches)
		{
			MixFloat(Cache.Pos.X);
			MixFloat(Cache.Pos.Y);
			for (size_t i = 0; i < Cache.Items.size(); ++i)
			{
				Mix(0x2Bu);
				for (const char C : Cache.Items[i]->Id)
				{
					Mix(static_cast<uint64_t>(static_cast<unsigned char>(C)));
				}
				Mix(static_cast<uint64_t>(i < Cache.Cooldowns.size() ? Cache.Cooldowns[i] : 0));
			}
		}
		if (!Camps.empty())
		{
			Mix(CampRng.GetState());
			Mix(LootRng.GetState());
		}
		return Hash;
	}

	std::string FBattle::LoadoutProblem() const
	{
		// Checked before a battle starts (the setup screen, and the host of an
		// online match for the joiner's side): three open slots, no item twice
		// on one unit, only items the game knows, and each side within its points.
		int Spent[2] = { 0, 0 };
		for (const FUnit& Unit : Units)
		{
			for (int i = 0; i < Items::Slots; ++i)
			{
				const FItemDef* Item = Unit.Gear[i];
				if (!Item)
				{
					continue;
				}
				if (FindItem(Item->Id) != Item)
				{
					return "An item that is not in the game.";
				}
				for (int j = 0; j < i; ++j)
				{
					if (Unit.Gear[j] == Item)
					{
						return "A unit can't carry two " + Item->Name + "s.";
					}
				}
				if (Item->Cost <= 0)
				{
					return Item->Name + " can't be bought; it has to be found.";
				}
				if (Unit.Team == 0 || Unit.Team == 1)
				{
					Spent[Unit.Team] += Item->Cost;
				}
			}
		}
		const int Budget = RoundToInt(Tuning.ItemBudget);
		for (int Team = 0; Team < 2; ++Team)
		{
			if (Spent[Team] > Budget)
			{
				return std::string(Team == 0 ? "Blue" : "Red") + "'s items cost " + std::to_string(Spent[Team])
					+ " points, and each side has " + std::to_string(Budget) + ".";
			}
		}
		return std::string();
	}

	std::string FBattle::AbilityBlockedReason(const FUnit& Unit, int Slot) const
	{
		const FAbility* Ability = Unit.Ability(Slot);
		if (!Ability)
		{
			return "There is nothing in that slot.";
		}
		if (Unit.IsSilenced())
		{
			return Unit.Job + " cannot use abilities: " + Unit.NoAbilitiesStatus() + ".";
		}
		if (Ability->Kind == "passive" || Ability->Kind == "aura")
		{
			return Ability->Name + " is always on.";
		}
		if (Ability->Kind == "toggle")
		{
			// Once a turn, so it cannot be flicked on and off for nothing; and
			// nothing else below applies to a switch (game_state.gd:787-790).
			if (Unit.ToggledTurn[Slot])
			{
				return Ability->Name + " was already switched this turn.";
			}
			return std::string();
		}
		// The fourth slot is the ultimate, and it waits for a full meter. This
		// is the one that matters most to the computer player: an ultimate it
		// cannot use yet must not count towards how far it can threaten from,
		// or it stands too far back for the whole first half of the battle.
		if (Slot == 3 && Unit.Ult < Pace::UltMax)
		{
			return "The ultimate meter isn't full yet.";
		}
		if (Unit.Cooldowns[Slot] > 0)
		{
			return Ability->Name + " is recharging.";
		}
		return std::string();
	}

	std::string FBattle::Validate(const FOrder& Order)
	{
		if (Order.Type == EOrderType::Advance)
		{
			// Time is the one thing nobody orders, but it still arrives as one so
			// that a replay is a single list. A step has to be a sensible size:
			// an enormous one would run a whole battle inside one order and give
			// the other machine nothing to compare against until it was over.
			if (Order.Ticks < 1 || Order.Ticks > Pace::MaxAdvance)
			{
				return "Bad time step.";
			}
			return std::string();
		}

		// New rule numbers: known ones, and numbers. Out of range is not refused
		// but held to the range when applied, as Godot's clean_tuning does
		// (game_state.gd:255-260, 1043-1045).
		if (Order.Type == EOrderType::Tune)
		{
			if (Winner != -1)
			{
				return "The battle is over.";
			}
			const int Count = static_cast<int>(TuningKeys().size());
			for (const std::pair<int, double>& Value : Order.TuneValues)
			{
				if (Value.first < 0 || Value.first >= Count || !std::isfinite(Value.second))
				{
					return "Bad tuning values.";
				}
			}
			return std::string();
		}

		// Done placing: a side, not a unit, and only while there is placing to be
		// done (game_state.gd:1049-1053).
		if (Order.Type == EOrderType::Ready)
		{
			if (Order.Team != 0 && Order.Team != 1)
			{
				return "Bad team.";
			}
			return IsPlanning() ? std::string() : "The battle has already started.";
		}

		FUnit* Unit = FindUnit(Order.UnitId);
		if (!Unit || !Unit->IsAlive())
		{
			return "No such unit.";
		}
		// Equipping from the stash is any time, not a turn's (2026-10-01).
		if (Order.Type == EOrderType::Equip)
		{
			if (Winner != -1)
			{
				return "The battle is over.";
			}
			return ValidateEquip(Order.UnitId, Order.ItemId, Order.GearSlot);
		}
		// Placing happens before the battle, while nobody is ready yet, so it is
		// checked before the turn an order was written for: there are no turns
		// yet (game_state.gd:1058-1065).
		if (Order.Type == EOrderType::Place)
		{
			if (!IsPlanning())
			{
				return "Units can only be placed before the battle starts.";
			}
			return CanPlace(*Unit, Order.To) ? std::string() : "That isn't in your spawn area.";
		}
		if (IsPlanning())
		{
			return "The sides are still placing their units.";
		}
		// The turn it was written for. An order held up on the network, or one
		// replayed out of order, must not land on a later turn.
		if (Order.Serial >= 0 && Order.Serial != Unit->Serial)
		{
			return "That order was for an earlier turn.";
		}
		if (!Unit->bReady)
		{
			return "It isn't ready.";
		}
		if (Unit->IsStunned())
		{
			return "It can't act.";
		}

		switch (Order.Type)
		{
		case EOrderType::Move:
			if (Order.Face < -1 || Order.Face >= FacingWays)
			{
				return "That isn't a way to face.";
			}
			return ValidateMove(Order.UnitId, Order.To, Order.bSprint, Order.Via);
		case EOrderType::EndTurn:
			return std::string();
		case EOrderType::UseAbility:
			return ValidateAbility(Order.UnitId, Order.Slot, Order.Target, Order.Follow);
		case EOrderType::Capture:
			return ValidateCapture(Order.UnitId, Order.Tower);
		case EOrderType::Take:
			return ValidateTake(Order.UnitId, Order.Cache, Order.ItemId, Order.GearSlot);
		case EOrderType::Drop:
			return ValidateDrop(Order.UnitId, Order.GearSlot);
		default:
			return "Unknown order.";
		}
	}

	std::string FBattle::ValidateAbility(int UnitId, int Slot, const FVec2& Target, int Follow)
	{
		// Checked here in full rather than trusting whoever called, because this is
		// an entry point in its own right: the director asks it before offering an
		// ability, and Validate asks it about an order off the network.
		FUnit* Unit = FindUnit(UnitId);
		if (!Unit || !Unit->IsAlive())
		{
			return "No such unit.";
		}
		if (!Unit->bReady)
		{
			return "It isn't ready.";
		}
		if (Unit->IsStunned())
		{
			return "It can't act.";
		}
		if (Unit->bActed)
		{
			return "Already used an ability this turn.";
		}
		if (Unit->ActsOnce() && Unit->bMoved)
		{
			return Unit->Job + " is knocked down: it can walk or act this turn, not both.";
		}
		if (Slot < 0 || Slot >= AbilitySlots)
		{
			return "No such ability.";
		}
		const std::string Blocked = AbilityBlockedReason(*Unit, Slot);
		if (!Blocked.empty())
		{
			return Blocked;
		}
		if (!InAbilityRange(*Unit, Slot, Unit->Pos, Target))
		{
			return "That target is out of range.";
		}
		const FAbility* Ability = Unit->Ability(Slot);
		if (!Ability)
		{
			return "No such ability.";
		}
		// A zone that sees (2026-10-04, a flare) is thrown to look where nobody can.
		const bool bLobbed = Ability->LaysZone() && Ability->ZoneSight > 0.0f;
		// Fog: a side cannot aim at ground none of it can see.
		if (!bLobbed && !CanSee(Unit->Team, Target))
		{
			return "You can't see that spot.";
		}
		if (!bLobbed && NeedsLineOfSight(*Ability) && !HasLineOfSight(Unit->Pos, Target))
		{
			return "No line of sight.";
		}
		// A leap or a step behind needs somewhere to land (2026-10-03), and so do
		// the new spells' moves (2026-10-05): a vault, a charge, a shadow hop, a dash.
		const std::string Problem = SpecialProblem(*Unit, *Ability, Unit->Pos, Target);
		if (!Problem.empty())
		{
			return Problem;
		}

		// Taunted: while whoever taunted it is within reach, an attack has to be
		// aimed somewhere that catches them.
		if (Ability->Effect == EEffect::Damage)
		{
			const FUnit* Taunter = FindUnit(Unit->TauntedBy());
			if (Taunter && Taunter->IsAlive()
				&& InAbilityRange(*Unit, Slot, Unit->Pos, Taunter->Pos)
				&& !InShape(*Ability, Unit->Pos, Target, Taunter->Pos))
			{
				return Unit->Job + " is taunted: it must attack unit "
					+ std::to_string(Taunter->Id) + ".";
			}
		}

		// Following a unit: it has to be a unit, in the state this ability wants,
		// and standing where the order says it is. Otherwise the two halves of the
		// order disagree and the cast would land somewhere nobody asked for.
		if (Follow != -1)
		{
			const FUnit* Followed = FindUnit(Follow);
			const bool bRightState = Followed
				&& (Ability->Target == ETargetSide::KoAlly ? Followed->IsKo() : Followed->IsAlive());
			if (!bRightState || Followed->Pos.DistanceTo(Target) > Ground::HitRadius
				|| (Ability->Target != ETargetSide::KoAlly && !CanSeeUnit(Unit->Team, *Followed)))
			{
				return "Bad target.";
			}
		}
		return std::string();
	}

	bool FBattle::Apply(const FOrder& Order, FTickReport& Report)
	{
		switch (Order.Type)
		{
		case EOrderType::Advance:
			Advance(Order.Ticks, Report);
			return true;

		case EOrderType::Move:
			return ApplyMove(Order.UnitId, Order.To, Order.bSprint, Report, Order.Via, Order.Face);

		case EOrderType::UseAbility:
			if (FUnit* Unit = FindUnit(Order.UnitId))
			{
				UseAbility(*Unit, Order.Slot, Order.Target, Order.Follow, Report);
				return true;
			}
			return false;

		case EOrderType::EndTurn:
			if (FUnit* Unit = FindUnit(Order.UnitId))
			{
				EndTurnFor(*Unit, false, Report);
				return true;
			}
			return false;

		case EOrderType::Place:
			if (FUnit* Unit = FindUnit(Order.UnitId))
			{
				// Put down facing the middle, as every unit opens the battle
				// (game_state.gd:1177-1180).
				Unit->Pos = Order.To;
				const FVec2 Size = Map.SizeMeters();
				Unit->Facing = (FVec2(Size.X * 0.5f, Size.Y * 0.5f) - Unit->Pos).Normalized();
				Report.Say(EEventKind::Moved, Unit->Id);
				return true;
			}
			return false;

		case EOrderType::Tune:
			for (const std::pair<int, double>& Value : Order.TuneValues)
			{
				const FTuningKey& Key = TuningKeys()[static_cast<size_t>(Value.first)];
				Tuning.*Key.Member = std::min(std::max(Value.second, Key.Low), Key.High);
			}
			return true;

		case EOrderType::Capture:
			if (FUnit* Unit = FindUnit(Order.UnitId))
			{
				ApplyCapture(*Unit, Order.Tower, Report);
				return true;
			}
			return false;

		case EOrderType::Take:
			if (FUnit* Unit = FindUnit(Order.UnitId))
			{
				ApplyTake(*Unit, Order.Cache, Order.ItemId, Order.GearSlot, Report);
				return true;
			}
			return false;

		case EOrderType::Drop:
			if (FUnit* Unit = FindUnit(Order.UnitId))
			{
				ApplyDrop(*Unit, Order.GearSlot, Report);
				return true;
			}
			return false;

		case EOrderType::Equip:
			if (FUnit* Unit = FindUnit(Order.UnitId))
			{
				ApplyEquip(*Unit, Order.ItemId, Order.GearSlot, Report);
				return true;
			}
			return false;

		case EOrderType::Ready:
			// Both sides done: the fighting starts before the time is up
			// (game_state.gd:1171-1175).
			PlanningDone[Order.Team] = true;
			if (PlanningDone[0] && PlanningDone[1])
			{
				PlanningTicks = 0;
			}
			return true;

		default:
			return false;
		}
	}
}
