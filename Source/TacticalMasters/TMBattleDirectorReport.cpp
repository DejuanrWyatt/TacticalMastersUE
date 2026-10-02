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
	/** Points per thing done (the human's weights, 2026-10-02). */
	constexpr double PerDamage = 0.1;
	constexpr double PerTaken = 0.04;
	constexpr double PerMitigated = 0.03;
	constexpr double PerHealing = 0.1;
	constexpr double PerKill = 12.0;
	constexpr double PerAssist = 5.0;
	constexpr double PerDeath = -10.0;
	constexpr double PerMonster = 4.0;
	constexpr double PerBoss = 15.0;
	constexpr double PerBuff = 2.0;
	constexpr double PerDebuff = 2.0;
	constexpr double PerControl = 3.0;
	constexpr double PerRevive = 10.0;
	constexpr double PerTower = 8.0;
	/** An assist is help in the minute before the fall. */
	constexpr int32 AssistTicks = 60 * TMSim::Pace::TicksPerSecond;

	bool IsControl(const TMSim::FStatusDef* Def)
	{
		return Def && (Def->bNoOrders || Def->bInterrupt || Def->bTaunt || Def->bNoMove);
	}
}

double ATMBattleDirector::ScoreOf(const FTMUnitTally& T)
{
	using namespace TMReport;
	return T.Damage * PerDamage + T.Taken * PerTaken + T.Mitigated * PerMitigated + T.Healing * PerHealing
		+ T.Kills * PerKill + T.Assists * PerAssist + T.Deaths * PerDeath + T.Monsters * PerMonster + T.Bosses * PerBoss
		+ T.Buffs * PerBuff + T.Debuffs * PerDebuff + T.Control * PerControl + T.Revives * PerRevive + T.Towers * PerTower;
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
			PendingSoak.FindOrAdd(Event.Unit) += Event.Amount;
			break;
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
					break;
				}
				if (Event.Id == "mend" || Event.Id == "ground")
				{
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
					T->Biggest = FMath::Max(T->Biggest, Amount);
					const FString Name = Ability ? FString(UTF8_TO_TCHAR(Ability->Name.c_str())) : FString(UTF8_TO_TCHAR(Event.Id.c_str())).Left(1).ToUpper() + FString(UTF8_TO_TCHAR(Event.Id.c_str())).Mid(1);
					T->ByAbility.FindOrAdd(Name) += Amount;
				}
			}
			if (By >= 0 && Amount > 0)
			{
				LastHurtBy.Add(Event.Unit, By);
				Helped(Event.Unit, By);
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
					T->Control += CastAbility && CastAbility->HasStatus() && CastAbility->StatusId == Event.Id ? FMath::Max(1, CastAbility->StatusTurns) : 1;
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
				}
			}
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
