// The battle HUD's panels: the turn order, the log, the unit cards, the list of
// every unit, the corner buttons, the Unit Guide and the tooltips that explain
// the numbers. The frame, the board markings, the action bar and the menus are
// in TMBattleHud.cpp.

#include "TMBattleHud.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

#include "TMBattleDirector.h"
#include "TMBattleHudStyle.h"
#include "SimAbility.h"

#include <algorithm>
#include <vector>

using namespace TMHudStyle;

namespace
{
	/** A number the way Godot's _n prints one: no trailing zeros. */
	FString Num(double Value)
	{
		FString Out = FString::Printf(TEXT("%.2f"), Value);
		while (Out.Contains(TEXT(".")) && (Out.EndsWith(TEXT("0")) || Out.EndsWith(TEXT("."))))
		{
			Out.LeftChopInline(1, EAllowShrinking::No);
		}
		return Out;
	}

	/** Seconds until something next happens to this unit (game_state.gd:750-755, _ticks_left). */
	float SecondsLeft(const TMSim::FUnit& Unit, const TMSim::FBattle& Battle)
	{
		if (Unit.bReady)
		{
			return Unit.Clock / Tps;
		}
		if (Unit.IsCasting())
		{
			return Unit.Casting.Ticks / Tps;
		}
		return Battle.TicksToReady(Unit) / Tps;
	}

	/** What an ability is for, and its colour (jobs.gd:345-366). */
	FLinearColor AbilityClassColour(const TMSim::FAbility& Ability, FString* Name = nullptr)
	{
		auto Say = [Name](const TCHAR* What) { if (Name) { *Name = What; } };
		switch (Ability.Effect)
		{
		case TMSim::EEffect::Damage:
			if (Ability.Scale == TMSim::EScale::Att)
			{
				Say(TEXT("Physical attack"));
				return FLinearColor(1.0f, 0.55f, 0.45f);
			}
			Say(TEXT("Magic attack"));
			return FLinearColor(0.75f, 0.6f, 1.0f);
		case TMSim::EEffect::Heal:
		case TMSim::EEffect::Revive:
			Say(TEXT("Healing"));
			return FLinearColor(0.45f, 0.95f, 0.5f);
		default:
			break;
		}
		if (!Ability.Buffs.empty() || Ability.HasStatus() || Ability.TgChange != 0)
		{
			Say(TEXT("Buff / debuff"));
			return FLinearColor(0.45f, 0.8f, 1.0f);
		}
		Say(TEXT("Utility"));
		return FLinearColor(0.85f, 0.85f, 0.9f);
	}

	/**
	 * The units whose spell in flight would catch this one where it stands
	 * (hud.gd:1123-1150). A cast names the spot it will land on, or the unit it
	 * follows, so anyone inside its shape is about to be caught.
	 */
	std::vector<const TMSim::FUnit*> CastersAt(const TMSim::FBattle& Battle, const TMSim::FUnit& Unit)
	{
		std::vector<const TMSim::FUnit*> Out;
		for (const TMSim::FUnit& Caster : Battle.Units)
		{
			if (&Caster == &Unit || !Caster.IsAlive() || !Caster.IsCasting())
			{
				continue;
			}
			const TMSim::FAbility* Ability = TMSim::JobAbility(Caster.Job, Caster.Casting.Slot);
			if (!Ability)
			{
				continue;
			}
			TMSim::FVec2 Aim = Caster.Casting.Target;
			if (Caster.Casting.FollowId >= 0)
			{
				for (const TMSim::FUnit& Followed : Battle.Units)
				{
					if (Followed.Id == Caster.Casting.FollowId)
					{
						Aim = Followed.Pos;
					}
				}
			}
			if (Battle.InShape(*Ability, Caster.Pos, Aim, Unit.Pos))
			{
				Out.push_back(&Caster);
			}
		}
		return Out;
	}
}

// ------------------------------------------------------------------ words

void ATMBattleHud::AddTip(float X, float Y, float W, float H, const FString& Tip)
{
	FTMHudTip Entry;
	Entry.Area = FBox2D(FVector2D(X, Y), FVector2D(X + W, Y + H));
	Entry.Text = Tip;
	Tips.Add(Entry);
}

FString ATMBattleHud::ExplainTurn(ATMBattleDirector& From, const TMSim::FUnit& Unit) const
{
	// game_state.gd:952-958.
	const TMSim::FBattle& Battle = From.Battle;
	const int32 Gain = FMath::Max(1, Battle.BaseTgGain(Unit));
	const int32 Ticks = FMath::DivideAndRoundUp(TMSim::Pace::TgMax, Gain);
	FString Text = FString::Printf(TEXT("TG per tick = Speed %d x %d x Speed multiplier %s = %d\n"),
		Unit.Stat(TMSim::EStat::Speed), TMSim::Pace::TgPerSpeed, *Num(Battle.Tuning.SpeedMultiplier), Gain);
	Text += FString::Printf(TEXT("Full gauge %d / %d = %d ticks = %s s between turns"),
		TMSim::Pace::TgMax, Gain, Ticks, *Num(Ticks / Tps));
	if (Unit.TgFactor() != 1.0f)
	{
		Text += FString::Printf(TEXT("\nStatuses: x %s"), *Num(Unit.TgFactor()));
	}
	return Text;
}

FString ATMBattleHud::ExplainCountdown(ATMBattleDirector& From, const TMSim::FUnit& Unit) const
{
	// game_state.gd:962-964.
	const TMSim::FBattle& Battle = From.Battle;
	return FString::Printf(TEXT("Countdown = base %s s + Patience %d x %s s = %s s"),
		*Num(Battle.Tuning.ClockBase), Unit.Stat(TMSim::EStat::Patience), *Num(Battle.Tuning.PatienceMultiplier),
		*Num(Battle.ClockTicks(Unit) / Tps));
}

FString ATMBattleHud::ExplainMove(ATMBattleDirector& From, const TMSim::FUnit& Unit) const
{
	// game_state.gd:1008-1012.
	const TMSim::FBattle& Battle = From.Battle;
	FString Text = FString::Printf(TEXT("Move %d m x move multiplier %s"),
		Unit.Stat(TMSim::EStat::Move), *Num(Battle.Tuning.MoveMultiplier));
	if (!FMath::IsNearlyEqual(Unit.MoveFactor(), 1.0f))
	{
		Text += FString::Printf(TEXT(" x status %s"), *Num(Unit.MoveFactor()));
	}
	return Text + FString::Printf(TEXT(" = %s m"), *Num(Battle.MoveOf(Unit)));
}

FString ATMBattleHud::ExplainSight(ATMBattleDirector& From, const TMSim::FUnit& Unit) const
{
	// game_state.gd:1015-1016.
	const TMSim::FBattle& Battle = From.Battle;
	return FString::Printf(TEXT("Sight %d m x sight multiplier %s = %s m"),
		Unit.Stat(TMSim::EStat::Sight), *Num(Battle.Tuning.SightMultiplier), *Num(Battle.SightOf(Unit)));
}

FString ATMBattleHud::ExplainAbility(ATMBattleDirector& From, const TMSim::FUnit& Unit, int32 Slot) const
{
	// game_state.gd:973-1005: what the ability does, with its numbers worked out.
	const TMSim::FBattle& Battle = From.Battle;
	const TMSim::FTuning& Tune = Battle.Tuning;
	const TMSim::FAbility* Ability = TMSim::JobAbility(Unit.Job, Slot);
	if (!Ability)
	{
		return FString();
	}
	TArray<FString> Lines;
	switch (Ability->Effect)
	{
	case TMSim::EEffect::Damage:
		Lines.Add(FString::Printf(TEXT("Damage = power %s"), *Num(Ability->Power)));
		Lines.Add(FString::Printf(TEXT("  x height (%s per level) x side %s / back %s"),
			*Num(Tune.HeightBonus), *Num(Tune.SideBonus), *Num(Tune.BackBonus)));
		Lines.Add(FString::Printf(TEXT("  - target's %s, x damage multiplier %s (at least 1)"),
			Ability->Scale == TMSim::EScale::Att ? TEXT("AttDef") : TEXT("MagDef"), *Num(Tune.DamageMultiplier)));
		Lines.Add(FString::Printf(TEXT("  target's %s evades it; %d%% chance of a critical hit (x %s)"),
			Ability->Scale == TMSim::EScale::Att ? TEXT("A-Eva") : TEXT("M-Eva"), Battle.CritChance(Unit), *Num(Tune.CritMultiplier)));
		break;
	case TMSim::EEffect::Heal:
		Lines.Add(FString::Printf(TEXT("Heal = power %s x %s x heal multiplier %s = %d"), *Num(Ability->Power),
			*Num(TMSim::Combat::HealScale), *Num(Tune.HealMultiplier),
			TMSim::RoundToInt(Ability->Power * TMSim::Combat::HealScale * Tune.HealMultiplier)));
		break;
	case TMSim::EEffect::Revive:
		Lines.Add(FString::Printf(TEXT("Revives with %d%% of max HP"), TMSim::RoundToInt(Ability->Power * 100.0)));
		break;
	default:
		break;
	}
	if (Ability->TgChange != 0)
	{
		Lines.Add(FString::Printf(TEXT("Turn Gauge %+d%% = %+d TG (of %d)"), Ability->TgChange,
			Ability->TgChange * TMSim::Pace::TgMax / 100, TMSim::Pace::TgMax));
	}
	for (const TMSim::FBuff& Buff : Ability->Buffs)
	{
		Lines.Add(FString::Printf(TEXT("%s %+d for %d turn%s"), ShownStatName(Buff.Stat), Buff.Amount, Buff.Turns,
			Buff.Turns == 1 ? TEXT("") : TEXT("s")));
	}
	if (Ability->HasStatus())
	{
		Lines.Add(FString::Printf(TEXT("%hs for %d turn%s"), Ability->StatusId.c_str(), Ability->StatusTurns,
			Ability->StatusTurns == 1 ? TEXT("") : TEXT("s")));
	}
	if (Ability->Cast > 0.0f)
	{
		Lines.Add(FString::Printf(TEXT("Cast %s s x cast time multiplier %s = %s s"), *Num(Ability->Cast),
			*Num(Tune.CastTimeMultiplier), *Num(Battle.CastTicks(*Ability) / Tps)));
	}
	else
	{
		Lines.Add(TEXT("Instant"));
	}
	if (Ability->Cooldown > 0)
	{
		Lines.Add(FString::Printf(TEXT("Cooldown %d turn%s"), Ability->Cooldown, Ability->Cooldown == 1 ? TEXT("") : TEXT("s")));
	}
	if (Slot == 3)
	{
		Lines.Add(FString::Printf(TEXT("Ultimate: needs a full meter (+%d per turn, +%d per ability used, +%s per 1%% of max HP lost)"),
			TMSim::RoundToInt(Tune.UltPerTurn), TMSim::RoundToInt(Tune.UltPerAction), *Num(TMSim::Combat::UltFromDamage)));
	}
	return FString::Join(Lines, TEXT("\n"));
}

FString ATMBattleHud::BuffText(const TMSim::FUnit& Unit)
{
	FString Text;
	for (const TMSim::FBuff& Buff : Unit.Buffs)
	{
		Text += FString::Printf(TEXT("\n%s %+d (%d turn%s left)"), ShownStatName(Buff.Stat), Buff.Amount, Buff.Turns,
			Buff.Turns == 1 ? TEXT("") : TEXT("s"));
	}
	return Text.IsEmpty() ? Text : TEXT("\nBuffs:") + Text;
}

void ATMBattleHud::DrawTooltip()
{
	// The last tip drawn under the pointer is the one on top.
	const FVector2D Mouse = MousePoint();
	const FTMHudTip* Found = nullptr;
	for (const FTMHudTip& Tip : Tips)
	{
		if (Tip.Area.IsInside(Mouse) && !Tip.Text.IsEmpty())
		{
			Found = &Tip;
		}
	}
	if (!Found)
	{
		return;
	}
	UFont* Font = GEngine->GetMediumFont();
	const float Scale = 0.5f * S;
	TArray<FString> Lines;
	Found->Text.ParseIntoArrayLines(Lines, false);
	float W = 0.0f;
	const float LineH = TextSize(TEXT("Ag"), Font, Scale).Y;
	for (const FString& Line : Lines)
	{
		W = FMath::Max(W, TextSize(Line, Font, Scale).X);
	}
	const float Pad = 8.0f * S;
	const float BoxW = W + 2.0f * Pad;
	const float BoxH = Lines.Num() * LineH + 2.0f * Pad;
	float X = Mouse.X + 16.0f * S;
	float Y = Mouse.Y + 16.0f * S;
	if (X + BoxW > Canvas->ClipX)
	{
		X = Mouse.X - BoxW - 8.0f * S;
	}
	if (Y + BoxH > Canvas->ClipY)
	{
		Y = Mouse.Y - BoxH - 8.0f * S;
	}
	Panel(X, Y, BoxW, BoxH, FLinearColor(0.03f, 0.04f, 0.07f, 0.96f), FLinearColor(1, 1, 1, 0.25f), 1.0f);
	for (int32 i = 0; i < Lines.Num(); ++i)
	{
		Text(Lines[i], X + Pad, Y + Pad + i * LineH, TextColour, Font, Scale);
	}
}

// ------------------------------------------------------------- turn order

void ATMBattleHud::DrawTurnOrder(ATMBattleDirector& From)
{
	// Two bars, one per team. A chip slides along its team's bar toward the READY
	// zone at the left, placed by seconds until ready on a square-root scale so
	// the last seconds get the most room; far-off turns are drawn smaller, chips
	// that would touch merge into a framed group, and each glides rather than
	// jumps (hud.gd:52-74, 307-311, 1226-1387).
	const TMSim::FBattle& Battle = From.Battle;
	const float X0 = 16.0f * S;
	const float Y0 = 12.0f * S;
	const float Width = FMath::Max(300.0f * S, Canvas->ClipX - 32.0f * S - CornerWidth * S);
	const float End = X0 + Width;
	const float Track = TrackStart * S;
	const float Chip = ChipSize * S;
	auto BarX = [&](float Seconds)
	{
		const float Frac = FMath::Sqrt(FMath::Clamp(Seconds / TimelineSeconds, 0.0f, 1.0f));
		return X0 + Track + Frac * (Width - Track - Chip * 0.5f);
	};
	const float Blend = FMath::Min(1.0f, (GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.016f) * 12.0f);
	UFont* Font = GEngine->GetMediumFont();
	const FVector2D Mouse = MousePoint();

	struct FEntry
	{
		const TMSim::FUnit* Unit = nullptr;
		float Seconds = 0.0f;
		float Scale = 1.0f;
		FVector2D Target;
	};
	struct FGroup
	{
		std::vector<int32> Ids;
		float Centre = 0.0f;
		float Width = 0.0f;
	};

	TSet<int32> Shown;
	for (int32 Team = 0; Team < 2; ++Team)
	{
		const float RowY = Y0 + Team * RowHeight * S;
		const float LineY = RowY + Chip * 0.5f + 2.0f * S;
		Panel(X0 - 6.0f * S, RowY - 3.0f * S, Width + 12.0f * S, (RowHeight - 2.0f) * S, PanelFill);
		DrawRect(FLinearColor(Gold.R, Gold.G, Gold.B, 0.12f), X0, RowY, Track - 6.0f * S, Chip);
		DrawRect(TeamColour(Team) * FLinearColor(1, 1, 1, 0.45f), X0 + Track, LineY - 1.5f * S, Width - Track, 3.0f * S);
		for (float Seconds : TickSeconds)
		{
			const float X = BarX(Seconds);
			const float Tall = (Seconds == 0.0f ? 10.0f : 6.0f) * S;
			DrawRect(Dim, X - 1.0f, LineY - Tall * 0.5f, 2.0f, Tall);
			if (Team == 1)
			{
				Text(FString::Printf(TEXT("%.0fs"), Seconds), X - 6.0f * S, RowY + Chip + 1.0f * S, Dim, Font, 0.4f * S, false);
			}
		}

		// Ready units in the zone at the left, least time first; the rest along
		// the bar, soonest first, so that left to right is the order they come.
		std::vector<FEntry> Ready;
		std::vector<FEntry> OnBar;
		for (const TMSim::FUnit& Unit : Battle.Units)
		{
			if (Unit.Team != Team || !Unit.IsAlive())
			{
				continue;
			}
			FEntry Entry;
			Entry.Unit = &Unit;
			Entry.Seconds = SecondsLeft(Unit, Battle);
			(Unit.bReady ? Ready : OnBar).push_back(Entry);
		}
		auto Sooner = [](const FEntry& A, const FEntry& B)
		{
			return A.Seconds != B.Seconds ? A.Seconds < B.Seconds : A.Unit->Id < B.Unit->Id;
		};
		std::sort(Ready.begin(), Ready.end(), Sooner);
		std::sort(OnBar.begin(), OnBar.end(), Sooner);
		for (size_t i = 0; i < Ready.size(); ++i)
		{
			Ready[i].Target = FVector2D(X0 + FMath::Min<int32>(static_cast<int32>(i), ReadySlots - 1) * (ChipSize + ChipGap) * S, RowY);
		}

		// Group chips that would touch, merging until no two groups do; a group
		// sits centred on the average time of its units (hud.gd:1261-1295).
		std::vector<FGroup> Groups;
		for (FEntry& Entry : OnBar)
		{
			Entry.Scale = FMath::Lerp(1.0f, ChipMinScale, FMath::Clamp(Entry.Seconds / TimelineSeconds, 0.0f, 1.0f));
			FGroup Group;
			Group.Ids.push_back(Entry.Unit->Id);
			Group.Centre = BarX(Entry.Seconds);
			Group.Width = Chip * Entry.Scale;
			Groups.push_back(Group);
		}
		auto EntryOf = [&OnBar](int32 Id) -> FEntry&
		{
			for (FEntry& Entry : OnBar)
			{
				if (Entry.Unit->Id == Id)
				{
					return Entry;
				}
			}
			return OnBar.front();
		};
		bool bMerged = true;
		while (bMerged)
		{
			bMerged = false;
			for (size_t i = 0; i + 1 < Groups.size(); ++i)
			{
				const FGroup& A = Groups[i];
				const FGroup& B = Groups[i + 1];
				if (A.Centre + A.Width * 0.5f + MergeDistance * S > B.Centre - B.Width * 0.5f)
				{
					FGroup Joined;
					Joined.Ids = A.Ids;
					Joined.Ids.insert(Joined.Ids.end(), B.Ids.begin(), B.Ids.end());
					Joined.Width = -ChipGap * S;
					float Total = 0.0f;
					for (int32 Id : Joined.Ids)
					{
						const FEntry& Entry = EntryOf(Id);
						Joined.Width += Chip * Entry.Scale + ChipGap * S;
						Total += BarX(Entry.Seconds);
					}
					Joined.Centre = Total / Joined.Ids.size();
					Groups[i] = Joined;
					Groups.erase(Groups.begin() + static_cast<std::ptrdiff_t>(i) + 1);
					bMerged = true;
					break;
				}
			}
		}
		for (const FGroup& Group : Groups)
		{
			const float Centre = FMath::Clamp(Group.Centre, X0 + Track + Group.Width * 0.5f, End - Group.Width * 0.5f);
			float X = Centre - Group.Width * 0.5f;
			for (int32 Id : Group.Ids)
			{
				FEntry& Entry = EntryOf(Id);
				Entry.Target = FVector2D(X, LineY - Chip * Entry.Scale * 0.5f);
				X += Chip * Entry.Scale + ChipGap * S;
			}
		}

		// Where each chip is now, a little closer to where it is going.
		auto Glide = [&](FEntry& Entry)
		{
			const int32 Id = Entry.Unit->Id;
			Shown.Add(Id);
			FVector2D& Place = ChipPlace.FindOrAdd(Id, Entry.Target);
			float& Scale = ChipScale.FindOrAdd(Id, Entry.Scale);
			Place = FMath::Lerp(Place, Entry.Target, Blend);
			Scale = FMath::Lerp(Scale, Entry.Scale, Blend);
		};
		for (FEntry& Entry : Ready)
		{
			Glide(Entry);
		}
		for (FEntry& Entry : OnBar)
		{
			Glide(Entry);
		}

		// A frame behind each merged group, fitted to its chips as they glide.
		for (const FGroup& Group : Groups)
		{
			if (Group.Ids.size() < 2)
			{
				continue;
			}
			FBox2D Box(ForceInit);
			for (int32 Id : Group.Ids)
			{
				const FVector2D Place = ChipPlace[Id];
				const float Size = Chip * ChipScale[Id];
				Box += Place;
				Box += Place + FVector2D(Size, Size);
			}
			Box = Box.ExpandBy(3.0f * S);
			Panel(Box.Min.X, Box.Min.Y, Box.GetSize().X, Box.GetSize().Y,
				FLinearColor(0.03f, 0.04f, 0.07f, 0.75f), FLinearColor(1, 1, 1, 0.25f), 1.0f);
		}

		auto DrawChip = [&](const FEntry& Entry)
		{
			const TMSim::FUnit& Unit = *Entry.Unit;
			const FVector2D Place = ChipPlace[Unit.Id];
			const float Size = Chip * ChipScale[Unit.Id];
			const bool bFogged = !From.IsSeen(Unit);
			const bool bOver = FBox2D(Place, Place + FVector2D(Size, Size)).IsInside(Mouse);

			// Gold, red or purple outline when ready, urgent or casting; white when
			// selected (hud.gd:1088-1107, 1312-1333).
			FLinearColor Edge = TeamColour(Team) * FLinearColor(1, 1, 1, 0.6f);
			float Thick = 1.5f;
			FString Badge;
			FLinearColor BadgeColour = TextColour;
			if (bFogged)
			{
				Edge = FLinearColor(0.5f, 0.5f, 0.55f, 0.6f);
			}
			else if (Unit.bReady)
			{
				// Time left before its Patience runs out.
				const bool bUrgent = Entry.Seconds <= 5.0f;
				Edge = bUrgent ? Urgent : Gold;
				BadgeColour = Edge;
				Thick = 2.5f;
				Badge = FString::Printf(TEXT("%ds"), FMath::CeilToInt(Entry.Seconds));
			}
			else if (Unit.IsCasting())
			{
				Edge = CastColour;
				BadgeColour = FLinearColor(0.85f, 0.7f, 1.0f);
				Thick = 2.5f;
				Badge = FString::Printf(TEXT("%.1fs"), Entry.Seconds);
			}
			else if (Entry.Seconds <= ShowTimeSeconds || bOver)
			{
				// The time until ready shows only just before a turn, and while the
				// pointer is over the chip.
				Badge = FString::Printf(TEXT("%.1fs"), Entry.Seconds);
			}
			if (Unit.Id == From.SelectedId)
			{
				Edge = FLinearColor::White;
				Thick = 3.5f;
			}
			Panel(Place.X, Place.Y, Size, Size, bFogged ? FLinearColor(0.15f, 0.15f, 0.18f, 0.95f) : TeamFill(Team), Edge, Thick);
			const float Letters = 0.58f * S * ChipScale[Unit.Id];
			const FString Mark = bFogged ? FString(TEXT("?")) : Initials(JobName(Unit));
			const FVector2D MarkSize = TextSize(Mark, Font, Letters);
			const float MarkY = Badge.IsEmpty() ? Place.Y + (Size - MarkSize.Y) * 0.5f : Place.Y + 1.0f * S;
			Text(Mark, Place.X + (Size - MarkSize.X) * 0.5f, MarkY, bFogged ? Dim : TextColour, Font, Letters);
			if (!Badge.IsEmpty())
			{
				const float BadgeScale = 0.42f * S * ChipScale[Unit.Id];
				const FVector2D BadgeSize = TextSize(Badge, Font, BadgeScale);
				Text(Badge, Place.X + (Size - BadgeSize.X) * 0.5f, Place.Y + Size - BadgeSize.Y - 1.0f * S, BadgeColour, Font, BadgeScale);
			}
			AddButton(Place.X, Place.Y, Size, Size, ETMHudAction::PickUnit, Unit.Id);
			// Hidden by the fog, it says so and nothing more (hud.gd:1346-1356).
			FString Tip;
			if (bFogged)
			{
				Tip = TEXT("Hidden by the fog of war");
			}
			else
			{
				Tip = FString::Printf(TEXT("%s %s %d  hp %d/%d"), Team == 0 ? TEXT("Blue") : TEXT("Red"),
					*JobName(Unit), Unit.Id, Unit.Hp, Unit.MaxHp());
				if (Unit.IsCasting())
				{
					const TMSim::FAbility* Ability = TMSim::JobAbility(Unit.Job, Unit.Casting.Slot);
					Tip += FString::Printf(TEXT("\nCasting %hs"), Ability ? Ability->Name.c_str() : "");
				}
				Tip += TEXT("\n\n") + ExplainTurn(From, Unit) + TEXT("\n") + ExplainCountdown(From, Unit);
			}
			AddTip(Place.X, Place.Y, Size, Size, Tip);
		};
		for (const FEntry& Entry : Ready)
		{
			DrawChip(Entry);
		}
		for (const FEntry& Entry : OnBar)
		{
			DrawChip(Entry);
		}
	}

	// Units that are gone lose their place, so a new battle starts them fresh.
	for (auto It = ChipPlace.CreateIterator(); It; ++It)
	{
		if (!Shown.Contains(It.Key()))
		{
			ChipScale.Remove(It.Key());
			It.RemoveCurrent();
		}
	}
}

// --------------------------------------------------------------------- log

void ATMBattleHud::DrawLog(ATMBattleDirector& From)
{
	// The combat log under the turn order: the last lines of the fight, older
	// ones fading. L or the Log button shows or hides it, + makes it taller, and
	// the mouse wheel scrolls back through the fight. Godot's can be dragged and
	// resized; this one keeps its place.
	if (!From.bShowLog)
	{
		return;
	}
	UFont* Font = GEngine->GetMediumFont();
	const float Scale = 0.5f * S;
	const float LineH = TextSize(TEXT("Ag"), Font, Scale).Y;
	const int32 Shown = From.bLogLarge ? 28 : 10;
	const float X = 16.0f * S;
	const float Y = 12.0f * S + 2.0f * RowHeight * S + 18.0f * S;
	const float W = 520.0f * S;
	const float Head = 24.0f * S;
	const int32 Last = FMath::Max(0, From.Log.Num() - From.LogScroll);
	const int32 First = FMath::Max(0, Last - Shown);
	const int32 Count = Last - First;
	const float H = Head + FMath::Max(1, Count) * LineH + 8.0f * S;
	Panel(X - 6.0f * S, Y - 4.0f * S, W, H, FLinearColor(0.06f, 0.08f, 0.12f, 0.62f), FLinearColor(1, 1, 1, 0.12f), 1.0f);
	LogBottom = Y - 4.0f * S + H;
	FString Title = TEXT("Log");
	if (From.LogScroll > 0)
	{
		Title += FString::Printf(TEXT("   (%d lines back; scroll down for the newest)"), From.LogScroll);
	}
	Text(Title, X, Y, Dim, Font, 0.48f * S);
	MenuButton(X + W - 70.0f * S, Y - 2.0f * S, 28.0f * S, 20.0f * S, From.bLogLarge ? TEXT("-") : TEXT("+"), ETMHudAction::GrowLog);
	MenuButton(X + W - 38.0f * S, Y - 2.0f * S, 28.0f * S, 20.0f * S, TEXT("x"), ETMHudAction::ToggleLog);
	for (int32 i = 0; i < Count; ++i)
	{
		const float Age = static_cast<float>(Count - 1 - i) / FMath::Max(1, Count - 1);
		const FLinearColor Colour(0.8f, 0.83f, 0.9f, 1.0f - 0.5f * Age);
		Text(From.Log[First + i], X, Y + Head + i * LineH, Colour, Font, Scale);
	}
}

// ------------------------------------------------------------- unit cards

float ATMBattleHud::DrawIncoming(ATMBattleDirector& From, const TMSim::FUnit& Unit, float X, float Y, float W)
{
	UFont* Font = GEngine->GetMediumFont();
	float Used = 0.0f;
	for (const TMSim::FUnit* Caster : CastersAt(From.Battle, Unit))
	{
		if (!From.IsSeen(*Caster))
		{
			continue;
		}
		const TMSim::FAbility* Ability = TMSim::JobAbility(Caster->Job, Caster->Casting.Slot);
		const FString Line = FString::Printf(TEXT("Incoming: %s %s %d, %hs in %.1fs"),
			Caster->Team == 0 ? TEXT("Blue") : TEXT("Red"), *JobName(*Caster), Caster->Id,
			Ability ? Ability->Name.c_str() : "", Caster->Casting.Ticks / Tps);
		Text(Line, X, Y + Used, CastColour, Font, 0.5f * S);
		AddTip(X, Y + Used, W, 18.0f * S, FString::Printf(TEXT("%s %s is casting %hs on this spot"),
			Caster->Team == 0 ? TEXT("Blue") : TEXT("Red"), *JobName(*Caster), Ability ? Ability->Name.c_str() : ""));
		Used += 20.0f * S;
	}
	return Used;
}

float ATMBattleHud::DrawUnitBody(ATMBattleDirector& From, const TMSim::FUnit& Unit, float X, float Y, float W)
{
	// The gauges and numbers both cards show (hud.gd:1405-1427, 822-863), each
	// with a tooltip saying how it is worked out.
	TMSim::FBattle& Battle = From.Battle;
	UFont* Font = GEngine->GetMediumFont();
	const float GaugeH = 22.0f * S;
	float Row = Y;

	const int32 MaxHp = FMath::Max(1, Unit.MaxHp());
	const float HpPart = static_cast<float>(Unit.Hp) / MaxHp;
	Gauge(X, Row, W, GaugeH, HpPart,
		HpPart > 0.5f ? FLinearColor(0.25f, 0.7f, 0.35f) : HpPart > 0.25f ? FLinearColor(0.8f, 0.65f, 0.2f) : FLinearColor(0.8f, 0.25f, 0.2f),
		FString::Printf(TEXT("HP  %d / %d"), Unit.Hp, Unit.MaxHp()));
	AddTip(X, Row, W, GaugeH, FString::Printf(TEXT("Max HP %d (class %s)%s"), Unit.MaxHp(), *JobName(Unit),
		Unit.Hp == Unit.MaxHp() ? TEXT("") : *FString::Printf(TEXT("\nMissing %d"), Unit.MaxHp() - Unit.Hp)));
	Row += GaugeH + 4.0f * S;

	if (Unit.IsCasting())
	{
		const TMSim::FAbility* Ability = TMSim::JobAbility(Unit.Job, Unit.Casting.Slot);
		const float Done = 1.0f - static_cast<float>(Unit.Casting.Ticks) / FMath::Max(1, Unit.Casting.Total);
		Gauge(X, Row, W, GaugeH, Done, CastColour * FLinearColor(1, 1, 1, 0.8f),
			FString::Printf(TEXT("Casting %hs  %.1fs"), Ability ? Ability->Name.c_str() : "", Unit.Casting.Ticks / Tps));
	}
	else if (Unit.bReady)
	{
		Gauge(X, Row, W, GaugeH, 1.0f, Gold * FLinearColor(1, 1, 1, 0.7f), TEXT("TG  READY"));
	}
	else
	{
		Gauge(X, Row, W, GaugeH, static_cast<float>(Unit.Tg) / TMSim::Pace::TgMax, FLinearColor(0.3f, 0.45f, 0.8f),
			FString::Printf(TEXT("TG  %d%%"), Unit.Tg * 100 / TMSim::Pace::TgMax));
	}
	AddTip(X, Row, W, GaugeH, ExplainTurn(From, Unit));
	Row += GaugeH + 4.0f * S;

	Gauge(X, Row, W, GaugeH, static_cast<float>(Unit.Ult) / TMSim::Pace::UltMax,
		Unit.Ult >= TMSim::Pace::UltMax ? FLinearColor(1.0f, 0.95f, 0.6f, 0.85f) : FLinearColor(0.55f, 0.5f, 0.3f),
		FString::Printf(TEXT("ULT  %d%%"), Unit.Ult));
	AddTip(X, Row, W, GaugeH, FString::Printf(TEXT("Ultimate meter %d / %d\n+%d each turn, +%d per ability used, +%s per 1%% of max HP lost"),
		Unit.Ult, TMSim::Pace::UltMax, TMSim::RoundToInt(Battle.Tuning.UltPerTurn),
		TMSim::RoundToInt(Battle.Tuning.UltPerAction), *Num(TMSim::Combat::UltFromDamage)));
	Row += GaugeH + 8.0f * S;

	const float StatsTop = Row;
	Text(FString::Printf(TEXT("DEF %d  MDF %d  CRIT %d%%"),
		Unit.Stat(TMSim::EStat::AttDef), Unit.Stat(TMSim::EStat::MagDef), Unit.Stat(TMSim::EStat::Crit)),
		X, Row, Dim, Font, 0.52f * S);
	Row += 20.0f * S;
	Text(FString::Printf(TEXT("AEV %d%%  MEV %d%%  SPD %d  MOV %sm  PAT %d  SGT %sm"),
		Unit.Stat(TMSim::EStat::AEva), Unit.Stat(TMSim::EStat::MEva), Unit.Stat(TMSim::EStat::Speed),
		*Num(Battle.MoveOf(Unit)), Unit.Stat(TMSim::EStat::Patience), *Num(Battle.SightOf(Unit))),
		X, Row, Dim, Font, 0.52f * S);
	Row += 20.0f * S;
	AddTip(X, StatsTop, W, Row - StatsTop, ExplainMove(From, Unit) + TEXT("\n") + ExplainSight(From, Unit)
		+ TEXT("\n") + ExplainCountdown(From, Unit) + TEXT("\n") + ExplainTurn(From, Unit) + BuffText(Unit));
	return Row - Y;
}

void ATMBattleHud::DrawUnitCard(ATMBattleDirector& From)
{
	// The selected unit; while watching, the unit whose turn it is; otherwise the
	// one under the pointer (hud.gd:1390-1427).
	const TMSim::FUnit* Unit = From.SelectedUnit();
	if (!Unit && From.ComputerPlays(0) && From.ComputerPlays(1))
	{
		Unit = From.WaitingOn();
	}
	if (!Unit)
	{
		Unit = From.Battle.FindUnit(From.HoverUnitId);
	}
	if (!Unit || (!Unit->IsAlive() && !Unit->IsKo()) || !From.IsSeen(*Unit))
	{
		return;
	}
	UFont* Font = GEngine->GetMediumFont();
	UFont* Big = GEngine->GetLargeFont();
	const float W = 440.0f * S;
	const float Pad = 10.0f * S;
	int32 Incoming = 0;
	for (const TMSim::FUnit* Caster : CastersAt(From.Battle, *Unit))
	{
		Incoming += From.IsSeen(*Caster) ? 1 : 0;
	}
	const float H = 238.0f * S + Incoming * 20.0f * S;
	const float X = 16.0f * S;
	const float Y = Canvas->ClipY - H - 16.0f * S;
	Panel(X, Y, W, H, PanelFill, TeamColour(Unit->Team) * FLinearColor(1, 1, 1, 0.5f), 1.5f);

	float Row = Y + Pad;
	Text(FString::Printf(TEXT("%s %d"), *JobName(*Unit), Unit->Id), X + Pad, Row, TeamColour(Unit->Team), Big, 0.85f * S);
	Row += 38.0f * S;

	// When it acts next, and what is on it.
	FString Sub;
	FLinearColor SubColour = Dim;
	if (!Unit->IsAlive())
	{
		Sub = FString::Printf(TEXT("DOWN  %.0fs to raise"), Unit->KoTicks / Tps);
		SubColour = Urgent;
	}
	else if (Unit->bReady)
	{
		const float Left = Unit->Clock / Tps;
		Sub = FString::Printf(TEXT("READY  %ds left"), FMath::CeilToInt(Left));
		SubColour = Left <= 5.0f ? Urgent : Gold;
	}
	else
	{
		Sub = FString::Printf(TEXT("Ready in %.1fs"), From.Battle.TicksToReady(*Unit) / Tps);
	}
	for (const TMSim::FStatus& Status : Unit->Statuses)
	{
		Sub += FString::Printf(TEXT("  %hs"), Status.Id.c_str());
	}
	if (Unit->bMoved)
	{
		Sub += TEXT("  walked");
	}
	if (Unit->bActed)
	{
		Sub += TEXT("  acted");
	}
	Text(Sub, X + Pad, Row, SubColour, Font, 0.6f * S);
	AddTip(X + Pad, Row, W - 2.0f * Pad, 22.0f * S, ExplainCountdown(From, *Unit) + TEXT("\n") + ExplainTurn(From, *Unit));
	Row += 26.0f * S;

	Row += DrawUnitBody(From, *Unit, X + Pad, Row, W - 2.0f * Pad);
	DrawIncoming(From, *Unit, X + Pad, Row + 2.0f * S, W - 2.0f * Pad);
}

void ATMBattleHud::DrawInspectCard(ATMBattleDirector& From)
{
	// A clicked unit that is not taking orders, on its own card at the right
	// (hud.gd:708-863), with its four abilities coloured by what they are for.
	const TMSim::FUnit* Unit = From.Battle.FindUnit(From.InspectedId);
	if (!Unit || !From.IsSeen(*Unit))
	{
		return;
	}
	UFont* Font = GEngine->GetMediumFont();
	UFont* Big = GEngine->GetLargeFont();
	int32 Incoming = 0;
	for (const TMSim::FUnit* Caster : CastersAt(From.Battle, *Unit))
	{
		Incoming += From.IsSeen(*Caster) ? 1 : 0;
	}
	const float W = 420.0f * S;
	const float Pad = 10.0f * S;
	const float H = 214.0f * S + 4.0f * 24.0f * S + 30.0f * S + Incoming * 20.0f * S;
	const float X = Canvas->ClipX - W - 16.0f * S;
	const float Y = FMath::Max(12.0f * S + 2.0f * RowHeight * S + 60.0f * S, (Canvas->ClipY - H) * 0.5f);
	Panel(X, Y, W, H, PanelFill, TeamColour(Unit->Team) * FLinearColor(1, 1, 1, 0.6f), 1.5f);

	float Row = Y + Pad;
	Text(FString::Printf(TEXT("%s %s %d"), Unit->Team == 0 ? TEXT("Blue") : TEXT("Red"), *JobName(*Unit), Unit->Id),
		X + Pad, Row, TeamColour(Unit->Team), Big, 0.7f * S);
	MenuButton(X + W - 36.0f * S, Row, 26.0f * S, 24.0f * S, TEXT("x"), ETMHudAction::CloseCard);
	Row += 32.0f * S;

	FString Sub;
	if (Unit->IsKo())
	{
		Sub = TEXT("KNOCKED OUT");
	}
	else if (Unit->bReady)
	{
		Sub = FString::Printf(TEXT("READY  %ds left"), FMath::CeilToInt(Unit->Clock / Tps));
	}
	else
	{
		Sub = FString::Printf(TEXT("Ready in %.1fs"), SecondsLeft(*Unit, From.Battle));
	}
	if (Unit->IsCasting())
	{
		const TMSim::FAbility* Ability = TMSim::JobAbility(Unit->Job, Unit->Casting.Slot);
		Sub += FString::Printf(TEXT("  casting %hs"), Ability ? Ability->Name.c_str() : "");
	}
	for (const TMSim::FStatus& Status : Unit->Statuses)
	{
		Sub += FString::Printf(TEXT("  %hs"), Status.Id.c_str());
	}
	Text(Sub, X + Pad, Row, Dim, Font, 0.55f * S);
	AddTip(X + Pad, Row, W - 2.0f * Pad, 20.0f * S, ExplainCountdown(From, *Unit) + TEXT("\n") + ExplainTurn(From, *Unit));
	Row += 24.0f * S;

	Row += DrawUnitBody(From, *Unit, X + Pad, Row, W - 2.0f * Pad) + 4.0f * S;

	for (int32 Slot = 0; Slot < 4; ++Slot)
	{
		const TMSim::FAbility* Ability = TMSim::JobAbility(Unit->Job, Slot);
		if (!Ability)
		{
			continue;
		}
		FString Note;
		if (Slot == 3)
		{
			Note = Unit->Ult < TMSim::Pace::UltMax ? FString::Printf(TEXT("  (ULT %d%%)"), Unit->Ult) : FString(TEXT("  (ULT ready)"));
		}
		else if (Unit->Cooldowns[Slot] > 0)
		{
			Note = FString::Printf(TEXT("  (wait %d)"), Unit->Cooldowns[Slot]);
		}
		FString ClassName;
		FLinearColor Tint = AbilityClassColour(*Ability, &ClassName);
		const bool bWaiting = Unit->Cooldowns[Slot] > 0 || (Slot == 3 && Unit->Ult < TMSim::Pace::UltMax);
		if (bWaiting)
		{
			Tint = Tint * FLinearColor(0.65f, 0.65f, 0.65f, 1.0f);
		}
		Text(FString::Printf(TEXT("%s  %hs%s"), Slot == 3 ? TEXT("U") : *FString::FromInt(Slot + 1), Ability->Name.c_str(), *Note),
			X + Pad, Row, Tint, Font, 0.55f * S);
		AddTip(X + Pad, Row, W - 2.0f * Pad, 22.0f * S, FString::Printf(TEXT("%hs  -  %s\n\n"), Ability->Name.c_str(), *ClassName)
			+ ExplainAbility(From, *Unit, Slot));
		Row += 24.0f * S;
	}
	// What the colours mean (hud.gd:763-777).
	Text(TEXT("physical"), X + Pad, Row + 2.0f * S, FLinearColor(1.0f, 0.55f, 0.45f), Font, 0.42f * S);
	Text(TEXT("magic"), X + Pad + 80.0f * S, Row + 2.0f * S, FLinearColor(0.75f, 0.6f, 1.0f), Font, 0.42f * S);
	Text(TEXT("healing"), X + Pad + 140.0f * S, Row + 2.0f * S, FLinearColor(0.45f, 0.95f, 0.5f), Font, 0.42f * S);
	Text(TEXT("buff / debuff"), X + Pad + 210.0f * S, Row + 2.0f * S, FLinearColor(0.45f, 0.8f, 1.0f), Font, 0.42f * S);
	Text(TEXT("utility"), X + Pad + 320.0f * S, Row + 2.0f * S, FLinearColor(0.85f, 0.85f, 0.9f), Font, 0.42f * S);
	Row += 26.0f * S;
	DrawIncoming(From, *Unit, X + Pad, Row, W - 2.0f * Pad);
}

// ------------------------------------------------------- corner and field

void ATMBattleHud::DrawCornerButtons(ATMBattleDirector& From)
{
	// Log / Field / Units / Pause / Menu, top right (hud.gd:608-623).
	const float BW = 60.0f * S;
	const float BH = 30.0f * S;
	const float Gap = 4.0f * S;
	float X = Canvas->ClipX - 10.0f * S - 5.0f * BW - 4.0f * Gap;
	const float Y = 12.0f * S;
	MenuButton(X, Y, BW, BH, TEXT("Log"), ETMHudAction::ToggleLog, -1, From.bShowLog);
	AddTip(X, Y, BW, BH, TEXT("Show or hide the combat log (L). + makes it taller; the wheel scrolls it."));
	X += BW + Gap;
	MenuButton(X, Y, BW, BH, TEXT("Field"), ETMHudAction::ToggleField, -1, From.bShowField);
	AddTip(X, Y, BW, BH, TEXT("Show or hide the list of every unit on the field"));
	X += BW + Gap;
	MenuButton(X, Y, BW, BH, TEXT("Units"), ETMHudAction::ToggleGuide, -1, From.bGuideOpen);
	AddTip(X, Y, BW, BH, TEXT("The Unit Guide: every class's stats and abilities (U)"));
	X += BW + Gap;
	MenuButton(X, Y, BW, BH, From.bPaused ? TEXT("Go") : TEXT("Pause"), ETMHudAction::Pause, -1, From.bPaused);
	AddTip(X, Y, BW, BH, TEXT("Pause or carry on (P)"));
	X += BW + Gap;
	MenuButton(X, Y, BW, BH, TEXT("Menu"), ETMHudAction::OpenMenu);
	AddTip(X, Y, BW, BH, TEXT("Resume, restart, change the setup, or leave (Esc)"));
}

void ATMBattleHud::DrawField(ATMBattleDirector& From)
{
	// Both teams down the left edge: name, health, and whether each is ready,
	// casting or waiting. Clicking a row picks that unit, like its chip
	// (hud.gd:515-606). A unit hidden by the fog shows as a question.
	if (!From.bShowField)
	{
		return;
	}
	const TMSim::FBattle& Battle = From.Battle;
	UFont* Font = GEngine->GetMediumFont();
	const float RowH = 30.0f * S;
	const float W = 330.0f * S;
	int32 Rows = 0;
	for (const TMSim::FUnit& Unit : Battle.Units)
	{
		Rows += (Unit.IsAlive() || Unit.IsKo()) ? 1 : 0;
	}
	const float H = Rows * RowH + 16.0f * S;
	const float X = 16.0f * S;
	// Under the log when it is open, so the two never overlap.
	const float Top = From.bShowLog ? LogBottom + 16.0f * S : 12.0f * S + 2.0f * RowHeight * S + 18.0f * S;
	const float Y = FMath::Max(Top, (Canvas->ClipY - H) * 0.5f);
	Panel(X - 6.0f * S, Y - 8.0f * S, W, H, FLinearColor(0.03f, 0.05f, 0.08f, 0.9f), FLinearColor(1, 1, 1, 0.12f), 1.0f);
	float Row = Y;
	for (int32 Team = 0; Team < 2; ++Team)
	{
		for (const TMSim::FUnit& Unit : Battle.Units)
		{
			if (Unit.Team != Team || (!Unit.IsAlive() && !Unit.IsKo()))
			{
				continue;
			}
			const bool bFogged = !From.IsSeen(Unit);
			const float Alpha = bFogged ? 0.5f : 1.0f;
			if (Unit.Id == From.SelectedId)
			{
				DrawRect(FLinearColor(1, 1, 1, 0.08f), X - 4.0f * S, Row - 2.0f * S, W - 4.0f * S, RowH - 2.0f * S);
			}
			const FString Name = FString::Printf(TEXT("%s %d"), *JobName(Unit), Unit.Id);
			Text(Name, X, Row + 4.0f * S, TeamColour(Team) * FLinearColor(1, 1, 1, Alpha), Font, 0.52f * S);
			const float BarX = X + 140.0f * S;
			const float BarW = 90.0f * S;
			const float Part = static_cast<float>(Unit.Hp) / FMath::Max(1, Unit.MaxHp());
			DrawRect(FLinearColor(0, 0, 0, 0.55f), BarX, Row + 7.0f * S, BarW, 14.0f * S);
			if (!bFogged)
			{
				DrawRect(Part < 0.35f ? Urgent : FLinearColor(0.35f, 0.82f, 0.4f), BarX, Row + 7.0f * S, BarW * Part, 14.0f * S);
			}
			Text(bFogged ? FString(TEXT("?")) : FString::FromInt(Unit.Hp), BarX + 4.0f * S, Row + 5.0f * S, TextColour, Font, 0.42f * S);
			FString State;
			FLinearColor StateColour = Dim;
			if (!Unit.IsAlive())
			{
				State = TEXT("DOWN");
				StateColour = Urgent;
			}
			else if (Unit.bReady)
			{
				State = FString::Printf(TEXT("READY %d"), FMath::CeilToInt(Unit.Clock / Tps));
				StateColour = Gold;
			}
			else if (Unit.IsCasting())
			{
				State = FString::Printf(TEXT("cast %.1f"), Unit.Casting.Ticks / Tps);
				StateColour = CastColour;
			}
			else
			{
				State = FString::Printf(TEXT("%.1fs"), Battle.TicksToReady(Unit) / Tps);
			}
			Text(State, BarX + BarW + 8.0f * S, Row + 5.0f * S, StateColour * FLinearColor(1, 1, 1, Alpha), Font, 0.46f * S);
			AddButton(X - 4.0f * S, Row - 2.0f * S, W - 4.0f * S, RowH - 2.0f * S, ETMHudAction::PickUnit, Unit.Id);
			FString Tip = bFogged ? FString(TEXT("Hidden by the fog of war")) : FString::Printf(TEXT("%s  hp %d/%d"), *Name, Unit.Hp, Unit.MaxHp());
			if (!bFogged)
			{
				for (const TMSim::FStatus& Status : Unit.Statuses)
				{
					Tip += FString::Printf(TEXT("\n%hs (%d turns)"), Status.Id.c_str(), Status.Turns);
				}
			}
			AddTip(X - 4.0f * S, Row - 2.0f * S, W - 4.0f * S, RowH - 2.0f * S, Tip);
			Row += RowH;
		}
	}
}

// -------------------------------------------------------------- Unit Guide

void ATMBattleHud::DrawGuide(ATMBattleDirector& From)
{
	// unit_guide.gd: every class's stats, and one class's abilities with what
	// each does to a chosen target on level ground, before buffs. The numbers
	// are the rules' own (CalcAmount, EvadeChance), not worked out again here.
	TMSim::FBattle& Battle = From.Battle;
	const std::vector<const TMSim::FJobDef*>& Jobs = TMSim::AllJobs();
	if (Jobs.empty())
	{
		return;
	}
	From.GuideJob = FMath::Clamp(From.GuideJob, 0, static_cast<int32>(Jobs.size()) - 1);
	From.GuideAgainst = FMath::Clamp(From.GuideAgainst, 0, static_cast<int32>(Jobs.size()) - 1);
	UFont* Font = GEngine->GetMediumFont();
	UFont* Big = GEngine->GetLargeFont();

	DrawRect(FLinearColor(0.02f, 0.03f, 0.06f, 0.75f), 0.0f, 0.0f, Canvas->ClipX, Canvas->ClipY);
	const float PW = FMath::Min(Canvas->ClipX - 40.0f * S, 1500.0f * S);
	const float PH = Canvas->ClipY - 60.0f * S;
	const float PX = (Canvas->ClipX - PW) * 0.5f;
	const float PY = 30.0f * S;
	Panel(PX, PY, PW, PH, FLinearColor(0.05f, 0.06f, 0.1f, 0.97f), FLinearColor(0.4f, 0.45f, 0.55f, 0.8f), 1.5f);
	Text(TEXT("Unit Guide"), PX + 24.0f * S, PY + 16.0f * S, Gold, Big, 0.9f * S);
	MenuButton(PX + PW - 150.0f * S, PY + 16.0f * S, 126.0f * S, 36.0f * S, TEXT("Close  (U)"), ETMHudAction::ToggleGuide);

	// Every class's stats, one row each; click a row to see its abilities.
	const TMSim::EStat Columns[] =
	{
		TMSim::EStat::Hp, TMSim::EStat::AttDef, TMSim::EStat::MagDef, TMSim::EStat::AEva, TMSim::EStat::MEva,
		TMSim::EStat::Crit, TMSim::EStat::Speed, TMSim::EStat::Move, TMSim::EStat::Patience, TMSim::EStat::Sight
	};
	float Y = PY + 70.0f * S;
	const float NameW = 200.0f * S;
	const float RoleW = 190.0f * S;
	const float ColW = FMath::Min(95.0f * S, (PW - 48.0f * S - NameW - RoleW) / 10.0f);
	float X = PX + 24.0f * S;
	Text(TEXT("Class"), X, Y, Dim, Font, 0.5f * S);
	Text(TEXT("Role"), X + NameW, Y, Dim, Font, 0.5f * S);
	for (int32 c = 0; c < 10; ++c)
	{
		Text(ShownStatName(Columns[c]), X + NameW + RoleW + c * ColW, Y, Dim, Font, 0.5f * S);
	}
	Y += 26.0f * S;
	for (int32 j = 0; j < static_cast<int32>(Jobs.size()); ++j)
	{
		const TMSim::FJobDef& Job = *Jobs[j];
		const bool bChosen = j == From.GuideJob;
		const float RowH = 30.0f * S;
		if (bChosen)
		{
			DrawRect(FLinearColor(Gold.R, Gold.G, Gold.B, 0.12f), X - 6.0f * S, Y - 3.0f * S, PW - 36.0f * S, RowH);
		}
		Text(UTF8_TO_TCHAR(Job.Name.c_str()), X, Y, bChosen ? Gold : TextColour, Font, 0.56f * S);
		FString Roles;
		for (const std::string& JobRole : Job.Roles)
		{
			Roles += (Roles.IsEmpty() ? TEXT("") : TEXT(", ")) + FString(UTF8_TO_TCHAR(JobRole.c_str()));
		}
		Text(Roles, X + NameW, Y, Dim, Font, 0.5f * S);
		for (int32 c = 0; c < 10; ++c)
		{
			Text(FString::FromInt(Job.Stats.Get(Columns[c])), X + NameW + RoleW + c * ColW, Y, TextColour, Font, 0.54f * S);
		}
		AddButton(X - 6.0f * S, Y - 3.0f * S, PW - 36.0f * S, RowH, ETMHudAction::GuideJob, j);
		Y += RowH + 2.0f * S;
	}

	// The chosen class's abilities, worked out against one target.
	const TMSim::FJobDef& Job = *Jobs[From.GuideJob];
	const TMSim::FJobDef& Against = *Jobs[From.GuideAgainst];
	Y += 18.0f * S;
	Text(FString::Printf(TEXT("%hs's abilities"), Job.Name.c_str()), X, Y, Gold, Big, 0.6f * S);
	Text(TEXT("Worked out against"), X + 420.0f * S, Y + 4.0f * S, Dim, Font, 0.52f * S);
	MenuButton(X + 600.0f * S, Y, 220.0f * S, 32.0f * S, UTF8_TO_TCHAR(Against.Name.c_str()), ETMHudAction::GuideAgainst);
	Text(TEXT("on level ground, facing, before buffs (click to change)"), X + 830.0f * S, Y + 6.0f * S, Dim, Font, 0.44f * S);
	Y += 44.0f * S;

	// Two stand-in units, one the class and one the target, facing each other on
	// level ground. They are only asked questions; neither is in any battle.
	TMSim::FUnit User;
	User.Id = -100;
	User.Team = 0;
	User.Job = Job.Id;
	User.Stats = &Job.Stats;
	User.Hp = Job.Stats.Get(TMSim::EStat::Hp);
	User.Pos = TMSim::FVec2(0.0f, 0.0f);
	User.Facing = TMSim::FVec2(1.0f, 0.0f);
	TMSim::FUnit Target;
	Target.Id = -101;
	Target.Team = 1;
	Target.Job = Against.Id;
	Target.Stats = &Against.Stats;
	Target.Hp = Against.Stats.Get(TMSim::EStat::Hp);
	Target.Pos = TMSim::FVec2(1.0f, 0.0f);
	Target.Facing = TMSim::FVec2(-1.0f, 0.0f);

	const float Cols[] = { 0.0f, 200.0f, 380.0f, 560.0f, 700.0f, 830.0f };
	const TCHAR* Heads[] = { TEXT("Ability"), TEXT("Does"), TEXT("Shape / range"), TEXT("Cast"), TEXT("Cooldown"), TEXT("To that target") };
	for (int32 c = 0; c < 6; ++c)
	{
		Text(Heads[c], X + Cols[c] * S, Y, Dim, Font, 0.5f * S);
	}
	Y += 26.0f * S;
	for (int32 Slot = 0; Slot < 4; ++Slot)
	{
		const TMSim::FAbility* Ability = TMSim::JobAbility(Job.Id, Slot);
		if (!Ability)
		{
			continue;
		}
		FString ClassName;
		const FLinearColor Tint = AbilityClassColour(*Ability, &ClassName);
		Text(FString::Printf(TEXT("%s  %hs"), Slot == 3 ? TEXT("U") : *FString::FromInt(Slot + 1), Ability->Name.c_str()),
			X, Y, Tint, Font, 0.56f * S);
		Text(ClassName, X + Cols[1] * S, Y, Dim, Font, 0.5f * S);
		const FString Reach = Ability->MaxRange == 0.0f ? FString(TEXT("self"))
			: FString::Printf(TEXT("%s-%s m"), *Num(Ability->MinRange), *Num(Ability->MaxRange));
		Text(FString::Printf(TEXT("%hs  %s%s"), TMSim::ShapeOf(*Ability).c_str(), *Reach,
			Ability->Aoe > 0.0f ? *FString::Printf(TEXT("  r%s"), *Num(Ability->Aoe)) : TEXT("")),
			X + Cols[2] * S, Y, TextColour, Font, 0.5f * S);
		Text(Ability->Cast > 0.0f ? FString::Printf(TEXT("%ss"), *Num(Battle.CastTicks(*Ability) / Tps)) : FString(TEXT("instant")),
			X + Cols[3] * S, Y, TextColour, Font, 0.5f * S);
		Text(Ability->Cooldown > 0 ? FString::Printf(TEXT("%d turns"), Ability->Cooldown) : FString(TEXT("-")),
			X + Cols[4] * S, Y, TextColour, Font, 0.5f * S);

		// What it does to that target, as the rules work it out.
		FString Result;
		if (Ability->Effect == TMSim::EEffect::Damage)
		{
			const int32 Amount = Battle.CalcAmount(User, *Ability, User.Pos, Target, Target.Pos, 1, 1);
			Result = FString::Printf(TEXT("-%d  (%d%% miss, %d%% crit)"), Amount,
				Battle.EvadeChance(Target, *Ability, &User), Battle.CritChance(User));
		}
		else if (Ability->Effect == TMSim::EEffect::Heal || Ability->Effect == TMSim::EEffect::Revive)
		{
			// Healing is done to a friend, so it is worked out on the class itself.
			TMSim::FUnit Friend = User;
			Friend.Id = -102;
			const int32 Amount = Battle.CalcAmount(User, *Ability, User.Pos, Friend, User.Pos, 1, 1);
			Result = Ability->Effect == TMSim::EEffect::Heal ? FString::Printf(TEXT("+%d"), Amount)
				: FString::Printf(TEXT("up with %d"), Amount);
		}
		else
		{
			Result = Ability->HasStatus() ? FString(UTF8_TO_TCHAR(Ability->StatusId.c_str())) : FString(TEXT("see tooltip"));
		}
		Text(Result, X + Cols[5] * S, Y, Tint, Font, 0.54f * S);
		AddTip(X, Y - 3.0f * S, PW - 48.0f * S, 30.0f * S, FString::Printf(TEXT("%hs  -  %s\n\n"), Ability->Name.c_str(), *ClassName)
			+ ExplainAbility(From, User, Slot));
		Y += 32.0f * S;
	}
	Text(TEXT("Rest the pointer on a row to see how its numbers are worked out. The other 81 classes arrive with the class importer."),
		X, PY + PH - 34.0f * S, Dim, Font, 0.46f * S);
}
