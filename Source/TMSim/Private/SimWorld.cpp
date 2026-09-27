// The ground as the rules ask about it: how high it is, what is on it, what can
// be seen across it, and how far it is to walk somewhere.
//
// Also the one door every order comes through. Ported from game_state.gd.

#include "SimAbility.h"
#include "SimBattle.h"

#include <cmath>
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
		const float Distance = A.DistanceTo(B);
		if (Distance <= LosStep * 2.0f)
		{
			return true;
		}
		// A line from eye height over there to chest height over here: a hill in
		// between blocks the view only if it rises above that line.
		const float FromH = static_cast<float>(GroundHeight(A)) + EyeHeight;
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
		return false;
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
		RunDijkstra({ GoalNode }, Infinity, -1, Ground::Jump);
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
			}
			for (int Slot = 0; Slot < 4; ++Slot)
			{
				Mix(static_cast<uint64_t>(Unit.Cooldowns[Slot]));
			}
		}
		return Hash;
	}

	std::string FBattle::AbilityBlockedReason(const FUnit& Unit, int Slot) const
	{
		const FAbility* Ability = JobAbility(Unit.Job, Slot);
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
			// Whether it was already switched this turn is not kept yet, so a
			// toggle counts as available. No ported ability is one.
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
			return std::string();
		}

		FUnit* Unit = FindUnit(Order.UnitId);
		if (!Unit || !Unit->IsAlive())
		{
			return "No such unit.";
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
			return ValidateMove(Order.UnitId, Order.To, Order.bSprint);
		case EOrderType::EndTurn:
			return std::string();
		case EOrderType::UseAbility:
			// Abilities are not wired to orders yet: the damage they do is
			// ported and tested, what is missing is casting and cooldowns.
			return "Abilities cannot be ordered yet.";
		default:
			return "Unknown order.";
		}
	}

	bool FBattle::Apply(const FOrder& Order, FTickReport& Report)
	{
		switch (Order.Type)
		{
		case EOrderType::Advance:
			Advance(Order.Ticks, Report);
			return true;

		case EOrderType::Move:
			return ApplyMove(Order.UnitId, Order.To, Order.bSprint, Report);

		case EOrderType::EndTurn:
			if (FUnit* Unit = FindUnit(Order.UnitId))
			{
				EndTurnFor(*Unit, false, Report);
				return true;
			}
			return false;

		default:
			return false;
		}
	}
}
