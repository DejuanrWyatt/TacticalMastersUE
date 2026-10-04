#include "TMBattleHud.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Misc/ScopeExit.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"

#include "TMBattleDirector.h"
#include "TMSettings.h"
#include "TMBattleHudStyle.h"
#include "SimAbility.h"

#include <algorithm>
#include <cmath>

using namespace TMHudStyle;

FVector2D ATMBattleHud::Nudge(const TCHAR* Id) const
{
	const FVector2D* Moved = FTMSettings::Get().Layout.Find(Id);
	return Moved ? *Moved * BaseS : FVector2D::ZeroVector;
}

void ATMBattleHud::Movable(const TCHAR* Id, const TCHAR* Label, float X, float Y, float W, float H)
{
	Movables.Add({ Id, Label, FBox2D(FVector2D(X, Y), FVector2D(X + W, Y + H)) });
}

void ATMBattleHud::DrawLayoutEditing(ATMBattleDirector& From)
{
	// Every panel drawn this frame gets a handle over it: blue, with its name,
	// and the whole of it answers the pointer (layout_editor.gd:33-60). The
	// turn squares keep their own buttons, to drag one along its row.
	UFont* Font = GEngine->GetMediumFont();
	const FLinearColor Fill(0.25f, 0.65f, 1.0f, 0.18f);
	const FLinearColor Edge(0.45f, 0.8f, 1.0f, 0.9f);
	TArray<FTMHudButton> Squares;
	for (const FTMHudButton& Button : Buttons)
	{
		if (Button.Action == ETMHudAction::PickUnit && SquareAreas.Contains(Button.Value))
		{
			FTMHudButton Card = Button;
			Card.Action = ETMHudAction::LayoutCard;
			Squares.Add(Card);
		}
	}
	Buttons.Reset();
	Tips.Reset();

	// The grid, under the handles: what a dragged panel's corner snaps to.
	const FTMSettings& Settings = FTMSettings::Get();
	if (Settings.bLayoutGrid)
	{
		const float Step = FMath::Max(4.0f, Settings.GridSize * BaseS);
		int32 Line = 0;
		for (float GX = 0.0f; GX < Canvas->ClipX; GX += Step, ++Line)
		{
			DrawRect(FLinearColor(0.45f, 0.8f, 1.0f, Line % 5 == 0 ? 0.16f : 0.07f), GX, 0.0f, 1.0f, Canvas->ClipY);
		}
		Line = 0;
		for (float GY = 0.0f; GY < Canvas->ClipY; GY += Step, ++Line)
		{
			DrawRect(FLinearColor(0.45f, 0.8f, 1.0f, Line % 5 == 0 ? 0.16f : 0.07f), 0.0f, GY, Canvas->ClipX, 1.0f);
		}
	}

	const float Grip = 16.0f * BaseS;
	for (int32 i = 0; i < Movables.Num(); ++i)
	{
		const FBox2D& Area = Movables[i].Area;
		const FVector2D Size = Area.GetSize();
		Panel(Area.Min.X, Area.Min.Y, Size.X, Size.Y, Fill, Edge, 2.0f);
		// Its name and size on a tag at the corner, clear of what the panel says.
		const FString Tag = FString::Printf(TEXT("%s  %.0f%%"), *Movables[i].Label, Settings.ScaleOf(*Movables[i].Id) * 100.0f);
		const FVector2D LabelSize = TextSize(Tag, Font, 0.45f * BaseS);
		const float TagY = Area.Min.Y > LabelSize.Y + 8.0f * BaseS ? Area.Min.Y - LabelSize.Y - 6.0f * BaseS : Area.Max.Y + 2.0f * BaseS;
		Panel(Area.Min.X, TagY, LabelSize.X + 12.0f * BaseS, LabelSize.Y + 4.0f * BaseS, FLinearColor(0.03f, 0.08f, 0.16f, 0.95f), Edge, 1.0f);
		Text(Tag, Area.Min.X + 6.0f * BaseS, TagY + 2.0f * BaseS, Edge, Font, 0.45f * BaseS);
		AddButton(Area.Min.X, Area.Min.Y, Size.X, Size.Y, ETMHudAction::LayoutGrab, i);
		AddTip(Area.Min.X, Area.Min.Y, Size.X, Size.Y, TEXT("Drag to move. Drag the corner grip, or turn the wheel over it, to make it bigger or smaller."));
		// The grip at the bottom right, which resizes (added last, so on top).
		Panel(Area.Max.X - Grip, Area.Max.Y - Grip, Grip, Grip, Edge, FLinearColor(0.03f, 0.08f, 0.16f, 1.0f), 1.0f);
		AddButton(Area.Max.X - Grip, Area.Max.Y - Grip, Grip, Grip, ETMHudAction::LayoutResize, i);
	}
	Buttons.Append(Squares);

	// The bar that says what is going on, and how to stop.
	const FString Hint = TEXT("Drag to move, the corner grip to resize, a turn square to reorder its side");
	const FVector2D HintSize = TextSize(Hint, Font, 0.55f * S);
	const float BW = 150.0f * S;
	const float BH = 38.0f * S;
	const float W = HintSize.X + 4.0f * BW + 80.0f * S;
	const float X = (Canvas->ClipX - W) * 0.5f;
	const float Y = Canvas->ClipY - 170.0f * S;
	Panel(X, Y, W, BH + 16.0f * S, FLinearColor(0.05f, 0.07f, 0.11f, 0.95f), Edge, 1.5f);
	Text(Hint, X + 14.0f * S, Y + 8.0f * S + (BH - HintSize.Y) * 0.5f, Edge, Font, 0.55f * S);
	float BX = X + HintSize.X + 28.0f * S;
	MenuButton(BX, Y + 8.0f * S, BW, BH, Settings.bLayoutGrid ? TEXT("Grid: on") : TEXT("Grid: off"), ETMHudAction::LayoutGrid, -1, Settings.bLayoutGrid);
	AddTip(BX, Y + 8.0f * S, BW, BH, TEXT("A grid to line panels up on: a dragged panel's corner snaps to it."));
	BX += BW + 12.0f * S;
	MenuButton(BX, Y + 8.0f * S, BW, BH, FString::Printf(TEXT("Grid %.0f px"), Settings.GridSize), ETMHudAction::LayoutGridSize);
	AddTip(BX, Y + 8.0f * S, BW, BH, TEXT("How far apart the grid's lines are: finer for more precision."));
	BX += BW + 12.0f * S;
	MenuButton(BX, Y + 8.0f * S, BW, BH, TEXT("Reset layout"), ETMHudAction::LayoutReset);
	BX += BW + 12.0f * S;
	MenuButton(BX, Y + 8.0f * S, BW, BH,
		FString::Printf(TEXT("Lock  (%s)"), *FTMSettings::Get().KeyName(ETMAction::EditLayout)), ETMHudAction::ToggleLayout, -1, true);
}

void ATMBattleHud::DrawOverlays(ATMBattleDirector& From)
{
	if (!From.bOptionsOpen && !From.bDevToolsOpen)
	{
		return;
	}
	// Only the open one's buttons answer.
	Buttons.Reset();
	Tips.Reset();
	SliderAreas.Reset();
	if (From.bOptionsOpen)
	{
		DrawOptions(From);
	}
	else
	{
		DrawDevTools(From);
	}
}

bool ATMBattleHud::FindButton(ETMHudAction Action, int32 Value, FTMHudButton& Out) const
{
	// The last drawn is on top, as ButtonAt has it.
	for (int32 i = Buttons.Num() - 1; i >= 0; --i)
	{
		if (Buttons[i].Action == Action && (Value == -2 || Buttons[i].Value == Value))
		{
			Out = Buttons[i];
			return true;
		}
	}
	return false;
}

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

void ATMBattleHud::OutlinedText(const FString& What, float X, float Y, const FLinearColor& Colour, UFont* Font, float Scale, float Edge)
{
	// Eight dark copies round it, then the text: an outline that holds on snow and on night alike.
	const FLinearColor Ink(0.02f, 0.02f, 0.04f, Colour.A);
	const float E = FMath::Max(1.0f, Edge);
	for (int32 k = 0; k < 8; ++k)
	{
		const float A = k * PI / 4.0f;
		DrawText(What, Ink, X + FMath::Cos(A) * E, Y + FMath::Sin(A) * E, Font, Scale * FontBoost);
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

FString ATMBattleHud::DescribeMiss(const FVector2D& Point) const
{
	float Best = TNumericLimits<float>::Max();
	const FTMHudButton* Near = nullptr;
	for (const FTMHudButton& Button : Buttons)
	{
		const float Away = FMath::Sqrt(Button.Area.ComputeSquaredDistanceToPoint(Point));
		if (Away < Best)
		{
			Best = Away;
			Near = &Button;
		}
	}
	const FString Size = Canvas ? FString::Printf(TEXT("canvas %.0fx%.0f"), Canvas->ClipX, Canvas->ClipY) : FString(TEXT("no canvas"));
	if (!Near)
	{
		return FString::Printf(TEXT("no buttons, %s"), *Size);
	}
	return FString::Printf(TEXT("%d buttons; nearest action %d value %d at %.0f,%.0f-%.0f,%.0f, %.0f px away; %s"), Buttons.Num(),
		static_cast<int32>(Near->Action), Near->Value, Near->Area.Min.X, Near->Area.Min.Y, Near->Area.Max.X, Near->Area.Max.Y, Best, *Size);
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
	SliderAreas.Reset();
	LogArea = FBox2D(ForceInit);
	Movables.Reset();
	SquareAreas.Reset();
	ATMBattleDirector* Found = FindDirector();
	if (!Canvas || !Found || !Found->bBuilt || !GEngine)
	{
		return;
	}
	S = Canvas->ClipY / 1080.0f * FTMSettings::Get().UiScale;
	BaseS = S;

	// Away from a battle, the menu is the whole screen, over the board.
	if (Found->Screen != ATMBattleDirector::EScreen::Battle)
	{
		if (Found->Screen == ATMBattleDirector::EScreen::Title)
		{
			DrawTitle(*Found);
		}
		else if (Found->Screen == ATMBattleDirector::EScreen::Online)
		{
			DrawOnline(*Found);
		}
		else if (Found->Screen == ATMBattleDirector::EScreen::Lobby)
		{
			DrawLobby(*Found);
			if (Found->PickerSlot >= 0)
			{
				Buttons.Reset();
				Tips.Reset();
				DrawClassPicker(*Found);
			}
		}
		else if (Found->Screen == ATMBattleDirector::EScreen::Draft)
		{
			DrawDraft(*Found);
		}
		else if (Found->Screen == ATMBattleDirector::EScreen::Replays)
		{
			DrawReplays(*Found);
		}
		else
		{
			DrawSetup(*Found);
			if (Found->PickerSlot >= 0)
			{
				Buttons.Reset();
				Tips.Reset();
				DrawClassPicker(*Found);
			}
			else if (Found->ItemPickerSlot >= 0)
			{
				Buttons.Reset();
				Tips.Reset();
				DrawItemPicker(*Found);
			}
		}
		// The Unit Guide can be opened from the title too, over everything.
		if (Found->bGuideOpen)
		{
			Buttons.Reset();
			Tips.Reset();
			DrawGuide(*Found);
		}
		DrawOverlays(*Found);
		DrawLoading(*Found);
		DrawTooltip();
		return;
	}

	// Under everything else, since it is drawn onto the board.
	{ TM_SLOW("Hud BoardAids"); DrawBoardAids(*Found); }
	DrawFeel(*Found);
	if (Found->bShowStatusBars)
	{
		TM_SLOW("Hud Overheads");
		DrawOverheads(*Found);
	}
	DrawZoneShields(*Found);
	DrawReadyMarks(*Found);
	DrawCastWarnings(*Found);
	{ TM_SLOW("Hud WorldWords"); DrawWorldWords(*Found); }
	DrawThreats(*Found);
	DrawZoneWords(*Found);
	TM_SLOW("Hud rest");
	if (FTMSettings::Get().bTurnSquares)
	{
		DrawTurnSquares(*Found);
	}
	else
	{
		DrawTurnOrder(*Found);
	}
	DrawLog(*Found);
	DrawSquadStrip(*Found);
	DrawUnitCard(*Found);
	DrawActionBar(*Found);
	DrawField(*Found);
	DrawInspectCard(*Found);
	DrawCornerButtons(*Found);
	// Watching a replay: its bar along the bottom (TMBattleHudReplay.cpp).
	DrawReplayBar(*Found);
	DrawBanners(*Found);
	DrawBossBar(*Found);
	// Over the panels: the team items screen, while it is open.
	DrawTeamItems(*Found);
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
	else if (Found->bEditingLayout)
	{
		// Only the handles answer while the screen is being arranged.
		DrawLayoutEditing(*Found);
	}
	DrawOverlays(*Found);
	DrawLoading(*Found);
	DrawTooltip();
}

void ATMBattleHud::DrawLoading(ATMBattleDirector& From)
{
	FString What;
	float Fraction = 0.0f;
	if (!From.LoadingProgress(What, Fraction))
	{
		return;
	}
	// High in the middle, under the turn cards and clear of the action bar,
	// over everything but the tooltip; nothing to press.
	const float W = 520.0f * S;
	const float H = 26.0f * S;
	const float X = (Canvas->ClipX - W) * 0.5f;
	const float Y = 135.0f * S;
	Panel(X - 10.0f * S, Y - 10.0f * S, W + 20.0f * S, H + 20.0f * S, PanelFill, Gold, 1.0f);
	Gauge(X, Y, W, H, Fraction, FLinearColor(Gold.R, Gold.G, Gold.B, 0.85f), What);
}

void ATMBattleHud::DrawBoardAids(ATMBattleDirector& From)
{
	// While planning: where the unit being placed may go (battle.gd:381-384).
	if (From.Battle.IsPlanning())
	{
		if (const TMSim::FUnit* Placing = From.Battle.FindUnit(From.PlaceId))
		{
			const float Dot = FMath::Max(4.0f, 6.0f * S);
			for (const TMSim::FNode& Node : From.Battle.PlaceableNodes(*Placing))
			{
				FVector2D At;
				if (ToScreen(From, TMSim::FMap::NodePos(Node), 4.0f, At))
				{
					DrawRect(FLinearColor(0.45f, 0.8f, 1.0f, 0.7f), At.X - Dot * 0.5f, At.Y - Dot * 0.5f, Dot, Dot);
				}
			}
			BoardRing(From, Placing->Pos, 0.45f, Gold, 3.0f);
		}
	}

	// The middle, while holding it can win: a ring on the board, in the colour of
	// whoever is holding it alone, grey while it is contested or empty
	// (board_view.gd:91).
	if (From.Battle.Tuning.CaptureSeconds > 0.0)
	{
		int32 Standing[2] = { 0, 0 };
		const TMSim::FVec2 Middle = From.Battle.CapturePoint();
		for (const TMSim::FUnit& Unit : From.Battle.Units)
		{
			// Only the two sides count (the rules' TickCapture): a monster (team 2)
			// standing here once wrote past the end of this array.
			if (Unit.IsAlive() && (Unit.Team == 0 || Unit.Team == 1) && static_cast<double>(Unit.Pos.DistanceTo(Middle)) <= TMSim::FBattle::CaptureRadius)
			{
				++Standing[Unit.Team];
			}
		}
		const FLinearColor Ring = (Standing[0] > 0) == (Standing[1] > 0)
			? FLinearColor(0.85f, 0.85f, 0.9f, 0.7f)
			: TeamColour(Standing[0] > 0 ? 0 : 1);
		// Painted on the ground itself when it can be (TMBattleDirectorIndicators.cpp).
		if (!From.bIndicatorDecal)
		{
			BoardRing(From, Middle, static_cast<float>(TMSim::FBattle::CaptureRadius), Ring, 3.0f);
		}
	}

	// The watchtowers: over each, how far a side is into taking it. Their reach
	// is shown only for the one under the pointer, as thin white lines painted on
	// the ground (TMBattleDirectorIndicators.cpp); flat here only without that.
	{
		UFont* TowerFont = GEngine->GetMediumFont();
		const int32 Needed = From.Battle.CaptureTurnsNeeded();
		const int32 Hovered = From.bIndicatorDecal ? -1 : From.HoveredTower();
		int32 TowerIndex = -1;
		for (const TMSim::FWatchtower& Tower : From.Battle.Watchtowers)
		{
			++TowerIndex;
			if (TowerIndex == Hovered)
			{
				BoardRing(From, Tower.Pos, static_cast<float>(From.Battle.Tuning.WatchtowerSight), FLinearColor(1.0f, 1.0f, 1.0f, 0.9f), 1.0f);
				BoardRing(From, Tower.Pos, static_cast<float>(TMSim::Watchtower::Reach), FLinearColor(1.0f, 1.0f, 1.0f, 0.55f), 1.0f);
			}
			if (Tower.Capturer >= 0 && Tower.Progress > 0)
			{
				FVector2D At;
				if (ToScreen(From, Tower.Pos, 9.6f * From.TileSize, At))
				{
					const FString Line = FString::Printf(TEXT("%s %d/%d"), Tower.Capturer == 0 ? TEXT("Blue") : TEXT("Red"),
						Tower.Progress, Needed);
					const FVector2D Size = TextSize(Line, TowerFont, 0.6f * S);
					Panel(At.X - Size.X * 0.5f - 6.0f * S, At.Y - 3.0f * S, Size.X + 12.0f * S, Size.Y + 6.0f * S, FLinearColor(0.0f, 0.0f, 0.0f, 0.6f));
					Text(Line, At.X - Size.X * 0.5f, At.Y, TeamColour(Tower.Capturer), TowerFont, 0.6f * S);
				}
			}
		}
	}

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

	// Under the pointer or in the aim: a red ring at its feet, under the red
	// outline the body wears (UpdateMarks).
	for (const int32 Id : From.MarkedUnits)
	{
		if (const TMSim::FUnit* Marked = From.Battle.FindUnit(Id))
		{
			BoardRing(From, Marked->Pos, 0.55f, FLinearColor(1.0f, 0.1f, 0.06f, 0.95f), 3.0f);
			BoardRing(From, Marked->Pos, 0.62f, FLinearColor(1.0f, 0.1f, 0.06f, 0.35f), 5.0f);
		}
	}

	const TMSim::FUnit* Unit = From.SelectedUnit();
	if (!From.PlayerCanCommand(Unit))
	{
		return;
	}

	// Whose orders these are (its turn ring says so when the ground is painted).
	if (!From.bIndicatorDecal)
	{
		BoardRing(From, Unit->Pos, 0.45f, FLinearColor(0.35f, 0.86f, 1.0f), 3.0f);
	}

	// The walk area, its edge and the way are painted on the ground, and the
	// grass, rocks and cliffs over it take the paint too, so nothing is drawn
	// again here (the flat lines floated over the land: 2026-09-30).
	if (From.AimMode == ATMBattleDirector::EAimMode::Move && From.bIndicatorDecal)
	{
		return;
	}
	if (From.AimMode == ATMBattleDirector::EAimMode::Move)
	{
		// How far it can walk, as one line round the edge of that ground, and
		// the way to the spot under the pointer. Each reachable node's side that
		// faces ground it cannot reach is a piece of the line; the gaps other
		// units leave (nobody may stand on them) are not edges, so a unit
		// standing in the middle of the ground is not boxed in.
		const FLinearColor Edge = From.bSprinting ? FLinearColor(1.0f, 0.67f, 0.24f, 1.0f) : FLinearColor(0.31f, 0.63f, 1.0f, 1.0f);
		const TMSim::FMap& Map = From.Battle.Map;
		TSet<int32> Inside;
		for (const std::pair<TMSim::FNode, double>& Entry : From.Reachable)
		{
			Inside.Add(Map.NodeIndex(Entry.first));
		}
		auto Taken = [&From, Unit](const TMSim::FNode& Node)
		{
			const TMSim::FVec2 At = TMSim::FMap::NodePos(Node);
			for (const TMSim::FUnit& Other : From.Battle.Units)
			{
				if (&Other != Unit && Other.IsAlive() && Other.Pos.DistanceTo(At) < TMSim::Ground::UnitSpacing)
				{
					return true;
				}
			}
			return false;
		};
		const float Half = TMSim::Ground::NavStep * 0.5f * From.TileSize;
		const float Thick = FMath::Max(1.0f, 1.5f * S);
		const int Steps[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
		for (const std::pair<TMSim::FNode, double>& Entry : From.Reachable)
		{
			const TMSim::FNode Node = Entry.first;
			// Lifted to this node's own height, so a cliff edge sits on its cliff.
			const FVector Middle = From.BoardPoint(TMSim::FMap::NodePos(Node), 6.0f);
			for (const auto& Step : Steps)
			{
				const TMSim::FNode Next{ Node.X + Step[0], Node.Y + Step[1] };
				const bool bOnMap = Next.X >= 0 && Next.Y >= 0 && Next.X < Map.NavX && Next.Y < Map.NavY;
				if (bOnMap && (Inside.Contains(Map.NodeIndex(Next)) || Taken(Next)))
				{
					continue;
				}
				// The side of this node's square facing that way.
				const FVector Out = From.GetActorTransform().TransformVector(FVector(Step[0], Step[1], 0.0f)) * Half;
				const FVector Along = From.GetActorTransform().TransformVector(FVector(-Step[1], Step[0], 0.0f)) * Half;
				FVector2D A;
				FVector2D B;
				if (PlayerOwner && PlayerOwner->ProjectWorldLocationToScreen(Middle + Out + Along, A)
					&& PlayerOwner->ProjectWorldLocationToScreen(Middle + Out - Along, B))
				{
					DrawLine(A.X, A.Y, B.X, B.Y, Edge, Thick);
				}
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
	// The walk an out-of-range aim would take first.
	for (size_t i = 1; i < From.PathShown.size() && !From.bIndicatorDecal; ++i)
	{
		FVector2D A;
		FVector2D B;
		if (ToScreen(From, From.PathShown[i - 1], 10.0f, A) && ToScreen(From, From.PathShown[i], 10.0f, B))
		{
			DrawLine(A.X, A.Y, B.X, B.Y, FLinearColor(1.0f, 0.9f, 0.35f), 4.0f);
		}
	}
	const TMSim::FAbility* Ability = Unit->Ability(From.AimSlot);
	if (!Ability)
	{
		return;
	}
	// How far it reaches, and the ring it cannot be used inside (on the ground
	// instead, when the indicator decal is up).
	// Its reach is drawn here even with the decal up, so scenery can't hide it.
	if (Ability->MaxRange > 0.0f && !From.bIndicatorDecal)
	{
		BoardRing(From, Unit->Pos, Ability->MaxRange, FLinearColor(0.8f, 0.8f, 0.86f, 0.9f), 2.0f);
	}
	if (Ability->MinRange > 0.0f && !From.bIndicatorDecal)
	{
		BoardRing(From, Unit->Pos, Ability->MinRange, FLinearColor(0.6f, 0.35f, 0.35f, 0.9f), 2.0f);
	}

	const ATMBattleDirector::FAim Where = From.Aim();
	if (!Where.bHave)
	{
		return;
	}
	const float Radius = FMath::Max(Ability->Aoe, TMSim::Ground::HitRadius);
	if (!From.bIndicatorDecal)
	{
		BoardRing(From, Where.Point, Radius, Where.bOk ? FLinearColor(0.43f, 1.0f, 0.55f) : FLinearColor(1.0f, 0.35f, 0.31f), 3.0f);
	}
	if (!Where.bOk)
	{
		return;
	}

	// The forecast over each unit it would reach, from the same Preview the rules
	// resolve with. It cannot say who a cast will have caught by the time it lands.
	UFont* Font = GEngine->GetMediumFont();
	const float Scale = 0.8f * S;
	const std::vector<TMSim::FHit> Hits = From.Battle.Preview(*Unit, From.AimSlot, Unit->Pos, Where.Point);
	// A damaging blow ("Open Odds Mockups" A, 2026-10-02): a card of its odds over
	// each unit it would reach -- the three likeliest to fall, when it catches
	// more; the rest keep the one line.
	TSet<int32> Carded;
	if (Ability->Effect == TMSim::EEffect::Damage)
	{
		struct FCardFor
		{
			const TMSim::FUnit* Target;
			TMSim::FOdds Odds;
		};
		TArray<FCardFor> Cards;
		for (const TMSim::FHit& Hit : Hits)
		{
			if (const TMSim::FUnit* Target = From.Battle.FindUnit(Hit.UnitId))
			{
				Cards.Add({ Target, From.Battle.OddsOf(*Unit, *Ability, *Target, Hit.Amount) });
			}
		}
		Cards.StableSort([](const FCardFor& A, const FCardFor& B) { return A.Odds.Ko > B.Odds.Ko; });
		const FString Extra = Ability->HasStatus()
			? FString::Printf(TEXT("+%hs unless dodged"), Ability->StatusId.c_str()) : FString();
		for (int32 i = 0; i < Cards.Num() && i < 3; ++i)
		{
			FVector2D At;
			if (ToScreen(From, Cards[i].Target->Pos, 225.0f, At))
			{
				OddsCard(*Cards[i].Target, Cards[i].Odds, From.IsFriend(*Cards[i].Target), Extra, At.X, At.Y);
				Carded.Add(Cards[i].Target->Id);
			}
		}
	}
	for (const TMSim::FHit& Hit : Hits)
	{
		const TMSim::FUnit* Target = From.Battle.FindUnit(Hit.UnitId);
		if (!Target || Carded.Contains(Hit.UnitId))
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
			if (Miss > 0 && From.Battle.NewDefense())
			{
				// An evasion mostly grazes (2026-10-01): say for how much.
				Line += FString::Printf(TEXT("  %d%% evade (graze -%d)"), Miss,
					FMath::Max(TMSim::Combat::MinimumDamage, TMSim::RoundToInt(Hit.Amount * TMSim::Combat::GrazeDamage)));
			}
			else if (Miss > 0)
			{
				Line += FString::Printf(TEXT("  %d%% miss"), Miss);
			}
			const TMSim::FOdds Odds = From.Battle.OddsOf(*Unit, *Ability, *Target, Hit.Amount);
			if (Odds.Ko > 0.0)
			{
				Line += FString::Printf(TEXT("  KO %.0f%%"), Odds.Ko);
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
	const FPanelScale Sized(*this, TEXT("action_bar"));
	// Atlas Reactor's bar: the abilities as icons, coloured when they can be
	// used and grey with the turns left when they cannot, between Move and
	// Sprint and End Turn (hud.gd:866-909, 1429-1474).
	const TMSim::FUnit* Unit = From.SelectedUnit();
	ActionBarTop = Canvas->ClipY - 16.0f * S;
	if (!From.bPlayerInput || !Unit || !Unit->IsAlive())
	{
		return;
	}
	// A unit being planned: its buttons as they will be when its turn comes.
	const ATMBattleDirector::FPlanStandIn Stand(From);
	const bool bControllable = From.PlayerCanCommand(Unit);

	UFont* Font = GEngine->GetMediumFont();
	const float Tile = 100.0f * S;
	// Wide enough for an icon beside the longest word, CAPTURE ("Action Bar Mockups" B).
	const float Small = 74.0f * S;
	const float Gap = 10.0f * S;
	// A fourth small tile, Capture, when the battle has watchtowers; and ITEMS
	// with the neutral camps, or when anything lies on the ground or is carried;
	// and one tile for each ability an item gives.
	const bool bTowers = !From.Battle.Watchtowers.empty();
	const bool bItems = !From.Battle.Camps.empty() || !From.Battle.Caches.empty() || Unit->HasItems()
		|| ((Unit->Team == 0 || Unit->Team == 1) && !From.Battle.Stash[Unit->Team].empty());
	int32 ItemAbilities = 0;
	for (int32 Slot = TMSim::ClassSlots; Slot < TMSim::AbilitySlots; ++Slot)
	{
		ItemAbilities += Unit->Ability(Slot) ? 1 : 0;
	}
	const float Smalls = 3.0f + (bTowers ? 1.0f : 0.0f) + (bItems ? 1.0f : 0.0f) + ItemAbilities;
	const float Total = Smalls * Small + 4.0f * Tile + (Smalls + 3.0f) * Gap + 20.0f * S;
	float X = (Canvas->ClipX - Total) * 0.5f + Nudge(TEXT("action_bar")).X;
	const float Y = Canvas->ClipY - Tile - 22.0f * S + Nudge(TEXT("action_bar")).Y;
	ActionBarTop = Y - 8.0f * S;
	Movable(TEXT("action_bar"), TEXT("Action bar"), X, Y, Total, Tile);

	// The long dark strip everything sits on.
	Slant(X - 26.0f * S, Y - 10.0f * S, Total + 40.0f * S, Tile + 20.0f * S, FLinearColor(0.02f, 0.03f, 0.05f, 0.72f), 18.0f * S);

	const FVector2D Mouse = MousePoint();
	// Move, Sprint, Items, Capture and End Turn: smaller tiles, an icon beside the
	// word and the key or count under it ("Action Bar Mockups" B, 2026-10-02).
	// The icons are drawn as strokes in a 24-unit square, so they need no art.
	enum class EGlyph : uint8 { Move, Sprint, Items, Capture, End };
	auto Glyph = [&](EGlyph Kind, float GX, float GY, float Box, const FLinearColor& Colour)
	{
		const float U = Box / 24.0f;
		const float Thick = FMath::Max(1.0f, 1.9f * U);
		auto P = [&](float PX, float PY) { return FVector2D(GX + PX * U, GY + PY * U); };
		auto Seg = [&](float AX, float AY, float BX, float BY)
		{
			const FVector2D A = P(AX, AY), B = P(BX, BY);
			DrawLine(A.X, A.Y, B.X, B.Y, Colour, Thick);
		};
		auto Oval = [&](float CX, float CY, float RX, float RY)
		{
			constexpr int32 Steps = 14;
			for (int32 k = 0; k < Steps; ++k)
			{
				const float A0 = 2.0f * PI * k / Steps, A1 = 2.0f * PI * (k + 1) / Steps;
				Seg(CX + FMath::Cos(A0) * RX, CY + FMath::Sin(A0) * RY, CX + FMath::Cos(A1) * RX, CY + FMath::Sin(A1) * RY);
			}
		};
		switch (Kind)
		{
		case EGlyph::Move:  // two footprints, one ahead of the other
			Oval(8.0f, 8.5f, 2.6f, 4.2f); Oval(8.0f, 16.5f, 2.0f, 1.6f);
			Oval(16.0f, 12.5f, 2.6f, 4.2f); Oval(16.0f, 20.5f, 2.0f, 1.6f);
			break;
		case EGlyph::Sprint:  // speed lines and a double chevron
			Seg(2.0f, 8.0f, 7.0f, 8.0f); Seg(1.0f, 12.0f, 7.0f, 12.0f); Seg(2.0f, 16.0f, 7.0f, 16.0f);
			Seg(10.0f, 5.0f, 16.0f, 12.0f); Seg(16.0f, 12.0f, 10.0f, 19.0f);
			Seg(15.0f, 5.0f, 21.0f, 12.0f); Seg(21.0f, 12.0f, 15.0f, 19.0f);
			break;
		case EGlyph::Items:  // a satchel with its handle
			Seg(5.5f, 9.0f, 18.5f, 9.0f); Seg(18.5f, 9.0f, 17.3f, 20.0f); Seg(17.3f, 20.0f, 6.7f, 20.0f); Seg(6.7f, 20.0f, 5.5f, 9.0f);
			Seg(9.0f, 9.0f, 9.0f, 6.5f); Seg(9.0f, 6.5f, 12.0f, 4.0f); Seg(12.0f, 4.0f, 15.0f, 6.5f); Seg(15.0f, 6.5f, 15.0f, 9.0f);
			Seg(10.0f, 13.0f, 14.0f, 13.0f);
			break;
		case EGlyph::Capture:  // a flag on its pole
			Seg(6.0f, 21.0f, 6.0f, 3.5f); Seg(6.0f, 4.0f, 17.0f, 4.0f); Seg(17.0f, 4.0f, 14.5f, 8.0f); Seg(14.5f, 8.0f, 17.0f, 12.0f); Seg(17.0f, 12.0f, 6.0f, 12.0f);
			break;
		case EGlyph::End:  // skip to the end
			Seg(6.0f, 5.0f, 14.0f, 12.0f); Seg(14.0f, 12.0f, 6.0f, 19.0f); Seg(6.0f, 19.0f, 6.0f, 5.0f); Seg(18.0f, 5.0f, 18.0f, 19.0f);
			break;
		}
	};
	// What is left of a turn half spent flashes ("Turn Left Indicator Mockups" D,
	// the human's pick with flashing borders, 2026-10-02): its edge pulses.
	// 2026-10-03, "more visually noticeable": a thicker edge, a three-step glow
	// round it, a quicker beat, and never fully dark between beats.
	const float Pulse = 0.35f + 0.65f * (0.5f + 0.5f * FMath::Sin((GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0f) * 2.0f * PI * 1.4f));
	const FLinearColor Flash(1.0f, 0.95f, 0.55f, 1.0f);
	auto WordTile = [&](EGlyph Kind, const FString& Word, const FString& Under, bool bEnabled, bool bPressed, ETMHudAction Action, bool bFlash = false)
	{
		const float TY = Y + Tile - Small;
		const bool bOver = FBox2D(FVector2D(X, TY), FVector2D(X + Small, TY + Small)).IsInside(Mouse);
		if (bFlash)
		{
			for (int32 Ring = 3; Ring >= 1; --Ring)
			{
				const float Out = (3.0f + 4.0f * Ring) * S;
				DrawRect(Flash * FLinearColor(1.0f, 1.0f, 1.0f, (0.14f + 0.12f * (3 - Ring)) * Pulse), X - Out, TY - Out, Small + 2.0f * Out, Small + 2.0f * Out);
			}
		}
		DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.75f), X - 3.0f * S, TY - 3.0f * S, Small + 6.0f * S, Small + 6.0f * S);
		const FLinearColor Rim = bPressed ? Gold : FLinearColor(0.55f, 0.62f, 0.75f, bEnabled ? 0.8f : 0.3f);
		const float RimW = (bFlash ? 4.0f : 2.0f) * S;
		DrawRect(bFlash ? FMath::Lerp(Rim, Flash, Pulse) : Rim, X - RimW, TY - RimW, Small + 2.0f * RimW, Small + 2.0f * RimW);
		DrawRect(bEnabled && bOver ? FLinearColor(0.16f, 0.2f, 0.3f, 1.0f) : FLinearColor(0.06f, 0.07f, 0.1f, 1.0f), X, TY, Small, Small);
		// The icon and the word side by side, centred, the word made smaller if they would not fit.
		const float IconBox = 15.0f * S;
		const float Between = 4.0f * S;
		float WordScale = 0.42f * S;
		FVector2D WordSize = TextSize(Word, Font, WordScale);
		const float Room = Small - 8.0f * S;
		if (IconBox + Between + WordSize.X > Room)
		{
			WordScale *= (Room - IconBox - Between) / FMath::Max(1.0f, WordSize.X);
			WordSize = TextSize(Word, Font, WordScale);
		}
		const float RowW = IconBox + Between + WordSize.X;
		const float RowX = X + (Small - RowW) * 0.5f;
		const float RowY = TY + Small * 0.2f;
		const FLinearColor Ink = bPressed ? Gold : bEnabled ? TextColour : Dim;
		Glyph(Kind, RowX, RowY + (WordSize.Y - IconBox) * 0.5f, IconBox, Ink);
		Text(Word, RowX + IconBox + Between, RowY, bEnabled ? TextColour : Dim, Font, WordScale);
		const FVector2D UnderSize = TextSize(Under, Font, 0.32f * S);
		Text(Under, X + (Small - UnderSize.X) * 0.5f, TY + Small * 0.6f, Dim, Font, 0.32f * S);
		AddButton(X, TY, Small, Small, Action, -1);
		X += Small + Gap;
	};

	const bool bMoving = From.AimMode == ATMBattleDirector::EAimMode::Move;
	// A unit that has acted but not moved: its move is what is left, and flashes.
	const bool bMoveLeft = bControllable && Unit->bReady && Unit->bActed && !Unit->bMoved && !Unit->IsCasting();
	WordTile(EGlyph::Move, TEXT("MOVE"), Unit->bMoved ? FString(TEXT("moved")) : FTMSettings::Get().KeyName(ETMAction::Move),
		bControllable && !Unit->bMoved && !Unit->IsCasting(), bMoving && !From.bSprinting, ETMHudAction::Move, bMoveLeft);
	WordTile(EGlyph::Sprint, TEXT("SPRINT"), Unit->bMoved ? FString(TEXT("moved")) : Unit->bActed ? FString(TEXT("acted"))
		: FString::Printf(TEXT("%s  %.1fm"), *FTMSettings::Get().KeyName(ETMAction::Sprint), From.Battle.MoveOf(*Unit, true)),
		bControllable && !Unit->bMoved && !Unit->bActed && !Unit->IsCasting(), bMoving && From.bSprinting, ETMHudAction::Sprint);
	X += 10.0f * S;
	for (int32 Slot = 0; Slot < 4; ++Slot)
	{
		AbilityTile(From, *Unit, Slot, X, Y, Tile, true);
		X += Tile + Gap;
	}
	// The items' abilities: smaller tiles, keys 5 to 7.
	for (int32 Slot = TMSim::ClassSlots; Slot < TMSim::AbilitySlots; ++Slot)
	{
		if (Unit->Ability(Slot))
		{
			AbilityTile(From, *Unit, Slot, X, Y + Tile - Small, Small, true);
			X += Small + Gap;
		}
	}
	X += 10.0f * S;
	if (bItems)
	{
		FString WhyNot;
		const int32 Near = From.TakeableCache(*Unit, &WhyNot);
		const float TileX = X;
		const int32 Stashed = Unit->Team == 0 || Unit->Team == 1 ? static_cast<int32>(From.Battle.Stash[Unit->Team].size()) : 0;
		WordTile(EGlyph::Items, TEXT("ITEMS"), Near >= 0 ? FString(TEXT("pick up")) : Stashed > 0 ? FString::Printf(TEXT("stash %d"), Stashed)
			: FString::Printf(TEXT("%d/3"), (Unit->Gear[0] ? 1 : 0) + (Unit->Gear[1] ? 1 : 0) + (Unit->Gear[2] ? 1 : 0)),
			true, From.bTeamItemsOpen, ETMHudAction::TakeOpen);
		AddTip(TileX, Y + Tile - Small, Small, Small,
			TEXT("The team's items: each unit, what it wears and its open slots, and the stash. Items picked up go to the stash; equip them on any unit with an open slot. A worn item comes off only when its unit falls or spends a turn taking it off."));
	}
	if (bTowers)
	{
		// Lit when the rules would take it now; otherwise the tip says why not.
		FString WhyNot;
		const bool bCan = bControllable && From.CapturableTower(*Unit, &WhyNot) >= 0;
		const float TileX = X;
		WordTile(EGlyph::Capture, TEXT("CAPTURE"), FString::Printf(TEXT("%d turn%s"), From.Battle.CaptureTurnsNeeded(),
			From.Battle.CaptureTurnsNeeded() == 1 ? TEXT("") : TEXT("s")), bCan, false, ETMHudAction::Capture);
		AddTip(TileX, Y + Tile - Small, Small, Small, bCan
			? FString(TEXT("Spend this unit's whole turn taking the watchtower it stands next to."))
			: WhyNot);
	}
	WordTile(EGlyph::End, TEXT("END"), FTMSettings::Get().KeyName(ETMAction::EndTurn), bControllable, false, ETMHudAction::EndTurn);
}

void ATMBattleHud::DrawBanners(ATMBattleDirector& From)
{
	UFont* Font = GEngine->GetMediumFont();
	UFont* Big = GEngine->GetLargeFont();
	const float CentreX = Canvas->ClipX * 0.5f;

	// The other ways to win, under the turn order: the time left, and how long
	// each side has held the middle out of what it needs (battle.gd:1326-1340).
	{
		const TMSim::FBattle& Battle = From.Battle;
		FString Rules;
		if (Battle.Tuning.BattleSeconds > 0.0)
		{
			const int32 Left = FMath::Max(0, TMSim::RoundToInt(Battle.Tuning.BattleSeconds * Tps) - Battle.TickCount);
			const int32 Seconds = FMath::CeilToInt(Left / Tps);
			Rules += FString::Printf(TEXT("Time %d:%02d"), Seconds / 60, Seconds % 60);
		}
		if (Battle.Tuning.CaptureSeconds > 0.0)
		{
			const int32 Needed = FMath::Max(1, TMSim::RoundToInt(Battle.Tuning.CaptureSeconds * Tps));
			Rules += FString::Printf(TEXT("%sMiddle  Blue %d%%  Red %d%%"), Rules.IsEmpty() ? TEXT("") : TEXT("     "),
				FMath::Min(100, Battle.CaptureTicks[0] * 100 / Needed), FMath::Min(100, Battle.CaptureTicks[1] * 100 / Needed));
		}
		if (Battle.IsPlanning())
		{
			// battle.gd:362-380, the planning banner.
			const int32 Team = From.PlanningTeam();
			FString Plan = FString::Printf(TEXT("Planning: %ds"), FMath::CeilToInt(Battle.PlanningTicks / Tps));
			if (Team == -1)
			{
				Plan += TEXT("   ·   both sides are placing their units");
			}
			else if (Battle.PlanningDone[Team])
			{
				Plan += TEXT("   ·   waiting for the other side");
			}
			else
			{
				Plan += TEXT("   ·   click one of your units, then click a spot in your area");
			}
			const FPanelScale Sized(*this, TEXT("planning"));
			const FVector2D Size = TextSize(Plan, Font, 0.7f * S);
			const FVector2D Moved = Nudge(TEXT("planning"));
			const float PY = 152.0f * S + Moved.Y;
			const float PX = CentreX + Moved.X;
			Panel(PX - Size.X * 0.5f - 16.0f * S, PY - 6.0f * S, Size.X + 32.0f * S, Size.Y + 12.0f * S,
				FLinearColor(0.05f, 0.06f, 0.1f, 0.85f), Gold, 1.5f);
			Text(Plan, PX - Size.X * 0.5f, PY, Gold, Font, 0.7f * S);
			float Bottom = PY + Size.Y + 6.0f * S;
			if (Team != -1 && !Battle.PlanningDone[Team])
			{
				const float BW = 220.0f * S;
				MenuButton(PX - BW * 0.5f, PY + Size.Y + 16.0f * S, BW, 44.0f * S,
					FString::Printf(TEXT("Ready  (%s)"), *FTMSettings::Get().KeyName(ETMAction::EndTurn)), ETMHudAction::PlanningReady);
				Bottom += 54.0f * S;
			}
			Movable(TEXT("planning"), TEXT("Planning banner"), PX - Size.X * 0.5f - 16.0f * S, PY - 6.0f * S, Size.X + 32.0f * S, Bottom - PY + 6.0f * S);
		}
		if (!Rules.IsEmpty())
		{
			const FPanelScale Sized(*this, TEXT("objective"));
			const FVector2D Size = TextSize(Rules, Font, 0.62f * S);
			const FVector2D Moved = Nudge(TEXT("objective"));
			const float RY = 118.0f * S + Moved.Y;
			const float RX = CentreX + Moved.X;
			Panel(RX - Size.X * 0.5f - 12.0f * S, RY - 4.0f * S, Size.X + 24.0f * S, Size.Y + 8.0f * S,
				FLinearColor(0.05f, 0.06f, 0.1f, 0.75f), FLinearColor(0.3f, 0.32f, 0.4f, 1.0f), 1.0f);
			Text(Rules, RX - Size.X * 0.5f, RY, TextColour, Font, 0.62f * S);
			Movable(TEXT("objective"), TEXT("Time and middle"), RX - Size.X * 0.5f - 12.0f * S, RY - 4.0f * S, Size.X + 24.0f * S, Size.Y + 8.0f * S);
		}
	}

	// The hover preview, just above the action bar: what a click here would do.
	FString Preview;
	FLinearColor PreviewColour = TextColour;
	const TMSim::FUnit* Unit = From.SelectedUnit();
	FVector2D Mouse(-1.0f, -1.0f);
	if (PlayerOwner)
	{
		float MX = 0.0f;
		float MY = 0.0f;
		if (From.CursorPosition(MX, MY))
		{
			Mouse = FVector2D(MX, MY);
		}
	}
	FTMHudButton Over;
	if (Unit && ButtonAt(Mouse, Over) && Over.Action == ETMHudAction::Ability)
	{
		// The mouse is over an ability: say what it is, whether or not it can be used.
		if (const TMSim::FAbility* Ability = Unit->Ability(Over.Value))
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
	else if (From.PlayerCanCommand(Unit) && From.AimMode == ATMBattleDirector::EAimMode::Move && From.bHaveHover)
	{
		const TMSim::FNode Node = TMSim::FMap::NodeOf(From.HoverPoint);
		const FTMSettings& Keys = FTMSettings::Get();
		const int32 Ways = static_cast<int32>(From.WayPoints.size());
		for (const std::pair<TMSim::FNode, double>& Entry : From.Reachable)
		{
			if (Entry.first == Node)
			{
				// Planned or now, by any waypoints, and how to add one (2026-10-01).
				Preview = FString::Printf(TEXT("%s: %.1f m of %.1f m%s   %s+click: waypoint%s"),
					From.IsPlanningSelected() ? TEXT("Plan the walk here") : TEXT("Walk here"),
					Entry.second, From.Battle.MoveOf(*Unit, From.bSprinting),
					Ways > 0 ? *FString::Printf(TEXT(" by %d waypoint%s"), Ways, Ways == 1 ? TEXT("") : TEXT("s")) : TEXT(""),
					*Keys.KeyName(ETMAction::Waypoint),
					Ways > 0 ? *FString::Printf(TEXT("   %s: undo one"), *Keys.KeyName(ETMAction::PlanUndo)) : TEXT(""));
				break;
			}
		}
		if (Preview.IsEmpty() && !From.GoToHoverStops.empty())
		{
			// Beyond the walk area: a Go To, over as many turns as it takes.
			const int32 Turns = static_cast<int32>(From.GoToHoverStops.size());
			Preview = FString::Printf(TEXT("Go To: %d turn%s, %.1f m. Click to set: each turn it walks, then ends its turn."),
				Turns, Turns == 1 ? TEXT("") : TEXT("s"), From.GoToHoverMetres);
			PreviewColour = FLinearColor(0.55f, 0.75f, 1.0f);
		}
	}
	else if (From.PlayerCanCommand(Unit) && From.AimMode == ATMBattleDirector::EAimMode::Ability)
	{
		// A unit being planned aims from where its planned walk ends.
		const ATMBattleDirector::FPlanStandIn Stand(From);
		const bool bPlanned = From.IsPlanningSelected();
		const ATMBattleDirector::FAim Where = From.Aim();
		TMSim::FVec2 Spot;
		double Walk = 0.0;
		const TMSim::FAbility* Aimed = Unit->Ability(From.AimSlot);
		if (!Where.bOk && Where.Why == UTF8_TO_TCHAR(ATMBattleDirector::OutOfRange) && Aimed)
		{
			// It can be used from somewhere it can walk to: say so, rather than
			// simply refusing (battle.gd:1438-1447).
			if (!Unit->bMoved && !Unit->IsCasting() && From.ClosestSpotInRange(*Unit, From.AimSlot, Where.Point, Spot, Walk))
			{
				const TMSim::FUnit* Target = From.Battle.FindUnit(Where.Follow);
				Preview = bPlanned
					? FString::Printf(TEXT("%hs: out of range. Click to plan a walk of %.1f m, then it."), Aimed->Name.c_str(), Walk)
					: FString::Printf(TEXT("%hs: out of range. Click to walk %.1f m and use it where %s is standing now."),
						Aimed->Name.c_str(), Walk, Target ? *JobName(*Target) : TEXT("the target"));
				PreviewColour = FLinearColor(1.0f, 0.9f, 0.35f);
			}
			else if (From.bAbilityHoverGoTo)
			{
				// Beyond this turn's reach (2026-10-03): said plainly, with what a click does.
				const int32 Turns = static_cast<int32>(From.GoToHoverStops.size());
				Preview = FString::Printf(TEXT("OUT OF RANGE: %hs. Click to go into range (%d turn%s, %.0f m), then use it there. It stops if it sees an enemy."),
					Aimed->Name.c_str(), Turns, Turns == 1 ? TEXT("") : TEXT("s"), From.GoToHoverMetres);
				PreviewColour = FLinearColor(1.0f, 0.62f, 0.3f);
			}
			else
			{
				Preview = FString::Printf(TEXT("OUT OF RANGE: %hs, and there is no way into range of that."), Aimed->Name.c_str());
				PreviewColour = Urgent;
			}
		}
		else if (!Where.bOk)
		{
			Preview = Where.Why;
			PreviewColour = Urgent;
		}
		else
		{
			const TMSim::FAbility* Ability = Unit->Ability(From.AimSlot);
			const int32 Hits = static_cast<int32>(From.Battle.Preview(*Unit, From.AimSlot, Unit->Pos, Where.Point).size());
			Preview = FString::Printf(TEXT("%hs: click to %s. Reaches %d%s."), Ability ? Ability->Name.c_str() : "",
				bPlanned ? TEXT("plan it") : TEXT("use"), Hits, bPlanned ? TEXT(" as things stand now") : TEXT(""));
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

	// Queued orders (2026-10-01): the plan being made, as steps, with its buttons.
	if (Unit && From.GoToOf(Unit->Id) && From.PlayerCanCommand(Unit))
	{
		// A Go To (2026-10-01): its turns, how each ends, and its buttons.
		Above = DrawGoToStrip(From, *Unit, Above);
	}
	else if (Unit && From.IsPlanningSelected())
	{
		Above = DrawPlanStrip(From, *Unit, Above);
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

	// Nobody of ours is ready: say who is next, and when. Not while planning,
	// when no time passes and the banner says what to do.
	if (From.bPlayerInput && !Unit && From.Battle.Winner == -1 && !From.Battle.IsPlanning())
	{
		const TMSim::FUnit* Next = nullptr;
		int32 Soonest = 0;
		for (const TMSim::FUnit& Candidate : From.Battle.Units)
		{
			if (!Candidate.IsAlive() || (From.bOnline ? From.UnitOwner(Candidate) != From.LocalPlayer : From.ComputerPlays(Candidate.Team)))
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
			const FString Line = FString::Printf(TEXT("Waiting: %s %d is up in %.1fs   Click one of yours to plan its turn"),
				*JobName(*Next), Next->Id, Soonest / Tps);
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

	// Typing a line to the other player (battle.gd:732, hud.gd's chat line).
	if (From.Typing == ATMBattleDirector::ETypeField::Chat)
	{
		const float CW = 620.0f * S;
		const float CY = Canvas->ClipY * 0.62f;
		Text(TEXT("Say to your opponent (Enter sends, Esc closes):"), CentreX - CW * 0.5f, CY - 24.0f * S, Dim, Font, 0.5f * S);
		TextField(CentreX - CW * 0.5f, CY, CW, 44.0f * S, From.ChatLine, FString(), true, static_cast<int32>(ATMBattleDirector::ETypeField::Chat));
	}

	// An online match that can't go on (battle.gd:676-694).
	if (!From.OnlineStopped.IsEmpty() && From.Battle.Winner == -1)
	{
		const FString Line = From.OnlineStopped;
		const FString Under = TEXT("The match can't continue.");
		const FVector2D Size = TextSize(Line, Big, 0.8f * S);
		const float PW = FMath::Max(Size.X + 80.0f * S, 560.0f * S);
		const float PH = 170.0f * S;
		const float PX = CentreX - PW * 0.5f;
		const float PY = Canvas->ClipY * 0.32f;
		Panel(PX, PY, PW, PH, FLinearColor(0.05f, 0.06f, 0.1f, 0.94f), Urgent, 2.0f);
		Text(Line, CentreX - Size.X * 0.5f, PY + 20.0f * S, Urgent, Big, 0.8f * S);
		const FVector2D UnderSize = TextSize(Under, Font, 0.6f * S);
		Text(Under, CentreX - UnderSize.X * 0.5f, PY + 66.0f * S, Dim, Font, 0.6f * S);
		MenuButton(CentreX - 90.0f * S, PY + PH - 64.0f * S, 180.0f * S, 44.0f * S, TEXT("Main menu"), ETMHudAction::MenuTitle);
	}

	// The end of a battle (hud.gd:911, the game-over panel).
	if (From.Battle.Winner != -1)
	{
		FString Line;
		FLinearColor Colour = Gold;
		if (From.Battle.Winner == TMSim::FBattle::Draw)
		{
			Line = From.HowWon().IsEmpty() ? TEXT("Nobody is left standing") : TEXT("A draw");
			Colour = Dim;
		}
		else if (From.bReplaying)
		{
			Line = From.Battle.Winner == 0 ? TEXT("Blue wins") : TEXT("Red wins");
			Colour = TeamColour(From.Battle.Winner);
		}
		else if (From.bOnline)
		{
			Line = From.Battle.Winner == From.LocalTeam ? TEXT("Your side wins!") : TEXT("Your side is beaten");
			Colour = From.Battle.Winner == From.LocalTeam ? Gold : Urgent;
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
		// The battle report: the MVP, every unit's numbers, the moments (TMBattleHudReport.cpp);
		// or, put away to look at the board, a button to bring it back.
		if (From.Tallies.Num() > 0)
		{
			if (!From.bReportHidden)
			{
				DrawBattleReport(From, Line, Colour);
			}
			else
			{
				MenuButton(CentreX - 120.0f * S, 70.0f * S, 240.0f * S, 48.0f * S, TEXT("Battle report"), ETMHudAction::ReportHide, -1, true);
			}
			return;
		}
		const FString How = From.HowWon();
		const FString Time = FString::Printf(TEXT("%safter %.0f seconds   seed %llu"),
			How.IsEmpty() ? TEXT("") : *(How + TEXT("   ")), From.Battle.TickCount / Tps, From.BattleSeed);
		const FVector2D Size = TextSize(Line, Big, 1.0f * S);
		const float PW = FMath::Max(Size.X + 80.0f * S, 760.0f * S);  // four buttons fit
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
			if (From.bReplaying)
			{
				// The end of a replay: again, the other replays, or the title.
				float BX = CentreX - (3.0f * BW + 2.0f * Gap) * 0.5f;
				MenuButton(BX, BY, BW, BH, TEXT("Watch again"), ETMHudAction::ReplayAgain, -1, true);
				BX += BW + Gap;
				MenuButton(BX, BY, BW, BH, TEXT("Replays"), ETMHudAction::ReplayLeave);
				BX += BW + Gap;
				MenuButton(BX, BY, BW, BH, TEXT("Main menu"), ETMHudAction::MenuTitle);
			}
			else if (From.bOnline)
			{
				// Online, the host takes everyone back to the lobby for the next one.
				const bool bHost = From.Net.IsValid() && From.Net->IsHost();
				float BX = CentreX - (2.0f * BW + Gap) * 0.5f - 20.0f * S;
				if (bHost)
				{
					MenuButton(BX, BY, BW + 40.0f * S, BH, TEXT("Back to lobby  (R)"), ETMHudAction::NewBattle, -1, true);
				}
				else
				{
					ChoiceButton(BX, BY, BW + 40.0f * S, BH, TEXT("Waiting for the host..."), ETMHudAction::NewBattle, -1, false);
				}
				BX += BW + 40.0f * S + Gap;
				MenuButton(BX, BY, BW, BH, TEXT("Main menu"), ETMHudAction::MenuTitle);
			}
			else
			{
				// And the battle just played, from the start (TMBattleDirectorReplay.cpp).
				float BX = CentreX - (4.0f * BW + 3.0f * Gap) * 0.5f;
				MenuButton(BX, BY, BW, BH, TEXT("Rematch  (R)"), ETMHudAction::NewBattle, -1, true);
				BX += BW + Gap;
				MenuButton(BX, BY, BW, BH, TEXT("Watch replay"), ETMHudAction::ReplayWatch, -1);
				BX += BW + Gap;
				MenuButton(BX, BY, BW, BH, TEXT("Change setup"), ETMHudAction::MenuSetup);
				BX += BW + Gap;
				MenuButton(BX, BY, BW, BH, TEXT("Main menu"), ETMHudAction::MenuTitle);
			}
		}
	}
}

// ================================================================ menus

FVector2D ATMBattleHud::MousePoint() const
{
	float MX = 0.0f;
	float MY = 0.0f;
	const ATMBattleDirector* Board = const_cast<ATMBattleHud*>(this)->FindDirector();
	if (Board && Board->CursorPosition(MX, MY))
	{
		return FVector2D(MX, MY);
	}
	return FVector2D(-1.0f, -1.0f);
}

float ATMBattleHud::DrawGoToStrip(ATMBattleDirector& From, const TMSim::FUnit& Unit, float Above)
{
	// "Multi-Turn Move Mockups" A and B: where it is going and in how many turns,
	// how each turn on the road ends, and, stopped, why and Keep going.
	const ATMBattleDirector::FTMGoTo* Order = From.GoToOf(Unit.Id);
	if (!Order)
	{
		return Above;
	}
	UFont* Font = GEngine->GetMediumFont();
	const FTMSettings& Keys = FTMSettings::Get();
	const float Scale = 0.6f * S;
	const float ChipH = 34.0f * S;
	const float Gap = 8.0f * S;
	const FLinearColor Blue(0.44f, 0.66f, 1.0f);
	const int32 Turns = static_cast<int32>(Order->Stops.size());
	const TMSim::FAbility* Carried = Order->HasAbility() ? Unit.Ability(Order->Slot) : nullptr;
	const FString Lead = Order->bStopped
		? FString::Printf(TEXT("Stopped: %s."), *Order->Why)
		: Carried ? FString::Printf(TEXT("Into range for %hs: %d turn%s left"), Carried->Name.c_str(), Turns, Turns == 1 ? TEXT("") : TEXT("s"))
		: FString::Printf(TEXT("Go To: %d turn%s left"), Turns, Turns == 1 ? TEXT("") : TEXT("s"));
	const FString EndLabel = TEXT("Walk and end");
	const FString WaitLabel = TEXT("Walk, wait for me");
	const FString GoLabel = FString::Printf(TEXT("Keep going (%s)"), *Keys.KeyName(ETMAction::PlanTurn));
	const FString CancelLabel = FString::Printf(TEXT("Cancel (%s)"), *Keys.KeyName(ETMAction::PlanUndo));
	auto Width = [&](const FString& Label) { return TextSize(Label, Font, Scale).X + 24.0f * S; };
	float W = TextSize(Lead, Font, Scale).X + Gap + Width(EndLabel) + Gap + Width(WaitLabel) + Gap + Width(CancelLabel);
	if (Order->bStopped)
	{
		W += Width(GoLabel) + Gap;
	}
	const float PadX = 12.0f * S;
	const float H = ChipH + 16.0f * S;
	const float X0 = Canvas->ClipX * 0.5f - (W + 2.0f * PadX) * 0.5f;
	const float Y0 = Above - H - 8.0f * S;
	Panel(X0, Y0, W + 2.0f * PadX, H, PanelFill,
		Order->bStopped ? FLinearColor(1.0f, 0.54f, 0.48f, 0.6f) : FLinearColor(Blue.R, Blue.G, Blue.B, 0.6f), 1.0f);
	float X = X0 + PadX;
	const float Y = Y0 + 8.0f * S;
	Text(Lead, X, Y + (ChipH - TextSize(Lead, Font, Scale).Y) * 0.5f, Order->bStopped ? FLinearColor(1.0f, 0.68f, 0.62f) : FLinearColor(0.81f, 0.88f, 1.0f), Font, Scale);
	X += TextSize(Lead, Font, Scale).X + Gap;
	if (Order->bStopped)
	{
		MenuButton(X, Y, Width(GoLabel), ChipH, GoLabel, ETMHudAction::PlanGo, -1, true);
		X += Width(GoLabel) + Gap;
	}
	// The two ways a turn on the road ends; the one chosen is lit.
	MenuButton(X, Y, Width(EndLabel), ChipH, EndLabel, ETMHudAction::GoToMode, 0, !Order->bWaitForMe);
	X += Width(EndLabel) + Gap;
	MenuButton(X, Y, Width(WaitLabel), ChipH, WaitLabel, ETMHudAction::GoToMode, 1, Order->bWaitForMe);
	X += Width(WaitLabel) + Gap;
	MenuButton(X, Y, Width(CancelLabel), ChipH, CancelLabel, ETMHudAction::GoToCancel);
	return Y0 - 8.0f * S;
}

float ATMBattleHud::DrawPlanStrip(ATMBattleDirector& From, const TMSim::FUnit& Unit, float Above)
{
	// The plan as the steps it will take -- walk, ability, end of turn -- and
	// its buttons: Go inside a turn, Done for a unit still waiting, Undo, Clear
	// (Docs/design/feat-move-queue.md, B).
	UFont* Font = GEngine->GetMediumFont();
	const FTMSettings& Keys = FTMSettings::Get();
	const ATMBattleDirector::FTMPlan* Plan = From.PlanOf(Unit.Id);
	const float Scale = 0.6f * S;
	const float ChipH = 34.0f * S;
	const float Gap = 8.0f * S;
	const FLinearColor WalkFill(0.43f, 0.66f, 1.0f, 0.2f);
	const FLinearColor AimFill(0.82f, 0.65f, 1.0f, 0.2f);
	struct FChip { FString Label; FLinearColor Fill; FLinearColor Ink; };
	TArray<FChip> Chips;
	if (Plan && Plan->bWalk)
	{
		const int32 Ways = static_cast<int32>(Plan->Via.size());
		Chips.Add({ FString::Printf(TEXT("%s %.1f m%s"), Plan->bSprint ? TEXT("Sprint") : TEXT("Walk"), Plan->Metres,
			Ways > 0 ? *FString::Printf(TEXT(", %d waypoint%s"), Ways, Ways == 1 ? TEXT("") : TEXT("s")) : TEXT("")),
			WalkFill, FLinearColor(0.81f, 0.88f, 1.0f) });
	}
	if (Plan && Plan->HasAbility())
	{
		const TMSim::FAbility* Ability = Unit.Ability(Plan->Slot);
		Chips.Add({ FString(UTF8_TO_TCHAR(Ability ? Ability->Name.c_str() : "Ability")), AimFill, FLinearColor(0.92f, 0.87f, 1.0f) });
	}
	if (Plan && Plan->bWalk && (Plan->HasAbility() || Plan->bSprint))
	{
		Chips.Add({ TEXT("End turn"), FLinearColor(1.0f, 1.0f, 1.0f, 0.06f), Dim });
	}
	const FString Lead = Unit.bReady ? TEXT("This turn:") : TEXT("Next turn:");
	const FString Empty = TEXT("Nothing yet: click where it walks, or pick an ability");
	const bool bGo = Unit.bReady && Plan && !Plan->IsEmpty();
	const FString GoLabel = FString::Printf(TEXT("%s (%s)"), bGo ? TEXT("Go") : TEXT("Done"), *Keys.KeyName(ETMAction::PlanTurn));
	const FString UndoLabel = FString::Printf(TEXT("Undo (%s)"), *Keys.KeyName(ETMAction::PlanUndo));
	const FString ClearLabel = TEXT("Clear");
	auto Width = [&](const FString& Label) { return TextSize(Label, Font, Scale).X + 24.0f * S; };
	const float Arrow = 18.0f * S;
	float W = Width(Lead) + Gap;
	if (Chips.Num() == 0)
	{
		W += TextSize(Empty, Font, Scale).X + Gap;
	}
	for (const FChip& Chip : Chips)
	{
		W += Width(Chip.Label) + Arrow;
	}
	W += Width(GoLabel) + Gap + Width(UndoLabel) + Gap + Width(ClearLabel);
	const float PadX = 12.0f * S;
	const float H = ChipH + 16.0f * S;
	const float X0 = Canvas->ClipX * 0.5f - (W + 2.0f * PadX) * 0.5f;
	const float Y0 = Above - H - 8.0f * S;
	Panel(X0, Y0, W + 2.0f * PadX, H, PanelFill, FLinearColor(Gold.R, Gold.G, Gold.B, 0.55f), 1.0f);
	float X = X0 + PadX;
	const float Y = Y0 + 8.0f * S;
	auto Middle = [&](const FString& Label) { return Y + (ChipH - TextSize(Label, Font, Scale).Y) * 0.5f; };
	Text(Lead, X, Middle(Lead), Dim, Font, Scale);
	X += Width(Lead) - 12.0f * S + Gap;
	if (Chips.Num() == 0)
	{
		Text(Empty, X, Middle(Empty), Dim, Font, Scale);
		X += TextSize(Empty, Font, Scale).X + Gap;
	}
	for (int32 i = 0; i < Chips.Num(); ++i)
	{
		const FChip& Chip = Chips[i];
		const float CW = Width(Chip.Label);
		Panel(X, Y, CW, ChipH, Chip.Fill);
		const FString Numbered = Chip.Label;
		Text(Numbered, X + 12.0f * S, Middle(Numbered), Chip.Ink, Font, Scale);
		X += CW;
		if (i + 1 < Chips.Num())
		{
			const FString To = TEXT(">");
			Text(To, X + (Arrow - TextSize(To, Font, Scale).X) * 0.5f, Middle(To), Dim, Font, Scale);
		}
		X += Arrow;
	}
	MenuButton(X, Y, Width(GoLabel), ChipH, GoLabel, ETMHudAction::PlanGo, -1, true);
	X += Width(GoLabel) + Gap;
	MenuButton(X, Y, Width(UndoLabel), ChipH, UndoLabel, ETMHudAction::PlanUndo);
	X += Width(UndoLabel) + Gap;
	MenuButton(X, Y, Width(ClearLabel), ChipH, ClearLabel, ETMHudAction::PlanClear);
	return Y0 - 8.0f * S;
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
	Y += H + Gap;
	MenuButton(X, Y, W, H, TEXT("Play Online"), ETMHudAction::TitleOnline, -1, false, TEXT("host a battle, or join one"));
	Y += H + Gap * 3.0f;
	MenuButton(X, Y, W, 44.0f * S, TEXT("Replays"), ETMHudAction::TitleReplays);
	Y += 44.0f * S + Gap;
	MenuButton(X, Y, W, 44.0f * S, FString::Printf(TEXT("Codex  (%s)"), *FTMSettings::Get().KeyName(ETMAction::UnitGuide)), ETMHudAction::ToggleGuide);
	Y += 44.0f * S + Gap;
	MenuButton(X, Y, W, 44.0f * S, TEXT("Options"), ETMHudAction::OpenOptions);
	Y += 44.0f * S + Gap;
	MenuButton(X, Y, W, 44.0f * S, TEXT("Developer Tools"), ETMHudAction::OpenDevTools);
	Y += 44.0f * S + Gap;
	MenuButton(X, Y, W, 44.0f * S, TEXT("Quit"), ETMHudAction::Quit);
	Y += 44.0f * S + 30.0f * S;

	// Said rather than left out quietly: what the Godot title has that this one does not yet.
	const FString Missing = TEXT("How to Play is not ported yet.");
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
	// The whole screen always fits, whatever the UI scale: shrunk to the
	// window if it would run off it, so Start is never out of reach.
	const float WasS = S;
	S = FMath::Min(S, FMath::Min((Canvas->ClipY - 16.0f) / 980.0f, (Canvas->ClipX - 16.0f) / 1180.0f));
	ON_SCOPE_EXIT
	{
		S = WasS;
	};
	const bool bVsComputer = Setup.Mode == TEXT("ai");
	const bool bWatch = Setup.Mode == TEXT("cpu");
	const bool bHosting = Setup.Mode == TEXT("online");

	const float PW = 1180.0f * S;
	// Tall enough for the right column's ten rules with Back and Start below
	// them, not over the last (2026-10-01: Start covered Friendly fire).
	const float PH = 980.0f * S;
	const float PX = (Canvas->ClipX - PW) * 0.5f;
	const float PY = FMath::Max(8.0f * S, (Canvas->ClipY - PH) * 0.5f);
	Panel(PX, PY, PW, PH, FLinearColor(0.05f, 0.06f, 0.1f, 0.94f), FLinearColor(0.4f, 0.45f, 0.55f, 0.8f), 1.5f);

	const FString Heading = bVsComputer ? TEXT("Battle Setup: Play vs Computer")
		: bWatch ? TEXT("Battle Setup: Computer vs Computer")
		: bHosting ? TEXT("Battle Setup: Host an Online Game") : TEXT("Battle Setup: Two Players");
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
			: bHosting ? FString(Team == 0 ? TEXT("You (the host)") : TEXT("Your opponent"))
			: bVsComputer ? FString(TEXT("You")) : FString::Printf(TEXT("Player %d"), Team + 1);
		Text(FString::Printf(TEXT("%s   %s"), Team == 0 ? TEXT("Blue") : TEXT("Red"), *Who),
			CX, ColumnTop, TeamColour(Team), Font, 0.8f * S);
		// Its item points, on the right of its heading.
		if (Setup.ItemBudget > 0)
		{
			const FString Points = From.PicksOwnItems(Team) ? FString(TEXT("picks its own items"))
				: FString::Printf(TEXT("Items %d / %d points"), From.ItemPointsSpent(Team), Setup.ItemBudget);
			const FVector2D PointsSize = TextSize(Points, Font, 0.5f * S);
			Text(Points, CX + ColumnW - PointsSize.X, ColumnTop + 6.0f * S, Dim, Font, 0.5f * S);
		}

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
			// The class, and beside it the unit's three item slots when there are points to spend.
			const float Box = 44.0f * S;
			const float ItemsW = Setup.ItemBudget > 0 ? 3.0f * (Box + 6.0f * S) : 0.0f;
			MenuButton(CX, Y, ColumnW - ItemsW, 56.0f * S, Name, ETMHudAction::SetupClass, Team * 4 + Slot, false,
				Roles.IsEmpty() ? FString(TEXT("click to choose")) : Roles + TEXT("   (click to choose)"));
			for (int32 Item = 0; Item < 3 && Setup.ItemBudget > 0; ++Item)
			{
				const float BX = CX + ColumnW - ItemsW + 6.0f * S + Item * (Box + 6.0f * S);
				const float BY = Y + 6.0f * S;
				if (From.PicksOwnItems(Team))
				{
					Panel(BX, BY, Box, Box, FLinearColor(0.03f, 0.04f, 0.07f, 0.8f), FLinearColor(0.3f, 0.33f, 0.4f, 0.5f), 1.0f);
					Text(TEXT("?"), BX + Box * 0.4f, BY + Box * 0.25f, Dim, Font, 0.6f * S);
					AddTip(BX, BY, Box, Box, TEXT("The computer picks this unit's items when the battle starts."));
					continue;
				}
				const TMSim::FItemDef* Carried = TMSim::FindItem(Setup.Items[Team][Slot][Item]);
				ItemBadge(Carried, BX, BY, Box, Carried != nullptr);
				if (!Carried)
				{
					AddTip(BX, BY, Box, Box, TEXT("An empty item slot: click to choose an item."));
				}
				AddButton(BX, BY, Box, Box, ETMHudAction::SetupItem, Team * 12 + Slot * 3 + Item);
			}
			Y += 64.0f * S;
		}
		const float Half = (ColumnW - 10.0f * S) * 0.5f;
		MenuButton(CX, Y, Half, 40.0f * S, TEXT("Random team"), ETMHudAction::SetupRandom, Team);
		MenuButton(CX + Half + 10.0f * S, Y, Half, 40.0f * S, TEXT("Default"), ETMHudAction::SetupDefault, Team);
	}

	// Who plays, how hard, and the seed.
	float Y = ColumnTop + 36.0f * S + 4.0f * 64.0f * S + 60.0f * S;
	float RowX = PX + 30.0f * S;
	const float LabelW = 150.0f * S;
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
	// Online, every match gets a fresh seed from the host.
	if (!bHosting)
	{
		Row(TEXT("Seed"), Setup.bRandomSeed ? FString(TEXT("New each battle"))
			: FString::Printf(TEXT("Fixed: %llu"), Setup.FixedSeed), ETMHudAction::SetupSeed, -1);
	}
	{
		const TMSim::FMapDef& Map = TMSim::FindMap(Setup.MapId);
		Row(TEXT("Map"), UTF8_TO_TCHAR(Map.Name.c_str()), ETMHudAction::SetupMap, -1);
		AddTip(RowX, Y - 46.0f * S, LabelW + ValueW, 38.0f * S, FString::Printf(TEXT("%hs\n%d x %d tiles"),
			Map.Desc.c_str(), static_cast<int32>(Map.Top[0].size()), static_cast<int32>(Map.Top.size() * 2)));
		const FString Look = Setup.ThemeId.IsEmpty() ? FString(UTF8_TO_TCHAR(Map.Theme.c_str())) : Setup.ThemeId;
		const ATMBattleDirector::FTMTheme* Theme = From.Themes.Find(Look);
		Row(TEXT("Theme"), (Theme ? Theme->Name : Look) + (Setup.ThemeId.IsEmpty() ? TEXT(" (the map's own)") : TEXT("")),
			ETMHudAction::SetupTheme, -1);
		AddTip(RowX, Y - 46.0f * S, LabelW + ValueW, 38.0f * S,
			TEXT("How the battlefield looks: the ground, the land around it, the sun and the sky. It changes nothing in the rules."));
		Row(TEXT("Duplicate classes"), Setup.bUniqueClasses ? FString(TEXT("Off: one of each class")) : FString(TEXT("Allowed")),
			ETMHudAction::SetupUniqueClasses, -1);
		AddTip(RowX, Y - 46.0f * S, LabelW + ValueW, 38.0f * S,
			TEXT("Off: no class appears twice in the battle, on either side. A class already on a slot is greyed in the class picker, and turning this on gives any second copy another class of its role. Online, the host's setting holds for everyone."));
	}

	// How the battle can be won, on the right under red's team.
	const float NoteX = PX + PW * 0.5f + 40.0f * S;
	{
		const float LeftY = Y;
		Y = ColumnTop + 36.0f * S + 4.0f * 64.0f * S + 60.0f * S;
		const float LeftRowX = RowX;
		RowX = NoteX;
		auto Seconds = [](double Value)
		{
			return FMath::Fmod(Value, 60.0) == 0.0
				? FString::Printf(TEXT("%.0f minutes"), Value / 60.0)
				: FString::Printf(TEXT("%.0f seconds"), Value);
		};
		Row(TEXT("Victory"), Setup.CaptureSeconds > 0.0
				? FString::Printf(TEXT("Hold the middle: %.0fs"), Setup.CaptureSeconds)
				: FString(TEXT("Last team standing")), ETMHudAction::SetupVictory, -1);
		AddTip(RowX, Y - 46.0f * S, LabelW + ValueW, 38.0f * S,
			TEXT("Hold the middle: a side that stands alone in the ring in the middle of the map for this long wins. While both sides are in it, neither gains; a side pushed out keeps what it held."));
		Row(TEXT("Time"), Setup.BattleSeconds > 0.0 ? Seconds(Setup.BattleSeconds) : FString(TEXT("No time limit")),
			ETMHudAction::SetupTime, -1);
		AddTip(RowX, Y - 46.0f * S, LabelW + ValueW, 38.0f * S,
			TEXT("When time runs out, the side with more of its health left wins. Within a point of each other is a draw."));
		Row(TEXT("Planning"), Setup.PlanningSeconds > 0.0 ? FString::Printf(TEXT("%.0f seconds"), Setup.PlanningSeconds)
			: FString(TEXT("No planning")), ETMHudAction::SetupPlanning, -1);
		AddTip(RowX, Y - 46.0f * S, LabelW + ValueW, 38.0f * S,
			TEXT("Time before the fighting to put your units where you want them in your own spawn area. It ends early once both sides are ready; the computer never says it is, so against it the time runs out."));
		Row(TEXT("Watchtowers"), Setup.Watchtowers > 0 ? FString::Printf(TEXT("%d, placed at random"), Setup.Watchtowers)
			: FString(TEXT("None")), ETMHudAction::SetupTowers, -1);
		const float TowerRowY = Y - 46.0f * S;
		Row(TEXT("Item points"), Setup.ItemBudget > 0 ? FString::Printf(TEXT("%d per side"), Setup.ItemBudget)
			: FString(TEXT("No items")), ETMHudAction::SetupItemBudget, -1);
		AddTip(RowX, Y - 46.0f * S, LabelW + ValueW, 38.0f * S,
			TEXT("Points each side spends on items before the battle: common items cost 1, uncommon 2, rare 3. Epic items can't be bought. Each unit carries up to three. Click a unit's empty slot to choose; the computer picks its own."));
		AddTip(RowX, TowerRowY, LabelW + ValueW, 38.0f * S,
			FString::Printf(TEXT("Towers placed somewhere new each battle, in mirrored pairs so neither side has a nearer one (an odd one stands in the middle; a small map may have room for fewer). Stand next to one and press Capture: it takes that unit's whole turn, and %d such turns take the tower (Developer Tools: Watchtower capture). A side that holds a tower sees %.0f m around it."),
				FMath::Max(1, TMSim::RoundToInt(From.Battle.Tuning.WatchtowerTurns)), From.Battle.Tuning.WatchtowerSight));
		static const TCHAR* CampWords[] = { TEXT("Off"), TEXT("Light"), TEXT("Standard"), TEXT("Wild") };
		Row(TEXT("Neutral camps"), CampWords[FMath::Clamp(Setup.CampLevel, 0, 3)], ETMHudAction::SetupCamps, -1);
		AddTip(RowX, Y - 46.0f * S, LabelW + ValueW, 38.0f * S,
			TEXT("Monster camps in mirrored pairs, waking over time: easy ones at once, harder ones later, and the map's boss in the middle (Standard and Wild). Their temperament shows over the camp. Beaten camps leave items on the ground to take; with Camp respawns on, they come back a while later. Monsters never fight each other."));
		const TMSim::FMapDef& BossMap = TMSim::FindMap(Setup.MapId);
		const TMSim::FJobDef* Boss = TMSim::FindJob(BossMap.Boss);
		Row(TEXT("Camp respawns"), Setup.bCampRespawn ? FString(TEXT("On: cleared camps come back")) : FString(TEXT("Off: cleared stays cleared")),
			ETMHudAction::SetupCampRespawn, -1);
		AddTip(RowX, Y - 46.0f * S, LabelW + ValueW, 38.0f * S,
			TEXT("On, a cleared camp wakes again a while later (easy after 60 s, up to 5 minutes for the boss). Off, each camp can be cleared once."));
		Row(TEXT("Boss"), Setup.bRandomBoss ? FString(TEXT("Random each battle"))
			: FString::Printf(TEXT("%hs (the map's own)"), Boss ? Boss->Name.c_str() : "none"), ETMHudAction::SetupBoss, -1);
		AddTip(RowX, Y - 46.0f * S, LabelW + ValueW, 38.0f * S,
			TEXT("Which boss wakes at the boss camp, when camps are Standard or Wild: the one that belongs to the map, or any of them, chosen by the battle's seed."));
		Row(TEXT("Elements"), Setup.bElements ? FString(TEXT("Reactions on")) : FString(TEXT("Off")), ETMHudAction::SetupElements, -1);
		AddTip(RowX, Y - 46.0f * S, LabelW + ValueW, 38.0f * S,
			TEXT("Water abilities leave their target Wet and ice abilities Chill it. Lightning then stuns the Wet (and the Wet near them), ice freezes them, and fire sets the Oiled burning. Off, only abilities that say so apply Wet or Chilled; the reactions still happen."));
		Row(TEXT("Friendly fire"), Setup.bFriendlyFire ? FString(TEXT("On: area blows hit allies")) : FString(TEXT("Off")),
			ETMHudAction::SetupFriendlyFire, -1);
		AddTip(RowX, Y - 46.0f * S, LabelW + ValueW, 38.0f * S,
			TEXT("On: cones, lines, charges and blasts aimed at the enemy also hurt your own units standing in them (never the caster, never a single-target blow). Aim with care."));
		// The boss's hunt and claim (2026-10-02, "Camps and Bosses Mockups" C and D).
		Row(TEXT("Bosses hunt"), Setup.bBossHunt ? FString(TEXT("On: it hunts whoever hurt it most")) : FString(TEXT("Off")),
			ETMHudAction::SetupBossHunt, -1);
		AddTip(RowX, Y - 46.0f * S, LabelW + ValueW, 38.0f * S,
			TEXT("On: a boss remembers who has hurt it most, marks them Hunted, and goes for them first, until they fall or it loses sight of them for 3 of its turns; then it turns to the next on its list. The boss bar shows the list."));
		Row(TEXT("Claim the boss"), Setup.bBossClaim ? FString(TEXT("On: the last blow takes the boon")) : FString(TEXT("Off")),
			ETMHudAction::SetupBossClaim, -1);
		AddTip(RowX, Y - 46.0f * S, LabelW + ValueW, 38.0f * S,
			TEXT("On: the boss bar shows each side's share of the damage. The side that lands the last blow gets the Boss's Boon (+10% damage for 3 turns); the other side, if it dealt 30% of the boss's health, gets a rare item in its stash."));
		RowX = LeftRowX;
		Y = LeftY;
	}

	// Back, and start.
	const float BW = 220.0f * S;
	const float BH = 52.0f * S;
	const float BY = PY + PH - BH - 24.0f * S;
	MenuButton(PX + 30.0f * S, BY, 160.0f * S, BH, TEXT("Back  (Esc)"), ETMHudAction::SetupBack);
	if (!bHosting)
	{
		MenuButton(PX + PW - BW - 30.0f * S, BY, BW, BH, TEXT("Start Battle"), ETMHudAction::SetupStart, -1, true);
		return;
	}
	// Hosting: the lobby is open, and this screen changed its rules.
	if (From.Net.IsValid() && From.Net->IsHost())
	{
		MenuButton(PX + PW - BW - 30.0f * S, BY, BW, BH, TEXT("Back to lobby"), ETMHudAction::SetupStart, -1, true);
	}
	else if (From.IsWaitingOnline())
	{
		Panel(PX + PW - BW - 30.0f * S, BY, BW, BH, FLinearColor(0.1f, 0.12f, 0.18f, 0.95f), Gold, 1.0f);
		const FString Waiting = TEXT("Waiting for an opponent...");
		const FVector2D WaitSize = TextSize(Waiting, Font, 0.55f * S);
		Text(Waiting, PX + PW - BW - 30.0f * S + (BW - WaitSize.X) * 0.5f, BY + (BH - WaitSize.Y) * 0.5f, Gold, Font, 0.55f * S);
	}
	else
	{
		MenuButton(PX + PW - BW - 30.0f * S, BY, BW, BH, TEXT("Host Game"), ETMHudAction::SetupStart, -1, true,
			FString::Printf(TEXT("on port %s"), *From.JoinPort));
	}
	float StatusY = BY - 76.0f * S;
	for (const FString& Line : { From.OnlineStatus, From.OnlineAddresses, From.OnlineRouter })
	{
		if (!Line.IsEmpty())
		{
			Text(Line, PX + 210.0f * S, StatusY, Line == From.OnlineStatus ? TextColour : Dim, Font, 0.5f * S);
			StatusY += 22.0f * S;
		}
	}
}

void ATMBattleHud::TextField(float X, float Y, float W, float H, const FString& Value, const FString& Placeholder, bool bTyping, int32 Field)
{
	UFont* Font = GEngine->GetMediumFont();
	Panel(X, Y, W, H, FLinearColor(0.02f, 0.03f, 0.05f, 0.95f), bTyping ? Gold : FLinearColor(0.4f, 0.45f, 0.55f, 0.8f), bTyping ? 2.0f : 1.0f);
	const bool bEmpty = Value.IsEmpty();
	// A caret that blinks while typing.
	const bool bCaret = bTyping && FMath::Fmod(GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0, 1.0) < 0.55;
	const FString Shown = (bEmpty && !bTyping ? Placeholder : Value) + (bCaret ? TEXT("|") : TEXT(""));
	const FVector2D Size = TextSize(Shown.IsEmpty() ? FString(TEXT("A")) : Shown, Font, 0.6f * S);
	Text(Shown, X + 10.0f * S, Y + (H - Size.Y) * 0.5f, bEmpty && !bTyping ? Dim : TextColour, Font, 0.6f * S);
	AddButton(X, Y, W, H, ETMHudAction::OnlineField, Field);
}

void ATMBattleHud::DrawOnline(ATMBattleDirector& From)
{
	// main_menu.gd:79-230: an address and a port, Host or Join, and what is happening.
	DrawRect(FLinearColor(0.02f, 0.03f, 0.06f, 0.6f), 0.0f, 0.0f, Canvas->ClipX, Canvas->ClipY);
	UFont* Font = GEngine->GetMediumFont();
	UFont* Big = GEngine->GetLargeFont();
	const float PW = 940.0f * S;
	const float PH = 560.0f * S;
	const float PX = (Canvas->ClipX - PW) * 0.5f;
	const float PY = (Canvas->ClipY - PH) * 0.5f;
	Panel(PX, PY, PW, PH, FLinearColor(0.05f, 0.06f, 0.1f, 0.95f), Gold, 1.5f);
	Text(TEXT("Play Online"), PX + 30.0f * S, PY + 20.0f * S, Gold, Big, 0.9f * S);
	Text(TEXT("The host chooses the battle and plays Blue; the other player joins with the host's address."),
		PX + 30.0f * S, PY + 70.0f * S, Dim, Font, 0.55f * S);

	using EField = ATMBattleDirector::ETypeField;
	float Y = PY + 120.0f * S;
	const float FieldH = 48.0f * S;
	const float AddressW = PW - 60.0f * S - 190.0f * S;
	Text(TEXT("Host address"), PX + 30.0f * S, Y, Dim, Font, 0.55f * S);
	Text(TEXT("Port"), PX + 30.0f * S + AddressW + 30.0f * S, Y, Dim, Font, 0.55f * S);
	Y += 26.0f * S;
	TextField(PX + 30.0f * S, Y, AddressW, FieldH, From.JoinAddress, TEXT("click and type the host's address"),
		From.Typing == EField::Address, static_cast<int32>(EField::Address));
	TextField(PX + 30.0f * S + AddressW + 30.0f * S, Y, 160.0f * S, FieldH, From.JoinPort, TEXT("7777"),
		From.Typing == EField::Port, static_cast<int32>(EField::Port));
	Y += FieldH + 24.0f * S;

	const float BW = (PW - 60.0f * S - 20.0f * S) * 0.5f;
	const float BH = 56.0f * S;
	const bool bBusy = From.IsWaitingOnline();
	if (bBusy)
	{
		Panel(PX + 30.0f * S, Y, PW - 60.0f * S, BH, FLinearColor(0.1f, 0.12f, 0.18f, 0.95f), Gold, 1.0f);
		const FString Busy = TEXT("Connecting... (Back to give up)");
		const FVector2D BusySize = TextSize(Busy, Font, 0.62f * S);
		Text(Busy, PX + (PW - BusySize.X) * 0.5f, Y + (BH - BusySize.Y) * 0.5f, Gold, Font, 0.62f * S);
	}
	else
	{
		MenuButton(PX + 30.0f * S, Y, BW, BH, TEXT("Host Game"), ETMHudAction::OnlineHost, -1, true, TEXT("choose the battle, then wait for a player"));
		MenuButton(PX + 30.0f * S + BW + 20.0f * S, Y, BW, BH, TEXT("Join Game"), ETMHudAction::OnlineJoin, -1, true, TEXT("connect to the address above"));
	}
	Y += BH + 30.0f * S;

	for (const FString& Line : { From.OnlineStatus, From.OnlineAddresses, From.OnlineRouter })
	{
		if (!Line.IsEmpty())
		{
			Text(Line, PX + 30.0f * S, Y, Line == From.OnlineStatus ? TextColour : Dim, Font, 0.55f * S);
			Y += 26.0f * S;
		}
	}

	const float HelpY = PY + PH - 150.0f * S;
	const TCHAR* Help[] =
	{
		TEXT("On the same network, join with the address the host's screen shows."),
		TEXT("Over the internet the host needs TCP port 7777 open: the game asks the router itself, and says if it can't."),
		TEXT("If it can't, forward the port on the router by hand, or both players join a VPN such as Tailscale."),
		TEXT("Both players need the same build of the game, and the same class files."),
	};
	float LineY = HelpY;
	for (const TCHAR* Line : Help)
	{
		Text(Line, PX + 30.0f * S, LineY, Dim, Font, 0.5f * S);
		LineY += 22.0f * S;
	}
	MenuButton(PX + 30.0f * S, PY + PH - 60.0f * S, 160.0f * S, 44.0f * S, TEXT("Back  (Esc)"), ETMHudAction::OnlineBack);
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
	const float PH = 6.0f * (H + Gap) + 100.0f * S;
	const float PX = (Canvas->ClipX - PW) * 0.5f;
	const float PY = (Canvas->ClipY - PH) * 0.5f;
	Panel(PX, PY, PW, PH, FLinearColor(0.05f, 0.06f, 0.1f, 0.95f), Gold, 1.5f);
	const FString Title = From.bOnline ? TEXT("Menu (the battle goes on)") : TEXT("Paused");
	const FVector2D Size = TextSize(Title, Big, 0.9f * S);
	Text(Title, PX + (PW - Size.X) * 0.5f, PY + 18.0f * S, Gold, Big, 0.9f * S);

	float Y = PY + 80.0f * S;
	const float X = PX + 30.0f * S;
	MenuButton(X, Y, W, H, TEXT("Resume  (Esc)"), ETMHudAction::MenuResume, -1, true);
	Y += H + Gap;
	if (!From.bOnline)
	{
		MenuButton(X, Y, W, H, TEXT("Restart battle"), ETMHudAction::MenuRestart, -1, false,
			FString::Printf(TEXT("same teams, same seed (%llu)"), From.BattleSeed));
		Y += H + Gap;
	}
	MenuButton(X, Y, W, H, TEXT("Options"), ETMHudAction::OpenOptions);
	Y += H + Gap;
	MenuButton(X, Y, W, H, TEXT("Developer Tools"), ETMHudAction::OpenDevTools);
	Y += H + Gap;
	MenuButton(X, Y, W, H, TEXT("Change setup"), ETMHudAction::MenuSetup);
	Y += H + Gap;
	MenuButton(X, Y, W, H, TEXT("Main menu"), ETMHudAction::MenuTitle);
}