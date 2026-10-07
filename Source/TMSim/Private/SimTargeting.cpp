// Who an ability reaches, and from where.
//
// Ported from game_state.gd: shape_of and in_shape (:809-845), in_ability_range
// (:798-804), needs_line_of_sight (:366-368) and preview (:847-873).
//
// Preview is the important one. It answers "what would this do" without doing
// any of it, and the original resolves an ability by walking exactly the list it
// returns rather than working the targets out a second time. That is worth
// keeping: it means the forecast a player sees, the hits that actually land, and
// the guesses the computer makes while choosing are all the same function, so
// they cannot drift apart. It is also testable on its own, which the resolution
// around it is not.

#include "SimAbility.h"
#include "SimBattle.h"

#include <algorithm>
#include <cmath>

namespace TMSim
{
	const std::vector<std::string>& AnimMotions()
	{
		static const std::vector<std::string> Motions =
		{
			"melee", "heavy", "shoot", "bolt", "area", "heal", "buff", "revive", "channel", "dash", "none"
		};
		return Motions;
	}

	std::string MotionOf(const FAbility& Ability, int Slot)
	{
		if (!Ability.Anim.empty())
		{
			return Ability.Anim;
		}
		// A switch flipped, or something always on, is no motion at all.
		if (Ability.Kind == "toggle" || Ability.Kind == "passive" || Ability.Kind == "aura")
		{
			return "none";
		}
		if (Ability.Kind == "channeled")
		{
			return "channel";
		}
		const std::string Shape = ShapeOf(Ability);
		switch (Ability.Effect)
		{
		case EEffect::Revive:
			return "revive";
		case EEffect::Heal:
			return "heal";
		case EEffect::Support:
			// On itself, or all around it: a blessing. On others: the healer's
			// gesture for a friend, the caster's bolt for a foe.
			if (Shape == "self" || Ability.MaxRange == 0.0f)
			{
				return "buff";
			}
			return Ability.Target == ETargetSide::Enemy ? "bolt" : "heal";
		case EEffect::Damage:
		default:
			if (Shape == "vector")
			{
				return "dash";
			}
			if (Ability.Scale == EScale::Att)
			{
				// Arm's length is two metres or less, as a melee reach is.
				if (Ability.MaxRange <= 2.0f)
				{
					return Slot == 3 || Ability.Aoe > 0.0f ? "heavy" : "melee";
				}
				return "shoot";
			}
			if (Ability.Aoe > 0.0f || Shape == "circle" || Shape == "cone" || Shape == "line" || Shape == "global")
			{
				return "area";
			}
			return "bolt";
		}
	}

	std::string ShapeOf(const FAbility& Ability)
	{
		// Written down wins; otherwise it follows from the ranges, which is how
		// every built-in ability gets its shape without saying so.
		if (!Ability.Shape.empty())
		{
			return Ability.Shape;
		}
		if (Ability.MaxRange == 0.0f)
		{
			return "self";
		}
		return Ability.Aoe > 0.0f ? "circle" : "unit";
	}

	bool NeedsLineOfSight(const FAbility& Ability)
	{
		// Anything reaching past arm's length has to see where it is going.
		return Ability.MaxRange > Ground::MeleeRange;
	}

	bool FBattle::InShape(const FAbility& Ability, const FVec2& From, const FVec2& Target,
		const FVec2& TargetPos) const
	{
		const std::string Shape = ShapeOf(Ability);

		if (Shape == "global")
		{
			return true;  // everyone of that side, wherever they are
		}

		if (Shape == "line" || Shape == "vector")
		{
			const FVec2 Along = Target - From;
			const float Length = Along.Length();
			if (Length < 0.01f)
			{
				// Aimed at its own feet: a circle is all a line can be.
				return TargetPos.DistanceTo(From) <= Ability.Aoe + Ground::HitRadius;
			}
			const FVec2 Dir = Along / Length;
			const float Travelled = (TargetPos - From).Dot(Dir);
			if (Travelled < -Ground::HitRadius || Travelled > Length + Ground::HitRadius)
			{
				return false;  // before the caster or past the far end
			}
			// Half a metre of width even for a line with no spread written down,
			// so a unit standing a hair off the line is still in the way.
			return std::fabs((TargetPos - From).Cross(Dir))
				<= std::max(Ability.Aoe, 0.6f) + Ground::HitRadius;
		}

		if (Shape == "cone")
		{
			const FVec2 ToTarget = Target - From;
			const FVec2 ToUnit = TargetPos - From;
			const float Reach = ToUnit.Length();
			if (Reach > Ability.MaxRange + Ground::HitRadius || ToTarget.Length() < 0.01f)
			{
				return false;
			}
			if (Reach < 0.01f)
			{
				return true;  // standing on the caster, so inside any spread
			}
			const float Spread = Ability.Angle * 0.5f;
			return std::fabs(ToUnit.AngleTo(ToTarget)) * 180.0f / 3.14159265358979323846f <= Spread;
		}

		// unit, point, circle and self all come to the same thing: near enough to
		// where it was aimed. A single-target ability has no radius at all, so it
		// catches everyone within HitRadius and the caller keeps the closest.
		return TargetPos.DistanceTo(Target) <= Ability.Aoe + Ground::HitRadius;
	}

	bool FBattle::InAbilityRange(const FUnit& Unit, int Slot, const FVec2& From,
		const FVec2& Target) const
	{
		const FAbility* Ability = Unit.Ability(Slot);
		if (!Ability)
		{
			return false;
		}
		if (Ability->MaxRange == 0.0f)
		{
			// Centred on the caster, and the aim point has to be exactly its own
			// spot -- not merely near it. Both the player's aim and the computer's
			// hand over the spot itself, so exact equality is what the original
			// means and anything looser would accept aims it refuses.
			return Target == From;
		}
		const float Distance = From.DistanceTo(Target);
		return Distance >= Ability->MinRange && Distance <= Ability->MaxRange && InBounds(Target);
	}

	std::vector<FHit> FBattle::Preview(const FUnit& Unit, int Slot, const FVec2& From,
		const FVec2& Target) const
	{
		std::vector<FHit> Out;
		const FAbility* Ability = Unit.Ability(Slot);
		if (!Ability)
		{
			return Out;
		}

		// Friendly fire: an area blow meant for the enemy catches the caster's
		// own side too (FTuning::FriendlyFire), though never the caster.
		const std::string Shape = ShapeOf(*Ability);
		const bool bFriendlyFire = Tuning.FriendlyFire >= 0.5 && !Unit.bMonster && Ability->Target == ETargetSide::Enemy
			&& Ability->Effect == EEffect::Damage && Shape != "global"
			&& (Ability->Aoe > 0.0f || Shape == "cone" || Shape == "line" || Shape == "vector" || Shape == "circle");

		// In id order, which is not merely tidy: resolution rolls to evade and to
		// crit while walking this list, so the order decides the order the dice
		// come out in, and two machines disagreeing about it would disagree about
		// every roll after the first.
		for (const FUnit& Target_ : Units)
		{
			if (bFriendlyFire && Target_.IsAlive() && Target_.Team == Unit.Team && Target_.Id != Unit.Id)
			{
				// One of its own, standing in the way: taken as an enemy would be.
			}
			else if (Ability->Target == ETargetSide::KoAlly)
			{
				if (!Target_.IsKo() || Target_.Team != Unit.Team)
				{
					continue;
				}
			}
			else if (!Target_.IsAlive()
				|| (Target_.Team != Unit.Team) != (Ability->Target == ETargetSide::Enemy))
			{
				continue;
			}

			// The caster is judged where it is casting from, which may not be
			// where it is standing: the computer asks about spots it could walk to.
			const bool bIsSelf = Target_.Id == Unit.Id;
			const FVec2 Pos = bIsSelf ? From : Target_.Pos;
			if (!InShape(*Ability, From, Target, Pos))
			{
				continue;
			}
			// Off the ground: only something with reach can touch it.
			if (!bIsSelf && Target_.Flies() && Ability->MaxRange <= Ground::MeleeRange
				&& Ability->Target == ETargetSide::Enemy)
			{
				continue;
			}

			FHit Hit;
			Hit.UnitId = Target_.Id;
			Hit.Where = Pos;
			if (Ability->LaysZone())
			{
				// Ground (2026-10-04): what it will take from whoever starts a turn in
				// it, which is nothing for anything off the ground.
				if (Target_.Flies())
				{
					continue;
				}
				Hit.Amount = Ability->ZonePercent > 0.0f
					? std::max(1, RoundToInt(Target_.MaxHp() * Ability->ZonePercent * 0.01)) : 0;
			}
			else
			{
				Hit.Amount = CalcAmount(Unit, *Ability, From, Target_, Pos,
					LevelAt(From), LevelAt(Pos));
			}
			Hit.Distance = Pos.DistanceTo(Target);
			Hit.Flank = Ability->Effect == EEffect::Damage
				? FlankBonus(Target_, Pos, From) : 1.0;
			Out.push_back(Hit);
		}

		// A single-target ability swept up everyone standing near the aim point;
		// only the nearest of them is actually hit. A tie goes to the lower id,
		// because the comparison is strict and the list is in id order.
		if (Ability->Aoe == 0.0f && ShapeOf(*Ability) != "global" && Out.size() > 1)
		{
			const FHit* Closest = &Out[0];
			for (const FHit& Hit : Out)
			{
				if (Hit.Distance < Closest->Distance)
				{
					Closest = &Hit;
				}
			}
			const FHit Kept = *Closest;
			Out.clear();
			Out.push_back(Kept);
		}
		// Echo Slam (2026-10-06, Cire's Spell Codex): a fifth harder for each enemy
		// caught beyond the first.
		if (Ability->Special == "crowd" && Ability->Effect == EEffect::Damage && Out.size() > 1)
		{
			int Caught = 0;
			for (const FHit& Hit : Out)
			{
				for (const FUnit& Struck : Units)
				{
					Caught += Struck.Id == Hit.UnitId && Struck.Team != Unit.Team ? 1 : 0;
				}
			}
			if (Caught > 1)
			{
				const double Scale = 1.0 + CrowdShare * (Caught - 1);
				for (FHit& Hit : Out)
				{
					Hit.Amount = RoundToInt(Hit.Amount * Scale);
				}
			}
		}
		return Out;
	}
}
