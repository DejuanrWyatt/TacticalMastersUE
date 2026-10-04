// How the battle answers the hand and how its blows land (2026-10-03,
// Docs/design/feat-combat-feel.md): a ring and a tick where a click was taken,
// a pointer that says what a click would do, Quick Cast, orders kept while an
// online answer is on its way, fast-forward through the other side's turns,
// hit-stop and slow beats, and the camera's close-ups, zoom, edge pan and lead.
//
// None of it is a rule. The battle's clock runs on real time whatever the
// world's look is slowed or sped to (Tick), except that fast-forward -- only
// offline, only while none of this machine's units is ready -- runs it faster:
// that is time nobody here could have used.

#include "TMBattleDirector.h"
#include "TMBattleHud.h"
#include "TMSettings.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"

#include "SimAbility.h"

#include <algorithm>

namespace TMFeel
{
	/** How much faster the world and the clock run when fast-forwarding. */
	constexpr float FastFactor = 3.0f;
	/** How still the world is held for a hit-stop. */
	constexpr float StillFactor = 0.06f;

	APlayerController* PlayerOf(const AActor& Actor)
	{
		UWorld* World = Actor.GetWorld();
		return World ? World->GetFirstPlayerController() : nullptr;
	}
}

// ------------------------------------------------------------ the hand

void ATMBattleDirector::Acknowledge(const TMSim::FVec2& Point, const FLinearColor& Colour, bool bSound)
{
	FTMClickMark& Mark = ClickMarks.AddDefaulted_GetRef();
	Mark.Point = Point;
	Mark.Colour = Colour;
	Mark.Born = FPlatformTime::Seconds();
	if (ClickMarks.Num() > 8)
	{
		ClickMarks.RemoveAt(0);
	}
	if (bSound)
	{
		// sounds.json events.order: a short tick, quieter than a menu's click.
		PlayEventSound(TEXT("order"), nullptr, 0.4f);
	}
}

void ATMBattleDirector::UpdateCursor()
{
	APlayerController* Player = TMFeel::PlayerOf(*this);
	if (!Player || !bPlayerInput)
	{
		return;
	}
	EMouseCursor::Type Want = EMouseCursor::Default;
	float X = 0.0f;
	float Y = 0.0f;
	const bool bHaveCursor = CursorPosition(X, Y);
	const ATMBattleHud* Hud = Cast<ATMBattleHud>(Player->GetHUD());
	FTMHudButton Button;
	if (bRightHeld && RightDragged >= 6.0f)
	{
		Want = EMouseCursor::GrabHandClosed;
	}
	else if (bMiddleHeld)
	{
		Want = EMouseCursor::CardinalCross;
	}
	else if (bHaveCursor && Hud && Hud->ButtonAt(FVector2D(X, Y), Button) && Button.Action != ETMHudAction::None
		&& Button.Action != ETMHudAction::OverlayBlock)
	{
		Want = EMouseCursor::Hand;
	}
	else if (Screen == EScreen::Battle && !bMenuOpen && !bGuideOpen && !bEditingLayout && !bOptionsOpen && !bDevToolsOpen && bHaveHover
		&& !Battle.IsPlanning())
	{
		const TMSim::FUnit* Unit = SelectedUnit();
		const TMSim::FUnit* Hovered = HoverUnitId >= 0 ? Battle.FindUnit(HoverUnitId) : nullptr;
		if (Unit && AimMode == EAimMode::Ability && PlayerCanCommand(Unit))
		{
			// Crosshairs where it can go (or be walked into range of); a bar where it can't.
			const FAim Where = Aim();
			Want = Where.bOk || Where.Why == UTF8_TO_TCHAR(OutOfRange) ? EMouseCursor::Crosshairs : EMouseCursor::SlashedCircle;
		}
		else if (Hovered && Hovered != Unit && (PlayerCanOrder(Hovered) || PlayerCanPlan(Hovered)))
		{
			// One of yours: a click takes it up.
			Want = EMouseCursor::Hand;
		}
		else if (Unit && AimMode == EAimMode::Move && PlayerCanCommand(Unit) && !Hovered)
		{
			// Inside the walk area, the plain pointer; past it, the Go To's four ways.
			const TMSim::FNode Node = TMSim::FMap::NodeOf(HoverPoint);
			const bool bReachable = std::any_of(Reachable.begin(), Reachable.end(),
				[&Node](const std::pair<TMSim::FNode, double>& Entry) { return Entry.first == Node; });
			Want = bReachable ? EMouseCursor::Default : (Battle.Map.NodeLevel(Node) > 0 ? EMouseCursor::CardinalCross : EMouseCursor::SlashedCircle);
		}
		else if (Unit && Unit->bMoved && AimMode == EAimMode::None && !Hovered && PlayerCanOrder(Unit))
		{
			// Its walk is spent: a click on the ground would only be refused.
			Want = EMouseCursor::SlashedCircle;
		}
	}
	if (Player->CurrentMouseCursor != Want)
	{
		Player->CurrentMouseCursor = Want;
	}
}

// ------------------------------------------------------------ Quick Cast

void ATMBattleDirector::QuickRelease()
{
	const int32 Slot = QuickSlot;
	QuickSlot = -1;
	if (Slot < 0 || Screen != EScreen::Battle || bMenuOpen || bGuideOpen || bOptionsOpen || bDevToolsOpen || bEditingLayout)
	{
		return;
	}
	if (AimMode != EAimMode::Ability || AimSlot != Slot)
	{
		return;
	}
	// Online, with the last order still out: used as soon as the answer comes.
	if (BufferWhileWaiting(EKeys::LeftMouseButton, true))
	{
		return;
	}
	const TMSim::FUnit* Unit = SelectedUnit();
	if (!PlayerCanCommand(Unit))
	{
		return;
	}
	// Let go over a button: the aim is put down, nothing is used.
	float X = 0.0f;
	float Y = 0.0f;
	APlayerController* Player = TMFeel::PlayerOf(*this);
	const ATMBattleHud* Hud = Player ? Cast<ATMBattleHud>(Player->GetHUD()) : nullptr;
	FTMHudButton Button;
	if (CursorPosition(X, Y) && Hud && Hud->ButtonAt(FVector2D(X, Y), Button) && Button.Action != ETMHudAction::None)
	{
		CancelAim();
		return;
	}
	PickUnderCursor();
	// Centred on the user: let go anywhere and it goes off where the user stands
	// (planned, where its planned walk ends).
	const TMSim::FAbility* Ability = Unit->Ability(Slot);
	if (Ability && Ability->MaxRange == 0.0f)
	{
		const FPlanStandIn Stand(*this);
		bHaveHover = true;
		HoverPoint = Unit->Pos;
		HoverUnitId = Unit->Id;
	}
	if (!bHaveHover)
	{
		CancelAim();
		return;
	}
	ClickAbility(*Unit, IsPlanningSelected());
}

void ATMBattleDirector::ClickAbility(const TMSim::FUnit& Unit, bool bPlanning)
{
	const FLinearColor Violet(0.72f, 0.5f, 1.0f);
	const FLinearColor Orange(1.0f, 0.6f, 0.2f);
	const FLinearColor Refused(1.0f, 0.3f, 0.25f);
	const FAim Where = Aim();
	const bool bOutOfRange = Where.Why == UTF8_TO_TCHAR(OutOfRange);
	if (Where.bOk && bPlanning)
	{
		PlanAbility(AimSlot, Where.Point, Where.Follow);
		Acknowledge(Where.Point, Violet);
	}
	else if (Where.bOk)
	{
		const bool bTaken = OrderSelected(TMSim::FOrder::MakeUseAbility(Unit.Id, Unit.Serial, AimSlot, Where.Point, Where.Follow));
		Acknowledge(Where.Point, bTaken ? Violet : Refused, bTaken);
	}
	else if (bOutOfRange && bPlanning && PlanWalkIntoRange(Unit, Where.Point))
	{
		// Planned: the walk into range, then it.
		Acknowledge(Where.Point, Orange);
	}
	else if (bOutOfRange && !bPlanning && WalkIntoRange(Unit, Where.Point))
	{
		// Walking there first; the ability goes off on arrival (battle.gd:791).
		Acknowledge(Where.Point, Orange);
	}
	else if (bOutOfRange && SetAbilityGoTo(Unit.Id, AimSlot, Where.Point, Where.Follow))
	{
		// Beyond this turn's reach: a Go To into range, and it there (2026-10-03).
		Acknowledge(Where.Point, Orange);
	}
	else if (!Where.Why.IsEmpty())
	{
		Tell(bOutOfRange ? FString(TEXT("That target is out of range, and there is no way into range of it.")) : Where.Why);
		if (Where.bHave)
		{
			Acknowledge(Where.Point, Refused, false);
		}
	}
}

// ------------------------------------------------------------ facing on arrival

int32 ATMBattleDirector::FaceToward(const TMSim::FVec2& Direction)
{
	if (Direction.X * Direction.X + Direction.Y * Direction.Y < 1e-6f)
	{
		return -1;
	}
	// TMSim::FacingWay: 0 along +X, a step of 45 degrees on towards +Y.
	const float Degrees = FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X));
	const int32 Way = FMath::RoundToInt(Degrees / 45.0f);
	return ((Way % TMSim::FacingWays) + TMSim::FacingWays) % TMSim::FacingWays;
}

void ATMBattleDirector::UpdateWalkPress()
{
	if (!WalkPress.bActive)
	{
		return;
	}
	const TMSim::FUnit* Unit = SelectedUnit();
	// Called off when the unit can no longer be given it: its turn gone, another
	// unit taken up, the aim changed.
	if (!Unit || Unit->Id != WalkPress.UnitId || Unit->Serial != WalkPress.Serial || !PlayerCanCommand(Unit)
		|| AimMode != EAimMode::Move || Screen != EScreen::Battle || bMenuOpen)
	{
		WalkPress.bActive = false;
		return;
	}
	// Let go somewhere the release was not heard (another window): as if heard.
	const APlayerController* Player = TMFeel::PlayerOf(*this);
	if (!bRobotDriving && Player && !Player->IsInputKeyDown(EKeys::LeftMouseButton))
	{
		ReleaseWalk();
		return;
	}
	// The way: from where it ends toward the pointer, once dragged far enough
	// on the screen to mean it (a click that wobbles is still a plain click).
	float X = 0.0f;
	float Y = 0.0f;
	WalkPress.Face = -1;
	if (CursorPosition(X, Y) && FVector2D::Distance(FVector2D(X, Y), WalkPress.Mouse) >= 18.0 && bHaveHover)
	{
		WalkPress.Face = FaceToward(HoverPoint - WalkPress.To);
	}
}

void ATMBattleDirector::ReleaseWalk()
{
	const FTMWalkPress Press = WalkPress;
	WalkPress.bActive = false;
	const TMSim::FUnit* Unit = SelectedUnit();
	if (!Press.bActive || !Unit || Unit->Id != Press.UnitId || Unit->Serial != Press.Serial || !PlayerCanCommand(Unit)
		|| AimMode != EAimMode::Move)
	{
		return;
	}
	const FLinearColor WalkGold(0.95f, 0.78f, 0.35f);
	const FLinearColor RefusedRed(1.0f, 0.3f, 0.25f);
	if (Press.bPlanning)
	{
		PlanWalk(Press.To, Press.Face);
		Acknowledge(Press.To, WalkGold);
		return;
	}
	const TMSim::FVec2 Start = Unit->Pos;
	const int32 Walker = Unit->Id;
	const bool bTaken = OrderSelected(TMSim::FOrder::MakeMove(Unit->Id, Unit->Serial, Press.To, bSprinting, WayPoints, Press.Face));
	Acknowledge(Press.To, bTaken ? WalkGold : RefusedRed, bTaken);
	// Its end near the edge of the screen: the camera goes on ahead -- unless
	// that ended its turn, and the camera is off to the next.
	const TMSim::FUnit* Still = SelectedUnit();
	if (bTaken && Still && Still->Id == Walker && Still->bReady)
	{
		LeadCamera(Start, Press.To);
	}
}

// ------------------------------------------------------------ online, waiting on the host

bool ATMBattleDirector::BufferWhileWaiting(const FKey& Key, bool bClick)
{
	if (!bOnline || !bWaitingForHost || Screen != EScreen::Battle || !OnlineStopped.IsEmpty())
	{
		return false;
	}
	// The newest wins: what was pressed last is what is meant.
	BufferedKey = Key;
	bBufferedClick = bClick;
	BufferedAt = FPlatformTime::Seconds();
	float X = 0.0f;
	float Y = 0.0f;
	BufferedMouse = CursorPosition(X, Y) ? FVector2D(X, Y) : FVector2D(-1.0, -1.0);
	return true;
}

void ATMBattleDirector::ReplayBuffered()
{
	if (!BufferedKey.IsValid())
	{
		return;
	}
	const FKey Key = BufferedKey;
	const bool bClick = bBufferedClick;
	BufferedKey = FKey();
	// An answer that took longer than this: the moment has passed.
	if (FPlatformTime::Seconds() - BufferedAt > 1.5)
	{
		return;
	}
	if (bClick)
	{
		// Only where it was clicked: a pointer since moved away means something else now.
		float X = 0.0f;
		float Y = 0.0f;
		if (!CursorPosition(X, Y) || FVector2D::Distance(FVector2D(X, Y), BufferedMouse) > 40.0)
		{
			return;
		}
		OnClick();
		// A walk pressed by it, and the button already let go: walked now.
		const APlayerController* Clicker = TMFeel::PlayerOf(*this);
		if (WalkPress.bActive && Clicker && !Clicker->IsInputKeyDown(EKeys::LeftMouseButton))
		{
			ReleaseWalk();
		}
		return;
	}
	OnKey(Key);
	// A Quick Cast key let go while it waited: used now.
	const APlayerController* Player = TMFeel::PlayerOf(*this);
	if (QuickSlot >= 0 && QuickKey == Key && Player && !Player->IsInputKeyDown(Key))
	{
		QuickRelease();
	}
}

// ------------------------------------------------------------ time

bool ATMBattleDirector::WantsFastForward() const
{
	if (bOnline || !bBuilt || !bPlayerInput || Screen != EScreen::Battle || bPaused || bMenuOpen || bGuideOpen || bOptionsOpen
		|| bDevToolsOpen || bReplaying || Battle.Winner != -1 || Battle.IsPlanning() || CaptureAction >= 0)
	{
		return false;
	}
	const FTMSettings& Settings = FTMSettings::Get();
	bool bHeld = false;
	if (const APlayerController* Player = TMFeel::PlayerOf(*this))
	{
		for (const FKey& Key : Settings.Keys(ETMAction::FastForward))
		{
			bHeld = bHeld || Player->IsInputKeyDown(Key);
		}
	}
	// The option, but not while one of the player's waiting units is being planned.
	if (!bHeld && !(Settings.bFastEnemyTurns && !bPlanMode))
	{
		return false;
	}
	// Never while one of this machine's units has a turn: that time is the player's.
	for (const TMSim::FUnit& Unit : Battle.Units)
	{
		if (PlayerCanOrder(&Unit))
		{
			return false;
		}
	}
	return true;
}

void ATMBattleDirector::HitStop(float Seconds)
{
	if (Seconds <= 0.0f || FApp::IsUnattended())
	{
		return;
	}
	// The longest of what lands together, not the sum.
	HitStopUntil = FMath::Max(HitStopUntil, FPlatformTime::Seconds() + Seconds);
}

void ATMBattleDirector::SlowWorld(float Factor, float Seconds)
{
	if (FApp::IsUnattended())
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();
	if (Now >= SlowUntil || Factor < SlowFactor)
	{
		SlowFactor = Factor;
		SlowUntil = Now + Seconds;
	}
	else if (FMath::IsNearlyEqual(Factor, SlowFactor))
	{
		SlowUntil = FMath::Max(SlowUntil, Now + Seconds);
	}
}

float ATMBattleDirector::UpdateDilation()
{
	const double Now = FPlatformTime::Seconds();
	bFastForwarding = WantsFastForward();
	float Want = 1.0f;
	if (Now < HitStopUntil)
	{
		Want = TMFeel::StillFactor;
	}
	else if (SlowUntil > 0.0 && Now < SlowUntil)
	{
		Want = SlowFactor;
	}
	else
	{
		SlowUntil = 0.0;
		SlowFactor = 1.0f;
		if (bFastForwarding)
		{
			Want = TMFeel::FastFactor;
		}
	}
	if (!FMath::IsNearlyEqual(Want, AppliedDilation))
	{
		UGameplayStatics::SetGlobalTimeDilation(this, Want);
		AppliedDilation = Want;
	}
	// The clock runs fast only while the world does: not through a hit-stop or a slow beat.
	return Want == TMFeel::FastFactor ? TMFeel::FastFactor : 1.0f;
}

// ------------------------------------------------------------ the camera

void ATMBattleDirector::CloseUp(const FVector& Where, int32 Caster)
{
	// Not in the middle of an order (camera rules A) -- unless it is the
	// ultimate just ordered, by the unit being ordered.
	if (!FTMSettings::Get().bCloseUps || FApp::IsUnattended() || bRobotDriving || !bPlayerInput
		|| (MidOrder() && !(Caster >= 0 && Caster == SelectedId)))
	{
		return;
	}
	if (CloseUpUntil <= 0.0)
	{
		CloseUpReturnTarget = CamWantTarget;
		CloseUpReturnDistance = CamWantDistance;
	}
	CamWantTarget = Where;
	CamWantDistance = FMath::Min(CloseUpReturnDistance, 1500.0f);
	CloseUpUntil = FPlatformTime::Seconds() + 1.4;
}

void ATMBattleDirector::EndCloseUp()
{
	if (CloseUpUntil <= 0.0)
	{
		return;
	}
	CloseUpUntil = 0.0;
	CamWantTarget = CloseUpReturnTarget;
	CamWantDistance = CloseUpReturnDistance;
}

void ATMBattleDirector::ZoomCamera(bool bIn)
{
	// Far enough out to see the whole of a big map (80 tiles, 160 m).
	const float Before = CamWantDistance;
	CamWantDistance = FMath::Clamp(Before * (bIn ? 0.9f : 1.1f), 400.0f, 16000.0f);
	const float Kept = CamWantDistance / FMath::Max(1.0f, Before);
	APlayerController* Player = TMFeel::PlayerOf(*this);
	float X = 0.0f;
	float Y = 0.0f;
	if (!FTMSettings::Get().bZoomToCursor || FMath::IsNearlyEqual(Kept, 1.0f) || !Player || !CursorPosition(X, Y))
	{
		return;
	}
	// The ground under the pointer stays (nearly) under it: the camera's aim
	// moves toward it by as much as the distance shrank, away as it grew.
	FHitResult Hit;
	if (Player->GetHitResultAtScreenPosition(FVector2D(X, Y), ECC_Visibility, false, Hit))
	{
		FVector Shift = (Hit.ImpactPoint - CamWantTarget) * (1.0f - Kept);
		Shift.Z = 0.0f;
		CamWantTarget += Shift;
	}
}

void ATMBattleDirector::LeadCamera(const TMSim::FVec2& From, const TMSim::FVec2& To)
{
	APlayerController* Player = TMFeel::PlayerOf(*this);
	if (!Player || CloseUpUntil > 0.0 || !FTMSettings::Get().bLeadCamera)
	{
		return;
	}
	int32 W = 0;
	int32 H = 0;
	Player->GetViewportSize(W, H);
	const FVector End = GetActorTransform().TransformPosition(WorldFromMetres(To, Battle.Map.NodeLevel(TMSim::FMap::NodeOf(To))));
	FVector2D OnScreen;
	const bool bShown = Player->ProjectWorldLocationToScreen(End, OnScreen);
	const bool bInside = bShown && OnScreen.X > W * 0.12 && OnScreen.X < W * 0.88 && OnScreen.Y > H * 0.12 && OnScreen.Y < H * 0.85;
	if (bInside)
	{
		return;
	}
	// Ahead of the walk: halfway between where it set off and where it is going.
	const FVector Start = GetActorTransform().TransformPosition(WorldFromMetres(From, Battle.Map.NodeLevel(TMSim::FMap::NodeOf(From))));
	CamWantTarget = FVector((Start.X + End.X) * 0.5, (Start.Y + End.Y) * 0.5, CamWantTarget.Z);
}

// ------------------------------------------------------------ camera rules

bool ATMBattleDirector::MidOrder() const
{
	return WalkPress.bActive || AimMode == EAimMode::Ability || bPlanMode || !WayPoints.empty() || QuickSlot >= 0
		|| bRightHeld || bMiddleHeld || DragCard >= 0 || !DragPanel.IsEmpty() || !ResizePanel.IsEmpty() || DragSlider >= 0
		|| bMenuOpen || bGuideOpen || bOptionsOpen || bDevToolsOpen || bTeamItemsOpen || bEditingLayout || EdgeHeld > 0.0f;
}

bool ATMBattleDirector::OnScreenNow(const TMSim::FUnit& Unit) const
{
	const APlayerController* Player = TMFeel::PlayerOf(*this);
	if (!Player)
	{
		return false;
	}
	int32 W = 0;
	int32 H = 0;
	Player->GetViewportSize(W, H);
	FVector2D At;
	return Player->ProjectWorldLocationToScreen(GetActorTransform().TransformPosition(ShownAt(Unit) + FVector(0.0f, 0.0f, 90.0f)), At)
		&& At.X > W * 0.1 && At.X < W * 0.9 && At.Y > H * 0.1 && At.Y < H * 0.9;
}

void ATMBattleDirector::RequestFollow(int32 UnitId)
{
	if (FTMSettings::Get().CameraFollow == 2)
	{
		return;
	}
	PendingFollowId = UnitId;
	PendingFollowSince = FPlatformTime::Seconds();
}

void ATMBattleDirector::PointOut(int32 UnitId)
{
	if (UnitId == ReadyToastId)
	{
		return;
	}
	ReadyToastId = UnitId;
	ReadyToastSince = FPlatformTime::Seconds();
}

void ATMBattleDirector::UpdateFollow()
{
	const double Now = FPlatformTime::Seconds();
	CameraHeldWhy.Reset();
	// The "is ready" note: until that unit is taken up or its turn is over.
	if (ReadyToastId >= 0)
	{
		// Its own state, not whether it can be ordered this instant (a pause, a
		// menu, or an answer awaited online would lose it for good); taken up and
		// in view, it has been found.
		const TMSim::FUnit* Ready = Battle.FindUnit(ReadyToastId);
		if (!Ready || !Ready->IsAlive() || !Ready->bReady || ComputerPlaysUnit(*Ready) || Battle.Winner != -1
			|| (bOnline && UnitOwner(*Ready) != LocalPlayer) || (ReadyToastId == SelectedId && OnScreenNow(*Ready)))
		{
			ReadyToastId = -1;
		}
	}
	if (PendingFollowId < 0)
	{
		return;
	}
	const FTMSettings& Settings = FTMSettings::Get();
	const TMSim::FUnit* Unit = Battle.FindUnit(PendingFollowId);
	// Let go: Never, the unit no longer the one taken up, or asked too long ago
	// for the move still to make sense (C: never a late jump).
	if (!Unit || !Unit->IsAlive() || Settings.CameraFollow == 2 || SelectedId != PendingFollowId || Now - PendingFollowSince > 4.0)
	{
		PendingFollowId = -1;
		return;
	}
	// A: never in the middle of an order; When I'm idle, not within 1.5 s of a key or a click either.
	if (MidOrder())
	{
		CameraHeldWhy = AimMode == EAimMode::Ability ? TEXT("you're aiming") : (bPlanMode ? TEXT("you're planning")
			: (WalkPress.bActive ? TEXT("you're giving a walk") : TEXT("you're busy")));
		return;
	}
	if (Settings.CameraFollow == 1 && Now - LastInputAt < 1.5)
	{
		CameraHeldWhy = TEXT("waiting for you to finish");
		return;
	}
	// C: already in view, it stays where it is.
	if (!OnScreenNow(*Unit))
	{
		CenterCamera();
	}
	PendingFollowId = -1;
}
