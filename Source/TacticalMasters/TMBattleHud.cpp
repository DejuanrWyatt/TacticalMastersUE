#include "TMBattleHud.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"

#include "TMBattleDirector.h"
#include "SimAbility.h"

#include <algorithm>
#include <cmath>

namespace
{
	// The Godot HUD's colours (hud.gd:51, 75-79).
	const FLinearColor TextColour(0.92f, 0.94f, 1.0f);
	const FLinearColor Dim(0.92f, 0.94f, 1.0f, 0.55f);
	const FLinearColor Gold(1.0f, 0.82f, 0.35f);
	const FLinearColor Urgent(1.0f, 0.38f, 0.32f);
	const FLinearColor CastColour(0.75f, 0.45f, 1.0f);
	const FLinearColor PanelFill(0.06f, 0.08f, 0.12f, 0.78f);
	const FLinearColor Shadow(0.0f, 0.0f, 0.0f, 0.8f);

	FLinearColor TeamColour(int32 Team)
	{
		return Team == 0 ? FLinearColor(0.47f, 0.7f, 1.0f) : FLinearColor(1.0f, 0.51f, 0.47f);
	}

	FLinearColor TeamFill(int32 Team)
	{
		return Team == 0 ? FLinearColor(0.1f, 0.2f, 0.42f, 0.95f) : FLinearColor(0.42f, 0.12f, 0.1f, 0.95f);
	}

	// The turn order timeline (hud.gd:56-74).
	const float TimelineSeconds = 30.0f;
	const float RowHeight = 42.0f;
	const float ChipSize = 36.0f;
	const float ChipGap = 3.0f;
	const int32 ReadySlots = 4;
	const float TrackStart = ReadySlots * (ChipSize + ChipGap) + 10.0f;
	const float TickSeconds[] = { 0.0f, 1.0f, 3.0f, 5.0f, 10.0f, 20.0f, 30.0f };

	const float Tps = static_cast<float>(TMSim::Pace::TicksPerSecond);

	/**
	 * The engine's fonts are drawn for a small screen. Every text scale below is
	 * written as a share of a readable size and multiplied up here, once.
	 */
	const float FontBoost = 2.2f;

	/** Ready first, least time left, then id -- the same order the director selects in. */
	bool SoonerReady(const TMSim::FUnit* A, const TMSim::FUnit* B)
	{
		if (A->Clock != B->Clock)
		{
			return A->Clock < B->Clock;
		}
		return A->Id < B->Id;
	}

	/** Two letters for a chip, from the class name: "Black Mage" is BM, "Knight" Kn. */
	FString Initials(const FString& Name)
	{
		TArray<FString> Words;
		Name.ParseIntoArray(Words, TEXT(" "));
		if (Words.Num() >= 2)
		{
			return Words[0].Left(1) + Words[1].Left(1);
		}
		return Name.Left(2);
	}
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
	ATMBattleDirector* Found = FindDirector();
	if (!Canvas || !Found || !Found->bBuilt || !GEngine)
	{
		return;
	}
	S = Canvas->ClipY / 1080.0f;

	// Under everything else, since it is drawn onto the board.
	DrawBoardAids(*Found);
	DrawTurnOrder(*Found);
	DrawLog(*Found);
	DrawUnitCard(*Found);
	DrawActionBar(*Found);
	DrawBanners(*Found);
}

void ATMBattleHud::DrawBoardAids(ATMBattleDirector& From)
{
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

void ATMBattleHud::DrawTurnOrder(ATMBattleDirector& From)
{
	// Two bars, one per team. A chip slides along its team's bar toward the READY
	// zone at the left, placed by seconds until ready on a square-root scale so
	// the last seconds get the most room (hud.gd:52-74, 307-311). Godot merges
	// chips that land on one another into a framed group; not done here yet, so
	// two units due at the same moment overlap.
	const float X0 = 16.0f * S;
	const float Y0 = 12.0f * S;
	const float Width = Canvas->ClipX - 32.0f * S;
	const float Track = TrackStart * S;
	const float Chip = ChipSize * S;
	auto BarX = [&](float Seconds)
	{
		const float Frac = FMath::Sqrt(FMath::Clamp(Seconds / TimelineSeconds, 0.0f, 1.0f));
		return X0 + Track + Frac * (Width - Track - Chip * 0.5f);
	};

	UFont* Font = GEngine->GetMediumFont();
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

	for (int32 Team = 0; Team < 2; ++Team)
	{
		const float RowY = Y0 + Team * RowHeight * S;
		const float LineY = RowY + Chip * 0.5f + 2.0f * S;
		Panel(X0 - 6.0f * S, RowY - 3.0f * S, Width + 12.0f * S, (RowHeight - 2.0f) * S, PanelFill);
		// The ready zone, and the bar with its marks.
		DrawRect(FLinearColor(Gold.R, Gold.G, Gold.B, 0.12f), X0, RowY, Track - 6.0f * S, Chip);
		DrawRect(TeamColour(Team) * FLinearColor(1, 1, 1, 0.5f), X0 + Track, LineY - 1.5f * S, Width - Track, 3.0f * S);
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

		std::vector<const TMSim::FUnit*> Ready;
		std::vector<const TMSim::FUnit*> Waiting;
		for (const TMSim::FUnit& Unit : From.Battle.Units)
		{
			if (Unit.Team != Team || !Unit.IsAlive())
			{
				continue;
			}
			(Unit.bReady ? Ready : Waiting).push_back(&Unit);
		}
		std::sort(Ready.begin(), Ready.end(), SoonerReady);

		auto DrawChip = [&](const TMSim::FUnit& Unit, float X, const FString& Badge)
		{
			// Gold, red or purple outline when ready, urgent or casting; white when
			// selected (hud.gd:1088-1107).
			FLinearColor Edge = TeamColour(Team) * FLinearColor(1, 1, 1, 0.6f);
			float Thick = 1.5f;
			if (Unit.IsCasting())
			{
				Edge = CastColour;
				Thick = 2.5f;
			}
			else if (Unit.bReady)
			{
				Edge = Unit.Clock <= 5 * TMSim::Pace::TicksPerSecond ? Urgent : Gold;
				Thick = 2.5f;
			}
			if (Unit.Id == From.SelectedId)
			{
				Edge = FLinearColor::White;
				Thick = 3.5f;
			}
			Panel(X, RowY, Chip, Chip, TeamFill(Team), Edge, Thick);
			const FString Letters = Initials(JobName(Unit));
			const FVector2D Size = TextSize(Letters, Font, 0.58f * S);
			Text(Letters, X + (Chip - Size.X) * 0.5f, RowY + 1.0f * S, TextColour, Font, 0.58f * S);
			const FVector2D BadgeSize = TextSize(Badge, Font, 0.42f * S);
			Text(Badge, X + (Chip - BadgeSize.X) * 0.5f, RowY + Chip - BadgeSize.Y - 1.0f * S,
				Unit.bReady ? Gold : Dim, Font, 0.42f * S);
			AddButton(X, RowY, Chip, Chip, ETMHudAction::PickUnit, Unit.Id);
		};

		for (size_t i = 0; i < Ready.size(); ++i)
		{
			const TMSim::FUnit& Unit = *Ready[i];
			const float X = X0 + static_cast<float>(i) * (ChipSize + ChipGap) * S;
			// A ready unit's badge is its countdown, or the cast it is holding.
			const FString Badge = Unit.IsCasting()
				? FString::Printf(TEXT("c%.0f"), FMath::CeilToFloat(Unit.Casting.Ticks / Tps))
				: FString::Printf(TEXT("%.0f"), FMath::CeilToFloat(Unit.Clock / Tps));
			DrawChip(Unit, X, Badge);
		}
		for (const TMSim::FUnit* Unit : Waiting)
		{
			// A cast in flight is shown by when it lands, since that is when
			// anything happens; otherwise by when the unit is next ready.
			const float Seconds = Unit->IsCasting()
				? Unit->Casting.Ticks / Tps
				: From.Battle.TicksToReady(*Unit) / Tps;
			const FString Badge = Unit->IsCasting()
				? FString::Printf(TEXT("c%.0f"), FMath::CeilToFloat(Seconds))
				: FString::Printf(TEXT("%.0f"), FMath::CeilToFloat(Seconds));
			DrawChip(*Unit, BarX(Seconds) - Chip * 0.5f, Badge);
		}

		// Whoever the pointer is over, named, since two letters are not a name.
		for (const TMSim::FUnit& Unit : From.Battle.Units)
		{
			if (Unit.Team != Team)
			{
				continue;
			}
			FTMHudButton Over;
			if (ButtonAt(Mouse, Over) && Over.Action == ETMHudAction::PickUnit && Over.Value == Unit.Id)
			{
				const FString Name = FString::Printf(TEXT("%s %d  hp %d/%d"), *JobName(Unit), Unit.Id, Unit.Hp, Unit.MaxHp());
				const FVector2D Size = TextSize(Name, Font, 0.62f * S);
				Panel(Mouse.X + 12.0f * S, Mouse.Y + 12.0f * S, Size.X + 12.0f * S, Size.Y + 6.0f * S, PanelFill);
				Text(Name, Mouse.X + 18.0f * S, Mouse.Y + 15.0f * S, TeamColour(Team), Font, 0.62f * S);
			}
		}
	}
}

void ATMBattleHud::DrawLog(ATMBattleDirector& From)
{
	// The last few lines of the fight, under the turn order on the left.
	const int32 Shown = 10;
	if (From.Log.Num() == 0)
	{
		return;
	}
	UFont* Font = GEngine->GetMediumFont();
	const float Scale = 0.5f * S;
	const float LineH = TextSize(TEXT("Ag"), Font, Scale).Y;
	const int32 First = FMath::Max(0, From.Log.Num() - Shown);
	const int32 Count = From.Log.Num() - First;
	const float X = 16.0f * S;
	const float Y = 12.0f * S + 2.0f * RowHeight * S + 18.0f * S;
	const float W = 520.0f * S;
	Panel(X - 6.0f * S, Y - 4.0f * S, W, Count * LineH + 8.0f * S, FLinearColor(0.06f, 0.08f, 0.12f, 0.55f));
	for (int32 i = 0; i < Count; ++i)
	{
		// Older lines fade, so the eye goes to what just happened.
		const float Age = static_cast<float>(Count - 1 - i) / FMath::Max(1, Count - 1);
		const FLinearColor Colour(0.8f, 0.83f, 0.9f, 1.0f - 0.5f * Age);
		Text(From.Log[First + i], X, Y + i * LineH, Colour, Font, Scale);
	}
}

void ATMBattleHud::DrawUnitCard(ATMBattleDirector& From)
{
	// The selected unit; while watching, the unit whose turn it is; otherwise the
	// one under the pointer (hud.gd:1390-1427).
	const TMSim::FUnit* Unit = From.SelectedUnit();
	if (!Unit && !From.bPlayerInput)
	{
		Unit = From.WaitingOn();
	}
	if (!Unit)
	{
		Unit = From.Battle.FindUnit(From.HoverUnitId);
	}
	if (!Unit || (!Unit->IsAlive() && !Unit->IsKo()))
	{
		return;
	}

	UFont* Font = GEngine->GetMediumFont();
	UFont* Big = GEngine->GetLargeFont();
	const float W = 440.0f * S;
	const float H = 238.0f * S;
	const float X = 16.0f * S;
	const float Y = Canvas->ClipY - H - 16.0f * S;
	Panel(X, Y, W, H, PanelFill, TeamColour(Unit->Team) * FLinearColor(1, 1, 1, 0.5f), 1.5f);

	const float Pad = 10.0f * S;
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
		Sub = FString::Printf(TEXT("READY  %.0fs left"), FMath::CeilToFloat(Left));
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
	Row += 26.0f * S;

	const float GaugeW = W - 2.0f * Pad;
	const float GaugeH = 22.0f * S;
	const int32 MaxHp = FMath::Max(1, Unit->MaxHp());
	const float HpPart = static_cast<float>(Unit->Hp) / MaxHp;
	Gauge(X + Pad, Row, GaugeW, GaugeH, HpPart,
		HpPart > 0.5f ? FLinearColor(0.25f, 0.7f, 0.35f) : HpPart > 0.25f ? FLinearColor(0.8f, 0.65f, 0.2f) : FLinearColor(0.8f, 0.25f, 0.2f),
		FString::Printf(TEXT("HP  %d / %d"), Unit->Hp, Unit->MaxHp()));
	Row += GaugeH + 4.0f * S;

	if (Unit->IsCasting())
	{
		const TMSim::FAbility* Ability = TMSim::JobAbility(Unit->Job, Unit->Casting.Slot);
		const float Done = 1.0f - static_cast<float>(Unit->Casting.Ticks) / FMath::Max(1, Unit->Casting.Total);
		Gauge(X + Pad, Row, GaugeW, GaugeH, Done, CastColour * FLinearColor(1, 1, 1, 0.8f),
			FString::Printf(TEXT("Casting %hs  %.1fs"), Ability ? Ability->Name.c_str() : "", Unit->Casting.Ticks / Tps));
	}
	else if (Unit->bReady)
	{
		Gauge(X + Pad, Row, GaugeW, GaugeH, 1.0f, Gold * FLinearColor(1, 1, 1, 0.7f), TEXT("TG  READY"));
	}
	else
	{
		Gauge(X + Pad, Row, GaugeW, GaugeH, static_cast<float>(Unit->Tg) / TMSim::Pace::TgMax, FLinearColor(0.3f, 0.45f, 0.8f),
			FString::Printf(TEXT("TG  %d%%"), Unit->Tg * 100 / TMSim::Pace::TgMax));
	}
	Row += GaugeH + 4.0f * S;

	Gauge(X + Pad, Row, GaugeW, GaugeH, static_cast<float>(Unit->Ult) / TMSim::Pace::UltMax,
		Unit->Ult >= TMSim::Pace::UltMax ? FLinearColor(1.0f, 0.95f, 0.6f, 0.85f) : FLinearColor(0.55f, 0.5f, 0.3f),
		FString::Printf(TEXT("ULT  %d%%"), Unit->Ult));
	Row += GaugeH + 8.0f * S;

	// The numbers, as hud.gd:1416 prints them.
	Text(FString::Printf(TEXT("DEF %d  MDF %d  CRIT %d%%"),
		Unit->Stat(TMSim::EStat::AttDef), Unit->Stat(TMSim::EStat::MagDef), Unit->Stat(TMSim::EStat::Crit)),
		X + Pad, Row, Dim, Font, 0.52f * S);
	Row += 20.0f * S;
	Text(FString::Printf(TEXT("AEV %d%%  MEV %d%%  SPD %d  MOV %.1fm  PAT %d  SGT %.1fm"),
		Unit->Stat(TMSim::EStat::AEva), Unit->Stat(TMSim::EStat::MEva), Unit->Stat(TMSim::EStat::Speed),
		From.Battle.MoveOf(*Unit), Unit->Stat(TMSim::EStat::Patience), From.Battle.SightOf(*Unit)),
		X + Pad, Row, Dim, Font, 0.52f * S);
}

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
		else if (!From.bPlayerInput)
		{
			Line = From.Battle.Winner == 0 ? TEXT("Blue wins") : TEXT("Red wins");
			Colour = TeamColour(From.Battle.Winner);
		}
		else
		{
			Line = From.ComputerPlays(From.Battle.Winner) ? TEXT("The computer wins") : TEXT("You win!");
			Colour = From.ComputerPlays(From.Battle.Winner) ? Urgent : Gold;
		}
		const FString Time = FString::Printf(TEXT("after %.0f seconds"), From.Battle.TickCount / Tps);
		const FVector2D Size = TextSize(Line, Big, 1.0f * S);
		const float PW = FMath::Max(Size.X + 80.0f * S, 420.0f * S);
		const float PH = 190.0f * S;
		const float PX = CentreX - PW * 0.5f;
		const float PY = Canvas->ClipY * 0.32f;
		Panel(PX, PY, PW, PH, FLinearColor(0.05f, 0.06f, 0.1f, 0.92f), Colour, 2.0f);
		Text(Line, CentreX - Size.X * 0.5f, PY + 20.0f * S, Colour, Big, 1.0f * S);
		const FVector2D TimeSize = TextSize(Time, Font, 0.65f * S);
		Text(Time, CentreX - TimeSize.X * 0.5f, PY + 72.0f * S, Dim, Font, 0.65f * S);
		if (From.bPlayerInput)
		{
			const float BW = 220.0f * S;
			const float BH = 44.0f * S;
			const float BX = CentreX - BW * 0.5f;
			const float BY = PY + PH - BH - 20.0f * S;
			const bool bOver = FBox2D(FVector2D(BX, BY), FVector2D(BX + BW, BY + BH)).IsInside(Mouse);
			Panel(BX, BY, BW, BH, bOver ? FLinearColor(0.2f, 0.25f, 0.36f, 0.95f) : FLinearColor(0.12f, 0.15f, 0.22f, 0.95f), Gold, 1.5f);
			const FString Label = TEXT("Another battle  (R)");
			const FVector2D LabelSize = TextSize(Label, Font, 0.7f * S);
			Text(Label, CentreX - LabelSize.X * 0.5f, BY + (BH - LabelSize.Y) * 0.5f, TextColour, Font, 0.7f * S);
			AddButton(BX, BY, BW, BH, ETMHudAction::NewBattle, -1);
		}
	}
}
