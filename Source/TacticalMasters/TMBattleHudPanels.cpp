// The battle HUD's panels: the turn order, the log, the unit cards, the list of
// every unit, the corner buttons, the Unit Guide and the tooltips that explain
// the numbers. The frame, the board markings, the action bar and the menus are
// in TMBattleHud.cpp.

#include "TMBattleHud.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "CanvasItem.h"
#include "ImageUtils.h"
#include "ImageCore.h"
#include "Misc/Paths.h"
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
	const FPanelScale Sized(*this, TEXT("turn_order"));
	// Two bars, one per team. A chip slides along its team's bar toward the READY
	// zone at the left, placed by seconds until ready on a square-root scale so
	// the last seconds get the most room; far-off turns are drawn smaller, chips
	// that would touch merge into a framed group, and each glides rather than
	// jumps (hud.gd:52-74, 307-311, 1226-1387).
	const TMSim::FBattle& Battle = From.Battle;
	const float X0 = 16.0f * S + Nudge(TEXT("turn_order")).X;
	const float Y0 = 12.0f * S + Nudge(TEXT("turn_order")).Y;
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

	Movable(TEXT("turn_order"), TEXT("Turn order bars"), X0 - 6.0f * S, Y0 - 3.0f * S, Width + 12.0f * S, 2.0f * RowHeight * S);

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

// ------------------------------------------------------------ turn squares

void ATMBattleHud::DrawTurnSquares(ATMBattleDirector& From)
{
	// hud.gd:318-470. A square per unit, one row per side, each row movable on
	// its own and its squares reorderable (Edit layout). Waiting, a square is
	// grey and a red meter fills from the bottom as its gauge fills; on its
	// turn it goes gold, its border pulses, and the meter drains as its
	// countdown runs out. The badge is the seconds to either, or to a cast.
	const TMSim::FBattle& Battle = From.Battle;
	UFont* Font = GEngine->GetMediumFont();
	const float Pulse = 0.55f + 0.45f * FMath::Sin((GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0f) * 1000.0f / 180.0f);
	for (int32 Team = 0; Team < 2; ++Team)
	{
		const FPanelScale Sized(*this, Team == 0 ? TEXT("turn_cards_blue") : TEXT("turn_cards_red"));
		const float SW = 56.0f * S;
		const float SH = 62.0f * S;
		const float Gap = 5.0f * S;
		// The order the player left them in, then anyone else by id.
		TArray<const TMSim::FUnit*> Row;
		for (int32 Id : FTMSettings::Get().CardOrder[Team])
		{
			const TMSim::FUnit* Unit = nullptr;
			for (const TMSim::FUnit& Each : Battle.Units)
			{
				if (Each.Id == Id)
				{
					Unit = &Each;
				}
			}
			if (Unit && Unit->Team == Team && Unit->IsAlive())
			{
				Row.AddUnique(Unit);
			}
		}
		for (const TMSim::FUnit& Unit : Battle.Units)
		{
			if (Unit.Team == Team && Unit.IsAlive())
			{
				Row.AddUnique(&Unit);
			}
		}
		// Centred at the top, a row either side of the middle, as Atlas Reactor
		// lays its teams out: the player's side on the left, the enemy's on the right.
		const FVector2D Moved = Nudge(Team == 0 ? TEXT("turn_cards_blue") : TEXT("turn_cards_red"));
		const float RowW = FMath::Max(0, Row.Num()) * (SW + Gap) - Gap;
		const float Middle = Canvas->ClipX * 0.5f;
		const float Apart = 36.0f * BaseS;
		const bool bLeft = Team == From.FriendTeam();
		const float RowX = (bLeft ? Middle - Apart - RowW : Middle + Apart) + Moved.X;
		const float RowY = 12.0f * S + Moved.Y;
		float X = RowX;
		for (const TMSim::FUnit* Unit : Row)
		{
			const bool bFogged = !From.IsSeen(*Unit);
			const float Seconds = SecondsLeft(*Unit, Battle);
			FLinearColor Fill(0.07f, 0.08f, 0.11f, 0.85f);
			FLinearColor Edge(1.0f, 1.0f, 1.0f, 0.2f);
			FLinearColor Meter(0.8f, 0.25f, 0.2f, 0.45f);
			float Level = static_cast<float>(Unit->Tg) / TMSim::Pace::TgMax;
			FString Badge = FString::Printf(TEXT("%ds"), FMath::CeilToInt(Seconds));
			FLinearColor BadgeColour = Dim;
			if (Unit->bReady)
			{
				Fill = FLinearColor(0.45f, 0.35f, 0.08f, 0.9f);
				Edge = FLinearColor(Gold.R, Gold.G, Gold.B, Pulse);
				Meter = FLinearColor(1.0f, 0.82f, 0.35f, 0.35f);
				const int32 Full = FMath::Max(1, Battle.ClockTicks(*Unit));
				Level = FMath::Clamp(static_cast<float>(Unit->Clock) / Full, 0.0f, 1.0f);
				BadgeColour = Gold;
			}
			else if (Unit->IsCasting())
			{
				Badge = FString::Printf(TEXT("%.1fs"), Seconds);
				BadgeColour = CastColour;
			}
			const float Alpha = bFogged ? 0.45f : 1.0f;
			Fill.A *= Alpha;
			Meter.A *= Alpha;
			float Thick = Unit->bReady ? 2.5f : 1.5f;
			if (Unit->Id == From.SelectedId)
			{
				Edge = FLinearColor::White;
				Thick = 3.5f;
			}
			Panel(X, RowY, SW, SH, Fill, Edge, Thick);
			DrawRect(Meter, X + 2.0f, RowY + SH - SH * Level, SW - 4.0f, SH * Level - 2.0f);
			// A strip of the side's colour along the top, so the rows read apart:
			// blue for the player's side, red for the enemy's, as everywhere else.
			DrawRect(SideColour(From.IsFriend(*Unit)) * FLinearColor(1, 1, 1, 0.9f * Alpha), X + 2.0f, RowY + 2.0f, SW - 4.0f, 4.0f * S);
			// Its class's icon; a question mark for one hidden in the fog.
			const float Face = 34.0f * S;
			UTexture2D* Picture0 = bFogged ? nullptr : ClassIcon(*Unit);
			if (Picture0)
			{
				Picture(Picture0, X + (SW - Face) * 0.5f, RowY + 7.0f * S, Face, Face,
					FLinearColor(1, 1, 1, (Unit->bReady ? 1.0f : 0.8f) * Alpha));
			}
			else
			{
				const FString Mark = bFogged ? FString(TEXT("?")) : Initials(JobName(*Unit));
				const float Letters = 0.62f * S;
				const FVector2D MarkSize = TextSize(Mark, Font, Letters);
				Text(Mark, X + (SW - MarkSize.X) * 0.5f, RowY + 8.0f * S, (Unit->bReady ? TextColour : Dim) * FLinearColor(1, 1, 1, Alpha), Font, Letters);
			}
			const float BadgeScale = 0.42f * S;
			const FVector2D BadgeSize = TextSize(Badge, Font, BadgeScale);
			Text(Badge, X + (SW - BadgeSize.X) * 0.5f, RowY + SH - BadgeSize.Y - 2.0f * S, BadgeColour * FLinearColor(1, 1, 1, Alpha), Font, BadgeScale);
			AddButton(X, RowY, SW, SH, ETMHudAction::PickUnit, Unit->Id);
			SquareAreas.Add(Unit->Id, FBox2D(FVector2D(X, RowY), FVector2D(X + SW, RowY + SH)));
			FString Tip;
			if (bFogged)
			{
				Tip = TEXT("Hidden by the fog of war");
			}
			else
			{
				Tip = FString::Printf(TEXT("%s %s %d  hp %d/%d"), Team == 0 ? TEXT("Blue") : TEXT("Red"),
					*JobName(*Unit), Unit->Id, Unit->Hp, Unit->MaxHp());
				if (Unit->IsCasting())
				{
					const TMSim::FAbility* Ability = TMSim::JobAbility(Unit->Job, Unit->Casting.Slot);
					Tip += FString::Printf(TEXT("\nCasting %hs"), Ability ? Ability->Name.c_str() : "");
				}
				Tip += TEXT("\n\n") + ExplainTurn(From, *Unit) + TEXT("\n") + ExplainCountdown(From, *Unit);
			}
			AddTip(X, RowY, SW, SH, Tip);
			X += SW + Gap;
		}
		if (Row.Num() > 0)
		{
			Movable(Team == 0 ? TEXT("turn_cards_blue") : TEXT("turn_cards_red"), Team == 0 ? TEXT("Blue squares") : TEXT("Red squares"),
				RowX - 3.0f * S, RowY - 3.0f * S, X - RowX - Gap + 6.0f * S, SH + 6.0f * S);
		}
	}
}

// --------------------------------------------------------------------- log

void ATMBattleHud::DrawLog(ATMBattleDirector& From)
{
	const FPanelScale Sized(*this, TEXT("log"));
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
	const float X = 16.0f * S + Nudge(TEXT("log")).X;
	const float Y = 12.0f * S + 2.0f * RowHeight * S + 18.0f * S + Nudge(TEXT("log")).Y;
	const float W = 520.0f * S;
	const float Head = 24.0f * S;
	const int32 Last = FMath::Max(0, From.Log.Num() - From.LogScroll);
	const int32 First = FMath::Max(0, Last - Shown);
	const int32 Count = Last - First;
	const float H = Head + FMath::Max(1, Count) * LineH + 8.0f * S;
	Panel(X - 6.0f * S, Y - 4.0f * S, W, H, FLinearColor(0.06f, 0.08f, 0.12f, 0.62f), FLinearColor(1, 1, 1, 0.12f), 1.0f);
	LogArea = FBox2D(FVector2D(X - 6.0f * S, Y - 4.0f * S), FVector2D(X - 6.0f * S + W, Y - 4.0f * S + H));
	Movable(TEXT("log"), TEXT("Combat log"), X - 6.0f * S, Y - 4.0f * S, W, H);
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

// ------------------------------------------------------- icons and shapes

UTexture2D* ATMBattleHud::Icon(const FString& Name, bool bGrey)
{
	const FString Key = bGrey ? Name + TEXT("#grey") : Name;
	if (TObjectPtr<UTexture2D>* Known = Icons.Find(Key))
	{
		return *Known;
	}
	if (NoIcon.Contains(Name))
	{
		return nullptr;
	}
	// Pictures made from the Godot game's own icons (Tools/import_icons.py),
	// read at run time: nothing here is an asset made in the editor.
	FImage Image;
	const FString File = FPaths::ProjectContentDir() / TEXT("Data/Icons") / (Name + TEXT(".png"));
	if (!FPaths::FileExists(File) || !FImageUtils::LoadImage(*File, Image))
	{
		NoIcon.Add(Name);
		return nullptr;
	}
	Image.ChangeFormat(ERawImageFormat::BGRA8, EGammaSpace::sRGB);
	if (bGrey)
	{
		// What cannot be used yet is grey, keeping its shape and shading.
		for (FColor& Pixel : Image.AsBGRA8())
		{
			const uint8 Level = static_cast<uint8>(FMath::Clamp(0.3f * Pixel.R + 0.59f * Pixel.G + 0.11f * Pixel.B, 0.0f, 255.0f) * 0.7f);
			Pixel = FColor(Level, Level, Level, Pixel.A);
		}
	}
	UTexture2D* Texture = FImageUtils::CreateTexture2DFromImage(Image);
	if (!Texture)
	{
		NoIcon.Add(Name);
		return nullptr;
	}
	Icons.Add(Key, Texture);
	return Texture;
}

UTexture2D* ATMBattleHud::AbilityIcon(const TMSim::FAbility& Ability, bool bGrey)
{
	// Its own, or one for what it does, as Godot chooses (jobs.gd:452-467).
	if (UTexture2D* Own = Icon(TEXT("abilities/") + FString(UTF8_TO_TCHAR(Ability.Id.c_str())), bGrey))
	{
		return Own;
	}
	const TCHAR* Glyph = TEXT("damage");
	switch (Ability.Effect)
	{
	case TMSim::EEffect::Heal: Glyph = TEXT("heal"); break;
	case TMSim::EEffect::Revive: Glyph = TEXT("revive"); break;
	case TMSim::EEffect::Support:
		Glyph = Ability.Target == TMSim::ETargetSide::Enemy ? TEXT("debuff")
			: (!Ability.Buffs.empty() || Ability.TgChange != 0 ? TEXT("buff") : TEXT("status"));
		break;
	default: break;
	}
	return Icon(FString(TEXT("abilities/any_")) + Glyph, bGrey);
}

UTexture2D* ATMBattleHud::ClassIcon(const TMSim::FUnit& Unit)
{
	// Its own, the one its file names, or the generic one (jobs.gd:479-487).
	if (UTexture2D* Own = Icon(TEXT("classes/") + FString(UTF8_TO_TCHAR(Unit.Job.c_str())), false))
	{
		return Own;
	}
	const TMSim::FJobDef* Job = TMSim::FindJob(Unit.Job);
	if (Job && !Job->Icon.empty())
	{
		if (UTexture2D* Named = Icon(TEXT("classes/") + FString(UTF8_TO_TCHAR(Job->Icon.c_str())), false))
		{
			return Named;
		}
	}
	return Icon(TEXT("classes/generic"), false);
}

void ATMBattleHud::Picture(UTexture* Texture, float X, float Y, float W, float H, const FLinearColor& Tint)
{
	if (!Texture || !Canvas)
	{
		return;
	}
	FCanvasTileItem Tile(FVector2D(X, Y), Texture->GetResource(), FVector2D(W, H), Tint);
	Tile.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Tile);
}

void ATMBattleHud::Slant(float X, float Y, float W, float H, const FLinearColor& Colour, float Skew)
{
	if (!Canvas || W <= 0.0f)
	{
		return;
	}
	// A parallelogram leaning right by Skew pixels over its height.
	const FVector2D A(X + Skew, Y);
	const FVector2D B(X + W + Skew, Y);
	const FVector2D C(X + W, Y + H);
	const FVector2D D(X, Y + H);
	FCanvasTriangleItem One(A, B, C, GWhiteTexture);
	One.SetColor(Colour);
	One.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(One);
	FCanvasTriangleItem Two(A, C, D, GWhiteTexture);
	Two.SetColor(Colour);
	Two.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Two);
}

void ATMBattleHud::Bar(float X, float Y, float W, float H, float Fraction, const FLinearColor& Fill, const FLinearColor& Back, float Skew)
{
	Slant(X, Y, W, H, Back, Skew);
	Slant(X, Y, W * FMath::Clamp(Fraction, 0.0f, 1.0f), H, Fill, Skew);
	// A light edge along the top, which is most of what makes a flat bar read as a bar.
	Slant(X, Y, W * FMath::Clamp(Fraction, 0.0f, 1.0f), FMath::Max(1.0f, H * 0.18f), FLinearColor(1.0f, 1.0f, 1.0f, 0.25f), Skew * 0.18f);
}

float ATMBattleHud::StatusChips(const TMSim::FUnit& Unit, float X, float Y, float Size, bool bLeftward, bool bTips)
{
	UFont* Font = GEngine->GetSmallFont();
	const float Gap = Size * 0.15f;
	float Used = 0.0f;
	for (const TMSim::FStatus& Status : Unit.Statuses)
	{
		const TMSim::FStatusDef* Def = TMSim::FindStatus(Status.Id);
		const FStatusLook& Look = StatusLook(Status.Id);
		const float At = bLeftward ? X - Used - Size : X + Used;
		DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.7f), At - 1.0f, Y - 1.0f, Size + 2.0f, Size + 2.0f);
		DrawRect(Look.Colour * FLinearColor(0.8f, 0.8f, 0.8f, 1.0f), At, Y, Size, Size);
		DrawRect(Look.Colour, At, Y, Size, Size * 0.45f);
		const FString Tag = Def ? FString(UTF8_TO_TCHAR(Def->Tag)) : FString(UTF8_TO_TCHAR(Status.Id.c_str())).Left(3).ToUpper();
		const float Scale = Size / 60.0f;
		const FVector2D TagSize = TextSize(Tag, Font, Scale);
		Text(Tag, At + (Size - TagSize.X) * 0.5f, Y + (Size - TagSize.Y) * 0.5f - Size * 0.08f, FLinearColor(0.05f, 0.05f, 0.08f), Font, Scale, false);
		const FString Turns = FString::FromInt(Status.Turns);
		const FVector2D TurnSize = TextSize(Turns, Font, Scale * 0.8f);
		Text(Turns, At + Size - TurnSize.X - 1.0f, Y + Size - TurnSize.Y, FLinearColor::White, Font, Scale * 0.8f);
		if (bTips)
		{
			FString Tip = FString::Printf(TEXT("%s  (%d turn%s)"), Def ? UTF8_TO_TCHAR(Def->Name) : UTF8_TO_TCHAR(Status.Id.c_str()),
				Status.Turns, Status.Turns == 1 ? TEXT("") : TEXT("s"));
			if (Status.Amount > 0)
			{
				Tip += FString::Printf(TEXT("\nSoaks %d more"), Status.Amount);
			}
			if (*Look.Desc)
			{
				Tip += TEXT("\n") + FString(Look.Desc);
			}
			AddTip(At, Y, Size, Size, Tip);
		}
		Used += Size + Gap;
	}
	return Used;
}

// ------------------------------------------------------------- over heads

void ATMBattleHud::DrawOverheads(ATMBattleDirector& From)
{
	// Name, health and statuses over every unit, as Atlas Reactor floats them:
	// blue for an ally and red for an enemy, nearest drawn last so it is on top.
	if (!PlayerOwner)
	{
		return;
	}
	FVector Eye;
	FRotator Look;
	PlayerOwner->GetPlayerViewPoint(Eye, Look);
	struct FHead
	{
		const TMSim::FUnit* Unit;
		FVector2D At;
		float Distance;
	};
	TArray<FHead> Heads;
	for (const TMSim::FUnit& Unit : From.Battle.Units)
	{
		if ((!Unit.IsAlive() && !Unit.IsKo()) || !From.IsSeen(Unit))
		{
			continue;
		}
		const FVector World = From.GetActorTransform().TransformPosition(From.ShownAt(Unit) + FVector(0.0f, 0.0f, Unit.IsAlive() ? 262.0f : 70.0f));
		FVector2D At;
		if (PlayerOwner->ProjectWorldLocationToScreen(World, At))
		{
			Heads.Add({ &Unit, At, static_cast<float>(FVector::Dist(Eye, World)) });
		}
	}
	Heads.Sort([](const FHead& A, const FHead& B) { return A.Distance > B.Distance; });

	UFont* Font = GEngine->GetMediumFont();
	for (const FHead& Head : Heads)
	{
		const TMSim::FUnit& Unit = *Head.Unit;
		const bool bFriend = From.IsFriend(Unit);
		const float W = 124.0f * S;
		const float H = 14.0f * S;
		const float X = Head.At.X - W * 0.5f;
		float Y = Head.At.Y;

		// The name, gold for the unit being ordered.
		const FString Name = FString::Printf(TEXT("%s %d"), *JobName(Unit), Unit.Id);
		const FVector2D NameSize = TextSize(Name, Font, 0.36f * S);
		Text(Name, Head.At.X - NameSize.X * 0.5f, Y - NameSize.Y - 1.0f * S,
			Unit.Id == From.SelectedId ? Gold : TextColour, Font, 0.36f * S);

		if (!Unit.IsAlive())
		{
			Bar(X, Y, W, H, 0.0f, Dim, FLinearColor(0.1f, 0.1f, 0.12f, 0.85f), 6.0f * S);
			Text(FString::Printf(TEXT("DOWN %.0fs"), Unit.KoTicks / Tps), X + 10.0f * S, Y - 1.0f * S, Urgent, Font, 0.34f * S);
			continue;
		}

		// Health, with a shield's worth drawn on after it in white.
		const float MaxHp = static_cast<float>(FMath::Max(1, Unit.MaxHp()));
		int32 Soak = 0;
		for (const TMSim::FStatus& Status : Unit.Statuses)
		{
			const TMSim::FStatusDef* Def = TMSim::FindStatus(Status.Id);
			Soak += Def && Def->bAbsorbs ? Status.Amount : 0;
		}
		if (Unit.bReady)
		{
			// Its turn: a gold frame round its health.
			Slant(X - 2.0f * S, Y - 2.0f * S, W + 4.0f * S, H + 4.0f * S, Gold, 6.0f * S);
		}
		Bar(X, Y, W, H, Unit.Hp / MaxHp, SideColour(bFriend), FLinearColor(0.05f, 0.05f, 0.08f, 0.9f), 6.0f * S);
		if (Soak > 0)
		{
			const float From0 = W * FMath::Clamp(Unit.Hp / MaxHp, 0.0f, 1.0f);
			Slant(X + From0, Y, FMath::Min(W - From0, W * Soak / MaxHp), H, FLinearColor(1.0f, 1.0f, 1.0f, 0.75f), 6.0f * S);
		}
		const FString Hp = FString::FromInt(Unit.Hp);
		Text(Hp, X + 10.0f * S, Y + (H - TextSize(Hp, Font, 0.34f * S).Y) * 0.5f, FLinearColor::White, Font, 0.34f * S);
		Y += H + 3.0f * S;

		// A spell on its way out: how long it has left.
		if (Unit.IsCasting())
		{
			const float Done = 1.0f - static_cast<float>(Unit.Casting.Ticks) / FMath::Max(1, Unit.Casting.Total);
			Bar(X, Y, W, 4.0f * S, Done, CastColour, FLinearColor(0.05f, 0.05f, 0.08f, 0.8f), 2.0f * S);
			Y += 7.0f * S;
		}

		// Its statuses, centred under the bar.
		const float Chip = 18.0f * S;
		const float Across = Unit.Statuses.size() * Chip * 1.15f - Chip * 0.15f;
		StatusChips(Unit, Head.At.X - Across * 0.5f, Y, Chip, false, false);
	}
}

// ------------------------------------------------------------ unit panels

void ATMBattleHud::AbilityTile(ATMBattleDirector& From, const TMSim::FUnit& Unit, int32 Slot, float X, float Y, float Size, bool bButton)
{
	const TMSim::FAbility* Ability = TMSim::JobAbility(Unit.Job, Slot);
	if (!Ability)
	{
		DrawRect(FLinearColor(0.05f, 0.06f, 0.09f, 0.6f), X, Y, Size, Size);
		return;
	}
	UFont* Font = GEngine->GetMediumFont();
	UFont* Big = GEngine->GetLargeFont();
	const bool bPassive = Ability->Kind == "passive" || Ability->Kind == "aura";
	const bool bCooling = Unit.Cooldowns[Slot] > 0;
	const bool bUltWaiting = Slot == 3 && Unit.Ult < TMSim::Pace::UltMax;
	const bool bBlocked = !From.Battle.AbilityBlockedReason(Unit, Slot).empty();
	const bool bUsable = bButton ? From.PlayerCanOrder(&Unit) && !Unit.bActed && !bBlocked : !bBlocked || Unit.bActed;
	const bool bColour = !bCooling && !bUltWaiting && !bPassive && bUsable;
	const bool bAiming = bButton && From.AimMode == ATMBattleDirector::EAimMode::Ability && From.AimSlot == Slot;
	const bool bOver = FBox2D(FVector2D(X, Y), FVector2D(X + Size, Y + Size)).IsInside(MousePoint());

	// The frame: gold for the ultimate, bright while aiming it.
	const FLinearColor Edge = bAiming ? Gold : (Slot == 3 ? Gold * FLinearColor(1, 1, 1, bColour ? 0.95f : 0.45f)
		: FLinearColor(0.55f, 0.62f, 0.75f, bColour ? 0.9f : 0.4f));
	DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.75f), X - 3.0f * S, Y - 3.0f * S, Size + 6.0f * S, Size + 6.0f * S);
	DrawRect(Edge, X - 2.0f * S, Y - 2.0f * S, Size + 4.0f * S, Size + 4.0f * S);
	DrawRect(bColour && bOver && bButton ? FLinearColor(0.16f, 0.2f, 0.3f, 1.0f) : FLinearColor(0.06f, 0.07f, 0.1f, 1.0f), X, Y, Size, Size);
	const float Inset = Size * 0.08f;
	Picture(AbilityIcon(*Ability, !bColour), X + Inset, Y + Inset, Size - 2.0f * Inset, Size - 2.0f * Inset,
		bColour || bPassive ? FLinearColor::White : FLinearColor(1, 1, 1, 0.85f));

	if (bUltWaiting)
	{
		// The meter rising up the tile, and how full it is.
		const float Part = static_cast<float>(Unit.Ult) / TMSim::Pace::UltMax;
		DrawRect(Gold * FLinearColor(1, 1, 1, 0.28f), X, Y + Size * (1.0f - Part), Size, Size * Part);
		const FString Pct = FString::Printf(TEXT("%d%%"), Unit.Ult);
		const FVector2D PctSize = TextSize(Pct, Font, Size / 170.0f);
		Text(Pct, X + (Size - PctSize.X) * 0.5f, Y + (Size - PctSize.Y) * 0.5f, Gold, Font, Size / 170.0f);
	}
	else if (bCooling)
	{
		// Turns until it can be used again, large enough to read at a glance.
		DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.35f), X, Y, Size, Size);
		const FString Turns = FString::FromInt(Unit.Cooldowns[Slot]);
		const FVector2D TurnSize = TextSize(Turns, Big, Size / 110.0f);
		Text(Turns, X + (Size - TurnSize.X) * 0.5f, Y + (Size - TurnSize.Y) * 0.5f, FLinearColor::White, Big, Size / 110.0f);
	}
	if (bPassive)
	{
		const FString Word = Ability->Kind == "aura" ? TEXT("AURA") : TEXT("PASSIVE");
		DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.6f), X, Y, Size, Size * 0.22f);
		Text(Word, X + 3.0f * S, Y + 1.0f * S, Dim, Font, Size / 300.0f);
	}
	else if (Ability->Cast > 0.0f && bColour)
	{
		// A cast time, in the corner.
		const FString Cast = FString::Printf(TEXT("%.1fs"), From.Battle.CastTicks(*Ability) / Tps);
		const FVector2D CastSize = TextSize(Cast, Font, Size / 300.0f);
		DrawRect(CastColour * FLinearColor(0.4f, 0.4f, 0.4f, 0.9f), X + Size - CastSize.X - 6.0f * S, Y, CastSize.X + 6.0f * S, CastSize.Y);
		Text(Cast, X + Size - CastSize.X - 3.0f * S, Y, FLinearColor::White, Font, Size / 300.0f);
	}
	if (bButton)
	{
		// Its key, bottom left.
		const FString Key = FTMSettings::Get().KeyName(static_cast<ETMAction>(static_cast<int32>(ETMAction::Ability1) + Slot));
		Text(Key, X + 4.0f * S, Y + Size - TextSize(Key, Font, Size / 260.0f).Y - 1.0f * S, bColour ? TextColour : Dim, Font, Size / 260.0f);
		AddButton(X, Y, Size, Size, ETMHudAction::Ability, Slot);
	}
	FString ClassName;
	AbilityClassColour(*Ability, &ClassName);
	AddTip(X, Y, Size, Size, FString::Printf(TEXT("%hs  -  %s\n\n"), Ability->Name.c_str(), *ClassName) + ExplainAbility(From, Unit, Slot));
}

void ATMBattleHud::DrawUnitPanel(ATMBattleDirector& From, const TMSim::FUnit& Unit, bool bRight)
{
	const TCHAR* Id = bRight ? TEXT("inspect") : TEXT("unit_card");
	const FPanelScale Sized(*this, Id);
	TMSim::FBattle& Battle = From.Battle;
	UFont* Font = GEngine->GetMediumFont();
	UFont* Big = GEngine->GetLargeFont();
	const bool bFriend = From.IsFriend(Unit);
	const FLinearColor Side = SideColour(bFriend);

	const float Portrait = 150.0f * S;
	const float InfoW = 380.0f * S;
	const float Gap = 12.0f * S;
	const float W = Portrait + Gap + InfoW;
	const float Abilities = bRight ? 52.0f * S : 0.0f;
	const float H = Portrait + Abilities;
	const float X = (bRight ? Canvas->ClipX - W - 16.0f * S : 16.0f * S) + Nudge(Id).X;
	const float Y = Canvas->ClipY - H - 16.0f * S + Nudge(Id).Y;
	Movable(Id, bRight ? TEXT("Enemy panel") : TEXT("Unit panel"), X, Y, W, H);

	// Who is casting at it, above the panel.
	float Warned = 0.0f;
	for (const TMSim::FUnit* Caster : CastersAt(Battle, Unit))
	{
		Warned += From.IsSeen(*Caster) ? 20.0f * S : 0.0f;
	}
	if (Warned > 0.0f)
	{
		DrawIncoming(From, Unit, X, Y - Warned - 4.0f * S, W);
	}

	// A dark slanted ground behind the words, leaning away from the portrait.
	const float PX = bRight ? X + W - Portrait : X;
	const float IX = bRight ? X : X + Portrait + Gap;
	Slant(IX - (bRight ? 0.0f : 10.0f * S), Y + 18.0f * S, InfoW + 10.0f * S, Portrait - 18.0f * S, PanelFill, bRight ? -14.0f * S : 14.0f * S);

	// The portrait: the unit itself, filmed live, framed in its side's colour,
	// with its class's icon in the corner.
	DrawRect(Side, PX - 3.0f * S, Y - 3.0f * S, Portrait + 6.0f * S, Portrait + 6.0f * S);
	DrawRect(FLinearColor(0.02f, 0.03f, 0.05f, 1.0f), PX, Y, Portrait, Portrait);
	if (UTexture* Face = From.PortraitOf(bRight ? 1 : 0, Unit))
	{
		Picture(Face, PX, Y, Portrait, Portrait, Unit.IsAlive() ? FLinearColor::White : FLinearColor(0.4f, 0.4f, 0.4f, 1.0f));
	}
	const float Badge = 42.0f * S;
	DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.6f), PX + 4.0f * S, Y + 4.0f * S, Badge, Badge);
	Picture(ClassIcon(Unit), PX + 4.0f * S, Y + 4.0f * S, Badge, Badge);
	AddTip(PX, Y, Portrait, Portrait, FString::Printf(TEXT("%s %d\nDEF %d  MDF %d  CRIT %d%%\nAEV %d%%  MEV %d%%  SPD %d  MOV %sm  PAT %d  SGT %sm\n\n"),
		*JobName(Unit), Unit.Id, Unit.Stat(TMSim::EStat::AttDef), Unit.Stat(TMSim::EStat::MagDef), Unit.Stat(TMSim::EStat::Crit),
		Unit.Stat(TMSim::EStat::AEva), Unit.Stat(TMSim::EStat::MEva), Unit.Stat(TMSim::EStat::Speed), *Num(Battle.MoveOf(Unit)),
		Unit.Stat(TMSim::EStat::Patience), *Num(Battle.SightOf(Unit)))
		+ ExplainMove(From, Unit) + TEXT("\n") + ExplainSight(From, Unit) + BuffText(Unit));

	// Words run away from the portrait: left to right on the left, right to left on the right.
	auto Place = [&](const FString& What, UFont* F, float Scale, float Row, const FLinearColor& Colour)
	{
		const float Wide = TextSize(What, F, Scale).X;
		Text(What, bRight ? IX + InfoW - Wide : IX, Row, Colour, F, Scale);
	};
	float Row = Y + 20.0f * S;
	Place(JobName(Unit).ToUpper(), Big, 0.85f * S, Row - 6.0f * S, Side);
	// When it acts, on the other end of the name's line.
	FString When;
	FLinearColor WhenColour = Dim;
	if (!Unit.IsAlive())
	{
		When = FString::Printf(TEXT("DOWN  %.0fs to raise"), Unit.KoTicks / Tps);
		WhenColour = Urgent;
	}
	else if (Unit.bReady)
	{
		When = FString::Printf(TEXT("READY  %ds"), FMath::CeilToInt(Unit.Clock / Tps));
		WhenColour = Unit.Clock / Tps <= 5.0f ? Urgent : Gold;
	}
	else
	{
		When = FString::Printf(TEXT("next turn %.1fs"), Battle.TicksToReady(Unit) / Tps);
	}
	const float WhenWide = TextSize(When, Font, 0.5f * S).X;
	Text(When, bRight ? IX : IX + InfoW - WhenWide, Row + 8.0f * S, WhenColour, Font, 0.5f * S);
	AddTip(bRight ? IX : IX + InfoW - WhenWide, Row, WhenWide, 30.0f * S, ExplainCountdown(From, Unit) + TEXT("\n") + ExplainTurn(From, Unit));
	Row += 38.0f * S;

	// Health: its side's colour, a shield's worth after it in white, the number over it.
	const float MaxHp = static_cast<float>(FMath::Max(1, Unit.MaxHp()));
	int32 Soak = 0;
	for (const TMSim::FStatus& Status : Unit.Statuses)
	{
		const TMSim::FStatusDef* Def = TMSim::FindStatus(Status.Id);
		Soak += Def && Def->bAbsorbs ? Status.Amount : 0;
	}
	const float HpH = 30.0f * S;
	Bar(IX, Row, InfoW, HpH, Unit.Hp / MaxHp, Side, FLinearColor(0.03f, 0.04f, 0.06f, 0.95f), 10.0f * S);
	if (Soak > 0)
	{
		const float Filled = InfoW * FMath::Clamp(Unit.Hp / MaxHp, 0.0f, 1.0f);
		Slant(IX + Filled, Row, FMath::Min(InfoW - Filled, InfoW * Soak / MaxHp), HpH, FLinearColor(1, 1, 1, 0.7f), 10.0f * S);
	}
	const FString Hp = Soak > 0 ? FString::Printf(TEXT("HP %d / %d   +%d"), Unit.Hp, Unit.MaxHp(), Soak) : FString::Printf(TEXT("HP %d / %d"), Unit.Hp, Unit.MaxHp());
	Text(Hp, IX + 16.0f * S, Row + (HpH - TextSize(Hp, Big, 0.5f * S).Y) * 0.5f, FLinearColor::White, Big, 0.5f * S);
	AddTip(IX, Row, InfoW, HpH, FString::Printf(TEXT("Max HP %d (class %s)%s%s"), Unit.MaxHp(), *JobName(Unit),
		Unit.Hp == Unit.MaxHp() ? TEXT("") : *FString::Printf(TEXT("\nMissing %d"), Unit.MaxHp() - Unit.Hp),
		Soak > 0 ? *FString::Printf(TEXT("\nShields soak %d more"), Soak) : TEXT("")));
	Row += HpH + 6.0f * S;

	// The Turn Gauge, or the spell it is casting.
	const float GaugeH = 16.0f * S;
	FString GaugeText;
	if (Unit.IsCasting())
	{
		const TMSim::FAbility* Ability = TMSim::JobAbility(Unit.Job, Unit.Casting.Slot);
		Bar(IX, Row, InfoW, GaugeH, 1.0f - static_cast<float>(Unit.Casting.Ticks) / FMath::Max(1, Unit.Casting.Total), CastColour,
			FLinearColor(0.03f, 0.04f, 0.06f, 0.95f), 5.0f * S);
		GaugeText = FString::Printf(TEXT("CASTING %hs  %.1fs"), Ability ? Ability->Name.c_str() : "", Unit.Casting.Ticks / Tps);
	}
	else
	{
		Bar(IX, Row, InfoW, GaugeH, Unit.bReady ? 1.0f : static_cast<float>(Unit.Tg) / TMSim::Pace::TgMax,
			Unit.bReady ? Gold : FLinearColor(0.55f, 0.75f, 0.95f), FLinearColor(0.03f, 0.04f, 0.06f, 0.95f), 5.0f * S);
		GaugeText = Unit.bReady ? FString(TEXT("TG  READY")) : FString::Printf(TEXT("TG  %d%%"), Unit.Tg * 100 / TMSim::Pace::TgMax);
	}
	Text(GaugeText, IX + 12.0f * S, Row + (GaugeH - TextSize(GaugeText, Font, 0.36f * S).Y) * 0.5f, FLinearColor::White, Font, 0.36f * S);
	AddTip(IX, Row, InfoW, GaugeH, ExplainTurn(From, Unit));
	Row += GaugeH + 4.0f * S;

	// The ultimate meter, gold, brighter when full.
	const bool bUltFull = Unit.Ult >= TMSim::Pace::UltMax;
	Bar(IX, Row, InfoW, 14.0f * S, static_cast<float>(Unit.Ult) / TMSim::Pace::UltMax,
		bUltFull ? FLinearColor(1.0f, 0.9f, 0.45f) : Gold * FLinearColor(0.75f, 0.75f, 0.75f, 1.0f), FLinearColor(0.03f, 0.04f, 0.06f, 0.95f), 4.0f * S);
	const FString Ult = bUltFull ? FString(TEXT("ULT  READY")) : FString::Printf(TEXT("ULT  %d%%"), Unit.Ult);
	Text(Ult, IX + 12.0f * S, Row + (14.0f * S - TextSize(Ult, Font, 0.34f * S).Y) * 0.5f, FLinearColor::White, Font, 0.34f * S);
	AddTip(IX, Row, InfoW, 12.0f * S, FString::Printf(TEXT("Ultimate meter %d / %d\n+%d each turn, +%d per ability used, +%s per 1%% of max HP lost"),
		Unit.Ult, TMSim::Pace::UltMax, TMSim::RoundToInt(Battle.Tuning.UltPerTurn),
		TMSim::RoundToInt(Battle.Tuning.UltPerAction), *Num(TMSim::Combat::UltFromDamage)));
	Row += 14.0f * S + 5.0f * S;

	// What is on it.
	StatusChips(Unit, bRight ? IX + InfoW : IX, Row, 26.0f * S, bRight, true);

	// An enemy's abilities, under its panel: what it could do next, and when.
	if (bRight)
	{
		const float Tile = 44.0f * S;
		float TX = X + W - 4.0f * (Tile + 8.0f * S) + 8.0f * S;
		for (int32 Slot = 0; Slot < 4; ++Slot)
		{
			AbilityTile(From, Unit, Slot, TX, Y + Portrait + 8.0f * S, Tile, false);
			TX += Tile + 8.0f * S;
		}
		MenuButton(X, Y + Portrait + 12.0f * S, 30.0f * S, 30.0f * S, TEXT("x"), ETMHudAction::CloseCard);
	}
}

void ATMBattleHud::DrawUnitCard(ATMBattleDirector& From)
{
	// Bottom left an ally, bottom right an enemy (DrawInspectCard). The ally is
	// the unit being ordered; while watching, whoever's turn it is; otherwise one
	// clicked on or under the pointer.
	const TMSim::FUnit* Unit = nullptr;
	auto Consider = [&](const TMSim::FUnit* Candidate)
	{
		if (!Unit && Candidate && From.IsFriend(*Candidate) && (Candidate->IsAlive() || Candidate->IsKo()) && From.IsSeen(*Candidate))
		{
			Unit = Candidate;
		}
	};
	Consider(From.SelectedUnit());
	if (From.ComputerPlays(0) && From.ComputerPlays(1))
	{
		Consider(From.WaitingOn());
	}
	Consider(From.Battle.FindUnit(From.InspectedId));
	Consider(From.Battle.FindUnit(From.HoverUnitId));
	if (Unit)
	{
		DrawUnitPanel(From, *Unit, false);
	}
}

void ATMBattleHud::DrawInspectCard(ATMBattleDirector& From)
{
	const TMSim::FUnit* Unit = nullptr;
	auto Consider = [&](const TMSim::FUnit* Candidate)
	{
		if (!Unit && Candidate && !From.IsFriend(*Candidate) && (Candidate->IsAlive() || Candidate->IsKo()) && From.IsSeen(*Candidate))
		{
			Unit = Candidate;
		}
	};
	if (From.ComputerPlays(0) && From.ComputerPlays(1))
	{
		Consider(From.WaitingOn());
	}
	Consider(From.Battle.FindUnit(From.InspectedId));
	Consider(From.Battle.FindUnit(From.HoverUnitId));
	if (Unit)
	{
		DrawUnitPanel(From, *Unit, true);
	}
}

// ------------------------------------------------------- corner and field

void ATMBattleHud::DrawCornerButtons(ATMBattleDirector& From)
{
	const FPanelScale Sized(*this, TEXT("corner"));
	// Log / Field / Units / Pause / Menu, top right (hud.gd:608-623), and Layout.
	const float BW = 60.0f * S;
	const float BH = 30.0f * S;
	const float Gap = 4.0f * S;
	float X = Canvas->ClipX - 10.0f * S - 6.0f * BW - 5.0f * Gap + Nudge(TEXT("corner")).X;
	const float Y = 12.0f * S + Nudge(TEXT("corner")).Y;
	Movable(TEXT("corner"), TEXT("Buttons"), X, Y, 6.0f * BW + 5.0f * Gap, BH);
	MenuButton(X, Y, BW, BH, TEXT("Layout"), ETMHudAction::ToggleLayout, -1, From.bEditingLayout);
	AddTip(X, Y, BW, BH, FString::Printf(TEXT("Move the screen's panels where you want them, then lock them again (%s)"),
		*FTMSettings::Get().KeyName(ETMAction::EditLayout)));
	X += BW + Gap;
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
	const FPanelScale Sized(*this, TEXT("field"));
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
	const float X = 16.0f * S + Nudge(TEXT("field")).X;
	// Under the log when it is open, so the two never overlap.
	const float Top = From.bShowLog ? LogBottom + 16.0f * S : 12.0f * S + 2.0f * RowHeight * S + 18.0f * S;
	const float Y = FMath::Max(Top, (Canvas->ClipY - H) * 0.5f) + Nudge(TEXT("field")).Y;
	Panel(X - 6.0f * S, Y - 8.0f * S, W, H, FLinearColor(0.03f, 0.05f, 0.08f, 0.9f), FLinearColor(1, 1, 1, 0.12f), 1.0f);
	Movable(TEXT("field"), TEXT("Field list"), X - 6.0f * S, Y - 8.0f * S, W, H);
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

// ------------------------------------------------------------ class picker

void ATMBattleHud::DrawClassPicker(ATMBattleDirector& From)
{
	// Every class the game has loaded, as a grid, for one setup slot
	// (class_picker.gd). A role filter along the top, because there are dozens.
	const std::vector<const TMSim::FJobDef*>& Jobs = TMSim::AllJobs();
	UFont* Font = GEngine->GetMediumFont();
	UFont* Big = GEngine->GetLargeFont();
	const int32 Team = From.PickerSlot / 4;
	const int32 Slot = From.PickerSlot % 4;
	const char* Roles[4] = { "tank", "damage", "support", "special" };

	DrawRect(FLinearColor(0.02f, 0.03f, 0.06f, 0.7f), 0.0f, 0.0f, Canvas->ClipX, Canvas->ClipY);
	const float PW = FMath::Min(Canvas->ClipX - 40.0f * S, 1560.0f * S);
	const float PH = Canvas->ClipY - 60.0f * S;
	const float PX = (Canvas->ClipX - PW) * 0.5f;
	const float PY = 30.0f * S;
	Panel(PX, PY, PW, PH, FLinearColor(0.05f, 0.06f, 0.1f, 0.97f), TeamColour(Team) * FLinearColor(1, 1, 1, 0.7f), 1.5f);
	Text(FString::Printf(TEXT("%s, slot %d: choose a class"), Team == 0 ? TEXT("Blue") : TEXT("Red"), Slot + 1),
		PX + 24.0f * S, PY + 16.0f * S, TeamColour(Team), Big, 0.8f * S);
	MenuButton(PX + PW - 150.0f * S, PY + 16.0f * S, 126.0f * S, 36.0f * S, TEXT("Close  (Esc)"), ETMHudAction::PickerClose);

	// The role filter.
	float FX = PX + 24.0f * S;
	const float FY = PY + 64.0f * S;
	MenuButton(FX, FY, 110.0f * S, 32.0f * S, TEXT("All"), ETMHudAction::PickerRole, -1, From.PickerRole < 0);
	FX += 118.0f * S;
	for (int32 r = 0; r < 4; ++r)
	{
		MenuButton(FX, FY, 110.0f * S, 32.0f * S, FString(UTF8_TO_TCHAR(Roles[r])), ETMHudAction::PickerRole, r, From.PickerRole == r);
		FX += 118.0f * S;
	}

	// The grid.
	const int32 Columns = 6;
	const float Gap = 6.0f * S;
	const float CellW = (PW - 48.0f * S - (Columns - 1) * Gap) / Columns;
	const float CellH = 40.0f * S;
	float Y = FY + 48.0f * S;
	int32 Column = 0;
	for (int32 j = 0; j < static_cast<int32>(Jobs.size()); ++j)
	{
		const TMSim::FJobDef& Job = *Jobs[j];
		if (From.PickerRole >= 0 && !TMSim::JobHasRole(Job.Id, Roles[From.PickerRole]))
		{
			continue;
		}
		FString RoleText;
		for (const std::string& JobRole : Job.Roles)
		{
			RoleText += (RoleText.IsEmpty() ? TEXT("") : TEXT(", ")) + FString(UTF8_TO_TCHAR(JobRole.c_str()));
		}
		const float X = PX + 24.0f * S + Column * (CellW + Gap);
		const bool bCurrent = Job.Id == From.Setup.Rosters[Team][Slot];
		MenuButton(X, Y, CellW, CellH, UTF8_TO_TCHAR(Job.Name.c_str()), ETMHudAction::PickerChoose, j, bCurrent, RoleText);
		// The numbers that tell classes apart at a glance.
		AddTip(X, Y, CellW, CellH, FString::Printf(TEXT("%hs  (%s)\nHP %d  AttDef %d  MagDef %d  Speed %d  Move %d\n%hs, %hs, %hs, %hs"),
			Job.Name.c_str(), *RoleText, Job.Stats.Get(TMSim::EStat::Hp), Job.Stats.Get(TMSim::EStat::AttDef),
			Job.Stats.Get(TMSim::EStat::MagDef), Job.Stats.Get(TMSim::EStat::Speed), Job.Stats.Get(TMSim::EStat::Move),
			TMSim::JobAbility(Job.Id, 0) ? TMSim::JobAbility(Job.Id, 0)->Name.c_str() : "",
			TMSim::JobAbility(Job.Id, 1) ? TMSim::JobAbility(Job.Id, 1)->Name.c_str() : "",
			TMSim::JobAbility(Job.Id, 2) ? TMSim::JobAbility(Job.Id, 2)->Name.c_str() : "",
			TMSim::JobAbility(Job.Id, 3) ? TMSim::JobAbility(Job.Id, 3)->Name.c_str() : ""));
		if (++Column == Columns)
		{
			Column = 0;
			Y += CellH + Gap;
		}
	}
	Text(TEXT("Rest the pointer on a class for its numbers. Classes are made in the class creator (E:\\TacticsClassCreator)."),
		PX + 24.0f * S, PY + PH - 32.0f * S, Dim, Font, 0.46f * S);
}