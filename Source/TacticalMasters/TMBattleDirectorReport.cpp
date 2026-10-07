// The battle report: what each unit did, tallied from the battle's own events
// as they happen, and its points for the MVP ("Battle Report Mockups";
// Docs/design/feat-battle-report.md).
//
// Only events are read, never anything the view decides, so a replay of the
// battle tallies exactly the same numbers. Where the rules don't say a number
// outright -- how much damage armor took off, who healed whom -- it is worked
// out from the event and the units' state at that moment.

#include "TMBattleDirector.h"

#include "SimAbility.h"

namespace TMReport
{
	// Points per thing done. The human's weights (2026-10-02), revalued after the
	// v20 play test (2026-10-04, "more accurately depict the value of each unit"):
	// - a takedown is 15 points shared by everyone who hurt or held the fallen unit
	//   in the minute before, by their share (a turn of control counts as 15% of
	//   its health in damage), and 3 more for the last blow; it used to be 12 to the
	//   last blow and 5 to anyone else, so whoever stole the kill was the MVP;
	// - damage to monsters is worth 0.4 a 10 against the other side's 1 a 10;
	// - simply being hit is worth little (0.2 a 10, was 0.4); what armour, dodges,
	//   wards and shields stopped is worth more (0.5 a 10, was 0.3), and damage
	//   taken in an ally's place (Guard) another 0.5 a 10: a tank's work;
	// - shields put on allies count as healing, for what they soaked;
	// - a turn of control 4 (was 3), a revive 12 (was 10), knocked out -8 (was -10).
	constexpr double PerDamage = 0.1;
	constexpr double PerMonsterDamage = 0.04;
	constexpr double PerTaken = 0.02;
	constexpr double PerMitigated = 0.05;
	constexpr double PerGuarded = 0.05;
	constexpr double PerHealing = 0.1;
	constexpr double PerShielding = 0.1;
	constexpr double TakedownPool = 15.0;
	constexpr double LastBlow = 3.0;
	constexpr double ControlAsHealth = 0.15;
	constexpr double PerDeath = -8.0;
	constexpr double PerMonster = 4.0;
	constexpr double PerBoss = 15.0;
	constexpr double PerBuff = 2.0;
	constexpr double PerDebuff = 2.0;
	constexpr double PerControl = 4.0;
	constexpr double PerRevive = 12.0;
	constexpr double PerTower = 8.0;
	/** An assist is help in the minute before the fall. */
	constexpr int32 AssistTicks = 60 * TMSim::Pace::TicksPerSecond;

	bool IsControl(const TMSim::FStatusDef* Def)
	{
		return Def && (Def->bNoOrders || Def->bInterrupt || Def->bTaunt || Def->bNoMove);
	}
}

TArray<ATMBattleDirector::FTMValuePart> ATMBattleDirector::ValueParts(const FTMUnitTally& T)
{
	using namespace TMReport;
	const int32 OnSides = FMath::Max(0, T.Damage - T.MonsterDamage);
	return {
		{ TEXT("Damage"), OnSides * PerDamage },
		{ TEXT("Monster damage"), T.MonsterDamage * PerMonsterDamage },
		{ TEXT("Takedowns"), T.Takedowns },
		{ TEXT("Damage taken"), T.Taken * PerTaken },
		{ TEXT("Mitigated"), T.Mitigated * PerMitigated },
		{ TEXT("Guarding"), T.Guarded * PerGuarded },
		{ TEXT("Healing"), T.Healing * PerHealing },
		{ TEXT("Shielding"), T.Shielding * PerShielding },
		{ TEXT("Knocked out"), T.Deaths * PerDeath },
		{ TEXT("Monsters"), T.Monsters * PerMonster },
		{ TEXT("Boss"), T.Bosses * PerBoss },
		{ TEXT("Buffs"), T.Buffs * PerBuff },
		{ TEXT("Debuffs"), T.Debuffs * PerDebuff },
		{ TEXT("Control"), T.Control * PerControl },
		{ TEXT("Revives"), T.Revives * PerRevive },
		{ TEXT("Towers"), T.Towers * PerTower } };
}

FString ATMBattleDirector::ValueRules()
{
	return TEXT("Points: 1 per 10 damage to the other side, 0.4 per 10 to monsters; each enemy that falls is worth 15, shared by everyone who hurt or held it in the minute before (a turn of control counts as 15% of its health), and 3 more for the last blow; 0.2 per 10 taken, 0.5 per 10 mitigated, 0.5 per 10 taken for an ally (Guard); 1 per 10 healed or soaked by your shields on allies; -8 knocked out; 4 a monster, 15 a boss; 2 a buff or debuff, 4 a turn of control, 12 a revive, 8 a tower.");
}

double ATMBattleDirector::ScoreOf(const FTMUnitTally& T)
{
	double Sum = 0.0;
	for (const FTMValuePart& Part : ValueParts(T))
	{
		Sum += Part.Points;
	}
	return Sum;
}

int32 ATMBattleDirector::MvpId() const
{
	int32 Best = -1;
	for (const TPair<int32, FTMUnitTally>& Pair : Tallies)
	{
		if (Best < 0)
		{
			Best = Pair.Key;
			continue;
		}
		const FTMUnitTally& B = Tallies[Best];
		const double Mine = FMath::RoundToDouble(ScoreOf(Pair.Value) * 10.0);
		const double Theirs = FMath::RoundToDouble(ScoreOf(B) * 10.0);
		if (Mine > Theirs || (Mine == Theirs && (Pair.Value.Deaths < B.Deaths
			|| (Pair.Value.Deaths == B.Deaths && Pair.Value.Damage > B.Damage))))
		{
			Best = Pair.Key;
		}
	}
	return Best;
}

void ATMBattleDirector::ResetTallies()
{
	Tallies.Reset();
	for (const TMSim::FUnit& Unit : Battle.Units)
	{
		if (!Unit.bMonster && (Unit.Team == 0 || Unit.Team == 1))
		{
			Tallies.Add(Unit.Id, FTMUnitTally());
		}
	}
	TallyHp.Reset();
	LastHurtBy.Reset();
	RecentHurt.Reset();
	HelpedAgainst.Reset();
	StatusFrom.Reset();
	PendingSoak.Reset();
	PendingGraze.Reset();
	PendingGuard.Reset();
	ReportTab = 0;
	ReportUnit = -1;
	bReportHidden = false;
}

void ATMBattleDirector::SnapshotForTally()
{
	TallyHp.Reset();
	for (const TMSim::FUnit& Unit : Battle.Units)
	{
		TallyHp.Add(Unit.Id, Unit.Hp);
	}
}

void ATMBattleDirector::TallyEvents(const TMSim::FTickReport& Report)
{
	using namespace TMReport;
	const int32 Now = Battle.TickCount;
	auto TallyOf = [this](int32 Id) { return Tallies.Find(Id); };
	auto TeamOf = [this](int32 Id) { const TMSim::FUnit* Unit = Battle.FindUnit(Id); return Unit ? Unit->Team : -1; };
	auto Helped = [this, Now](int32 Victim, int32 Helper)
	{
		if (Victim != Helper && Helper >= 0)
		{
			HelpedAgainst.FindOrAdd(Victim).Add(Helper, Now);
		}
	};
	// Who cast what this report, for revives and for a status's length.
	int32 Caster = -1;
	const TMSim::FAbility* CastAbility = nullptr;
	PendingSoak.Reset();
	PendingGraze.Reset();
	PendingGuard.Reset();

	for (const TMSim::FEvent& Event : Report.Events)
	{
		switch (Event.Kind)
		{
		case TMSim::EEventKind::Resolved:
			Caster = Event.Unit;
			CastAbility = TMSim::FindAbility(Event.Id);
			break;
		case TMSim::EEventKind::Absorbed:
		{
			PendingSoak.FindOrAdd(Event.Unit) += Event.Amount;
			// A shield put on an ally: what it soaked is its caster's, like healing.
			const int32* From = StatusFrom.Find(FString::Printf(TEXT("%d:%hs"), Event.Unit, Event.Id.c_str()));
			if (From && *From != Event.Unit && TeamOf(*From) == TeamOf(Event.Unit))
			{
				if (FTMUnitTally* T = TallyOf(*From))
				{
					T->Shielding += Event.Amount;
				}
			}
			break;
		}
		case TMSim::EEventKind::Grazed:
			PendingGraze.Add(Event.Unit);
			break;
		case TMSim::EEventKind::Redirected:
			if (Event.Id == "guard")
			{
				PendingGuard.Add(Event.Unit);
			}
			break;
		case TMSim::EEventKind::Critical:
			if (FTMUnitTally* T = TallyOf(Event.By))
			{
				++T->Crits;
			}
			break;
		case TMSim::EEventKind::Evaded:
		{
			// Dodged outright: all of what it would have done was stopped.
			const TMSim::FUnit* Struck = Battle.FindUnit(Event.Unit);
			const TMSim::FUnit* User = Battle.FindUnit(Event.By);
			const TMSim::FAbility* Ability = TMSim::FindAbility(Event.Id);
			FTMUnitTally* T = TallyOf(Event.Unit);
			if (T && Struck && User && Ability)
			{
				T->Mitigated += Battle.CalcAmount(*User, *Ability, User->Pos, *Struck, Struck->Pos, 1, 1);
			}
			break;
		}
		case TMSim::EEventKind::Hit:
		{
			const TMSim::FAbility* Ability = TMSim::FindAbility(Event.Id);
			const TMSim::FUnit* Struck = Battle.FindUnit(Event.Unit);
			if (!Struck)
			{
				break;
			}
			// Whose it was: the caster, or for a status's tick whoever put it there.
			int32 By = Event.By;
			if (By < 0)
			{
				if (const int32* From = StatusFrom.Find(FString::Printf(TEXT("%d:%hs"), Event.Unit, Event.Id.c_str())))
				{
					By = *From;
				}
			}
			if (Ability && Ability->Effect == TMSim::EEffect::Heal)
			{
				// Only what was missing counts.
				int32& Hp = TallyHp.FindOrAdd(Event.Unit);
				const int32 Healed = FMath::Clamp(Event.Amount, 0, FMath::Max(0, Struck->MaxHp() - Hp));
				Hp += Healed;
				if (FTMUnitTally* T = TallyOf(By))
				{
					T->Healing += Healed;
				}
				if (FTMUnitTally* T = TallyOf(Event.Unit))
				{
					T->HealingReceived += Healed;
				}
				break;
			}
			if (Ability && Ability->Effect != TMSim::EEffect::Damage)
			{
				break;  // a revive or a support: nothing taken
			}
			if (!Ability)
			{
				// Regen's ticks are its caster's healing, already only what was missing
				// (SimBattle.cpp); mending and the ground belong to nobody.
				if (Event.Id == "regen")
				{
					if (FTMUnitTally* T = TallyOf(By))
					{
						T->Healing += Event.Amount;
					}
					if (FTMUnitTally* T = TallyOf(Event.Unit))
					{
						T->HealingReceived += Event.Amount;
					}
					break;
				}
				// The new spells (2026-10-05): a tether's drain and a spirit swap's gain are health taken in.
				if (Event.Id == "drain" || Event.Id == "spirit_up")
				{
					if (int32* Hp = TallyHp.Find(Event.Unit))
					{
						*Hp = FMath::Min(Struck->MaxHp(), *Hp + Event.Amount);
					}
					if (FTMUnitTally* T = TallyOf(Event.Unit))
					{
						T->HealingReceived += Event.Amount;
					}
					break;
				}
				if (Event.Id == "mend" || Event.Id == "ground")
				{
					// Nobody's healing to give, but the unit took it in. "ground" is
					// burning ground's hurt too: a spring's is the one on a spring,
					// unless Decay turned it to harm (SimBattle.cpp, GroundEffect).
					const bool bHealed = Event.Id == "mend"
						|| (Battle.HazardAt(Event.Where) > 0 && !Struck->HasStatus("decay"));
					if (FTMUnitTally* T = TallyOf(Event.Unit))
					{
						T->HealingReceived += bHealed ? Event.Amount : 0;
					}
					break;
				}
			}
			const int32 Soaked = PendingSoak.Contains(Event.Unit) ? PendingSoak[Event.Unit] : 0;
			PendingSoak.Remove(Event.Unit);
			const int32 Amount = Event.Amount;
			if (int32* Hp = TallyHp.Find(Event.Unit))
			{
				*Hp = FMath::Max(0, *Hp - Amount);
			}
			// Struck: what it took, what never landed, and who did it.
			if (FTMUnitTally* T = TallyOf(Event.Unit))
			{
				T->Taken += Amount;
				if (By >= 0)
				{
					T->TakenFrom.FindOrAdd(By) += Amount;
				}
				int32 Stopped = Soaked;
				if (Ability)
				{
					// Armor or Resist took Defence / (30 + Defence) of what came through it.
					const int32 Defence = FMath::Max(0, Struck->Stat(Ability->Scale == TMSim::EScale::Att ? TMSim::EStat::AttDef : TMSim::EStat::MagDef));
					const double Scale = FMath::Max(1.0, Battle.Tuning.DefenseScale);
					Stopped += FMath::RoundToInt((Amount + Soaked) * Defence / Scale);
					// A graze landed half: the other half was stopped.
					if (PendingGraze.Contains(Event.Unit))
					{
						Stopped += Amount + Soaked;
					}
					// Protect or Shell took a third.
					const bool bWard = (Ability->Scale == TMSim::EScale::Att && Struck->HasStatus("protect"))
						|| (Ability->Scale == TMSim::EScale::Mag && Struck->HasStatus("shell"));
					if (bWard)
					{
						Stopped += FMath::RoundToInt((Amount + Soaked) * 0.33 / 0.67);
					}
				}
				T->Mitigated += Stopped;
				if (PendingGuard.Contains(Event.Unit))
				{
					T->Guarded += Amount;
				}
			}
			PendingGraze.Remove(Event.Unit);
			PendingGuard.Remove(Event.Unit);
			// The one who did it: damage to the other side or to monsters only.
			const int32 ByTeam = TeamOf(By);
			if (FTMUnitTally* T = TallyOf(By))
			{
				if (ByTeam != Struck->Team)
				{
					T->Damage += Amount;
					T->MonsterDamage += Struck->bMonster ? Amount : 0;
					T->Biggest = FMath::Max(T->Biggest, Amount);
					const FString Name = Ability ? FString(UTF8_TO_TCHAR(Ability->Name.c_str())) : FString(UTF8_TO_TCHAR(Event.Id.c_str())).Left(1).ToUpper() + FString(UTF8_TO_TCHAR(Event.Id.c_str())).Mid(1);
					T->ByAbility.FindOrAdd(Name) += Amount;
				}
			}
			if (By >= 0 && Amount > 0)
			{
				LastHurtBy.Add(Event.Unit, By);
				Helped(Event.Unit, By);
				RecentHurt.FindOrAdd(Event.Unit).Add({ By, Now, Amount, 0 });
			}
			break;
		}
		case TMSim::EEventKind::StatusApplied:
		{
			const TMSim::FStatusDef* Def = TMSim::FindStatus(Event.Id);
			const int32 By = Event.By;
			StatusFrom.Add(FString::Printf(TEXT("%d:%hs"), Event.Unit, Event.Id.c_str()), By);
			FTMUnitTally* T = TallyOf(By);
			if (!T || By == Event.Unit)
			{
				break;
			}
			if (TeamOf(By) == TeamOf(Event.Unit))
			{
				++T->Buffs;
			}
			else
			{
				++T->Debuffs;
				Helped(Event.Unit, By);
				if (IsControl(Def))
				{
					// Turns of it, as the ability gave it.
					const int32 Turns = CastAbility && CastAbility->HasStatus() && CastAbility->StatusId == Event.Id ? FMath::Max(1, CastAbility->StatusTurns) : 1;
					T->Control += Turns;
					RecentHurt.FindOrAdd(Event.Unit).Add({ By, Now, 0, Turns });
				}
			}
			break;
		}
		case TMSim::EEventKind::Knocked:
		{
			const TMSim::FUnit* Fallen = Battle.FindUnit(Event.Unit);
			if (!Fallen)
			{
				break;
			}
			const int32* KillerAt = LastHurtBy.Find(Event.Unit);
			const int32 Killer = KillerAt ? *KillerAt : -1;
			if (Fallen->bMonster)
			{
				if (FTMUnitTally* T = TallyOf(Killer))
				{
					if (Fallen->Job == Battle.BossJob)
					{
						++T->Bosses;
					}
					else
					{
						++T->Monsters;
					}
				}
				RecentHurt.Remove(Event.Unit);
				break;
			}
			if (FTMUnitTally* T = TallyOf(Event.Unit))
			{
				++T->Deaths;
			}
			if (FTMUnitTally* T = TallyOf(Killer))
			{
				if (TeamOf(Killer) != Fallen->Team)
				{
					++T->Kills;
					T->Takedowns += LastBlow;
				}
			}
			// The takedown's points, shared by the other side's units by what they did
			// to it in the last minute: damage, and turns of control as a share of its health.
			{
				TMap<int32, double> Shares;
				double Whole = 0.0;
				if (const TArray<FTMHurt>* Hurts = RecentHurt.Find(Event.Unit))
				{
					for (const FTMHurt& Hurt : *Hurts)
					{
						if (Now - Hurt.Tick <= AssistTicks && TeamOf(Hurt.By) != Fallen->Team && Tallies.Contains(Hurt.By))
						{
							const double Weight = Hurt.Amount + Hurt.ControlTurns * ControlAsHealth * Fallen->MaxHp();
							Shares.FindOrAdd(Hurt.By) += Weight;
							Whole += Weight;
						}
					}
				}
				if (Whole <= 0.0 && Killer >= 0 && TeamOf(Killer) != Fallen->Team && Tallies.Contains(Killer))
				{
					Shares.Add(Killer, 1.0);
					Whole = 1.0;
				}
				for (const TPair<int32, double>& Share : Shares)
				{
					if (FTMUnitTally* T = TallyOf(Share.Key))
					{
						T->Takedowns += TakedownPool * Share.Value / Whole;
					}
				}
			}
			RecentHurt.Remove(Event.Unit);
			// Everyone on the other side who hurt, debuffed or held it in the last minute.
			if (const TMap<int32, int32>* Helpers = HelpedAgainst.Find(Event.Unit))
			{
				for (const TPair<int32, int32>& Helper : *Helpers)
				{
					if (Helper.Key != Killer && Now - Helper.Value <= AssistTicks && TeamOf(Helper.Key) != Fallen->Team)
					{
						if (FTMUnitTally* T = TallyOf(Helper.Key))
						{
							++T->Assists;
						}
					}
				}
			}
			HelpedAgainst.Remove(Event.Unit);
			break;
		}
		case TMSim::EEventKind::Revived:
			if (Event.Id != "reraise" && Caster >= 0 && Caster != Event.Unit)
			{
				if (FTMUnitTally* T = TallyOf(Caster))
				{
					++T->Revives;
				}
			}
			break;
		case TMSim::EEventKind::Captured:
			if (FTMUnitTally* T = TallyOf(Event.Unit))
			{
				++T->Towers;
			}
			break;
		default:
			break;
		}
	}

	// Stat buffs and cuts have no event of their own: the ability's hits carry them.
	for (const TMSim::FEvent& Event : Report.Events)
	{
		if (Event.Kind != TMSim::EEventKind::Hit || Event.By < 0 || Event.By == Event.Unit)
		{
			continue;
		}
		const TMSim::FAbility* Ability = TMSim::FindAbility(Event.Id);
		FTMUnitTally* T = TallyOf(Event.By);
		if (!Ability || !T || Ability->Buffs.empty())
		{
			continue;
		}
		const bool bAlly = TeamOf(Event.By) == TeamOf(Event.Unit);
		for (const TMSim::FBuff& Buff : Ability->Buffs)
		{
			if (bAlly && Buff.Amount > 0)
			{
				++T->Buffs;
			}
			else if (!bAlly && Buff.Amount < 0)
			{
				++T->Debuffs;
				Helped(Event.Unit, Event.By);
			}
		}
	}
}
