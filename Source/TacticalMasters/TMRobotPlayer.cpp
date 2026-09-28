#include "TMRobotPlayer.h"

#include "TMBattleDirector.h"
#include "TMBattleHud.h"
#include "TMSettings.h"

#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

#include "SimAI.h"
#include "SimAbility.h"
#include "SimMap.h"

namespace
{
	/** How long a whole robot session may run before it is called off, in seconds. */
	constexpr float TotalLimit = 25.0f * 60.0f;
	/** A battle that makes no progress for this long is stuck. */
	constexpr float StuckAfter = 90.0f;
	/** Pictures stop after this many problems; the report still lists them all. */
	constexpr int32 MaxPictures = 30;
}

ATMRobotPlayer::ATMRobotPlayer()
{
	PrimaryActorTick.bCanEverTick = true;
	// After the director, so the board it looks at is this frame's.
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
}

void ATMRobotPlayer::BeginPlay()
{
	Super::BeginPlay();
	for (TActorIterator<ATMBattleDirector> It(GetWorld()); It; ++It)
	{
		Director = *It;
		break;
	}
	OutDir = FPaths::ProjectSavedDir() / TEXT("Robot");
	IFileManager::Get().DeleteDirectory(*OutDir, false, true);
	IFileManager::Get().MakeDirectory(*OutDir, true);
	Note(TEXT("robot playtester starting at the title screen"));
}

ATMBattleHud* ATMRobotPlayer::Hud() const
{
	const APlayerController* Player = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	return Player ? Cast<ATMBattleHud>(Player->GetHUD()) : nullptr;
}

// ------------------------------------------------------------------ building

void ATMRobotPlayer::Button(ETMHudAction Action, int32 Value)
{
	FStep Step;
	Step.Kind = FStep::EKind::ClickButton;
	Step.Action = Action;
	Step.Value = Value;
	Push(MoveTemp(Step));
}

void ATMRobotPlayer::Check(TFunction<bool()> Test, const FString& Meaning, float Seconds)
{
	FStep Step;
	Step.Kind = FStep::EKind::Check;
	Step.Test = MoveTemp(Test);
	Step.Meaning = Meaning;
	Step.Seconds = Seconds;
	Push(MoveTemp(Step));
}

void ATMRobotPlayer::Pause(float Seconds)
{
	FStep Step;
	Step.Kind = FStep::EKind::Wait;
	Step.Seconds = Seconds;
	Push(MoveTemp(Step));
}

// ------------------------------------------------------------------ the frame

void ATMRobotPlayer::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Director || Session == ESession::Done)
	{
		return;
	}
	TotalClock += DeltaSeconds;
	SessionClock += DeltaSeconds;
	if (TotalClock > TotalLimit)
	{
		Problem(TEXT("the robot's whole session ran out of time; it stopped where it was"));
		Finish();
		return;
	}

	// A click in flight lands once the pointer has been in place for a couple
	// of frames, so the HUD has drawn with it there -- as it would have for a
	// person, whose pointer arrives before their finger goes down.
	if (ClickIn > 0)
	{
		--ClickIn;
		return;
	}
	if (ClickIn == 0)
	{
		ClickIn = -1;
		Director->OnKey(EKeys::LeftMouseButton);
		StepClock = -Between;
		return;
	}
	if (StepClock < 0.0f)
	{
		StepClock += DeltaSeconds;
		return;
	}

	if (bTurnPlan && Director->Battle.Winner != -1)
	{
		// The battle ended part way through a plan: nothing left to do with it.
		Queue.Empty();
		bTurnPlan = false;
	}
	// A check waiting to see its step land is left to finish: ending a turn
	// is exactly what an End Turn plan is for.
	if (bTurnPlan && !(Queue.Num() > 0 && Queue[0].Kind == FStep::EKind::Check))
	{
		// Its turn ran out, or it was stunned or knocked down, part way through:
		// as for a person, the rest of the plan is moot. Not a problem.
		const TMSim::FUnit* Planned = Director->Battle.FindUnit(CurrentUnit);
		if (!Planned || !Planned->IsAlive() || !Planned->bReady || Planned->IsStunned() || Planned->Serial != CurrentSerial)
		{
			if (Queue.Num() > 0)
			{
				Tally.FindOrAdd(TEXT("cut short")).Y += 1;
			}
			Queue.Empty();
			bTurnPlan = false;
		}
	}
	if (Queue.Num() > 0)
	{
		if (RunStep(Queue[0], DeltaSeconds))
		{
			Queue.RemoveAt(0);
			StepClock = 0.0f;
		}
		return;
	}
	bTurnPlan = false;
	Decide();
}

// ------------------------------------------------------------------ deciding

void ATMRobotPlayer::Decide()
{
	using EScreen = ATMBattleDirector::EScreen;
	if (!bSessionStarted)
	{
		StartSession();
		return;
	}
	if (Director->Screen != EScreen::Battle)
	{
		IdleFor += GetWorld()->GetDeltaSeconds();
		if (IdleFor > 10.0f)
		{
			Problem(TEXT("away from the battle with nothing to do next"));
			IdleFor = 0.0f;
			bSessionStarted = false;
		}
		return;
	}
	TMSim::FBattle& Battle = Director->Battle;

	// The end of a battle: read the result, then on to the next session.
	if (Battle.Winner != -1)
	{
		++BattlesSeen;
		Note(FString::Printf(TEXT("battle %d over: %s after %.0fs, %d turns played through the controls"),
			BattlesSeen, Battle.Winner == TMSim::FBattle::Draw ? TEXT("a draw") : (Battle.Winner == 0 ? TEXT("blue won") : TEXT("red won")),
			Battle.TickCount / 10.0f, Turns));
		Turns = 0;
		Pause(1.5f);
		// Main menu: the next session starts from the title, as a person's would.
		Button(ETMHudAction::MenuTitle);
		Check([this]() { return Director->Screen == ATMBattleDirector::EScreen::Title; },
			TEXT("the result's Main menu button should go back to the title"));
		Session = static_cast<ESession>(static_cast<uint8>(Session) + 1);
		bSessionStarted = false;
		bLooked = false;
		bArranged = false;
		bPlaced = false;
		PlannedSerial.Reset();
		Attempts.Reset();
		if (Session == ESession::Done)
		{
			Finish();
		}
		return;
	}

	// Planning: put this side's units down, then say it is ready.
	if (Battle.IsPlanning())
	{
		const int32 Team = Director->PlanningTeam();
		if (Team != -1 && !Battle.PlanningDone[Team] && !bPlaced)
		{
			bPlaced = true;
			PlanPlacing(Team);
		}
		return;
	}

	// Once a battle, look around the way a person would: the panels, the guide,
	// the menu, pause.
	if (!bLooked && Turns >= 2)
	{
		bLooked = true;
		PlanLooking();
		return;
	}
	// Once a session, when a unit of ours is up (so its card is on screen).
	if (!bArranged && Turns >= 3 && Director->SelectedUnit())
	{
		bArranged = true;
		PlanLayout();
		return;
	}

	// A unit of ours waiting on an order.
	for (const TMSim::FUnit& Unit : Battle.Units)
	{
		if (Unit.IsAlive() && Unit.bReady && !Unit.IsStunned() && !Director->ComputerPlays(Unit.Team))
		{
			const int32* Seen = PlannedSerial.Find(Unit.Id);
			int32& Tries = Attempts.FindOrAdd(Unit.Id);
			if (!Seen || *Seen != Unit.Serial)
			{
				Tries = 0;
			}
			if (Tries >= 4)
			{
				continue;  // left to its clock; the problems are already written down
			}
			++Tries;
			IdleFor = 0.0f;
			PlanTurn(Unit);
			return;
		}
	}

	// Nobody of ours is up: the computer is thinking, or time is passing.
	IdleFor += GetWorld()->GetDeltaSeconds();
	if (IdleFor > StuckAfter)
	{
		Problem(TEXT("nothing happened for 90 seconds in a battle that is not over"));
		IdleFor = 0.0f;
	}
}

void ATMRobotPlayer::StartSession()
{
	using EScreen = ATMBattleDirector::EScreen;
	bSessionStarted = true;
	SessionClock = 0.0f;
	IdleFor = 0.0f;
	if (Director->Screen != EScreen::Title)
	{
		Problem(TEXT("a session should start from the title screen"));
	}
	switch (Session)
	{
	case ESession::VsComputerBlue:
	{
		Note(TEXT("session 1: against the computer, as blue, choosing a class in the picker"));
		Button(ETMHudAction::TitleVsComputer);
		Check([this]() { return Director->Screen == ATMBattleDirector::EScreen::Setup; },
			TEXT("Vs Computer on the title should open the battle setup"));
		// Blue's first slot: open the picker and choose the Cantor, an aura class.
		Button(ETMHudAction::SetupClass, 0);
		Check([this]() { return Director->PickerSlot == 0; }, TEXT("a class slot should open the class picker"));
		int32 Cantor = -1;
		const std::vector<const TMSim::FJobDef*>& Jobs = TMSim::AllJobs();
		for (size_t i = 0; i < Jobs.size(); ++i)
		{
			if (Jobs[i]->Id == "cantor")
			{
				Cantor = static_cast<int32>(i);
			}
		}
		Button(ETMHudAction::PickerChoose, Cantor);
		Check([this]() { return Director->PickerSlot == -1 && Director->Setup.Rosters[0][0] == "cantor"; },
			TEXT("choosing a class in the picker should put it in the slot and close the picker"));
		Button(ETMHudAction::SetupStart);
		Check([this]() { return Director->Screen == ATMBattleDirector::EScreen::Battle; },
			TEXT("Start Battle should start the battle"));
		break;
	}
	case ESession::VsComputerRed:
		Note(TEXT("session 2: against the computer, as red, with planning time and holding the middle"));
		Button(ETMHudAction::TitleVsComputer);
		Check([this]() { return Director->Screen == ATMBattleDirector::EScreen::Setup; },
			TEXT("Vs Computer on the title should open the battle setup"));
		Button(ETMHudAction::SetupSide);
		Check([this]() { return Director->Setup.PlayerTeam == 1; }, TEXT("You play: should switch to red"));
		Button(ETMHudAction::SetupPlanning);
		Check([this]() { return Director->Setup.PlanningSeconds == 30.0; }, TEXT("Planning should step to 30 seconds"));
		Button(ETMHudAction::SetupVictory);
		Check([this]() { return Director->Setup.CaptureSeconds == 30.0; }, TEXT("Victory should step to holding the middle for 30s"));
		Button(ETMHudAction::SetupStart);
		Check([this]() { return Director->Screen == ATMBattleDirector::EScreen::Battle; },
			TEXT("Start Battle should start the battle"));
		break;
	case ESession::TwoPlayers:
		Note(TEXT("session 3: two players at one machine, the robot playing both"));
		Button(ETMHudAction::TitleTwoPlayers);
		Check([this]() { return Director->Screen == ATMBattleDirector::EScreen::Setup; },
			TEXT("Two Players on the title should open the battle setup"));
		// Back to no planning and last team standing: the setup keeps what was
		// chosen last time, which is itself worth knowing works.
		Check([this]() { return Director->Setup.PlanningSeconds == 30.0; },
			TEXT("the setup should remember the last battle's planning time"), 0.5f);
		Button(ETMHudAction::SetupPlanning);
		Button(ETMHudAction::SetupPlanning);
		Button(ETMHudAction::SetupPlanning);
		Check([this]() { return Director->Setup.PlanningSeconds == 0.0; }, TEXT("Planning should step round to none"));
		Button(ETMHudAction::SetupStart);
		Check([this]() { return Director->Screen == ATMBattleDirector::EScreen::Battle; },
			TEXT("Start Battle should start the battle"));
		break;
	default:
		break;
	}
}

void ATMRobotPlayer::PlanPlacing(int32 Team)
{
	// Each unit: pick it up, then put it on a spot the game marks for it -- a
	// different one each, so they do not all try the same.
	TMSim::FBattle& Battle = Director->Battle;
	int32 Nth = 0;
	for (const TMSim::FUnit& Unit : Battle.Units)
	{
		if (Unit.Team != Team || !Unit.IsAlive())
		{
			continue;
		}
		const int32 Id = Unit.Id;
		CurrentUnit = Id;
		CurrentIntent = TEXT("place it");
		FStep Pick;
		Pick.Kind = FStep::EKind::ClickUnit;
		Pick.UnitId = Id;
		Push(MoveTemp(Pick));
		Check([this, Id]() { return Director->PlaceId == Id; }, TEXT("while planning, clicking one of my units should pick it up"));
		FStep Put;
		Put.Kind = FStep::EKind::ClickPlaceable;
		Put.UnitId = Id;
		Put.Value = Nth++;
		Push(MoveTemp(Put));
		Check([this, Id]() { const TMSim::FUnit* U = Director->Battle.FindUnit(Id); return U && U->Pos == LastPlaced; },
			FString::Printf(TEXT("clicking a marked spot should put unit %d there"), Id));
	}
	Button(ETMHudAction::PlanningReady);
	Check([this, Team]()
		{
			const bool bDone = Director->Battle.PlanningDone[Team] || !Director->Battle.IsPlanning();
			if (bDone)
			{
				Tally.FindOrAdd(TEXT("planning")).X += 1;
			}
			return bDone;
		}, TEXT("the Ready button should say this side is done placing"));
	Tally.FindOrAdd(TEXT("planning")).Y += 1;
}

void ATMRobotPlayer::PlanLooking()
{
	using EScreen = ATMBattleDirector::EScreen;
	Note(TEXT("looking around: log, field, guide, menu, pause"));
	Button(ETMHudAction::ToggleLog);
	Check([this]() { return !Director->bShowLog; }, TEXT("the Log button should hide the log"));
	Button(ETMHudAction::ToggleLog);
	Check([this]() { return Director->bShowLog; }, TEXT("the Log button should show the log again"));
	Button(ETMHudAction::ToggleField);
	Check([this]() { return Director->bShowField; }, TEXT("the Field button should open the field panel"));
	Button(ETMHudAction::ToggleField);
	Check([this]() { return !Director->bShowField; }, TEXT("the Field button should close the field panel"));
	Button(ETMHudAction::ToggleGuide);
	Check([this]() { return Director->bGuideOpen; }, TEXT("the Units button should open the Unit Guide"));
	FStep Esc;
	Esc.Kind = FStep::EKind::Key;
	Esc.Key = EKeys::Escape;
	Push(MoveTemp(Esc));
	Check([this]() { return !Director->bGuideOpen; }, TEXT("Esc should close the Unit Guide"));
	Button(ETMHudAction::OpenMenu);
	Check([this]() { return Director->bMenuOpen; }, TEXT("the Menu button should open the battle menu"));
	Button(ETMHudAction::MenuResume);
	Check([this]() { return !Director->bMenuOpen; }, TEXT("Resume should close the battle menu"));
	Button(ETMHudAction::Pause);
	Check([this]() { return Director->bPaused; }, TEXT("the Pause button should pause"));
	Button(ETMHudAction::Pause);
	Check([this]() { return !Director->bPaused; }, TEXT("the Pause button again should carry on"));
}

void ATMRobotPlayer::PlanLayout()
{
	Note(TEXT("arranging the screen: Edit layout, move the unit card, reorder a turn square, reset, lock"));
	Button(ETMHudAction::ToggleLayout);
	Check([this]() { return Director->bEditingLayout; }, TEXT("the Layout button should start Edit layout"));

	// The unit card, 120 pixels right and 80 up.
	FStep Grab;
	Grab.Kind = FStep::EKind::GrabPanel;
	Grab.Meaning = TEXT("unit_card");
	Push(MoveTemp(Grab));
	FStep Slide;
	Slide.Kind = FStep::EKind::Slide;
	Slide.Point = TMSim::FVec2(120.0f, -80.0f);
	Push(MoveTemp(Slide));
	FStep Let;
	Let.Kind = FStep::EKind::Release;
	Push(MoveTemp(Let));
	Check([this]()
		{
			const FVector2D* Moved = FTMSettings::Get().Layout.Find(TEXT("unit_card"));
			return Moved && Moved->X > 40.0 && Moved->Y < -25.0;
		}, TEXT("dragging the unit card's handle should move it and remember where"));

	// The first blue square, dropped past the last.
	FStep Card;
	Card.Kind = FStep::EKind::GrabSquare;
	Card.Value = 0;
	Push(MoveTemp(Card));
	FStep Along;
	Along.Kind = FStep::EKind::Slide;
	Along.Point = TMSim::FVec2(400.0f, 0.0f);
	Push(MoveTemp(Along));
	FStep Drop;
	Drop.Kind = FStep::EKind::Release;
	Push(MoveTemp(Drop));
	Check([this]()
		{
			const TArray<int32>& Order = FTMSettings::Get().CardOrder[0];
			return Order.Num() > 1 && Order.Last() == LastGrabbedSquare;
		}, TEXT("dropping a turn square past the others should put it last in its row"));

	if (Session == ESession::VsComputerBlue)
	{
		FStep Shot;
		Shot.Kind = FStep::EKind::Picture;
		Shot.Meaning = TEXT("layout_editing.png");
		Push(MoveTemp(Shot));
	}
	Button(ETMHudAction::LayoutReset);
	Check([]() { return FTMSettings::Get().Layout.Num() == 0 && FTMSettings::Get().CardOrder[0].Num() == 0; },
		TEXT("Reset layout should put everything back"));
	Button(ETMHudAction::ToggleLayout);
	Check([this]() { return !Director->bEditingLayout; }, TEXT("Lock should end Edit layout"));
	Tally.FindOrAdd(TEXT("layout")).Y += 1;
	Check([this]() { Tally.FindOrAdd(TEXT("layout")).X += 1; return true; }, TEXT(""));
}

void ATMRobotPlayer::PlanTurn(const TMSim::FUnit& Unit)
{
	// What a sensible person would do with this unit now: the computer player's
	// own choice, worked out on a copy so nothing about the real battle moves.
	TMSim::FBattle Copy = Director->Battle;
	TMSim::FAIPlayer Brain("hard");
	const TMSim::FUnit* Mine = Copy.FindUnit(Unit.Id);
	if (!Mine)
	{
		return;
	}
	const TMSim::FOrder Intent = Brain.NextCommand(Copy, *Mine);
	bTurnPlan = true;
	CurrentSerial = Unit.Serial;
	const int32 Id = Unit.Id;
	CurrentUnit = Id;
	CurrentIntent = Intent.Type == TMSim::EOrderType::Move
		? FString::Printf(TEXT("%s to %.2f,%.2f"), Intent.bSprint ? TEXT("sprint") : TEXT("walk"), Intent.To.X, Intent.To.Y)
		: Intent.Type == TMSim::EOrderType::UseAbility
			? FString::Printf(TEXT("ability %d at %.2f,%.2f (follow %d)"), Intent.Slot + 1, Intent.Target.X, Intent.Target.Y, Intent.Follow)
			: FString(TEXT("end turn"));
	const int32 Serial = Unit.Serial;
	PlannedSerial.Add(Id, Serial);
	const int32 OrdersBefore = Director->OrdersApplied;

	// Pick it up first, unless it already is.
	if (Director->SelectedId != Id)
	{
		FStep Pick;
		Pick.Kind = FStep::EKind::ClickUnit;
		Pick.UnitId = Id;
		Push(MoveTemp(Pick));
		Check([this, Id]() { return Director->SelectedId == Id; }, TEXT("clicking a ready unit of mine should pick it up"));
	}

	switch (Intent.Type)
	{
	case TMSim::EOrderType::Move:
	{
		const FString Kind = Intent.bSprint ? TEXT("sprint") : TEXT("walk");
		Tally.FindOrAdd(Kind).Y += 1;
		FStep Mode;
		Mode.Kind = FStep::EKind::EnsureMove;
		Mode.bFlag = Intent.bSprint;
		Push(MoveTemp(Mode));
		FStep Go;
		Go.Kind = FStep::EKind::ClickGround;
		Go.Point = Intent.To;
		Go.bFlag = true;  // a walk: a hidden spot may be swapped for a visible one
		Push(MoveTemp(Go));
		Check([this, Id, Kind]()
			{
				const TMSim::FUnit* U = Director->Battle.FindUnit(Id);
				const bool bDone = U && U->Pos == WalkTarget;
				if (bDone)
				{
					Tally.FindOrAdd(Kind).X += 1;
				}
				return bDone;
			}, FString::Printf(TEXT("clicking a spot it can reach should %s unit %d there"), *Kind, Id));
		break;
	}
	case TMSim::EOrderType::UseAbility:
	{
		const TMSim::FAbility* Ability = TMSim::JobAbility(Unit.Job, Intent.Slot);
		const FString Name = Ability ? FString(UTF8_TO_TCHAR(Ability->Name.c_str())) : FString(TEXT("?"));
		Tally.FindOrAdd(TEXT("ability")).Y += 1;
		CurrentSlot = Intent.Slot;
		FStep Mode;
		Mode.Kind = FStep::EKind::EnsureAbility;
		Mode.Value = Intent.Slot;
		// Every other time with its number key, as people do both.
		Mode.bFlag = (Turns % 2) == 1;
		Push(MoveTemp(Mode));
		FStep At;
		At.Kind = FStep::EKind::ClickTarget;
		At.Point = Intent.Target;
		At.UnitId = Intent.Follow;
		Push(MoveTemp(At));
		Check([this, Id, Serial, OrdersBefore]()
			{
				const TMSim::FUnit* U = Director->Battle.FindUnit(Id);
				const bool bDone = !U || !U->IsAlive() || U->bActed || U->IsCasting() || U->Serial != Serial
					|| Director->OrdersApplied > OrdersBefore + 0;
				if (bDone)
				{
					Tally.FindOrAdd(TEXT("ability")).X += 1;
				}
				return bDone;
			}, FString::Printf(TEXT("aiming %s at its target and clicking should use it"), *Name), 3.0f);
		break;
	}
	default:
	{
		Tally.FindOrAdd(TEXT("end turn")).Y += 1;
		if (Turns % 3 == 2)
		{
			FStep Enter;
			Enter.Kind = FStep::EKind::Key;
			Enter.Key = EKeys::Enter;
			Push(MoveTemp(Enter));
		}
		else
		{
			Button(ETMHudAction::EndTurn);
		}
		Check([this, Id, Serial]()
			{
				const TMSim::FUnit* U = Director->Battle.FindUnit(Id);
				const bool bDone = !U || !U->bReady || U->Serial != Serial;
				if (bDone)
				{
					Tally.FindOrAdd(TEXT("end turn")).X += 1;
				}
				return bDone;
			}, TEXT("End Turn should end the turn"));
		break;
	}
	}
	++Turns;
}

// ------------------------------------------------------------------ doing

bool ATMRobotPlayer::ScreenOfUnit(int32 UnitId, FVector2D& Out) const
{
	const TMSim::FUnit* Unit = Director->Battle.FindUnit(UnitId);
	APlayerController* Player = GetWorld()->GetFirstPlayerController();
	if (!Unit || !Player)
	{
		return false;
	}
	// Where it is drawn: its chest, as the director picks units by (PickUnderCursor).
	const float Up = Unit->IsAlive() ? 90.0f : 20.0f;
	return Player->ProjectWorldLocationToScreen(
		Director->GetActorTransform().TransformPosition(Director->ShownAt(*Unit) + FVector(0.0f, 0.0f, Up)), Out);
}

bool ATMRobotPlayer::ScreenOfGround(const TMSim::FVec2& Point, FVector2D& Out) const
{
	APlayerController* Player = GetWorld()->GetFirstPlayerController();
	return Player && Player->ProjectWorldLocationToScreen(Director->BoardPoint(Point, 4.0f), Out);
}

bool ATMRobotPlayer::Visible(const TMSim::FVec2& Point, const FVector2D& Screen) const
{
	// The same test the director makes of the pointer: what the camera's ray
	// meets there, in the rules' metres.
	APlayerController* Player = GetWorld()->GetFirstPlayerController();
	FHitResult Hit;
	if (!Player || !Player->GetHitResultAtScreenPosition(Screen, ECC_Visibility, false, Hit))
	{
		return false;
	}
	const FVector Local = Director->GetActorTransform().InverseTransformPosition(Hit.ImpactPoint);
	const TMSim::FVec2 Seen(static_cast<float>(Local.X / Director->TileSize), static_cast<float>(Local.Y / Director->TileSize));
	FTMHudButton Over;
	const ATMBattleHud* Screen2D = Hud();
	const bool bUnderButton = Screen2D && Screen2D->ButtonAt(Screen, Over);
	return TMSim::FMap::NodeOf(Seen) == TMSim::FMap::NodeOf(Point) && !CoveredByBody(Screen) && !bUnderButton;
}

bool ATMRobotPlayer::CoveredByBody(const FVector2D& Screen) const
{
	// A unit standing in front of the spot, on screen: a click there is a click
	// on that unit, in Godot and here. A person clicks a clear bit of ground.
	APlayerController* Player = GetWorld()->GetFirstPlayerController();
	for (const TMSim::FUnit& Unit : Director->Battle.Units)
	{
		if ((!Unit.IsAlive() && !Unit.IsKo()) || !Director->IsSeen(Unit))
		{
			continue;
		}
		if (!Unit.IsAlive())
		{
			// Lying down: only its plate stands up, and the director takes a
			// click within 18 pixels of it as a click on the fallen unit.
			FVector2D Plate;
			if (Player && Player->ProjectWorldLocationToScreen(Director->GetActorTransform().TransformPosition(
					Director->ShownAt(Unit) + FVector(0.0f, 0.0f, 20.0f)), Plate)
				&& FVector2D::Distance(Plate, Screen) <= 24.0)
			{
				return true;
			}
			continue;
		}
		const FVector Base = Director->GetActorTransform().TransformPosition(Director->ShownAt(Unit));
		FVector2D Feet;
		FVector2D Head;
		if (Player && Player->ProjectWorldLocationToScreen(Base, Feet)
			&& Player->ProjectWorldLocationToScreen(Base + FVector(0.0f, 0.0f, 180.0f), Head))
		{
			const FVector2D Along = Head - Feet;
			const double Length = Along.Size();
			if (Length > 1.0)
			{
				const double T = FMath::Clamp(FVector2D::DotProduct(Screen - Feet, Along) / (Length * Length), 0.0, 1.0);
				// A little wider than the director's test, as a person leaves a margin.
				if (FVector2D::Distance(Screen, Feet + Along * T) <= Length * 0.32)
				{
					return true;
				}
			}
		}
	}
	return false;
}

bool ATMRobotPlayer::Aim(const FVector2D& Screen)
{
	Director->RobotCursor = Screen;
	ClickIn = 2;
	return true;
}

bool ATMRobotPlayer::RunStep(FStep& Step, float DeltaSeconds)
{
	using EKind = FStep::EKind;
	switch (Step.Kind)
	{
	case EKind::Wait:
		Step.Seconds -= DeltaSeconds;
		return Step.Seconds <= 0.0f;

	case EKind::ClickButton:
	{
		FTMHudButton Found;
		ATMBattleHud* Screen = Hud();
		if (!Screen || !Screen->FindButton(Step.Action, Step.Value, Found))
		{
			// Buttons come and go with the frame; give it a moment before
			// deciding it is not there.
			Step.Seconds += DeltaSeconds;
			if (Step.Seconds < 1.0f)
			{
				return false;
			}
			Problem(FString::Printf(TEXT("the button a person would press next (%d) was not on screen"), static_cast<int32>(Step.Action)));
			Queue.SetNum(1);  // this plan is off; the rest of it would only fail too
			return true;
		}
		return Aim(Found.Area.GetCenter());
	}

	case EKind::ClickUnit:
	{
		FVector2D At;
		if (!ScreenOfUnit(Step.UnitId, At))
		{
			Problem(FString::Printf(TEXT("unit %d is not on screen to click"), Step.UnitId));
			Queue.SetNum(1);
			return true;
		}
		return Aim(At);
	}

	case EKind::ClickGround:
	{
		FVector2D At;
		if (!ScreenOfGround(Step.Point, At))
		{
			Problem(TEXT("the spot to click is not on screen"));
			Queue.SetNum(1);
			return true;
		}
		WalkTarget = Step.Point;
		if (Step.bFlag && !Visible(Step.Point, At))
		{
			// Hidden behind higher ground from where the camera stands: a person
			// could not click it either, so they would click the nearest spot
			// they can see that the unit can reach. Not the game's fault.
			TArray<TPair<double, TMSim::FVec2>> Near;
			for (const std::pair<TMSim::FNode, double>& Entry : Director->Reachable)
			{
				const TMSim::FVec2 Spot = TMSim::FMap::NodePos(Entry.first);
				Near.Add(TPair<double, TMSim::FVec2>(Spot.DistanceTo(Step.Point), Spot));
			}
			Near.Sort([](const TPair<double, TMSim::FVec2>& A, const TPair<double, TMSim::FVec2>& B) { return A.Key < B.Key; });
			bool bFound = false;
			for (int32 i = 0; i < Near.Num() && i < 80 && !bFound; ++i)
			{
				FVector2D There;
				const TMSim::FUnit* Walker = Director->Battle.FindUnit(CurrentUnit);
				if (Walker && Near[i].Value == Walker->Pos)
				{
					continue;
				}
				if (ScreenOfGround(Near[i].Value, There) && Visible(Near[i].Value, There))
				{
					WalkTarget = Near[i].Value;
					At = There;
					bFound = true;
				}
			}
			Tally.FindOrAdd(TEXT("hidden spot")).Y += 1;
			if (bFound)
			{
				Tally.FindOrAdd(TEXT("hidden spot")).X += 1;
			}
		}
		return Aim(At);
	}

	case EKind::ClickTarget:
	{
		// A person clicks the unit they are aiming at, where there is one, and
		// the ground otherwise.
		int32 Standing = Step.UnitId;
		if (Standing < 0)
		{
			for (const TMSim::FUnit& Unit : Director->Battle.Units)
			{
				if ((Unit.IsAlive() || Unit.IsKo()) && Unit.Pos.DistanceTo(Step.Point) < 0.3f)
				{
					Standing = Unit.Id;
					break;
				}
			}
		}
		FVector2D At;
		const bool bOk = Standing >= 0 ? ScreenOfUnit(Standing, At) : ScreenOfGround(Step.Point, At);
		if (!bOk)
		{
			Problem(TEXT("the target is not on screen to click"));
			Queue.SetNum(1);
			return true;
		}
		if (Standing < 0 && !Visible(Step.Point, At))
		{
			// Aimed at ground the camera cannot see: a person aims at the
			// nearest spot they can see that the rules still accept.
			Tally.FindOrAdd(TEXT("hidden aim")).Y += 1;
			for (int32 Ring = 1; Ring <= 4; ++Ring)
			{
				bool bFound = false;
				for (int32 Dx = -Ring; Dx <= Ring && !bFound; ++Dx)
				{
					for (int32 Dy = -Ring; Dy <= Ring && !bFound; ++Dy)
					{
						if (FMath::Max(FMath::Abs(Dx), FMath::Abs(Dy)) != Ring)
						{
							continue;
						}
						const TMSim::FVec2 Spot = TMSim::FMap::Snap(Step.Point + TMSim::FVec2(Dx * 0.5f, Dy * 0.5f));
						FVector2D There;
						if (ScreenOfGround(Spot, There) && Visible(Spot, There)
							&& Director->Battle.ValidateAbility(CurrentUnit, CurrentSlot, Spot, -1).empty())
						{
							At = There;
							bFound = true;
						}
					}
				}
				if (bFound)
				{
					Tally.FindOrAdd(TEXT("hidden aim")).X += 1;
					break;
				}
			}
		}
		return Aim(At);
	}

	case EKind::ClickPlaceable:
	{
		// One of the spots the game marks for it now, a different one for each
		// unit; far from where it stands, so the move is plain to see.
		const TMSim::FUnit* Unit = Director->Battle.FindUnit(Step.UnitId);
		const std::vector<TMSim::FNode> Spots = Unit ? Director->Battle.PlaceableNodes(*Unit) : std::vector<TMSim::FNode>();
		if (Spots.empty())
		{
			Problem(FString::Printf(TEXT("unit %d has nowhere marked to be placed"), Step.UnitId));
			Queue.SetNum(1);
			return true;
		}
		// Start from a different one for each unit, and take the first a person
		// could click: in view, not behind higher ground or another unit.
		FVector2D At;
		bool bFound = false;
		for (size_t Offset = 0; Offset < Spots.size() && !bFound; ++Offset)
		{
			const TMSim::FVec2 Spot = TMSim::FMap::NodePos(Spots[(Step.Value * 11 + 5 + Offset * 7) % Spots.size()]);
			if (ScreenOfGround(Spot, At) && Visible(Spot, At))
			{
				LastPlaced = Spot;
				bFound = true;
			}
		}
		if (!bFound)
		{
			Problem(TEXT("none of the spots marked for placing can be seen to click"));
			Queue.SetNum(1);
			return true;
		}
		return Aim(At);
	}

	case EKind::GrabPanel:
	{
		const ATMBattleHud* Screen = Hud();
		if (Screen)
		{
			for (int32 i = 0; i < Screen->Movables.Num(); ++i)
			{
				if (Screen->Movables[i].Id == Step.Meaning)
				{
					return Aim(Screen->Movables[i].Area.GetCenter());
				}
			}
		}
		Problem(FString::Printf(TEXT("the %s had no handle to take hold of in Edit layout"), *Step.Meaning));
		Queue.SetNum(1);
		return true;
	}

	case EKind::GrabSquare:
	{
		// The Nth of this side's squares, left to right.
		const ATMBattleHud* Screen = Hud();
		TArray<TPair<double, int32>> Row;
		if (Screen)
		{
			for (const TPair<int32, FBox2D>& Square : Screen->SquareAreas)
			{
				const TMSim::FUnit* Unit = Director->Battle.FindUnit(Square.Key);
				if (Unit && Unit->Team == 0)
				{
					Row.Add(TPair<double, int32>(Square.Value.GetCenter().X, Square.Key));
				}
			}
		}
		Row.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B) { return A.Key < B.Key; });
		if (!Row.IsValidIndex(Step.Value))
		{
			Problem(TEXT("there was no turn square to take hold of in Edit layout"));
			Queue.SetNum(1);
			return true;
		}
		LastGrabbedSquare = Row[Step.Value].Value;
		return Aim(Screen->SquareAreas[LastGrabbedSquare].GetCenter());
	}

	case EKind::Slide:
		// Held down and moved a little each frame, as a hand does.
		if (!bSliding)
		{
			bSliding = true;
			SlideLeft = FVector2D(Step.Point.X, Step.Point.Y);
		}
		{
			const FVector2D Bit = SlideLeft.GetClampedToMaxSize(12.0);
			Director->RobotCursor += Bit;
			SlideLeft -= Bit;
			if (SlideLeft.Size() < 0.5)
			{
				bSliding = false;
				return true;
			}
		}
		return false;

	case EKind::Picture:
		FScreenshotRequest::RequestScreenshot(OutDir / Step.Meaning, true, false);
		StepClock = -0.5f;
		return true;

	case EKind::Release:
		Director->OnKeyUp(EKeys::LeftMouseButton);
		StepClock = -Between;
		return true;

	case EKind::Key:
		Director->OnKey(Step.Key);
		StepClock = -Between;
		return true;

	case EKind::EnsureMove:
	{
		using EAimMode = ATMBattleDirector::EAimMode;
		if (Director->AimMode == EAimMode::Move && Director->bSprinting == Step.bFlag)
		{
			return true;
		}
		// Press the button, then look again next time round.
		FTMHudButton Found;
		ATMBattleHud* Screen = Hud();
		const ETMHudAction Want = Step.bFlag ? ETMHudAction::Sprint : ETMHudAction::Move;
		if (Step.Value > 2 || !Screen || !Screen->FindButton(Want, -2, Found))
		{
			Problem(FString::Printf(TEXT("could not get into %s mode with the %s button"),
				Step.bFlag ? TEXT("sprint") : TEXT("walk"), Step.bFlag ? TEXT("Sprint") : TEXT("Move")));
			Queue.SetNum(1);
			return true;
		}
		++Step.Value;
		Aim(Found.Area.GetCenter());
		return false;
	}

	case EKind::EnsureAbility:
	{
		using EAimMode = ATMBattleDirector::EAimMode;
		if (Director->AimMode == EAimMode::Ability && Director->AimSlot == Step.Value)
		{
			return true;
		}
		if (Step.UnitId > 2)
		{
			Problem(FString::Printf(TEXT("could not start aiming ability %d"), Step.Value + 1));
			Queue.SetNum(1);
			return true;
		}
		++Step.UnitId;  // used here as a count of tries
		if (Step.bFlag)
		{
			const FKey Keys[4] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four };
			Director->OnKey(Keys[FMath::Clamp(Step.Value, 0, 3)]);
			StepClock = -Between;
			return false;
		}
		FTMHudButton Found;
		ATMBattleHud* Screen = Hud();
		if (!Screen || !Screen->FindButton(ETMHudAction::Ability, Step.Value, Found))
		{
			Problem(FString::Printf(TEXT("ability %d's button was not on screen"), Step.Value + 1));
			Queue.SetNum(1);
			return true;
		}
		Aim(Found.Area.GetCenter());
		return false;
	}

	case EKind::Check:
		if (Step.Test && Step.Test())
		{
			return true;
		}
		Step.Seconds -= DeltaSeconds;
		if (Step.Seconds > 0.0f)
		{
			return false;
		}
		Problem(Step.Meaning);
		// The rest of this plan depended on it. A person would give up on the
		// turn too: end it, if it is still ours.
		Queue.SetNum(1);
		if (Director->Screen == ATMBattleDirector::EScreen::Battle && Director->SelectedUnit())
		{
			FStep End;
			End.Kind = EKind::ClickButton;
			End.Action = ETMHudAction::EndTurn;
			Queue.Add(End);
		}
		if (Director->bMenuOpen)
		{
			FStep Resume;
			Resume.Kind = EKind::ClickButton;
			Resume.Action = ETMHudAction::MenuResume;
			Queue.Add(Resume);
		}
		return true;
	}
	return true;
}

// ------------------------------------------------------------------ writing it down

FString ATMRobotPlayer::StateNow() const
{
	FString Out = FString::Printf(TEXT("aim %d slot %d sprint %d, selected %d, placing %d, hover unit %d, pointer %.0f,%.0f"),
		static_cast<int32>(Director->AimMode), Director->AimSlot, Director->bSprinting ? 1 : 0, Director->SelectedId,
		Director->PlaceId, Director->HoverUnitId, Director->RobotCursor.X, Director->RobotCursor.Y);
	if (Director->bHaveHover)
	{
		Out += FString::Printf(TEXT(", board under pointer %.2f,%.2f"), Director->HoverPoint.X, Director->HoverPoint.Y);
	}
	if (const TMSim::FUnit* Unit = Director->Battle.FindUnit(CurrentUnit))
	{
		Out += FString::Printf(TEXT("; unit %d (%hs) at %.2f,%.2f ready %d moved %d acted %d turn %d"), Unit->Id, Unit->Job.c_str(),
			Unit->Pos.X, Unit->Pos.Y, Unit->bReady ? 1 : 0, Unit->bMoved ? 1 : 0, Unit->bActed ? 1 : 0, Unit->Serial);
	}
	if (!CurrentIntent.IsEmpty())
	{
		Out += TEXT("; wanted ") + CurrentIntent;
	}
	return Out;
}

void ATMRobotPlayer::Problem(const FString& What)
{
	// The same problem again is a count on the first one, not a new entry.
	int32& Seen = Repeats.FindOrAdd(What);
	++Seen;
	if (Seen > 1)
	{
		UE_LOG(LogTemp, Warning, TEXT("ROBOT PROBLEM again (%d times): %s -- %s"), Seen, *What, *StateNow());
		return;
	}
	Problems.Add(What);
	const int32 N = Problems.Num();
	FString Picture;
	if (N <= MaxPictures)
	{
		Picture = FString::Printf(TEXT("problem_%02d.png"), N);
		FScreenshotRequest::RequestScreenshot(OutDir / Picture, true, false);
	}
	const FString Line = FString::Printf(TEXT("PROBLEM %d (session %d, %.0fs in): %s%s\n      %s"), N, static_cast<int32>(Session) + 1,
		SessionClock, *What, Picture.IsEmpty() ? TEXT("") : *(TEXT("   [") + Picture + TEXT("]")), *StateNow());
	Log.Add(Line);
	UE_LOG(LogTemp, Warning, TEXT("ROBOT %s"), *Line);
}

void ATMRobotPlayer::Note(const FString& What)
{
	Log.Add(What);
	UE_LOG(LogTemp, Log, TEXT("ROBOT %s"), *What);
}

void ATMRobotPlayer::Finish()
{
	Session = ESession::Done;
	FString Report = TEXT("Robot playtester report\n\n");
	Report += FString::Printf(TEXT("%d battles played through the controls, %d problems.\n\n"), BattlesSeen, Problems.Num());
	Report += TEXT("What it tried, and how often it worked:\n");
	for (const TPair<FString, FIntPoint>& Each : Tally)
	{
		Report += FString::Printf(TEXT("  %-10s %d of %d\n"), *Each.Key, Each.Value.X, Each.Value.Y);
	}
	Report += TEXT("\nHow often each problem happened:\n");
	for (const TPair<FString, int32>& Each : Repeats)
	{
		Report += FString::Printf(TEXT("  %3d x %s\n"), Each.Value, *Each.Key);
	}
	Report += TEXT("\nIn order:\n");
	for (const FString& Line : Log)
	{
		Report += TEXT("  ") + Line + TEXT("\n");
	}
	FFileHelper::SaveStringToFile(Report, *(OutDir / TEXT("report.txt")));
	UE_LOG(LogTemp, Log, TEXT("ROBOT DONE: %d battles, %d problems -> %s"), BattlesSeen, Problems.Num(), *OutDir);
	FPlatformMisc::RequestExit(false);
}
