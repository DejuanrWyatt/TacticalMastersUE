#include "SimBattle.h"

#include "SimAbility.h"

#include <algorithm>
#include <cmath>

namespace TMSim
{
	void FBattle::Start(uint64_t InSeed)
	{
		// Somewhere to stand, if nobody has said where yet.
		if (Map.TilesX == 0)
		{
			Map.BuildMirrored(HighlandsRows());
		}
		// Where each side started, for a unit that cannot see anybody to walk at.
		if (SpawnPoints[0] == FVec2() && SpawnPoints[1] == FVec2())
		{
			const FVec2 Size = Map.SizeMeters();
			SpawnPoints[0] = FVec2(2.75f, 4.75f);
			SpawnPoints[1] = FVec2(Size.X - 2.75f, Size.Y - 4.75f);
		}
		Rng.Seed(InSeed);
		// Before anybody moves, and from the same seed, so a match's two machines
		// and a replay put the towers in the same places. Their own generator:
		// the battle's dice are exactly as they were without them.
		PlaceWatchtowers(InSeed);
		TickCount = 0;
		Winner = -1;
		// Time to place units first, if the battle has any (game_state.gd:292-293).
		PlanningTicks = RoundToInt(Tuning.PlanningSeconds * Pace::TicksPerSecond);
		PlanningDone[0] = false;
		PlanningDone[1] = false;
		const FVec2 Size = Map.SizeMeters();
		const FVec2 Middle(Size.X * 0.5f, Size.Y * 0.5f);
		for (FUnit& Unit : Units)
		{
			// Everybody opens the battle facing the middle of the map. Which way a
			// unit faces is a rule -- a hit from the side or behind does more -- so
			// the rules set it rather than whoever deals the units
			// (game_state.gd:289).
			Unit.Facing = (Middle - Unit.Pos).Normalized();
			// What its class is worth. Without this a unit has no stats at all,
			// which means no health, so it would quietly start the battle dead.
			// Binding it here means a roster is only ever "these ids, these
			// classes, these spots" and cannot be half-built.
			if (!Unit.Stats)
			{
				if (const FJobDef* Job = FindJob(Unit.Job))
				{
					Unit.Stats = &Job->Stats;
				}
			}
			Unit.Hp = Unit.MaxHp();
			// A faster unit starts nearer its first turn, so the opening order is
			// the Speed order rather than a scramble. One short of full: becoming
			// ready is the clock's job, not the setup's.
			Unit.Tg = std::min(Pace::TgMax - 1, Unit.Stat(EStat::Speed) * Pace::StartTgPerSpeed);
			Unit.bReady = false;
			Unit.Clock = 0;
			Unit.Serial = 0;
			Unit.bMoved = false;
			Unit.bActed = false;
			Unit.bHustling = false;
			Unit.UnharmedTurns = 0;
		}
		// The camps and their monsters, waiting off the board, after both sides'
		// units so every id is fixed before anybody moves. Their own generators.
		PlaceCamps(InSeed);
		// Pets, after the camps so every monster keeps the id it had.
		PlacePets();
	}

	int FBattle::BaseTgGain(const FUnit& Unit) const
	{
		return std::max(1, RoundToInt(Unit.Stat(EStat::Speed) * Pace::TgPerSpeed * Tuning.SpeedMultiplier));
	}

	double FBattle::HustleFactor(const FUnit& Unit) const
	{
		return Unit.IsHustling() ? 1.0 + Tuning.HustleBonus * 0.01 : 1.0;
	}

	int FBattle::TgGain(const FUnit& Unit) const
	{
		// Items can make the gauge fill faster; with none the last factor is
		// exactly 1.0 and this is the old number bit for bit.
		const double ItemFactor = Unit.HasItems() ? 1.0 + Unit.ItemTgPercent() * 0.01 : 1.0;
		return RoundToInt(BaseTgGain(Unit) * Unit.TgFactor() * HustleFactor(Unit) * ItemFactor);
	}

	int FBattle::ClockTicks(const FUnit& Unit) const
	{
		return RoundToInt((Tuning.ClockBase + Tuning.PatienceMultiplier * Unit.Stat(EStat::Patience))
			* Pace::TicksPerSecond);
	}

	int FBattle::TicksToReady(const FUnit& Unit) const
	{
		if (Unit.bReady)
		{
			return 0;
		}
		// A spell in flight holds the gauge: the wait is the cast, then the fill.
		// Casting is not ported yet, so that part is zero for now.
		const int CastTicks = 0;
		int Gain = TgGain(Unit);
		if (Gain <= 0)
		{
			Gain = BaseTgGain(Unit);  // held by a status: guess as if it were not
		}
		const int Remaining = Pace::TgMax - Unit.Tg;
		return CastTicks + std::max(0, static_cast<int>(std::ceil(static_cast<double>(Remaining) / Gain)));
	}

	FUnit* FBattle::FindUnit(int Id)
	{
		for (FUnit& Unit : Units)
		{
			if (Unit.Id == Id)
			{
				return &Unit;
			}
		}
		return nullptr;
	}

	void FBattle::Tick(FTickReport& Report)
	{
		if (Winner != -1)
		{
			return;
		}

		// While the sides are still placing their units, nothing else happens.
		if (PlanningTicks > 0)
		{
			// Reaching zero is the fighting starting; Godot also logs it
			// (game_state.gd:1186-1190, 1516-1518).
			--PlanningTicks;
			return;
		}

		++TickCount;

		// A battle can have a time limit, so it cannot run for ever; and a side
		// can win by holding the middle. Both are checked before anybody moves on
		// this tick (game_state.gd:1192-1200).
		if (Tuning.BattleSeconds > 0.0 && TickCount >= RoundToInt(Tuning.BattleSeconds * Pace::TicksPerSecond))
		{
			FinishOnTime(Report);
			return;
		}
		TickCapture(Report);
		if (Winner != -1)
		{
			return;
		}
		if (!Camps.empty())
		{
			TickCamps(Report);
		}

		for (FUnit& Unit : Units)
		{
			if (Unit.IsKo())
			{
				// Reraise: back on its feet with a quarter of its health (feat-status-effects.md).
				if (Unit.ReraiseTicks > 0 && --Unit.ReraiseTicks == 0)
				{
					Unit.Hp = std::max(1, Unit.MaxHp() / 4);
					Unit.KoTicks = 0;
					Unit.Tg = 0;
					Unit.bReady = false;
					FEvent Event;
					Event.Kind = EEventKind::Revived;
					Event.Unit = Unit.Id;
					Event.Where = Unit.Pos;
					Event.Id = "reraise";
					Report.Events.push_back(Event);
					continue;
				}
				--Unit.KoTicks;
				if (Unit.KoTicks <= 0)
				{
					Report.Say(EEventKind::Gone, Unit.Id);
					OnGone(Unit, Report);
				}
				continue;
			}
			if (!Unit.IsAlive())
			{
				continue;
			}
			if (!Unit.Statuses.empty())
			{
				TickStops(Unit);
			}

			// A spell part-way out. Three things follow from doing this here, before
			// the gauge and the countdown below, and from nothing else:
			//
			//  - the gauge does not fill while casting, and not even on the tick the
			//    cast lands, because bWasCasting is still true for that one;
			//  - the turn countdown keeps running, so a cast begun late can time its
			//    own turn out -- and the spell still lands afterwards, off a gauge
			//    that then starts from nothing;
			//  - units are walked in id order, so a spell can strike down someone
			//    later in the list before their own tick is reached.
			const bool bWasCasting = Unit.IsCasting();
			if (bWasCasting)
			{
				--Unit.Casting.Ticks;
				if (Unit.Casting.Ticks <= 0)
				{
					const FCast Cast = Unit.Casting;
					Unit.Casting = FCast();

					// Where it lands is decided now, not when it was cast: aimed at
					// someone it follows them wherever they went, aimed at the ground
					// it stays put. A followed unit that has fallen stops being
					// followed, and the spell lands where they were.
					FVec2 Where = Cast.Target;
					if (const FUnit* Followed = FindUnit(Cast.FollowId))
					{
						if (Followed->IsAlive())
						{
							Where = Followed->Pos;
						}
					}
					FEvent Event;
					Event.Kind = EEventKind::CastFinished;
					Event.Unit = Unit.Id;
					Event.Slot = Cast.Slot;
					Event.Where = Where;
					Report.Events.push_back(Event);

					ResolveAbility(Unit, Cast.Slot, Where, Report);
					if (Winner != -1)
					{
						return;
					}
					if (!Unit.IsAlive())
					{
						continue;
					}
				}
			}

			if (Unit.bReady)
			{
				--Unit.Clock;
				if (Unit.Clock <= 0)
				{
					EndTurn(Unit, true, Report);
				}
			}
			else if (!bWasCasting)
			{
				Unit.Tg = std::min(Pace::TgMax, Unit.Tg + TgGain(Unit));
				if (Unit.Tg >= Pace::TgMax)
				{
					BecomeReady(Unit, Report);
				}
			}
		}
	}

	void FBattle::Advance(int Ticks, FTickReport& Report)
	{
		for (int i = 0; i < Ticks; ++i)
		{
			Tick(Report);
		}
	}

	double FBattle::FlankBonus(const FUnit& Target, FVec2 TargetPos, FVec2 From) const
	{
		const FVec2 ToAttacker = From - TargetPos;
		if (ToAttacker.Length() < 0.01f)
		{
			return 1.0;
		}
		// Off-Balance: whichever way the blow comes, it lands as if from behind.
		if (!Target.Statuses.empty() && Target.HasStatus("offbalance"))
		{
			return Tuning.BackBonus;
		}
		// Which way the target is looking, against where the blow comes from.
		const float Facing = Target.Facing.Dot(ToAttacker.Normalized());
		if (Facing < -0.5f)
		{
			return Tuning.BackBonus;
		}
		if (Facing < 0.5f)
		{
			return Tuning.SideBonus;
		}
		return 1.0;
	}

	int FBattle::CalcAmount(const FUnit& User, const FAbility& Ability, FVec2 From,
		const FUnit& Target, FVec2 TargetPos, int FromLevel, int TargetLevel) const
	{
		// What an ability does is its own power, and nothing else of its user's
		// -- except what its items add: a flat amount, then a percent of the
		// total (Docs/design/feat-items.md 3.4). With no items both are 0 and
		// (Power + 0) x 1.0 is exactly Power, so every number is the old one.
		const bool bAtt = Ability.Scale == EScale::Att;
		const bool bItems = User.HasItems();
		const bool bHeal = Ability.Effect == EEffect::Heal;
		const double Flat = !bItems ? 0.0 : bHeal ? User.ItemHealFlat(bAtt) : User.ItemDamageFlat(bAtt);
		double Percent = !bItems ? 0.0 : bHeal ? User.ItemHealPercent(bAtt) : User.ItemDamagePercent(bAtt);
		// What only counts sometimes (Docs/design/feat-neutral-camps.md 14): Surge,
		// a monster's rage, and the situational items. All 0 in a Godot battle.
		int Extra = 0;
		if (Ability.Effect == EEffect::Damage)
		{
			for (const FStatus& Status : User.Statuses)
			{
				const FStatusDef* Def = FindStatus(Status.Id);
				Extra += Def ? Def->DamagePercent : 0;
			}
			if (User.bMonster && User.Hp * 2 < User.MaxHp())
			{
				const FMonsterInfo* Info = User.MonsterInfo();
				Extra += Info && Info->Has(MonsterTrait::Enrage) ? 25 : 0;
			}
			if (bItems)
			{
				Extra += Target.bMonster ? GearSum(User, &FItemDef::VsMonstersPercent) : 0;
				Extra += FromLevel > TargetLevel ? GearSum(User, &FItemDef::HighGroundPercent) : 0;
				Extra += User.Hp > 0 && User.Hp < User.MaxHp() * Items::LowHealthShare ? GearSum(User, &FItemDef::LowHealthPercent) : 0;
				const int Alone = GearSum(User, &FItemDef::AlonePercent);
				Extra += Alone > 0 && IsAlone(User, From) ? Alone : 0;
			}
		}
		if (Extra != 0)
		{
			Percent = std::min<double>(Items::DamagePercentCap, Percent + Extra);
		}
		double Power = (bItems || Extra != 0) && (bHeal || Ability.Effect == EEffect::Damage)
			? (Ability.Power + Flat) * (1.0 + Percent / 100.0) : Ability.Power;
		// The new spells (2026-10-05): an echo at half power; Reckoning harder the
		// more of the user's health is gone; a Charge harder for every metre run.
		if (EchoScale != 1.0 && (bHeal || Ability.Effect == EEffect::Damage))
		{
			Power *= EchoScale;
		}
		if (Ability.Special == "reckoning" && User.MaxHp() > 0)
		{
			Power *= 1.0 + std::max(0.0, 1.0 - static_cast<double>(User.Hp) / User.MaxHp());
		}
		else if (Ability.Special == "charge")
		{
			Power *= 1.0 + 0.05 * std::max(0.0, static_cast<double>(From.DistanceTo(TargetPos)) - 1.5);
		}

		switch (Ability.Effect)
		{
		case EEffect::Damage:
		{
			const int Defence = Target.Stat(Ability.Scale == EScale::Att ? EStat::AttDef : EStat::MagDef);

			int Levels = FromLevel - TargetLevel;
			Levels = std::max(-Combat::MaxHeightLevels, std::min(Combat::MaxHeightLevels, Levels));
			const double Height = 1.0 + Tuning.HeightBonus * Levels;
			const double Flank = FlankBonus(Target, TargetPos, From);

			int Raw = RoundToInt(Power * Combat::DamageScale * Height * Flank);
			// Reckless Idol: its holder takes more.
			if (Target.HasItems())
			{
				const int More = GearSum(Target, &FItemDef::DamageTakenPercent);
				Raw = More > 0 ? RoundToInt(Raw * (1.0 + More / 100.0)) : Raw;
			}
			// Marked, Oiled against fire, Protect and Shell (feat-status-effects.md).
			if (!Target.Statuses.empty())
			{
				double Scale = 1.0;
				Scale *= Target.HasStatus("marked") ? 1.3 : 1.0;
				Scale *= Target.HasStatus("oiled") && ElementOf(Ability) == "fire" ? 1.5 : 1.0;
				Scale *= Ability.Scale == EScale::Att && Target.HasStatus("protect") ? 0.67 : 1.0;
				Scale *= Ability.Scale == EScale::Mag && Target.HasStatus("shell") ? 0.67 : 1.0;
				Raw = Scale != 1.0 ? RoundToInt(Raw * Scale) : Raw;
			}
			// Defense model 1 (2026-10-01): a share off, never a slice, so a small
			// hit counts and no pile of defense makes a unit immune. Defense below
			// nothing (a debuff larger than it) counts as none.
			int Dealt = 0;
			if (NewDefense())
			{
				const double Scale = std::max(1.0, Tuning.DefenseScale);
				Dealt = std::max(Combat::MinimumDamage,
					RoundToInt(Raw * Tuning.DamageMultiplier * Scale / (Scale + std::max(0, Defence))));
			}
			else
			{
				Dealt = std::max(Combat::MinimumDamage, RoundToInt((Raw - Defence) * Tuning.DamageMultiplier));
			}
			// Verdict (2026-10-06, Cire's Spell Codex): a quarter of what the target
			// is missing on top, past its defence.
			if (Ability.Special == "execute")
			{
				Dealt += RoundToInt(std::max(0, Target.MaxHp() - Target.Hp) * ExecuteShare);
			}
			return Dealt;
		}
		case EEffect::Heal:
		{
			int Full = RoundToInt(Power * Combat::HealScale * Tuning.HealMultiplier);
			// Berserker's Brand: healing from others lands weaker on its holder.
			if (Target.HasItems() && Target.Id != User.Id)
			{
				const int Change = GearSum(Target, &FItemDef::HealTakenPercent);
				Full = Change != 0 ? std::max(0, RoundToInt(Full * (1.0 + Change / 100.0))) : Full;
			}
			// Wounded: less of it lands, whoever heals.
			Full = Target.HealReceived(Full);
			// Never more than it is short of: overhealing is not a thing here.
			const int Missing = Target.MaxHp() - Target.Hp;
			return std::min(Full, Missing);
		}
		case EEffect::Revive:
			// A revive's power is the share of max HP it comes back with.
			return std::max(1, RoundToInt(Target.MaxHp() * Power));

		case EEffect::Support:
		default:
			return 0;
		}
	}

	bool FBattle::IsAlone(const FUnit& Unit, const FVec2& At) const
	{
		for (const FUnit& Other : Units)
		{
			if (Other.Id != Unit.Id && Other.IsAlive() && Other.Team == Unit.Team
				&& Other.Pos.DistanceTo(At) <= Items::AloneReach)
			{
				return false;
			}
		}
		return true;
	}

	int FBattle::EvadeChance(const FUnit& Target, const FAbility& Ability, const FUnit* Attacker) const
	{
		// Only something harmful can be got out of the way of.
		if (Ability.Effect != EEffect::Damage)
		{
			return 0;
		}
		// Defense model 1: the one Evasion, against either kind of hit.
		const int Base = NewDefense() ? EvasionOf(Target) : Target.Stat(Ability.Scale == EScale::Att ? EStat::AEva : EStat::MEva);
		// A blinded attacker is that much easier to step around.
		const int Blind = Attacker ? Attacker->MissChance() : 0;
		const int Chance = RoundToInt(Base * Tuning.EvadeMultiplier) + Blind;
		return std::max(0, std::min(95, Chance));
	}

	int FBattle::EvasionOf(const FUnit& Unit) const
	{
		if (!Unit.Stats)
		{
			return 0;
		}
		const int BaseA = Unit.Stats->Get(EStat::AEva);
		const int BaseM = Unit.Stats->Get(EStat::MEva);
		// Everything added on top of the class's own, to either: items, buffs,
		// passives. Nightcloak adds to both, so it is counted once.
		const int Added = (Unit.Stat(EStat::AEva) - BaseA) + (Unit.Stat(EStat::MEva) - BaseM);
		const int Twice = Unit.HasItems() && Unit.bUnseenAtStart ? GearSum(Unit, &FItemDef::UnseenEvasion) : 0;
		return std::max(0, std::max(BaseA, BaseM) + Added - Twice);
	}

	int FBattle::DefenseShare(int Defense) const
	{
		const double Scale = std::max(1.0, Tuning.DefenseScale);
		const double D = std::max(0, Defense);
		return RoundToInt(100.0 * D / (Scale + D));
	}

	int FBattle::CritChance(const FUnit& User) const
	{
		const int Chance = RoundToInt(User.Stat(EStat::Crit) * Tuning.CritChanceMultiplier);
		return std::max(0, std::min(100, Chance));
	}

	FOdds FBattle::OddsOf(const FUnit& User, const FAbility& Ability, const FUnit& Target, int Amount) const
	{
		FOdds Odds;
		if (Ability.Effect != EEffect::Damage)
		{
			return Odds;
		}
		Odds.Evade = EvadeChance(Target, Ability, &User);
		// First Strike Gauntlet: its first hit that lands is critical, no roll.
		const bool bFirstStrike = User.HasItems() && !User.bFirstStrikeUsed && GearHas(User, &FItemDef::bFirstStrike);
		Odds.CritChance = bFirstStrike ? 100 : CritChance(User);
		const double Evaded = Odds.Evade / 100.0;
		const double Lands = 1.0 - Evaded;
		Odds.Dodge = NewDefense() ? Evaded / Combat::DodgeOneIn * 100.0 : Evaded * 100.0;
		Odds.Graze = Evaded * 100.0 - Odds.Dodge;
		Odds.Crit = Lands * Odds.CritChance;
		Odds.Hit = Lands * (100.0 - Odds.CritChance);
		Odds.HitAmount = Amount;
		Odds.CritAmount = std::max(1, RoundToInt(Amount * Tuning.CritMultiplier));
		Odds.GrazeAmount = std::max(Combat::MinimumDamage, RoundToInt(Amount * Combat::GrazeDamage));
		// Shields soak first (TakeFromShield), so they count as health here.
		int Soak = 0;
		for (const FStatus& Status : Target.Statuses)
		{
			const FStatusDef* Def = FindStatus(Status.Id);
			Soak += (Def && Def->bAbsorbs) ? Status.Amount : 0;
		}
		const int Lasts = Target.Hp + Soak;
		Odds.bHitKo = Odds.HitAmount >= Lasts;
		Odds.bCritKo = Odds.CritAmount >= Lasts;
		Odds.bGrazeKo = Odds.GrazeAmount >= Lasts;
		Odds.Ko = (Odds.bHitKo ? Odds.Hit : 0.0) + (Odds.bCritKo ? Odds.Crit : 0.0) + (Odds.bGrazeKo ? Odds.Graze : 0.0);
		return Odds;
	}

	FThreat FBattle::ThreatOn(const FUnit& Attacker, const FUnit& Target, const FVec2& TargetPos) const
	{
		FThreat Best;
		if (!Attacker.IsAlive() || !Target.IsAlive() || Attacker.IsSilenced())
		{
			return Best;
		}
		// Each count goes down by one as its next turn begins, so one at 1 is
		// ready then; on its turn, only what is ready now.
		const int ReadyBy = Attacker.bReady ? 0 : 1;
		const bool bCanWalk = !(Attacker.bReady && Attacker.bMoved);
		const double Walk = bCanWalk ? MoveOf(Attacker) : 0.0;
		const double Apart = Attacker.Pos.DistanceTo(TargetPos);
		double BestAverage = -1.0;
		for (int Slot = 0; Slot < AbilitySlots; ++Slot)
		{
			const FAbility* Ability = Attacker.Ability(Slot);
			if (!Ability || Ability->Effect != EEffect::Damage || Ability->Target != ETargetSide::Enemy
				|| Ability->Kind == "passive" || Ability->Kind == "aura" || Ability->Kind == "toggle"
				|| Attacker.Cooldowns[Slot] > ReadyBy || (Slot == 3 && Attacker.Ult < Pace::UltMax))
			{
				continue;
			}
			// How far off it can catch someone: its range, and the blast round
			// where it lands. Only something with reach touches a flier.
			if (Target.Flies() && Ability->MaxRange <= Ground::MeleeRange)
			{
				continue;
			}
			const double Reach = static_cast<double>(Ability->MaxRange) + Ability->Aoe + Ground::HitRadius;
			const bool bHere = Apart <= Reach && Apart + Ability->Aoe + Ground::HitRadius >= Ability->MinRange;
			const bool bAfterWalk = !bHere && Apart <= Reach + Walk;
			if (!bHere && !bAfterWalk)
			{
				continue;
			}
			// Struck from where it stands, or from the edge of its reach after the walk.
			FVec2 From = Attacker.Pos;
			if (bAfterWalk && Apart > 0.01)
			{
				const double Keep = std::max(0.0, std::min(static_cast<double>(Ability->MaxRange), Apart));
				From = TargetPos + (Attacker.Pos - TargetPos) * static_cast<float>(Keep / Apart);
			}
			const int Amount = CalcAmount(Attacker, *Ability, From, Target, TargetPos, LevelAt(From), LevelAt(TargetPos));
			const FOdds Odds = OddsOf(Attacker, *Ability, Target, Amount);
			const double Average = (Odds.Hit * Odds.HitAmount + Odds.Crit * Odds.CritAmount + Odds.Graze * Odds.GrazeAmount) / 100.0;
			// Not having to walk beats walking; then the likelier knockout; then the harder blow.
			const bool bBetter = Best.Slot < 0
				|| (Best.bMoves && !bAfterWalk)
				|| (Best.bMoves == bAfterWalk && (Odds.Ko > Best.Odds.Ko + 0.001
					|| (std::fabs(Odds.Ko - Best.Odds.Ko) <= 0.001 && Average > BestAverage)));
			if (bBetter)
			{
				Best.Slot = Slot;
				Best.bMoves = bAfterWalk;
				Best.Odds = Odds;
				BestAverage = Average;
			}
		}
		return Best;
	}

	void FBattle::TickStatuses(FUnit& Unit, FTickReport& Report)
	{
		// Statuses act on the unit's own turn and then count down, so "two turns"
		// means its next two. Burn can knock it out before it ever gets to act.
		if (Unit.Statuses.empty())
		{
			return;
		}
		std::vector<FStatus> Kept;
		Kept.reserve(Unit.Statuses.size());
		// A copy, walked instead of the unit's own list, because Hurt replaces that
		// list (anything asleep is woken) part-way through this loop. Walking the
		// live one read freed memory on every burn or bleed tick -- found by
		// AddressSanitizer when it began crashing the class lab. The copy is also
		// what Godot does: its `for s in u.statuses` keeps walking the array it
		// began with after _hurt assigns a new one, and the `u.statuses = kept` at
		// the end overrides the waking, so a Sleep that damage woke comes back if
		// it had turns left (game_state.gd:1237-1277, 1339-1350). Kept bug-for-bug.
		const std::vector<FStatus> Current = Unit.Statuses;
		// Time Bomb (2026-10-05): it goes off once the statuses have had their say.
		int BombAmount = 0;
		int BombBy = -1;
		bool bBomb = false;
		for (FStatus Status : Current)
		{
			const FStatusDef* Def = FindStatus(Status.Id);
			if (!Def)
			{
				continue;
			}
			// Life Tether (2026-10-05): a share of its health to the caster, while
			// the caster stands and is near enough; otherwise it snaps.
			if (Status.Id == "tethered")
			{
				FUnit* Caster = FindUnit(Status.By);
				if (!Caster || !Caster->IsAlive() || static_cast<double>(Caster->Pos.DistanceTo(Unit.Pos)) > 8.0)
				{
					continue;
				}
				const int Taken = Hurt(Unit, std::max(1, RoundToInt(Unit.MaxHp() * 0.05)));
				if (Taken > 0)
				{
					FEvent Event;
					Event.Kind = EEventKind::Hit;
					Event.Unit = Unit.Id;
					Event.By = Caster->Id;
					Event.Amount = Taken;
					Event.Where = Unit.Pos;
					Event.Id = "tethered";
					Report.Events.push_back(Event);
					const int Healed = Caster->HasStatus("decay") ? 0 : std::min(Caster->HealReceived(Taken), Caster->MaxHp() - Caster->Hp);
					if (Healed > 0)
					{
						Caster->Hp += Healed;
						FEvent Drain;
						Drain.Kind = EEventKind::Hit;
						Drain.Unit = Caster->Id;
						Drain.Amount = Healed;
						Drain.Where = Caster->Pos;
						Drain.Id = "drain";
						Report.Events.push_back(Drain);
					}
					if (!Unit.IsAlive())
					{
						KnockOut(Unit, Report);
						CheckWinner();
						return;
					}
				}
			}
			if (Def->PerTurn != 0.0f && Unit.IsAlive())
			{
				const int Amount = std::max(1,
					RoundToInt(Unit.MaxHp() * std::fabs(static_cast<double>(Def->PerTurn))));
				if (Def->PerTurn < 0.0f)
				{
					const int Taken = Hurt(Unit, Amount);
					if (Taken > 0)
					{
						FEvent Event;
						Event.Kind = EEventKind::Hit;
						Event.Unit = Unit.Id;
						Event.Amount = Taken;
						Event.Where = Unit.Pos;
						Event.Id = Status.Id;
						Report.Events.push_back(Event);
						if (!Unit.IsAlive())
						{
							KnockOut(Unit, Report);
							CheckWinner();
							return;
						}
					}
				}
				else if (Unit.HasStatus("decay"))
				{
					// Decay: what would mend it rots it.
					const int Taken = Hurt(Unit, Amount);
					FEvent Event;
					Event.Kind = EEventKind::Hit;
					Event.Unit = Unit.Id;
					Event.Amount = Taken;
					Event.Where = Unit.Pos;
					Event.Id = "decay";
					Report.Events.push_back(Event);
					if (!Unit.IsAlive())
					{
						KnockOut(Unit, Report);
						CheckWinner();
						return;
					}
				}
				else
				{
					const int Healed = std::min(Unit.HealReceived(Amount), Unit.MaxHp() - Unit.Hp);
					if (Healed > 0)
					{
						Unit.Hp += Healed;
						FEvent Event;
						Event.Kind = EEventKind::Hit;
						Event.Unit = Unit.Id;
						Event.Amount = Healed;
						Event.Where = Unit.Pos;
						Event.Id = Status.Id;
						Report.Events.push_back(Event);
					}
				}
			}

			// Charmed ends with the turn it fights for the other side, and Stop
			// counts in ticks (TickStops): neither counts down here.
			if (Status.Id == "charmed" || Status.Id == "stop")
			{
				Kept.push_back(Status);
				continue;
			}
			--Status.Turns;
			if (Status.Turns > 0)
			{
				Kept.push_back(Status);
			}
			else if (Status.Id == "bomb")
			{
				bBomb = true;
				BombAmount = Status.Amount;
				BombBy = Status.By;
			}
			else if (Def->bDoom && Unit.IsAlive())
			{
				// The count has run out, and that is that however much health is
				// left. Whatever else was on it goes too.
				Unit.Hp = 0;
				Unit.UnharmedTurns = 0;
				Unit.Statuses.swap(Kept);
				KnockOut(Unit, Report);
				CheckWinner();
				return;
			}
		}
		Unit.Statuses.swap(Kept);
		// Brought to its senses part-way through (a burn tick): the copy walked above still had it.
		if (Unit.CharmedFrom < 0 && Unit.HasStatus("charmed"))
		{
			RemoveStatus(Unit, "charmed");
		}
		if (bBomb && Unit.IsAlive())
		{
			Detonate(Unit, BombAmount, BombBy, Report);
		}
	}

	void FBattle::GroundEffect(FUnit& Unit, FTickReport& Report)
	{
		// Whether the spring underfoot is dry, before this turn counts it down; a
		// dry spring counts down on its user's turns, on anyone's once that one is gone.
		const bool bDry = SpringRestAt(Unit.Pos) > 0;
		if (!SpringRests.empty())
		{
			for (FSpringRest& Rest : SpringRests)
			{
				const FUnit* Owner = FindUnit(Rest.UnitId);
				if (Rest.UnitId == Unit.Id || !Owner || !Owner->IsAlive())
				{
					--Rest.Turns;
				}
			}
			SpringRests.erase(std::remove_if(SpringRests.begin(), SpringRests.end(),
				[](const FSpringRest& Rest) { return Rest.Turns <= 0; }), SpringRests.end());
		}
		// Burning ground hurts and a spring heals, and either happens when the
		// unit's turn comes round rather than when it walks on.
		int Kind = HazardAt(Unit.Pos);
		if (Kind == 0)
		{
			return;
		}
		if (Kind > 0 && bDry)
		{
			return;  // dry for now
		}
		const int Amount = std::max(1, RoundToInt(Unit.MaxHp() * (Kind > 0 ? Tuning.SpringPercent : Tuning.HazardPercent) * 0.01));
		if (Kind < 0 && !Unit.Statuses.empty())
		{
			// Embers dry the Wet and thaw the Chilled.
			RemoveStatus(Unit, "wet");
			RemoveStatus(Unit, "chilled");
		}
		if (Kind > 0 && Unit.HasStatus("decay"))
		{
			Kind = -1;  // a spring only rots what Decay holds
		}
		if (Kind < 0)
		{
			const int Taken = Hurt(Unit, Amount);
			if (Taken > 0)
			{
				FEvent Event;
				Event.Kind = EEventKind::Hit;
				Event.Unit = Unit.Id;
				Event.Amount = Taken;
				Event.Where = Unit.Pos;
				Event.Id = "ground";
				Report.Events.push_back(Event);
				if (!Unit.IsAlive())
				{
					KnockOut(Unit, Report);
					CheckWinner();
				}
			}
			return;
		}
		const int Healed = std::min(Unit.HealReceived(Amount), Unit.MaxHp() - Unit.Hp);
		if (Healed > 0)
		{
			Unit.Hp += Healed;
			FEvent Event;
			Event.Kind = EEventKind::Hit;
			Event.Unit = Unit.Id;
			Event.Amount = Healed;
			Event.Where = Unit.Pos;
			Event.Id = "ground";
			Report.Events.push_back(Event);
			// Used: it rests now, dry through that many of the user's turns.
			const int Rest = RoundToInt(Tuning.SpringRestTurns);
			if (Rest > 0)
			{
				FSpringRest Dry;
				Dry.Tile = static_cast<int>(std::floor(Unit.Pos.Y / Ground::TileSize)) * Map.TilesX
					+ static_cast<int>(std::floor(Unit.Pos.X / Ground::TileSize));
				Dry.UnitId = Unit.Id;
				Dry.Turns = Rest;
				SpringRests.push_back(Dry);
			}
		}
	}

	int FBattle::SpringRestAt(const FVec2& Point) const
	{
		if (SpringRests.empty() || !InBounds(Point))
		{
			return 0;
		}
		const int Tile = static_cast<int>(std::floor(Point.Y / Ground::TileSize)) * Map.TilesX
			+ static_cast<int>(std::floor(Point.X / Ground::TileSize));
		int Left = 0;
		for (const FSpringRest& Rest : SpringRests)
		{
			Left = Rest.Tile == Tile ? std::max(Left, Rest.Turns) : Left;
		}
		return Left;
	}

	void FBattle::UndamagedRegen(FUnit& Unit, FTickReport& Report)
	{
		// Left alone long enough, a unit mends at the start of each of its turns.
		// Anything that hurt it since its last turn puts the count back to nothing,
		// so the room has to be given rather than merely waited for.
		++Unit.UnharmedTurns;
		if (Tuning.RegenPercent <= 0.0 || (!Unit.Statuses.empty() && Unit.HasStatus("decay"))
			|| Unit.UnharmedTurns < std::max(1, RoundToInt(Tuning.RegenAfterTurns)))
		{
			return;
		}
		const int Amount = std::min(
			Unit.HealReceived(std::max(1, RoundToInt(Unit.MaxHp() * Tuning.RegenPercent * 0.01))),
			Unit.MaxHp() - Unit.Hp);
		if (Amount <= 0)
		{
			return;
		}
		Unit.Hp += Amount;
		FEvent Event;
		Event.Kind = EEventKind::Hit;
		Event.Unit = Unit.Id;
		Event.Amount = Amount;
		Event.Where = Unit.Pos;
		Event.Id = "mend";
		Report.Events.push_back(Event);
	}

	void FBattle::BecomeReady(FUnit& Unit, FTickReport& Report)
	{
		// The order here is the original's and is load-bearing: a status can kill
		// the unit before its turn begins, and so can the ground it is standing on,
		// so each is followed by a check that there is still anybody to give a turn
		// to.

		// A pet's time: counted as each of its turns begins, and when it runs out
		// the pet leaves instead of taking the turn.
		if (Unit.PetOf >= 0 && Unit.PetTurns > 0 && --Unit.PetTurns == 0)
		{
			SendPetAway(Unit, Report);
			return;
		}

		// A new turn: whatever gave it away in the grass is behind it.
		Unit.bSpotted = false;
		// Read before the statuses count down, because the status taking its orders
		// away costs it this turn and then wears off in the same breath.
		const std::string BlockedBy = Unit.NoOrdersStatus();
		// Terrified runs this turn even if the fear wears off as it begins.
		int FearOf = -1;
		for (const FStatus& Status : Unit.Statuses)
		{
			FearOf = Status.Id == "terrified" ? Status.By : FearOf;
		}

		TickStatuses(Unit, Report);
		if (!Unit.IsAlive())
		{
			return;
		}
		GroundEffect(Unit, Report);
		if (!Unit.IsAlive())
		{
			return;
		}
		// Ground zones (2026-10-04): they count down, and touch whoever starts here.
		const bool bZoneLost = !Zones.empty() && ZonesAtTurnStart(Unit, Report);
		if (!Unit.IsAlive())
		{
			return;
		}
		UndamagedRegen(Unit, Report);
		// Seen or not as its turn begins (Nightcloak), and a shrine underfoot.
		if (Unit.HasItems())
		{
			Unit.bUnseenAtStart = (Unit.Team == 0 || Unit.Team == 1) && !CanSeeUnit(1 - Unit.Team, Unit);
		}
		if (!Camps.empty())
		{
			UseShrine(Unit, Report);
		}

		Unit.bReady = true;
		Unit.Tg = Pace::TgMax;
		++Unit.Serial;
		Unit.Clock = ClockTicks(Unit);
		Unit.bMoved = false;
		Unit.bActed = false;
		Unit.bHustling = false;
		for (int Slot = 0; Slot < AbilitySlots; ++Slot)
		{
			Unit.ToggledTurn[Slot] = false;
			Unit.Cooldowns[Slot] = std::max(0, Unit.Cooldowns[Slot] - 1);
		}
		Unit.Ult = std::min(Pace::UltMax, Unit.Ult + RoundToInt(Tuning.UltPerTurn));

		// Buffs last a number of the unit's own turns.
		std::vector<FBuff> Kept;
		Kept.reserve(Unit.Buffs.size());
		for (FBuff& Buff : Unit.Buffs)
		{
			if (--Buff.Turns > 0)
			{
				Kept.push_back(Buff);
			}
		}
		Unit.Buffs.swap(Kept);
		ApplyAuras(Unit);

		// Channelling: it goes off again, and that is what the turn was for.
		if (Unit.IsChanneling())
		{
			--Unit.Channeling.Turns;
			Report.Say(EEventKind::BecameReady, Unit.Id);
			const int Slot = Unit.Channeling.Slot;
			const FVec2 Where = Unit.Channeling.Target;
			const bool bDone = Unit.Channeling.Turns <= 0;
			ResolveAbility(Unit, Slot, Where, Report);
			if (bDone)
			{
				Unit.Channeling = FChannel();
			}
			if (Unit.IsAlive() && Unit.bReady)
			{
				EndTurnFor(Unit, false, Report);
			}
			return;
		}

		// A status that takes its orders away: the turn it just earned is lost, and
		// the status has counted down for it. Stun is not one of these -- that
		// interrupts the turn a unit is already in.
		if (!BlockedBy.empty() || bZoneLost)
		{
			EndTurn(Unit, true, Report);
			return;
		}

		// Charmed: this is the turn it fights for the other side.
		for (FStatus& Status : Unit.Statuses)
		{
			Status.Amount = Status.Id == "charmed" ? 1 : Status.Amount;
		}

		// A monster's mood, which may spend the turn (the warning it gives when set off).
		if (Unit.bMonster && MonsterTurnStarts(Unit, Report))
		{
			return;
		}

		Report.Say(EEventKind::BecameReady, Unit.Id);
		if (FearOf >= 0)
		{
			Flee(Unit, FearOf, Report);
		}
	}

	FVec2 FBattle::CapturePoint() const
	{
		const FVec2 Size = Map.SizeMeters();
		return FVec2(Size.X * 0.5f, Size.Y * 0.5f);
	}

	double FBattle::HealthShare(int Team) const
	{
		double Alive = 0.0;
		double Total = 0.0;
		for (const FUnit& Unit : Units)
		{
			if (Unit.HomeTeam() == Team && !Unit.bMonster && Unit.PetOf < 0)
			{
				Total += Unit.MaxHp();
				Alive += std::max(0, Unit.Hp);
			}
		}
		return Total > 0.0 ? Alive / Total : 0.0;
	}

	void FBattle::TickCapture(FTickReport& Report)
	{
		// While both sides have somebody there it is contested and neither gains,
		// but nothing is lost either (game_state.gd:1534-1558).
		if (Tuning.CaptureSeconds <= 0.0)
		{
			return;
		}
		const FVec2 Middle = CapturePoint();
		int Standing[2] = { 0, 0 };
		for (const FUnit& Unit : Units)
		{
			// Godot measures in float and compares with the radius as a double.
			if (Unit.IsAlive() && !Unit.bMonster && (Unit.Team == 0 || Unit.Team == 1)
				&& static_cast<double>(Unit.Pos.DistanceTo(Middle)) <= CaptureRadius)
			{
				++Standing[Unit.Team];
			}
		}
		if (Standing[0] > 0 && Standing[1] > 0)
		{
			return;
		}
		const int Needed = RoundToInt(Tuning.CaptureSeconds * Pace::TicksPerSecond);
		for (int Team = 0; Team < 2; ++Team)
		{
			if (Standing[Team] == 0)
			{
				continue;
			}
			++CaptureTicks[Team];
			if (CaptureTicks[Team] >= Needed)
			{
				Winner = Team;
				Report.Say(EEventKind::Won, Winner);
				return;
			}
		}
	}

	void FBattle::PlaceWatchtowers(uint64_t InSeed)
	{
		// Not Godot's (Docs/design/feat-objectives.md). Somewhere new every battle,
		// but fair: each tower on blue's half has its twin on red's, turned about
		// the middle as the ground itself is, so neither side has a nearer one.
		Watchtowers.clear();
		const int Count = std::max(0, std::min(8, RoundToInt(Tuning.WatchtowerCount)));
		if (Count == 0 || Map.TilesX == 0)
		{
			return;
		}
		FSimRandom Placer(InSeed ^ Watchtower::Salt);
		const FVec2 Size = Map.SizeMeters();
		auto Twin = [&Size](const FVec2& Point) { return FVec2(Size.X - Point.X, Size.Y - Point.Y); };

		// Only ground both sides can walk to. Copied, because asking for another
		// distance field may clear the cache this one lives in.
		const std::vector<double> Walk = DistanceFrom(SpawnPoints[0]);
		auto Usable = [&](const FVec2& Point)
		{
			const FNode Node = FMap::NodeOf(Point);
			const int Index = Map.NodeIndex(Node);
			if (!InBounds(Point) || !Map.NodeWalkable(Node) || HazardAt(Point) != 0 || IsCover(Point)
				|| Index < 0 || Index >= static_cast<int>(Walk.size()) || !std::isfinite(Walk[static_cast<size_t>(Index)]))
			{
				return false;
			}
			// Not against the edge of the map, where a tower watches nothing but the
			// way round it (2026-10-02).
			if (static_cast<double>(std::min(std::min(Point.X, Size.X - Point.X), std::min(Point.Y, Size.Y - Point.Y))) < Watchtower::FromEdge)
			{
				return false;
			}
			// Out in the field, not where anybody starts: a tower taken on the
			// first turn without a fight is no choice at all.
			for (int Team = 0; Team < 2; ++Team)
			{
				if (static_cast<double>(Point.DistanceTo(SpawnPoints[Team])) < Watchtower::AwayFromStart)
				{
					return false;
				}
			}
			for (const FUnit& Unit : Units)
			{
				if (static_cast<double>(Point.DistanceTo(Unit.Pos)) < Watchtower::AwayFromStart)
				{
					return false;
				}
			}
			for (const FWatchtower& Other : Watchtowers)
			{
				if (static_cast<double>(Point.DistanceTo(Other.Pos)) < Watchtower::Apart)
				{
					return false;
				}
			}
			return true;
		};

		// An odd one stands in the middle, where it is its own twin, if the ground
		// there will have it; otherwise the battle has one fewer.
		if (Count % 2 == 1)
		{
			const FVec2 Middle = CapturePoint();
			if (Usable(Middle))
			{
				FWatchtower Tower;
				Tower.Pos = Middle;
				Watchtowers.push_back(Tower);
			}
		}

		// The pairs: every tile of blue's half, shuffled, and the first that fit.
		// Tiles in row order before the shuffle, so the same seed always picks the
		// same ones.
		std::vector<FVec2> Tiles;
		for (int Y = 0; Y < Map.TilesY / 2; ++Y)
		{
			for (int X = 0; X < Map.TilesX; ++X)
			{
				Tiles.emplace_back((static_cast<float>(X) + 0.5f) * Ground::TileSize, (static_cast<float>(Y) + 0.5f) * Ground::TileSize);
			}
		}
		for (int i = static_cast<int>(Tiles.size()) - 1; i > 0; --i)
		{
			const int j = static_cast<int>(Placer.RandiRange(0, i));
			std::swap(Tiles[static_cast<size_t>(i)], Tiles[static_cast<size_t>(j)]);
		}
		const int Pairs = Count / 2;
		int Placed = 0;
		for (const FVec2& Point : Tiles)
		{
			if (Placed >= Pairs)
			{
				break;
			}
			// A pair's two not side by side across the middle either. Everything
			// else about the twin follows from the ground being turned about too.
			if (!Usable(Point) || static_cast<double>(Point.DistanceTo(Twin(Point))) < Watchtower::Apart)
			{
				continue;
			}
			FWatchtower Blue;
			Blue.Pos = Point;
			FWatchtower Red;
			Red.Pos = Twin(Point);
			Watchtowers.push_back(Blue);
			Watchtowers.push_back(Red);
			++Placed;
		}
	}

	void FBattle::ApplyCapture(FUnit& Unit, int Tower, FTickReport& Report)
	{
		CaptureProgress(Unit, Tower, Report);

		// It costs the turn: the action, and the end of it. A unit that walked
		// there first has used the whole turn, and keeps the gauge of one that did.
		// The end of the turn would count the tower again (AutoCapture): not twice.
		Unit.bActed = true;
		bCaptureOrder = true;
		EndTurn(Unit, false, Report);
		bCaptureOrder = false;
	}

	void FBattle::AutoCapture(FUnit& Unit, FTickReport& Report)
	{
		// Only a side's own units (not a monster, not a pet), alive, at a tower
		// its side doesn't hold, on ground it could step to from the tower's, with
		// no enemy at it: what ValidateCapture asks, less the turn's action.
		if (bCaptureOrder || Watchtowers.empty() || !Unit.IsAlive() || Unit.IsStunned() || Unit.bMonster || Unit.PetOf >= 0
			|| (Unit.Team != 0 && Unit.Team != 1))
		{
			return;
		}
		const int Tower = TowerNear(Unit.Pos);
		if (Tower < 0)
		{
			return;
		}
		const FWatchtower& Held = Watchtowers[static_cast<size_t>(Tower)];
		if (Held.Owner == Unit.Team || std::abs(LevelAt(Unit.Pos) - LevelAt(Held.Pos)) > Ground::Jump)
		{
			return;
		}
		for (const FUnit& Other : Units)
		{
			if (Other.IsAlive() && Other.Team != Unit.Team
				&& static_cast<double>(Other.Pos.DistanceTo(Held.Pos)) <= Watchtower::Reach)
			{
				return;
			}
		}
		CaptureProgress(Unit, Tower, Report);
	}

	void FBattle::CaptureProgress(FUnit& Unit, int Tower, FTickReport& Report)
	{
		FWatchtower& Held = Watchtowers[static_cast<size_t>(Tower)];
		// Progress belongs to one side at a time: the other side starting on a
		// tower wipes out whatever the first had put in.
		if (Held.Capturer != Unit.Team)
		{
			Held.Capturer = Unit.Team;
			Held.Progress = 0;
		}
		++Held.Progress;
		const int Needed = CaptureTurnsNeeded();

		FEvent Spent;
		Spent.Kind = EEventKind::Capturing;
		Spent.Unit = Unit.Id;
		Spent.Slot = Tower;
		Spent.Amount = Held.Progress;
		Spent.By = Needed;
		Spent.Where = Held.Pos;
		Report.Events.push_back(Spent);

		if (Held.Progress >= Needed)
		{
			Held.Owner = Unit.Team;
			Held.Capturer = -1;
			Held.Progress = 0;
			FEvent Taken;
			Taken.Kind = EEventKind::Captured;
			Taken.Unit = Unit.Id;
			Taken.Slot = Tower;
			Taken.By = Unit.Team;
			Taken.Where = Held.Pos;
			Report.Events.push_back(Taken);
		}
	}

	void FBattle::FinishOnTime(FTickReport& Report)
	{
		// Level to within a point of a percent is a draw (game_state.gd:1562-1569).
		const double Blue = HealthShare(0);
		const double Red = HealthShare(1);
		if (std::fabs(Blue - Red) < 0.01)
		{
			Winner = Draw;
		}
		else
		{
			Winner = Blue > Red ? 0 : 1;
		}
		Report.Say(EEventKind::Won, Winner);
	}

	void FBattle::ApplyAuras(FUnit& Unit)
	{
		// Every living unit's auras, in unit order, that are meant for this one's
		// side (or against it) and stand close enough. Each tops up its buffs for
		// two turns: long enough to last until the next turn begins and the aura
		// is asked again, so standing in one keeps it and walking off loses it a
		// turn later (game_state.gd:1305-1334). The owner is in its own aura.
		for (const FUnit& Source : Units)
		{
			if (!Source.IsAlive())
			{
				continue;
			}
			for (int Slot = 0; Slot < ClassSlots; ++Slot)
			{
				const FAbility* Ability = Source.Ability(Slot);
				if (!Ability || Ability->Kind != "aura")
				{
					continue;
				}
				const bool bWantsEnemy = Ability->Target == ETargetSide::Enemy;
				if ((Source.Team != Unit.Team) != bWantsEnemy)
				{
					continue;
				}
				// Godot measures in float (Vector2) and compares against the aoe
				// as a GDScript float, a double, with a reach of at least a metre.
				const double Reach = std::max(static_cast<double>(Ability->Aoe), 1.0);
				if (static_cast<double>(Source.Pos.DistanceTo(Unit.Pos)) > Reach)
				{
					continue;
				}
				for (const FBuff& Given : Ability->Buffs)
				{
					bool bFound = false;
					for (FBuff& Existing : Unit.Buffs)
					{
						// Every one that matches is refreshed, as Godot does,
						// rather than the first.
						if (Existing.Stat == Given.Stat && Existing.Aura == Ability->Name)
						{
							Existing.Turns = 2;
							bFound = true;
						}
					}
					if (!bFound)
					{
						FBuff Copy = Given;
						Copy.Turns = 2;
						Copy.Aura = Ability->Name;
						Unit.Buffs.push_back(Copy);
					}
				}
				if (Ability->HasStatus())
				{
					AddStatus(Unit, Ability->StatusId, Ability->StatusTurns);
				}
			}
		}
	}

	void FBattle::EndTurnFor(FUnit& Unit, bool bTimedOut, FTickReport& Report)
	{
		EndTurn(Unit, bTimedOut, Report);
	}

	void FBattle::EndTurn(FUnit& Unit, bool bTimedOut, FTickReport& Report)
	{
		// Standing at a watchtower as the turn ends puts the turn into it (v20 play test).
		AutoCapture(Unit, Report);
		// Contagion (2026-10-05): a carrier ending its turn among its allies passes it on.
		if (!Unit.Statuses.empty() && Unit.HasStatus("plague"))
		{
			SpreadPlague(Unit, Report);
		}

		// A turn lost to the countdown stops a channel; giving the turn up on
		// purpose does not, because carrying on is the whole point of one.
		if (bTimedOut && Unit.IsChanneling())
		{
			Unit.Channeling = FChannel();
		}

		// What it kept of its gauge says what it did with the turn. Doing nothing
		// leaves it nearest its next one, which is what makes waiting a choice
		// rather than a punishment.
		if (bTimedOut)
		{
			Unit.Tg = 0;
			Report.Say(EEventKind::TimedOut, Unit.Id);
		}
		else if (Unit.bMoved && Unit.bActed)
		{
			Unit.Tg = 0;
		}
		else if (Unit.bMoved || Unit.bActed)
		{
			Unit.Tg = Pace::TgKeepOne;
		}
		else
		{
			Unit.Tg = Pace::TgKeepNone;
		}

		// Momentum Charm: gauge back for what it knocked out this turn.
		if (Unit.KillTgPercent > 0)
		{
			Unit.Tg = std::min(Pace::TgMax - 1, Unit.Tg + Pace::TgMax * Unit.KillTgPercent / 100);
			Unit.KillTgPercent = 0;
		}
		Unit.bStillLastTurn = !Unit.bMoved;

		// Held its ability back: the gauge fills faster until its next turn.
		Unit.bHustling = !Unit.bActed && !bTimedOut;
		Unit.bReady = false;
		Unit.Clock = 0;
		Unit.WalkVia.clear();
		Unit.bMoved = false;
		Unit.bActed = false;
		Report.Say(EEventKind::TurnEnded, Unit.Id);

		// Relentless: straight back round again, and it is spent doing so. Ported
		// with the rest of EndTurn for fidelity, but nothing exercises it until
		// abilities can apply a status.
		bool bGoesAgain = false;
		std::vector<FStatus> Kept;
		Kept.reserve(Unit.Statuses.size());
		for (const FStatus& Status : Unit.Statuses)
		{
			const FStatusDef* Def = FindStatus(Status.Id);
			if (Def && Def->bExtraTurn)
			{
				bGoesAgain = true;
			}
			else
			{
				Kept.push_back(Status);
			}
		}
		if (bGoesAgain)
		{
			Unit.Statuses.swap(Kept);
			Unit.Tg = Pace::TgMax - 1;
		}

		// Charmed: its turn for the other side is over.
		if (Unit.CharmedFrom >= 0)
		{
			for (const FStatus& Status : Unit.Statuses)
			{
				if (Status.Id == "charmed" && Status.Amount >= 1)
				{
					RemoveStatus(Unit, "charmed");
					ReleaseCharm(Unit, &Report);
					break;
				}
			}
		}
	}
}
