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

	/** A box edged in dashes: a ghost, for something that has not happened yet. */
	void DashedEdge(TFunctionRef<void(const FLinearColor&, float, float, float, float)> Rect, float BX, float BY, float BW, float BH,
		const FLinearColor& Colour, float Thick, float Run, float Skip)
	{
		for (float Along = 0.0f; Along < BW; Along += Run + Skip)
		{
			const float Piece = FMath::Min(Run, BW - Along);
			Rect(Colour, BX + Along, BY, Piece, Thick);
			Rect(Colour, BX + Along, BY + BH - Thick, Piece, Thick);
		}
		for (float Along = 0.0f; Along < BH; Along += Run + Skip)
		{
			const float Piece = FMath::Min(Run, BH - Along);
			Rect(Colour, BX, BY + Along, Thick, Piece);
			Rect(Colour, BX + BW - Thick, BY + Along, Thick, Piece);
		}
	}

	/** A wild monster part-way through a wind-up (whether this side sees it is the caller's to ask). */
	bool WindingUp(const TMSim::FUnit& Unit)
	{
		return Unit.bMonster && Unit.Team == 2 && Unit.IsAlive() && !Unit.bOffBoard && Unit.IsCasting();
	}

	/** Where a cast will land: the unit it follows, or the spot it was aimed at. */
	TMSim::FVec2 CastAim(const TMSim::FBattle& Battle, const TMSim::FUnit& Unit)
	{
		if (Unit.Casting.FollowId >= 0)
		{
			for (const TMSim::FUnit& Each : Battle.Units)
			{
				if (Each.Id == Unit.Casting.FollowId)
				{
					return Each.Pos;
				}
			}
		}
		return Unit.Casting.Target;
	}

	/** "Shield Bash / Back on its 2nd turn from now, in about 16 s" (cooldown pins and squares). */
	FString BackLine(const FString& Who, const TMSim::FAbility& Ability, bool bPreview, int32 Turn, float Seconds)
	{
		FString Which = TEXT("its next turn");
		if (Turn > 1)
		{
			Which = FString::Printf(TEXT("its %d%s turn from now"), Turn, Turn == 2 ? TEXT("nd") : Turn == 3 ? TEXT("rd") : TEXT("th"));
		}
		return FString::Printf(TEXT("%s: %hs%s\nBack on %s, in about %.0f s"), *Who, Ability.Name.c_str(),
			bPreview ? TEXT(", if used now") : TEXT(""), *Which, Seconds);
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

void ATMBattleHud::DrawTooltip(ATMBattleDirector* From)
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
	// An ability: its card, beside the pointer and kept on screen.
	if (Found->CardUnit >= 0 && From)
	{
		if (const TMSim::FUnit* CardOwner = From->Battle.FindUnit(Found->CardUnit))
		{
			const bool bDetail = AltHeld();
			const FVector2D Card = AbilityCard(*From, *CardOwner, Found->CardSlot, 0.0f, 0.0f, bDetail, 0.0f, FString());
			if (Card.X > 0.0f)
			{
				float CardX = Mouse.X + 16.0f * S;
				float CardY = Mouse.Y + 16.0f * S;
				if (CardX + Card.X > Canvas->ClipX)
				{
					CardX = Mouse.X - Card.X - 8.0f * S;
				}
				if (CardY + Card.Y > Canvas->ClipY)
				{
					CardY = Mouse.Y - Card.Y - 8.0f * S;
				}
				const float CardEdge = 4.0f * S;
				CardX = FMath::Clamp(CardX, CardEdge, FMath::Max(CardEdge, Canvas->ClipX - Card.X - CardEdge));
				CardY = FMath::Clamp(CardY, CardEdge, FMath::Max(CardEdge, Canvas->ClipY - Card.Y - CardEdge));
				AbilityCard(*From, *CardOwner, Found->CardSlot, CardX, CardY, bDetail, Card.Y, FString());
				return;
			}
		}
	}
	UFont* Font = GEngine->GetMediumFont();
	const float Scale = 0.5f * S;
	// Long tips wrap at a reading width (and never wider than the screen) rather than running off its edge.
	const float TipWidth = FMath::Min(440.0f * S, Canvas->ClipX - 40.0f * S);
	const TArray<FString> Lines = Wrap(Found->Text, Font, Scale, TipWidth);
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
	// Kept wholly on screen whichever side of the pointer it went.
	const float TipEdge = 4.0f * S;
	X = FMath::Clamp(X, TipEdge, FMath::Max(TipEdge, Canvas->ClipX - BoxW - TipEdge));
	Y = FMath::Clamp(Y, TipEdge, FMath::Max(TipEdge, Canvas->ClipY - BoxH - TipEdge));
	Panel(X, Y, BoxW, BoxH, FLinearColor(0.03f, 0.04f, 0.07f, 0.96f), FLinearColor(1, 1, 1, 0.25f), 1.0f);
	for (int32 i = 0; i < Lines.Num(); ++i)
	{
		Text(Lines[i], X + Pad, Y + Pad + i * LineH, TextColour, Font, Scale);
	}
}

// ------------------------------------------------------------- ability card

// "Ability Info Mockups" (2026-10-05), the pick: D, a short card at once and the
// whole of it while Alt is held, with A's chips as the short card, and E, the
// card docked above the action bar while aiming. Every ability says each thing
// the same way, in the same order (the mockups' "Rules for every option"):
// damage, damage type, cooldown, range, target, cast, then its statuses, each
// with its turns, red for harm and green for help, then what it is for.

namespace
{
	const FLinearColor CardPhysical(0.95f, 0.65f, 0.35f);
	const FLinearColor CardMagic(0.56f, 0.71f, 1.0f);
	const FLinearColor CardHeal(0.5f, 0.85f, 0.54f);
	const FLinearColor CardReach(0.55f, 0.95f, 0.92f);
	const FLinearColor CardBad(0.91f, 0.47f, 0.42f);
	const FLinearColor CardGood(0.44f, 0.83f, 0.63f);

	FString CardTurns(int32 Count)
	{
		return FString::Printf(TEXT("%d turn%s"), Count, Count == 1 ? TEXT("") : TEXT("s"));
	}

	/** "2–8 m", "6 m", "Melee (1.8 m)", "Self", "Whole map". */
	FString CardRange(const TMSim::FAbility& Ability)
	{
		const std::string Shape = TMSim::ShapeOf(Ability);
		if (Shape == "global")
		{
			return TEXT("Whole map");
		}
		if (Ability.MaxRange <= 0.0f)
		{
			return TEXT("Self");
		}
		if (Ability.MinRange > 0.0f)
		{
			return FString::Printf(TEXT("%s–%s m"), *Num(Ability.MinRange), *Num(Ability.MaxRange));
		}
		if (Ability.MaxRange <= 2.0f)
		{
			return FString::Printf(TEXT("Melee (%s m)"), *Num(Ability.MaxRange));
		}
		return FString::Printf(TEXT("%s m"), *Num(Ability.MaxRange));
	}

	/** "One enemy", "Circle 2.5 m", "Line 7 m", "Cone 60°", "Around self 3 m". */
	FString CardTarget(const TMSim::FAbility& Ability)
	{
		const std::string Shape = TMSim::ShapeOf(Ability);
		const TCHAR* One = Ability.Target == TMSim::ETargetSide::Enemy ? TEXT("enemy")
			: Ability.Target == TMSim::ETargetSide::Ally ? TEXT("ally") : TEXT("fallen ally");
		const TCHAR* Many = Ability.Target == TMSim::ETargetSide::Enemy ? TEXT("enemies")
			: Ability.Target == TMSim::ETargetSide::Ally ? TEXT("allies") : TEXT("fallen allies");
		if (Ability.LaysZone())
		{
			return Ability.Aoe > 0.0f ? FString::Printf(TEXT("Ground, circle %s m"), *Num(Ability.Aoe)) : FString(TEXT("Ground"));
		}
		if (Shape == "unit")
		{
			return FString::Printf(TEXT("One %s"), One);
		}
		if (Shape == "circle")
		{
			return FString::Printf(TEXT("Circle %s m, %s"), *Num(Ability.Aoe), Many);
		}
		if (Shape == "line")
		{
			return FString::Printf(TEXT("Line %s m, %s"), *Num(Ability.MaxRange), Many);
		}
		if (Shape == "cone")
		{
			return FString::Printf(TEXT("Cone %s°, %s"), *Num(Ability.Angle), Many);
		}
		if (Shape == "self")
		{
			return Ability.Aoe > 0.0f ? FString::Printf(TEXT("Around self %s m, %s"), *Num(Ability.Aoe), Many) : FString(TEXT("Self"));
		}
		if (Shape == "global")
		{
			return FString::Printf(TEXT("All %s"), Many);
		}
		if (Shape == "vector")
		{
			return TEXT("A direction");
		}
		return TEXT("A spot");
	}

	struct FCardPill
	{
		FString Name;
		FString Turns;
		FLinearColor Colour;
		FString Desc;
	};

	/** Its statuses, buffs, ground and turn gauge change, as pills. */
	TArray<FCardPill> CardPills(const TMSim::FAbility& Ability)
	{
		TArray<FCardPill> Out;
		auto Status = [&Out, &Ability](const std::string& Id, int32 Count, const TCHAR* Prefix)
		{
			if (Id.empty())
			{
				return;
			}
			const TMSim::FStatusDef* Def = TMSim::FindStatus(Id);
			FString Name = Def && *Def->Name ? FString(UTF8_TO_TCHAR(Def->Name)) : FString(UTF8_TO_TCHAR(Id.c_str())).Replace(TEXT("_"), TEXT(" "));
			Name = FString(Prefix) + Name.Left(1).ToUpper() + Name.Mid(1);
			const bool bHarm = Def ? Def->bHarmful : Ability.Target == TMSim::ETargetSide::Enemy;
			Out.Add({ Name, Count > 0 ? CardTurns(Count) : FString(), bHarm ? CardBad : CardGood, StatusLook(Id).Desc });
		};
		Status(Ability.StatusId, Ability.StatusTurns, TEXT(""));
		Status(Ability.ZoneStatus2, Ability.ZoneStatus2Turns, TEXT(""));
		Status(Ability.SelfStatusId, Ability.SelfStatusTurns, TEXT("Self: "));
		for (const TMSim::FBuff& Buff : Ability.Buffs)
		{
			Out.Add({ FString::Printf(TEXT("%s %+d"), ShownStatName(Buff.Stat), Buff.Amount), CardTurns(Buff.Turns),
				Buff.Amount < 0 ? CardBad : CardGood, FString() });
		}
		if (Ability.TgChange != 0)
		{
			Out.Add({ FString::Printf(TEXT("Turn gauge %+d%%"), Ability.TgChange), FString(), Ability.TgChange < 0 ? CardBad : CardGood,
				Ability.TgChange < 0 ? FString(TEXT("Pushes its next turn back.")) : FString(TEXT("Brings its next turn closer.")) });
		}
		if (Ability.LaysZone())
		{
			Out.Add({ TEXT("Ground"), CardTurns(Ability.ZoneTurns), CardReach,
				Ability.ZonePercent > 0.0f ? FString::Printf(TEXT("Who starts or ends a walk on it loses %s%% of max HP, once a turn."), *Num(Ability.ZonePercent))
				: FString(TEXT("Who starts or ends a walk on it is touched by it, once a turn.")) });
		}
		return Out;
	}

	/** The first sentence of a description. */
	FString FirstSentence(const FString& Whole)
	{
		for (int32 i = 0; i + 1 < Whole.Len(); ++i)
		{
			if ((Whole[i] == TCHAR('.') || Whole[i] == TCHAR('!')) && Whole[i + 1] == TCHAR(' '))
			{
				return Whole.Left(i + 1);
			}
		}
		return Whole;
	}
}

bool ATMBattleHud::AltHeld() const
{
	return PlayerOwner && (PlayerOwner->IsInputKeyDown(EKeys::LeftAlt) || PlayerOwner->IsInputKeyDown(EKeys::RightAlt));
}

void ATMBattleHud::AddAbilityTip(float X, float Y, float W, float H, const TMSim::FUnit& Unit, int32 Slot)
{
	const TMSim::FAbility* Ability = Unit.Ability(Slot);
	FTMHudTip Entry;
	Entry.Area = FBox2D(FVector2D(X, Y), FVector2D(X + W, Y + H));
	Entry.Text = Ability ? FString(UTF8_TO_TCHAR(Ability->Name.c_str())) : FString(TEXT(" "));
	Entry.CardUnit = Unit.Id;
	Entry.CardSlot = Slot;
	Tips.Add(Entry);
}

FVector2D ATMBattleHud::AbilityCard(ATMBattleDirector& From, const TMSim::FUnit& Unit, int32 Slot, float X, float Y, bool bDetail, float DrawH, const FString& Note)
{
	const TMSim::FAbility* Ability = Unit.Ability(Slot);
	if (!Ability)
	{
		return FVector2D::ZeroVector;
	}
	const bool bDraw = DrawH > 0.0f;
	const TMSim::FBattle& Battle = From.Battle;
	UFont* Font = GEngine->GetMediumFont();
	const float W = (bDetail ? 470.0f : 400.0f) * S;
	const float Pad = 12.0f * S;
	const float Inner = W - 2.0f * Pad;
	const bool bPassive = Ability->Kind == "passive" || Ability->Kind == "aura";
	if (bDraw)
	{
		Panel(X, Y, W, DrawH, FLinearColor(0.03f, 0.04f, 0.07f, 0.96f), FLinearColor(1.0f, 1.0f, 1.0f, 0.22f), 1.0f);
	}
	float CY = Y + Pad;

	// The damage and its type: magic blue, physical orange, healing green.
	FString Damage;
	FString DamageKind;
	FLinearColor DamageColour = CardMagic;
	switch (Ability->Effect)
	{
	case TMSim::EEffect::Damage:
	{
		FLinearColor Element;
		const FString Type = DamageType(Unit.Job, Slot, *Ability, Element);
		const bool bPhysical = Ability->Scale == TMSim::EScale::Att;
		DamageColour = bPhysical ? CardPhysical : CardMagic;
		// An element the creator gave ("36 fire") stands in for the plain word.
		const bool bElement = Type.ToLower() != TEXT("physical") && Type.ToLower() != TEXT("magic");
		const FString Word = bElement ? Type.ToLower() : FString(bPhysical ? TEXT("physical") : TEXT("magic"));
		Damage = FString::Printf(TEXT("%d %s"), TMSim::RoundToInt(Ability->Power), *Word);
		DamageKind = Battle.NewDefense() ? FString(bPhysical ? TEXT("Physical (vs Armor)") : TEXT("Magic (vs Resist)"))
			: FString(bPhysical ? TEXT("Physical (vs AttDef)") : TEXT("Magic (vs MagDef)"));
		if (bElement)
		{
			DamageKind = Type.Left(1).ToUpper() + Type.Mid(1).ToLower() + TEXT(", ") + DamageKind.Left(1).ToLower() + DamageKind.Mid(1);
		}
		break;
	}
	case TMSim::EEffect::Heal:
		DamageColour = CardHeal;
		Damage = FString::Printf(TEXT("Heals %d"), TMSim::RoundToInt(Ability->Power * TMSim::Combat::HealScale * Battle.Tuning.HealMultiplier));
		DamageKind = TEXT("Healing");
		break;
	case TMSim::EEffect::Revive:
		DamageColour = CardHeal;
		Damage = FString::Printf(TEXT("Revives at %d%%"), TMSim::RoundToInt(Ability->Power * 100.0));
		DamageKind = TEXT("Healing");
		break;
	default:
		DamageKind = TEXT("Support");
		break;
	}
	const FString Cooldown = Slot == 3 ? (Unit.Ult >= TMSim::Pace::UltMax ? FString(TEXT("Ultimate: ready")) : FString::Printf(TEXT("Ultimate: gauge %d%%"), Unit.Ult))
		: Ability->Cooldown <= 0 ? FString(TEXT("None"))
		: Slot < TMSim::AbilitySlots && Unit.Cooldowns[Slot] > 0 ? FString::Printf(TEXT("%s (%d left)"), *CardTurns(Ability->Cooldown), Unit.Cooldowns[Slot])
		: CardTurns(Ability->Cooldown);
	const FString Cast = Ability->Cast > 0.0f ? FString::Printf(TEXT("%.1fs"), Battle.CastTicks(*Ability) / Tps) : FString(TEXT("Instant"));
	const FString Range = CardRange(*Ability);
	const FString Target = CardTarget(*Ability);
	const TArray<FCardPill> Pills = CardPills(*Ability);
	const FString Desc = UTF8_TO_TCHAR(Ability->Desc.c_str());
	const std::string Blocked = Battle.AbilityBlockedReason(Unit, Slot);

	// Its name, what it is, and a note (aiming) on the right.
	{
		const float NameScale = 0.62f * S;
		if (bDraw)
		{
			Text(UTF8_TO_TCHAR(Ability->Name.c_str()), X + Pad, CY, TextColour, Font, NameScale);
			if (!Note.IsEmpty())
			{
				const FVector2D NoteSize = TextSize(Note, Font, 0.4f * S);
				Text(Note, X + W - Pad - NoteSize.X, CY + 4.0f * S, Gold, Font, 0.4f * S);
			}
		}
		CY += TextSize(TEXT("Ag"), Font, NameScale).Y;
		FString ClassName;
		const FLinearColor ClassTint = AbilityClassColour(*Ability, &ClassName);
		FString What = ClassName;
		What += Slot == 3 ? TEXT("  ·  Ultimate") : Slot >= TMSim::ClassSlots ? TEXT("  ·  Item") : TEXT("");
		if (Ability->Kind != "active")
		{
			FString Kind = UTF8_TO_TCHAR(Ability->Kind.c_str());
			Kind.ReplaceInline(TEXT("_"), TEXT(" "));
			What += TEXT("  ·  ") + Kind.Left(1).ToUpper() + Kind.Mid(1);
		}
		if (bDraw)
		{
			Text(What, X + Pad, CY, ClassTint * FLinearColor(1.0f, 1.0f, 1.0f, 0.85f), Font, 0.4f * S);
		}
		CY += TextSize(TEXT("Ag"), Font, 0.4f * S).Y + 8.0f * S;
	}

	// A small stroke icon for each chip, drawn in a 24-unit square.
	enum class EChip : uint8 { Damage, Range, Cooldown, Target, Cast };
	auto Glyph = [&](EChip Kind, float GX, float GY, float Box, const FLinearColor& Colour)
	{
		const float U = Box / 24.0f;
		const float Thick = FMath::Max(1.0f, 1.8f * U);
		auto Seg = [&](float AX, float AY, float BX, float BY) { DrawLine(GX + AX * U, GY + AY * U, GX + BX * U, GY + BY * U, Colour, Thick); };
		auto Ring = [&](float CX, float CYY, float R)
		{
			constexpr int32 Steps = 12;
			for (int32 k = 0; k < Steps; ++k)
			{
				const float A0 = 2.0f * PI * k / Steps, A1 = 2.0f * PI * (k + 1) / Steps;
				Seg(CX + FMath::Cos(A0) * R, CYY + FMath::Sin(A0) * R, CX + FMath::Cos(A1) * R, CYY + FMath::Sin(A1) * R);
			}
		};
		switch (Kind)
		{
		case EChip::Damage:  // a blade
			Seg(4.0f, 20.0f, 19.0f, 5.0f); Seg(19.0f, 5.0f, 20.0f, 4.0f); Seg(6.0f, 13.0f, 11.0f, 18.0f); Seg(3.0f, 21.0f, 6.0f, 18.0f);
			break;
		case EChip::Range:  // a target
			Ring(12.0f, 12.0f, 9.0f); Ring(12.0f, 12.0f, 4.0f);
			break;
		case EChip::Cooldown:  // an hourglass
			Seg(6.0f, 3.0f, 18.0f, 3.0f); Seg(6.0f, 21.0f, 18.0f, 21.0f); Seg(7.0f, 3.0f, 17.0f, 21.0f); Seg(17.0f, 3.0f, 7.0f, 21.0f);
			break;
		case EChip::Target:  // a crosshair
			Ring(12.0f, 12.0f, 7.0f); Seg(12.0f, 1.0f, 12.0f, 7.0f); Seg(12.0f, 17.0f, 12.0f, 23.0f); Seg(1.0f, 12.0f, 7.0f, 12.0f); Seg(17.0f, 12.0f, 23.0f, 12.0f);
			break;
		case EChip::Cast:  // a clock
			Ring(12.0f, 12.0f, 9.0f); Seg(12.0f, 12.0f, 12.0f, 6.0f); Seg(12.0f, 12.0f, 16.0f, 14.0f);
			break;
		}
	};

	const float Small = 0.44f * S;
	const float LineH = TextSize(TEXT("Ag"), Font, Small).Y;
	const float WordScale = 0.46f * S;
	const float WordH = TextSize(TEXT("Ag"), Font, WordScale).Y;

	// Pills laid left to right, wrapping: a status's name in its colour, then its turns.
	auto PillSize = [&](const FCardPill& Pill)
	{
		const float NameW = TextSize(Pill.Name, Font, Small).X;
		const float TurnsW = Pill.Turns.IsEmpty() ? 0.0f : TextSize(Pill.Turns, Font, Small).X + 8.0f * S;
		return FVector2D(NameW + TurnsW + 16.0f * S, LineH + 6.0f * S);
	};
	auto DrawPill = [&](const FCardPill& Pill, float PX, float PY)
	{
		const FVector2D Size = PillSize(Pill);
		Panel(PX, PY, Size.X, Size.Y, Pill.Colour * FLinearColor(1.0f, 1.0f, 1.0f, 0.16f), Pill.Colour * FLinearColor(1.0f, 1.0f, 1.0f, 0.7f), 1.0f);
		Text(Pill.Name, PX + 8.0f * S, PY + 3.0f * S, Pill.Colour, Font, Small);
		if (!Pill.Turns.IsEmpty())
		{
			Text(Pill.Turns, PX + 8.0f * S + TextSize(Pill.Name, Font, Small).X + 8.0f * S, PY + 3.0f * S, TextColour, Font, Small);
		}
	};

	if (!bDetail)
	{
		// A's chips: the numbers that decide most turns, each with its icon.
		struct FChip { EChip Kind; FString Words; FLinearColor Colour; };
		TArray<FChip> Chips;
		if (bPassive)
		{
			Chips.Add({ EChip::Target, Ability->Kind == "aura" ? FString(TEXT("Aura")) : FString(TEXT("Passive")), Dim });
		}
		else
		{
			if (!Damage.IsEmpty())
			{
				Chips.Add({ EChip::Damage, Damage, DamageColour });
			}
			Chips.Add({ EChip::Range, Range, CardReach });
			Chips.Add({ EChip::Target, Target, TextColour });
			Chips.Add({ EChip::Cooldown, Slot == 3 ? Cooldown : Ability->Cooldown > 0 ? Cooldown : FString(TEXT("No cooldown")), Slot == 3 ? Gold : TextColour });
			Chips.Add({ EChip::Cast, Ability->Cast > 0.0f ? Cast + TEXT(" cast") : Cast, Ability->Cast > 0.0f ? CastColour : Dim });
		}
		const float ChipH = LineH + 6.0f * S;
		const float Icon = 13.0f * S;
		float CX = X + Pad;
		for (const FChip& Chip : Chips)
		{
			const float ChipW = Icon + 5.0f * S + TextSize(Chip.Words, Font, Small).X + 14.0f * S;
			if (CX + ChipW > X + Pad + Inner && CX > X + Pad)
			{
				CX = X + Pad;
				CY += ChipH + 5.0f * S;
			}
			if (bDraw)
			{
				Panel(CX, CY, ChipW, ChipH, FLinearColor(1.0f, 1.0f, 1.0f, 0.05f), FLinearColor(1.0f, 1.0f, 1.0f, 0.14f), 1.0f);
				Glyph(Chip.Kind, CX + 7.0f * S, CY + (ChipH - Icon) * 0.5f, Icon, Chip.Colour);
				Text(Chip.Words, CX + 7.0f * S + Icon + 5.0f * S, CY + 3.0f * S, Chip.Colour, Font, Small);
			}
			CX += ChipW + 5.0f * S;
		}
		CY += ChipH + 8.0f * S;

		if (Pills.Num() > 0)
		{
			float PX = X + Pad;
			for (const FCardPill& Pill : Pills)
			{
				const FVector2D Size = PillSize(Pill);
				if (PX + Size.X > X + Pad + Inner && PX > X + Pad)
				{
					PX = X + Pad;
					CY += Size.Y + 5.0f * S;
				}
				if (bDraw)
				{
					DrawPill(Pill, PX, CY);
				}
				PX += Size.X + 6.0f * S;
			}
			CY += LineH + 6.0f * S + 8.0f * S;
		}

		// What it is for, in a sentence (two lines at most).
		if (!Desc.IsEmpty())
		{
			TArray<FString> Lines = Wrap(FirstSentence(Desc), Font, WordScale, Inner);
			if (Lines.Num() > 2)
			{
				Lines.SetNum(2);
				Lines[1] += TEXT("…");
			}
			for (const FString& Line : Lines)
			{
				if (bDraw)
				{
					Text(Line, X + Pad, CY, TextColour, Font, WordScale);
				}
				CY += WordH;
			}
			CY += 4.0f * S;
		}
	}
	else
	{
		// D's long card: every field on its own labelled row, in the one order.
		const float LabelW = 112.0f * S;
		auto Row = [&](const TCHAR* Label, const FString& Value, const FLinearColor& Colour)
		{
			const TArray<FString> Lines = Wrap(Value, Font, WordScale, Inner - LabelW);
			if (bDraw)
			{
				Text(FString(Label).ToUpper(), X + Pad, CY + 2.0f * S, Dim, Font, 0.36f * S);
				for (int32 i = 0; i < Lines.Num(); ++i)
				{
					Text(Lines[i], X + Pad + LabelW, CY + i * WordH, Colour, Font, WordScale);
				}
			}
			CY += FMath::Max(1, Lines.Num()) * WordH + 3.0f * S;
		};
		if (bPassive)
		{
			Row(TEXT("Kind"), Ability->Kind == "aura" ? FString(TEXT("Aura: always on")) : FString(TEXT("Passive: always on")), Dim);
		}
		else
		{
			Row(TEXT("Damage"), Damage.IsEmpty() ? FString(TEXT("—")) : Damage, Damage.IsEmpty() ? Dim : DamageColour);
			Row(TEXT("Damage type"), DamageKind, Damage.IsEmpty() ? Dim : DamageColour);
			Row(TEXT("Cooldown"), Cooldown, Slot == 3 ? Gold : TextColour);
			Row(TEXT("Range"), Range, CardReach);
			Row(TEXT("Target"), Target, TextColour);
			Row(TEXT("Cast"), Ability->Cast > 0.0f ? Cast + TEXT(" (a blow can break it)") : Cast, Ability->Cast > 0.0f ? CastColour : Dim);
		}
		if (Pills.Num() > 0)
		{
			CY += 6.0f * S;
			if (bDraw)
			{
				DrawRect(FLinearColor(1.0f, 1.0f, 1.0f, 0.1f), X + Pad, CY, Inner, 1.0f);
				Text(TEXT("STATUS"), X + Pad, CY + 5.0f * S, Dim, Font, 0.36f * S);
			}
			CY += 5.0f * S + TextSize(TEXT("Ag"), Font, 0.36f * S).Y + 4.0f * S;
			for (const FCardPill& Pill : Pills)
			{
				if (bDraw)
				{
					DrawPill(Pill, X + Pad, CY);
				}
				CY += PillSize(Pill).Y + 3.0f * S;
				if (!Pill.Desc.IsEmpty())
				{
					for (const FString& Line : Wrap(Pill.Desc, Font, 0.42f * S, Inner - 10.0f * S))
					{
						if (bDraw)
						{
							Text(Line, X + Pad + 10.0f * S, CY, TextColour * FLinearColor(1.0f, 1.0f, 1.0f, 0.8f), Font, 0.42f * S);
						}
						CY += TextSize(TEXT("Ag"), Font, 0.42f * S).Y;
					}
				}
				CY += 5.0f * S;
			}
		}
		if (!Desc.IsEmpty())
		{
			CY += 4.0f * S;
			if (bDraw)
			{
				DrawRect(FLinearColor(1.0f, 1.0f, 1.0f, 0.1f), X + Pad, CY, Inner, 1.0f);
			}
			CY += 7.0f * S;
			for (const FString& Line : Wrap(Desc, Font, WordScale, Inner))
			{
				if (bDraw)
				{
					Text(Line, X + Pad, CY, TextColour, Font, WordScale);
				}
				CY += WordH;
			}
			CY += 4.0f * S;
		}
	}

	// Why it can't be used now, in red.
	if (!Blocked.empty() && !bPassive)
	{
		for (const FString& Line : Wrap(FString::Printf(TEXT("Can't now: %hs"), Blocked.c_str()), Font, 0.42f * S, Inner))
		{
			if (bDraw)
			{
				Text(Line, X + Pad, CY, Urgent, Font, 0.42f * S);
			}
			CY += TextSize(TEXT("Ag"), Font, 0.42f * S).Y;
		}
		CY += 4.0f * S;
	}
	if (!bDetail)
	{
		const FString Hint = TEXT("Hold Alt for details");
		if (bDraw)
		{
			Text(Hint, X + Pad, CY, Dim, Font, 0.36f * S);
		}
		CY += TextSize(Hint, Font, 0.36f * S).Y;
	}
	return FVector2D(W, CY - Y + Pad);
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
	const float Lane = PinLane * S;
	HoverLink = -1;
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
		// Each bar keeps a lane for its cooldown pins: above the blue bar, below the red.
		const float RowY = Y0 + Lane + Team * RowHeight * S;
		const float LineY = RowY + Chip * 0.5f + 2.0f * S;
		Panel(X0 - 6.0f * S, RowY - 3.0f * S - (Team == 0 ? Lane : 0.0f), Width + 12.0f * S, (RowHeight - 2.0f) * S + Lane, PanelFill);
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

		// Cooldown pins (2026-10-02, "Cooldown Ghost Chip Mockups" B): each ability
		// cooling down, as its tile on a stem, at the time of its owner's turn on
		// which it is back -- above the blue bar, below the red. The ability being
		// aimed shows too, beating gold, where it would come back if used now.
		{
			struct FPin
			{
				const TMSim::FUnit* Unit = nullptr;
				FTMBack Back;
				float X = 0.0f;
				float Seconds = 0.0f;
			};
			std::vector<FPin> Pins;
			for (const TMSim::FUnit& Unit : Battle.Units)
			{
				if (Unit.Team != Team || !Unit.IsAlive() || !From.IsSeen(Unit))
				{
					continue;
				}
				for (const FTMBack& Back : ComingBack(From, Unit))
				{
					FPin Pin;
					Pin.Unit = &Unit;
					Pin.Back = Back;
					Pin.Seconds = TurnInSeconds(Battle, Unit, Back.Turn);
					Pin.X = BarX(Pin.Seconds);
					Pins.push_back(Pin);
				}
			}
			std::sort(Pins.begin(), Pins.end(), [](const FPin& A, const FPin& B) { return A.X < B.X; });
			const float PinSize = 18.0f * S;
			const float PinY = Team == 0 ? RowY - Lane + 4.0f * S : RowY + Chip + 11.0f * S;
			const float PinBeat = 0.55f + 0.45f * FMath::Sin((GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0f) * 6.0f);
			float Last = -1.0e6f;
			for (const FPin& Pin : Pins)
			{
				const TMSim::FAbility* Ability = Pin.Unit->Ability(Pin.Back.Slot);
				if (!Ability)
				{
					continue;
				}
				// Side by side where two would overlap, the later nudged right.
				const float Left = FMath::Min(FMath::Max(Pin.X - PinSize * 0.5f, Last + 2.0f * S), End - PinSize);
				Last = Left + PinSize;
				const bool bLit = TurnLinked(From, Pin.Unit->Id);
				const FLinearColor Stem = Pin.Back.bPreview ? Gold : TeamColour(Team) * FLinearColor(1, 1, 1, 0.6f);
				const float StemX = Left + PinSize * 0.5f - 0.5f * S;
				if (Team == 0)
				{
					DrawRect(Stem, StemX, PinY + PinSize, 1.0f * S, LineY - PinY - PinSize);
				}
				else
				{
					DrawRect(Stem, StemX, LineY, 1.0f * S, PinY - LineY);
				}
				FLinearColor PinEdge = bLit ? Gold : TeamColour(Team) * FLinearColor(1, 1, 1, 0.85f);
				if (Pin.Back.bPreview)
				{
					PinEdge = FLinearColor(Gold.R, Gold.G, Gold.B, PinBeat);
				}
				Panel(Left - 1.0f * S, PinY - 1.0f * S, PinSize + 2.0f * S, PinSize + 2.0f * S, FLinearColor(0.02f, 0.03f, 0.05f, 0.95f),
					PinEdge, bLit || Pin.Back.bPreview ? 2.0f : 1.0f);
				if (UTexture2D* Tile = AbilityIcon(*Ability, false))
				{
					Picture(Tile, Left, PinY, PinSize, PinSize,
						Pin.Back.bPreview ? FLinearColor(1.0f, 1.0f, 1.0f, 0.5f + 0.5f * PinBeat) : FLinearColor::White);
				}
				if (FBox2D(FVector2D(Left, PinY), FVector2D(Left + PinSize, PinY + PinSize)).IsInside(Mouse))
				{
					HoverLink = Pin.Unit->Id;
				}
				AddTip(Left, PinY, PinSize, PinSize, BackLine(JobName(*Pin.Unit), *Ability, Pin.Back.bPreview, Pin.Back.Turn, Pin.Seconds));
			}
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
			// Pointed at here, on its pins or on the board (2026-10-02): gold, with a glow round it.
			if (!bFogged && TurnLinked(From, Unit.Id))
			{
				Edge = Gold;
				Thick = 3.0f;
				DrawRect(FLinearColor(Gold.R, Gold.G, Gold.B, 0.22f), Place.X - 4.0f * S, Place.Y - 4.0f * S, Size + 8.0f * S, Size + 8.0f * S);
			}
			if (bOver)
			{
				HoverLink = Unit.Id;
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
			if (Unit.Id == NextOwnUnit(From))
			{
				const float Beat = 0.55f + 0.45f * FMath::Sin(static_cast<float>(FPlatformTime::Seconds()) * 5.0f);
				Panel(Place.X - 3.0f * S, Place.Y - 3.0f * S, Size + 6.0f * S, Size + 6.0f * S, FLinearColor::Transparent, Gold * FLinearColor(1, 1, 1, Beat), 2.5f * S);
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

	// A monster's wind-up ("Camps and Bosses Mockups" B): a purple ghost chip on
	// the line between the two bars, where it lands.
	for (const TMSim::FUnit& Caster : Battle.Units)
	{
		if (!WindingUp(Caster) || !From.IsSeen(Caster))
		{
			continue;
		}
		const TMSim::FAbility* Winding = Caster.Ability(Caster.Casting.Slot);
		const float Lands = Caster.Casting.Ticks / Tps;
		const float Ghost = Chip * 0.8f;
		const float GX = FMath::Clamp(BarX(Lands) - Ghost * 0.5f, X0 + Track, End - Ghost);
		const float GY = Y0 + Lane + RowHeight * S - 2.5f * S - Ghost * 0.5f;
		const bool bLit = TurnLinked(From, Caster.Id);
		DrawRect(FLinearColor(0.14f, 0.07f, 0.26f, 0.9f), GX, GY, Ghost, Ghost);
		DashedEdge([this](const FLinearColor& C, float RX, float RY, float RW, float RH) { DrawRect(C, RX, RY, RW, RH); },
			GX, GY, Ghost, Ghost, bLit ? Gold : CastColour, 2.0f * S, 4.0f * S, 3.0f * S);
		const FString Mark = Initials(JobName(Caster));
		const FVector2D MarkSize = TextSize(Mark, Font, 0.42f * S);
		Text(Mark, GX + (Ghost - MarkSize.X) * 0.5f, GY + (Ghost - MarkSize.Y) * 0.5f, FLinearColor(0.9f, 0.82f, 1.0f), Font, 0.42f * S);
		if (FBox2D(FVector2D(GX, GY), FVector2D(GX + Ghost, GY + Ghost)).IsInside(Mouse))
		{
			HoverLink = Caster.Id;
		}
		AddTip(GX, GY, Ghost, Ghost, FString::Printf(TEXT("%s winds up %hs: it lands in %.1f s\nHits from behind break a boss's wind-up."),
			*JobName(Caster), Winding ? Winding->Name.c_str() : "something", Lands));
	}

	Movable(TEXT("turn_order"), TEXT("Turn order bars"), X0 - 6.0f * S, Y0 - 3.0f * S, Width + 12.0f * S, 2.0f * RowHeight * S + 2.0f * Lane);
	From.HudHoverUnitId = HoverLink;

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
	// Pointing at a square lights its unit up on the board, and the reverse; over
	// its portrait, its coming turns drop down under the row (2026-10-02).
	const FVector2D Mouse = MousePoint();
	HoverLink = -1;
	const TMSim::FUnit* DropFor = nullptr;
	float DropX = 0.0f;
	float DropTop = 0.0f;
	bool bDropLeft = false;
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
			if (!bFogged && TurnLinked(From, Unit->Id))
			{
				Frame = Gold;
				FrameWidth = 3.5f * S;
				DrawRect(FLinearColor(Gold.R, Gold.G, Gold.B, 0.22f), X - 4.0f * S, RowY - 4.0f * S, SW + 8.0f * S, SH + 8.0f * S);
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
				// A dot per ability cooling down ("Cooldown Ghost Chip Mockups" C), gold
				// and beating for the one being aimed.
				const TArray<FTMBack> Backs = ComingBack(From, *Unit);
				const float Dot = 7.0f * S;
				for (int32 i = 0; i < Backs.Num(); ++i)
				{
					const float DX = X + SW - FrameWidth - 2.0f * S - (i + 1) * (Dot + 2.0f * S) + 2.0f * S;
					const float DY = RowY + SW - FrameWidth - 3.0f * S - Dot;
					DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.85f), DX - 1.0f * S, DY - 1.0f * S, Dot + 2.0f * S, Dot + 2.0f * S);
					DrawRect(Backs[i].bPreview ? FLinearColor(Gold.R, Gold.G, Gold.B, Pulse) : FLinearColor(0.79f, 0.83f, 0.9f, 1.0f), DX, DY, Dot, Dot);
				}
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
			// Health under the card, green to red as it falls ("Squad Strip Mockups"
			// B; the frame already says whose it is).
			const float BarY = RowY + SW + 2.0f * S;
			const float HpPart = bFogged ? 0.0f : static_cast<float>(Unit->Hp) / FMath::Max(1, Unit->MaxHp());
			DrawRect(FLinearColor(0.03f, 0.04f, 0.06f, 0.95f * Alpha), X, BarY, SW, 5.0f * S);
			DrawRect(HealthColour(HpPart) * FLinearColor(1, 1, 1, Alpha), X, BarY, SW * HpPart, 5.0f * S);
			// Its statuses under it, both sides, each with its turns left: three at
			// most, the third "+N" when there are more.
			if (!bFogged)
			{
				StatusChips(*Unit, X, RowY + SH + 3.0f * S, 18.0f * S, false, true, 3);
			}
			const float GaugeY = BarY + 1.0f * S;
			const float BadgeScale = 0.4f * S;
			const FVector2D BadgeSize = TextSize(Badge, Font, BadgeScale);
			Text(Badge, X + (SW - BadgeSize.X) * 0.5f, GaugeY + 5.0f * S, BadgeColour * FLinearColor(1, 1, 1, Alpha), Font, BadgeScale);
			// None of yours ready: your next one glows (v20 play test: instead of the banner).
			if (Unit->Id == NextOwnUnit(From))
			{
				const float Beat = 0.55f + 0.45f * FMath::Sin(static_cast<float>(FPlatformTime::Seconds()) * 5.0f);
				Panel(X - 3.0f * S, RowY - 3.0f * S, SW + 6.0f * S, SH + 6.0f * S, FLinearColor::Transparent, Gold * FLinearColor(1, 1, 1, Beat), 2.5f * S);
			}
			AddButton(X, RowY, SW, SH, ETMHudAction::PickUnit, Unit->Id);
			SquareAreas.Add(Unit->Id, FBox2D(FVector2D(X, RowY), FVector2D(X + SW, RowY + SH)));
			if (FBox2D(FVector2D(X, RowY), FVector2D(X + SW, RowY + SH)).IsInside(Mouse))
			{
				HoverLink = Unit->Id;
				if (!bFogged && Mouse.Y < RowY + SW)
				{
					DropFor = Unit;
					DropX = bLeft ? X + SW : X;
					DropTop = RowY + SH + SquareLane * S + 6.0f * S;
					bDropLeft = bLeft;
				}
			}
			FString Tip;
			if (bFogged)
			{
				Tip = TEXT("Hidden by the fog of war");
			}
			else
			{
				Tip = FString::Printf(TEXT("%s %s %d  hp %d/%d"), Team == 0 ? TEXT("Blue") : TEXT("Red"),
					*JobName(*Unit), Unit->Id, Unit->Hp, Unit->MaxHp());
				for (const TMSim::FStatus& Status : Unit->Statuses)
				{
					const TMSim::FStatusDef* Def = TMSim::FindStatus(Status.Id);
					Tip += FString::Printf(TEXT("\n%s: %d %s"), Def ? UTF8_TO_TCHAR(Def->Name) : UTF8_TO_TCHAR(Status.Id.c_str()), Status.Turns,
						Status.Id == "stop" ? TEXT("s") : (Status.Turns == 1 ? TEXT("turn left") : TEXT("turns left")));
				}
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
			// The portrait shows the coming turns now; the words are on the strip under it.
			AddTip(X, bFogged ? RowY : RowY + SW, SW, bFogged ? SH : SH - SW, Tip);
			X += SW + Gap;
		}
		if (Row.Num() > 0)
		{
			Movable(Team == 0 ? TEXT("turn_cards_blue") : TEXT("turn_cards_red"), Team == 0 ? TEXT("Blue squares") : TEXT("Red squares"),
				RowX - 3.0f * S, RowY - 3.0f * S, X - RowX - Gap + 6.0f * S, SH + 6.0f * S);
		}
	}
	// A monster's wind-up ("Camps and Bosses Mockups" B): a purple ghost square
	// between the two rows, with the seconds until it lands.
	{
		float CastX = Canvas->ClipX * 0.5f;
		int32 Shown = 0;
		for (const TMSim::FUnit& Caster : Battle.Units)
		{
			if (!WindingUp(Caster) || !From.IsSeen(Caster))
			{
				continue;
			}
			const TMSim::FAbility* Winding = Caster.Ability(Caster.Casting.Slot);
			const float Lands = Caster.Casting.Ticks / Tps;
			const float Ghost = 54.0f * S;
			const float GX = CastX - Ghost * 0.5f + Shown * (Ghost + 6.0f * S);
			const float GY = 12.0f * S + 98.0f * S;
			const bool bLit = TurnLinked(From, Caster.Id);
			DrawRect(FLinearColor(0.14f, 0.07f, 0.26f, 0.92f), GX, GY, Ghost, Ghost);
			DashedEdge([this](const FLinearColor& C, float RX, float RY, float RW, float RH) { DrawRect(C, RX, RY, RW, RH); },
				GX, GY, Ghost, Ghost, bLit ? Gold : CastColour, 2.0f * S, 5.0f * S, 4.0f * S);
			const FString Mark = Initials(JobName(Caster));
			const FVector2D MarkSize = TextSize(Mark, Font, 0.5f * S);
			Text(Mark, GX + (Ghost - MarkSize.X) * 0.5f, GY + 6.0f * S, FLinearColor(0.9f, 0.82f, 1.0f), Font, 0.5f * S);
			const FString Secs = FString::Printf(TEXT("%.1fs"), Lands);
			const FVector2D SecsSize = TextSize(Secs, Font, 0.42f * S);
			Text(Secs, GX + (Ghost - SecsSize.X) * 0.5f, GY + Ghost - SecsSize.Y - 4.0f * S, CastColour, Font, 0.42f * S);
			if (FBox2D(FVector2D(GX, GY), FVector2D(GX + Ghost, GY + Ghost)).IsInside(Mouse))
			{
				HoverLink = Caster.Id;
			}
			AddTip(GX, GY, Ghost, Ghost, FString::Printf(TEXT("%s winds up %hs: it lands in %.1f s\nHits from behind break a boss's wind-up."),
				*JobName(Caster), Winding ? Winding->Name.c_str() : "something", Lands));
			++Shown;
		}
	}
	if (DropFor)
	{
		DrawComingTurns(From, *DropFor, DropX, DropTop, bDropLeft);
	}
	From.HudHoverUnitId = HoverLink;
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
	// Its few newest lines, until the pointer is on it or it is scrolled back (v20 play test, less text).
	const int32 Shown = From.bLogLarge ? 26 : (bLogHovered || From.LogScroll > 0) ? 10 : 4;
	const float X = 16.0f * S + Nudge(TEXT("log")).X;
	const float Y = 12.0f * S + (2.0f * RowHeight + (FTMSettings::Get().bTurnSquares ? SquareLane : 2.0f * PinLane)) * S + 18.0f * S + Nudge(TEXT("log")).Y;
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
	bLogHovered = LogArea.IsInside(MousePoint());
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

float ATMBattleHud::StatusChips(const TMSim::FUnit& Unit, float X, float Y, float Size, bool bLeftward, bool bTips, int32 Most)
{
	UFont* Font = GEngine->GetSmallFont();
	const float Gap = Size * 0.15f;
	float Used = 0.0f;
	const int32 Count = static_cast<int32>(Unit.Statuses.size());
	int32 Drawn = 0;
	for (const TMSim::FStatus& Status : Unit.Statuses)
	{
		// No room for them all: the last place says how many more (2026-10-02).
		if (Most > 0 && Count > Most && Drawn == Most - 1)
		{
			const float At = bLeftward ? X - Used - Size : X + Used;
			const FString More = FString::Printf(TEXT("+%d"), Count - Drawn);
			const FVector2D MoreSize = TextSize(More, Font, Size / 60.0f);
			DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.75f), At, Y, Size, Size);
			Text(More, At + (Size - MoreSize.X) * 0.5f, Y + (Size - MoreSize.Y) * 0.5f, FLinearColor::White, Font, Size / 60.0f, false);
			if (bTips)
			{
				FString Rest;
				for (int32 i = Drawn; i < Count; ++i)
				{
					const TMSim::FStatus& Other = Unit.Statuses[i];
					const TMSim::FStatusDef* OtherDef = TMSim::FindStatus(Other.Id);
					Rest += FString::Printf(TEXT("%s%s  (%d)"), Rest.IsEmpty() ? TEXT("") : TEXT("\n"),
						OtherDef ? UTF8_TO_TCHAR(OtherDef->Name) : UTF8_TO_TCHAR(Other.Id.c_str()), Other.Turns);
				}
				AddTip(At, Y, Size, Size, Rest);
			}
			Used += Size + Gap;
			break;
		}
		++Drawn;
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
		// Red when it ends at the start of the unit's next turn ("Squad Strip Mockups").
		const bool bEnding = !bLayers && Status.Id != "stop" && Status.Turns == 1;
		DrawRect(bEnding ? FLinearColor(0.7f, 0.12f, 0.1f, 0.95f) : FLinearColor(0.02f, 0.02f, 0.04f, 0.8f),
			At + Size - TurnSize.X - 3.0f, Y + Size - TurnSize.Y, TurnSize.X + 3.0f, TurnSize.Y);
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
			if (bEnding)
			{
				Tip += TEXT("\nEnds as its next turn begins.");
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
	// No number beside it (v20 play test, less text): the bar and its notches say it;
	// the pointer on the unit gives the number.
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
		// Only the one pointed at (2026-10-03).
		if (!Unit || !Unit->IsAlive() || !From.GoToShown(Pair.Key))
		{
			continue;
		}
		const float Strength = 1.0f;
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

	// "Queued orders" C (2026-10-06): an x where a shown queue ends -- the selected
	// unit's, or one pointed at -- that cancels the whole of it.
	auto CancelAt = [&](int32 UnitId, const TMSim::FVec2& Point)
	{
		const int Level = From.Battle.Map.NodeLevel(TMSim::FMap::NodeOf(Point));
		const FVector Local = From.WorldFromMetres(Point, Level) + FVector(0.0f, 0.0f, 6.0f);
		FVector2D At;
		if (!PlayerOwner->ProjectWorldLocationToScreen(From.GetActorTransform().TransformPosition(Local), At))
		{
			return;
		}
		const float Box = 20.0f * S;
		const float BX = FMath::RoundToFloat(At.X + 14.0f * S);
		const float BY = FMath::RoundToFloat(At.Y - 34.0f * S);
		const bool bOver = FBox2D(FVector2D(BX, BY), FVector2D(BX + Box, BY + Box)).IsInside(MousePoint());
		DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.55f), BX - 1.0f, BY - 1.0f, Box + 2.0f, Box + 2.0f);
		DrawRect(bOver ? FLinearColor(0.55f, 0.16f, 0.14f, 1.0f) : FLinearColor(0.23f, 0.11f, 0.11f, 0.95f), BX, BY, Box, Box);
		const FLinearColor Cross(1.0f, 0.8f, 0.76f, 1.0f);
		const float Inset = Box * 0.3f;
		DrawLine(BX + Inset, BY + Inset, BX + Box - Inset, BY + Box - Inset, Cross, 2.0f * S);
		DrawLine(BX + Box - Inset, BY + Inset, BX + Inset, BY + Box - Inset, Cross, 2.0f * S);
		AddButton(BX, BY, Box, Box, ETMHudAction::QueueCancel, UnitId);
		if (bOver)
		{
			const FString Say = TEXT("cancel the queue");
			const float SayScale = 0.42f * S;
			OutlinedText(Say, BX + Box + 6.0f * S, BY + (Box - TextSize(Say, Font, SayScale).Y) * 0.5f, FLinearColor(1.0f, 0.75f, 0.7f, 1.0f), Font, SayScale, 1.5f * S);
		}
	};
	for (const TMSim::FUnit& Each : From.Battle.Units)
	{
		if (!Each.IsAlive() || !From.PlayerCanPlan(&Each) || !(Each.Id == From.SelectedId || From.GoToShown(Each.Id)))
		{
			continue;
		}
		if (const ATMBattleDirector::FTMGoTo* Order = From.GoToOf(Each.Id))
		{
			CancelAt(Each.Id, Order->Stops.empty() ? Order->Dest : Order->Stops.back());
		}
		else if (const ATMBattleDirector::FTMPlan* Plan = From.PlanOf(Each.Id))
		{
			if (Plan->bWalk)
			{
				CancelAt(Each.Id, Plan->To);
			}
		}
	}
}

void ATMBattleHud::TurnPips(const TMSim::FUnit& Unit, float CX, float Top, float Zoom)
{
	// The move and act tokens, a quarter bigger since 2026-10-03.
	const float R = 9.375f * S * Zoom;
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
		const FVector World = From.GetActorTransform().TransformPosition(Shown + FVector(0.0f, 0.0f, Unit.IsAlive() ? ATMBattleDirector::UnitHeadCm + 82.0f : 70.0f));
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
		// A unit a blow is being aimed at shows its full bar too, with the blow on it.
		const FTMAimOdds* Aimed = AimOdds.Find(Unit.Id);
		const bool bHovered = Unit.Id == From.HoverUnitId || Aimed != nullptr;

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
			// Its statuses, small, beside its ring -- 2026-10-06 ("too busy during
			// fights"): only on the unit being ordered, and on any unit for a moment
			// after a status is put on it. The turn order and the squad strip still
			// show them all, and pointing at a unit shows its full read.
			FString Ids = TEXT(",");
			for (const TMSim::FStatus& Status : Unit.Statuses)
			{
				Ids += FString(UTF8_TO_TCHAR(Status.Id.c_str())) + TEXT(",");
			}
			const double Clock = FPlatformTime::Seconds();
			if (TPair<FString, double>* Seen = StatusSeen.Find(Unit.Id))
			{
				if (Seen->Key != Ids)
				{
					// Shown again only for a new one, not for one wearing off.
					for (const TMSim::FStatus& Status : Unit.Statuses)
					{
						if (!Seen->Key.Contains(TEXT(",") + FString(UTF8_TO_TCHAR(Status.Id.c_str())) + TEXT(",")))
						{
							Seen->Value = Clock + 1.5;
						}
					}
					Seen->Key = Ids;
				}
			}
			else
			{
				StatusSeen.Add(Unit.Id, TPair<FString, double>(Ids, 0.0));
			}
			const TPair<FString, double>* ShownUntil = StatusSeen.Find(Unit.Id);
			if (Unit.Id == From.SelectedId || (ShownUntil && Clock < ShownUntil->Value))
			{
				const float Small = 11.0f * S * FTMSettings::Get().StatusIconScale;
				StatusChips(Unit, Head.Foot.X + Head.Ring + 4.0f * S, Head.Foot.Y - Small * 0.5f, Small, false, false);
			}
			// One of ours unseen in tall grass (FBattle::Hidden): says so under its feet.
			if (bFriend && !Unit.bSpotted && From.Battle.Map.InGrass(Unit.Pos))
			{
				// An eye struck through (v20 play test: an icon, not words).
				const FLinearColor Leaf(0.7f, 1.0f, 0.6f, 0.95f);
				const float EW = 9.0f * S * O;
				const float EH = 4.5f * S * O;
				const FVector2D EyeAt(Head.Foot.X, Head.Foot.Y + 12.0f * S);
				for (int32 k = 0; k < 12; ++k)
				{
					const float A0 = UE_TWO_PI * k / 12.0f;
					const float A1 = UE_TWO_PI * (k + 1) / 12.0f;
					DrawLine(EyeAt.X + FMath::Cos(A0) * EW, EyeAt.Y + FMath::Sin(A0) * EH, EyeAt.X + FMath::Cos(A1) * EW, EyeAt.Y + FMath::Sin(A1) * EH, Leaf, 1.5f * S);
				}
				DrawRect(Leaf, EyeAt.X - 1.5f * S, EyeAt.Y - 1.5f * S, 3.0f * S, 3.0f * S);
				DrawLine(EyeAt.X - EW, EyeAt.Y + EH * 1.6f, EyeAt.X + EW, EyeAt.Y - EH * 1.6f, Leaf, 1.5f * S);
			}
			// A spell on its way out stays in sight: what it is and how long, over its head.
			float PopY = Head.At.Y;
			if (Unit.IsCasting())
			{
				PopY -= CastCard(Unit, Head.At.X, Head.At.Y + 6.0f * S * O, O) + 4.0f * S * O;
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
		if (Aimed)
		{
			// The blow, cut out of the bar ("Hit Preview Mockups" B): the part a hit takes
			// striped and pulsing, what a crit takes on top outlined in gold dashes, a
			// white tick where a graze leaves it. Shields soak first.
			auto After = [&](int32 Amount)
			{
				return FMath::Clamp(static_cast<float>(FMath::Min(Unit.Hp, Unit.Hp + Soak - Amount)) / MaxHp, 0.0f, 1.0f);
			};
			const float AfterHit = After(Aimed->HitAmount);
			const float AfterCrit = After(Aimed->CritAmount);
			const float AfterGraze = After(Aimed->GrazeAmount);
			const float Beat = 0.55f + 0.45f * FMath::Abs(FMath::Sin((GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0f) * 2.9f));
			const FLinearColor Cut(1.0f, 0.69f, 0.66f, 1.0f);
			if (Part > AfterHit)
			{
				const float CX = X + W * AfterHit;
				const float CW = W * (Part - AfterHit);
				DrawRect(FLinearColor(0.05f, 0.03f, 0.04f, 0.95f), CX, Y, CW, H);
				const float Stripe = 2.0f * S;
				for (float SX = CX; SX < CX + CW; SX += 2.0f * Stripe)
				{
					DrawRect(FLinearColor(0.88f, 0.34f, 0.3f, 0.8f * Beat), SX, Y, FMath::Min(Stripe, CX + CW - SX), H);
				}
				Panel(CX, Y - 1.0f * S, CW, H + 2.0f * S, FLinearColor::Transparent, Cut * FLinearColor(1.0f, 1.0f, 1.0f, Beat), 1.5f * S);
			}
			if (Aimed->Crit > 0.0f && AfterHit > AfterCrit)
			{
				const float DX = X + W * AfterCrit;
				const float DW = W * (AfterHit - AfterCrit);
				const FLinearColor Dash(0.94f, 0.76f, 0.29f, 1.0f);
				for (float SX = DX; SX < DX + DW; SX += 5.0f * S)
				{
					const float Len = FMath::Min(3.0f * S, DX + DW - SX);
					DrawRect(Dash, SX, Y - 2.0f * S, Len, 1.5f * S);
					DrawRect(Dash, SX, Y + H + 0.5f * S, Len, 1.5f * S);
				}
				DrawRect(Dash, DX, Y - 2.0f * S, 1.5f * S, H + 4.0f * S);
			}
			if (Aimed->Graze > 0.0f)
			{
				DrawRect(FLinearColor::White, X + W * AfterGraze - 1.0f * S, Y - 3.0f * S, 2.0f * S, H + 6.0f * S);
			}
			AddTip(X, Y - 3.0f * S, W, H + 6.0f * S, FString::Printf(
				TEXT("Health %d of %d%s\nStriped: what a hit takes (%d). Gold dashes: what a crit takes on top (%d in all).%s\nEvasion %d%%; critical %d%% of the blows that land."),
				Unit.Hp, MaxHp, Soak > 0 ? *FString::Printf(TEXT(" (shields soak %d first)"), Soak) : TEXT(""),
				Aimed->HitAmount, Aimed->CritAmount,
				Aimed->Graze > 0.0f ? *FString::Printf(TEXT("\nWhite tick: where a graze (%d) leaves it."), Aimed->GrazeAmount) : TEXT(""),
				Aimed->Evade, Aimed->CritChance));
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

		if (Aimed)
		{
			// One small row naming each mark on the bar, in its colour: chance, then damage.
			struct FKey
			{
				FLinearColor Colour;
				const TCHAR* Name;
				float Chance;
				int32 Amount;
			};
			const FKey Keys[4] = {
				{ FLinearColor(1.0f, 0.69f, 0.66f, 1.0f), TEXT("Hit"), Aimed->Hit, Aimed->HitAmount },
				{ FLinearColor(0.94f, 0.76f, 0.29f, 1.0f), TEXT("Crit"), Aimed->Crit, Aimed->CritAmount },
				{ FLinearColor::White, TEXT("Graze"), Aimed->Graze, Aimed->GrazeAmount },
				{ FLinearColor(0.55f, 0.58f, 0.64f, 1.0f), TEXT("Dodge"), Aimed->Dodge, 0 },
			};
			const float KeyScale = 0.27f * S * O;
			const float Swatch = 5.0f * S * O;
			const float Space = 7.0f * S * O;
			const FLinearColor Grey(0.75f, 0.78f, 0.84f, 1.0f);
			TArray<TPair<FString, FLinearColor>> Words;
			float RowW = 0.0f;
			float RowH = 0.0f;
			// Measured first, so the row sits centred on the bar on a dark band.
			for (const FKey& Key : Keys)
			{
				if (Key.Chance <= 0.0f)
				{
					continue;
				}
				const FString Said = Key.Amount > 0 ? FString::Printf(TEXT("%s %.0f%% -%d"), Key.Name, Key.Chance, Key.Amount)
					: FString::Printf(TEXT("%s %.0f%%"), Key.Name, Key.Chance);
				const FVector2D Size = TextSize(Said, Font, KeyScale);
				RowW += Swatch + 3.0f * S + Size.X + Space;
				RowH = FMath::Max(RowH, Size.Y);
				Words.Add(TPair<FString, FLinearColor>(Said, Key.Colour));
			}
			FString Tail;
			if (Aimed->Ko > 0.0f)
			{
				Tail = FString::Printf(TEXT("KO %.0f%%"), Aimed->Ko);
			}
			if (!AimOddsExtra.IsEmpty())
			{
				Tail += (Tail.IsEmpty() ? TEXT("") : TEXT("  ")) + AimOddsExtra;
			}
			const float TailW = Tail.IsEmpty() ? 0.0f : TextSize(Tail, Font, KeyScale).X + Space;
			RowW += TailW - Space;
			float KX = Head.At.X - RowW * 0.5f;
			DrawRect(FLinearColor(0.02f, 0.03f, 0.05f, 0.78f), KX - 4.0f * S, Y - 1.0f * S, RowW + 8.0f * S, RowH + 2.0f * S);
			for (int32 k = 0; k < Words.Num(); ++k)
			{
				DrawRect(Words[k].Value, KX, Y + (RowH - Swatch) * 0.5f, Swatch, Swatch);
				KX += Swatch + 3.0f * S;
				Text(Words[k].Key, KX, Y, k == 0 ? FLinearColor::White : Grey, Font, KeyScale, false);
				KX += TextSize(Words[k].Key, Font, KeyScale).X + Space;
			}
			if (!Tail.IsEmpty())
			{
				Text(Tail, KX, Y, Aimed->Ko > 0.0f ? Gold : FLinearColor(0.88f, 0.6f, 1.0f), Font, KeyScale, false);
			}
			Y += RowH + 3.0f * S;
		}

		// A spell on its way out: the card, over the name, full size for the unit pointed at.
		if (Unit.IsCasting())
		{
			CastCard(Unit, Head.At.X, Head.At.Y - 26.0f * S * O, O, true);
		}

		// Its statuses, under the bars.
		const float Chip = 15.0f * S * FTMSettings::Get().StatusIconScale;
		StatusChips(Unit, X, Y, Chip, false, false);
	}
}

float ATMBattleHud::CastCard(const TMSim::FUnit& Unit, float X, float Bottom, float Scale, bool bFull)
{
	const TMSim::FAbility* Ability = Unit.Ability(Unit.Casting.Slot);
	if (!Ability)
	{
		return 0.0f;
	}
	UFont* Font = GEngine->GetMediumFont();
	const float K = S * Scale;
	// v23 play test, "the screen gets too busy" with several casting: the card
	// says what is coming as the cast starts, then over a second shrinks to a
	// small chip -- the icon, the seconds left and the bar -- with the name gone.
	// Pointing at the caster shows it full again (bFull).
	const float Elapsed = static_cast<float>(FMath::Max(0, Unit.Casting.Total - Unit.Casting.Ticks)) / TMSim::Pace::TicksPerSecond;
	const float Small = bFull ? 0.0f : FMath::SmoothStep(0.8f, 2.0f, Elapsed);
	const float W = FMath::Lerp(196.0f, 82.0f, Small) * K;
	const float H = FMath::Lerp(44.0f, 26.0f, Small) * K;
	const float Left = X - W * 0.5f;
	const float Top = Bottom - H;
	Panel(Left, Top, W, H, FLinearColor(0.04f, 0.05f, 0.08f, FMath::Lerp(0.9f, 0.8f, Small)), CastColour, FMath::Lerp(2.0f, 1.5f, Small) * K);
	const float Inset = FMath::Lerp(5.0f, 4.0f, Small) * K;
	const float IconSize = H - 2.0f * Inset;
	if (UTexture2D* Picture2D = AbilityIcon(*Ability, false))
	{
		Picture(Picture2D, Left + Inset, Top + Inset, IconSize, IconSize);
	}
	const float TX = Left + Inset + IconSize + FMath::Lerp(7.0f, 5.0f, Small) * K;
	const float TW = W - (TX - Left) - FMath::Lerp(8.0f, 5.0f, Small) * K;
	const float TimeScale = FMath::Lerp(0.5f, 0.4f, Small) * K;
	const FString LeftText = FString::Printf(TEXT("%.1f s"), static_cast<float>(Unit.Casting.Ticks) / TMSim::Pace::TicksPerSecond);
	const FVector2D LeftSize = TextSize(LeftText, Font, TimeScale);
	// The name fades out in the first half of the shrink.
	const float NameAlpha = 1.0f - FMath::Clamp(Small * 2.0f, 0.0f, 1.0f);
	if (NameAlpha > 0.01f)
	{
		OutlinedText(UTF8_TO_TCHAR(Ability->Name.c_str()), TX, Top + 4.0f * K, TextColour * FLinearColor(1.0f, 1.0f, 1.0f, NameAlpha), Font, 0.52f * K, 1.0f * K);
	}
	OutlinedText(LeftText, TX + TW - LeftSize.X, Top + FMath::Lerp(4.0f, 2.0f, Small) * K, CastColour, Font, TimeScale, 1.0f * K);
	const float Done = 1.0f - static_cast<float>(Unit.Casting.Ticks) / FMath::Max(1, Unit.Casting.Total);
	const float BarH = FMath::Lerp(7.0f, 4.0f, Small) * K;
	Bar(TX, Top + H - BarH - FMath::Lerp(6.0f, 4.0f, Small) * K, TW, BarH, FMath::Clamp(Done, 0.0f, 1.0f), CastColour, FLinearColor(0.12f, 0.12f, 0.16f, 0.95f), 0.0f);
	return H;
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
	// 2026-10-03, "more visually noticeable": a thicker edge, a three-step glow,
	// a quicker beat, never fully dark between beats (as the move tiles, TMBattleHud.cpp).
	const float Pulse = bFlash ? 0.35f + 0.65f * (0.5f + 0.5f * FMath::Sin((GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0f) * 2.0f * PI * 1.4f)) : 0.0f;
	const FLinearColor Flash(1.0f, 0.95f, 0.55f, 1.0f);
	if (bFlash)
	{
		for (int32 Ring = 3; Ring >= 1; --Ring)
		{
			const float Out = (3.0f + 4.0f * Ring) * S;
			DrawRect(Flash * FLinearColor(1.0f, 1.0f, 1.0f, (0.14f + 0.12f * (3 - Ring)) * Pulse), X - Out, Y - Out, Size + 2.0f * Out, Size + 2.0f * Out);
		}
	}
	DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.75f), X - 3.0f * S, Y - 3.0f * S, Size + 6.0f * S, Size + 6.0f * S);
	const float RimW = (bFlash ? 4.0f : 2.0f) * S;
	DrawRect(bFlash ? FMath::Lerp(Edge, Flash, Pulse) : Edge, X - RimW, Y - RimW, Size + 2.0f * RimW, Size + 2.0f * RimW);
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
	// Its card under the pointer (AbilityCard); while aiming it, the card is docked over the bar instead.
	if (!bAiming)
	{
		AddAbilityTip(X, Y, Size, Size, Unit, Slot);
	}
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
	if (bRight)
	{
		// The boss bar sits on top of this (DrawBossBar).
		InspectTop = Y - 3.0f * S - (Warned > 0.0f ? Warned + 4.0f * S : 0.0f);
		InspectRight = X + W;
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
	// The turn's clock round the portrait (2026-10-03): its frame, from the top
	// middle clockwise, is the time left before the turn is lost, shrinking as
	// it runs; red and beating in the last five seconds.
	if (Unit.IsAlive() && Unit.bReady)
	{
		const float Share = FMath::Clamp(static_cast<float>(Unit.Clock) / FMath::Max(1, Battle.ClockTicks(Unit)), 0.0f, 1.0f);
		const bool bUrgent = Unit.Clock / Tps <= 5.0f;
		const float Beat = bUrgent ? 0.6f + 0.4f * FMath::Abs(FMath::Sin(GetWorld()->GetRealTimeSeconds() * 5.0f)) : 1.0f;
		const FLinearColor Hand = (bUrgent ? Urgent : Gold) * FLinearColor(1.0f, 1.0f, 1.0f, Beat);
		const float Thick = 4.0f * S;
		const float FX = PX - 3.0f * S;
		const float FY = Y - 3.0f * S;
		const float FrameSide = Portrait + 6.0f * S;
		float ClockLeft = Share * 4.0f * FrameSide;
		// Each leg of the frame in turn, as much of it as is left.
		auto Leg = [&](float AX, float AY, float DX, float DY, float Length)
		{
			const float Drawn = FMath::Min(ClockLeft, Length);
			if (Drawn <= 0.0f)
			{
				return;
			}
			ClockLeft -= Drawn;
			const float EX = AX + DX * Drawn;
			const float EY = AY + DY * Drawn;
			DrawRect(Hand, FMath::Min(AX, EX) - (DX == 0.0f ? Thick * 0.5f : 0.0f), FMath::Min(AY, EY) - (DY == 0.0f ? Thick * 0.5f : 0.0f),
				DX == 0.0f ? Thick : FMath::Abs(EX - AX), DY == 0.0f ? Thick : FMath::Abs(EY - AY));
		};
		Leg(FX + FrameSide * 0.5f, FY, 1.0f, 0.0f, FrameSide * 0.5f);
		Leg(FX + FrameSide, FY, 0.0f, 1.0f, FrameSide);
		Leg(FX + FrameSide, FY + FrameSide, -1.0f, 0.0f, FrameSide);
		Leg(FX, FY + FrameSide, 0.0f, -1.0f, FrameSide);
		Leg(FX, FY, 1.0f, 0.0f, FrameSide * 0.5f);
	}
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
	InspectTop = 0.0f;
	InspectRight = 0.0f;
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
	const float Top = SquadBottom > 0.0f ? SquadBottom + 16.0f * S : From.bShowLog ? LogBottom + 16.0f * S : 12.0f * S + (2.0f * RowHeight + (FTMSettings::Get().bTurnSquares ? SquareLane : 2.0f * PinLane)) * S + 18.0f * S;
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

void ATMBattleHud::DrawReadyMarks(ATMBattleDirector& From)
{
	// "Battle Indicator Alternatives" Ready D, chosen after the v19 play test: a
	// gold READY tag where the health bar sits over the head; nothing on the ground.
	if (!PlayerOwner || From.Battle.Winner != -1)
	{
		return;
	}
	UFont* Font = GEngine->GetMediumFont();
	const FTransform& BoardAt = From.GetActorTransform();
	const float O = FTMSettings::Get().OverheadScale;
	const FString Word = TEXT("READY");
	const float Scale = 0.44f * S * O;
	const FVector2D WordSize = TextSize(Word, Font, Scale);
	for (const TMSim::FUnit& Each : From.Battle.Units)
	{
		if (!Each.bReady || !Each.IsAlive() || Each.bOffBoard || !From.IsSeen(Each))
		{
			continue;
		}
		FVector2D At;
		if (!PlayerOwner->ProjectWorldLocationToScreen(BoardAt.TransformPosition(From.ShownAt(Each)
			+ FVector(0.0f, 0.0f, ATMBattleDirector::UnitHeadCm + 82.0f)), At))
		{
			continue;
		}
		const FLinearColor Edge = From.IsFriend(Each) ? Gold : FLinearColor(1.0f, 0.55f, 0.45f);
		const float PadX = 6.0f * S * O;
		const float W = WordSize.X + 2.0f * PadX;
		const float H = WordSize.Y + 2.0f * S * O;
		// Its bottom just above the bars and the name (DrawOverheads draws them from At).
		const float X = At.X - W * 0.5f;
		const float Y = At.Y - H - 34.0f * S * O;
		Panel(X, Y, W, H, FLinearColor(0.06f, 0.07f, 0.1f, 0.9f), Edge, 2.0f * S * O);
		Text(Word, X + PadX, Y + 1.0f * S * O, Edge, Font, Scale);
	}
}

void ATMBattleHud::DrawCastWarnings(ATMBattleDirector& From)
{
	// "Battle Indicator Alternatives" Cast B, with A (CastCard, PaintCasts): a
	// sigil turning under every caster in sight, and a banner across the top for
	// each enemy cast that will catch units of yours.
	if (!PlayerOwner || From.Battle.Winner != -1)
	{
		return;
	}
	UFont* Font = GEngine->GetMediumFont();
	const FTransform& BoardAt = From.GetActorTransform();
	const float Spin = static_cast<float>(FPlatformTime::Seconds() * 1.4);
	const int32 Mine = From.FriendTeam();
	float BannerY = 150.0f * S;
	for (const TMSim::FUnit& Caster : From.Battle.Units)
	{
		if (!Caster.IsAlive() || Caster.bOffBoard || !Caster.IsCasting() || !From.IsSeen(Caster))
		{
			continue;
		}
		const TMSim::FAbility* Ability = Caster.Ability(Caster.Casting.Slot);
		if (!Ability)
		{
			continue;
		}
		// The sigil: two dashed rings round the caster's feet, turning opposite ways.
		const FVector Feet = From.ShownAt(Caster) + FVector(0.0f, 0.0f, 6.0f);
		for (int32 Ring = 0; Ring < 2; ++Ring)
		{
			const float Radius = (Ring == 0 ? 0.9f : 0.62f) * From.TileSize;
			const int32 Steps = 32;
			const float Turn = Ring == 0 ? Spin : -Spin * 0.6f;
			const FLinearColor Ink(CastColour.R, CastColour.G, CastColour.B, Ring == 0 ? 0.95f : 0.6f);
			for (int32 k = 0; k < Steps; k += 2)
			{
				const float A0 = Turn + UE_TWO_PI * k / Steps;
				const float A1 = Turn + UE_TWO_PI * (k + 1) / Steps;
				FVector2D P0;
				FVector2D P1;
				if (PlayerOwner->ProjectWorldLocationToScreen(BoardAt.TransformPosition(Feet + FVector(FMath::Cos(A0), FMath::Sin(A0), 0.0f) * Radius), P0)
					&& PlayerOwner->ProjectWorldLocationToScreen(BoardAt.TransformPosition(Feet + FVector(FMath::Cos(A1), FMath::Sin(A1), 0.0f) * Radius), P1))
				{
					DrawLine(P0.X, P0.Y, P1.X, P1.Y, Ink, (Ring == 0 ? 2.5f : 1.5f) * S);
				}
			}
		}
		// The banner: an enemy's cast, and how many of yours stand where it lands.
		if (Caster.Team == Mine || BannerY > 330.0f * S)
		{
			continue;
		}
		TMSim::FVec2 Aim = Caster.Casting.Target;
		if (Caster.Casting.FollowId >= 0)
		{
			if (const TMSim::FUnit* Followed = From.Battle.FindUnit(Caster.Casting.FollowId))
			{
				Aim = Followed->Pos;
			}
		}
		int32 Caught = 0;
		for (const TMSim::FHit& Hit : From.Battle.Preview(Caster, Caster.Casting.Slot, Caster.Pos, Aim))
		{
			const TMSim::FUnit* Struck = From.Battle.FindUnit(Hit.UnitId);
			Caught += Struck && Struck->Team == Mine && Struck->IsAlive() ? 1 : 0;
		}
		if (Caught == 0)
		{
			continue;
		}
		const float W = FMath::Min(560.0f * S, Canvas->ClipX - 40.0f * S);
		const float H = 40.0f * S;
		const float X = (Canvas->ClipX - W) * 0.5f;
		const FLinearColor Foe(1.0f, 0.56f, 0.52f);
		Panel(X, BannerY, W, H, FLinearColor(0.05f, 0.05f, 0.08f, 0.94f), Foe, 2.0f * S);
		const FString Say = FString::Printf(TEXT("ENEMY %s casting %hs"), *JobName(Caster), Ability->Name.c_str());
		const FString Right = FString::Printf(TEXT("%d of yours in it   %.1f s"), Caught,
			static_cast<float>(Caster.Casting.Ticks) / TMSim::Pace::TicksPerSecond);
		Text(Say, X + 12.0f * S, BannerY + 5.0f * S, TextColour, Font, 0.5f * S);
		const FVector2D RightSize = TextSize(Right, Font, 0.5f * S);
		Text(Right, X + W - 12.0f * S - RightSize.X, BannerY + 5.0f * S, Foe, Font, 0.5f * S);
		const float Done = 1.0f - static_cast<float>(Caster.Casting.Ticks) / FMath::Max(1, Caster.Casting.Total);
		Bar(X + 12.0f * S, BannerY + H - 10.0f * S, W - 24.0f * S, 5.0f * S, FMath::Clamp(Done, 0.0f, 1.0f), CastColour,
			FLinearColor(0.12f, 0.12f, 0.16f, 0.95f), 0.0f);
		BannerY += H + 6.0f * S;
	}
}

void ATMBattleHud::DrawZoneShields(ATMBattleDirector& From)
{
	const TMSim::FTuning& Tune = From.Battle.Tuning;
	if (!PlayerOwner || Tune.ZoneOfControl < 1.0 || Tune.EngageRadius <= 0.0 || From.Battle.Winner != -1)
	{
		return;
	}
	UTexture2D* Shield = Icon(TEXT("hud/zone_shield"), false);
	if (!Shield)
	{
		return;
	}
	const int32 Mine = From.FriendTeam();
	const float Spin = static_cast<float>(FPlatformTime::Seconds() * 0.45);
	const float Reach = static_cast<float>(Tune.EngageRadius) * From.TileSize;
	const FTransform& BoardAt = From.GetActorTransform();
	// v23 play test: the ring is on the ground, so it goes behind the units
	// standing on it. Each unit in sight is a box on the screen, its feet to the
	// top of its head; a piece of ring inside one is ground behind that body and
	// is left out (ground in front of a unit falls below its feet, outside it).
	FVector Eye;
	FRotator Look;
	PlayerOwner->GetPlayerViewPoint(Eye, Look);
	const FVector Across = FRotationMatrix(Look).GetScaledAxis(EAxis::Y);
	TArray<FBox2D> Bodies;
	for (const TMSim::FUnit& Unit : From.Battle.Units)
	{
		if (!Unit.IsAlive() || Unit.bOffBoard || !From.IsSeen(Unit))
		{
			continue;
		}
		const FVector Feet = BoardAt.TransformPosition(From.ShownAt(Unit));
		const FVector Top = BoardAt.TransformPosition(From.ShownAt(Unit) + FVector(0.0f, 0.0f, ATMBattleDirector::UnitHeadCm + 25.0f));
		FVector2D FeetAt;
		FVector2D TopAt;
		FVector2D SideAt;
		if (PlayerOwner->ProjectWorldLocationToScreen(Feet, FeetAt) && PlayerOwner->ProjectWorldLocationToScreen(Top, TopAt)
			&& PlayerOwner->ProjectWorldLocationToScreen(Feet + Across * 45.0f, SideAt))
		{
			const float Half = static_cast<float>(FVector2D::Distance(FeetAt, SideAt));
			Bodies.Add(FBox2D(FVector2D(FMath::Min(FeetAt.X, TopAt.X) - Half, TopAt.Y), FVector2D(FMath::Max(FeetAt.X, TopAt.X) + Half, FeetAt.Y)));
		}
	}
	auto Behind = [&Bodies](const FVector2D& Point)
	{
		for (const FBox2D& Body : Bodies)
		{
			if (Body.IsInside(Point))
			{
				return true;
			}
		}
		return false;
	};
	for (const TMSim::FUnit& Each : From.Battle.Units)
	{
		if (!Each.IsAlive() || !From.IsSeen(Each) || !TMSim::FBattle::HoldsTheLine(Each))
		{
			continue;
		}
		const bool bEnemy = Each.Team != Mine;
		const bool bHovered = Each.Id == From.HoverUnitId || Each.Id == From.HudHoverUnitId;
		if (!bEnemy && !bHovered)
		{
			continue;
		}
		const FVector Middle = From.ShownAt(Each) + FVector(0.0f, 0.0f, 10.0f);
		FVector2D At;
		FVector2D Edge;
		if (!PlayerOwner->ProjectWorldLocationToScreen(BoardAt.TransformPosition(Middle), At)
			|| !PlayerOwner->ProjectWorldLocationToScreen(BoardAt.TransformPosition(Middle + FVector(Reach, 0.0f, 0.0f)), Edge))
		{
			continue;
		}
		const float Size = FMath::Clamp(static_cast<float>(FVector2D::Distance(At, Edge)) * 0.3f, 14.0f * S, 40.0f * S);
		const FLinearColor Tint = bEnemy ? FLinearColor(1.0f, 0.55f, 0.2f, bHovered ? 1.0f : 0.9f) : FLinearColor(0.45f, 0.7f, 1.0f, 0.95f);
		// "Battle Indicator Alternatives" Tank B, chosen: three big shields riding a
		// ring broken into arcs, the arcs turning with them.
		const int32 Arcs = 3;
		const int32 Steps = 32;
		for (int32 a = 0; a < Arcs; ++a)
		{
			// Each arc fills the space between two shields, short of both.
			const float Start = Spin + (a + 0.18f) * UE_TWO_PI / Arcs;
			const float Stop = Spin + (a + 0.82f) * UE_TWO_PI / Arcs;
			FVector2D Last(0.0f, 0.0f);
			bool bLast = false;
			for (int32 k = 0; k <= Steps; ++k)
			{
				const float Angle = FMath::Lerp(Start, Stop, static_cast<float>(k) / Steps);
				FVector2D Here;
				const bool bHere = PlayerOwner->ProjectWorldLocationToScreen(
					BoardAt.TransformPosition(Middle + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f) * Reach), Here);
				if (bHere && bLast && !Behind((Last + Here) * 0.5f))
				{
					DrawLine(Last.X, Last.Y, Here.X, Here.Y, Tint, 3.0f * S);
				}
				Last = Here;
				bLast = bHere;
			}
		}
		const float Big = Size * 1.4f;
		for (int32 k = 0; k < Arcs; ++k)
		{
			const float Angle = Spin + k * UE_TWO_PI / Arcs;
			FVector2D Spot;
			if (PlayerOwner->ProjectWorldLocationToScreen(BoardAt.TransformPosition(Middle + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f) * Reach), Spot))
			{
				// A shield behind a body shows faintly through it rather than over it.
				Picture(Shield, Spot.X - Big * 0.5f, Spot.Y - Big * 0.5f, Big, Big,
					Behind(Spot) ? Tint * FLinearColor(1.0f, 1.0f, 1.0f, 0.25f) : Tint);
			}
		}
	}
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

	// The pointer on a chest opens the whole of it (v19 play test); nothing over
	// it otherwise (v20 play test).
	int32 HoverCache = -1;
	FVector2D HoverCacheAt(0.0f, 0.0f);
	const FVector2D Mouse = MousePoint();
	for (int32 i = 0; i < From.CacheChests.Num() && i < static_cast<int32>(From.Battle.Caches.size()); ++i)
	{
		const TMSim::FCache& Cache = From.Battle.Caches[static_cast<size_t>(i)];
		if (!From.CacheChests[i] || !From.CacheChests[i]->IsVisible() || Cache.Items.empty())
		{
			continue;
		}
		// No label over it any more (v20 play test, less text): the beam's colour is
		// its best item's tier, and the pointer on it lists the lot.
		FVector2D ChestAt;
		if (PlayerOwner->ProjectWorldLocationToScreen(From.CacheChests[i]->GetComponentLocation(), ChestAt)
			&& FVector2D::Distance(ChestAt, Mouse) < 56.0f * S)
		{
			HoverCache = i;
			HoverCacheAt = ChestAt;
		}
	}
	if (HoverCache >= 0)
	{
		const TMSim::FCache& Cache = From.Battle.Caches[static_cast<size_t>(HoverCache)];
		const float W = 380.0f * S;
		const float Pad = 10.0f * S;
		struct FRow { FString Text; FLinearColor Colour; float Scale; };
		TArray<FRow> Rows;
		Rows.Add({ FString::Printf(TEXT("%d item%s on the ground"), static_cast<int32>(Cache.Items.size()), Cache.Items.size() == 1 ? TEXT("") : TEXT("s")), Dim, 0.46f * S });
		for (const TMSim::FItemDef* Item : Cache.Items)
		{
			Rows.Add({ FString::Printf(TEXT("%hs  (%hs)"), Item->Name.c_str(), TMSim::ItemTierName(Item->Tier)),
				Whiter(TierColour(static_cast<int32>(Item->Tier)), 0.3f, 1.0f), 0.56f * S });
			const FString Summary = ItemSummary(*Item);
			if (!Summary.IsEmpty())
			{
				Rows.Add({ Summary, TextColour, 0.48f * S });
			}
			for (const FString& Row : Wrap(UTF8_TO_TCHAR(Item->Desc.c_str()), Font, 0.46f * S, W - 2.0f * Pad))
			{
				Rows.Add({ Row, FLinearColor(0.8f, 0.83f, 0.9f), 0.46f * S });
			}
		}
		Rows.Add({ TEXT("Walk a unit next to it to take one."), Dim, 0.44f * S });

		float H = 2.0f * Pad;
		for (const FRow& Row : Rows)
		{
			H += TextSize(Row.Text, Font, Row.Scale).Y + 2.0f * S;
		}
		const float X = FMath::Clamp(HoverCacheAt.X + 28.0f * S, 4.0f * S, Canvas->ClipX - W - 4.0f * S);
		const float Y = FMath::Clamp(HoverCacheAt.Y - H * 0.5f, 4.0f * S, Canvas->ClipY - H - 4.0f * S);
		Panel(X, Y, W, H, FLinearColor(0.03f, 0.04f, 0.07f, 0.94f), FLinearColor(0.5f, 0.55f, 0.65f, 0.7f), 1.0f);
		float RY = Y + Pad;
		for (const FRow& Row : Rows)
		{
			Text(Row.Text, X + Pad, RY, Row.Colour, Font, Row.Scale);
			RY += TextSize(Row.Text, Font, Row.Scale).Y + 2.0f * S;
		}
	}

	// Springs (wells) run dry once used (FTuning::SpringRestTurns): for how many
	// more of their user's turns.
	for (const TMSim::FBattle::FSpringRest& Rest : From.Battle.SpringRests)
	{
		const int32 Across = FMath::Max(1, From.Battle.Map.TilesX);
		const TMSim::FVec2 Middle((Rest.Tile % Across + 0.5f) * TMSim::Ground::TileSize, (Rest.Tile / Across + 0.5f) * TMSim::Ground::TileSize);
		FVector2D At;
		if (Rest.Turns > 0 && From.IsPointSeen(Middle) && PlayerOwner->ProjectWorldLocationToScreen(From.BoardPoint(Middle, 60.0f), At))
		{
			// A pip for each turn it stays dry (v20 play test: pips, not words).
			const float Pip = 7.0f * S;
			const float Gap = 4.0f * S;
			const float Wide = Rest.Turns * Pip + (Rest.Turns - 1) * Gap;
			for (int32 k = 0; k < Rest.Turns; ++k)
			{
				const float PX = At.X - Wide * 0.5f + k * (Pip + Gap);
				DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.6f), PX - 1.0f * S, At.Y - 1.0f * S, Pip + 2.0f * S, Pip + 2.0f * S);
				DrawRect(FLinearColor(0.62f, 0.86f, 1.0f, 0.95f), PX, At.Y, Pip, Pip);
			}
		}
	}

	// Ground zones (2026-10-04, area denial): a pip for each of the caster's
	// turns it has left, filled, and hollow for those gone, over its middle; and
	// with the pointer on its middle, what it is and what it does.
	{
		int32 HoverZone = -1;
		FVector2D HoverZoneAt(0.0f, 0.0f);
		float HoverBest = 60.0f * S;
		for (int32 i = 0; i < static_cast<int32>(From.Battle.Zones.size()); ++i)
		{
			const TMSim::FBattle::FZone& Zone = From.Battle.Zones[static_cast<size_t>(i)];
			const TMSim::FAbility* Laid = From.Battle.ZoneAbility(Zone);
			FVector2D At;
			if (!Laid || !From.GroundZoneShown(Zone)
				|| !PlayerOwner->ProjectWorldLocationToScreen(From.BoardPoint(Zone.Target, 30.0f), At))
			{
				continue;
			}
			const FLinearColor Tint = ATMBattleDirector::GroundZoneColour(*Laid, Zone.bIgnited);
			const float Pip = 7.0f * S;
			const float Gap = 4.0f * S;
			const int32 Total = FMath::Max(Zone.Total, Zone.Turns);
			const float Wide = Total * Pip + (Total - 1) * Gap;
			for (int32 k = 0; k < Total; ++k)
			{
				const float PX = At.X - Wide * 0.5f + k * (Pip + Gap);
				DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.6f), PX - 1.0f * S, At.Y - 1.0f * S, Pip + 2.0f * S, Pip + 2.0f * S);
				if (k < Zone.Turns)
				{
					DrawRect(FLinearColor(Tint.R, Tint.G, Tint.B, 0.95f), PX, At.Y, Pip, Pip);
				}
				else
				{
					DrawRect(FLinearColor(Tint.R, Tint.G, Tint.B, 0.3f), PX + 1.5f * S, At.Y + 1.5f * S, Pip - 3.0f * S, Pip - 3.0f * S);
				}
			}
			const float Near = FVector2D::Distance(At, Mouse);
			if (Near < HoverBest)
			{
				HoverBest = Near;
				HoverZone = i;
				HoverZoneAt = At;
			}
		}
		if (HoverZone >= 0 && HoverCache < 0)
		{
			const TMSim::FBattle::FZone& Zone = From.Battle.Zones[static_cast<size_t>(HoverZone)];
			const TMSim::FAbility* Laid = From.Battle.ZoneAbility(Zone);
			const float W = 360.0f * S;
			const float Pad = 10.0f * S;
			struct FRow { FString Text; FLinearColor Colour; float Scale; };
			TArray<FRow> Rows;
			const FLinearColor Tint = ATMBattleDirector::GroundZoneColour(*Laid, Zone.bIgnited);
			Rows.Add({ FString::Printf(TEXT("%hs%s"), Laid->Name.c_str(), Zone.bIgnited ? TEXT(" (burning)") : TEXT("")),
				Whiter(Tint, 0.2f, 1.0f), 0.56f * S });
			Rows.Add({ From.Battle.ZoneIsWarning(Zone)
				? FString::Printf(TEXT("%s's: lands as its next turn begins"), *From.NameOf(Zone.Owner))
				: FString::Printf(TEXT("%s's, %d of its turns left"), *From.NameOf(Zone.Owner), Zone.Turns), Dim, 0.46f * S });
			const FString Says = Zone.bIgnited
				? FString(TEXT("Burning: whoever starts a turn here, or stops here, loses 6% of its health and Burns. Water puts it out."))
				: FString(UTF8_TO_TCHAR(Laid->Desc.c_str()));
			for (const FString& Row : Wrap(Says, Font, 0.46f * S, W - 2.0f * Pad))
			{
				Rows.Add({ Row, FLinearColor(0.8f, 0.83f, 0.9f), 0.46f * S });
			}
			float H = 2.0f * Pad;
			for (const FRow& Row : Rows)
			{
				H += TextSize(Row.Text, Font, Row.Scale).Y + 2.0f * S;
			}
			const float X = FMath::Clamp(HoverZoneAt.X + 28.0f * S, 4.0f * S, Canvas->ClipX - W - 4.0f * S);
			const float Y = FMath::Clamp(HoverZoneAt.Y - H * 0.5f, 4.0f * S, Canvas->ClipY - H - 4.0f * S);
			Panel(X, Y, W, H, FLinearColor(0.03f, 0.04f, 0.07f, 0.94f), FLinearColor(Tint.R, Tint.G, Tint.B, 0.7f), 1.0f);
			float RY = Y + Pad;
			for (const FRow& Row : Rows)
			{
				Text(Row.Text, X + Pad, RY, Row.Colour, Font, Row.Scale);
				RY += TextSize(Row.Text, Font, Row.Scale).Y + 2.0f * S;
			}
		}
	}

	// A monster's wind-up ("Camps and Bosses Mockups" B): the seconds where it
	// lands, and a mark over everyone it would catch.
	const FTransform& BoardAt = From.GetActorTransform();
	const FLinearColor WindWords(0.85f, 0.72f, 1.0f, 1.0f);
	for (const TMSim::FUnit& Caster : From.Battle.Units)
	{
		if (!WindingUp(Caster) || !From.IsSeen(Caster))
		{
			continue;
		}
		const TMSim::FVec2 Aim = CastAim(From.Battle, Caster);
		const TMSim::FAbility* Winding = Caster.Ability(Caster.Casting.Slot);
		const bool bSelf = Winding && TMSim::ShapeOf(*Winding) == "self";
		Lines(FString::Printf(TEXT("%.1f s"), Caster.Casting.Ticks / Tps), From.BoardPoint(bSelf ? Caster.Pos : Aim, 40.0f),
			WindWords, Big, 0.8f * S, 2.0f * S);
		for (const TMSim::FHit& Hit : From.Battle.Preview(Caster, Caster.Casting.Slot, Caster.Pos, Aim))
		{
			const TMSim::FUnit* Caught = From.Battle.FindUnit(Hit.UnitId);
			if (Caught && Caught->IsAlive() && Caught->Id != Caster.Id && From.IsSeen(*Caught))
			{
				Lines(TEXT("!"), BoardAt.TransformPosition(From.WorldFor(*Caught) + FVector(0.0f, 0.0f, ATMBattleDirector::UnitHeadCm + 35.0f)), WindWords, Big, 0.9f * S, 2.0f * S);
			}
		}
	}

	// A boss on the hunt ("Camps and Bosses Mockups" C): a dashed line to its prey.
	if (From.Battle.Tuning.BossHunt >= 0.5)
	{
		const FLinearColor HuntColour(1.0f, 0.54f, 0.36f, 0.95f);
		for (const TMSim::FUnit& Boss : From.Battle.Units)
		{
			const TMSim::FUnit* Prey = Boss.bMonster && Boss.HuntTarget >= 0 && Boss.IsAlive() ? From.Battle.FindUnit(Boss.HuntTarget) : nullptr;
			if (!Prey || !Prey->IsAlive() || !From.IsSeen(Boss) || !From.IsSeen(*Prey))
			{
				continue;
			}
			FVector2D A;
			FVector2D B;
			if (!PlayerOwner->ProjectWorldLocationToScreen(BoardAt.TransformPosition(From.WorldFor(Boss) + FVector(0.0f, 0.0f, 120.0f)), A)
				|| !PlayerOwner->ProjectWorldLocationToScreen(BoardAt.TransformPosition(From.WorldFor(*Prey) + FVector(0.0f, 0.0f, 90.0f)), B))
			{
				continue;
			}
			const float Length = FVector2D::Distance(A, B);
			const FVector2D Step = Length > 1.0f ? (B - A) / Length : FVector2D::ZeroVector;
			for (float Along = 0.0f; Along < Length; Along += 22.0f * S)
			{
				const FVector2D P0 = A + Step * Along;
				const FVector2D P1 = A + Step * FMath::Min(Length, Along + 13.0f * S);
				DrawLine(P0.X, P0.Y, P1.X, P1.Y, HuntColour, 3.0f * S);
			}
			Lines(TEXT("HUNTED"), BoardAt.TransformPosition(From.WorldFor(*Prey) + FVector(0.0f, 0.0f, ATMBattleDirector::UnitHeadCm + 60.0f)), HuntColour, Font, 0.55f * S, 1.6f * S);
		}
	}

	// The camps: in full (name, temperament, clock, noise) only under the pointer;
	// otherwise a small clock face and the time while it waits, "!" if roused
	// (v20 play test, less text).
	for (const TObjectPtr<UTextRenderComponent>& Label : From.CampLabels)
	{
		if (!Label || !Label->IsVisible())
		{
			continue;
		}
		const FString Whole = Label->Text.ToString();
		const FLinearColor Ink = Whiter(Label->TextRenderColor.ReinterpretAsLinear(), 0.6f, 1.0f);
		FVector2D LabelAt;
		if (!PlayerOwner->ProjectWorldLocationToScreen(Label->GetComponentLocation(), LabelAt))
		{
			continue;
		}
		if (FVector2D::Distance(LabelAt, Mouse) < 80.0f * S)
		{
			Lines(Whole, Label->GetComponentLocation(), Ink, Font, 0.66f * S, 1.6f * S);
			continue;
		}
		FString Short;
		int32 Wakes = Whole.Find(TEXT("wakes in "));
		if (Whole.Contains(TEXT("roused")) || Whole.EndsWith(TEXT("  !")))
		{
			Short = TEXT("!");
		}
		else if (Wakes != INDEX_NONE)
		{
			Short = Whole.Mid(Wakes + 9);
			int32 End = INDEX_NONE;
			if (Short.FindChar(TEXT('\n'), End))
			{
				Short = Short.Left(End);
			}
		}
		if (Short.IsEmpty())
		{
			continue;
		}
		const float Scale = 0.52f * S;
		const FVector2D Size = TextSize(Short, Font, Scale);
		const float R = 7.0f * S;
		const FVector2D Dial(LabelAt.X - Size.X * 0.5f - R - 4.0f * S, LabelAt.Y - Size.Y * 0.5f);
		for (int32 k = 0; k < 16; ++k)
		{
			const float A0 = UE_TWO_PI * k / 16.0f;
			const float A1 = UE_TWO_PI * (k + 1) / 16.0f;
			DrawLine(Dial.X + FMath::Cos(A0) * R, Dial.Y + FMath::Sin(A0) * R, Dial.X + FMath::Cos(A1) * R, Dial.Y + FMath::Sin(A1) * R, Ink, 1.5f * S);
		}
		DrawLine(Dial.X, Dial.Y, Dial.X, Dial.Y - R * 0.7f, Ink, 1.5f * S);
		DrawLine(Dial.X, Dial.Y, Dial.X + R * 0.5f, Dial.Y, Ink, 1.5f * S);
		OutlinedText(Short, LabelAt.X - Size.X * 0.5f, LabelAt.Y - Size.Y, Ink, Font, Scale, 1.4f * S);
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
		// A status put on: its icon, popping (v20 play test: icons, not names).
		if (!Floater.StatusIcon.IsEmpty())
		{
			if (UTexture2D* Picture2D = Icon(TEXT("statuses/") + Floater.StatusIcon, false))
			{
				const float Side = 30.0f * S * Numbers * Pop;
				Picture(Picture2D, At.X - Side * 0.5f, At.Y - Side, Side, Side, FLinearColor(1.0f, 1.0f, 1.0f, Alpha));
				continue;
			}
		}
		const float Scale = 0.95f * S * Numbers * Floater.Scale * Pop;
		const FString Word = Floater.Text->Text.ToString();
		const FVector2D Size = TextSize(Word, Big, Scale);
		const float TagScale = 0.95f * S * Numbers * 0.5f;
		const FVector2D TagSize = Floater.Tag.IsEmpty() ? FVector2D::ZeroVector : TextSize(Floater.Tag, Big, TagScale);
		const float Gap = Floater.Tag.IsEmpty() ? 0.0f : 5.0f * S;
		const float X = At.X - (Size.X + Gap + TagSize.X) * 0.5f;
		const float Y = At.Y - Size.Y;
		const FLinearColor Colour = FColor(C.R, C.G, C.B).ReinterpretAsLinear().CopyWithNewOpacity(Alpha);
		// 2026-10-06 ("sharper ... combat text", the human's pick B): no glow, a hard
		// dark edge about a fourteenth of the word's height, all on whole pixels.
		const float Edge = FMath::Max(2.0f * S, Size.Y * 0.07f);
		const float PX = FMath::RoundToFloat(X);
		const float PY = FMath::RoundToFloat(Y);
		if (Floater.bEmber)
		{
			// Dark red reads on a dark field by a pale edge (its soft glow is gone).
			const FLinearColor Rim(1.0f, 0.79f, 0.72f, Alpha);
			const float E = FMath::Max(1.0f, FMath::RoundToFloat(1.3f * S));
			const float Ways[8][2] = { { E, 0.0f }, { -E, 0.0f }, { 0.0f, E }, { 0.0f, -E }, { E, E }, { -E, E }, { E, -E }, { -E, -E } };
			for (int32 k = 0; k < 8; ++k)
			{
				DrawText(Word, Rim, PX + Ways[k][0], PY + Ways[k][1], Big, Scale * FontBoost);
			}
			DrawText(Word, Colour, PX, PY, Big, Scale * FontBoost);
		}
		else
		{
			OutlinedText(Word, PX, PY, Colour, Big, Scale, Edge);
		}
		if (Floater.bBold)
		{
			// Twice, a whole pixel or more apart: heavier.
			DrawText(Word, Colour, PX + FMath::Max(1.0f, FMath::RoundToFloat(0.9f * S * Floater.Scale)), PY, Big, Scale * FontBoost);
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
		// Ground zones (2026-10-04): how long the ground lasts, and what it takes.
		if (Ability->LaysZone())
		{
			Facts.Add(FString::Printf(TEXT("Ground %d turns"), Ability->ZoneTurns));
			if (Ability->ZonePercent > 0.0f)
			{
				Facts.Add(FString::Printf(TEXT("%s%% max HP a turn"), *Num(Ability->ZonePercent)));
			}
			if (!Ability->ZoneStatus2.empty())
			{
				Facts.Add(FString::Printf(TEXT("%hs %d turn%s"), Ability->ZoneStatus2.c_str(), Ability->ZoneStatus2Turns, Ability->ZoneStatus2Turns == 1 ? TEXT("") : TEXT("s")));
			}
			if (Ability->ZoneSight > 0.0f)
			{
				Facts.Add(FString::Printf(TEXT("Sees %s m"), *Num(Ability->ZoneSight)));
			}
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
	// What it summons, in full (v19 play test): each pet its abilities call.
	for (int32 Slot = 0; Slot < 4; ++Slot)
	{
		const TMSim::FAbility* Calls = TMSim::JobAbility(Job.Id, Slot);
		const TMSim::FJobDef* Pet = Calls && !Calls->PetJob.empty() ? TMSim::FindJob(Calls->PetJob) : nullptr;
		if (!Pet)
		{
			continue;
		}
		const float HeadH = 30.0f * S;
		if (Inside(Y, HeadH))
		{
			Text(FString::Printf(TEXT("SUMMONS  (%hs%s)"), Calls->Name.c_str(),
				Calls->PetTurns > 0 ? *FString::Printf(TEXT(", stays %d turns"), Calls->PetTurns) : TEXT("")), RX, Y + 4.0f * S, Gold, Font, 0.55f * S);
		}
		Y += HeadH;
		const float PetH = RW > 700.0f * S ? 300.0f * S : 470.0f * S;
		if (Inside(Y, PetH))
		{
			DrawClassCard(*Pet, RX, Y, RW, PetH);
		}
		Y += PetH + 10.0f * S;
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
	// v20 play test: a class at random, of the role shown.
	MenuButton(PX + PW - 290.0f * S, PY + 16.0f * S, 130.0f * S, 36.0f * S, TEXT("Random"), ETMHudAction::PickerChoose, -2, false);
	AddTip(PX + PW - 290.0f * S, PY + 16.0f * S, 130.0f * S, 36.0f * S, TEXT("Any class this slot could have, of the role shown (All for any)."));

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

	// The grid, with the class under the pointer shown in full beside it.
	const float CardW = FMath::Min(440.0f * S, PW * 0.32f);
	const float GridW = PW - 48.0f * S - CardW - 20.0f * S;
	const int32 Columns = GridW > 900.0f * S ? 4 : 3;
	const float Gap = 6.0f * S;
	const float CellW = (GridW - (Columns - 1) * Gap) / Columns;
	const float CellH = 40.0f * S;
	float Y = FY + 48.0f * S;
	int32 Column = 0;
	const FVector2D Mouse = MousePoint();
	const TMSim::FJobDef* Shown = TMSim::FindJob(From.Setup.Rosters[Team][Slot]);
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
		if (FBox2D(FVector2D(X, Y), FVector2D(X + CellW, Y + CellH)).IsInside(Mouse))
		{
			Shown = &Job;
		}
		if (!bCurrent && From.Setup.bUniqueClasses && From.ClassTaken(Job.Id, Team, Slot))
		{
			// One of each class: this one is someone else's already.
			Panel(X, Y, CellW, CellH, FLinearColor(0.06f, 0.06f, 0.08f, 0.9f), FLinearColor(0.25f, 0.25f, 0.3f, 0.5f), 1.0f);
			const FString Name = UTF8_TO_TCHAR(Job.Name.c_str());
			const FVector2D Size = TextSize(Name, Font, 0.58f * S);
			Text(Name, X + (CellW - Size.X) * 0.5f, Y + (CellH - Size.Y) * 0.5f, FLinearColor(0.5f, 0.5f, 0.55f, 0.5f), Font, 0.58f * S);
			AddTip(X, Y, CellW, CellH, TEXT("Taken: one of each class in this battle (setup, Duplicate classes)."));
			if (++Column == Columns)
			{
				Column = 0;
				Y += CellH + Gap;
			}
			continue;
		}
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
	if (Shown)
	{
		DrawClassCard(*Shown, PX + PW - 24.0f * S - CardW, FY, CardW, PY + PH - 48.0f * S - FY);
	}
	Text(TEXT("Rest the pointer on a class to see it in full. Classes are made in the class creator (E:\\TacticsClassCreator)."),
		PX + 24.0f * S, PY + PH - 32.0f * S, Dim, Font, 0.46f * S);
}

void ATMBattleHud::DrawClassCard(const TMSim::FJobDef& Job, float X, float Y, float W, float H)
{
	UFont* Font = GEngine->GetMediumFont();
	UFont* Big = GEngine->GetLargeFont();
	Panel(X, Y, W, H, FLinearColor(0.03f, 0.04f, 0.07f, 0.97f), FLinearColor(0.4f, 0.45f, 0.55f, 0.6f), 1.0f);
	const float Pad = 14.0f * S;
	float CY = Y + Pad;
	Text(UTF8_TO_TCHAR(Job.Name.c_str()), X + Pad, CY, Gold, Big, 0.7f * S);
	CY += 34.0f * S;
	FString RoleText;
	for (const std::string& JobRole : Job.Roles)
	{
		RoleText += (RoleText.IsEmpty() ? TEXT("") : TEXT(", ")) + FString(UTF8_TO_TCHAR(JobRole.c_str()));
	}
	Text(RoleText, X + Pad, CY, Dim, Font, 0.5f * S);
	CY += 22.0f * S;
	Text(FString::Printf(TEXT("HP %d   Armor %d   Resist %d   Evasion %d%%   Speed %d   Move %d"),
		Job.Stats.Get(TMSim::EStat::Hp), Job.Stats.Get(TMSim::EStat::AttDef), Job.Stats.Get(TMSim::EStat::MagDef),
		ClassEvasion(Job.Stats), Job.Stats.Get(TMSim::EStat::Speed), Job.Stats.Get(TMSim::EStat::Move)),
		X + Pad, CY, TextColour, Font, 0.5f * S);
	CY += 30.0f * S;

	// The four abilities: in two columns when the card is wide (the draft's), else one.
	const int32 Cols = W > 700.0f * S ? 2 : 1;
	const float ColW = (W - 2.0f * Pad - (Cols - 1) * Pad) / Cols;
	const float BlockH = (Y + H - Pad - CY) / (4 / Cols);
	const float IconSize = 36.0f * S;
	for (int32 Slot = 0; Slot < 4; ++Slot)
	{
		const TMSim::FAbility* Ability = TMSim::JobAbility(Job.Id, Slot);
		if (!Ability)
		{
			continue;
		}
		const float BX = X + Pad + (Slot % Cols) * (ColW + Pad);
		const float BY = CY + (Slot / Cols) * BlockH;
		if (UTexture2D* Icon = AbilityIcon(*Ability, false))
		{
			Picture(Icon, BX, BY, IconSize, IconSize);
		}
		const float TX = BX + IconSize + 8.0f * S;
		const float TW = ColW - IconSize - 8.0f * S;
		Text(FString::Printf(TEXT("%hs%s"), Ability->Name.c_str(), Slot == 3 ? TEXT("  (ultimate)") : TEXT("")), TX, BY, TextColour, Font, 0.55f * S);
		FString Meta = Ability->Kind == "active" ? FString() : FString(UTF8_TO_TCHAR(Ability->Kind.c_str()));
		auto Add = [&Meta](const FString& Part) { Meta += (Meta.IsEmpty() ? TEXT("") : TEXT(" · ")) + Part; };
		if (Ability->MaxRange > 0.0f)
		{
			Add(FString::Printf(TEXT("%g-%g m"), Ability->MinRange, Ability->MaxRange));
		}
		if (Ability->Cast > 0.0f)
		{
			Add(FString::Printf(TEXT("%g s to cast"), Ability->Cast));
		}
		if (Ability->Cooldown > 0)
		{
			Add(FString::Printf(TEXT("every %d turns"), Ability->Cooldown));
		}
		Text(Meta, TX, BY + 20.0f * S, Dim, Font, 0.44f * S);
		float LY = BY + 38.0f * S;
		for (const FString& Line : Wrap(UTF8_TO_TCHAR(Ability->Desc.c_str()), Font, 0.46f * S, TW))
		{
			if (LY + 18.0f * S > BY + BlockH - 4.0f * S)
			{
				break;
			}
			Text(Line, TX, LY, FLinearColor(0.82f, 0.85f, 0.92f), Font, 0.46f * S);
			LY += 18.0f * S;
		}
	}
}

// ------------------------------------------------- cooldowns on the turn order

TArray<ATMBattleHud::FTMBack> ATMBattleHud::ComingBack(ATMBattleDirector& From, const TMSim::FUnit& Unit) const
{
	// A slot's count goes down by one as each of its owner's turns begins, and it
	// is usable at nought: a count of N is back on its Nth coming turn. One just
	// used is set to its cooldown plus one (SimResolve.cpp), so the one being
	// aimed would be back on that turn.
	TArray<FTMBack> Out;
	for (int32 Slot = 0; Slot < TMSim::AbilitySlots; ++Slot)
	{
		if (Unit.Cooldowns[Slot] > 0 && Unit.Ability(Slot))
		{
			FTMBack Back;
			Back.Slot = Slot;
			Back.Turn = Unit.Cooldowns[Slot];
			Out.Add(Back);
		}
	}
	if (From.AimMode == ATMBattleDirector::EAimMode::Ability && From.SelectedId == Unit.Id && From.AimSlot >= 0)
	{
		const TMSim::FAbility* Aimed = Unit.Ability(From.AimSlot);
		if (Aimed && Aimed->Cooldown > 0 && Unit.Cooldowns[From.AimSlot] == 0)
		{
			FTMBack Back;
			Back.Slot = From.AimSlot;
			Back.Turn = Aimed->Cooldown + 1;
			Back.bPreview = true;
			Out.Add(Back);
		}
	}
	return Out;
}

float ATMBattleHud::TurnInSeconds(const TMSim::FBattle& Battle, const TMSim::FUnit& Unit, int32 Turn) const
{
	// A full gauge at its pace now; a unit on its turn starts the next from empty.
	int32 Gain = Battle.TgGain(Unit);
	if (Gain <= 0)
	{
		Gain = Battle.BaseTgGain(Unit);
	}
	const float Every = FMath::DivideAndRoundUp(TMSim::Pace::TgMax, FMath::Max(1, Gain)) / Tps;
	if (Unit.bReady)
	{
		return FMath::Max(1, Turn) * Every;
	}
	return SecondsLeft(Unit, Battle) + FMath::Max(0, Turn - 1) * Every;
}

bool ATMBattleHud::TurnLinked(ATMBattleDirector& From, int32 UnitId) const
{
	return UnitId >= 0 && (UnitId == From.HoverUnitId || UnitId == From.HudHoverUnitId);
}

void ATMBattleHud::DrawComingTurns(ATMBattleDirector& From, const TMSim::FUnit& Unit, float X, float Top, bool bLeftward)
{
	// "Cooldown Ghost Chip Mockups" C: the unit's next three turns as ghost
	// squares, each carrying the abilities that are back on it, and when.
	const TMSim::FBattle& Battle = From.Battle;
	UFont* Font = GEngine->GetMediumFont();
	const TArray<FTMBack> Backs = ComingBack(From, Unit);
	const float Beat = 0.55f + 0.45f * FMath::Sin((GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0f) * 6.0f);
	const float Column = 108.0f * S;
	const float Ghost = 46.0f * S;
	const float Pad = 10.0f * S;
	const float Heading = 22.0f * S;
	const float Small = 0.4f * S;
	const float Smaller = 0.36f * S;
	const float BoxW = 3.0f * Column + 2.0f * Pad;
	const float BoxH = Heading + Ghost + 50.0f * S + 2.0f * Pad;
	const float Left = FMath::Clamp(bLeftward ? X - BoxW : X, 4.0f * S, FMath::Max(4.0f * S, Canvas->ClipX - BoxW - 4.0f * S));
	Panel(Left, Top, BoxW, BoxH, FLinearColor(0.02f, 0.03f, 0.05f, 0.96f), FLinearColor(Gold.R, Gold.G, Gold.B, 0.5f), 1.0f);
	Text(FString::Printf(TEXT("%s's coming turns"), *JobName(Unit)), Left + Pad, Top + Pad - 2.0f * S, Dim, Font, Small);
	const FLinearColor Side = SideColour(From.IsFriend(Unit));
	auto Dashes = [this](float BX, float BY, float BW, float BH, const FLinearColor& Colour, float Thick)
	{
		const float Run = 5.0f * S;
		const float Skip = 4.0f * S;
		for (float Along = 0.0f; Along < BW; Along += Run + Skip)
		{
			const float Piece = FMath::Min(Run, BW - Along);
			DrawRect(Colour, BX + Along, BY, Piece, Thick);
			DrawRect(Colour, BX + Along, BY + BH - Thick, Piece, Thick);
		}
		for (float Along = 0.0f; Along < BH; Along += Run + Skip)
		{
			const float Piece = FMath::Min(Run, BH - Along);
			DrawRect(Colour, BX, BY + Along, Thick, Piece);
			DrawRect(Colour, BX + BW - Thick, BY + Along, Thick, Piece);
		}
	};
	const FString Who = Initials(JobName(Unit));
	for (int32 Turn = 1; Turn <= 3; ++Turn)
	{
		const float CX = Left + Pad + (Turn - 1) * Column;
		const float GX = CX + (Column - Ghost) * 0.5f;
		const float GY = Top + Pad + Heading;
		TArray<FTMBack> Here;
		for (const FTMBack& Back : Backs)
		{
			if (Back.Turn == Turn)
			{
				Here.Add(Back);
			}
		}
		DrawRect(FLinearColor(0.05f, 0.06f, 0.09f, 0.8f), GX, GY, Ghost, Ghost);
		Dashes(GX, GY, Ghost, Ghost, Here.Num() > 0 ? Side : FLinearColor(0.63f, 0.67f, 0.75f, 0.35f), 2.0f * S);
		const FVector2D WhoSize = TextSize(Who, Font, Small);
		Text(Who, GX + (Ghost - WhoSize.X) * 0.5f, GY + 4.0f * S, Dim, Font, Small);
		const float Tile = 18.0f * S;
		const float TilesW = Here.Num() * (Tile + 2.0f * S) - 2.0f * S;
		FString Names;
		bool bAnyPreview = false;
		for (int32 i = 0; i < Here.Num(); ++i)
		{
			const TMSim::FAbility* Ability = Unit.Ability(Here[i].Slot);
			if (!Ability)
			{
				continue;
			}
			const float TX = GX + (Ghost - TilesW) * 0.5f + i * (Tile + 2.0f * S);
			const float TY = GY + Ghost - Tile - 4.0f * S;
			const FLinearColor TileEdge = Here[i].bPreview ? FLinearColor(Gold.R, Gold.G, Gold.B, Beat) : FLinearColor(1.0f, 1.0f, 1.0f, 0.3f);
			Panel(TX - 1.0f * S, TY - 1.0f * S, Tile + 2.0f * S, Tile + 2.0f * S, FLinearColor(0.0f, 0.0f, 0.0f, 0.9f), TileEdge, Here[i].bPreview ? 2.0f : 1.0f);
			if (UTexture2D* Picture0 = AbilityIcon(*Ability, false))
			{
				Picture(Picture0, TX, TY, Tile, Tile);
			}
			if (!Names.IsEmpty())
			{
				Names += TEXT(", ");
			}
			Names += UTF8_TO_TCHAR(Ability->Name.c_str());
			if (Here[i].bPreview)
			{
				Names += TEXT(" (if used now)");
				bAnyPreview = true;
			}
		}
		const float Seconds = TurnInSeconds(Battle, Unit, Turn);
		const FString When = Turn == 1 && !Unit.bReady ? FString::Printf(TEXT("next, %.1f s"), Seconds) : FString::Printf(TEXT("in %.0f s"), Seconds);
		const FVector2D WhenSize = TextSize(When, Font, Small);
		Text(When, CX + (Column - WhenSize.X) * 0.5f, GY + Ghost + 3.0f * S, TextColour, Font, Small);
		const FString What = Here.Num() > 0 ? Names + TEXT(" back") : (Turn == 1 ? FString(TEXT("nothing new")) : FString());
		const FLinearColor WhatColour = bAnyPreview ? Gold : (Here.Num() > 0 ? TextColour : Dim);
		const TArray<FString> Lines = Wrap(What, Font, Smaller, Column - 6.0f * S);
		float LineY = GY + Ghost + 3.0f * S + WhenSize.Y;
		for (int32 i = 0; i < Lines.Num() && i < 2; ++i)
		{
			const FVector2D LineSize = TextSize(Lines[i], Font, Smaller);
			Text(Lines[i], CX + (Column - LineSize.X) * 0.5f, LineY, WhatColour, Font, Smaller);
			LineY += LineSize.Y;
		}
	}
}

// ------------------------------------------------------------- the boss bar

void ATMBattleHud::DrawBossBar(ATMBattleDirector& From)
{
	// "Camps and Bosses Mockups" B, C and D in one panel: the boss, its health
	// (with the claim on, split into each side's share), its wind-up and the
	// stagger that breaks it, and whom it hunts and remembers (the hunt on).
	// v23 play test: shown only while the pointer is on the boss (on the board,
	// its turn chip or square, or the bar itself), and out of the way, sitting
	// on top of the enemy panel bottom right rather than mid-screen. What it
	// announces (a hunt, a claim) is in the log.
	if (From.Screen != ATMBattleDirector::EScreen::Battle)
	{
		BossBarUnitId = -1;
		BossBarRect = FBox2D(ForceInit);
		return;
	}
	const TMSim::FBattle& Battle = From.Battle;
	auto BossInfo = [&From](const TMSim::FUnit* Each) -> const TMSim::FMonsterInfo*
	{
		const TMSim::FMonsterInfo* Its = Each && Each->bMonster ? Each->MonsterInfo() : nullptr;
		return Its && Its->Tier >= 3 && Each->IsAlive() && !Each->bOffBoard && From.IsSeen(*Each) ? Its : nullptr;
	};
	const TMSim::FUnit* Boss = nullptr;
	const TMSim::FMonsterInfo* Info = nullptr;
	const bool bOnBar = BossBarUnitId >= 0 && BossBarRect.bIsValid && BossBarRect.IsInside(MousePoint());
	for (const int32 Id : { From.HoverUnitId, From.HudHoverUnitId, bOnBar ? BossBarUnitId : -1 })
	{
		const TMSim::FUnit* Each = Id >= 0 ? From.Battle.FindUnit(Id) : nullptr;
		if (const TMSim::FMonsterInfo* Its = BossInfo(Each))
		{
			Boss = Each;
			Info = Its;
			break;
		}
	}
	if (!Boss && From.bEditingLayout)
	{
		// While moving panels, any boss in sight, so the bar can be placed.
		for (const TMSim::FUnit& Each : Battle.Units)
		{
			if (const TMSim::FMonsterInfo* Its = BossInfo(&Each))
			{
				Boss = &Each;
				Info = Its;
				break;
			}
		}
	}
	BossBarUnitId = -1;
	BossBarRect = FBox2D(ForceInit);
	if (!Boss)
	{
		return;
	}
	const FPanelScale Sized(*this, TEXT("boss_hover"));
	UFont* Font = GEngine->GetMediumFont();
	const bool bClaim = Battle.Tuning.BossClaim >= 0.5;
	const bool bHunt = Battle.Tuning.BossHunt >= 0.5;
	const FVector2D Moved = Nudge(TEXT("boss_hover"));
	const float W = 460.0f * S;
	const float Pad = 10.0f * S;
	const float RowH = 20.0f * S;
	const FLinearColor Purple(0.69f, 0.49f, 1.0f);
	const FLinearColor HuntColour(1.0f, 0.54f, 0.36f);
	const FLinearColor BlueShare(0.31f, 0.5f, 0.84f);
	const FLinearColor RedShare(0.77f, 0.32f, 0.29f);

	// What it remembers, most first (the hunt).
	TArray<TPair<int32, int32>> Wrath;
	if (Boss && bHunt)
	{
		for (const std::pair<int, int>& Entry : Boss->Wrath)
		{
			Wrath.Add(TPair<int32, int32>(Entry.first, Entry.second));
		}
		Wrath.StableSort([](const TPair<int32, int32>& A, const TPair<int32, int32>& B) { return A.Value > B.Value; });
		if (Wrath.Num() > 3)
		{
			Wrath.SetNum(3);
		}
	}
	const bool bStagger = Boss && Info && (Info->Has(TMSim::MonsterTrait::Stagger) || Boss->IsCasting());
	float H = 2.0f * Pad;
	if (Boss)
	{
		H += RowH + 16.0f * S + 6.0f * S;
		H += bStagger ? RowH : 0.0f;
		H += Boss->IsCasting() ? RowH : 0.0f;
		H += bHunt ? RowH * (1 + Wrath.Num()) : 0.0f;
		H += bClaim ? RowH : 0.0f;
	}
	// On top of the enemy panel, its right edges lined up; bottom right without one.
	const float RightEdge = (InspectRight > 0.0f ? InspectRight : Canvas->ClipX - 16.0f * S) + Moved.X;
	const float Bottom = (InspectTop > 0.0f ? InspectTop - 8.0f * S : Canvas->ClipY - 16.0f * S) + Moved.Y;
	const float Left = FMath::Max(8.0f * S, RightEdge - W);
	const float Top = FMath::Max(8.0f * S, Bottom - H);
	BossBarUnitId = Boss->Id;
	BossBarRect = FBox2D(FVector2D(Left, Top), FVector2D(Left + W, Top + H));
	Panel(Left, Top, W, H, FLinearColor(0.03f, 0.03f, 0.06f, 0.92f), FLinearColor(Purple.R, Purple.G, Purple.B, 0.6f), 1.5f);
	float Y = Top + Pad;
	const float Small = 0.46f * S;
	if (Boss)
	{
		// Its name and phase, and with the claim on, each side's share.
		Text(JobName(*Boss), Left + Pad, Y, Gold, Font, 0.58f * S);
		const int32 Max = FMath::Max(1, Boss->MaxHp());
		FString Right = FString::Printf(TEXT("Boss  ·  phase %d"), Boss->Phase + 1);
		if (bClaim)
		{
			Right = FString::Printf(TEXT("Blue %d%%  ·  Red %d%%  ·  %d%% left"), FMath::Min(100, Boss->Claim[0] * 100 / Max),
				FMath::Min(100, Boss->Claim[1] * 100 / Max), Boss->Hp * 100 / Max);
		}
		const FVector2D RightSize = TextSize(Right, Font, Small);
		Text(Right, Left + W - Pad - RightSize.X, Y + 2.0f * S, TextColour, Font, Small);
		Y += RowH + 2.0f * S;
		// Its health: the shares of what is gone, then what is left.
		const float BarW = W - 2.0f * Pad;
		const float BarH = 14.0f * S;
		DrawRect(FLinearColor(0.08f, 0.06f, 0.12f, 1.0f), Left + Pad, Y, BarW, BarH);
		const float LeftShare = FMath::Clamp(static_cast<float>(Boss->Hp) / Max, 0.0f, 1.0f);
		if (bClaim)
		{
			float BlueW = static_cast<float>(Boss->Claim[0]) / Max;
			float RedW = static_cast<float>(Boss->Claim[1]) / Max;
			const float Gone = 1.0f - LeftShare;
			if (BlueW + RedW > Gone && BlueW + RedW > 0.0f)
			{
				const float Fit = Gone / (BlueW + RedW);
				BlueW *= Fit;
				RedW *= Fit;
			}
			DrawRect(BlueShare, Left + Pad, Y, BarW * BlueW, BarH);
			DrawRect(RedShare, Left + Pad + BarW * BlueW, Y, BarW * RedW, BarH);
			DrawRect(FLinearColor(0.42f, 0.36f, 0.5f, 1.0f), Left + Pad + BarW * (1.0f - LeftShare), Y, BarW * LeftShare, BarH);
		}
		else
		{
			DrawRect(Purple, Left + Pad, Y, BarW * LeftShare, BarH);
		}
		Y += BarH + 6.0f * S;
		if (bStagger)
		{
			// Hits from behind, of the three that stagger it (and break a wind-up).
			Text(TEXT("Stagger"), Left + Pad, Y, Gold, Font, Small);
			const float PipW = 26.0f * S;
			for (int32 Pip = 0; Pip < TMSim::Camp::StaggerHits; ++Pip)
			{
				const float PX = Left + Pad + 70.0f * S + Pip * (PipW + 4.0f * S);
				if (Pip < Boss->Stagger)
				{
					DrawRect(Gold, PX, Y + 5.0f * S, PipW, 8.0f * S);
				}
				else
				{
					Panel(PX, Y + 5.0f * S, PipW, 8.0f * S, FLinearColor::Transparent, Gold, 1.0f);
				}
			}
			const FString Hint = Boss->IsCasting()
				? (Boss->Stagger >= TMSim::Camp::StaggerHits - 1 ? FString(TEXT("one more from behind breaks the wind-up"))
					: FString(TEXT("hits from behind break the wind-up")))
				: FString(TEXT("three from behind stagger it"));
			Text(Hint, Left + Pad + 70.0f * S + TMSim::Camp::StaggerHits * (PipW + 4.0f * S) + 6.0f * S, Y, TextColour, Font, Small);
			Y += RowH;
		}
		if (Boss->IsCasting())
		{
			const TMSim::FAbility* Winding = Boss->Ability(Boss->Casting.Slot);
			Text(FString::Printf(TEXT("Winding up %hs: it lands in %.1f s"), Winding ? Winding->Name.c_str() : "something", Boss->Casting.Ticks / Tps),
				Left + Pad, Y, Purple, Font, Small);
			Y += RowH;
		}
		if (bHunt)
		{
			const TMSim::FUnit* Prey = From.Battle.FindUnit(Boss->HuntTarget);
			Text(Prey ? FString::Printf(TEXT("Hunts %s"), *From.NameOf(Prey->Id)) : FString(TEXT("Hunts nobody yet: it remembers who hurts it")),
				Left + Pad, Y, HuntColour, Font, Small);
			Y += RowH;
			const int32 Most = Wrath.Num() > 0 ? FMath::Max(1, Wrath[0].Value) : 1;
			for (const TPair<int32, int32>& Entry : Wrath)
			{
				const TMSim::FUnit* Who = From.Battle.FindUnit(Entry.Key);
				const FLinearColor Side = Who ? SideColour(From.IsFriend(*Who)) : TextColour;
				Text(From.NameOf(Entry.Key), Left + Pad + 12.0f * S, Y, Side, Font, Small);
				const float RowBarX = Left + Pad + 190.0f * S;
				const float RowBarW = W - 2.0f * Pad - 190.0f * S - 50.0f * S;
				DrawRect(FLinearColor(0.1f, 0.1f, 0.1f, 1.0f), RowBarX, Y + 6.0f * S, RowBarW, 8.0f * S);
				DrawRect(FLinearColor(HuntColour.R, HuntColour.G, HuntColour.B, Entry.Key == Boss->HuntTarget ? 1.0f : 0.55f),
					RowBarX, Y + 6.0f * S, RowBarW * Entry.Value / Most, 8.0f * S);
				Text(FString::FromInt(Entry.Value), RowBarX + RowBarW + 8.0f * S, Y, TextColour, Font, Small);
				Y += RowH;
			}
		}
		if (bClaim)
		{
			Text(FString::Printf(TEXT("Last blow: the Boss's Boon (+10%% damage, %d turns)  ·  %d%% share: a rare item"),
				TMSim::Camp::BoonTurns, TMSim::Camp::ClaimSharePercent), Left + Pad, Y, Dim, Font, 0.42f * S);
			Y += RowH;
		}
		AddTip(Left, Top, W, H, FString::Printf(TEXT("%s, the boss.%s%s%s"), *JobName(*Boss),
			bStagger ? TEXT("\nHits from behind count towards a stagger; three stagger it, and break a wind-up.") : TEXT(""),
			bHunt ? TEXT("\nBosses hunt: it goes for whoever has hurt it most, until they fall or it loses sight of them for 3 of its turns.") : TEXT(""),
			bClaim ? TEXT("\nClaim the boss: the side that lands the last blow gets the Boss's Boon; the other side, with 30% of its health dealt, a rare item.") : TEXT("")));
	}
	Movable(TEXT("boss_hover"), TEXT("Boss bar"), Left, Top, W, H);
}

// ------------------------------------------------------------- squad strip

void ATMBattleHud::DrawSquadStrip(ATMBattleDirector& From)
{
	// "Squad Strip Mockups" C (2026-10-02): your squad down the left edge, under
	// the log. At rest each unit is a health ring round its face and its status
	// icons, each with the turns it has left. The unit acting now, and one pointed
	// at -- here, on its turn square or on the board -- opens to its full row:
	// name, move and act left, health as a number, cooldowns. Pointing at a row
	// lights its unit and its square up, as pointing at a square does.
	SquadBottom = 0.0f;
	if (!FTMSettings::Get().bSquadStrip)
	{
		SquadHoverId = -1;
		return;
	}
	const FPanelScale Sized(*this, TEXT("squad"));
	const TMSim::FBattle& Battle = From.Battle;
	UFont* Font = GEngine->GetMediumFont();
	const FVector2D Mouse = MousePoint();
	const int32 Team = From.FriendTeam();

	// The order of its turn squares, then anyone else by id.
	TArray<const TMSim::FUnit*> Squad;
	for (int32 Id : FTMSettings::Get().CardOrder[Team])
	{
		for (const TMSim::FUnit& Each : Battle.Units)
		{
			if (Each.Id == Id && Each.Team == Team && (Each.IsAlive() || Each.IsKo()))
			{
				Squad.AddUnique(&Each);
			}
		}
	}
	for (const TMSim::FUnit& Each : Battle.Units)
	{
		if (Each.Team == Team && (Each.IsAlive() || Each.IsKo()))
		{
			Squad.AddUnique(&Each);
		}
	}
	if (Squad.Num() == 0)
	{
		SquadHoverId = -1;
		return;
	}

	const float Base = 12.0f * S + (2.0f * RowHeight + (FTMSettings::Get().bTurnSquares ? SquareLane : 2.0f * PinLane)) * S + 18.0f * S;
	const FVector2D Moved = Nudge(TEXT("squad"));
	const float X = 16.0f * S + Moved.X;
	const float Top = (From.bShowLog ? LogBottom + 16.0f * S : Base) + Moved.Y;
	const float Gap = 6.0f * S;
	const float SmallH = 50.0f * S;
	const float OpenW = 300.0f * S;
	const float OpenH = 86.0f * S;
	const FLinearColor Ground(0.03f, 0.05f, 0.08f, 0.9f);

	// A ring of short lines, from the top clockwise, Part of the way round.
	auto Ring = [this](float CX, float CY, float Radius, float Part, const FLinearColor& Colour, float Thick)
	{
		constexpr int32 Sides = 48;
		const int32 Upto = FMath::Clamp(FMath::RoundToInt(Sides * Part), 0, Sides);
		for (int32 k = 0; k < Upto; ++k)
		{
			const float A0 = -0.5f * PI + 2.0f * PI * k / Sides;
			const float A1 = -0.5f * PI + 2.0f * PI * (k + 1) / Sides;
			DrawLine(CX + FMath::Cos(A0) * Radius, CY + FMath::Sin(A0) * Radius, CX + FMath::Cos(A1) * Radius, CY + FMath::Sin(A1) * Radius, Colour, Thick);
		}
	};
	auto Outline = [this](float RX, float RY, float RW, float RH, const FLinearColor& Colour, float Thick)
	{
		DrawRect(Colour, RX, RY, RW, Thick);
		DrawRect(Colour, RX, RY + RH - Thick, RW, Thick);
		DrawRect(Colour, RX, RY, Thick, RH);
		DrawRect(Colour, RX + RW - Thick, RY, Thick, RH);
	};

	int32 HoveredNow = -1;
	float Y = Top;
	float Widest = 0.0f;
	for (const TMSim::FUnit* Unit : Squad)
	{
		const bool bDown = !Unit->IsAlive();
		const bool bLinked = TurnLinked(From, Unit->Id) || Unit->Id == SquadHoverId;
		const bool bOpen = !bDown && (Unit->Id == From.SelectedId || Unit->bReady || bLinked);
		const float HpPart = FMath::Clamp(static_cast<float>(Unit->Hp) / FMath::Max(1, Unit->MaxHp()), 0.0f, 1.0f);
		const FLinearColor HpColour = HealthColour(HpPart);
		FLinearColor Edge = SideColour(true) * FLinearColor(1, 1, 1, 0.5f);
		if (Unit->bReady || bLinked)
		{
			Edge = Gold;
		}
		if (Unit->Id == From.SelectedId)
		{
			Edge = FLinearColor::White;
		}
		float RowW = 0.0f;
		float RowH = 0.0f;
		if (!bOpen)
		{
			// At rest: the ring and the icons.
			RowH = SmallH;
			const float D = 44.0f * S;
			const float Chip = 22.0f * S;
			const float ChipsW = Unit->Statuses.empty() ? 0.0f : static_cast<float>(Unit->Statuses.size()) * (Chip + Chip * 0.15f) + 4.0f * S;
			RowW = 6.0f * S + D + 8.0f * S + ChipsW;
			DrawRect(Ground, X, Y, RowW, RowH);
			Outline(X, Y, RowW, RowH, Edge * FLinearColor(1, 1, 1, bLinked ? 1.0f : 0.45f), bLinked ? 2.0f * S : 1.0f * S);
			const float CX = X + 6.0f * S + D * 0.5f;
			const float CY = Y + RowH * 0.5f;
			const float Face = D * 0.62f;
			DrawRect(FLinearColor(0.02f, 0.03f, 0.05f, 1.0f), CX - Face * 0.5f, CY - Face * 0.5f, Face, Face);
			if (UTexture* Film = From.CardPortrait(*Unit))
			{
				Picture(Film, CX - Face * 0.5f, CY - Face * 0.5f, Face, Face, bDown ? FLinearColor(0.35f, 0.35f, 0.38f, 1.0f) : FLinearColor::White);
			}
			Ring(CX, CY, D * 0.5f - 2.5f * S, 1.0f, FLinearColor(0.1f, 0.11f, 0.14f, 1.0f), 4.0f * S);
			Ring(CX, CY, D * 0.5f - 2.5f * S, HpPart, HpColour, 4.0f * S);
			if (bDown)
			{
				const FString Down = FString::Printf(TEXT("DOWN %.0fs"), Unit->KoTicks / Tps);
				const FVector2D DownSize = TextSize(Down, Font, 0.36f * S);
				Text(Down, CX - DownSize.X * 0.5f, CY - DownSize.Y * 0.5f, Urgent, Font, 0.36f * S);
			}
			StatusChips(*Unit, X + 6.0f * S + D + 8.0f * S, Y + (RowH - Chip) * 0.5f, Chip, false, true);
			AddTip(X, Y, 6.0f * S + D + 8.0f * S, RowH, FString::Printf(TEXT("%s %d  hp %d/%d%s"), *JobName(*Unit), Unit->Id, Unit->Hp, Unit->MaxHp(),
				bDown ? TEXT("\nDown: raise it before the time runs out") : TEXT("")));
		}
		else
		{
			// Open: the full row.
			RowW = OpenW;
			RowH = OpenH;
			DrawRect(Ground, X, Y, RowW, RowH);
			Outline(X, Y, RowW, RowH, Edge, 2.0f * S);
			const float Face = 58.0f * S;
			const float FX = X + 8.0f * S;
			const float FY = Y + (RowH - Face) * 0.5f;
			DrawRect(SideColour(true), FX - 2.0f * S, FY - 2.0f * S, Face + 4.0f * S, Face + 4.0f * S);
			DrawRect(FLinearColor(0.02f, 0.03f, 0.05f, 1.0f), FX, FY, Face, Face);
			if (UTexture* Film = From.CardPortrait(*Unit))
			{
				Picture(Film, FX, FY, Face, Face);
			}
			const float Crest = 18.0f * S;
			DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.6f), FX, FY, Crest, Crest);
			Picture(ClassIcon(*Unit), FX, FY, Crest, Crest);

			const float IX = FX + Face + 10.0f * S;
			const float IW = X + RowW - 8.0f * S - IX;
			// The name, and what it has left this turn (move, act) on the right.
			Text(JobName(*Unit), IX, Y + 5.0f * S, SideColour(true) * FLinearColor(1.3f, 1.3f, 1.3f, 1.0f), Font, 0.5f * S);
			TurnPips(*Unit, X + RowW - 30.0f * S, Y + 2.0f * S, 0.8f);
			AddTip(X + RowW - 56.0f * S, Y + 2.0f * S, 52.0f * S, 22.0f * S,
				FString::Printf(TEXT("Move %s, act %s this turn"), Unit->bMoved ? TEXT("used") : TEXT("left"), Unit->bActed ? TEXT("used") : TEXT("left")));

			// Health as a bar and a number, and when it acts.
			const float BarY = Y + 33.0f * S;
			const float BarW = IW * 0.56f;
			DrawRect(FLinearColor(0.08f, 0.09f, 0.12f, 1.0f), IX, BarY, BarW, 10.0f * S);
			DrawRect(HpColour, IX, BarY, BarW * HpPart, 10.0f * S);
			const FString Hp = FString::Printf(TEXT("%d/%d"), Unit->Hp, Unit->MaxHp());
			const FVector2D HpSize = TextSize(Hp, Font, 0.42f * S);
			Text(Hp, IX + BarW + 6.0f * S, BarY + 5.0f * S - HpSize.Y * 0.5f, FLinearColor::White, Font, 0.42f * S);
			AddTip(IX, BarY - 3.0f * S, BarW + 6.0f * S + HpSize.X, 16.0f * S, FString::Printf(TEXT("Health %d of %d"), Unit->Hp, Unit->MaxHp()));
			FString When;
			FLinearColor WhenColour = Dim;
			if (Unit->bReady)
			{
				When = TEXT("NOW");
				WhenColour = Gold;
			}
			else if (Unit->IsCasting())
			{
				When = FString::Printf(TEXT("%.1fs"), Unit->Casting.Ticks / Tps);
				WhenColour = CastColour;
			}
			else
			{
				When = FString::Printf(TEXT("%.1fs"), Battle.TicksToReady(*Unit) / Tps);
			}
			const FVector2D WhenSize = TextSize(When, Font, 0.42f * S);
			Text(When, X + RowW - 8.0f * S - WhenSize.X, BarY + 5.0f * S - WhenSize.Y * 0.5f, WhenColour, Font, 0.42f * S);
			AddTip(X + RowW - 8.0f * S - WhenSize.X, BarY - 3.0f * S, WhenSize.X, 16.0f * S, ExplainCountdown(From, *Unit));

			// Its statuses with turns left; its abilities on the right, lit when
			// ready, the turns until each is back when not, gold for the one aimed.
			const float Chip = 20.0f * S;
			const float RowB = Y + RowH - Chip - 7.0f * S;
			const TArray<FTMBack> Backs = ComingBack(From, *Unit);
			const float Box = 16.0f * S;
			int32 Slots = 0;
			for (int32 Slot = 0; Slot < 4; ++Slot)
			{
				Slots += Unit->Ability(Slot) ? 1 : 0;
			}
			float BX = X + RowW - 8.0f * S - Slots * (Box + 3.0f * S) + 3.0f * S;
			const float CoolsX = BX;
			for (int32 Slot = 0; Slot < 4; ++Slot)
			{
				const TMSim::FAbility* Ability = Unit->Ability(Slot);
				if (!Ability)
				{
					continue;
				}
				int32 Back = 0;
				bool bAimed = false;
				for (const FTMBack& Each : Backs)
				{
					if (Each.Slot == Slot)
					{
						Back = Each.Turn;
						bAimed = Each.bPreview;
					}
				}
				const float BY = RowB + (Chip - Box) * 0.5f;
				if (Back == 0)
				{
					DrawRect(FLinearColor(0.56f, 0.72f, 1.0f, 1.0f), BX, BY, Box, Box);
					DrawRect(FLinearColor(0.16f, 0.26f, 0.45f, 1.0f), BX + 1.5f * S, BY + 1.5f * S, Box - 3.0f * S, Box - 3.0f * S);
				}
				else
				{
					DrawRect(bAimed ? Gold : FLinearColor(0.35f, 0.37f, 0.42f, 1.0f), BX, BY, Box, Box);
					DrawRect(FLinearColor(0.06f, 0.07f, 0.09f, 1.0f), BX + 1.0f * S, BY + 1.0f * S, Box - 2.0f * S, Box - 2.0f * S);
					const FString Num1 = FString::FromInt(Back);
					const FVector2D NumSize = TextSize(Num1, Font, 0.32f * S);
					Text(Num1, BX + (Box - NumSize.X) * 0.5f, BY + (Box - NumSize.Y) * 0.5f, bAimed ? Gold : Dim, Font, 0.32f * S, false);
				}
				AddTip(BX, BY, Box, Box, Back == 0
					? FString::Printf(TEXT("%hs: ready"), Ability->Name.c_str())
					: bAimed ? FString::Printf(TEXT("%hs: used now, back on its turn %d from now"), Ability->Name.c_str(), Back)
					: FString::Printf(TEXT("%hs: back on its turn %d from now"), Ability->Name.c_str(), Back));
				BX += Box + 3.0f * S;
			}
			const float ChipRoom = FMath::Max(0.0f, CoolsX - 8.0f * S - IX);
			const int32 Fit = FMath::Max(1, FMath::FloorToInt((ChipRoom + Chip * 0.15f) / (Chip * 1.15f)));
			StatusChips(*Unit, IX, RowB, Chip, false, true, Fit);
		}
		// Its queue ("Queued orders" B, 2026-10-06): what it will do, on a tab beside
		// its row, with an x that cancels it all -- no selecting, from anywhere.
		const FString Queued = bDown ? FString() : From.QueueSummary(*Unit);
		if (!Queued.IsEmpty())
		{
			const float TabH = FMath::Min(RowH, 32.0f * S);
			const float TabX = X + RowW + 4.0f * S;
			const float TabY = Y + (RowH - TabH) * 0.5f;
			const float WordScale = 0.42f * S;
			const FVector2D WordSize = TextSize(Queued, Font, WordScale);
			const float Close = TabH - 10.0f * S;
			const float TabW = 10.0f * S + WordSize.X + 8.0f * S + Close + 5.0f * S;
			DrawRect(Ground, TabX, TabY, TabW, TabH);
			Outline(TabX, TabY, TabW, TabH, Gold * FLinearColor(1.0f, 1.0f, 1.0f, 0.75f), 1.0f * S);
			Text(Queued, TabX + 10.0f * S, TabY + (TabH - WordSize.Y) * 0.5f, Gold, Font, WordScale);
			const float CloseX = TabX + TabW - 5.0f * S - Close;
			const float CloseY = TabY + 5.0f * S;
			const bool bOverClose = FBox2D(FVector2D(CloseX, CloseY), FVector2D(CloseX + Close, CloseY + Close)).IsInside(Mouse);
			DrawRect(bOverClose ? FLinearColor(0.55f, 0.16f, 0.14f, 1.0f) : FLinearColor(0.23f, 0.11f, 0.11f, 1.0f), CloseX, CloseY, Close, Close);
			Outline(CloseX, CloseY, Close, Close, FLinearColor(1.0f, 0.48f, 0.42f, 1.0f), 1.0f * S);
			const float Inset = Close * 0.3f;
			const FLinearColor Cross(1.0f, 0.8f, 0.76f, 1.0f);
			DrawLine(CloseX + Inset, CloseY + Inset, CloseX + Close - Inset, CloseY + Close - Inset, Cross, 2.0f * S);
			DrawLine(CloseX + Close - Inset, CloseY + Inset, CloseX + Inset, CloseY + Close - Inset, Cross, 2.0f * S);
			// The whole tab is the unit's card for a right click (TMBattleDirector.cpp); its x is a left click.
			AddButton(TabX, TabY, TabW, TabH, ETMHudAction::PickUnit, Unit->Id);
			AddButton(CloseX, CloseY, Close, Close, ETMHudAction::QueueCancel, Unit->Id);
			const FString CancelKey = FTMSettings::Get().KeyName(ETMAction::PlanCancel);
			AddTip(CloseX, CloseY, Close, Close, FString::Printf(TEXT("Cancel %s's queue (or right-click its card; %s on the selected unit; Shift+%s: every unit's)"),
				*JobName(*Unit), *CancelKey, *CancelKey));
			// Pointing at the tab lights the unit and its way, as pointing at its row does.
			if (FBox2D(FVector2D(TabX, TabY), FVector2D(TabX + TabW, TabY + TabH)).IsInside(Mouse))
			{
				HoveredNow = Unit->Id;
			}
			Widest = FMath::Max(Widest, RowW + 4.0f * S + TabW);
		}
		AddButton(X, Y, RowW, RowH, ETMHudAction::PickUnit, Unit->Id);
		if (FBox2D(FVector2D(X, Y), FVector2D(X + RowW, Y + RowH)).IsInside(Mouse))
		{
			HoveredNow = Unit->Id;
		}
		Widest = FMath::Max(Widest, RowW);
		Y += RowH + Gap;
	}
	const float H = Y - Gap - Top;
	Movable(TEXT("squad"), TEXT("Squad strip"), X, Top, FMath::Max(Widest, 120.0f * S), H);
	SquadBottom = Top + H;
	SquadHoverId = HoveredNow;
	if (HoveredNow >= 0)
	{
		From.HudHoverUnitId = HoveredNow;
	}
}

// ------------------------------------------------------------- open odds

float ATMBattleHud::OddsCard(const TMSim::FUnit& Target, const TMSim::FOdds& Odds, bool bFriend, const FString& Extra, float CX, float Bottom)
{
	// "Open Odds Mockups" A (2026-10-02): every way the blow can land, how likely,
	// and for how much, from the same OddsOf the dice follow.
	UFont* Font = GEngine->GetMediumFont();
	const float W = 260.0f * S;
	const float Pad = 8.0f * S;
	const float Inner = W - 2.0f * Pad;
	const float HpBarH = 9.0f * S;
	const float OutcomeH = 20.0f * S;
	const float LineH = 17.0f * S;
	const float H = Pad + LineH + 4.0f * S + HpBarH + 6.0f * S + OutcomeH + 4.0f * S + LineH + 2.0f * S + LineH + Pad;
	const float X = CX - W * 0.5f;
	const float Y = Bottom - H - 6.0f * S;
	const FLinearColor HitColour(0.79f, 0.33f, 0.29f);
	const FLinearColor GrazeColour(0.54f, 0.42f, 0.38f);
	const FLinearColor DodgeColour(0.25f, 0.27f, 0.31f);
	const FLinearColor Ink(0.16f, 0.11f, 0.02f);
	Panel(X, Y, W, H, FLinearColor(0.02f, 0.03f, 0.05f, 0.93f), FLinearColor(1, 1, 1, 0.22f), 1.0f);
	// A stem down to the unit.
	DrawRect(FLinearColor(1, 1, 1, 0.3f), CX - 1.0f, Y + H, 2.0f, Bottom - (Y + H));

	// Whose it is, and its health.
	float Row = Y + Pad;
	int32 Soak = 0;
	for (const TMSim::FStatus& Status : Target.Statuses)
	{
		const TMSim::FStatusDef* Def = TMSim::FindStatus(Status.Id);
		Soak += Def && Def->bAbsorbs ? Status.Amount : 0;
	}
	Text(JobName(Target), X + Pad, Row, SideColour(bFriend) * FLinearColor(1.4f, 1.4f, 1.4f, 1.0f), Font, 0.44f * S);
	const FString Hp = Soak > 0 ? FString::Printf(TEXT("%d/%d  +%d"), Target.Hp, Target.MaxHp(), Soak) : FString::Printf(TEXT("%d/%d"), Target.Hp, Target.MaxHp());
	const FVector2D HpSize = TextSize(Hp, Font, 0.4f * S);
	Text(Hp, X + W - Pad - HpSize.X, Row + 1.0f * S, TextColour, Font, 0.4f * S);
	Row += LineH + 4.0f * S;

	// The health bar, and where each outcome leaves it: red for what a hit takes,
	// gold for what a crit takes on top, a white tick where a graze leaves it.
	const float MaxHp = static_cast<float>(FMath::Max(1, Target.MaxHp()));
	auto Part = [&](int32 Amount)
	{
		// Shields soak first.
		return FMath::Clamp(static_cast<float>(FMath::Min(Target.Hp, Target.Hp + Soak - Amount)) / MaxHp, 0.0f, 1.0f);
	};
	const float Now = FMath::Clamp(Target.Hp / MaxHp, 0.0f, 1.0f);
	DrawRect(FLinearColor(0.08f, 0.09f, 0.12f, 1.0f), X + Pad, Row, Inner, HpBarH);
	DrawRect(HealthColour(Now), X + Pad, Row, Inner * Now, HpBarH);
	const float AfterHit = Part(Odds.HitAmount);
	const float AfterCrit = Part(Odds.CritAmount);
	const float AfterGraze = Part(Odds.GrazeAmount);
	DrawRect(FLinearColor(Gold.R, Gold.G, Gold.B, 0.9f), X + Pad + Inner * AfterCrit, Row, Inner * (AfterHit - AfterCrit), HpBarH);
	DrawRect(FLinearColor(HitColour.R, HitColour.G, HitColour.B, 0.95f), X + Pad + Inner * AfterHit, Row, Inner * (Now - AfterHit), HpBarH);
	if (Odds.Graze > 0.0)
	{
		DrawRect(FLinearColor::White, X + Pad + Inner * AfterGraze - 1.0f * S, Row - 3.0f * S, 2.0f * S, HpBarH + 6.0f * S);
	}
	AddTip(X + Pad, Row - 3.0f * S, Inner, HpBarH + 6.0f * S, FString::Printf(TEXT("Health %d of %d%s\nRed: what a hit takes. Gold: what a crit takes on top.%s"),
		Target.Hp, Target.MaxHp(), Soak > 0 ? *FString::Printf(TEXT(" (shields soak %d first)"), Soak) : TEXT(""),
		Odds.Graze > 0.0 ? TEXT("\nWhite tick: where a graze leaves it.") : TEXT("")));
	Row += HpBarH + 6.0f * S;

	// The four outcomes, each as wide as it is likely.
	struct FPiece
	{
		double Chance;
		FLinearColor Colour;
		FString Name;
		int32 Amount;
		bool bKo;
	};
	const FPiece Pieces[4] = {
		{ Odds.Hit, HitColour, TEXT("Hit"), Odds.HitAmount, Odds.bHitKo },
		{ Odds.Crit, Gold, TEXT("Crit"), Odds.CritAmount, Odds.bCritKo },
		{ Odds.Graze, GrazeColour, TEXT("Graze"), Odds.GrazeAmount, Odds.bGrazeKo },
		{ Odds.Dodge, DodgeColour, TEXT("Dodge"), 0, false },
	};
	float PX = X + Pad;
	for (const FPiece& Piece : Pieces)
	{
		const float PW = Inner * static_cast<float>(Piece.Chance / 100.0);
		if (PW <= 0.0f)
		{
			continue;
		}
		DrawRect(Piece.Colour, PX, Row, PW, OutcomeH);
		const FString Inside = PW > 90.0f * S ? FString::Printf(TEXT("%s %.0f%%"), *Piece.Name, Piece.Chance) : FString::Printf(TEXT("%.0f%%"), Piece.Chance);
		const FVector2D InSize = TextSize(Inside, Font, 0.36f * S);
		if (InSize.X + 6.0f * S <= PW)
		{
			Text(Inside, PX + (PW - InSize.X) * 0.5f, Row + (OutcomeH - InSize.Y) * 0.5f,
				&Piece == &Pieces[1] ? Ink : FLinearColor::White, Font, 0.36f * S, false);
		}
		AddTip(PX, Row, PW, OutcomeH, FString::Printf(TEXT("%s: %.1f%%, %d damage%s"), *Piece.Name, Piece.Chance, Piece.Amount,
			Piece.bKo ? TEXT(", knocks it out") : TEXT("")));
		PX += PW;
	}
	Row += OutcomeH + 4.0f * S;

	// The damage of each, under it; KO beside those it would not survive.
	const float Col = Inner / 4.0f;
	for (int32 i = 0; i < 4; ++i)
	{
		const FPiece& Piece = Pieces[i];
		const FString Words = i == 3 ? FString::Printf(TEXT("Dodge %.0f%%"), Piece.Chance)
			: FString::Printf(TEXT("%s -%d%s"), *Piece.Name, Piece.Amount, Piece.bKo ? TEXT(" KO") : TEXT(""));
		const FLinearColor Colour = Piece.Chance <= 0.0 ? Dim : (i == 1 ? Gold : (i == 3 ? Dim : FLinearColor(1.0f, 0.62f, 0.56f)));
		Text(Words, X + Pad + i * Col, Row, Colour, Font, 0.34f * S);
	}
	Row += LineH + 2.0f * S;

	// The chance it falls, and what comes with the blow.
	DrawRect(FLinearColor(1, 1, 1, 0.12f), X + Pad, Row - 1.0f * S, Inner, 1.0f);
	const FString Ko = FString::Printf(TEXT("KO %.0f%%"), Odds.Ko);
	Text(Ko, X + Pad, Row + 1.0f * S, Odds.Ko > 0.0 ? Gold : Dim, Font, 0.42f * S);
	AddTip(X + Pad, Row, TextSize(Ko, Font, 0.42f * S).X, LineH, FString::Printf(TEXT("Evasion %d%%: of the blows evaded, %s.\nCritical %d%% of the rest, for %d.\nThe chance it is knocked out is the sum of the outcomes it would not survive."),
		Odds.Evade, Odds.Graze > 0.0 ? TEXT("1 in 10 is dodged and the rest graze for half") : TEXT("all are dodged"),
		Odds.CritChance, Odds.CritAmount));
	if (!Extra.IsEmpty())
	{
		const FVector2D ExtraSize = TextSize(Extra, Font, 0.36f * S);
		Text(Extra, X + W - Pad - ExtraSize.X, Row + 3.0f * S, FLinearColor(0.88f, 0.6f, 1.0f), Font, 0.36f * S);
	}
	return H;
}

void ATMBattleHud::DrawThreats(ATMBattleDirector& From)
{
	// "Open Odds Mockups" C (2026-10-02): the enemy's blows read with the same
	// numbers as yours.
	const TMSim::FBattle& Battle = From.Battle;
	UFont* Font = GEngine->GetMediumFont();
	const TMSim::FUnit* Mine = From.SelectedUnit();

	// Planning a walk: which enemies reach where it ends, without walking.
	if (From.AimMode == ATMBattleDirector::EAimMode::Move && Mine && From.IsFriend(*Mine) && !From.PathShown.empty())
	{
		const TMSim::FVec2 End = From.PathShown.back();
		struct FReach
		{
			const TMSim::FUnit* Enemy;
			TMSim::FThreat Threat;
		};
		TArray<FReach> Reaching;
		int32 Walkers = 0;
		for (const TMSim::FUnit& Enemy : Battle.Units)
		{
			if (Enemy.Team == Mine->Team || !Enemy.IsAlive() || !From.IsSeen(Enemy))
			{
				continue;
			}
			const TMSim::FThreat Threat = Battle.ThreatOn(Enemy, *Mine, End);
			if (Threat.Slot < 0)
			{
				continue;
			}
			if (Threat.bMoves)
			{
				++Walkers;
			}
			else
			{
				Reaching.Add({ &Enemy, Threat });
			}
		}
		Reaching.StableSort([](const FReach& A, const FReach& B) { return A.Threat.Odds.Ko > B.Threat.Odds.Ko; });
		TArray<FString> Lines;
		double WorstKo = 0.0;
		for (int32 i = 0; i < Reaching.Num() && i < 3; ++i)
		{
			const TMSim::FAbility* Ability = Reaching[i].Enemy->Ability(Reaching[i].Threat.Slot);
			const TMSim::FOdds& Odds = Reaching[i].Threat.Odds;
			WorstKo = FMath::Max(WorstKo, Odds.Ko);
			Lines.Add(FString::Printf(TEXT("%s's %hs -%d  (%d%% evade)%s"), *JobName(*Reaching[i].Enemy), Ability ? Ability->Name.c_str() : "",
				Odds.HitAmount, Odds.Evade, Odds.Ko > 0.0 ? *FString::Printf(TEXT("  KO %.0f%%"), Odds.Ko) : TEXT("")));
		}
		if (Reaching.Num() > 3)
		{
			Lines.Add(FString::Printf(TEXT("and %d more"), Reaching.Num() - 3));
		}
		if (Walkers > 0)
		{
			Lines.Add(FString::Printf(TEXT("%d more if they walk"), Walkers));
		}
		FVector2D At;
		if (Lines.Num() > 0 && ToScreen(From, End, 60.0f, At))
		{
			const FString Head = Reaching.Num() > 0 ? FString(TEXT("Standing here, in reach of:")) : FString(TEXT("Here, no enemy reaches without walking"));
			float W = TextSize(Head, Font, 0.38f * S).X;
			for (const FString& Line : Lines)
			{
				W = FMath::Max(W, TextSize(Line, Font, 0.38f * S).X);
			}
			const float LineH = 16.0f * S;
			const float H = (Lines.Num() + 1) * LineH + 10.0f * S;
			const float X = At.X + 18.0f * S;
			const float Y = At.Y - H * 0.5f;
			Panel(X, Y, W + 16.0f * S, H, FLinearColor(0.02f, 0.03f, 0.05f, 0.9f),
				WorstKo > 0.0 ? FLinearColor(1.0f, 0.38f, 0.32f, 0.9f) : FLinearColor(0.56f, 0.78f, 1.0f, 0.8f), 1.0f);
			Text(Head, X + 8.0f * S, Y + 5.0f * S, FLinearColor(0.75f, 0.88f, 1.0f), Font, 0.38f * S);
			for (int32 i = 0; i < Lines.Num(); ++i)
			{
				Text(Lines[i], X + 8.0f * S, Y + 5.0f * S + (i + 1) * LineH, i < Reaching.Num() && i < 3 ? TextColour : Dim, Font, 0.38f * S);
			}
		}
	}

	// Pointing at an enemy, on the board or on its square or chip: its best blow
	// on each of your units. Not while aiming, which has its own forecast.
	if (From.AimMode == ATMBattleDirector::EAimMode::Ability)
	{
		return;
	}
	const TMSim::FUnit* Enemy = nullptr;
	for (const int32 Id : { From.HoverUnitId, From.HudHoverUnitId })
	{
		const TMSim::FUnit* Candidate = From.Battle.FindUnit(Id);
		if (!Enemy && Candidate && !From.IsFriend(*Candidate) && Candidate->IsAlive() && From.IsSeen(*Candidate))
		{
			Enemy = Candidate;
		}
	}
	if (!Enemy)
	{
		return;
	}
	struct FOn
	{
		const TMSim::FUnit* Target;
		TMSim::FThreat Threat;
	};
	TArray<FOn> Rows;
	for (const TMSim::FUnit& Target : Battle.Units)
	{
		if (From.IsFriend(Target) && Target.IsAlive())
		{
			Rows.Add({ &Target, Battle.ThreatOn(*Enemy, Target, Target.Pos) });
		}
	}
	if (Rows.Num() == 0)
	{
		return;
	}
	// Those it reaches from where it stands first, the likeliest to fall first.
	Rows.StableSort([](const FOn& A, const FOn& B)
	{
		const int32 RankA = A.Threat.Slot < 0 ? 2 : (A.Threat.bMoves ? 1 : 0);
		const int32 RankB = B.Threat.Slot < 0 ? 2 : (B.Threat.bMoves ? 1 : 0);
		if (RankA != RankB)
		{
			return RankA < RankB;
		}
		return A.Threat.Odds.Ko > B.Threat.Odds.Ko;
	});

	// On the board: a line to each unit it reaches, solid to the one most at risk,
	// dashed to the rest, dotted where it would have to walk first; and the odds
	// over each it reaches from where it stands.
	FVector2D EnemyAt;
	const bool bEnemyOn = ToScreen(From, Enemy->Pos, 40.0f, EnemyAt);
	const FLinearColor Threat(1.0f, 0.38f, 0.32f);
	for (int32 i = 0; i < Rows.Num(); ++i)
	{
		const FOn& Row = Rows[i];
		FVector2D TargetAt;
		if (Row.Threat.Slot < 0 || !bEnemyOn || !ToScreen(From, Row.Target->Pos, 40.0f, TargetAt))
		{
			continue;
		}
		const bool bWorst = i == 0 && !Row.Threat.bMoves;
		const FVector2D Way = TargetAt - EnemyAt;
		const float Long = Way.Size();
		if (Long < 1.0f)
		{
			continue;
		}
		const FVector2D Dir = Way / Long;
		const float Dash = bWorst ? Long : (Row.Threat.bMoves ? 4.0f * S : 12.0f * S);
		const float Gap = bWorst ? 0.0f : (Row.Threat.bMoves ? 7.0f * S : 7.0f * S);
		const FLinearColor Colour = Threat * FLinearColor(1, 1, 1, bWorst ? 1.0f : (Row.Threat.bMoves ? 0.45f : 0.75f));
		for (float Along = 0.0f; Along < Long; Along += Dash + Gap)
		{
			const FVector2D A = EnemyAt + Dir * Along;
			const FVector2D B = EnemyAt + Dir * FMath::Min(Long, Along + Dash);
			DrawLine(A.X, A.Y, B.X, B.Y, Colour, bWorst ? 3.0f * S : 2.0f * S);
		}
		// The numbers on the board only over the unit most at risk (v20 play test,
		// less text); the card top right has every one.
		FVector2D LabelAt;
		if (bWorst && FTMSettings::Get().bThreatOdds && ToScreen(From, Row.Target->Pos, 225.0f, LabelAt))
		{
			const TMSim::FAbility* Ability = Enemy->Ability(Row.Threat.Slot);
			const TMSim::FOdds& Odds = Row.Threat.Odds;
			const FString Label = Row.Threat.bMoves
				? FString::Printf(TEXT("if it walks: -%d"), Odds.HitAmount)
				: FString::Printf(TEXT("%hs -%d  %d%% evade  KO %.0f%%"), Ability ? Ability->Name.c_str() : "", Odds.HitAmount, Odds.Evade, Odds.Ko);
			const FVector2D Size = TextSize(Label, Font, 0.4f * S);
			Panel(LabelAt.X - Size.X * 0.5f - 6.0f * S, LabelAt.Y - 3.0f * S, Size.X + 12.0f * S, Size.Y + 6.0f * S,
				FLinearColor(0.02f, 0.03f, 0.05f, 0.9f), Threat * FLinearColor(1, 1, 1, bWorst ? 1.0f : 0.5f), 1.0f);
			Text(Label, LabelAt.X - Size.X * 0.5f, LabelAt.Y, Row.Threat.bMoves ? Dim : (Odds.Ko > 0.0 ? Gold : TextColour), Font, 0.4f * S);
		}
	}

	// The card, top right under the buttons: every one of your units, and what
	// it could take.
	const FPanelScale Sized(*this, TEXT("threat_card"));
	const float W = 330.0f * S;
	const float LineH = 19.0f * S;
	const float H = 12.0f * S + 22.0f * S + 16.0f * S + Rows.Num() * LineH + 22.0f * S + 8.0f * S;
	const FVector2D Moved = Nudge(TEXT("threat_card"));
	const float X = Canvas->ClipX - W - 16.0f * S + Moved.X;
	const float Y = 60.0f * S + Moved.Y;
	Movable(TEXT("threat_card"), TEXT("Enemy threat card"), X, Y, W, H);
	Panel(X, Y, W, H, FLinearColor(0.02f, 0.03f, 0.05f, 0.94f), Threat * FLinearColor(1, 1, 1, 0.7f), 1.0f);
	float Row = Y + 10.0f * S;
	Text(JobName(*Enemy), X + 12.0f * S, Row, SideColour(false) * FLinearColor(1.4f, 1.4f, 1.4f, 1.0f), Font, 0.5f * S);
	const FString When = Enemy->bReady ? FString(TEXT("acting now"))
		: FString::Printf(TEXT("acts in %.1f s"), Battle.TicksToReady(*Enemy) / Tps);
	const FVector2D WhenSize = TextSize(When, Font, 0.38f * S);
	Text(When, X + W - 12.0f * S - WhenSize.X, Row + 3.0f * S, Enemy->bReady ? Gold : Dim, Font, 0.38f * S);
	Row += 24.0f * S;
	Text(TEXT("ITS BEST BLOW ON EACH"), X + 12.0f * S, Row, Dim, Font, 0.3f * S);
	Row += 16.0f * S;
	for (const FOn& Each : Rows)
	{
		const TMSim::FAbility* Ability = Each.Threat.Slot >= 0 ? Enemy->Ability(Each.Threat.Slot) : nullptr;
		Text(JobName(*Each.Target), X + 12.0f * S, Row, SideColour(true) * FLinearColor(1.4f, 1.4f, 1.4f, 1.0f), Font, 0.38f * S);
		FString What;
		FString Ko = TEXT("-");
		FLinearColor WhatColour = TextColour;
		if (Each.Threat.Slot < 0)
		{
			What = TEXT("out of reach");
			WhatColour = Dim;
		}
		else
		{
			const TMSim::FOdds& Odds = Each.Threat.Odds;
			What = Each.Threat.bMoves
				? FString::Printf(TEXT("if it walks: %hs -%d"), Ability ? Ability->Name.c_str() : "", Odds.HitAmount)
				: FString::Printf(TEXT("%hs -%d, %d%% evade"), Ability ? Ability->Name.c_str() : "", Odds.HitAmount, Odds.Evade);
			WhatColour = Each.Threat.bMoves ? Dim : TextColour;
			Ko = FString::Printf(TEXT("%.0f%%"), Odds.Ko);
			AddTip(X, Row, W, LineH, FString::Printf(TEXT("%hs on the %s%s\nHit %.0f%% -%d, crit %.0f%% -%d, graze %.0f%% -%d, dodge %.0f%%\nKO %.0f%%"),
				Ability ? Ability->Name.c_str() : "", *JobName(*Each.Target), Each.Threat.bMoves ? TEXT(", after walking up to it") : TEXT(""),
				Odds.Hit, Odds.HitAmount, Odds.Crit, Odds.CritAmount, Odds.Graze, Odds.GrazeAmount, Odds.Dodge, Odds.Ko));
		}
		Text(What, X + 92.0f * S, Row, WhatColour, Font, 0.36f * S);
		const FVector2D KoSize = TextSize(Ko, Font, 0.38f * S);
		const bool bKo = Each.Threat.Slot >= 0 && Each.Threat.Odds.Ko > 0.0;
		Text(Ko, X + W - 12.0f * S - KoSize.X, Row, bKo ? Gold : Dim, Font, 0.38f * S);
		Row += LineH;
	}
	Text(Enemy->bReady ? TEXT("What is ready now.") : TEXT("What will be ready on its next turn."), X + 12.0f * S, Row + 4.0f * S, Dim, Font, 0.32f * S);
}

// ------------------------------------------------------- zones of control

void ATMBattleHud::DrawZoneWords(ATMBattleDirector& From)
{
	// "Zone of Control Mockups" A, B and D (2026-10-02): the ground carries the
	// rings (TMBattleDirectorIndicators.cpp); this says what they mean while a
	// walk is aimed.
	const TMSim::FUnit* Unit = From.SelectedUnit();
	if (!Unit || From.AimMode != ATMBattleDirector::EAimMode::Move || !From.PlayerCanCommand(Unit))
	{
		return;
	}
	const TMSim::FBattle& Battle = From.Battle;
	UFont* Font = GEngine->GetMediumFont();
	const FLinearColor ZoneOrange(1.0f, 0.54f, 0.36f);
	const FLinearColor ZoneBlue(0.43f, 0.63f, 1.0f);
	const float Reach = static_cast<float>(Battle.Tuning.EngageRadius);
	const bool bZones = Battle.Tuning.ZoneOfControl >= 1.0 && Reach > 0.0f;

	auto Tag = [&](const FString& Words, const FVector2D& At, const FLinearColor& Edge, const FLinearColor& Ink, float Scale)
	{
		const FVector2D Size = TextSize(Words, Font, Scale);
		Panel(At.X - Size.X * 0.5f - 6.0f * S, At.Y - 3.0f * S, Size.X + 12.0f * S, Size.Y + 6.0f * S, FLinearColor(0.02f, 0.03f, 0.05f, 0.9f), Edge, 1.0f);
		Text(Words, At.X - Size.X * 0.5f, At.Y, Ink, Font, Scale);
	};
	auto Polyline = [&](const std::vector<TMSim::FVec2>& Points, const FLinearColor& Colour, float Thick, bool bDashed)
	{
		for (size_t i = 1; i < Points.size(); ++i)
		{
			if (bDashed && i % 2 == 0)
			{
				continue;
			}
			FVector2D A;
			FVector2D B;
			if (ToScreen(From, Points[i - 1], 12.0f, A) && ToScreen(From, Points[i], 12.0f, B))
			{
				DrawLine(A.X, A.Y, B.X, B.Y, Colour, Thick);
			}
		}
	};
	auto StopMark = [&](const TMSim::FVec2& Where, const FLinearColor& Colour)
	{
		FVector2D At;
		if (ToScreen(From, Where, 12.0f, At))
		{
			const float Box = 14.0f * S;
			DrawRect(Colour, At.X - Box * 0.5f, At.Y - Box * 0.5f, Box, Box);
			const FLinearColor Ink(0.06f, 0.04f, 0.02f);
			DrawLine(At.X - Box * 0.3f, At.Y - Box * 0.3f, At.X + Box * 0.3f, At.Y + Box * 0.3f, Ink, 2.0f * S);
			DrawLine(At.X + Box * 0.3f, At.Y - Box * 0.3f, At.X - Box * 0.3f, At.Y + Box * 0.3f, Ink, 2.0f * S);
		}
	};

	// A's "HOLDS THE LINE" over each tank is gone (v20 play test, less text): the
	// shields round its zone say it. The words left are the costs of this walk.

	// B: breaking away from an enemy at the start of the walk costs.
	if (Battle.Tuning.EngageCost > 0.0 && Reach > 0.0f)
	{
		const TMSim::FVec2 Start = From.WalkStart(*Unit);
		const TMSim::FUnit* Holding = nullptr;
		for (const TMSim::FUnit& Enemy : Battle.Units)
		{
			if (!Holding && Enemy.IsAlive() && Enemy.Team != Unit->Team && From.IsSeen(Enemy) && Start.DistanceTo(Enemy.Pos) < Reach)
			{
				Holding = &Enemy;
			}
		}
		FVector2D At;
		if (Holding && ToScreen(From, Start, 60.0f, At))
		{
			Tag(FString::Printf(TEXT("-%s m breaking away"), *FString::SanitizeFloat(Battle.Tuning.EngageCost)),
				At + FVector2D(0.0f, 30.0f * S), ZoneOrange, TextColour, 0.34f * S);
		}
	}

	// B: pointing at ground a zone takes away: where a walk that way ends.
	if (From.ZoneStopBy >= 0 && !From.ZoneGhost.empty())
	{
		Polyline(From.ZoneGhost, FLinearColor(ZoneOrange.R, ZoneOrange.G, ZoneOrange.B, 0.5f), 2.0f * S, true);
		StopMark(From.ZoneStopAt, ZoneOrange);
		FVector2D At;
		if (ToScreen(From, From.ZoneStopAt, 60.0f, At))
		{
			Tag(TEXT("Stops here: a zone"), At, ZoneOrange, FLinearColor(1.0f, 0.78f, 0.68f), 0.36f * S);
		}
	}

	// D: walking a tank, the ways to your back line it would cut standing there.
	if (bZones && From.TankLanesFor == Unit->Id && !From.TankLanes.empty() && !From.PathShown.empty())
	{
		int32 Ways = 0;
		int32 Cut = 0;
		for (const TMSim::FLane& Lane : From.TankLanes)
		{
			const TMSim::FUnit* Enemy = From.Battle.FindUnit(Lane.EnemyId);
			const TMSim::FUnit* Toward = From.Battle.FindUnit(Lane.TowardId);
			if (!Enemy || !Toward)
			{
				continue;
			}
			bool bStillReaches = false;
			for (const int Reached : Lane.After)
			{
				bStillReaches = bStillReaches || Reached == Lane.TowardId;
			}
			const bool bLoses = Lane.After.size() < Lane.Before.size() && !bStillReaches;
			Ways += Lane.Before.empty() ? 0 : 1;
			Cut += bLoses ? 1 : 0;
			const FLinearColor Colour = bLoses ? ZoneBlue : SideColour(false);
			Polyline(Lane.Path, Colour, bLoses ? 3.0f * S : 2.0f * S, false);
			if (Lane.Path.empty())
			{
				continue;
			}
			if (bLoses)
			{
				StopMark(Lane.Path.back(), ZoneBlue);
			}
			// The lines and marks say it; the words per lane are gone (v20 play test).
		}
		FVector2D At;
		if (Ways > 0 && ToScreen(From, From.PathShown.back(), 120.0f, At))
		{
			Tag(FString::Printf(TEXT("Cuts %d of %d ways to your back line"), Cut, Ways),
				At, ZoneBlue, FLinearColor(0.8f, 0.88f, 1.0f), 0.38f * S);
		}
	}
}
