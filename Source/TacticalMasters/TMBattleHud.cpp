#include "TMBattleHud.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"

#include "TMBattleDirector.h"
#include "TMBattleHudStyle.h"
#include "SimAbility.h"

#include <algorithm>
#include <cmath>

using namespace TMHudStyle;

ATMBattleDirector* ATMBattleHud::FindDirector()
{
	if (!Director.IsValid())
	{
		for (TActorIterator<ATMBattleDirector> It(GetWorld()); It; ++It)
		{
			Director = *It;
			break;
		}
	}
	return Director.Get();
}

FString ATMBattleHud::JobName(const TMSim::FUnit& Unit)
{
	const TMSim::FJobDef* Job = TMSim::FindJob(Unit.Job);
	return UTF8_TO_TCHAR(Job && !Job->Name.empty() ? Job->Name.c_str() : Unit.Job.c_str());
}

// ------------------------------------------------------------------ helpers

void ATMBattleHud::Panel(float X, float Y, float W, float H, const FLinearColor& Fill, const FLinearColor& Edge, float Thickness)
{
	DrawRect(Fill, X, Y, W, H);
	if (Edge.A > 0.0f)
	{
		DrawLine(X, Y, X + W, Y, Edge, Thickness);
		DrawLine(X + W, Y, X + W, Y + H, Edge, Thickness);
		DrawLine(X + W, Y + H, X, Y + H, Edge, Thickness);
		DrawLine(X, Y + H, X, Y, Edge, Thickness);
	}
}

void ATMBattleHud::Text(const FString& What, float X, float Y, const FLinearColor& Colour, UFont* Font, float Scale, bool bShadow)
{
	if (bShadow)
	{
		DrawText(What, Shadow, X + 1.0f, Y + 1.0f, Font, Scale * FontBoost);
	}
	DrawText(What, Colour, X, Y, Font, Scale * FontBoost);
}

FVector2D ATMBattleHud::TextSize(const FString& What, UFont* Font, float Scale)
{
	float W = 0.0f;
	float H = 0.0f;
	GetTextSize(What, W, H, Font, Scale * FontBoost);
	return FVector2D(W, H);
}

void ATMBattleHud::Gauge(float X, float Y, float W, float H, float Fraction, const FLinearColor& Fill, const FString& Label)
{
	DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.55f), X, Y, W, H);
	DrawRect(Fill, X, Y, W * FMath::Clamp(Fraction, 0.0f, 1.0f), H);
	UFont* Font = GEngine->GetMediumFont();
	const float Scale = 0.55f * S;
	const FVector2D Size = TextSize(Label, Font, Scale);
	Text(Label, X + 6.0f * S, Y + (H - Size.Y) * 0.5f, TextColour, Font, Scale);
}

void ATMBattleHud::AddButton(float X, float Y, float W, float H, ETMHudAction Action, int32 Value)
{
	FTMHudButton Button;
	Button.Area = FBox2D(FVector2D(X, Y), FVector2D(X + W, Y + H));
	Button.Action = Action;
	Button.Value = Value;
	Buttons.Add(Button);
}

bool ATMBattleHud::ButtonAt(const FVector2D& Point, FTMHudButton& Out) const
{
	// Last drawn is on top, so it wins.
	for (int32 i = Buttons.Num() - 1; i >= 0; --i)
	{
		if (Buttons[i].Area.IsInside(Point))
		{
			Out = Buttons[i];
			return true;
		}
	}
	return false;
}

bool ATMBattleHud::ToScreen(ATMBattleDirector& From, const TMSim::FVec2& Point, float Lift, FVector2D& Out) const
{
	return PlayerOwner && PlayerOwner->ProjectWorldLocationToScreen(From.BoardPoint(Point, Lift), Out);
}

void ATMBattleHud::BoardRing(ATMBattleDirector& From, const TMSim::FVec2& Centre, float RadiusMetres, const FLinearColor& Colour, float Thickness)
{
	// Flat on the board at the centre's height, so a ring over a slope floats a
	// little -- which reads better than one cut into the hill.
	const int32 Steps = 48;
	FVector2D Last = FVector2D::ZeroVector;
	bool bHaveLast = false;
	for (int32 i = 0; i <= Steps; ++i)
	{
		const float Angle = 2.0f * UE_PI * i / Steps;
		const FVector World = From.BoardPoint(Centre, 8.0f)
			+ FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f) * RadiusMetres * From.TileSize;
		FVector2D Here = FVector2D::ZeroVector;
		const bool bHere = PlayerOwner && PlayerOwner->ProjectWorldLocationToScreen(World, Here);
		if (bHere && bHaveLast)
		{
			DrawLine(Last.X, Last.Y, Here.X, Here.Y, Colour, Thickness);
		}
		Last = Here;
		bHaveLast = bHere;
	}
}

// --------------------------------------------------------------------- frame

void ATMBattleHud::DrawHUD()
{
	Super::DrawHUD();
	Buttons.Reset();
	Tips.Reset();
	ATMBattleDirector* Found = FindDirector();
	if (!Canvas || !Found || !Found->bBuilt || !GEngine)
	{
		return;
	}
	S = Canvas->ClipY / 1080.0f;

	// Away from a battle, the menu is the whole screen, over the board.
	if (Found->Screen != ATMBattleDirector::EScreen::Battle)
	{
		if (Found->Screen == ATMBattleDirector::EScreen::Title)
		{
			DrawTitle(*Found);
		}
		else
		{
			DrawSetup(*Found);
		}
		// The Unit Guide can be opened from the title too, over everything.
		if (Found->bGuideOpen)
		{
			Buttons.Reset();
			Tips.Reset();
			DrawGuide(*Found);
		}
		DrawTooltip();
		return;
	}

	// Under everything else, since it is drawn onto the board.
	DrawBoardAids(*Found);
	DrawTurnOrder(*Found);
	DrawLog(*Found);
	DrawUnitCard(*Found);
	DrawActionBar(*Found);
	DrawField(*Found);
	DrawInspectCard(*Found);
	DrawCornerButtons(*Found);
	DrawBanners(*Found);
	// Over everything else, only the open overlay's buttons answer.
	if (Found->bGuideOpen)
	{
		Buttons.Reset();
		Tips.Reset();
		DrawGuide(*Found);
	}
	else if (Found->bMenuOpen)
	{
		Buttons.Reset();
		Tips.Reset();
		DrawBattleMenu(*Found);
	}
	DrawTooltip();
}

void ATMBattleHud::DrawBoardAids(ATMBattleDirector& From)
{
	// What an inspected enemy could do next: the ground it can walk to, and the
	// reach of its longest attack from where it stands (battle.gd:512-527).
	if (!From.ThreatNodes.empty())
	{
		const float Dot = FMath::Max(3.0f, 4.0f * S);
		for (const TMSim::FNode& Node : From.ThreatNodes)
		{
			FVector2D At;
			if (ToScreen(From, TMSim::FMap::NodePos(Node), 4.0f, At))
			{
				DrawRect(FLinearColor(1.0f, 0.35f, 0.3f, 0.65f), At.X - Dot * 0.5f, At.Y - Dot * 0.5f, Dot, Dot);
			}
		}
		if (const TMSim::FUnit* Enemy = From.Battle.FindUnit(From.InspectedId))
		{
			if (From.ThreatReach > 0.0f)
			{
				BoardRing(From, Enemy->Pos, From.ThreatReach, FLinearColor(1.0f, 0.4f, 0.35f, 0.85f), 2.0f);
			}
		}
	}

	const TMSim::FUnit* Unit = From.SelectedUnit();
	if (!From.PlayerCanOrder(Unit))
	{
		return;
	}

	// Whose orders these are.
	BoardRing(From, Unit->Pos, 0.45f, FLinearColor(0.35f, 0.86f, 1.0f), 3.0f);

	if (From.AimMode == ATMBattleDirector::EAimMode::Move)
	{
		// Every spot it can walk to, and the way to the one under the pointer.
		const FLinearColor Spot = From.bSprinting ? FLinearColor(1.0f, 0.67f, 0.24f, 0.9f) : FLinearColor(0.31f, 0.63f, 1.0f, 0.9f);
		const float Dot = FMath::Max(3.0f, 5.0f * S);
		for (const std::pair<TMSim::FNode, double>& Entry : From.Reachable)
		{
			FVector2D At;
			if (ToScreen(From, TMSim::FMap::NodePos(Entry.first), 4.0f, At))
			{
				DrawRect(Spot, At.X - Dot * 0.5f, At.Y - Dot * 0.5f, Dot, Dot);
			}
		}
		for (size_t i = 1; i < From.PathShown.size(); ++i)
		{
			FVector2D A;
			FVector2D B;
			if (ToScreen(From, From.PathShown[i - 1], 10.0f, A) && ToScreen(From, From.PathShown[i], 10.0f, B))
			{
				DrawLine(A.X, A.Y, B.X, B.Y, FLinearColor(1.0f, 0.9f, 0.35f), 4.0f);
			}
		}
		return;
	}

	if (From.AimMode != ATMBattleDirector::EAimMode::Ability)
	{
		return;
	}
	const TMSim::FAbility* Ability = TMSim::JobAbility(Unit->Job, From.AimSlot);
	if (!Ability)
	{
		return;
	}
	// How far it reaches, and the ring it cannot be used inside.
	if (Ability->MaxRange > 0.0f)
	{
		BoardRing(From, Unit->Pos, Ability->MaxRange, FLinearColor(0.8f, 0.8f, 0.86f, 0.9f), 2.0f);
	}
	if (Ability->MinRange > 0.0f)
	{
		BoardRing(From, Unit->Pos, Ability->MinRange, FLinearColor(0.6f, 0.35f, 0.35f, 0.9f), 2.0f);
	}

	const ATMBattleDirector::FAim Where = From.Aim();
	if (!Where.bHave)
	{
		return;
	}
	const float Radius = FMath::Max(Ability->Aoe, TMSim::Ground::HitRadius);
	BoardRing(From, Where.Point, Radius, Where.bOk ? FLinearColor(0.43f, 1.0f, 0.55f) : FLinearColor(1.0f, 0.35f, 0.31f), 3.0f);
	if (!Where.bOk)
	{
		return;
	}

	// The forecast over each unit it would reach, from the same Preview the rules
	// resolve with. It cannot say who a cast will have caught by the time it lands.
	UFont* Font = GEngine->GetMediumFont();
	const float Scale = 0.8f * S;
	for (const TMSim::FHit& Hit : From.Battle.Preview(*Unit, From.AimSlot, Unit->Pos, Where.Point))
	{
		const TMSim::FUnit* Target = From.Battle.FindUnit(Hit.UnitId);
		if (!Target)
		{
			continue;
		}
		FString Line;
		FLinearColor Colour = TextColour;
		switch (Ability->Effect)
		{
		case TMSim::EEffect::Damage:
		{
			Line = FString::Printf(TEXT("-%d"), Hit.Amount);
			const int32 Miss = From.Battle.EvadeChance(*Target, *Ability, Unit);
			if (Miss > 0)
			{
				Line += FString::Printf(TEXT("  %d%% miss"), Miss);
			}
			if (Hit.Amount >= Target->Hp)
			{
				Line += TEXT("  KO");
			}
			Colour = FLinearColor(1.0f, 0.47f, 0.37f);
			break;
		}
		case TMSim::EEffect::Heal:
			Line = FString::Printf(TEXT("+%d"), Hit.Amount);
			Colour = FLinearColor(0.47f, 1.0f, 0.53f);
			break;
		case TMSim::EEffect::Revive:
			Line = FString::Printf(TEXT("up with %d"), Hit.Amount);
			Colour = FLinearColor(1.0f, 0.95f, 0.6f);
			break;
		default:
			Line = Ability->HasStatus() ? FString(UTF8_TO_TCHAR(Ability->StatusId.c_str())) : FString(TEXT("affected"));
			Colour = FLinearColor(0.88f, 0.6f, 1.0f);
			break;
		}
		if (Ability->HasStatus() && Ability->Effect != TMSim::EEffect::Support)
		{
			Line += FString::Printf(TEXT("  +%hs"), Ability->StatusId.c_str());
		}
		FVector2D At;
		if (ToScreen(From, Target->Pos, 225.0f, At))
		{
			const FVector2D Size = TextSize(Line, Font, Scale);
			Panel(At.X - Size.X * 0.5f - 6.0f * S, At.Y - 3.0f * S, Size.X + 12.0f * S, Size.Y + 6.0f * S, FLinearColor(0.0f, 0.0f, 0.0f, 0.6f));
			Text(Line, At.X - Size.X * 0.5f, At.Y, Colour, Font, Scale);
		}
	}
}

// DrawTurnOrder, DrawLog and DrawUnitCard live in TMBattleHudPanels.cpp with the other panels.

void ATMBattleHud::DrawActionBar(ATMBattleDirector& From)
{
	// Move, Sprint, the four abilities and End Turn (hud.gd:866-909, 1429-1474).
	const TMSim::FUnit* Unit = From.SelectedUnit();
	ActionBarTop = Canvas->ClipY - 16.0f * S;
	if (!From.bPlayerInput || !Unit || !Unit->IsAlive())
	{
		return;
	}
	const bool bControllable = From.PlayerCanOrder(Unit);

	UFont* Font = GEngine->GetMediumFont();
	const float BW = 150.0f * S;
	const float BH = 58.0f * S;
	const float Gap = 6.0f * S;
	const int32 Count = 7;
	const float Total = Count * BW + (Count - 1) * Gap;
	const float CardRight = 16.0f * S + 440.0f * S + 12.0f * S;
	float X = FMath::Max(CardRight, (Canvas->ClipX - Total) * 0.5f);
	const float Y = Canvas->ClipY - BH - 16.0f * S;
	ActionBarTop = Y;

	FVector2D Mouse(-1.0f, -1.0f);
	if (PlayerOwner)
	{
		float MX = 0.0f;
		float MY = 0.0f;
		if (PlayerOwner->GetMousePosition(MX, MY))
		{
			Mouse = FVector2D(MX, MY);
		}
	}

	auto DrawButton = [&](const FString& Name, const FString& Details, bool bEnabled, bool bPressed,
		const FLinearColor& NameColour, ETMHudAction Action, int32 Value)
	{
		const bool bOver = FBox2D(FVector2D(X, Y), FVector2D(X + BW, Y + BH)).IsInside(Mouse);
		FLinearColor Fill = bEnabled ? FLinearColor(0.12f, 0.15f, 0.22f, 0.92f) : FLinearColor(0.08f, 0.09f, 0.12f, 0.8f);
		if (bEnabled && bOver)
		{
			Fill = FLinearColor(0.18f, 0.23f, 0.33f, 0.95f);
		}
		const FLinearColor Edge = bPressed ? Gold : FLinearColor(0.4f, 0.45f, 0.55f, bEnabled ? 0.8f : 0.35f);
		Panel(X, Y, BW, BH, Fill, Edge, bPressed ? 3.0f : 1.0f);
		const FLinearColor Colour = bEnabled ? NameColour : Dim;
		Text(Name, X + 8.0f * S, Y + 5.0f * S, Colour, Font, 0.62f * S);
		Text(Details, X + 8.0f * S, Y + 33.0f * S, bEnabled ? Dim : FLinearColor(1, 1, 1, 0.3f), Font, 0.5f * S);
		AddButton(X, Y, BW, BH, Action, Value);
		X += BW + Gap;
	};

	const bool bMoving = From.AimMode == ATMBattleDirector::EAimMode::Move;
	DrawButton(TEXT("Move"), TEXT("Space"),
		bControllable && !Unit->bMoved && !Unit->IsCasting(), bMoving && !From.bSprinting,
		TextColour, ETMHudAction::Move, -1);
	DrawButton(TEXT("Sprint"), FString::Printf(TEXT("Shift  %.1f m"), From.Battle.MoveOf(*Unit, true)),
		bControllable && !Unit->bMoved && !Unit->bActed && !Unit->IsCasting(), bMoving && From.bSprinting,
		TextColour, ETMHudAction::Sprint, -1);

	for (int32 Slot = 0; Slot < 4; ++Slot)
	{
		const TMSim::FAbility* Ability = TMSim::JobAbility(Unit->Job, Slot);
		if (!Ability)
		{
			X += BW + Gap;
			continue;
		}
		// What the second line says, in Godot's order of importance (hud.gd:1439-1452).
		FString Details = FString::Printf(TEXT("%d"), Slot + 1);
		if (Ability->Kind == "passive" || Ability->Kind == "aura")
		{
			Details = FString(UTF8_TO_TCHAR(Ability->Kind.c_str())).ToUpper();
		}
		if (Slot == 3 && Unit->Ult < TMSim::Pace::UltMax)
		{
			Details += FString::Printf(TEXT("  ULT %d%%"), Unit->Ult);
		}
		else if (Unit->Cooldowns[Slot] > 0)
		{
			Details += FString::Printf(TEXT("  wait %d"), Unit->Cooldowns[Slot]);
		}
		else if (Ability->Cast > 0.0f)
		{
			Details += FString::Printf(TEXT("  %.1fs"), From.Battle.CastTicks(*Ability) / Tps);
		}
		const bool bBlocked = !From.Battle.AbilityBlockedReason(*Unit, Slot).empty();
		// A ready ultimate stands out in gold.
		const FLinearColor NameColour = Slot == 3 && Unit->Ult >= TMSim::Pace::UltMax ? Gold : TextColour;
		DrawButton(UTF8_TO_TCHAR(Ability->Name.c_str()), Details,
			bControllable && !Unit->bActed && !bBlocked,
			From.AimMode == ATMBattleDirector::EAimMode::Ability && From.AimSlot == Slot,
			NameColour, ETMHudAction::Ability, Slot);
	}
	DrawButton(TEXT("End Turn"), TEXT("Enter"), bControllable, false, TextColour, ETMHudAction::EndTurn, -1);
}

void ATMBattleHud::DrawBanners(ATMBattleDirector& From)
{
	UFont* Font = GEngine->GetMediumFont();
	UFont* Big = GEngine->GetLargeFont();
	const float CentreX = Canvas->ClipX * 0.5f;

	// The hover preview, just above the action bar: what a click here would do.
	FString Preview;
	FLinearColor PreviewColour = TextColour;
	const TMSim::FUnit* Unit = From.SelectedUnit();
	FVector2D Mouse(-1.0f, -1.0f);
	if (PlayerOwner)
	{
		float MX = 0.0f;
		float MY = 0.0f;
		if (PlayerOwner->GetMousePosition(MX, MY))
		{
			Mouse = FVector2D(MX, MY);
		}
	}
	FTMHudButton Over;
	if (Unit && ButtonAt(Mouse, Over) && Over.Action == ETMHudAction::Ability)
	{
		// The mouse is over an ability: say what it is, whether or not it can be used.
		if (const TMSim::FAbility* Ability = TMSim::JobAbility(Unit->Job, Over.Value))
		{
			const std::string Blocked = From.Battle.AbilityBlockedReason(*Unit, Over.Value);
			const FString Reach = Ability->MaxRange == 0.0f
				? FString(TEXT("self"))
				: FString::Printf(TEXT("range %.1f-%.1f m"), Ability->MinRange, Ability->MaxRange);
			const FString CastText = Ability->Cast == 0.0f
				? FString(TEXT("instant"))
				: FString::Printf(TEXT("cast %.1fs"), From.Battle.CastTicks(*Ability) / Tps);
			Preview = FString::Printf(TEXT("%hs%s   %hs  %s  %s%s  cooldown %d"),
				Ability->Name.c_str(),
				Blocked.empty() ? TEXT("") : *FString::Printf(TEXT(" (%hs)"), Blocked.c_str()),
				TMSim::ShapeOf(*Ability).c_str(), *Reach, *CastText,
				Ability->Aoe > 0.0f ? *FString::Printf(TEXT("  radius %.1f m"), Ability->Aoe) : TEXT(""),
				Ability->Cooldown);
		}
	}
	else if (From.PlayerCanOrder(Unit) && From.AimMode == ATMBattleDirector::EAimMode::Move && From.bHaveHover)
	{
		const TMSim::FNode Node = TMSim::FMap::NodeOf(From.HoverPoint);
		for (const std::pair<TMSim::FNode, double>& Entry : From.Reachable)
		{
			if (Entry.first == Node)
			{
				Preview = FString::Printf(TEXT("Walk here: %.1f m of %.1f m"),
					Entry.second, From.Battle.MoveOf(*Unit, From.bSprinting));
				break;
			}
		}
	}
	else if (From.PlayerCanOrder(Unit) && From.AimMode == ATMBattleDirector::EAimMode::Ability)
	{
		const ATMBattleDirector::FAim Where = From.Aim();
		if (!Where.bOk)
		{
			Preview = Where.Why;
			PreviewColour = Urgent;
		}
		else
		{
			const TMSim::FAbility* Ability = TMSim::JobAbility(Unit->Job, From.AimSlot);
			const int32 Hits = static_cast<int32>(From.Battle.Preview(*Unit, From.AimSlot, Unit->Pos, Where.Point).size());
			Preview = FString::Printf(TEXT("%hs: click to use. Reaches %d."), Ability ? Ability->Name.c_str() : "", Hits);
			PreviewColour = FLinearColor(0.55f, 1.0f, 0.6f);
		}
	}
	float Above = ActionBarTop - 8.0f * S;
	if (!Preview.IsEmpty())
	{
		const float Scale = 0.68f * S;
		const FVector2D Size = TextSize(Preview, Font, Scale);
		Above -= Size.Y + 8.0f * S;
		Panel(CentreX - Size.X * 0.5f - 10.0f * S, Above - 4.0f * S, Size.X + 20.0f * S, Size.Y + 8.0f * S, PanelFill);
		Text(Preview, CentreX - Size.X * 0.5f, Above, PreviewColour, Font, Scale);
		Above -= 8.0f * S;
	}

	// Why the last thing did not work, for a few seconds.
	if (From.NoticeLeft > 0.0f && !From.Notice.IsEmpty())
	{
		const float Scale = 0.72f * S;
		const FVector2D Size = TextSize(From.Notice, Font, Scale);
		Above -= Size.Y + 8.0f * S;
		Panel(CentreX - Size.X * 0.5f - 10.0f * S, Above - 4.0f * S, Size.X + 20.0f * S, Size.Y + 8.0f * S, PanelFill);
		Text(From.Notice, CentreX - Size.X * 0.5f, Above, Gold, Font, Scale);
	}

	// Nobody of ours is ready: say who is next, and when.
	if (From.bPlayerInput && !Unit && From.Battle.Winner == -1)
	{
		const TMSim::FUnit* Next = nullptr;
		int32 Soonest = 0;
		for (const TMSim::FUnit& Candidate : From.Battle.Units)
		{
			if (!Candidate.IsAlive() || From.ComputerPlays(Candidate.Team))
			{
				continue;
			}
			const int32 Ticks = From.Battle.TicksToReady(Candidate);
			if (!Next || Ticks < Soonest)
			{
				Next = &Candidate;
				Soonest = Ticks;
			}
		}
		if (Next)
		{
			const FString Line = FString::Printf(TEXT("Waiting: %s %d is up in %.1fs"), *JobName(*Next), Next->Id, Soonest / Tps);
			const float Scale = 0.72f * S;
			const FVector2D Size = TextSize(Line, Font, Scale);
			const float Y = Canvas->ClipY - Size.Y - 30.0f * S;
			Panel(CentreX - Size.X * 0.5f - 10.0f * S, Y - 4.0f * S, Size.X + 20.0f * S, Size.Y + 8.0f * S, PanelFill);
			Text(Line, CentreX - Size.X * 0.5f, Y, Dim, Font, Scale);
		}
	}

	if (From.bPaused)
	{
		const FString Line = TEXT("PAUSED   P to carry on");
		const FVector2D Size = TextSize(Line, Big, 0.7f * S);
		const float Y = Canvas->ClipY * 0.4f;
		Panel(CentreX - Size.X * 0.5f - 20.0f * S, Y - 10.0f * S, Size.X + 40.0f * S, Size.Y + 20.0f * S, PanelFill, Gold, 2.0f);
		Text(Line, CentreX - Size.X * 0.5f, Y, Gold, Big, 0.7f * S);
	}

	// The end of a battle (hud.gd:911, the game-over panel).
	if (From.Battle.Winner != -1)
	{
		FString Line;
		FLinearColor Colour = Gold;
		if (From.Battle.Winner == TMSim::FBattle::Draw)
		{
			Line = TEXT("Nobody is left standing");
			Colour = Dim;
		}
		else if (From.ComputerPlays(0) == From.ComputerPlays(1))
		{
			// Two people, or two computers: nobody here is "you".
			Line = From.Battle.Winner == 0 ? TEXT("Blue wins") : TEXT("Red wins");
			Colour = TeamColour(From.Battle.Winner);
		}
		else
		{
			Line = From.ComputerPlays(From.Battle.Winner) ? TEXT("The computer wins") : TEXT("You win!");
			Colour = From.ComputerPlays(From.Battle.Winner) ? Urgent : Gold;
		}
		const FString Time = FString::Printf(TEXT("after %.0f seconds   seed %llu"), From.Battle.TickCount / Tps, From.BattleSeed);
		const FVector2D Size = TextSize(Line, Big, 1.0f * S);
		const float PW = FMath::Max(Size.X + 80.0f * S, 560.0f * S);
		const float PH = 190.0f * S;
		const float PX = CentreX - PW * 0.5f;
		const float PY = Canvas->ClipY * 0.32f;
		Panel(PX, PY, PW, PH, FLinearColor(0.05f, 0.06f, 0.1f, 0.92f), Colour, 2.0f);
		Text(Line, CentreX - Size.X * 0.5f, PY + 20.0f * S, Colour, Big, 1.0f * S);
		const FVector2D TimeSize = TextSize(Time, Font, 0.65f * S);
		Text(Time, CentreX - TimeSize.X * 0.5f, PY + 72.0f * S, Dim, Font, 0.65f * S);
		if (From.bPlayerInput)
		{
			// Play again, change the teams, or go back to the title (hud.gd:911).
			const float BW = 168.0f * S;
			const float BH = 44.0f * S;
			const float Gap = 10.0f * S;
			const float BY = PY + PH - BH - 20.0f * S;
			float BX = CentreX - (3.0f * BW + 2.0f * Gap) * 0.5f;
			MenuButton(BX, BY, BW, BH, TEXT("Rematch  (R)"), ETMHudAction::NewBattle, -1, true);
			BX += BW + Gap;
			MenuButton(BX, BY, BW, BH, TEXT("Change setup"), ETMHudAction::MenuSetup);
			BX += BW + Gap;
			MenuButton(BX, BY, BW, BH, TEXT("Main menu"), ETMHudAction::MenuTitle);
		}
	}
}

// ================================================================ menus

FVector2D ATMBattleHud::MousePoint() const
{
	float MX = 0.0f;
	float MY = 0.0f;
	if (PlayerOwner && PlayerOwner->GetMousePosition(MX, MY))
	{
		return FVector2D(MX, MY);
	}
	return FVector2D(-1.0f, -1.0f);
}

void ATMBattleHud::MenuButton(float X, float Y, float W, float H, const FString& Label, ETMHudAction Action, int32 Value,
	bool bPrimary, const FString& Detail)
{
	const bool bOver = FBox2D(FVector2D(X, Y), FVector2D(X + W, Y + H)).IsInside(MousePoint());
	const FLinearColor Fill = bOver ? FLinearColor(0.2f, 0.25f, 0.36f, 0.96f) : FLinearColor(0.12f, 0.15f, 0.22f, 0.96f);
	Panel(X, Y, W, H, Fill, bPrimary ? Gold : FLinearColor(0.4f, 0.45f, 0.55f, 0.8f), bPrimary ? 2.0f : 1.0f);
	UFont* Font = GEngine->GetMediumFont();
	const float Scale = 0.66f * S;
	const FVector2D Size = TextSize(Label, Font, Scale);
	if (Detail.IsEmpty())
	{
		Text(Label, X + (W - Size.X) * 0.5f, Y + (H - Size.Y) * 0.5f, bPrimary ? Gold : TextColour, Font, Scale);
	}
	else
	{
		// A name and, under it, what it is -- as the action bar's buttons are.
		const FVector2D DetailSize = TextSize(Detail, Font, 0.48f * S);
		const float Top = Y + (H - Size.Y - DetailSize.Y - 2.0f * S) * 0.5f;
		Text(Label, X + (W - Size.X) * 0.5f, Top, bPrimary ? Gold : TextColour, Font, Scale);
		Text(Detail, X + (W - DetailSize.X) * 0.5f, Top + Size.Y + 2.0f * S, Dim, Font, 0.48f * S);
	}
	AddButton(X, Y, W, H, Action, Value);
}

void ATMBattleHud::DrawTitle(ATMBattleDirector& From)
{
	// main_menu.gd:38-116. The board stands behind it, dimmed.
	DrawRect(FLinearColor(0.02f, 0.03f, 0.06f, 0.55f), 0.0f, 0.0f, Canvas->ClipX, Canvas->ClipY);
	UFont* Font = GEngine->GetMediumFont();
	UFont* Big = GEngine->GetLargeFont();
	const float CentreX = Canvas->ClipX * 0.5f;
	float Y = Canvas->ClipY * 0.2f;

	const FString Title = TEXT("TACTICAL MASTERS");
	const FVector2D TitleSize = TextSize(Title, Big, 1.6f * S);
	Text(Title, CentreX - TitleSize.X * 0.5f, Y, Gold, Big, 1.6f * S);
	Y += TitleSize.Y + 6.0f * S;
	const FString Subtitle = TEXT("Real-time tactics on a 3D battlefield");
	const FVector2D SubSize = TextSize(Subtitle, Font, 0.75f * S);
	Text(Subtitle, CentreX - SubSize.X * 0.5f, Y, Dim, Font, 0.75f * S);
	Y += SubSize.Y + 40.0f * S;

	const float W = 400.0f * S;
	const float H = 56.0f * S;
	const float Gap = 12.0f * S;
	const float X = CentreX - W * 0.5f;
	MenuButton(X, Y, W, H, TEXT("Play vs Computer"), ETMHudAction::TitleVsComputer, -1, true);
	Y += H + Gap;
	MenuButton(X, Y, W, H, TEXT("Two Players (Same Device)"), ETMHudAction::TitleTwoPlayers);
	Y += H + Gap;
	MenuButton(X, Y, W, H, TEXT("Computer vs Computer"), ETMHudAction::TitleWatch);
	Y += H + Gap * 3.0f;
	MenuButton(X, Y, W, 44.0f * S, TEXT("Unit Guide  (U)"), ETMHudAction::ToggleGuide);
	Y += 44.0f * S + Gap;
	MenuButton(X, Y, W, 44.0f * S, TEXT("Quit"), ETMHudAction::Quit);
	Y += 44.0f * S + 30.0f * S;

	// Said rather than left out quietly: what the Godot title has that this one does not yet.
	const FString Missing = TEXT("Online play, How to Play and Options are not ported yet.");
	const FVector2D MissingSize = TextSize(Missing, Font, 0.5f * S);
	Text(Missing, CentreX - MissingSize.X * 0.5f, Y, Dim, Font, 0.5f * S);
}

void ATMBattleHud::DrawSetup(ATMBattleDirector& From)
{
	// battle_setup.gd: both teams, who plays them, and how the battle is seeded.
	// The board behind shows the teams as they are chosen.
	DrawRect(FLinearColor(0.02f, 0.03f, 0.06f, 0.35f), 0.0f, 0.0f, Canvas->ClipX, Canvas->ClipY);
	UFont* Font = GEngine->GetMediumFont();
	UFont* Big = GEngine->GetLargeFont();
	const ATMBattleDirector::FMatchSetup& Setup = From.Setup;
	const bool bVsComputer = Setup.Mode == TEXT("ai");
	const bool bWatch = Setup.Mode == TEXT("cpu");

	const float PW = 1180.0f * S;
	const float PH = 720.0f * S;
	const float PX = (Canvas->ClipX - PW) * 0.5f;
	const float PY = (Canvas->ClipY - PH) * 0.5f;
	Panel(PX, PY, PW, PH, FLinearColor(0.05f, 0.06f, 0.1f, 0.94f), FLinearColor(0.4f, 0.45f, 0.55f, 0.8f), 1.5f);

	const FString Heading = bVsComputer ? TEXT("Battle Setup: Play vs Computer")
		: bWatch ? TEXT("Battle Setup: Computer vs Computer") : TEXT("Battle Setup: Two Players");
	Text(Heading, PX + 30.0f * S, PY + 20.0f * S, TextColour, Big, 0.9f * S);

	// The two teams, side by side.
	const float ColumnW = 520.0f * S;
	const float ColumnTop = PY + 80.0f * S;
	for (int32 Team = 0; Team < 2; ++Team)
	{
		const float CX = PX + 30.0f * S + Team * (ColumnW + 80.0f * S);
		const bool bComputer = bWatch || (bVsComputer && Setup.PlayerTeam != Team);
		const FString Who = bComputer
			? FString::Printf(TEXT("Computer (%s)"), *Setup.Difficulty[Team])
			: bVsComputer ? FString(TEXT("You")) : FString::Printf(TEXT("Player %d"), Team + 1);
		Text(FString::Printf(TEXT("%s   %s"), Team == 0 ? TEXT("Blue") : TEXT("Red"), *Who),
			CX, ColumnTop, TeamColour(Team), Font, 0.8f * S);

		float Y = ColumnTop + 36.0f * S;
		for (int32 Slot = 0; Slot < 4; ++Slot)
		{
			const TMSim::FJobDef* Job = TMSim::FindJob(Setup.Rosters[Team][Slot]);
			FString Name = Job ? FString(UTF8_TO_TCHAR(Job->Name.c_str())) : FString(UTF8_TO_TCHAR(Setup.Rosters[Team][Slot].c_str()));
			FString Roles;
			if (Job)
			{
				for (const std::string& JobRole : Job->Roles)
				{
					Roles += (Roles.IsEmpty() ? TEXT("") : TEXT(", ")) + FString(UTF8_TO_TCHAR(JobRole.c_str()));
				}
			}
			MenuButton(CX, Y, ColumnW, 56.0f * S, Name, ETMHudAction::SetupClass, Team * 4 + Slot, false,
				Roles.IsEmpty() ? FString(TEXT("click to change")) : Roles + TEXT("   (click to change)"));
			Y += 64.0f * S;
		}
		const float Half = (ColumnW - 10.0f * S) * 0.5f;
		MenuButton(CX, Y, Half, 40.0f * S, TEXT("Random team"), ETMHudAction::SetupRandom, Team);
		MenuButton(CX + Half + 10.0f * S, Y, Half, 40.0f * S, TEXT("Default"), ETMHudAction::SetupDefault, Team);
	}

	// Who plays, how hard, and the seed.
	float Y = ColumnTop + 36.0f * S + 4.0f * 64.0f * S + 60.0f * S;
	const float RowX = PX + 30.0f * S;
	const float LabelW = 230.0f * S;
	const float ValueW = 300.0f * S;
	auto Row = [&](const FString& Label, const FString& Value, ETMHudAction Action, int32 ActionValue)
	{
		Text(Label, RowX, Y + 8.0f * S, Dim, Font, 0.62f * S);
		MenuButton(RowX + LabelW, Y, ValueW, 38.0f * S, Value, Action, ActionValue);
		Y += 46.0f * S;
	};
	if (bVsComputer)
	{
		Row(TEXT("You play"), Setup.PlayerTeam == 0 ? TEXT("Blue (click to switch)") : TEXT("Red (click to switch)"),
			ETMHudAction::SetupSide, -1);
	}
	for (int32 Team = 0; Team < 2; ++Team)
	{
		if (bWatch || (bVsComputer && Setup.PlayerTeam != Team))
		{
			Row(FString::Printf(TEXT("%s computer"), Team == 0 ? TEXT("Blue") : TEXT("Red")),
				Setup.Difficulty[Team], ETMHudAction::SetupDifficulty, Team);
		}
	}
	Row(TEXT("Seed"), Setup.bRandomSeed ? FString(TEXT("New each battle"))
		: FString::Printf(TEXT("Fixed: %llu"), Setup.FixedSeed), ETMHudAction::SetupSeed, -1);
	Text(TEXT("Map"), RowX, Y + 2.0f * S, Dim, Font, 0.62f * S);
	Text(TEXT("Highlands"), RowX + LabelW, Y + 2.0f * S, TextColour, Font, 0.62f * S);

	// What the Godot setup offers that is not here yet, said where it would be.
	const float NoteX = PX + PW * 0.5f + 40.0f * S;
	float NoteY = ColumnTop + 36.0f * S + 4.0f * 64.0f * S + 64.0f * S;
	const TCHAR* Notes[] =
	{
		TEXT("Not ported yet:"),
		TEXT("  other maps, and saved teams"),
		TEXT("  hold-the-middle, time limits, planning time"),
		TEXT("  the other 81 classes (they wait on the importer)"),
		TEXT("Easy and medium think more simply than hard, but"),
		TEXT("  do not yet make Godot's random mistakes."),
	};
	for (const TCHAR* Note : Notes)
	{
		Text(Note, NoteX, NoteY, Dim, Font, 0.5f * S);
		NoteY += 20.0f * S;
	}

	// Back, and start.
	const float BW = 220.0f * S;
	const float BH = 52.0f * S;
	const float BY = PY + PH - BH - 24.0f * S;
	MenuButton(PX + 30.0f * S, BY, 160.0f * S, BH, TEXT("Back  (Esc)"), ETMHudAction::SetupBack);
	MenuButton(PX + PW - BW - 30.0f * S, BY, BW, BH, TEXT("Start Battle"), ETMHudAction::SetupStart, -1, true);
}

void ATMBattleHud::DrawBattleMenu(ATMBattleDirector& From)
{
	// The in-game menu (hud.gd:996-1026), which pauses a local game while open.
	DrawRect(FLinearColor(0.02f, 0.03f, 0.06f, 0.45f), 0.0f, 0.0f, Canvas->ClipX, Canvas->ClipY);
	UFont* Big = GEngine->GetLargeFont();
	const float W = 380.0f * S;
	const float H = 52.0f * S;
	const float Gap = 10.0f * S;
	const float PW = W + 60.0f * S;
	const float PH = 4.0f * (H + Gap) + 100.0f * S;
	const float PX = (Canvas->ClipX - PW) * 0.5f;
	const float PY = (Canvas->ClipY - PH) * 0.5f;
	Panel(PX, PY, PW, PH, FLinearColor(0.05f, 0.06f, 0.1f, 0.95f), Gold, 1.5f);
	const FString Title = TEXT("Paused");
	const FVector2D Size = TextSize(Title, Big, 0.9f * S);
	Text(Title, PX + (PW - Size.X) * 0.5f, PY + 18.0f * S, Gold, Big, 0.9f * S);

	float Y = PY + 80.0f * S;
	const float X = PX + 30.0f * S;
	MenuButton(X, Y, W, H, TEXT("Resume  (Esc)"), ETMHudAction::MenuResume, -1, true);
	Y += H + Gap;
	MenuButton(X, Y, W, H, TEXT("Restart battle"), ETMHudAction::MenuRestart, -1, false,
		FString::Printf(TEXT("same teams, same seed (%llu)"), From.BattleSeed));
	Y += H + Gap;
	MenuButton(X, Y, W, H, TEXT("Change setup"), ETMHudAction::MenuSetup);
	Y += H + Gap;
	MenuButton(X, Y, W, H, TEXT("Main menu"), ETMHudAction::MenuTitle);
}