// A robot that plays the game the way a person does, to find what a person
// would trip over.
//
// Every automated check so far has either asked the rules directly or taken
// pictures; none has used the controls. This does: it moves a pointer over
// the screen and clicks, through the same code a mouse click takes
// (ATMBattleDirector::CursorPosition and OnKey), and presses keys the same way.
// It reads the HUD's own buttons to know where to click, as a person reads the
// screen, and the board through the camera.
//
// What it wants to do each turn comes from the computer player -- a sensible
// person's intent -- and then it does it with the controls: picks the unit up,
// clicks where to walk, clicks the ability, clicks the target, ends the turn.
// After every step it checks the step did what it was meant to. When one did
// not, that is a problem a person would have hit too: it is written down with
// a picture, and the robot carries on as a person would.
//
// Started with -tmrobot. Plays three sessions -- against the computer as blue,
// as red with planning time and holding the middle, and two players at one
// machine -- then writes Saved/Robot/report.txt and exits.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InputCoreTypes.h"

#include "SimTypes.h"
#include "TMBattleHud.h"

#include "TMRobotPlayer.generated.h"

class ATMBattleDirector;

UCLASS(Transient, NotPlaceable)
class TACTICALMASTERS_API ATMRobotPlayer : public AActor
{
	GENERATED_BODY()

public:
	ATMRobotPlayer();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	/** One thing a person does, then how to tell it worked. */
	struct FStep
	{
		enum class EKind : uint8 { ClickButton, ClickUnit, ClickGround, ClickTarget, ClickPlaceable, Key, EnsureMove, EnsureAbility, Check, Wait,
			/** Edit layout: take hold of a panel's handle, or a turn square; slide the pointer; let go. */
			GrabPanel, GrabSquare, Slide, Release, GrabGrip,
			/** A picture of the screen as it is, named by Meaning. */
			Picture };
		EKind Kind = EKind::Wait;
		ETMHudAction Action = ETMHudAction::None;
		int32 Value = -1;
		int32 UnitId = -1;
		TMSim::FVec2 Point;
		FKey Key;
		bool bFlag = false;
		/** For a Check: what should now be true, and what it means if it is not. */
		TFunction<bool()> Test;
		FString Meaning;
		float Seconds = 0.0f;
	};

	enum class ESession : uint8 { VsComputerBlue, VsComputerRed, TwoPlayers, Done };

	// Building steps.
	void Push(FStep Step) { Queue.Add(MoveTemp(Step)); }
	void Button(ETMHudAction Action, int32 Value = -1);
	void Check(TFunction<bool()> Test, const FString& Meaning, float Seconds = 2.0f);
	void Pause(float Seconds);

	// Deciding what to do next when nothing is queued.
	void Decide();
	void StartSession();
	void PlanTurn(const struct TMSim::FUnit& Unit);
	void PlanPlacing(int32 Team);
	void PlanLooking();
	/** Edit layout: move a panel, reorder a turn square, reset, lock (hud.gd:203-240). */
	void PlanLayout();
	bool bArranged = false;
	/** The unit whose square was taken hold of, for the reorder check. */
	int32 LastGrabbedSquare = -1;
	/** The pointer part way along a slide, and how far is left. */
	FVector2D SlideLeft = FVector2D::ZeroVector;
	bool bSliding = false;
	void Finish();

	// Doing a step, frame by frame.
	bool RunStep(FStep& Step, float DeltaSeconds);
	bool Aim(const FVector2D& Screen);
	bool ScreenOfUnit(int32 UnitId, FVector2D& Out) const;
	bool ScreenOfGround(const TMSim::FVec2& Point, FVector2D& Out) const;
	/** Whether the camera sees this spot at that screen point, rather than something in front of it. */
	bool Visible(const TMSim::FVec2& Point, const FVector2D& Screen) const;
	void Click();
	void Problem(const FString& What);
	/** What the game looked like when a step failed: enough to tell the game's fault from the robot's. */
	FString StateNow() const;
	void Note(const FString& What);

	ATMBattleDirector* Director = nullptr;
	ATMBattleHud* Hud() const;

	TArray<FStep> Queue;
	/** Frames left before the pending click lands, once the pointer is in place. */
	int32 ClickIn = -1;
	float StepClock = 0.0f;
	/** Seconds between a person's actions: long enough for each to show. */
	float Between = 0.25f;

	ESession Session = ESession::VsComputerBlue;
	bool bSessionStarted = false;
	bool bLooked = false;
	int32 BattlesSeen = 0;
	float IdleFor = 0.0f;
	float SessionClock = 0.0f;
	float TotalClock = 0.0f;
	/** The last turn a plan was made for, so a turn that went wrong is not retried for ever. */
	TMap<int32, int32> PlannedSerial;
	/** How many plans this turn of this unit has had; after a few it is left to its clock. */
	TMap<int32, int32> Attempts;
	/** Where the last placing click was aimed, for its check. */
	TMSim::FVec2 LastPlaced;
	/** The unit and intent being carried out, for the problem report. */
	int32 CurrentUnit = -1;
	/** The turn the current plan is for; once it is over, so is the plan. */
	int32 CurrentSerial = -1;
	/** The ability slot the current plan uses, for finding another spot to aim at. */
	int32 CurrentSlot = -1;
	FString CurrentIntent;
	bool bPlaced = false;
	/** Where a walk is going: the intended spot, or the nearest one a person could see to click. */
	TMSim::FVec2 WalkTarget;
	/** Whether the queue holds a turn's plan, which the end of the battle cancels. */
	bool bTurnPlan = false;
	/** Whether a unit's body covers this screen point, as the director judges it. */
	bool CoveredByBody(const FVector2D& Screen) const;
	/** The same problem again is counted, not written out again. */
	TMap<FString, int32> Repeats;
	int32 Turns = 0;

	TArray<FString> Problems;
	TArray<FString> Log;
	TMap<FString, FIntPoint> Tally;  // what was tried: (worked, tried)
	FString OutDir;
	/**
	 * The player's settings file as it was when the robot started. The robot
	 * plays with the real controls, so it moves panels, resets the layout and
	 * the like; all of that is put back exactly as the player left it when it
	 * stops, however it stops.
	 */
	FString SettingsBefore;
	bool bHadSettings = false;
};
