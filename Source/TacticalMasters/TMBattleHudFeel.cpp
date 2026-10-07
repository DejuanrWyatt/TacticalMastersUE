// What the HUD draws for how the battle feels (2026-10-03,
// Docs/design/feat-combat-feel.md; TMBattleDirectorFeel.cpp): a ring spreading
// where each click was taken, a ghost of the unit where its walk would end,
// Quick Cast's word by the pointer, and fast-forward's badge.

#include "TMBattleHud.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"

#include "TMBattleDirector.h"
#include "TMBattleHudStyle.h"
#include "TMSettings.h"

using namespace TMHudStyle;

void ATMBattleHud::DrawFeel(ATMBattleDirector& From)
{
	if (!PlayerOwner || From.Screen != ATMBattleDirector::EScreen::Battle)
	{
		return;
	}
	UFont* Font = GEngine->GetMediumFont();
	const FTransform& Board = From.GetActorTransform();
	// A ring lying on the board, Radius metres round a point, as screen lines.
	auto GroundRing = [&](const FVector& Local, float Radius, const FLinearColor& Colour, float Width)
	{
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
			DrawLine(A.X, A.Y, B.X, B.Y, Colour, Width);
		}
	};
	auto OnBoard = [&From](const TMSim::FVec2& Point, float Lift)
	{
		return From.WorldFromMetres(Point, From.Battle.Map.NodeLevel(TMSim::FMap::NodeOf(Point))) + FVector(0.0f, 0.0f, Lift);
	};

	// Where clicks were taken: a ring that spreads and fades in under half a second.
	const double Now = FPlatformTime::Seconds();
	for (const ATMBattleDirector::FTMClickMark& Mark : From.ClickMarks)
	{
		const float T = FMath::Clamp(static_cast<float>((Now - Mark.Born) / ATMBattleDirector::ClickMarkSeconds), 0.0f, 1.0f);
		const float Ease = 1.0f - (1.0f - T) * (1.0f - T);
		const float Fade = (1.0f - T) * (1.0f - T);
		const FVector Local = OnBoard(Mark.Point, 6.0f);
		GroundRing(Local, (0.12f + 0.5f * Ease) * From.TileSize, Mark.Colour * FLinearColor(1.0f, 1.0f, 1.0f, 0.9f * Fade), 3.0f * S);
		if (T < 0.5f)
		{
			GroundRing(Local, 0.08f * From.TileSize, Mark.Colour * FLinearColor(1.0f, 1.0f, 1.0f, 1.0f - T * 2.0f), 2.0f * S);
		}
	}

	// The ghost: where the walk being aimed would end, the unit's outline
	// standing there in its side's colour, and an arrow the way it would face.
	const TMSim::FUnit* Unit = From.SelectedUnit();
	if (Unit && From.AimMode == ATMBattleDirector::EAimMode::Move && From.PathShown.size() >= 2 && !From.bMenuOpen)
	{
		const TMSim::FVec2 End = From.PathShown.back();
		const TMSim::FVec2 Before = From.PathShown[From.PathShown.size() - 2];
		const FLinearColor Ghost = TeamColour(Unit->Team) * FLinearColor(1.0f, 1.0f, 1.0f, 0.55f);
		const float Radius = 0.32f * From.TileSize;
		const FVector Feet = OnBoard(End, 4.0f);
		// Only a circle at its feet and the arrows (v21 play test: no body outline over it).
		GroundRing(Feet, Radius, Ghost, 2.5f * S);
		// The way it would face: the last step of the walk, or -- pressed and
		// dragged (2026-10-03, facing on arrival) -- the way chosen, in gold and
		// bigger, with the eight it can choose from marked round it.
		const bool bPressed = From.WalkPress.bActive;
		const bool bChosen = bPressed && From.WalkPress.Face >= 0;
		TMSim::FVec2 Step = End - Before;
		if (bChosen)
		{
			Step = TMSim::FacingWay(From.WalkPress.Face);
		}
		const float Length = FMath::Sqrt(Step.X * Step.X + Step.Y * Step.Y);
		auto OnScreen = [&](const FVector& Local, FVector2D& Out)
		{
			return PlayerOwner->ProjectWorldLocationToScreen(Board.TransformPosition(Local), Out);
		};
		if (bPressed)
		{
			for (int32 Way = 0; Way < TMSim::FacingWays; ++Way)
			{
				const TMSim::FVec2 Each = TMSim::FacingWay(Way);
				const FVector Out(Each.X, Each.Y, 0.0f);
				FVector2D In2;
				FVector2D Out2;
				if (OnScreen(Feet + Out * Radius * 1.25f, In2) && OnScreen(Feet + Out * Radius * 1.5f, Out2))
				{
					DrawLine(In2.X, In2.Y, Out2.X, Out2.Y, Ghost * FLinearColor(1.0f, 1.0f, 1.0f, 0.8f), 2.0f * S);
				}
			}
		}
		if (Length > 0.01f)
		{
			const FLinearColor Ink = bChosen ? Gold : Ghost;
			const float Reach = bChosen ? 2.4f : 1.9f;
			const FVector Dir(Step.X / Length, Step.Y / Length, 0.0f);
			const FVector Side(-Dir.Y, Dir.X, 0.0f);
			const FVector Tip = Feet + Dir * Radius * Reach;
			const FVector Base = Feet + Dir * Radius * (Reach - 0.7f);
			FVector2D T2;
			FVector2D L2;
			FVector2D R2;
			FVector2D F2;
			if (OnScreen(Tip, T2) && OnScreen(Base + Side * Radius * 0.45f, L2) && OnScreen(Base - Side * Radius * 0.45f, R2))
			{
				DrawLine(L2.X, L2.Y, T2.X, T2.Y, Ink, (bChosen ? 4.0f : 2.5f) * S);
				DrawLine(R2.X, R2.Y, T2.X, T2.Y, Ink, (bChosen ? 4.0f : 2.5f) * S);
				if (bChosen && OnScreen(Feet, F2))
				{
					DrawLine(F2.X, F2.Y, T2.X, T2.Y, Ink * FLinearColor(1.0f, 1.0f, 1.0f, 0.7f), 2.0f * S);
				}
			}
		}
		// Pressed, not dragged yet: a word on what dragging does.
		FVector2D Label;
		if (bPressed && !bChosen && OnScreen(Feet + FVector(0.0f, 0.0f, 205.0f), Label))
		{
			const FString Words = TEXT("drag to face");
			const FVector2D Size = TextSize(Words, Font, 0.42f * S);
			OutlinedText(Words, Label.X - Size.X * 0.5f, Label.Y - Size.Y, Gold, Font, 0.42f * S, 1.5f * S);
		}
	}

	// Quick Cast held: by the pointer, what letting go will do.
	float MX = 0.0f;
	float MY = 0.0f;
	if (From.QuickSlot >= 0 && From.AimMode == ATMBattleDirector::EAimMode::Ability && PlayerOwner->GetMousePosition(MX, MY))
	{
		OutlinedText(TEXT("let go to use"), MX + 22.0f * S, MY + 18.0f * S, Gold, Font, 0.42f * S, 1.5f * S);
	}

	// Camera rules B (2026-10-03): one of yours became ready while you were
	// busy with another. A note at the top -- who, and how long its turn has --
	// with Go, and, when it is off screen, a tab at that edge pointing to it.
	const TMSim::FUnit* Ready = From.ReadyToastId >= 0 ? From.Battle.FindUnit(From.ReadyToastId) : nullptr;
	if (Ready && !From.bMenuOpen && !From.bGuideOpen)
	{
		const FLinearColor Side = TeamColour(Ready->Team);
		const int32 Seconds = FMath::CeilToInt(Ready->Clock / static_cast<float>(TMSim::Pace::TicksPerSecond));
		const FString Words = FString::Printf(TEXT("%s is ready  ·  %d s"), *From.LogName(Ready->Id), Seconds);
		const FString Go = FString::Printf(TEXT("Go (%s)"), *FTMSettings::Get().KeyName(ETMAction::NextUnit));
		const FVector2D WordsSize = TextSize(Words, Font, 0.55f * S);
		const float WordsW = static_cast<float>(WordsSize.X);
		const float WordsH = static_cast<float>(WordsSize.Y);
		const float GoW = 92.0f * S;
		const float BoxW = WordsW + GoW + 52.0f * S;
		const float BoxH = 46.0f * S;
		const float BX = (Canvas->ClipX - BoxW) * 0.5f;
		const float BY = 140.0f * S;
		// It slides in, and beats while its clock is short.
		const float Age = static_cast<float>(FPlatformTime::Seconds() - From.ReadyToastSince);
		const float In = FMath::Clamp(Age / 0.2f, 0.0f, 1.0f);
		const float Urge = Seconds <= 5 ? 0.6f + 0.4f * FMath::Abs(FMath::Sin(static_cast<float>(GetWorld()->GetRealTimeSeconds()) * 5.0f)) : 1.0f;
		const float Top = BY - (1.0f - In) * 20.0f * S;
		DrawRect(FLinearColor(0.02f, 0.03f, 0.05f, 0.9f * In), BX, Top, BoxW, BoxH);
		DrawRect(Side * FLinearColor(1.0f, 1.0f, 1.0f, Urge * In), BX, Top, 5.0f * S, BoxH);
		Text(Words, BX + 18.0f * S, Top + (BoxH - WordsH) * 0.5f, TextColour * FLinearColor(1.0f, 1.0f, 1.0f, In), Font, 0.55f * S);
		MenuButton(BX + BoxW - GoW - 10.0f * S, Top + 7.0f * S, GoW, BoxH - 14.0f * S, Go, ETMHudAction::ReadyGo, Ready->Id, true);
		// Off screen: a tab at the edge it is past, in its side's colour, pointing at it.
		int32 ViewW = 0;
		int32 ViewH = 0;
		PlayerOwner->GetViewportSize(ViewW, ViewH);
		FVector2D At(ForceInit);
		const FVector Where = Board.TransformPosition(From.ShownAt(*Ready) + FVector(0.0f, 0.0f, 90.0f));
		const bool bProjected = PlayerOwner->ProjectWorldLocationToScreen(Where, At);
		const FVector2D Middle(ViewW * 0.5f, ViewH * 0.5f);
		if (ViewW > 0 && ViewH > 0 && (!bProjected || !From.OnScreenNow(*Ready)))
		{
			// Not on the screen at all (behind the camera): the way to it as the
			// camera sees it, across and up.
			FVector2D Way = At - Middle;
			if (!bProjected && PlayerOwner->PlayerCameraManager)
			{
				const FRotator Looking = PlayerOwner->PlayerCameraManager->GetCameraRotation();
				const FVector Offset = Where - PlayerOwner->PlayerCameraManager->GetCameraLocation();
				const FRotationMatrix Axes(Looking);
				Way = FVector2D(FVector::DotProduct(Offset, Axes.GetScaledAxis(EAxis::Y)), -FVector::DotProduct(Offset, Axes.GetScaledAxis(EAxis::Z)));
			}
			if (Way.IsNearlyZero())
			{
				Way = FVector2D(1.0, 0.0);
			}
			Way.Normalize();
			const float Margin = 40.0f * S;
			const float Reach = FMath::Min((ViewW * 0.5f - Margin) / FMath::Max(0.001f, static_cast<float>(FMath::Abs(Way.X))),
				(ViewH * 0.5f - Margin) / FMath::Max(0.001f, static_cast<float>(FMath::Abs(Way.Y))));
			const FVector2f Dir(static_cast<float>(Way.X), static_cast<float>(Way.Y));
			const FVector2f Tip = FVector2f(static_cast<float>(Middle.X), static_cast<float>(Middle.Y)) + Dir * Reach;
			const FVector2f Across(-Dir.Y, Dir.X);
			const FVector2f Back = Tip - Dir * (22.0f * S);
			const FLinearColor Arrow = Side * FLinearColor(1.0f, 1.0f, 1.0f, Urge);
			DrawLine(Back.X + Across.X * 14.0f * S, Back.Y + Across.Y * 14.0f * S, Tip.X, Tip.Y, Arrow, 5.0f * S);
			DrawLine(Back.X - Across.X * 14.0f * S, Back.Y - Across.Y * 14.0f * S, Tip.X, Tip.Y, Arrow, 5.0f * S);
			const FString Name = From.LogName(Ready->Id);
			const FVector2D NameSize = TextSize(Name, Font, 0.45f * S);
			const float NameW = static_cast<float>(NameSize.X);
			const float NameH = static_cast<float>(NameSize.Y);
			const FVector2f NameAt = Back - Dir * (NameW * 0.5f + 16.0f * S);
			OutlinedText(Name, NameAt.X - NameW * 0.5f, NameAt.Y - NameH * 0.5f, Arrow, Font, 0.45f * S, 1.5f * S);
		}
	}

	// Camera rules A: the camera wanted to move and is waiting for you; a small note says why it didn't.
	if (!From.CameraHeldWhy.IsEmpty() && FTMSettings::Get().bCameraHeldNote)
	{
		const FString Words = FString::Printf(TEXT("Camera held: %s"), *From.CameraHeldWhy);
		const FVector2D Size = TextSize(Words, Font, 0.45f * S);
		const float NX = 24.0f * S;
		const float NY = Canvas->ClipY * 0.5f - 200.0f * S;
		DrawRect(FLinearColor(0.02f, 0.03f, 0.05f, 0.8f), NX - 10.0f * S, NY - 5.0f * S, static_cast<float>(Size.X) + 20.0f * S, static_cast<float>(Size.Y) + 10.0f * S);
		Text(Words, NX, NY, Gold, Font, 0.45f * S);
	}

	// Fast-forward: a badge at the top of the screen while it runs.
	if (From.bFastForwarding)
	{
		const FString Words = FString::Printf(TEXT(">>  FAST-FORWARD  (%s)"), FTMSettings::Get().bFastEnemyTurns
			? TEXT("Options") : *FTMSettings::Get().KeyName(ETMAction::FastForward));
		const FVector2D Size = TextSize(Words, Font, 0.55f * S);
		const float BX = (Canvas->ClipX - Size.X) * 0.5f;
		const float BY = 96.0f * S;
		DrawRect(FLinearColor(0.02f, 0.03f, 0.05f, 0.75f), BX - 12.0f * S, BY - 4.0f * S, Size.X + 24.0f * S, Size.Y + 8.0f * S);
		const float Beat = 0.75f + 0.25f * FMath::Sin(GetWorld()->GetRealTimeSeconds() * 4.0f);
		Text(Words, BX, BY, Gold * FLinearColor(1.0f, 1.0f, 1.0f, Beat), Font, 0.55f * S);
	}
}
