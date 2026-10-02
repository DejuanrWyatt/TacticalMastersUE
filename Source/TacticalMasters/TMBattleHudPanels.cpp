// The battle HUD's panels: the turn order, the log, the unit cards, the list of
// every unit, the corner buttons, the Unit Guide and the tooltips that explain
// the numbers. The frame, the board markings, the action bar and the menus are
// in TMBattleHud.cpp.

#include "TMBattleHud.h"

#include "Engine/Canvas.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "CanvasItem.h"
#include "ImageUtils.h"
#include "ImageCore.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "GameFramework/PlayerController.h"

#include "TMBattleDirector.h"
#include "TMBattleHudStyle.h"
#include "SimAbility.h"
#include "SimItem.h"

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

	/**
	 * The damage type the class creator gave each of a class's four abilities,
	 * read once from the class files' creator notes ("creator.plan[slot].damageType",
	 * else the class's first "creator.damageTypes"). The rules have no damage
	 * types: this is only a word on the ability's tile.
	 */
	FString CreatorDamageType(const std::string& Job, int32 Slot)
	{
		static TMap<FString, TArray<FString>> Types;
		static bool bRead = false;
		if (!bRead)
		{
			bRead = true;
			const FString Dir = FPaths::ProjectContentDir() / TEXT("Data/Classes");
			TArray<FString> Files;
			IFileManager::Get().FindFiles(Files, *(Dir / TEXT("*.tmclass.json")), true, false);
			for (const FString& File : Files)
			{
				FString Json;
				TSharedPtr<FJsonObject> Root;
				if (!FFileHelper::LoadFileToString(Json, *(Dir / File))
					|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid())
				{
					continue;
				}
				const TSharedPtr<FJsonObject>* Creator = nullptr;
				if (!Root->TryGetObjectField(TEXT("creator"), Creator))
				{
					continue;
				}
				FString Whole;
				const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
				if ((*Creator)->TryGetArrayField(TEXT("damageTypes"), List) && List->Num() > 0)
				{
					Whole = (*List)[0]->AsString();
				}
				TArray<FString> PerSlot = { Whole, Whole, Whole, Whole };
				const TArray<TSharedPtr<FJsonValue>>* Plan = nullptr;
				if ((*Creator)->TryGetArrayField(TEXT("plan"), Plan))
				{
					for (int32 i = 0; i < 4 && i < Plan->Num(); ++i)
					{
						const TSharedPtr<FJsonObject>* Step = nullptr;
						FString Type;
						if ((*Plan)[i]->TryGetObject(Step) && (*Step)->TryGetStringField(TEXT("damageType"), Type) && !Type.IsEmpty())
						{
							PerSlot[i] = Type;
						}
					}
				}
				Types.Add(Root->GetStringField(TEXT("id")), PerSlot);
			}
		}
		const TArray<FString>* Found = Types.Find(UTF8_TO_TCHAR(Job.c_str()));
		return Found && Slot >= 0 && Slot < Found->Num() ? (*Found)[Slot] : FString();
	}

	/**
	 * An ability's damage type and its colour: the creator's word when it gave
	 * one, else guessed from the animation it borrows, else physical or magic
	 * by which defence resists it.
	 */
	FString DamageType(const std::string& Job, int32 Slot, const TMSim::FAbility& Ability, FLinearColor& Colour)
	{
		FString Type = CreatorDamageType(Job, Slot).ToLower();
		if (Type.IsEmpty())
		{
			const std::string& Fx = Ability.Fx;
			Type = Fx == "fire" || Fx == "meteor" ? TEXT("fire")
				: Fx == "blizzard" ? TEXT("ice")
				: Fx == "holy_blade" || Fx == "sanctuary" ? TEXT("holy")
				: Fx == "earth_slash" ? TEXT("earth")
				: Ability.Scale == TMSim::EScale::Att ? TEXT("physical") : TEXT("magic");
		}
		static const TMap<FString, FLinearColor> Colours =
		{
			{ TEXT("fire"), FLinearColor(1.0f, 0.55f, 0.2f) },
			{ TEXT("ice"), FLinearColor(0.6f, 0.9f, 1.0f) },
			{ TEXT("water"), FLinearColor(0.35f, 0.6f, 1.0f) },
			{ TEXT("lightning"), FLinearColor(1.0f, 0.95f, 0.35f) },
			{ TEXT("earth"), FLinearColor(0.8f, 0.6f, 0.35f) },
			{ TEXT("wind"), FLinearColor(0.65f, 1.0f, 0.8f) },
			{ TEXT("nature"), FLinearColor(0.5f, 0.95f, 0.4f) },
			{ TEXT("holy"), FLinearColor(1.0f, 0.9f, 0.55f) },
			{ TEXT("shadow"), FLinearColor(0.75f, 0.5f, 1.0f) },
			{ TEXT("magic"), FLinearColor(0.75f, 0.6f, 1.0f) },
			{ TEXT("physical"), FLinearColor(0.9f, 0.85f, 0.8f) },
		};
		const FLinearColor* Found = Colours.Find(Type);
		Colour = Found ? *Found : FLinearColor(0.9f, 0.9f, 0.9f);
		return Type.Left(1).ToUpper() + Type.Mid(1);
	}

	/** What an ability does, in two or three words for its tile: "36 Fire", "+30 Heal". */
	FString TileEffect(const TMSim::FUnit& Unit, int32 Slot, const TMSim::FAbility& Ability, FLinearColor& Colour)
	{
		auto Word = [](const std::string& Id)
		{
			FString Out = UTF8_TO_TCHAR(Id.c_str());
			Out.ReplaceInline(TEXT("_"), TEXT(" "));
			return Out.Left(1).ToUpper() + Out.Mid(1);
		};
		switch (Ability.Effect)
		{
		case TMSim::EEffect::Damage:
			return FString::Printf(TEXT("%d %s"), TMSim::RoundToInt(Ability.Power), *DamageType(Unit.Job, Slot, Ability, Colour));
		case TMSim::EEffect::Heal:
			Colour = FLinearColor(0.45f, 0.95f, 0.5f);
			return FString::Printf(TEXT("+%d Heal"), TMSim::RoundToInt(Ability.Power));
		case TMSim::EEffect::Revive:
			Colour = FLinearColor(0.45f, 0.95f, 0.5f);
			return FString::Printf(TEXT("Revive %d%%"), TMSim::RoundToInt(Ability.Power * 100.0f));
		default:
			break;
		}
		Colour = FLinearColor(0.45f, 0.8f, 1.0f);
		if (Ability.HasStatus())
		{
			return Word(Ability.StatusId);
		}
		if (Ability.TgChange != 0)
		{
			return FString::Printf(TEXT("TG %+d%%"), Ability.TgChange);
		}
		return Ability.Buffs.empty() ? FString(TEXT("Utility")) : FString(TEXT("Buff"));
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
			const TMSim::FAbility* Ability = Caster.Ability(Caster.Casting.Slot);
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
	const TMSim::FAbility* Ability = Unit.Ability(Slot);
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
		if (Battle.NewDefense())
		{
			// 2026-10-01 (Docs/design/feat-defense.md).
			Lines.Add(FString::Printf(TEXT("  x damage multiplier %s, less the target's %s share: %s / (%s + it) (at least 1)"),
				*Num(Tune.DamageMultiplier), Ability->Scale == TMSim::EScale::Att ? TEXT("Armor") : TEXT("Resist"),
				*Num(Tune.DefenseScale), *Num(Tune.DefenseScale)));
			Lines.Add(FString::Printf(TEXT("  target's Evasion: 1 in 10 evasions dodge it, the rest graze for half; %d%% chance of a critical hit (x %s)"),
				Battle.CritChance(Unit), *Num(Tune.CritMultiplier)));
		}
		else
		{
			Lines.Add(FString::Printf(TEXT("  - target's %s, x damage multiplier %s (at least 1)"),
				Ability->Scale == TMSim::EScale::Att ? TEXT("AttDef") : TEXT("MagDef"), *Num(Tune.DamageMultiplier)));
			Lines.Add(FString::Printf(TEXT("  target's %s evades it; %d%% chance of a critical hit (x %s)"),
				Ability->Scale == TMSim::EScale::Att ? TEXT("A-Eva") : TEXT("M-Eva"), Battle.CritChance(Unit), *Num(Tune.CritMultiplier)));
		}
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
					const TMSim::FAbility* Ability = Unit.Ability(Unit.Casting.Slot);
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
	// its own and its squares reorderable (Edit layout). Waiting, a card is
	// greyed and its gauge fills it from the bottom; when its turn comes it
	// flashes yellow, then stays gold with a pulsing border while the fill
	// drains from the top as its countdown runs out. The badge is the seconds
	// to either, or to a cast.
	const TMSim::FBattle& Battle = From.Battle;
	UFont* Font = GEngine->GetMediumFont();
	const float Now = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0f;
	const float Pulse = 0.55f + 0.45f * FMath::Sin(Now * 1000.0f / 180.0f);
	for (int32 Team = 0; Team < 2; ++Team)
	{
		const FPanelScale Sized(*this, Team == 0 ? TEXT("turn_cards_blue") : TEXT("turn_cards_red"));
		const float SW = 60.0f * S;
		const float SH = 60.0f * S + 30.0f * S;
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
			// A card in the unit panel's style (DrawUnitPanel): a live portrait
			// framed in its side's colour -- gold on its turn, white while being
			// ordered -- its class badge, its health, its gauge, and the time.
			const bool bFriend = From.IsFriend(*Unit);
			FLinearColor Frame = SideColour(bFriend) * FLinearColor(1, 1, 1, 0.9f * Alpha);
			float FrameWidth = 2.0f * S;
			if (Unit->bReady)
			{
				Frame = FLinearColor(Gold.R, Gold.G, Gold.B, Pulse * Alpha);
				FrameWidth = 3.0f * S;
			}
			if (Unit->Id == From.SelectedId)
			{
				Frame = FLinearColor::White;
				FrameWidth = 3.5f * S;
			}
			DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.7f * Alpha), X - 1.0f, RowY - 1.0f, SW + 2.0f, SH + 2.0f);
			DrawRect(Frame, X, RowY, SW, SW);
			DrawRect(FLinearColor(0.02f, 0.03f, 0.05f, Alpha), X + FrameWidth, RowY + FrameWidth, SW - 2.0f * FrameWidth, SW - 2.0f * FrameWidth);
			if (bFogged)
			{
				const FVector2D Mark = TextSize(TEXT("?"), Font, 0.9f * S);
				Text(TEXT("?"), X + (SW - Mark.X) * 0.5f, RowY + (SW - Mark.Y) * 0.5f, Dim, Font, 0.9f * S);
			}
			else
			{
				if (UTexture* Face0 = From.CardPortrait(*Unit))
				{
					// Greyed while it waits; full colour on its turn.
					Picture(Face0, X + FrameWidth, RowY + FrameWidth, SW - 2.0f * FrameWidth, SW - 2.0f * FrameWidth,
						Unit->bReady ? FLinearColor::White : FLinearColor(0.42f, 0.44f, 0.47f, 1.0f));
				}
				if (!Unit->bReady)
				{
					DrawRect(FLinearColor(0.3f, 0.31f, 0.33f, 0.3f), X + FrameWidth, RowY + FrameWidth, SW - 2.0f * FrameWidth, SW - 2.0f * FrameWidth);
				}
				// The gauge, up the card: filling from the bottom while it waits,
				// draining from the top through its turn.
				const float Inner = SW - 2.0f * FrameWidth;
				const float Tall = Inner * FMath::Clamp(Level, 0.0f, 1.0f);
				DrawRect(Meter, X + FrameWidth, RowY + FrameWidth + Inner - Tall, Inner, Tall);
				if (Tall > 1.0f && Tall < Inner)
				{
					// Its edge, so the level reads at a glance.
					DrawRect((Unit->bReady ? Gold : FLinearColor(1.0f, 0.45f, 0.35f)) * FLinearColor(1, 1, 1, 0.9f),
						X + FrameWidth, RowY + FrameWidth + Inner - Tall - 1.0f * S, Inner, 2.0f * S);
				}
				const float Crest = 18.0f * S;
				DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.6f), X + FrameWidth, RowY + FrameWidth, Crest, Crest);
				Picture(ClassIcon(*Unit), X + FrameWidth, RowY + FrameWidth, Crest, Crest);
			}
			// A plan waiting for its turn (2026-10-01): a gold corner with a P; on a
			// Go To, ">" and its turns left instead.
			const ATMBattleDirector::FTMGoTo* Marching = From.GoToOf(Unit->Id);
			if ((Marching || From.PlanOf(Unit->Id)) && !bFogged)
			{
				const FString Sign = Marching ? FString::Printf(TEXT(">%d"), static_cast<int32>(Marching->Stops.size())) : FString(TEXT("P"));
				const FVector2D PSize = TextSize(Sign, Font, 0.42f * S);
				const float Mark = FMath::Max(18.0f * S, PSize.X + 6.0f * S);
				const float MX = X + SW - Mark - 1.0f * S;
				const float MY = RowY + 1.0f * S;
				DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.7f), MX - 1.0f, MY - 1.0f, Mark + 2.0f, 18.0f * S + 2.0f);
				DrawRect(Gold, MX, MY, Mark, 18.0f * S);
				Text(Sign, MX + (Mark - PSize.X) * 0.5f, MY + (18.0f * S - PSize.Y) * 0.5f, FLinearColor(0.1f, 0.08f, 0.02f), Font, 0.42f * S, false);
			}
			// The moment its turn comes, the whole card flashes yellow and fades
			// to its gold; it keeps a soft yellow beat for as long as it can act.
			if (Unit->bReady)
			{
				const float* Since = ReadySince.Find(Unit->Id);
				if (!Since)
				{
					Since = &ReadySince.Add(Unit->Id, Now);
				}
				const float Flash = FMath::Clamp(1.0f - (Now - *Since) / 0.7f, 0.0f, 1.0f);
				const float Glow = FMath::Max(0.75f * Flash, 0.12f * Pulse) * Alpha;
				DrawRect(FLinearColor(1.0f, 0.9f, 0.25f, Glow), X, RowY, SW, SW);
			}
			else
			{
				ReadySince.Remove(Unit->Id);
			}
			// Health in its side's colour, under the card.
			const float BarY = RowY + SW + 2.0f * S;
			const float HpPart = bFogged ? 0.0f : static_cast<float>(Unit->Hp) / FMath::Max(1, Unit->MaxHp());
			DrawRect(FLinearColor(0.03f, 0.04f, 0.06f, 0.95f * Alpha), X, BarY, SW, 5.0f * S);
			DrawRect(SideColour(bFriend) * FLinearColor(1, 1, 1, Alpha), X, BarY, SW * HpPart, 5.0f * S);
			const float GaugeY = BarY + 1.0f * S;
			const float BadgeScale = 0.4f * S;
			const FVector2D BadgeSize = TextSize(Badge, Font, BadgeScale);
			Text(Badge, X + (SW - BadgeSize.X) * 0.5f, GaugeY + 5.0f * S, BadgeColour * FLinearColor(1, 1, 1, Alpha), Font, BadgeScale);
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
					const TMSim::FAbility* Ability = Unit->Ability(Unit->Casting.Slot);
					Tip += FString::Printf(TEXT("\nCasting %hs"), Ability ? Ability->Name.c_str() : "");
				}
				Tip += TEXT("\n\n") + ExplainTurn(From, *Unit) + TEXT("\n") + ExplainCountdown(From, *Unit);
				// Queued orders (2026-10-01).
				if (From.PlanOf(Unit->Id))
				{
					Tip += TEXT("\n\nPlanned: it carries the plan out the moment its turn begins. Click to change it.");
				}
				else if (!Unit->bReady && From.PlayerCanPlan(Unit))
				{
					Tip += TEXT("\n\nClick to plan its next turn.");
				}
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
	// The combat log under the turn order (2026-10-01, design A with C's tabs):
	// each action on a line of its own -- the time, who in their side's colour,
	// what -- and under it what it did to each unit, the numbers lined up on the
	// right, red for harm, green for healing, gold for a crit, with tags for
	// statuses. Tabs keep only what matters now. L or the Log button shows or
	// hides it, + makes it taller, the wheel scrolls back.
	if (!From.bShowLog)
	{
		return;
	}
	using FEntry = ATMBattleDirector::FTMLogEntry;
	using EKind = FEntry::EKind;
	UFont* Font = GEngine->GetMediumFont();
	const float Scale = 0.5f * S;
	const float SmallScale = 0.4f * S;
	const float LineH = TextSize(TEXT("Ag"), Font, Scale).Y + 3.0f * S;
	const int32 Shown = From.bLogLarge ? 26 : 10;
	const float X = 16.0f * S + Nudge(TEXT("log")).X;
	const float Y = 12.0f * S + 2.0f * RowHeight * S + 18.0f * S + Nudge(TEXT("log")).Y;
	const float W = 520.0f * S;
	const float Head = 30.0f * S;
	const FLinearColor Harm(1.0f, 0.54f, 0.48f);
	const FLinearColor Heal(0.5f, 0.88f, 0.6f);
	const FLinearColor TimeColour(0.44f, 0.47f, 0.56f);
	const FLinearColor Bright(0.95f, 0.96f, 0.98f);
	const FLinearColor MonsterColour(1.0f, 0.75f, 0.38f);

	// Which side is "mine" for the Mine tab.
	const int32 Mine = From.ItemsTeam();
	auto UnitColour = [&](int32 Id)
	{
		const TMSim::FUnit* Unit = From.Battle.FindUnit(Id);
		if (!Unit)
		{
			return TextColour;
		}
		return Unit->Team == 0 || Unit->Team == 1 ? TeamColour(Unit->Team) : MonsterColour;
	};
	auto IsMine = [&](int32 Id)
	{
		const TMSim::FUnit* Unit = From.Battle.FindUnit(Id);
		return Unit && Unit->Team == Mine;
	};

	// The tab's lines: whole actions are kept or dropped together.
	const TArray<FEntry>& All = From.LogEntries;
	TSet<int32> KeepGroups;
	if (From.LogTab >= 2)
	{
		for (const FEntry& Entry : All)
		{
			if ((From.LogTab == 2 && Entry.Kind != EKind::Turn && Entry.Kind != EKind::System && IsMine(Entry.Unit))
				|| (From.LogTab == 3 && Entry.bKey))
			{
				KeepGroups.Add(Entry.Group);
			}
		}
	}
	TArray<int32> Lines;
	for (int32 i = 0; i < All.Num(); ++i)
	{
		const FEntry& Entry = All[i];
		const bool bKeep = From.LogTab == 0
			|| (From.LogTab == 1 && Entry.Kind != EKind::Turn)
			|| (From.LogTab >= 2 && KeepGroups.Contains(Entry.Group) && Entry.Kind != EKind::Turn);
		if (bKeep)
		{
			Lines.Add(i);
		}
	}
	From.LogScroll = FMath::Clamp(From.LogScroll, 0, FMath::Max(0, Lines.Num() - 1));
	const int32 Last = FMath::Max(0, Lines.Num() - From.LogScroll);
	const int32 First = FMath::Max(0, Last - Shown);
	const int32 Count = Last - First;
	const float H = Head + FMath::Max(1, Count) * LineH + 10.0f * S;
	Panel(X - 6.0f * S, Y - 4.0f * S, W, H, FLinearColor(0.06f, 0.08f, 0.12f, 0.82f), FLinearColor(1, 1, 1, 0.12f), 1.0f);
	LogArea = FBox2D(FVector2D(X - 6.0f * S, Y - 4.0f * S), FVector2D(X - 6.0f * S + W, Y - 4.0f * S + H));
	Movable(TEXT("log"), TEXT("Combat log"), X - 6.0f * S, Y - 4.0f * S, W, H);
	LogBottom = Y - 4.0f * S + H;

	// The tabs, and grow / close.
	static const TCHAR* Tabs[] = { TEXT("All"), TEXT("Combat"), TEXT("Mine"), TEXT("Key") };
	float TX = X;
	for (int32 t = 0; t < 4; ++t)
	{
		const float TW = TextSize(Tabs[t], Font, 0.44f * S).X + 16.0f * S;
		const bool bOn = From.LogTab == t;
		Panel(TX, Y - 1.0f * S, TW, 22.0f * S, bOn ? FLinearColor(1.0f, 0.82f, 0.35f, 0.16f) : FLinearColor(0, 0, 0, 0),
			bOn ? Gold : FLinearColor(1, 1, 1, 0.14f), 1.0f);
		Text(Tabs[t], TX + 8.0f * S, Y + 1.0f * S, bOn ? Gold : Dim, Font, 0.44f * S);
		AddButton(TX, Y - 1.0f * S, TW, 22.0f * S, ETMHudAction::LogTab, t);
		TX += TW + 4.0f * S;
	}
	if (From.LogScroll > 0)
	{
		Text(FString::Printf(TEXT("%d back"), From.LogScroll), TX + 6.0f * S, Y + 2.0f * S, Dim, Font, SmallScale);
	}
	MenuButton(X + W - 70.0f * S, Y - 1.0f * S, 28.0f * S, 22.0f * S, From.bLogLarge ? TEXT("-") : TEXT("+"), ETMHudAction::GrowLog);
	MenuButton(X + W - 38.0f * S, Y - 1.0f * S, 28.0f * S, 22.0f * S, TEXT("x"), ETMHudAction::ToggleLog);

	const float TimeW = 38.0f * S;
	const float Right = X + W - 18.0f * S;
	for (int32 n = 0; n < Count; ++n)
	{
		const FEntry& Entry = All[Lines[First + n]];
		const float LY = Y + Head + n * LineH;
		// Older lines fade a little; the newest is brightest.
		const float Fade = 1.0f - 0.35f * static_cast<float>(Count - 1 - n) / FMath::Max(1, Count - 1);
		auto A = [Fade](const FLinearColor& C) { return FLinearColor(C.R, C.G, C.B, C.A * Fade); };
		const bool bResult = Entry.Kind == EKind::Result;
		if (!bResult)
		{
			const int32 Seconds = FMath::FloorToInt(Entry.Seconds);
			Text(FString::Printf(TEXT("%d:%02d"), Seconds / 60, Seconds % 60), X, LY + 1.0f * S, A(TimeColour), Font, SmallScale);
		}
		float LX = X + TimeW;
		auto Word = [&](const FString& What, const FLinearColor& Colour, float WScale = 0.0f)
		{
			const float Sc = WScale > 0.0f ? WScale : Scale;
			Text(What, LX, LY, A(Colour), Font, Sc);
			LX += TextSize(What + TEXT(" "), Font, Sc).X;
		};
		switch (Entry.Kind)
		{
		case EKind::Action:
			Word(From.LogName(Entry.Unit), UnitColour(Entry.Unit));
			Word(Entry.Verb, Dim);
			Word(Entry.What, Bright);
			break;
		case EKind::Result:
		{
			LX += 14.0f * S;
			Word(From.LogName(Entry.Unit), UnitColour(Entry.Unit));
			for (const FString& Tag : Entry.Tags)
			{
				const bool bCrit = Tag == TEXT("CRIT");
				const bool bAlly = Tag == TEXT("ALLY");
				const FVector2D TagSize = TextSize(Tag, Font, 0.36f * S);
				const FLinearColor Fill = bCrit ? Gold : bAlly ? FLinearColor(0.85f, 0.45f, 0.25f) : FLinearColor(0.42f, 0.36f, 0.72f);
				DrawRect(A(Fill), LX, LY + 2.0f * S, TagSize.X + 8.0f * S, LineH - 6.0f * S);
				Text(Tag, LX + 4.0f * S, LY + 3.0f * S, A(bCrit ? FLinearColor(0.1f, 0.12f, 0.18f) : Bright), Font, 0.36f * S);
				LX += TagSize.X + 12.0f * S;
			}
			if (!Entry.Amount.IsEmpty())
			{
				const FLinearColor Tone = Entry.Tone == 1 ? Harm : Entry.Tone == 2 ? Heal : Entry.Tone == 3 ? Gold : Dim;
				const FVector2D Size = TextSize(Entry.Amount, Font, Scale);
				Text(Entry.Amount, Right - Size.X, LY, A(Tone), Font, Scale);
			}
			break;
		}
		case EKind::Ko:
			DrawRect(A(FLinearColor(1.0f, 0.38f, 0.32f, 0.16f)), X - 4.0f * S, LY - 1.0f * S, W - 6.0f * S, LineH);
			Word(From.LogName(Entry.Unit), UnitColour(Entry.Unit));
			Word(TEXT("is knocked out"), Harm);
			break;
		case EKind::Turn:
			Word(From.LogName(Entry.Unit), A(UnitColour(Entry.Unit)) * FLinearColor(1, 1, 1, 0.7f), SmallScale);
			Word(Entry.Text, Dim, SmallScale);
			break;
		case EKind::System:
			Word(Entry.Text, Entry.bKey ? Gold : Dim);
			break;
		default:
			if (Entry.Unit >= 0)
			{
				Word(From.LogName(Entry.Unit), UnitColour(Entry.Unit));
			}
			Word(Entry.Text, Entry.bKey ? Bright : TextColour * FLinearColor(1, 1, 1, 0.85f));
			break;
		}
	}
	if (Count == 0)
	{
		Text(From.LogTab == 3 ? TEXT("No key moments yet.") : TEXT("Nothing yet."), X + TimeW, Y + Head, Dim, Font, Scale);
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
		const TMSim::FAbility* Ability = Caster->Ability(Caster->Casting.Slot);
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
	// Pictures drawn by the class creator (its Icons tab), read at run time:
	// nothing here is an asset made in the editor.
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

void ATMBattleHud::ShapeBadge(const std::string& Shape, float X, float Y, float Size, const FLinearColor& Colour)
{
	if (!Canvas)
	{
		return;
	}
	DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.75f), X, Y, Size, Size);
	const FVector2D C(X + Size * 0.5f, Y + Size * 0.5f);
	const float R = Size * 0.36f;
	const float Thick = FMath::Max(1.0f, Size / 14.0f);
	auto Tri = [&](const FVector2D& A, const FVector2D& B, const FVector2D& E, const FLinearColor& Tint)
	{
		FCanvasTriangleItem Item(A, B, E, GWhiteTexture);
		Item.SetColor(Tint);
		Item.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Item);
	};
	auto At = [&](float Degrees, float Radius, const FVector2D& Centre, float Squash = 1.0f)
	{
		const float Rad = FMath::DegreesToRadians(Degrees);
		return Centre + FVector2D(FMath::Cos(Rad) * Radius, FMath::Sin(Rad) * Radius * Squash);
	};
	auto Ring = [&](const FVector2D& Centre, float Radius, float Squash = 1.0f)
	{
		for (int32 i = 0; i < 20; ++i)
		{
			const FVector2D A = At(i * 18.0f, Radius, Centre, Squash);
			const FVector2D B = At((i + 1) * 18.0f, Radius, Centre, Squash);
			DrawLine(A.X, A.Y, B.X, B.Y, Colour, Thick);
		}
	};
	auto Disc = [&](const FVector2D& Centre, float Radius, const FLinearColor& Tint)
	{
		for (int32 i = 0; i < 20; ++i)
		{
			Tri(Centre, At(i * 18.0f, Radius, Centre), At((i + 1) * 18.0f, Radius, Centre), Tint);
		}
	};
	auto Arrow = [&](const FVector2D& From, const FVector2D& To)
	{
		// A shaft and a filled head.
		const FVector2D Along = (To - From).GetSafeNormal();
		const FVector2D Across(-Along.Y, Along.X);
		const float Head = Size * 0.2f;
		const FVector2D Base = To - Along * Head;
		DrawLine(From.X, From.Y, Base.X, Base.Y, Colour, Thick * 2.0f);
		Tri(To, Base + Across * Head * 0.7f, Base - Across * Head * 0.7f, Colour);
	};
	const FLinearColor Soft = Colour * FLinearColor(1, 1, 1, 0.4f);

	if (Shape == "circle")
	{
		// An area: a filled disc with its rim.
		Disc(C, R, Soft);
		Ring(C, R);
	}
	else if (Shape == "cone")
	{
		// A wedge fanning out from the user at the bottom.
		const FVector2D Apex(C.X, Y + Size * 0.86f);
		const float Reach = Size * 0.7f;
		for (int32 i = 0; i < 6; ++i)
		{
			Tri(Apex, At(-120.0f + i * 10.0f, Reach, Apex), At(-120.0f + (i + 1) * 10.0f, Reach, Apex), Soft);
		}
		const FVector2D Left = At(-120.0f, Reach, Apex);
		const FVector2D Right = At(-60.0f, Reach, Apex);
		DrawLine(Apex.X, Apex.Y, Left.X, Left.Y, Colour, Thick);
		DrawLine(Apex.X, Apex.Y, Right.X, Right.Y, Colour, Thick);
		for (int32 i = 0; i < 6; ++i)
		{
			const FVector2D A = At(-120.0f + i * 10.0f, Reach, Apex);
			const FVector2D B = At(-120.0f + (i + 1) * 10.0f, Reach, Apex);
			DrawLine(A.X, A.Y, B.X, B.Y, Colour, Thick);
		}
	}
	else if (Shape == "line")
	{
		// A straight band out from the user.
		DrawRect(Soft, C.X - Size * 0.12f, Y + Size * 0.22f, Size * 0.24f, Size * 0.66f);
		Arrow(FVector2D(C.X, Y + Size * 0.88f), FVector2D(C.X, Y + Size * 0.1f));
	}
	else if (Shape == "vector")
	{
		// Picked at a spot, then swept in a direction.
		Disc(FVector2D(X + Size * 0.24f, Y + Size * 0.72f), Size * 0.1f, Colour);
		Arrow(FVector2D(X + Size * 0.24f, Y + Size * 0.72f), FVector2D(X + Size * 0.86f, Y + Size * 0.18f));
	}
	else if (Shape == "self")
	{
		// Around the user: a figure standing in a ring.
		Ring(FVector2D(C.X, Y + Size * 0.76f), R, 0.35f);
		Disc(FVector2D(C.X, Y + Size * 0.24f), Size * 0.1f, Colour);
		Tri(FVector2D(C.X, Y + Size * 0.36f), FVector2D(C.X - Size * 0.15f, Y + Size * 0.76f), FVector2D(C.X + Size * 0.15f, Y + Size * 0.76f), Colour);
	}
	else if (Shape == "global")
	{
		// Everywhere: a globe.
		Ring(C, R);
		DrawLine(C.X - R, C.Y, C.X + R, C.Y, Colour, Thick);
		for (int32 i = 0; i < 20; ++i)
		{
			const float A0 = FMath::DegreesToRadians(i * 18.0f);
			const float A1 = FMath::DegreesToRadians((i + 1) * 18.0f);
			DrawLine(C.X + FMath::Cos(A0) * R * 0.45f, C.Y + FMath::Sin(A0) * R, C.X + FMath::Cos(A1) * R * 0.45f, C.Y + FMath::Sin(A1) * R, Colour, Thick);
		}
	}
	else if (Shape == "point")
	{
		// A spot on the ground: a cross on a flat ring.
		Ring(FVector2D(C.X, C.Y + Size * 0.12f), R, 0.45f);
		const float K = Size * 0.16f;
		DrawLine(C.X - K, C.Y + Size * 0.12f - K * 0.6f, C.X + K, C.Y + Size * 0.12f + K * 0.6f, Colour, Thick * 1.5f);
		DrawLine(C.X - K, C.Y + Size * 0.12f + K * 0.6f, C.X + K, C.Y + Size * 0.12f - K * 0.6f, Colour, Thick * 1.5f);
	}
	else
	{
		// One unit: a crosshair.
		Ring(C, R * 0.8f);
		const float In = R * 0.35f;
		const float Out = R * 1.15f;
		DrawLine(C.X, C.Y - Out, C.X, C.Y - In, Colour, Thick);
		DrawLine(C.X, C.Y + In, C.X, C.Y + Out, Colour, Thick);
		DrawLine(C.X - Out, C.Y, C.X - In, C.Y, Colour, Thick);
		DrawLine(C.X + In, C.Y, C.X + Out, C.Y, Colour, Thick);
		Disc(C, Size * 0.06f, Colour);
	}
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
		const float Scale = Size / 60.0f;
		// Its icon (Content/Data/Icons/statuses, drawn by Tools/StatusIcons); a
		// coloured chip with its three letters for a status that has none.
		if (UTexture2D* Picture2D = Icon(TEXT("statuses/") + FString(UTF8_TO_TCHAR(Status.Id.c_str())), false))
		{
			Picture(Picture2D, At, Y, Size, Size);
		}
		else
		{
			DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.7f), At - 1.0f, Y - 1.0f, Size + 2.0f, Size + 2.0f);
			DrawRect(Look.Colour * FLinearColor(0.8f, 0.8f, 0.8f, 1.0f), At, Y, Size, Size);
			DrawRect(Look.Colour, At, Y, Size, Size * 0.45f);
			const FString Tag = Def ? FString(UTF8_TO_TCHAR(Def->Tag)) : FString(UTF8_TO_TCHAR(Status.Id.c_str())).Left(3).ToUpper();
			const FVector2D TagSize = TextSize(Tag, Font, Scale);
			Text(Tag, At + (Size - TagSize.X) * 0.5f, Y + (Size - TagSize.Y) * 0.5f - Size * 0.08f, FLinearColor(0.05f, 0.05f, 0.08f), Font, Scale, false);
		}
		// Turns left in the corner on a dark tab, so it reads over any icon;
		// Chilled shows its layers, Stop its seconds.
		const bool bLayers = Status.Id == "chilled";
		const FString Turns = bLayers ? FString::Printf(TEXT("x%d"), FMath::Max(1, Status.Amount)) : FString::FromInt(Status.Turns);
		const FVector2D TurnSize = TextSize(Turns, Font, Scale * 0.8f);
		DrawRect(FLinearColor(0.02f, 0.02f, 0.04f, 0.8f), At + Size - TurnSize.X - 3.0f, Y + Size - TurnSize.Y, TurnSize.X + 3.0f, TurnSize.Y);
		Text(Turns, At + Size - TurnSize.X - 1.0f, Y + Size - TurnSize.Y, FLinearColor::White, Font, Scale * 0.8f);
		if (bTips)
		{
			FString Tip = FString::Printf(TEXT("%s  (%d %s%s)"), Def ? UTF8_TO_TCHAR(Def->Name) : UTF8_TO_TCHAR(Status.Id.c_str()),
				Status.Turns, Status.Id == "stop" ? TEXT("second") : TEXT("turn"), Status.Turns == 1 ? TEXT("") : TEXT("s"));
			if (Status.Id == "chilled")
			{
				Tip += FString::Printf(TEXT("\n%d of 3 layers"), FMath::Max(1, Status.Amount));
			}
			else if (Status.Amount > 0 && Def && Def->bAbsorbs)
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

void ATMBattleHud::PopBar(ATMBattleDirector& From, const TMSim::FUnit& Unit, float CentreX, float Bottom)
{
	const ATMBattleDirector::FTMHpPop* Found = From.HpPops.Find(Unit.Id);
	if (!Found)
	{
		return;
	}
	const ATMBattleDirector::FTMHpPop& Pop = *Found;
	// "Combat Text Mockups" B and C: up for three seconds, in and out softly; the
	// part lost stays in the bar and drains away over two, flashing as it goes.
	const double Now = From.PopClock();
	const float Since = static_cast<float>(Now - Pop.ShownAt);
	const float Life = static_cast<float>(ATMBattleDirector::HpPopSeconds);
	if (Since < 0.0f || Since >= Life)
	{
		return;
	}
	const float Fade = 0.35f;
	const float A = Since > Life - Fade ? (Life - Since) / Fade : FMath::Min(1.0f, Since / 0.12f);
	const float O = FTMSettings::Get().OverheadScale;
	const float W = 96.0f * S * O;
	const float H = 7.0f * S * O;
	const float X = CentreX - W * 0.5f;
	const float Y = Bottom - H;
	const int32 MaxHp = FMath::Max(1, Unit.MaxHp());
	auto Part = [MaxHp](float Hp) { return FMath::Clamp(Hp / MaxHp, 0.0f, 1.0f); };
	const bool bBlink = FMath::Fmod(Now, 0.22) < 0.11;

	DrawRect(FLinearColor(0.85f, 0.88f, 0.95f, 0.35f * A), X - 1.0f, Y - 1.0f, W + 2.0f, H + 2.0f);
	DrawRect(FLinearColor(0.03f, 0.03f, 0.05f, 0.92f * A), X, Y, W, H);
	const float Front = Part(static_cast<float>(Pop.Hp));
	DrawRect(SideColour(true).CopyWithNewOpacity(A), X, Y, W * Front, H);
	const float Trail = Part(From.PopTrail(Pop, Now));
	if (Trail > Front)
	{
		const FLinearColor Lost = bBlink ? FLinearColor(1.0f, 0.89f, 0.87f, A) : FLinearColor(1.0f, 0.23f, 0.18f, A);
		DrawRect(Lost, X + W * Front, Y, W * (Trail - Front), H);
	}
	if (Pop.HealAt >= 0.0 && Now - Pop.HealAt < ATMBattleDirector::HpHealGlowSeconds && Pop.HealFrom < Pop.Hp)
	{
		const float From0 = Part(static_cast<float>(Pop.HealFrom));
		const FLinearColor Gained = bBlink ? FLinearColor(0.85f, 1.0f, 0.89f, A) : FLinearColor(0.49f, 0.95f, 0.6f, A);
		DrawRect(Gained, X + W * From0, Y, W * (Front - From0), H);
	}
	// The same notches as the full read: 25 a notch, more for the big ones.
	const int32 Notch = MaxHp <= 400 ? 25 : MaxHp <= 800 ? 50 : MaxHp <= 2000 ? 100 : 250;
	for (int32 Mark = Notch, Count = 1; Mark < MaxHp; Mark += Notch, ++Count)
	{
		const bool bHeavy = Count % 4 == 0;
		DrawRect(FLinearColor(0.03f, 0.03f, 0.05f, 0.9f * A), X + W * Mark / MaxHp - (bHeavy ? 1.0f : 0.5f) * S,
			Y + (bHeavy ? 0.0f : H * 0.25f), (bHeavy ? 2.0f : 1.0f) * S, bHeavy ? H : H * 0.75f);
	}
	UFont* Font = GEngine->GetMediumFont();
	const FString Hp = FString::Printf(TEXT("%d/%d"), Pop.Hp, MaxHp);
	const FVector2D HpSize = TextSize(Hp, Font, 0.3f * S * O);
	OutlinedText(Hp, X + W + 4.0f * S, Y + (H - HpSize.Y) * 0.5f, FLinearColor(1.0f, 1.0f, 1.0f, A), Font, 0.3f * S * O, 1.0f * S);
}

void ATMBattleHud::DrawGoToMarks(ATMBattleDirector& From)
{
	// "Multi-Turn Move Mockups", Main: a number where each turn's walk ends, as
	// Civilization III marks a Go To; the selected unit's bright, others faint,
	// and the route under the pointer before it is set.
	if (!PlayerOwner || From.Screen != ATMBattleDirector::EScreen::Battle)
	{
		return;
	}
	UFont* Font = GEngine->GetMediumFont();
	// "Go To Marker Options" B (the human's pick, 2026-10-02): a ring lying on the
	// board where the turn ends, the number standing on it, the last ring gold
	// and glowing -- part of the ground rather than a box stuck on the screen.
	auto Mark = [&](const TMSim::FVec2& Point, const FString& Label, float Strength, bool bEnd)
	{
		const int Level = From.Battle.Map.NodeLevel(TMSim::FMap::NodeOf(Point));
		const FVector Local = From.WorldFromMetres(Point, Level) + FVector(0.0f, 0.0f, 6.0f);
		const FTransform& Board = From.GetActorTransform();
		FVector2D At;
		if (!PlayerOwner->ProjectWorldLocationToScreen(Board.TransformPosition(Local), At))
		{
			return;
		}
		const FLinearColor Colour = bEnd ? FLinearColor(0.91f, 0.75f, 0.35f) : FLinearColor(0.47f, 0.67f, 1.0f);
		const float Radius = (bEnd ? 0.34f : 0.28f) * From.TileSize;
		constexpr int32 Sides = 28;
		FVector2D Ring[Sides];
		for (int32 k = 0; k < Sides; ++k)
		{
			const float Angle = 2.0f * PI * k / Sides;
			const FVector Round = Local + FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.0f);
			if (!PlayerOwner->ProjectWorldLocationToScreen(Board.TransformPosition(Round), Ring[k]))
			{
				return;
			}
		}
		for (int32 k = 0; k < Sides; ++k)
		{
			const FVector2D& A = Ring[k];
			const FVector2D& B = Ring[(k + 1) % Sides];
			if (bEnd)
			{
				// The last stop's glow: a wide faint stroke under the ring.
				DrawLine(A.X, A.Y, B.X, B.Y, Colour * FLinearColor(1.0f, 1.0f, 1.0f, 0.22f * Strength), 7.0f * S);
			}
			DrawLine(A.X, A.Y, B.X, B.Y, Colour * FLinearColor(1.0f, 1.0f, 1.0f, (bEnd ? 0.85f : 0.6f) * Strength), 2.0f * S);
		}
		const float Scale = (bEnd ? 0.42f : 0.38f) * S;
		const FVector2D Size = TextSize(Label, Font, Scale);
		const FLinearColor Ink = bEnd ? FLinearColor(1.0f, 0.88f, 0.63f, Strength) : FLinearColor(0.95f, 0.96f, 0.99f, 0.92f * Strength);
		OutlinedText(Label, At.X - Size.X * 0.5f, At.Y - Size.Y - 4.0f * S, Ink, Font, Scale, 1.5f * S);
	};
	for (const TPair<int32, ATMBattleDirector::FTMGoTo>& Pair : From.GoTos)
	{
		const TMSim::FUnit* Unit = From.Battle.FindUnit(Pair.Key);
		if (!Unit || !Unit->IsAlive())
		{
			continue;
		}
		const float Strength = Pair.Key == From.SelectedId ? 1.0f : 0.5f;
		const int32 Count = static_cast<int32>(Pair.Value.Stops.size());
		for (int32 i = 0; i < Count; ++i)
		{
			Mark(Pair.Value.Stops[static_cast<size_t>(i)], FString::FromInt(i + 1), Strength, i + 1 == Count);
		}
	}
	const int32 Hovered = static_cast<int32>(From.GoToHoverStops.size());
	for (int32 i = 0; i < Hovered; ++i)
	{
		Mark(From.GoToHoverStops[static_cast<size_t>(i)], i + 1 == Hovered ? FString::Printf(TEXT("%d turn%s"), Hovered, Hovered == 1 ? TEXT("") : TEXT("s"))
			: FString::FromInt(i + 1), 0.85f, i + 1 == Hovered);
	}
}

void ATMBattleHud::TurnPips(const TMSim::FUnit& Unit, float CX, float Top, float Zoom)
{
	const float R = 7.5f * S * Zoom;
	const float Y0 = Top + R + 3.0f * S;
	const FLinearColor Ink(0.03f, 0.04f, 0.07f, 0.92f);
	const FLinearColor Spent(0.52f, 0.55f, 0.62f, 0.75f);
	auto Circle = [&](float PX, float PY, float Radius, const FLinearColor& Colour, float Thick)
	{
		constexpr int32 Sides = 18;
		for (int32 k = 0; k < Sides; ++k)
		{
			const float A0 = 2.0f * PI * k / Sides;
			const float A1 = 2.0f * PI * (k + 1) / Sides;
			DrawLine(PX + FMath::Cos(A0) * Radius, PY + FMath::Sin(A0) * Radius, PX + FMath::Cos(A1) * Radius, PY + FMath::Sin(A1) * Radius, Colour, Thick);
		}
	};
	auto Token = [&](float PX, bool bLeft, const FLinearColor& Colour, bool bMove)
	{
		// A dark disc (rings drawn inward), its rim, its glyph.
		for (float Radius = R; Radius > 0.5f; Radius -= 1.5f)
		{
			Circle(PX, Y0, Radius, Ink, 2.0f);
		}
		const FLinearColor Shade = bLeft ? Colour : Spent;
		Circle(PX, Y0, R, Shade, bLeft ? 2.2f * S : 1.2f * S);
		const float G = R * 0.55f;
		if (bMove)
		{
			Circle(PX - G * 0.38f, Y0 - G * 0.2f, G * 0.3f, Shade, 1.4f * S);
			Circle(PX + G * 0.38f, Y0 + G * 0.25f, G * 0.3f, Shade, 1.4f * S);
		}
		else
		{
			DrawLine(PX, Y0 - G, PX, Y0 + G, Shade, 1.6f * S);
			DrawLine(PX - G, Y0, PX + G, Y0, Shade, 1.6f * S);
			DrawLine(PX - G * 0.45f, Y0 - G * 0.45f, PX + G * 0.45f, Y0 + G * 0.45f, Shade, 1.2f * S);
			DrawLine(PX - G * 0.45f, Y0 + G * 0.45f, PX + G * 0.45f, Y0 - G * 0.45f, Shade, 1.2f * S);
		}
		if (!bLeft)
		{
			// Spent: struck through.
			DrawLine(PX - R * 0.75f, Y0 + R * 0.75f, PX + R * 0.75f, Y0 - R * 0.75f, Spent, 1.6f * S);
		}
	};
	const float Apart = R + 2.0f * S;
	// The bar that joins them, so the two read as one mark.
	DrawLine(CX - Apart, Y0, CX + Apart, Y0, Ink, 3.0f * S);
	Token(CX - Apart, !Unit.bMoved, FLinearColor(0.56f, 0.72f, 1.0f, 1.0f), true);
	Token(CX + Apart, !Unit.bActed, FLinearColor(0.94f, 0.81f, 0.45f, 1.0f), false);
}

void ATMBattleHud::DrawOverheads(ATMBattleDirector& From)
{
	// Each unit's health is the ring on the ground under it (TMBattleDirectorLooks.cpp,
	// the human's pick "C", 2026-10-01), so nothing hangs over the battlefield.
	// Here: its statuses small beside that ring, a cast on its way out, and for
	// the unit under the pointer alone the full read over its head -- name,
	// health in segments ("B"), turn gauge, ultimate, statuses, items ("E").
	if (!PlayerOwner)
	{
		return;
	}
	// Go To turn numbers on the ground first, under the units' own marks.
	DrawGoToMarks(From);
	FVector Eye;
	FRotator Look;
	PlayerOwner->GetPlayerViewPoint(Eye, Look);
	const FVector Right = FRotationMatrix(Look).GetScaledAxis(EAxis::Y);
	struct FHead
	{
		const TMSim::FUnit* Unit;
		FVector2D At;
		FVector2D Foot;
		float Ring;
		float Distance;
	};
	TArray<FHead> Heads;
	for (const TMSim::FUnit& Unit : From.Battle.Units)
	{
		if ((!Unit.IsAlive() && !Unit.IsKo()) || !From.IsSeen(Unit))
		{
			continue;
		}
		const FVector Shown = From.ShownAt(Unit);
		const FVector World = From.GetActorTransform().TransformPosition(Shown + FVector(0.0f, 0.0f, Unit.IsAlive() ? 262.0f : 70.0f));
		const FVector FootWorld = From.GetActorTransform().TransformPosition(Shown + FVector(0.0f, 0.0f, 5.0f));
		FVector2D At;
		FVector2D Foot;
		FVector2D Edge;
		if (PlayerOwner->ProjectWorldLocationToScreen(World, At) && PlayerOwner->ProjectWorldLocationToScreen(FootWorld, Foot)
			&& PlayerOwner->ProjectWorldLocationToScreen(FootWorld + Right * 55.0f, Edge))
		{
			Heads.Add({ &Unit, At, Foot, static_cast<float>(FVector2D::Distance(Foot, Edge)), static_cast<float>(FVector::Dist(Eye, World)) });
		}
	}
	Heads.Sort([](const FHead& A, const FHead& B) { return A.Distance > B.Distance; });

	UFont* Font = GEngine->GetMediumFont();
	const float O = FTMSettings::Get().OverheadScale;
	for (const FHead& Head : Heads)
	{
		const TMSim::FUnit& Unit = *Head.Unit;
		const bool bFriend = From.IsFriend(Unit);
		const bool bHovered = Unit.Id == From.HoverUnitId;

		if (!Unit.IsAlive())
		{
			// Knocked out: no ring, just how long it has to be revived.
			const FString Down = FString::Printf(TEXT("DOWN %.0fs"), Unit.KoTicks / Tps);
			const FVector2D DownSize = TextSize(Down, Font, 0.34f * S * O);
			OutlinedText(Down, Head.At.X - DownSize.X * 0.5f, Head.At.Y, Urgent, Font, 0.34f * S * O, 1.0f * S);
			continue;
		}

		// What is left of its turn ("Turn Left Indicator Mockups" A, 2026-10-02): two
		// round tokens joined, in front of its ring -- a footprint for the move, a star
		// for the action, ringed while still to use, grey and struck through once spent.
		// Round and paired, under the ring, so never taken for its statuses (rounded
		// squares beside the ring).
		if (bFriend && Unit.bReady && !(Unit.bMoved && Unit.bActed) && From.PlayerCanCommand(&Unit))
		{
			TurnPips(Unit, Head.Foot.X, Head.Foot.Y + Head.Ring * 0.42f, O);
		}

		if (!bHovered)
		{
			// Its statuses, small, beside its ring.
			const float Small = 11.0f * S * FTMSettings::Get().StatusIconScale;
			StatusChips(Unit, Head.Foot.X + Head.Ring + 4.0f * S, Head.Foot.Y - Small * 0.5f, Small, false, false);
			// A spell on its way out stays in sight: a short thin bar over its head.
			float PopY = Head.At.Y;
			if (Unit.IsCasting())
			{
				const float CastW = 48.0f * S * O;
				const float Done = 1.0f - static_cast<float>(Unit.Casting.Ticks) / FMath::Max(1, Unit.Casting.Total);
				Bar(Head.At.X - CastW * 0.5f, Head.At.Y, CastW, 3.0f * S * O, Done, CastColour, FLinearColor(0.05f, 0.05f, 0.08f, 0.8f), 0.0f);
				PopY -= 10.0f * S * O;
			}
			// One of ours that just took or dealt a blow: its health, for a few seconds.
			PopBar(From, Unit, Head.At.X, PopY);
			continue;
		}

		// The unit under the pointer: the full read, still slim.
		const float W = 104.0f * S * O;
		const float H = 7.0f * S * O;
		const float X = Head.At.X - W * 0.5f;
		float Y = Head.At.Y;
		// A monster by its name and what it is doing: its temperament until it is set off.
		const FString Name = Unit.bMonster ? From.MonsterLine(Unit) : FString::Printf(TEXT("%s %d"), *JobName(Unit), Unit.Id);
		const FVector2D NameSize = TextSize(Name, Font, 0.38f * S * O);
		OutlinedText(Name, Head.At.X - NameSize.X * 0.5f, Y - NameSize.Y - 3.0f * S,
			Unit.Id == From.SelectedId ? Gold : FLinearColor(0.97f, 0.97f, 1.0f), Font, 0.38f * S * O, 1.2f * S);

		// Health, in segments, with a shield's worth after it in white.
		const int32 MaxHp = FMath::Max(1, Unit.MaxHp());
		int32 Soak = 0;
		for (const TMSim::FStatus& Status : Unit.Statuses)
		{
			const TMSim::FStatusDef* Def = TMSim::FindStatus(Status.Id);
			Soak += Def && Def->bAbsorbs ? Status.Amount : 0;
		}
		const float Part = FMath::Clamp(static_cast<float>(Unit.Hp) / MaxHp, 0.0f, 1.0f);
		if (Unit.bReady)
		{
			// Its turn: a thin gold frame round its health.
			DrawRect(Gold, X - 2.0f * S, Y - 2.0f * S, W + 4.0f * S, H + 4.0f * S);
		}
		Bar(X, Y, W, H, Part, Unit.bMonster ? FLinearColor(0.95f, 0.68f, 0.18f) : SideColour(bFriend), FLinearColor(0.05f, 0.05f, 0.08f, 0.92f), 0.0f);
		if (Soak > 0)
		{
			DrawRect(FLinearColor(1.0f, 1.0f, 1.0f, 0.8f), X + W * Part, Y, FMath::Min(W * (1.0f - Part), W * Soak / MaxHp), H);
		}
		// A notch every so many points, heavier every fourth, so how much is left
		// reads without the number: 25 a notch, more for the big ones.
		const int32 Notch = MaxHp <= 400 ? 25 : MaxHp <= 800 ? 50 : MaxHp <= 2000 ? 100 : 250;
		for (int32 Mark = Notch, Count = 1; Mark < MaxHp; Mark += Notch, ++Count)
		{
			const bool bHeavy = Count % 4 == 0;
			const float MarkX = X + W * Mark / MaxHp;
			DrawRect(FLinearColor(0.03f, 0.03f, 0.05f, 0.9f), MarkX - (bHeavy ? 1.0f : 0.5f) * S, Y + (bHeavy ? 0.0f : H * 0.25f), (bHeavy ? 2.0f : 1.0f) * S,
				bHeavy ? H : H * 0.75f);
		}
		const FString Hp = Soak > 0 ? FString::Printf(TEXT("%d/%d +%d"), Unit.Hp, MaxHp, Soak) : FString::Printf(TEXT("%d/%d"), Unit.Hp, MaxHp);
		const FVector2D HpSize = TextSize(Hp, Font, 0.32f * S * O);
		OutlinedText(Hp, X + W + 5.0f * S, Y + (H - HpSize.Y) * 0.5f, FLinearColor::White, Font, 0.32f * S * O, 1.0f * S);
		// What it carries, after the number: each item's icon.
		if (Unit.HasItems())
		{
			const float Gear = 16.0f * S * O;
			GearRow(Unit, X + W + 9.0f * S + HpSize.X, Y + (H - Gear) * 0.5f, Gear);
		}
		Y += H + 3.0f * S;

		// Its turn gauge, filling toward its turn, then gold and draining while it can act.
		const float Thin = 2.0f * S * O;
		float Gauge = static_cast<float>(Unit.Tg) / TMSim::Pace::TgMax;
		FLinearColor GaugeColour(0.55f, 0.75f, 0.95f);
		if (Unit.bReady)
		{
			Gauge = static_cast<float>(Unit.Clock) / FMath::Max(1, From.Battle.ClockTicks(Unit));
			GaugeColour = Gold;
		}
		Bar(X, Y, W, Thin, FMath::Clamp(Gauge, 0.0f, 1.0f), GaugeColour, FLinearColor(0.05f, 0.05f, 0.08f, 0.85f), 0.0f);
		Y += Thin + 2.0f * S;
		// The ultimate meter, pulsing once it is full.
		const bool bUltReady = Unit.Ult >= TMSim::Pace::UltMax;
		const float Beat = bUltReady ? 0.7f + 0.3f * FMath::Sin((GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0f) * 6.0f) : 1.0f;
		const FLinearColor UltColour = bUltReady ? FLinearColor(1.0f, 0.62f, 0.2f, Beat) : FLinearColor(0.78f, 0.5f, 1.0f, 0.9f);
		Bar(X, Y, W, Thin, static_cast<float>(Unit.Ult) / TMSim::Pace::UltMax, UltColour, FLinearColor(0.05f, 0.05f, 0.08f, 0.85f), 0.0f);
		Y += Thin + 3.0f * S;

		// A spell on its way out: how long it has left.
		if (Unit.IsCasting())
		{
			const float Done = 1.0f - static_cast<float>(Unit.Casting.Ticks) / FMath::Max(1, Unit.Casting.Total);
			Bar(X, Y, W, 3.0f * S, Done, CastColour, FLinearColor(0.05f, 0.05f, 0.08f, 0.8f), 0.0f);
			Y += 6.0f * S;
		}

		// Its statuses, under the bars.
		const float Chip = 15.0f * S * FTMSettings::Get().StatusIconScale;
		StatusChips(Unit, X, Y, Chip, false, false);
	}
}

// ------------------------------------------------------------ unit panels

void ATMBattleHud::AbilityTile(ATMBattleDirector& From, const TMSim::FUnit& Unit, int32 Slot, float X, float Y, float Size, bool bButton)
{
	const TMSim::FAbility* Ability = Unit.Ability(Slot);
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
	const bool bUsable = bButton ? From.PlayerCanCommand(&Unit) && !Unit.bActed && !bBlocked : !bBlocked || Unit.bActed;
	const bool bColour = !bCooling && !bUltWaiting && !bPassive && bUsable;
	const bool bAiming = bButton && From.AimMode == ATMBattleDirector::EAimMode::Ability && From.AimSlot == Slot;
	const bool bOver = FBox2D(FVector2D(X, Y), FVector2D(X + Size, Y + Size)).IsInside(MousePoint());

	// The frame: gold for the ultimate, bright while aiming it.
	const FLinearColor Edge = bAiming ? Gold : (Slot == 3 ? Gold * FLinearColor(1, 1, 1, bColour ? 0.95f : 0.45f)
		: FLinearColor(0.55f, 0.62f, 0.75f, bColour ? 0.9f : 0.4f));
	// A unit that has moved but not acted: its abilities are what is left, and their
	// edges flash ("Turn Left Indicator Mockups" D, with flashing borders, 2026-10-02).
	const bool bFlash = bButton && bColour && Unit.bReady && Unit.bMoved && !Unit.bActed && From.PlayerCanCommand(&Unit);
	const float Pulse = bFlash ? 0.5f + 0.5f * FMath::Sin((GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0f) * 2.0f * PI * 1.1f) : 0.0f;
	const FLinearColor Flash(1.0f, 0.93f, 0.62f, 1.0f);
	if (bFlash)
	{
		DrawRect(Flash * FLinearColor(1.0f, 1.0f, 1.0f, 0.35f * Pulse), X - 6.0f * S, Y - 6.0f * S, Size + 12.0f * S, Size + 12.0f * S);
	}
	DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.75f), X - 3.0f * S, Y - 3.0f * S, Size + 6.0f * S, Size + 6.0f * S);
	DrawRect(bFlash ? FMath::Lerp(Edge, Flash, Pulse) : Edge, X - 2.0f * S, Y - 2.0f * S, Size + 4.0f * S, Size + 4.0f * S);
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
	else if (bCooling && bButton)
	{
		// "Action Bar Mockups" B (2026-10-02): the icon greyed (above), an
		// hourglass badge with the turns left, and a step bar that fills as they pass.
		const int32 Left = Unit.Cooldowns[Slot];
		const int32 Whole = FMath::Max(Left, Ability->Cooldown);
		DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.3f), X, Y, Size, Size);
		const FString Turns = FString::FromInt(Left);
		const float BadgeScale = Size / 200.0f;
		const FVector2D TurnSize = TextSize(Turns, Font, BadgeScale);
		const float Glass = 10.0f * S * Size / (100.0f * S);
		const float BadgeH = FMath::Max(TurnSize.Y, Glass) + 5.0f * S;
		const float BadgeW = Glass + TurnSize.X + 13.0f * S;
		const float BadgeX = X + Size - BadgeW - 4.0f * S;
		const float BadgeY = Y + Size * 0.21f;
		Panel(BadgeX, BadgeY, BadgeW, BadgeH, FLinearColor(0.05f, 0.06f, 0.1f, 0.92f), FLinearColor(0.56f, 0.64f, 0.77f, 1.0f), 1.0f);
		const float GX = BadgeX + 5.0f * S;
		const float GY = BadgeY + (BadgeH - Glass) * 0.5f;
		const FLinearColor Sand(0.81f, 0.85f, 0.92f, 1.0f);
		DrawLine(GX, GY, GX + Glass, GY, Sand, 1.5f * S);
		DrawLine(GX, GY + Glass, GX + Glass, GY + Glass, Sand, 1.5f * S);
		DrawLine(GX + Glass * 0.15f, GY, GX + Glass * 0.85f, GY + Glass, Sand, 1.5f * S);
		DrawLine(GX + Glass * 0.85f, GY, GX + Glass * 0.15f, GY + Glass, Sand, 1.5f * S);
		Text(Turns, GX + Glass + 4.0f * S, BadgeY + (BadgeH - TurnSize.Y) * 0.5f, FLinearColor::White, Font, BadgeScale, false);
		// One step for each turn of the cooldown, lit as each passes (a plain bar past six).
		const float BarX = X + 6.0f * S;
		const float BarW = Size - 12.0f * S;
		const float BarY = Y + Size * 0.8f - 9.0f * S;
		const float BarH = 4.0f * S;
		const FLinearColor Lit(0.56f, 0.72f, 1.0f, 1.0f);
		const FLinearColor Unlit(1.0f, 1.0f, 1.0f, 0.18f);
		const int32 Done = Whole - Left;
		if (Whole <= 6)
		{
			const float Step = 3.0f * S;
			const float Each = (BarW - Step * (Whole - 1)) / Whole;
			for (int32 Part = 0; Part < Whole; ++Part)
			{
				DrawRect(Part < Done ? Lit : Unlit, BarX + Part * (Each + Step), BarY, Each, BarH);
			}
		}
		else
		{
			DrawRect(Unlit, BarX, BarY, BarW, BarH);
			DrawRect(Lit, BarX, BarY, BarW * Done / Whole, BarH);
		}
	}
	else if (bCooling)
	{
		// Too small for the badge (the enemy panel): the turns until it can be used, large.
		DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.35f), X, Y, Size, Size);
		const FString Turns = FString::FromInt(Unit.Cooldowns[Slot]);
		const FVector2D TurnSize = TextSize(Turns, Big, Size / 110.0f);
		Text(Turns, X + (Size - TurnSize.X) * 0.5f, Y + (Size - TurnSize.Y) * 0.5f, FLinearColor::White, Big, Size / 110.0f);
	}
	// On the action bar, the tile says what it is: its name across the top and
	// what it does across the bottom ("36 Fire"), in the damage type's colour.
	// The enemy panel's tiles are too small for words; their tooltips say it.
	float Top = Y;
	if (bButton)
	{
		const float Band = Size * 0.2f;
		auto Fitted = [&](const FString& What, float Row, const FLinearColor& Colour)
		{
			float Scale = Size / 260.0f;
			FVector2D Wide = TextSize(What, Font, Scale);
			if (Wide.X > Size - 6.0f * S)
			{
				Scale *= (Size - 6.0f * S) / Wide.X;
				Wide = TextSize(What, Font, Scale);
			}
			Text(What, X + (Size - Wide.X) * 0.5f, Row + (Band - Wide.Y) * 0.5f, Colour, Font, Scale);
		};
		const float Fade = bColour || bPassive ? 1.0f : 0.6f;
		DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.7f), X, Y, Size, Band);
		Fitted(UTF8_TO_TCHAR(Ability->Name.c_str()), Y, TextColour * FLinearColor(1, 1, 1, Fade));
		DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.7f), X, Y + Size - Band, Size, Band);
		FLinearColor EffectColour = TextColour;
		FString Effect = bPassive ? FString(Ability->Kind == "aura" ? TEXT("AURA") : TEXT("PASSIVE"))
			: TileEffect(Unit, Slot, *Ability, EffectColour);
		if (bPassive)
		{
			EffectColour = Dim;
		}
		else if (Unit.bReady && Unit.bActed && !bCooling)
		{
			// Its action spent this turn.
			Effect = TEXT("acted");
			EffectColour = Dim;
		}
		Fitted(Effect, Y + Size - Band, EffectColour * FLinearColor(1, 1, 1, Fade));
		Top = Y + Band;
		if (!bPassive)
		{
			// How far it reaches, in the bottom left corner above the effect
			// (2026-10-01): "6m", "2-6m" with a near limit, "self", "all".
			const std::string Shape = TMSim::ShapeOf(*Ability);
			FString Reach;
			if (Shape == "global")
			{
				Reach = TEXT("all");
			}
			else if (Ability->MaxRange <= 0.0f)
			{
				Reach = Ability->Aoe > 0.0f ? FString::Printf(TEXT("self %gm"), FMath::RoundToFloat(Ability->Aoe * 10.0f) / 10.0f) : FString(TEXT("self"));
			}
			else if (Ability->MinRange > 0.0f)
			{
				Reach = FString::Printf(TEXT("%g-%gm"), FMath::RoundToFloat(Ability->MinRange * 10.0f) / 10.0f, FMath::RoundToFloat(Ability->MaxRange * 10.0f) / 10.0f);
			}
			else
			{
				Reach = FString::Printf(TEXT("%gm"), FMath::RoundToFloat(Ability->MaxRange * 10.0f) / 10.0f);
			}
			const float ReachScale = Size / 290.0f;
			const FVector2D ReachSize = TextSize(Reach, Font, ReachScale);
			const float ReachY = Y + Size - Band - ReachSize.Y - 1.0f * S;
			DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.72f), X, ReachY, ReachSize.X + 6.0f * S, ReachSize.Y + 1.0f * S);
			Text(Reach, X + 3.0f * S, ReachY, FLinearColor(0.55f, 0.95f, 0.92f, Fade), Font, ReachScale);
		}
		if (!bPassive)
		{
			// Its shape, so a cone reads as a cone before it is aimed.
			const float Badge = Size * 0.26f;
			ShapeBadge(TMSim::ShapeOf(*Ability), X + Size - Badge, Top, Badge, FLinearColor(1, 1, 1, bColour ? 0.95f : 0.55f));
		}
	}
	else if (bPassive)
	{
		const FString Word = Ability->Kind == "aura" ? TEXT("AURA") : TEXT("PASSIVE");
		DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.6f), X, Y, Size, Size * 0.22f);
		Text(Word, X + 3.0f * S, Y + 1.0f * S, Dim, Font, Size / 300.0f);
	}
	if (!bPassive && Ability->Cast > 0.0f && bColour)
	{
		// A cast time, in the corner.
		const FString Cast = FString::Printf(TEXT("%.1fs"), From.Battle.CastTicks(*Ability) / Tps);
		const FVector2D CastSize = TextSize(Cast, Font, Size / 300.0f);
		const float CastX = bButton ? X : X + Size - CastSize.X - 6.0f * S;
		DrawRect(CastColour * FLinearColor(0.4f, 0.4f, 0.4f, 0.9f), CastX, Top, CastSize.X + 6.0f * S, CastSize.Y);
		Text(Cast, CastX + 3.0f * S, Top, FLinearColor::White, Font, Size / 300.0f);
	}
	if (bButton)
	{
		// Its key, in a badge on the tile's top left corner.
		const FString Key = FTMSettings::Get().KeyName(static_cast<ETMAction>(static_cast<int32>(ETMAction::Ability1) + Slot));
		const FVector2D KeySize = TextSize(Key, Font, Size / 300.0f);
		DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.85f), X - 4.0f * S, Y - KeySize.Y - 2.0f * S, KeySize.X + 8.0f * S, KeySize.Y + 2.0f * S);
		Text(Key, X, Y - KeySize.Y - 1.0f * S, bColour ? TextColour : Dim, Font, Size / 300.0f);
		AddButton(X, Y, Size, Size, ETMHudAction::Ability, Slot);
	}
	// An aura, or a toggle switched on: the Comet Trail round its edge while it lasts.
	if (Ability->Kind == "aura" || (Ability->Kind == "toggle" && Slot >= 0 && Slot < TMSim::AbilitySlots && Unit.Toggled[Slot]))
	{
		const float Out = FMath::Max(3.0f, Size * 0.05f);
		AuraTrail(X - Out, Y - Out, Size + 2.0f * Out, Size + 2.0f * Out, ATMBattleDirector::LookColour(From.LookOf(*Ability)));
	}
	FString ClassName;
	AbilityClassColour(*Ability, &ClassName);
	FString ShapeName = UTF8_TO_TCHAR(TMSim::ShapeOf(*Ability).c_str());
	ShapeName = bPassive ? FString() : TEXT("  -  ") + ShapeName.Left(1).ToUpper() + ShapeName.Mid(1);
	AddTip(X, Y, Size, Size, FString::Printf(TEXT("%hs  -  %s%s\n\n"), Ability->Name.c_str(), *ClassName, *ShapeName) + ExplainAbility(From, Unit, Slot));
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
	// The defenses as the rules read them: a share off each hit and one
	// Evasion (2026-10-01), or, under the classic rules, the old four.
	const int32 ArmorNow = Unit.Stat(TMSim::EStat::AttDef);
	const int32 ResistNow = Unit.Stat(TMSim::EStat::MagDef);
	const FString Defenses = Battle.NewDefense()
		? FString::Printf(TEXT("Armor %d (-%d%% physical)  Resist %d (-%d%% magic)\nEvasion %d%% (1 in 10 dodged, the rest grazed for half)  CRIT %d%%"),
			ArmorNow, Battle.DefenseShare(ArmorNow), ResistNow, Battle.DefenseShare(ResistNow), Battle.EvasionOf(Unit), Unit.Stat(TMSim::EStat::Crit))
		: FString::Printf(TEXT("DEF %d  MDF %d  CRIT %d%%\nAEV %d%%  MEV %d%%"), ArmorNow, ResistNow, Unit.Stat(TMSim::EStat::Crit),
			Unit.Stat(TMSim::EStat::AEva), Unit.Stat(TMSim::EStat::MEva));
	AddTip(PX, Y, Portrait, Portrait, FString::Printf(TEXT("%s %d\n%s\nSPD %d  MOV %sm  PAT %d  SGT %sm\n\n"),
		*JobName(Unit), Unit.Id, *Defenses, Unit.Stat(TMSim::EStat::Speed), *Num(Battle.MoveOf(Unit)),
		Unit.Stat(TMSim::EStat::Patience), *Num(Battle.SightOf(Unit)))
		+ ExplainMove(From, Unit) + TEXT("\n") + ExplainSight(From, Unit) + BuffText(Unit));
	// What it carries, along the foot of the portrait.
	if (Unit.HasItems())
	{
		const float ItemSize = 30.0f * S;
		float ItemX = PX + 4.0f * S;
		for (const TMSim::FItemDef* Item : Unit.Gear)
		{
			if (Item)
			{
				ItemBadge(Item, ItemX, Y + Portrait - ItemSize - 4.0f * S, ItemSize);
				ItemX += ItemSize + 3.0f * S;
			}
		}
	}

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
		const TMSim::FAbility* Ability = Unit.Ability(Unit.Casting.Slot);
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
	float X = Canvas->ClipX - 10.0f * S - 7.0f * BW - 6.0f * Gap + Nudge(TEXT("corner")).X;
	const float Y = 12.0f * S + Nudge(TEXT("corner")).Y;
	Movable(TEXT("corner"), TEXT("Buttons"), X, Y, 7.0f * BW + 6.0f * Gap, BH);
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
	MenuButton(X, Y, BW, BH, TEXT("Codex"), ETMHudAction::ToggleGuide, -1, From.bGuideOpen);
	AddTip(X, Y, BW, BH, TEXT("The Codex: how to play, every class, item and status, the ground and the objectives (U)"));
	X += BW + Gap;
	MenuButton(X, Y, BW, BH, TEXT("Items"), ETMHudAction::TakeOpen, -1, From.bTeamItemsOpen);
	AddTip(X, Y, BW, BH, TEXT("Your team's items: what each unit wears, its open slots, and the stash of items picked up"));
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

// ------------------------------------------------------------------ items

FLinearColor ATMBattleHud::TierColour(int32 Tier)
{
	switch (Tier)
	{
	case 1: return FLinearColor(0.4f, 0.85f, 0.45f);   // uncommon
	case 2: return FLinearColor(0.45f, 0.65f, 1.0f);   // rare
	case 3: return FLinearColor(1.0f, 0.72f, 0.25f);   // epic
	default: return FLinearColor(0.82f, 0.83f, 0.86f); // common
	}
}

FString ATMBattleHud::ItemSummary(const TMSim::FItemDef& Item)
{
	TArray<FString> Parts;
	for (int32 k = 0; k < TMSim::StatCount; ++k)
	{
		const int32 Amount = Item.Stats[k];
		if (Amount == 0)
		{
			continue;
		}
		const TMSim::EStat Stat = static_cast<TMSim::EStat>(k);
		const bool bPercent = Stat == TMSim::EStat::AEva || Stat == TMSim::EStat::MEva || Stat == TMSim::EStat::Crit;
		Parts.Add(FString::Printf(TEXT("%+d%s %s"), Amount, bPercent ? TEXT("%") : TEXT(""),
			Stat == TMSim::EStat::Hp ? TEXT("max HP") : ShownStatName(Stat)));
	}
	const TCHAR* Reach = Item.Scale == TMSim::EItemScale::Att ? TEXT(" (weapons)") : Item.Scale == TMSim::EItemScale::Mag ? TEXT(" (spells)") : TEXT("");
	if (Item.DamageFlat > 0)
	{
		Parts.Add(FString::Printf(TEXT("+%d ability power%s"), Item.DamageFlat, Reach));
	}
	if (Item.DamagePercent > 0)
	{
		Parts.Add(FString::Printf(TEXT("+%d%% ability damage%s"), Item.DamagePercent, Reach));
	}
	if (Item.HealFlat > 0)
	{
		Parts.Add(FString::Printf(TEXT("+%d healing power"), Item.HealFlat));
	}
	if (Item.HealPercent > 0)
	{
		Parts.Add(FString::Printf(TEXT("+%d%% healing"), Item.HealPercent));
	}
	if (Item.TgPercent > 0)
	{
		Parts.Add(FString::Printf(TEXT("+%d%% Turn Gauge speed"), Item.TgPercent));
	}
	if (Item.Jump > 0)
	{
		Parts.Add(FString::Printf(TEXT("+%d Jump"), Item.Jump));
	}
	if (const TMSim::FAbility* Ability = Item.AbilityId.empty() ? nullptr : TMSim::FindAbility(Item.AbilityId))
	{
		Parts.Add(FString::Printf(TEXT("ability: %hs"), Ability->Name.c_str()));
	}
	// What only counts sometimes is in the item's own words (its desc) rather than a list of numbers.
	const bool bSituational = Item.VsMonstersPercent || Item.HighGroundPercent || Item.StillDefence || Item.AlonePercent
		|| Item.LowHealthPercent || Item.UnseenEvasion || Item.LifestealPercent || Item.Thorns || Item.KillTgPercent
		|| Item.bFirstStrike || Item.bPhoenix || Item.DamageTakenPercent || Item.HealTakenPercent || Item.bCalmsMonsters
		|| Item.bSteady || Item.bShrineKey;
	if (Parts.Num() == 0 || bSituational)
	{
		Parts.Add(UTF8_TO_TCHAR(Item.Desc.c_str()));
	}
	return FString::Join(Parts, TEXT(", "));
}

void ATMBattleHud::AuraTrail(float X, float Y, float W, float H, const FLinearColor& Colour)
{
	if (!Canvas)
	{
		return;
	}
	// A point on the edge, from 0 (top left) round clockwise to 1.
	const float Round = 2.0f * (W + H);
	auto At = [&](float S)
	{
		float D = FMath::Fmod(FMath::Fmod(S, 1.0f) + 1.0f, 1.0f) * Round;
		if (D < W) { return FVector2D(X + D, Y); }
		D -= W;
		if (D < H) { return FVector2D(X + W, Y + D); }
		D -= H;
		if (D < W) { return FVector2D(X + W - D, Y + H); }
		D -= W;
		return FVector2D(X, Y + H - D);
	};
	const FLinearColor Hot = FMath::Lerp(Colour, FLinearColor::White, 0.7f);
	auto Line = [&](const FVector2D& A, const FVector2D& B, const FLinearColor& C, float Thick)
	{
		FCanvasLineItem Item(A, B);
		Item.SetColor(C);
		Item.LineThickness = Thick;
		Item.BlendMode = SE_BLEND_Additive;
		Canvas->DrawItem(Item);
	};
	// The edge, faintly lit all round.
	for (int32 k = 0; k < 4; ++k)
	{
		const FVector2D A = At(k * 0.25f + 0.0001f);
		const FVector2D B = At((k + 1) * 0.25f - 0.0001f);
		Line(A, B, Colour * FLinearColor(1, 1, 1, 0.25f), 4.0f * S);
		Line(A, B, Colour * FLinearColor(1, 1, 1, 0.45f), 1.5f * S);
	}
	// Two heads half the edge apart, round once every two seconds, each with a fading tail.
	const float Now = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0f;
	const float Phase = FMath::Fmod(Now / 2.0f, 1.0f);
	constexpr int32 Tail = 22;
	constexpr float Step = 0.006f;
	for (const float Offset : { 0.0f, 0.5f })
	{
		const float Head = Phase + Offset;
		for (int32 j = 0; j < Tail; ++j)
		{
			const float Fade = FMath::Pow(1.0f - static_cast<float>(j) / Tail, 1.6f);
			const FVector2D A = At(Head - j * Step);
			const FVector2D B = At(Head - (j + 1) * Step);
			// Not across a corner in one stroke: a segment that turns is skipped.
			if (FMath::Abs(A.X - B.X) > 0.5f && FMath::Abs(A.Y - B.Y) > 0.5f)
			{
				continue;
			}
			Line(A, B, Colour * FLinearColor(1, 1, 1, 0.55f * Fade), (2.0f + 7.0f * Fade) * S);
			Line(A, B, Hot * FLinearColor(1, 1, 1, 0.95f * Fade), (1.0f + 2.5f * Fade) * S);
		}
		// The head itself, a bright spark.
		const FVector2D P = At(Head);
		const float R = 5.0f * S;
		Line(P - FVector2D(R, 0.0f), P + FVector2D(R, 0.0f), Hot, 3.0f * S);
		Line(P - FVector2D(0.0f, R), P + FVector2D(0.0f, R), Hot, 3.0f * S);
		Line(P - FVector2D(R * 0.6f, R * 0.6f), P + FVector2D(R * 0.6f, R * 0.6f), Colour * FLinearColor(1, 1, 1, 0.7f), 2.0f * S);
		Line(P - FVector2D(R * 0.6f, -R * 0.6f), P + FVector2D(R * 0.6f, -R * 0.6f), Colour * FLinearColor(1, 1, 1, 0.7f), 2.0f * S);
	}
}

UTexture2D* ATMBattleHud::ItemIcon(const TMSim::FItemDef& Item)
{
	return Icon(TEXT("items/") + FString(UTF8_TO_TCHAR(Item.Id.c_str())), false);
}

float ATMBattleHud::GearRow(const TMSim::FUnit& Unit, float X, float Y, float Size)
{
	float Used = 0.0f;
	for (int32 i = 0; i < TMSim::Items::Slots; ++i)
	{
		if (const TMSim::FItemDef* Item = Unit.Gear[i])
		{
			ItemBadge(Item, X + Used, Y, Size, false);
			Used += Size + 2.0f * S;
		}
	}
	return Used;
}

void ATMBattleHud::DrawWorldWords(ATMBattleDirector& From)
{
	// Over the board, so they are drawn by the HUD rather than as text in the
	// world: larger, near white, and outlined dark so they read over snow, grass
	// and fog alike. The director's text components still say where and what.
	if (!PlayerOwner)
	{
		return;
	}
	UFont* Big = GEngine->GetLargeFont();
	UFont* Font = GEngine->GetMediumFont();
	auto Whiter = [](const FLinearColor& C, float Part, float Alpha)
	{
		FLinearColor Out = FMath::Lerp(C, FLinearColor::White, Part);
		Out.A = Alpha;
		return Out;
	};
	auto Lines = [&](const FString& What, const FVector& World, const FLinearColor& Colour, UFont* With, float Scale, float Edge)
	{
		FVector2D At;
		if (!PlayerOwner->ProjectWorldLocationToScreen(World, At))
		{
			return;
		}
		TArray<FString> Rows;
		What.ParseIntoArray(Rows, TEXT("\n"), true);
		float Y = At.Y;
		for (const FString& Row : Rows)
		{
			const FVector2D Size = TextSize(Row, With, Scale);
			OutlinedText(Row, At.X - Size.X * 0.5f, Y - Size.Y, Colour, With, Scale, Edge);
			Y += Size.Y;
		}
	};

	// What lies in each cache: its best item's name in its tier's colour, and how many more.
	for (int32 i = 0; i < From.CacheChests.Num() && i < static_cast<int32>(From.Battle.Caches.size()); ++i)
	{
		const TMSim::FCache& Cache = From.Battle.Caches[static_cast<size_t>(i)];
		if (!From.CacheChests[i] || !From.CacheChests[i]->IsVisible() || Cache.Items.empty())
		{
			continue;
		}
		const TMSim::FItemDef* Best = Cache.Items[0];
		for (const TMSim::FItemDef* Item : Cache.Items)
		{
			Best = Item->Tier > Best->Tier ? Item : Best;
		}
		FString Line = UTF8_TO_TCHAR(Best->Name.c_str());
		if (Cache.Items.size() > 1)
		{
			Line += FString::Printf(TEXT("  +%d"), static_cast<int32>(Cache.Items.size()) - 1);
		}
		Lines(Line, From.CacheChests[i]->GetComponentLocation() + FVector(0.0f, 0.0f, 1.0f * From.TileSize),
			Whiter(TierColour(static_cast<int32>(Best->Tier)), 0.45f, 1.0f), Font, 0.62f * S, 1.6f * S);
	}

	// The camps' names and clocks.
	for (const TObjectPtr<UTextRenderComponent>& Label : From.CampLabels)
	{
		if (Label && Label->IsVisible())
		{
			Lines(Label->Text.ToString(), Label->GetComponentLocation(), Whiter(Label->TextRenderColor.ReinterpretAsLinear(), 0.6f, 1.0f),
				Font, 0.66f * S, 1.6f * S);
		}
	}

	// The numbers off each blow, last so they sit on top.
	const float Numbers = FTMSettings::Get().DamageTextScale;
	for (const FTMFloater& Floater : From.Floaters)
	{
		if (!Floater.Text)
		{
			continue;
		}
		const FColor C = Floater.Text->TextRenderColor;
		const float Alpha = C.A / 255.0f;
		if (!Floater.bExact)
		{
			Lines(Floater.Text->Text.ToString(), Floater.Text->GetComponentLocation(),
				Whiter(FColor(C.R, C.G, C.B).ReinterpretAsLinear(), 0.4f, Alpha), Big, 0.95f * S * Numbers, 2.0f * S);
			continue;
		}
		// Its own look ("Combat Text Mockups" A): exact colour, size, weight, a pop, a tag.
		FVector2D At;
		if (!PlayerOwner->ProjectWorldLocationToScreen(Floater.Text->GetComponentLocation(), At))
		{
			continue;
		}
		const float Pop = Floater.bPop ? 1.0f + 0.4f * FMath::Max(0.0f, 1.0f - Floater.Age / 0.16f) : 1.0f;
		const float Scale = 0.95f * S * Numbers * Floater.Scale * Pop;
		const FString Word = Floater.Text->Text.ToString();
		const FVector2D Size = TextSize(Word, Big, Scale);
		const float TagScale = 0.95f * S * Numbers * 0.5f;
		const FVector2D TagSize = Floater.Tag.IsEmpty() ? FVector2D::ZeroVector : TextSize(Floater.Tag, Big, TagScale);
		const float Gap = Floater.Tag.IsEmpty() ? 0.0f : 5.0f * S;
		const float X = At.X - (Size.X + Gap + TagSize.X) * 0.5f;
		const float Y = At.Y - Size.Y;
		const FLinearColor Colour = FColor(C.R, C.G, C.B).ReinterpretAsLinear().CopyWithNewOpacity(Alpha);
		if (Floater.bEmber)
		{
			// A soft ember glow, then a pale edge: dark red reads on a dark field.
			const FLinearColor Glow(1.0f, 0.43f, 0.2f, 0.28f * Alpha);
			const FLinearColor Rim(1.0f, 0.79f, 0.72f, Alpha);
			for (int32 k = 0; k < 8; ++k)
			{
				const float A = k * PI / 4.0f;
				DrawText(Word, Glow, X + FMath::Cos(A) * 3.5f * S, Y + FMath::Sin(A) * 3.5f * S, Big, Scale * FontBoost);
			}
			for (int32 k = 0; k < 8; ++k)
			{
				const float A = k * PI / 4.0f;
				DrawText(Word, Rim, X + FMath::Cos(A) * 1.3f * S, Y + FMath::Sin(A) * 1.3f * S, Big, Scale * FontBoost);
			}
			DrawText(Word, Colour, X, Y, Big, Scale * FontBoost);
		}
		else
		{
			OutlinedText(Word, X, Y, Colour, Big, Scale, 2.0f * S);
		}
		if (Floater.bBold)
		{
			// Twice, a hair apart: heavier.
			DrawText(Word, Colour, X + FMath::Max(1.0f, 0.9f * S * Floater.Scale), Y, Big, Scale * FontBoost);
		}
		if (!Floater.Tag.IsEmpty())
		{
			OutlinedText(Floater.Tag, X + Size.X + Gap, Y + Size.Y - TagSize.Y - 2.0f * S,
				Floater.TagTint.ReinterpretAsLinear().CopyWithNewOpacity(Alpha), Big, TagScale, 1.5f * S);
		}
	}
}

void ATMBattleHud::ItemBadge(const TMSim::FItemDef* Item, float X, float Y, float Size, bool bTip)
{
	UFont* Font = GEngine->GetMediumFont();
	if (!Item)
	{
		Panel(X, Y, Size, Size, FLinearColor(0.03f, 0.04f, 0.07f, 0.85f), FLinearColor(0.35f, 0.38f, 0.45f, 0.6f), 1.0f);
		const FVector2D PlusSize = TextSize(TEXT("+"), Font, Size / 60.0f);
		Text(TEXT("+"), X + (Size - PlusSize.X) * 0.5f, Y + (Size - PlusSize.Y) * 0.5f, Dim, Font, Size / 60.0f);
		return;
	}
	const FLinearColor Tint = TierColour(static_cast<int32>(Item->Tier));
	if (UTexture2D* Art = ItemIcon(*Item))
	{
		// Its icon, which carries its tier's colour in its own frame.
		DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.55f), X - 1.0f, Y - 1.0f, Size + 2.0f, Size + 2.0f);
		Picture(Art, X, Y, Size, Size);
	}
	else
	{
		Panel(X, Y, Size, Size, Tint * FLinearColor(0.18f, 0.18f, 0.18f, 0.95f), Tint, FMath::Max(1.0f, Size / 22.0f));
		// Its initials, when it has no icon of its own (Tools/ItemIcons draws them).
		FString Initials;
		TArray<FString> Words;
		FString(UTF8_TO_TCHAR(Item->Name.c_str())).ParseIntoArray(Words, TEXT(" "), true);
		for (const FString& Word : Words)
		{
			if (Initials.Len() < 2 && Word.Len() > 0 && FChar::IsUpper(Word[0]))
			{
				Initials.AppendChar(Word[0]);
			}
		}
		if (Initials.IsEmpty() && Words.Num() > 0)
		{
			Initials = Words[0].Left(2);
		}
		const float Scale = Size / 70.0f;
		const FVector2D InitialsSize = TextSize(Initials, Font, Scale);
		Text(Initials, X + (Size - InitialsSize.X) * 0.5f, Y + (Size - InitialsSize.Y) * 0.5f, Tint, Font, Scale);
	}
	if (bTip)
	{
		AddTip(X, Y, Size, Size, FString::Printf(TEXT("%hs  -  %hs%s\n%s\n\n%hs"), Item->Name.c_str(), TMSim::ItemTierName(Item->Tier),
			Item->Cost > 0 ? *FString::Printf(TEXT(", %d point%s"), Item->Cost, Item->Cost == 1 ? TEXT("") : TEXT("s")) : TEXT(", found only"),
			*ItemSummary(*Item), Item->Desc.c_str()));
	}
}

void ATMBattleHud::ChoiceButton(float X, float Y, float W, float H, const FString& Label, ETMHudAction Action, int32 Value, bool bEnabled)
{
	// A button that may not be pressed just now: drawn greyed, and not a button.
	if (bEnabled)
	{
		MenuButton(X, Y, W, H, Label, Action, Value);
		return;
	}
	Panel(X, Y, W, H, FLinearColor(0.08f, 0.09f, 0.12f, 0.9f), FLinearColor(0.3f, 0.32f, 0.38f, 0.6f), 1.0f);
	UFont* Font = GEngine->GetMediumFont();
	const FVector2D Size = TextSize(Label, Font, 0.66f * S);
	Text(Label, X + (W - Size.X) * 0.5f, Y + (H - Size.Y) * 0.5f, Dim, Font, 0.66f * S);
}

void ATMBattleHud::DrawTakePicker(ATMBattleDirector& From)
{
	// Over the action bar: the items lying within reach, each to take with this
	// unit's action (over one of its own when its three slots are full), and its
	// own three to leave on the ground. Docs/design/feat-neutral-camps.md 3.4.
	const TMSim::FUnit* Unit = From.SelectedUnit();
	if (From.TakePickerCache == -1 || !Unit || !Unit->IsAlive())
	{
		return;
	}
	const bool bControllable = From.PlayerCanOrder(Unit);
	const TMSim::FCache* Cache = From.TakePickerCache >= 0 && From.TakePickerCache < static_cast<int32>(From.Battle.Caches.size())
		? &From.Battle.Caches[static_cast<size_t>(From.TakePickerCache)] : nullptr;
	UFont* Font = GEngine->GetMediumFont();
	const float Row = 50.0f * S;
	const int32 Lying = Cache ? static_cast<int32>(Cache->Items.size()) : 0;
	const float PW = 720.0f * S;
	const float PH = 100.0f * S + FMath::Max(1, Lying) * Row + 3.0f * Row;
	const float PX = (Canvas->ClipX - PW) * 0.5f;
	const float PY = ActionBarTop - PH - 12.0f * S;
	Panel(PX, PY, PW, PH, FLinearColor(0.04f, 0.05f, 0.09f, 0.96f), Gold * FLinearColor(1, 1, 1, 0.6f), 1.5f);
	Text(TEXT("Items"), PX + 18.0f * S, PY + 12.0f * S, Gold, Font, 0.7f * S);
	MenuButton(PX + PW - 110.0f * S, PY + 10.0f * S, 92.0f * S, 30.0f * S, TEXT("Close"), ETMHudAction::TakeOpen);
	float Y = PY + 52.0f * S;
	const bool bFull = Unit->Gear[0] && Unit->Gear[1] && Unit->Gear[2];
	const bool bCanTake = bControllable && !Unit->bActed;
	Text(Cache ? (bFull ? TEXT("Within reach (slots full: choose which of yours to leave for it):") : TEXT("Within reach, to take with this unit's action:"))
		: TEXT("Nothing within reach. Stand next to items on the ground to take them."), PX + 18.0f * S, Y, Dim, Font, 0.48f * S);
	Y += 26.0f * S;
	for (int32 i = 0; i < Lying; ++i)
	{
		const TMSim::FItemDef& Item = *Cache->Items[static_cast<size_t>(i)];
		ItemBadge(&Item, PX + 18.0f * S, Y, 40.0f * S);
		Text(UTF8_TO_TCHAR(Item.Name.c_str()), PX + 68.0f * S, Y, TierColour(static_cast<int32>(Item.Tier)), Font, 0.52f * S);
		Text(ItemSummary(Item), PX + 68.0f * S, Y + 22.0f * S, Dim, Font, 0.4f * S);
		const bool bHas = Unit->Carries(Item.Id);
		if (!bFull)
		{
			ChoiceButton(PX + PW - 110.0f * S, Y + 4.0f * S, 92.0f * S, 32.0f * S, bHas ? TEXT("Carried") : TEXT("Take"), ETMHudAction::Take,
				i * 4, bCanTake && !bHas);
		}
		else
		{
			for (int32 Slot = 0; Slot < 3; ++Slot)
			{
				const TMSim::FItemDef* Mine = Unit->Gear[Slot];
				ChoiceButton(PX + PW - (3 - Slot) * 118.0f * S, Y + 4.0f * S, 110.0f * S, 32.0f * S,
					FString::Printf(TEXT("for %hs"), Mine ? Mine->Name.substr(0, 9).c_str() : "?"), ETMHudAction::Take, i * 4 + Slot + 1, bCanTake && !bHas);
			}
		}
		Y += Row;
	}
	if (Lying == 0)
	{
		Y += Row * 0.4f;
	}
	Y += 8.0f * S;
	Text(TEXT("Carried (leaving one is free; an ally can pick it up):"), PX + 18.0f * S, Y, Dim, Font, 0.48f * S);
	Y += 26.0f * S;
	float X = PX + 18.0f * S;
	for (int32 Slot = 0; Slot < 3; ++Slot)
	{
		const TMSim::FItemDef* Mine = Unit->Gear[Slot];
		ItemBadge(Mine, X, Y, 40.0f * S);
		if (Mine)
		{
			ChoiceButton(X + 46.0f * S, Y + 4.0f * S, 150.0f * S, 32.0f * S, TEXT("Leave it here"), ETMHudAction::Drop, Slot, bControllable);
		}
		X += 230.0f * S;
	}
}

void ATMBattleHud::DrawTeamItems(ATMBattleDirector& From)
{
	// The team items screen (2026-10-01): every unit of the side in one place,
	// what each wears and its open slots, and the side's stash. Pick an item in
	// the stash, then an open slot, to equip it (free, any time). A worn item
	// comes off only on its wearer's turn, and that is the whole turn.
	if (!From.bTeamItemsOpen || From.Screen != ATMBattleDirector::EScreen::Battle)
	{
		return;
	}
	const int32 Team = From.ItemsTeam();
	if (Team != 0 && Team != 1)
	{
		return;
	}
	const bool bManage = From.MayManageItems(Team);
	UFont* Font = GEngine->GetMediumFont();
	UFont* Big = GEngine->GetLargeFont();
	const std::vector<TMSim::FBattle::FStashed>& Held = From.Battle.Stash[Team];
	TArray<const TMSim::FUnit*> Party;
	for (const TMSim::FUnit& Unit : From.Battle.Units)
	{
		if (!Unit.bMonster && Unit.HomeTeam() == Team)
		{
			Party.Add(&Unit);
		}
	}

	const float Badge = 54.0f * S;
	const float RowH = Badge + 30.0f * S;
	const float PW = FMath::Min(Canvas->ClipX - 40.0f * S, 1080.0f * S);
	const float PH = 210.0f * S + Party.Num() * RowH;
	const float PX = (Canvas->ClipX - PW) * 0.5f;
	const float PY = FMath::Max(20.0f * S, (Canvas->ClipY - PH) * 0.42f);
	Panel(PX, PY, PW, PH, FLinearColor(0.04f, 0.05f, 0.09f, 0.97f), TeamColour(Team) * FLinearColor(1, 1, 1, 0.75f), 1.5f);
	// The panel itself catches clicks, so one on its empty part doesn't fall through to the board (and drops a pick).
	AddButton(PX, PY, PW, PH, ETMHudAction::StashPick, -1);
	Text(FString::Printf(TEXT("%s team items"), Team == 0 ? TEXT("Blue") : TEXT("Red")), PX + 22.0f * S, PY + 14.0f * S, TeamColour(Team), Big, 0.7f * S);
	MenuButton(PX + PW - 140.0f * S, PY + 14.0f * S, 120.0f * S, 34.0f * S, TEXT("Close  (Esc)"), ETMHudAction::TakeOpen);

	// The stash.
	float Y = PY + 60.0f * S;
	Text(FString::Printf(TEXT("Stash  (%d)"), static_cast<int32>(Held.size())), PX + 22.0f * S, Y, Gold, Font, 0.6f * S);
	Text(Held.empty() ? FString(TEXT("Empty: walk a unit onto items on the ground and they come here."))
		: From.StashPick >= 0 ? FString(TEXT("Now click an open slot below to equip it."))
		: FString(TEXT("Click an item, then an open slot below, to equip it. Free, any time.")),
		PX + 140.0f * S, Y + 4.0f * S, Dim, Font, 0.48f * S);
	Y += 30.0f * S;
	float X = PX + 22.0f * S;
	for (int32 i = 0; i < static_cast<int32>(Held.size()); ++i)
	{
		if (X + Badge > PX + PW - 20.0f * S)
		{
			break;
		}
		const TMSim::FItemDef* Item = Held[static_cast<size_t>(i)].Item;
		if (From.StashPick == i)
		{
			DrawRect(Gold, X - 3.0f * S, Y - 3.0f * S, Badge + 6.0f * S, Badge + 6.0f * S);
		}
		ItemBadge(Item, X, Y, Badge);
		if (bManage)
		{
			AddButton(X, Y, Badge, Badge, ETMHudAction::StashPick, i);
		}
		X += Badge + 10.0f * S;
	}

	// Items lying within reach of the unit in hand, to pick up into the stash.
	const TMSim::FUnit* InHand = From.SelectedUnit();
	const int32 Near = InHand && From.PlayerCanOrder(InHand) && InHand->Team == Team ? From.Battle.CacheNear(InHand->Pos) : -1;
	Y += Badge + 14.0f * S;
	if (Near >= 0 && !From.Battle.Caches[static_cast<size_t>(Near)].Items.empty())
	{
		const TMSim::FCache& Cache = From.Battle.Caches[static_cast<size_t>(Near)];
		Text(FString::Printf(TEXT("Within %s's reach:"), *From.NameOf(InHand->Id)), PX + 22.0f * S, Y + 6.0f * S, Dim, Font, 0.5f * S);
		float CX = PX + 230.0f * S;
		for (int32 i = 0; i < static_cast<int32>(Cache.Items.size()) && CX + 180.0f * S < PX + PW; ++i)
		{
			ItemBadge(Cache.Items[static_cast<size_t>(i)], CX, Y, 32.0f * S);
			MenuButton(CX + 36.0f * S, Y, 110.0f * S, 32.0f * S, TEXT("Pick up"), ETMHudAction::Take, i);
			CX += 160.0f * S;
		}
		Y += 42.0f * S;
	}
	else
	{
		Y += 6.0f * S;
	}

	// The party: each unit, what it wears, its open slots.
	for (const TMSim::FUnit* Unit : Party)
	{
		const bool bUp = Unit->IsAlive();
		const TMSim::FJobDef* Job = TMSim::FindJob(Unit->Job);
		Panel(PX + 16.0f * S, Y, PW - 32.0f * S, RowH - 8.0f * S, FLinearColor(0.07f, 0.08f, 0.12f, 0.9f),
			FLinearColor(0.3f, 0.34f, 0.42f, 0.6f), 1.0f);
		const FString Name = Job ? FString(UTF8_TO_TCHAR(Job->Name.c_str())) : From.NameOf(Unit->Id);
		Text(Name, PX + 30.0f * S, Y + 10.0f * S, bUp ? TextColour : Dim, Font, 0.62f * S);
		Text(bUp ? FString::Printf(TEXT("HP %d / %d"), Unit->Hp, Unit->MaxHp()) : FString(TEXT("fallen")),
			PX + 30.0f * S, Y + 38.0f * S, bUp ? Dim : Urgent * FLinearColor(1, 1, 1, 0.7f), Font, 0.48f * S);
		const bool bTurn = bUp && From.PlayerCanOrder(Unit);
		float SX = PX + 260.0f * S;
		const float SY = Y + (RowH - 8.0f * S - Badge) * 0.5f;
		for (int32 Slot = 0; Slot < 3; ++Slot)
		{
			const TMSim::FItemDef* Worn = Unit->Gear[Slot];
			const int32 Code = Unit->Id * 4 + Slot;
			if (Worn)
			{
				ItemBadge(Worn, SX, SY, Badge);
				Text(UTF8_TO_TCHAR(Worn->Name.c_str()), SX + Badge + 8.0f * S, SY + 2.0f * S, TierColour(static_cast<int32>(Worn->Tier)), Font, 0.45f * S);
				const bool bCanOff = bTurn && From.Battle.ValidateDrop(Unit->Id, Slot).empty();
				ChoiceButton(SX + Badge + 8.0f * S, SY + 24.0f * S, 150.0f * S, 28.0f * S, TEXT("Take off (turn)"), ETMHudAction::Drop, Code, bCanOff);
				AddTip(SX + Badge + 8.0f * S, SY + 24.0f * S, 150.0f * S, 28.0f * S, bCanOff
					? FString(TEXT("Back into the stash. It takes this unit's whole turn."))
					: FString(TEXT("An item comes off only on its wearer's turn, before it has moved or acted, and that is its whole turn. When a unit falls, what it wore comes back to the stash.")));
			}
			else
			{
				const bool bReady = bManage && bUp && From.StashPick >= 0;
				Panel(SX, SY, Badge, Badge, bReady ? FLinearColor(0.16f, 0.14f, 0.06f, 0.95f) : FLinearColor(0.03f, 0.04f, 0.07f, 0.85f),
					bReady ? Gold : FLinearColor(0.3f, 0.33f, 0.4f, 0.55f), bReady ? 1.5f : 1.0f);
				const FVector2D Plus = TextSize(TEXT("+"), Font, 0.8f * S);
				Text(TEXT("+"), SX + (Badge - Plus.X) * 0.5f, SY + (Badge - Plus.Y) * 0.5f, bReady ? Gold : Dim, Font, 0.8f * S);
				Text(TEXT("open slot"), SX + Badge + 8.0f * S, SY + 16.0f * S, Dim, Font, 0.45f * S);
				if (bManage && bUp)
				{
					AddButton(SX, SY, Badge, Badge, ETMHudAction::EquipSlot, Code);
				}
			}
			SX += Badge + 175.0f * S;
		}
		Y += RowH;
	}
}

void ATMBattleHud::DrawItemPicker(ATMBattleDirector& From)
{
	// One of a unit's three slots on the setup screen: every item by tier,
	// what it costs and does, and what the side has left to spend.
	const std::vector<const TMSim::FItemDef*>& All = TMSim::AllItems();
	UFont* Font = GEngine->GetMediumFont();
	UFont* Big = GEngine->GetLargeFont();
	const int32 Code = From.ItemPickerSlot;
	const int32 Team = Code / 12;
	const int32 UnitSlot = (Code % 12) / 3;
	const int32 ItemSlot = Code % 3;
	const TMSim::FJobDef* Job = TMSim::FindJob(From.Setup.Rosters[Team][UnitSlot]);
	const TMSim::FItemDef* Current = TMSim::FindItem(From.Setup.Items[Team][UnitSlot][ItemSlot]);
	const int32 Left = From.Setup.ItemBudget - From.ItemPointsSpent(Team) + (Current ? Current->Cost : 0);

	DrawRect(FLinearColor(0.02f, 0.03f, 0.06f, 0.7f), 0.0f, 0.0f, Canvas->ClipX, Canvas->ClipY);
	const float PW = FMath::Min(Canvas->ClipX - 40.0f * S, 1400.0f * S);
	const float PH = Canvas->ClipY - 60.0f * S;
	const float PX = (Canvas->ClipX - PW) * 0.5f;
	const float PY = 30.0f * S;
	Panel(PX, PY, PW, PH, FLinearColor(0.05f, 0.06f, 0.1f, 0.97f), TeamColour(Team) * FLinearColor(1, 1, 1, 0.7f), 1.5f);
	Text(FString::Printf(TEXT("%s %s, item slot %d"), Team == 0 ? TEXT("Blue") : TEXT("Red"),
		Job ? UTF8_TO_TCHAR(Job->Name.c_str()) : TEXT("unit"), ItemSlot + 1), PX + 24.0f * S, PY + 16.0f * S, TeamColour(Team), Big, 0.8f * S);
	Text(FString::Printf(TEXT("%d of %d points left for this slot"), Left, From.Setup.ItemBudget),
		PX + 24.0f * S, PY + 56.0f * S, Dim, Font, 0.55f * S);
	MenuButton(PX + PW - 150.0f * S, PY + 16.0f * S, 126.0f * S, 36.0f * S, TEXT("Close  (Esc)"), ETMHudAction::ItemClose);
	MenuButton(PX + PW - 330.0f * S, PY + 16.0f * S, 170.0f * S, 36.0f * S, TEXT("Empty this slot"), ETMHudAction::ItemChoose, -1);

	// The tier filter.
	float FX = PX + 24.0f * S;
	const float FY = PY + 88.0f * S;
	MenuButton(FX, FY, 110.0f * S, 32.0f * S, TEXT("All"), ETMHudAction::ItemTier, -1, From.ItemPickerTier < 0);
	FX += 118.0f * S;
	for (int32 t = 0; t < 4; ++t)
	{
		FString Name = UTF8_TO_TCHAR(TMSim::ItemTierName(static_cast<TMSim::EItemTier>(t)));
		Name = Name.Left(1).ToUpper() + Name.Mid(1);
		MenuButton(FX, FY, 130.0f * S, 32.0f * S, Name, ETMHudAction::ItemTier, t, From.ItemPickerTier == t);
		FX += 138.0f * S;
	}

	// The grid: wider once the catalog is long (the camps' items).
	int32 Shown = 0;
	for (const TMSim::FItemDef* Each : All)
	{
		Shown += From.ItemPickerTier < 0 || static_cast<int32>(Each->Tier) == From.ItemPickerTier ? 1 : 0;
	}
	const int32 Columns = Shown > 30 ? 5 : Shown > 24 ? 4 : 3;
	const float Gap = 8.0f * S;
	const float CellW = (PW - 48.0f * S - (Columns - 1) * Gap) / Columns;
	const float CellH = 64.0f * S;
	// Only the rows that fit, scrolled by the mouse wheel, with a bar to say where.
	const float GridTop = FY + 48.0f * S;
	const float GridBottom = PY + PH - 16.0f * S;
	const int32 Rows = (Shown + Columns - 1) / Columns;
	const int32 Fit = FMath::Max(1, FMath::FloorToInt((GridBottom - GridTop + Gap) / (CellH + Gap)));
	From.ItemPickerScroll = FMath::Clamp(From.ItemPickerScroll, 0, FMath::Max(0, Rows - Fit));
	if (Rows > Fit)
	{
		ScrollBar(PX + PW - 16.0f * S, GridTop, Fit * (CellH + Gap) - Gap, static_cast<float>(Fit), static_cast<float>(Rows),
			static_cast<float>(From.ItemPickerScroll));
		Text(FString::Printf(TEXT("Scroll for more: rows %d-%d of %d"), From.ItemPickerScroll + 1, From.ItemPickerScroll + Fit, Rows),
			PX + PW - 340.0f * S, PY + 94.0f * S, Dim, Font, 0.48f * S);
	}
	float Y = GridTop;
	int32 Column = 0;
	int32 Row = 0;
	const FVector2D Mouse = MousePoint();
	for (int32 i = 0; i < static_cast<int32>(All.size()); ++i)
	{
		const TMSim::FItemDef& Item = *All[static_cast<size_t>(i)];
		if (From.ItemPickerTier >= 0 && static_cast<int32>(Item.Tier) != From.ItemPickerTier)
		{
			continue;
		}
		if (Row < From.ItemPickerScroll || Row >= From.ItemPickerScroll + Fit)
		{
			// Scrolled out of view.
			if (++Column == Columns)
			{
				Column = 0;
				++Row;
			}
			continue;
		}
		const float X = PX + 24.0f * S + Column * (CellW + Gap);
		const bool bBuyable = Item.Cost > 0 && Item.Cost <= Left;
		const bool bHover = FBox2D(FVector2D(X, Y), FVector2D(X + CellW, Y + CellH)).IsInside(Mouse);
		Panel(X, Y, CellW, CellH, FLinearColor(0.08f, 0.09f, 0.14f, bHover && bBuyable ? 1.0f : 0.9f),
			&Item == Current ? Gold : TierColour(static_cast<int32>(Item.Tier)) * FLinearColor(1, 1, 1, bHover ? 0.9f : 0.35f), 1.0f);
		ItemBadge(&Item, X + 8.0f * S, Y + 10.0f * S, 44.0f * S, false);
		const FLinearColor Words = bBuyable ? TextColour : Dim;
		Text(UTF8_TO_TCHAR(Item.Name.c_str()), X + 62.0f * S, Y + 8.0f * S, bBuyable ? TierColour(static_cast<int32>(Item.Tier)) : Dim, Font, 0.55f * S);
		const FString Price = Item.Cost > 0 ? FString::Printf(TEXT("%d pt%s"), Item.Cost, Item.Cost == 1 ? TEXT("") : TEXT("s")) : FString(TEXT("found only"));
		const FVector2D PriceSize = TextSize(Price, Font, 0.5f * S);
		Text(Price, X + CellW - PriceSize.X - 10.0f * S, Y + 10.0f * S, Item.Cost > Left || Item.Cost <= 0 ? Urgent : Dim, Font, 0.5f * S);
		Text(ItemSummary(Item), X + 62.0f * S, Y + 34.0f * S, Words, Font, 0.44f * S);
		AddTip(X, Y, CellW, CellH, FString::Printf(TEXT("%hs\n%s\n\n%hs"), Item.Name.c_str(), *ItemSummary(Item), Item.Desc.c_str()));
		AddButton(X, Y, CellW, CellH, ETMHudAction::ItemChoose, i);
		if (++Column == Columns)
		{
			Column = 0;
			++Row;
			Y += CellH + Gap;
		}
	}
}

void ATMBattleHud::DrawGuideItems(ATMBattleDirector& From, float PX, float PY, float PW, float PH, float Top)
{
	// Every item by tier: what it does, what it costs, where it comes from.
	const std::vector<const TMSim::FItemDef*>& All = TMSim::AllItems();
	UFont* Font = GEngine->GetMediumFont();
	const float X = PX + 24.0f * S;
	const float RowW = PW - 64.0f * S;
	float Y = Top;
	Text(TEXT("Item"), X + 52.0f * S, Y, Dim, Font, 0.5f * S);
	Text(TEXT("Tier"), X + 330.0f * S, Y, Dim, Font, 0.5f * S);
	Text(TEXT("Cost"), X + 470.0f * S, Y, Dim, Font, 0.5f * S);
	Text(TEXT("Does"), X + 580.0f * S, Y, Dim, Font, 0.5f * S);
	Y += 28.0f * S;
	const float ListTop = Y;
	const float Bottom = PY + PH - 44.0f * S;
	const float RowH = 44.0f * S;
	const float Step = RowH + 2.0f * S;
	const int32 Fit = FMath::Max(1, FMath::FloorToInt((Bottom - ListTop) / Step));
	const int32 Count = static_cast<int32>(All.size());
	From.GuideListPage = Fit;
	From.GuideListScroll = FMath::Clamp(From.GuideListScroll, 0, FMath::Max(0, Count - Fit));
	for (int32 r = From.GuideListScroll; r < Count && r < From.GuideListScroll + Fit; ++r)
	{
		const TMSim::FItemDef& Item = *All[static_cast<size_t>(r)];
		const FLinearColor Tint = TierColour(static_cast<int32>(Item.Tier));
		ItemBadge(&Item, X, Y, 38.0f * S);
		Text(UTF8_TO_TCHAR(Item.Name.c_str()), X + 52.0f * S, Y + 8.0f * S, Tint, Font, 0.56f * S);
		FString TierName = UTF8_TO_TCHAR(TMSim::ItemTierName(Item.Tier));
		Text(TierName.Left(1).ToUpper() + TierName.Mid(1), X + 330.0f * S, Y + 8.0f * S, Tint, Font, 0.5f * S);
		Text(Item.Cost > 0 ? FString::Printf(TEXT("%d point%s"), Item.Cost, Item.Cost == 1 ? TEXT("") : TEXT("s")) : FString(TEXT("epic camp only")),
			X + 470.0f * S, Y + 8.0f * S, TextColour, Font, 0.5f * S);
		Text(ItemSummary(Item), X + 580.0f * S, Y + 2.0f * S, TextColour, Font, 0.5f * S);
		Text(UTF8_TO_TCHAR(Item.Desc.c_str()), X + 580.0f * S, Y + 22.0f * S, Dim, Font, 0.42f * S);
		(void)RowW;
		Y += Step;
	}
	ScrollBar(PX + PW - 22.0f * S, ListTop, Fit * Step, static_cast<float>(Fit), static_cast<float>(Count), static_cast<float>(From.GuideListScroll));
	const int32 Last = FMath::Min(Count, From.GuideListScroll + Fit);
	Text(Count == 0 ? FString(TEXT("No item files in Content/Data/Items."))
		: FString::Printf(TEXT("%d-%d of %d items. Common, uncommon and rare can be bought on the setup screen; every tier drops from neutral camps."),
			From.GuideListScroll + 1, Last, Count), X, PY + PH - 32.0f * S, Dim, Font, 0.48f * S);
}

// -------------------------------------------------------------- Unit Guide

void ATMBattleHud::DrawGuide(ATMBattleDirector& From)
{
	// unit_guide.gd, grown: a list of every class that scrolls, and for the
	// class clicked a page of its own -- its hero turning, its stats, and every
	// ability in full, with what each does to a chosen target on level ground
	// before buffs. The numbers are the rules' own (CalcAmount, EvadeChance),
	// not worked out again here.
	const std::vector<const TMSim::FJobDef*>& Jobs = TMSim::AllJobs();
	if (Jobs.empty())
	{
		return;
	}
	From.GuideJob = FMath::Clamp(From.GuideJob, 0, static_cast<int32>(Jobs.size()) - 1);
	From.GuideAgainst = FMath::Clamp(From.GuideAgainst, 0, static_cast<int32>(Jobs.size()) - 1);

	DrawRect(FLinearColor(0.02f, 0.03f, 0.06f, 0.75f), 0.0f, 0.0f, Canvas->ClipX, Canvas->ClipY);
	const float PW = FMath::Min(Canvas->ClipX - 40.0f * S, 1560.0f * S);
	const float PH = Canvas->ClipY - 60.0f * S;
	const float PX = (Canvas->ClipX - PW) * 0.5f;
	const float PY = 30.0f * S;
	Panel(PX, PY, PW, PH, FLinearColor(0.05f, 0.06f, 0.1f, 0.97f), FLinearColor(0.4f, 0.45f, 0.55f, 0.8f), 1.5f);
	if (From.bGuideDetail)
	{
		DrawGuideDetail(From, PX, PY, PW, PH);
	}
	else
	{
		DrawGuideList(From, PX, PY, PW, PH);
	}
	MenuButton(PX + PW - 150.0f * S, PY + 16.0f * S, 126.0f * S, 36.0f * S, TEXT("Close  (U)"), ETMHudAction::ToggleGuide);
}

void ATMBattleHud::ScrollBar(float X, float Y, float H, float Shown, float Whole, float At)
{
	if (Whole <= Shown || Whole <= 0.0f || H <= 0.0f)
	{
		return;
	}
	const float W = 6.0f * S;
	DrawRect(FLinearColor(1.0f, 1.0f, 1.0f, 0.08f), X, Y, W, H);
	const float ThumbH = FMath::Max(24.0f * S, H * Shown / Whole);
	const float ThumbY = Y + (H - ThumbH) * FMath::Clamp(At / FMath::Max(1.0f, Whole - Shown), 0.0f, 1.0f);
	DrawRect(FLinearColor(Gold.R, Gold.G, Gold.B, 0.7f), X, ThumbY, W, ThumbH);
}

TArray<FString> ATMBattleHud::Wrap(const FString& What, UFont* Font, float Scale, float Width)
{
	TArray<FString> Out;
	TArray<FString> Paragraphs;
	What.ParseIntoArrayLines(Paragraphs, false);
	for (const FString& Paragraph : Paragraphs)
	{
		TArray<FString> Words;
		Paragraph.ParseIntoArray(Words, TEXT(" "), true);
		FString Line;
		for (const FString& Word : Words)
		{
			const FString Longer = Line.IsEmpty() ? Word : Line + TEXT(" ") + Word;
			if (!Line.IsEmpty() && TextSize(Longer, Font, Scale).X > Width)
			{
				Out.Add(Line);
				Line = Word;
			}
			else
			{
				Line = Longer;
			}
		}
		Out.Add(Line);
	}
	return Out;
}

void ATMBattleHud::DrawGuideList(ATMBattleDirector& From, float PX, float PY, float PW, float PH)
{
	const std::vector<const TMSim::FJobDef*>& Jobs = TMSim::AllJobs();
	UFont* Font = GEngine->GetMediumFont();
	UFont* Big = GEngine->GetLargeFont();
	Text(TEXT("Codex"), PX + 24.0f * S, PY + 16.0f * S, Gold, Big, 0.9f * S);
	// The pages: the classes, the items, and the rest of the Codex (TMBattleHudCodex.cpp).
	const TCHAR* Tabs[] = { TEXT("Classes"), TEXT("Items"), TEXT("Basics"), TEXT("Keywords"), TEXT("Statuses"), TEXT("Ground"), TEXT("Objectives"), TEXT("Controls") };
	const int32 TabCount = UE_ARRAY_COUNT(Tabs);
	const float TabsFrom = PX + 170.0f * S;
	const float TabStep = FMath::Min(128.0f * S, (PW - 170.0f * S - 170.0f * S) / TabCount);
	for (int32 t = 0; t < TabCount; ++t)
	{
		MenuButton(TabsFrom + t * TabStep, PY + 16.0f * S, TabStep - 8.0f * S, 36.0f * S, Tabs[t], ETMHudAction::GuideTab, t, From.GuideTab == t);
	}
	From.GuideTab = FMath::Clamp(From.GuideTab, 0, TabCount - 1);
	if (From.GuideTab == 1)
	{
		DrawGuideItems(From, PX, PY, PW, PH, PY + 76.0f * S);
		return;
	}
	if (From.GuideTab >= 2)
	{
		DrawCodexPage(From, PX, PY, PW, PH, PY + 70.0f * S);
		return;
	}

	// The role filter, as the class picker has.
	const TArray<int32> Shown = From.GuideShown();
	const TCHAR* Roles[4] = { TEXT("tank"), TEXT("damage"), TEXT("support"), TEXT("special") };
	float FX = PX + 24.0f * S;
	const float FY = PY + 66.0f * S;
	MenuButton(FX, FY, 110.0f * S, 32.0f * S, TEXT("All"), ETMHudAction::GuideRole, -1, From.GuideRole < 0);
	FX += 118.0f * S;
	for (int32 r = 0; r < 4; ++r)
	{
		MenuButton(FX, FY, 110.0f * S, 32.0f * S, Roles[r], ETMHudAction::GuideRole, r, From.GuideRole == r);
		FX += 118.0f * S;
	}
	Text(TEXT("Click a class for its hero and every ability.  Wheel, arrows or Page Up / Down to scroll."),
		FX + 16.0f * S, FY + 7.0f * S, Dim, Font, 0.46f * S);

	// The table: a header, then as many rows as fit, from where it is scrolled to.
	// One Evasion column (the higher of a class's two, 2026-10-01).
	const TMSim::EStat Columns[] =
	{
		TMSim::EStat::Hp, TMSim::EStat::AttDef, TMSim::EStat::MagDef, TMSim::EStat::AEva,
		TMSim::EStat::Crit, TMSim::EStat::Speed, TMSim::EStat::Move, TMSim::EStat::Patience, TMSim::EStat::Sight
	};
	constexpr int32 ColumnCount = UE_ARRAY_COUNT(Columns);
	const float X = PX + 24.0f * S;
	const float IconW = 34.0f * S;
	const float NameW = 230.0f * S;
	const float RoleW = 190.0f * S;
	const float RowW = PW - 64.0f * S;
	const float ColW = FMath::Min(95.0f * S, (RowW - IconW - NameW - RoleW) / ColumnCount);
	float Y = FY + 50.0f * S;
	Text(TEXT("Class"), X + IconW, Y, Dim, Font, 0.5f * S);
	Text(TEXT("Role"), X + IconW + NameW, Y, Dim, Font, 0.5f * S);
	for (int32 c = 0; c < ColumnCount; ++c)
	{
		Text(ShownStatName(Columns[c]), X + IconW + NameW + RoleW + c * ColW, Y, Dim, Font, 0.5f * S);
	}
	Y += 28.0f * S;
	const float Top = Y;
	const float Bottom = PY + PH - 44.0f * S;
	const float RowH = 34.0f * S;
	const float Step = RowH + 2.0f * S;
	const int32 Fit = FMath::Max(1, FMath::FloorToInt((Bottom - Top) / Step));
	From.GuideListPage = Fit;
	From.GuideListScroll = FMath::Clamp(From.GuideListScroll, 0, FMath::Max(0, Shown.Num() - Fit));
	const FVector2D Mouse = MousePoint();
	for (int32 r = From.GuideListScroll; r < Shown.Num() && r < From.GuideListScroll + Fit; ++r)
	{
		const int32 j = Shown[r];
		const TMSim::FJobDef& Job = *Jobs[j];
		const bool bChosen = j == From.GuideJob;
		const bool bHover = FBox2D(FVector2D(X - 6.0f * S, Y - 3.0f * S), FVector2D(X - 6.0f * S + RowW, Y - 3.0f * S + RowH)).IsInside(Mouse);
		if (bChosen || bHover)
		{
			DrawRect(FLinearColor(Gold.R, Gold.G, Gold.B, bHover ? 0.18f : 0.1f), X - 6.0f * S, Y - 3.0f * S, RowW, RowH);
		}
		TMSim::FUnit Stand;
		Stand.Job = Job.Id;
		Picture(ClassIcon(Stand), X, Y - 1.0f * S, 26.0f * S, 26.0f * S);
		Text(UTF8_TO_TCHAR(Job.Name.c_str()), X + IconW, Y, bChosen || bHover ? Gold : TextColour, Font, 0.56f * S);
		FString RoleText;
		for (const std::string& JobRole : Job.Roles)
		{
			RoleText += (RoleText.IsEmpty() ? TEXT("") : TEXT(", ")) + FString(UTF8_TO_TCHAR(JobRole.c_str()));
		}
		Text(RoleText, X + IconW + NameW, Y, Dim, Font, 0.5f * S);
		for (int32 c = 0; c < ColumnCount; ++c)
		{
			const int32 Value = Columns[c] == TMSim::EStat::AEva ? ClassEvasion(Job.Stats) : Job.Stats.Get(Columns[c]);
			Text(FString::FromInt(Value), X + IconW + NameW + RoleW + c * ColW, Y, TextColour, Font, 0.54f * S);
		}
		AddButton(X - 6.0f * S, Y - 3.0f * S, RowW, RowH, ETMHudAction::GuideJob, j);
		Y += Step;
	}
	ScrollBar(PX + PW - 22.0f * S, Top, Fit * Step, static_cast<float>(Fit), static_cast<float>(Shown.Num()),
		static_cast<float>(From.GuideListScroll));

	// Where in the list this is, and a page at a time for those without a wheel.
	const float FootY = PY + PH - 38.0f * S;
	const int32 Last = FMath::Min(Shown.Num(), From.GuideListScroll + Fit);
	Text(Shown.Num() == 0 ? FString(TEXT("No class has that role."))
		: FString::Printf(TEXT("%d-%d of %d classes"), From.GuideListScroll + 1, Last, Shown.Num()),
		X, FootY + 6.0f * S, Dim, Font, 0.48f * S);
	if (Shown.Num() > Fit)
	{
		MenuButton(PX + PW - 290.0f * S, FootY, 130.0f * S, 30.0f * S, TEXT("Page up"), ETMHudAction::GuideScroll, -1);
		MenuButton(PX + PW - 152.0f * S, FootY, 130.0f * S, 30.0f * S, TEXT("Page down"), ETMHudAction::GuideScroll, 1);
	}
}

void ATMBattleHud::DrawGuideDetail(ATMBattleDirector& From, float PX, float PY, float PW, float PH)
{
	TMSim::FBattle& Battle = From.Battle;
	const std::vector<const TMSim::FJobDef*>& Jobs = TMSim::AllJobs();
	const TMSim::FJobDef& Job = *Jobs[From.GuideJob];
	const TMSim::FJobDef& Against = *Jobs[From.GuideAgainst];
	UFont* Font = GEngine->GetMediumFont();
	UFont* Big = GEngine->GetLargeFont();

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

	// The page scrolls under a fixed header. Everything is laid out from Top
	// less the scroll, and a piece is drawn only if it lies inside the view.
	const float Top = PY + 76.0f * S;
	const float Bottom = PY + PH - 14.0f * S;
	const float View = Bottom - Top;
	const float Off = From.GuideDetailScroll * S;
	auto Inside = [Top, Bottom](float Y, float H) { return Y >= Top - 1.0f && Y + H <= Bottom + 1.0f; };

	// ---- the left column: the hero, and the stats under it
	const float LX = PX + 24.0f * S;
	const float LW = FMath::Min(460.0f * S, PW * 0.34f);
	float Y = Top - Off;
	const float ModelH = LW * 720.0f / 560.0f;
	if (Inside(Y, ModelH))
	{
		Panel(LX, Y, LW, ModelH, FLinearColor(0.02f, 0.03f, 0.05f, 1.0f), FLinearColor(0.4f, 0.45f, 0.55f, 0.6f), 1.0f);
		bool bLoading = false;
		if (UTextureRenderTarget2D* Film = From.GuideModel(Job.Id, bLoading))
		{
			Picture(Film, LX + 1.0f * S, Y + 1.0f * S, LW - 2.0f * S, ModelH - 2.0f * S);
		}
		else
		{
			// The class's icon while the hero is read, or if it has none.
			const float IconSize = LW * 0.4f;
			Picture(ClassIcon(User), LX + (LW - IconSize) * 0.5f, Y + (ModelH - IconSize) * 0.4f, IconSize, IconSize, FLinearColor(1, 1, 1, 0.5f));
			const FString Say = bLoading ? TEXT("Loading the hero...") : TEXT("No hero model for this class yet");
			const FVector2D SaySize = TextSize(Say, Font, 0.5f * S);
			Text(Say, LX + (LW - SaySize.X) * 0.5f, Y + ModelH * 0.72f, Dim, Font, 0.5f * S);
		}
	}
	else
	{
		From.HideGuideModel();
	}
	Y += ModelH + 8.0f * S;
	if (Inside(Y, 32.0f * S))
	{
		MenuButton(LX, Y, 110.0f * S, 32.0f * S, TEXT("< Turn"), ETMHudAction::GuideTurn, 1);
		MenuButton(LX + LW - 110.0f * S, Y, 110.0f * S, 32.0f * S, TEXT("Turn >"), ETMHudAction::GuideTurn, -1);
		const FString Hint = TEXT("it turns on its own too");
		Text(Hint, LX + (LW - TextSize(Hint, Font, 0.44f * S).X) * 0.5f, Y + 8.0f * S, Dim, Font, 0.44f * S);
	}
	Y += 48.0f * S;

	// Every stat, with a bar against the highest any class has.
	if (Inside(Y, 30.0f * S))
	{
		Text(TEXT("Stats"), LX, Y, Gold, Big, 0.5f * S);
	}
	Y += 34.0f * S;
	for (int32 k = 0; k < TMSim::StatCount; ++k)
	{
		const TMSim::EStat Stat = static_cast<TMSim::EStat>(k);
		// One Evasion row: the higher of the two (2026-10-01).
		if (Stat == TMSim::EStat::MEva)
		{
			continue;
		}
		auto Read = [Stat](const TMSim::FJobStats& Stats) { return Stat == TMSim::EStat::AEva ? ClassEvasion(Stats) : Stats.Get(Stat); };
		int32 Most = 1;
		for (const TMSim::FJobDef* Other : Jobs)
		{
			Most = FMath::Max(Most, Read(Other->Stats));
		}
		const int32 Value = Read(Job.Stats);
		const float RowH = 26.0f * S;
		if (Inside(Y, RowH))
		{
			Text(ShownStatName(Stat), LX, Y, Dim, Font, 0.5f * S);
			Text(FString::FromInt(Value), LX + 110.0f * S, Y, TextColour, Font, 0.52f * S);
			const float BarX = LX + 170.0f * S;
			const float BarW = LW - 170.0f * S;
			DrawRect(FLinearColor(1.0f, 1.0f, 1.0f, 0.08f), BarX, Y + 7.0f * S, BarW, 10.0f * S);
			DrawRect(FLinearColor(Gold.R, Gold.G, Gold.B, 0.75f), BarX, Y + 7.0f * S, BarW * FMath::Clamp(static_cast<float>(Value) / Most, 0.0f, 1.0f), 10.0f * S);
			AddTip(LX, Y, LW, RowH, FString::Printf(TEXT("%s %d (the highest of any class is %d)"), ShownStatName(Stat), Value, Most));
		}
		Y += RowH + 2.0f * S;
	}
	const float LeftH = Y + Off - Top;

	// ---- the right column: every ability in full
	const float RX = LX + LW + 32.0f * S;
	const float RW = PX + PW - 40.0f * S - RX;
	Y = Top - Off;
	if (Inside(Y, 34.0f * S))
	{
		Text(TEXT("Abilities"), RX, Y, Gold, Big, 0.6f * S);
		Text(TEXT("Numbers against"), RX + 200.0f * S, Y + 8.0f * S, Dim, Font, 0.48f * S);
		MenuButton(RX + 350.0f * S, Y + 2.0f * S, 220.0f * S, 30.0f * S, UTF8_TO_TCHAR(Against.Name.c_str()), ETMHudAction::GuideAgainst);
		Text(TEXT("level ground, facing, before buffs (click to change)"), RX + 580.0f * S, Y + 9.0f * S, Dim, Font, 0.42f * S);
	}
	Y += 48.0f * S;
	for (int32 Slot = 0; Slot < 4; ++Slot)
	{
		const TMSim::FAbility* Ability = TMSim::JobAbility(Job.Id, Slot);
		if (!Ability)
		{
			continue;
		}
		FString ClassName;
		const FLinearColor Tint = AbilityClassColour(*Ability, &ClassName);
		const bool bPassive = Ability->Kind == "passive" || Ability->Kind == "aura";
		const float Pad = 12.0f * S;
		const float IconSize = 72.0f * S;
		const float TextX = RX + Pad + IconSize + 14.0f * S;
		const float TextW = RX + RW - Pad - TextX;

		// What it is, in facts.
		TArray<FString> Facts;
		const FString Kind = UTF8_TO_TCHAR(Ability->Kind.c_str());
		Facts.Add(Kind.Left(1).ToUpper() + Kind.Mid(1));
		if (!bPassive)
		{
			FString Shape = UTF8_TO_TCHAR(TMSim::ShapeOf(*Ability).c_str());
			Facts.Add(Shape.Left(1).ToUpper() + Shape.Mid(1));
			Facts.Add(Ability->MaxRange == 0.0f ? FString(TEXT("Self"))
				: Ability->MinRange > 0.0f ? FString::Printf(TEXT("Range %s-%s m"), *Num(Ability->MinRange), *Num(Ability->MaxRange))
				: FString::Printf(TEXT("Range %s m"), *Num(Ability->MaxRange)));
			if (Ability->Aoe > 0.0f)
			{
				Facts.Add(FString::Printf(TEXT("Radius %s m"), *Num(Ability->Aoe)));
			}
			if (TMSim::ShapeOf(*Ability) == "cone")
			{
				Facts.Add(FString::Printf(TEXT("%s degrees"), *Num(Ability->Angle)));
			}
			Facts.Add(Ability->Cast > 0.0f ? FString::Printf(TEXT("Cast %ss"), *Num(Battle.CastTicks(*Ability) / Tps)) : FString(TEXT("Instant")));
			Facts.Add(Ability->Cooldown > 0 ? FString::Printf(TEXT("Cooldown %d"), Ability->Cooldown) : FString(TEXT("No cooldown")));
		}
		if (Ability->TgChange != 0)
		{
			Facts.Add(FString::Printf(TEXT("Turn Gauge %+d%%"), Ability->TgChange));
		}
		if (Ability->HasStatus())
		{
			Facts.Add(FString::Printf(TEXT("%hs %d turn%s"), Ability->StatusId.c_str(), Ability->StatusTurns, Ability->StatusTurns == 1 ? TEXT("") : TEXT("s")));
		}
		for (const TMSim::FBuff& Buff : Ability->Buffs)
		{
			Facts.Add(FString::Printf(TEXT("%s %+d, %d turn%s"), ShownStatName(Buff.Stat), Buff.Amount, Buff.Turns, Buff.Turns == 1 ? TEXT("") : TEXT("s")));
		}

		// What it does to that target, as the rules work it out.
		FString Result;
		if (Ability->Effect == TMSim::EEffect::Damage)
		{
			const int32 Amount = Battle.CalcAmount(User, *Ability, User.Pos, Target, Target.Pos, 1, 1);
			Result = Battle.NewDefense()
				? FString::Printf(TEXT("Against %hs: %d damage  (%d%% evade: most graze for %d; %d%% crit)"), Against.Name.c_str(), Amount,
					Battle.EvadeChance(Target, *Ability, &User), FMath::Max(1, TMSim::RoundToInt(Amount * TMSim::Combat::GrazeDamage)), Battle.CritChance(User))
				: FString::Printf(TEXT("Against %hs: %d damage  (%d%% miss, %d%% crit)"), Against.Name.c_str(), Amount,
					Battle.EvadeChance(Target, *Ability, &User), Battle.CritChance(User));
		}
		else if (Ability->Effect == TMSim::EEffect::Heal || Ability->Effect == TMSim::EEffect::Revive)
		{
			// Healing is done to a friend, so it is worked out on the class itself.
			TMSim::FUnit Friend = User;
			Friend.Id = -102;
			const int32 Amount = Battle.CalcAmount(User, *Ability, User.Pos, Friend, User.Pos, 1, 1);
			Result = Ability->Effect == TMSim::EEffect::Heal ? FString::Printf(TEXT("Heals %d"), Amount)
				: FString::Printf(TEXT("Brings an ally back with %d HP"), Amount);
		}

		const float DescScale = 0.5f * S;
		const float SmallScale = 0.42f * S;
		const TArray<FString> Desc = Ability->Desc.empty() ? TArray<FString>()
			: Wrap(UTF8_TO_TCHAR(Ability->Desc.c_str()), Font, DescScale, TextW);
		const TArray<FString> FactLines = Wrap(FString::Join(Facts, TEXT("   |   ")), Font, 0.46f * S, TextW);
		const TArray<FString> How = Wrap(ExplainAbility(From, User, Slot), Font, SmallScale, TextW);
		const float LineH = TextSize(TEXT("Ag"), Font, DescScale).Y + 2.0f * S;
		const float SmallH = TextSize(TEXT("Ag"), Font, SmallScale).Y + 1.0f * S;
		const float HeadH = 34.0f * S;
		const float BodyH = HeadH + FactLines.Num() * LineH + 6.0f * S + Desc.Num() * LineH
			+ (Result.IsEmpty() ? 0.0f : LineH + 4.0f * S) + 6.0f * S + How.Num() * SmallH;
		const float CardH = FMath::Max(IconSize, BodyH) + 2.0f * Pad;

		if (Inside(Y, CardH))
		{
			Panel(RX, Y, RW, CardH, FLinearColor(0.08f, 0.09f, 0.14f, 0.95f), Tint * FLinearColor(1, 1, 1, 0.45f), 1.0f);
			if (UTexture2D* Picture2D = AbilityIcon(*Ability, false))
			{
				Picture(Picture2D, RX + Pad, Y + Pad, IconSize, IconSize);
			}
			const FString SlotName = Slot == 3 ? FString(TEXT("Ultimate")) : FString::Printf(TEXT("Ability %d"), Slot + 1);
			float TY = Y + Pad;
			Text(UTF8_TO_TCHAR(Ability->Name.c_str()), TextX, TY, Tint, Big, 0.55f * S);
			const FString Side = FString::Printf(TEXT("%s  -  %s"), *SlotName, *ClassName);
			Text(Side, RX + RW - Pad - TextSize(Side, Font, 0.46f * S).X, TY + 6.0f * S, Dim, Font, 0.46f * S);
			TY += HeadH;
			for (const FString& Line : FactLines)
			{
				Text(Line, TextX, TY, FLinearColor(0.75f, 0.85f, 1.0f), Font, 0.46f * S);
				TY += LineH;
			}
			TY += 6.0f * S;
			for (const FString& Line : Desc)
			{
				Text(Line, TextX, TY, TextColour, Font, DescScale);
				TY += LineH;
			}
			if (!Result.IsEmpty())
			{
				TY += 4.0f * S;
				Text(Result, TextX, TY, Tint, Font, DescScale);
				TY += LineH;
			}
			TY += 6.0f * S;
			for (const FString& Line : How)
			{
				Text(Line, TextX, TY, Dim, Font, SmallScale);
				TY += SmallH;
			}
		}
		Y += CardH + 10.0f * S;
	}
	const float RightH = Y + Off - Top;

	// Kept in range, and a bar to say there is more.
	const float Whole = FMath::Max(LeftH, RightH);
	const float MaxScroll = FMath::Max(0.0f, (Whole - View) / S);
	From.GuideDetailScroll = FMath::Clamp(From.GuideDetailScroll, 0.0f, MaxScroll);
	ScrollBar(PX + PW - 18.0f * S, Top, View, View, Whole, Off);

	// The header, over anything scrolled up under it: back, the class, and its neighbours.
	DrawRect(FLinearColor(0.05f, 0.06f, 0.1f, 1.0f), PX + 2.0f * S, PY + 2.0f * S, PW - 4.0f * S, Top - PY - 4.0f * S);
	MenuButton(PX + 24.0f * S, PY + 16.0f * S, 150.0f * S, 36.0f * S, TEXT("< All classes"), ETMHudAction::GuideBack);
	Picture(ClassIcon(User), PX + 190.0f * S, PY + 12.0f * S, 44.0f * S, 44.0f * S);
	Text(UTF8_TO_TCHAR(Job.Name.c_str()), PX + 244.0f * S, PY + 12.0f * S, Gold, Big, 0.8f * S);
	FString RoleText;
	for (const std::string& JobRole : Job.Roles)
	{
		RoleText += (RoleText.IsEmpty() ? TEXT("") : TEXT(", ")) + FString(UTF8_TO_TCHAR(JobRole.c_str()));
	}
	const float NameEnd = PX + 244.0f * S + TextSize(UTF8_TO_TCHAR(Job.Name.c_str()), Big, 0.8f * S).X;
	Text(RoleText, NameEnd + 16.0f * S, PY + 30.0f * S, Dim, Font, 0.5f * S);
	MenuButton(PX + PW - 420.0f * S, PY + 16.0f * S, 124.0f * S, 36.0f * S, TEXT("< Previous"), ETMHudAction::GuideStep, -1);
	MenuButton(PX + PW - 288.0f * S, PY + 16.0f * S, 124.0f * S, 36.0f * S, TEXT("Next >"), ETMHudAction::GuideStep, 1);
	AddTip(PX + PW - 420.0f * S, PY + 16.0f * S, 256.0f * S, 36.0f * S,
		TEXT("The previous or next class in the list (Left / Right). Esc or right-click goes back to the list."));
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
		AddTip(X, Y, CellW, CellH, FString::Printf(TEXT("%hs  (%s)\nHP %d  Armor %d  Resist %d  Evasion %d%%  Speed %d  Move %d\n%hs, %hs, %hs, %hs"),
			Job.Name.c_str(), *RoleText, Job.Stats.Get(TMSim::EStat::Hp), Job.Stats.Get(TMSim::EStat::AttDef),
			Job.Stats.Get(TMSim::EStat::MagDef), ClassEvasion(Job.Stats), Job.Stats.Get(TMSim::EStat::Speed), Job.Stats.Get(TMSim::EStat::Move),
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