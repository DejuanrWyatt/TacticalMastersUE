// Puts the ported rules on screen.
//
// The rules live in TMSim and know nothing about Unreal -- no actors, no
// meshes, no world. This is the piece in the middle: it owns a battle, steps
// its clock, and keeps one visible unit in the level for each unit in the
// simulation. Nothing here decides anything about the game; it reads what the
// simulation says and shows it.
//
// The controls are CallInEditor so a battle can be built and stepped without
// entering Play, which is how it is checked from outside the editor.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InputCoreTypes.h"

#include "SimAI.h"
#include "SimBattle.h"
#include "SimOrder.h"

#include "TMBattleDirector.generated.h"

class USkeletalMesh;
class UAnimSequence;
class USkeletalMeshComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UPointLightComponent;
class UTextRenderComponent;
class ACameraActor;

/**
 * A number rising off a unit. It is a USTRUCT only so the component it holds is
 * kept from the garbage collector; nothing about it is game state.
 */
USTRUCT()
struct FTMFloater
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<class UTextRenderComponent> Text = nullptr;

	UPROPERTY()
	int32 UnitId = -1;

	UPROPERTY()
	float Age = 0.0f;
};

UCLASS()
class TACTICALMASTERS_API ATMBattleDirector : public AActor
{
	GENERATED_BODY()

public:
	ATMBattleDirector();

	/** The mesh every unit wears until classes have their own. */
	UPROPERTY(EditAnywhere, Category = "Tactical Masters")
	TSoftObjectPtr<USkeletalMesh> UnitMesh;

	/** Metres to Unreal units. One tile is one metre in the rules. */
	UPROPERTY(EditAnywhere, Category = "Tactical Masters")
	float TileSize = 100.0f;

	/** How high the top of a tile sits, so units stand on the board. */
	UPROPERTY(EditAnywhere, Category = "Tactical Masters")
	float BoardHeight = 20.0f;

	/** The light under whoever's turn it is. */
	UPROPERTY(EditAnywhere, Category = "Tactical Masters")
	FLinearColor ReadyColour = FLinearColor(1.0f, 0.78f, 0.25f, 1.0f);

	UPROPERTY(EditAnywhere, Category = "Tactical Masters")
	float ReadyLightBrightness = 12000.0f;

	/** The mesh each tile of the board is built from. */
	UPROPERTY(EditAnywhere, Category = "Tactical Masters")
	TSoftObjectPtr<UStaticMesh> TileMesh;

	/** Builds a battle and a visible unit for each of its units. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Tactical Masters")
	void BuildBattle();

	/** Runs the clock on. Ten of these is a second of battle. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Tactical Masters")
	void StepTicks(int32 Ticks = 10);

	/** What the simulation currently says, as text, for checking against it. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Tactical Masters")
	FString DescribeBattle() const;
	/** How the battle was won, in a few words: on time, by holding the middle, or neither. */
	FString HowWon() const;
	/**
	 * Where the pointer is, in screen pixels. The mouse's, unless the robot
	 * playtester is driving (-tmrobot), when it is the robot's: everything that
	 * reads the pointer reads it here, so the robot's clicks take exactly the
	 * path a person's do.
	 */
	bool CursorPosition(float& X, float& Y) const;
	bool bRobotDriving = false;
	FVector2D RobotCursor = FVector2D(-1.0, -1.0);
	/**
	 * Which side this machine places units for while planning, or -1 when it
	 * places for nobody, as when watching two computers (battle.gd:340-351).
	 */
	int32 PlanningTeam() const;
	/** The unit being placed while planning, or -1. */
	int32 PlaceId = -1;
	/** Done placing: tells the rules this side is ready to fight. */
	void ReadyToFight();

	// ---------------------------------------------------------- the camera
	// camera_rig.gd: it sits on a point of the board and looks at it from a
	// distance, turned and tilted. Keys pan, turn and raise it; the right button
	// drags it round, the middle one drags it along, the wheel brings it closer.
	FVector CamTarget = FVector::ZeroVector;
	FVector CamWantTarget = FVector::ZeroVector;
	float CamYaw = 45.0f;
	float CamPitch = -32.0f;
	float CamDistance = 3000.0f;
	float CamWantDistance = 3000.0f;
	FVector2D LastCursor = FVector2D(-1.0, -1.0);
	bool bRightHeld = false;
	bool bMiddleHeld = false;
	/** How far the right button has dragged since it went down: a short click cancels an aim. */
	float RightDragged = 0.0f;
	void UpdateCamera(float DeltaSeconds);
	void ApplyCamera();
	/** Brings the camera round to the selected unit, or the first of this side's that is ready. */
	void CenterCamera();
	void OnKeyUp(FKey Key);

	// ---------------------------------------------- options and dev tools
	bool bOptionsOpen = false;
	bool bDevToolsOpen = false;
	/** The action waiting for its new key, or -1. */
	int32 CaptureAction = -1;
	/** The slider being dragged, or -1. 0 is the camera speed; 100 + i is rule number i. */
	int32 DragSlider = -1;
	static constexpr int32 SliderCameraSpeed = 0;
	static constexpr int32 SliderTuning = 100;
	/** Rule numbers changed during a battle, sent as one order once the dragging stops. */
	TMap<int32, double> TunePending;
	float TunePendingFor = -1.0f;
	bool SliderRange(int32 Id, double& Low, double& High, double& Step) const;
	double SliderValue(int32 Id) const;
	void SetSlider(int32 Id, double Value);
	void FlushTuning();
	/** Whether a rule number is set on the battle setup screen rather than in Developer Tools. */
	static bool OnSetupScreen(int32 TuningIndex);

	// ------------------------------------------------------- edit layout
	// layout_editor.gd: every panel gets a handle and can be dragged anywhere,
	// the turn squares reordered; one press to start, one to lock.
	bool bEditingLayout = false;
	void ToggleLayout();
	/** The panel being dragged: where it and the pointer started, and where it would sit unmoved. */
	FString DragPanel;
	FVector2D DragFrom = FVector2D::ZeroVector;
	FVector2D DragStartMin = FVector2D::ZeroVector;
	FVector2D DragHomeMin = FVector2D::ZeroVector;
	/** The panel being resized by its grip, its size and width when taken hold of. */
	FString ResizePanel;
	float ResizeStartScale = 1.0f;
	float ResizeStartWidth = 1.0f;
	/** The unit whose turn square is being dragged along its row, or -1. */
	int32 DragCard = -1;
	/** Puts the dragged square where it was dropped among its side's. */
	void DropCard();

	/** Clears the visible units and forgets the battle. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Tactical Masters")
	void ClearBattle();

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

private:
	/** Moves each visible unit to where the simulation has it. */
	void RefreshVisuals();

	/** Builds the board out of the map the rules are actually using. */
	void BuildBoard();

	/** Metres in the rules to Unreal's centimetres. */
	FVector WorldFromMetres(const TMSim::FVec2& Point, int Level) const;

	UPROPERTY()
	TArray<TObjectPtr<class UStaticMeshComponent>> TileVisuals;

	UPROPERTY()
	TArray<TObjectPtr<class UPointLightComponent>> ReadyLights;

	/** Where a unit standing at this spot on the board belongs in the world. */
	FVector WorldFor(const TMSim::FUnit& Unit) const;

public:
	/** Walks a unit to a spot on the board, in metres. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Tactical Masters")
	FString MoveUnitTo(int32 UnitId, float MetresX, float MetresY, bool bSprint = false);

	/** Ends a unit's turn, as giving no further orders would. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Tactical Masters")
	void EndUnitTurn(int32 UnitId);

	/** Every spot a unit could walk to this turn, for checking and for drawing. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Tactical Masters")
	int32 ReachableCount(int32 UnitId) const;

	/** Sends one of a unit's four abilities at a spot, or says why it cannot. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Tactical Masters")
	FString OrderAbility(int32 UnitId, int32 Slot, float MetresX, float MetresY, int32 FollowId = -1);

	/**
	 * The same, aimed at a unit rather than at a map reference. This is what a
	 * click on somebody amounts to: the spell is aimed at where they are standing
	 * and follows them if it takes a while to arrive.
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Tactical Masters")
	FString OrderAbilityAt(int32 UnitId, int32 Slot, int32 TargetUnitId);

	/** What has happened lately, newest last: the fight as a person would read it. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Tactical Masters")
	FString BattleLog() const;

	/** How many lines of it to keep. */
	UPROPERTY(EditAnywhere, Category = "Tactical Masters")
	int32 LogLines = 40;

	/** How long a damage number lingers, and how big it is drawn. */
	UPROPERTY(EditAnywhere, Category = "Tactical Masters|Reading a fight")
	float FloaterSeconds = 1.4f;

	UPROPERTY(EditAnywhere, Category = "Tactical Masters|Reading a fight")
	float FloaterSize = 26.0f;

	/** How long a struck unit's light flares. */
	UPROPERTY(EditAnywhere, Category = "Tactical Masters|Reading a fight")
	float FlashSeconds = 0.25f;

	/**
	 * How long an ability's particle effect plays before it is switched off, and
	 * how high above a unit's feet it plays, in centimetres.
	 */
	UPROPERTY(EditAnywhere, Category = "Tactical Masters|Reading a fight")
	float VfxSeconds = 2.0f;

	UPROPERTY(EditAnywhere, Category = "Tactical Masters|Reading a fight")
	float VfxHeight = 90.0f;

	// The view. Back along the diagonal and up, as a share of the board's width,
	// so the framing holds whatever size map is loaded.

	UPROPERTY(EditAnywhere, Category = "Tactical Masters|Camera")
	float CameraBack = 1.15f;

	UPROPERTY(EditAnywhere, Category = "Tactical Masters|Camera")
	float CameraHeight = 1.02f;

	UPROPERTY(EditAnywhere, Category = "Tactical Masters|Camera")
	float CameraPitch = -32.0f;

	UPROPERTY(EditAnywhere, Category = "Tactical Masters|Camera")
	float CameraFov = 40.0f;

	/** Seconds of battle between pictures; 0 takes none. */
	UPROPERTY(EditAnywhere, Category = "Tactical Masters|Camera")
	float CaptureEverySeconds = 0.0f;

	UPROPERTY(EditAnywhere, Category = "Tactical Masters|Camera")
	int32 CaptureWidth = 1600;

	UPROPERTY(EditAnywhere, Category = "Tactical Masters|Camera")
	int32 CaptureHeight = 900;

	// The computer player. It is for practising against and for testing with,
	// and it plays by clicking: it hands over an order and the order is checked
	// and applied exactly as one arriving from a person would be. Set both sides
	// to the computer to watch a battle play itself, or neither for two people.

	/**
	 * Blue is the person at this machine by default, and red the computer. A run
	 * with nobody watching (-unattended), or one started with -tmwatch, hands both
	 * sides to the computer so a battle can still play itself to the end.
	 */
	UPROPERTY(EditAnywhere, Category = "Tactical Masters|Computer")
	bool bComputerPlaysTeam0 = false;

	UPROPERTY(EditAnywhere, Category = "Tactical Masters|Computer")
	bool bComputerPlaysTeam1 = true;

	/** "easy", "medium" or "hard". Hard never settles for a worse option. */
	UPROPERTY(EditAnywhere, Category = "Tactical Masters|Computer")
	FString ComputerSkill = TEXT("hard");

	/** One order from the computer for this unit. Empty if it was accepted. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Tactical Masters")
	FString TakeComputerTurn(int32 UnitId);

	/**
	 * Lets the computer play every side it is set to play until nobody is
	 * waiting on it. Returns how many orders it gave. The cap is there so a
	 * mistake cannot spin the editor.
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Tactical Masters")
	int32 PlayComputerTurns(int32 MaxOrders = 64);

private:

	/**
	 * The one door every order comes through, whoever gave it: a person at this
	 * machine, the computer player, or -- when there is one -- a message off the
	 * network. Refusing is the same refusal for all three, which is the point.
	 */
	FString Submit(const TMSim::FOrder& Order);

	/** Whether the computer is the one playing this side. */
	bool ComputerPlays(int32 Team) const;

	/**
	 * Turns what the rules just reported into something readable. The events are
	 * presentation only -- nothing in the rules reads them back -- so this is free
	 * to say it however it likes, and free to be wrong without breaking a battle.
	 */
	void Narrate(const TMSim::FTickReport& Report);

	/** How a unit is referred to in the log. */
	FString NameOf(int32 UnitId) const;

	/**
	 * Numbers that rise off a unit and fade. Presentation and nothing else: they
	 * are built from the events the rules report, the rules never read them back,
	 * and losing one would not change a battle by a hair.
	 */
	void ShowEvents(const TMSim::FTickReport& Report);
	/** Plays an ability's particle effect, if it names one, at a place in the world. */
	void PlayVfx(const TMSim::FAbility& Ability, const FVector& Where);
	void AdvanceVfx(float DeltaSeconds);

	/** Moves, billboards and fades them, and settles the flash on a struck unit. */
	void AdvanceFloaters(float DeltaSeconds);

	/** Who each unit is and how it is doing, over its head. */
	void RefreshPlates();

	UPROPERTY()
	TArray<TObjectPtr<class UTextRenderComponent>> Plates;

	/** Something happened that a picture would show. */
	bool bWorthSeeing = false;

	/** Puts a camera on the board this director built and looks through it. */
	void FrameTheBoard();

	/** Takes a picture every so often, when asked to. */
	void MaybeCapture();

	// ------------------------------------------------------------ match flow
	//
	// The title screen, the battle setup and the menu inside a battle, after the
	// Godot game's main_menu.gd and battle_setup.gd. Like everything else in the
	// view, none of it is rules state: what it produces is a roster for each side,
	// who plays each side, and a seed, and those are what BuildBattle starts from.

	enum class EScreen : uint8 { Title, Setup, Battle };

	/** What the next battle will be (game_config.gd: mode, rosters, ai_team, difficulties). */
	struct FMatchSetup
	{
		/** "ai" (a person against the computer), "hotseat" (two people) or "cpu" (watch). */
		FString Mode = TEXT("ai");
		/** In "ai", the side the person plays. */
		int32 PlayerTeam = 0;
		/** Each side's four classes, by id. */
		std::string Rosters[2][4] = {
			{ "knight", "archer", "black_mage", "white_mage" },
			{ "knight", "archer", "black_mage", "white_mage" } };
		FString Difficulty[2] = { TEXT("hard"), TEXT("hard") };
		/** A fresh seed every battle, or the same one every time. */
		bool bRandomSeed = true;
		/**
		 * How else a battle can be won, as the Godot setup offers it
		 * (battle_setup.gd:40-42): seconds alone in the middle to win (0 is
		 * off: last team standing), and a time limit after which the healthier
		 * side wins (0 is none).
		 */
		double CaptureSeconds = 0.0;
		double BattleSeconds = 0.0;
		/** Seconds before the fighting to place units in (battle_setup.gd:44); 0 is none. */
		double PlanningSeconds = 0.0;
		uint64 FixedSeed = 12345;
	};

	FMatchSetup Setup;
	/** Whether a person chose the setup. If not, ComputerSkill sets both difficulties, as it always has. */
	bool bSetupChosen = false;
	EScreen Screen = EScreen::Battle;
	/** The menu opened inside a battle. It pauses a local game. */
	bool bMenuOpen = false;
	/** The seed the battle on the board was started from, to say and to replay. */
	uint64 BattleSeed = 12345;
	/** Seconds into -tmhudshots, or -1 when not taking pictures of the panels. */
	float HudShotsAt = -1.0f;
	/** A picture of the screen, HUD and all, under Saved/Match. */
	void CaptureNamed(const TCHAR* Name);
	/** Seconds into -tmmenushots, or -1 when not taking menu pictures. */
	float MenuShotsAt = -1.0f;

	/** Starts a battle from Setup and hands the sides to whoever plays them. */
	void StartMatch(bool bNewSeed);
	void OpenTitle();
	void OpenSetup();
	/** One of the buttons on the title, setup or in-battle menu. */
	void PressMenuButton(const struct FTMHudButton& Button);
	/** The setup slot a class is being picked for (team * 4 + slot), or -1 (class_picker.gd). */
	int32 PickerSlot = -1;
	/** The role the picker shows, as an index into tank, damage, support, special, or -1 for every class. */
	int32 PickerRole = -1;

	/** A tank, two damage dealers and someone to keep them standing (class_list.gd:62-77). */
	void RandomTeam(int32 Team);

	// ------------------------------------------------------ a person playing
	//
	// How a person gives orders, following the Godot game's battle.gd. None of
	// this is state: which unit is selected, what the mouse is over and what is
	// drawn on the board are the view's business. Every order still goes through
	// Submit, with the unit's real Serial, exactly as the computer's do.

	enum class EAimMode : uint8 { None, Move, Ability };

	/** Hooks up the keys and the mouse, and shows the pointer. */
	void SetUpPlayerInput();

	/** One handler for every key, so the bindings read as one table. */
	void OnKey(FKey Key);

	/** A left click on the board: walk, aim, or pick a unit. */
	void OnClick();

	/** Whether this machine may give this unit an order right now. */
	bool PlayerCanOrder(const TMSim::FUnit* Unit) const;

	/** The unit the person is ordering, or null. */
	const TMSim::FUnit* SelectedUnit() const;

	void SelectUnit(int32 UnitId);
	void Deselect();
	/** Picks this machine's ready unit with the least time left, if any. */
	void AutoSelect();
	/** The next of this machine's ready units, round and round. */
	void CycleReady();
	void EnterMoveMode(bool bSprint);
	void SelectAbility(int32 Slot);
	void CancelAim();

	/** Sends an order for the selected unit and takes the next step after it. */
	void OrderSelected(const TMSim::FOrder& Order);

	/** Keeps the selection honest as time runs: turns end, units fall. */
	void MaintainSelection();

	/** Works out what is under the mouse: a spot on the board, and a unit. */
	void PickUnderCursor();

	/** Where the chosen ability would land for where the mouse is, and whether it may. */
	struct FAim
	{
		bool bHave = false;
		TMSim::FVec2 Point;
		int32 Follow = -1;
		bool bOk = false;
		FString Why;
	};
	FAim Aim();

	/** Works out the way to the spot under the pointer, only when that spot changes. */
	void UpdateHoverPath();

	/** A click that landed on the HUD rather than the board. */
	void PressHudButton(const struct FTMHudButton& Button);

	/** Puts the battle HUD up for this machine's player. */
	void ShowHud();

	/** The HUD reads the selection, the aim and the log straight from here. */
	friend class ATMBattleHud;
	friend class ATMRobotPlayer;

	/** Something the person should read: why an order was refused, mostly. */
	void Tell(const FString& What);

	/** Metres on the board to a spot in the world, a little above the ground. */
	FVector BoardPoint(const TMSim::FVec2& Point, float Lift = 4.0f) const;

	/** Whether a person is at this machine giving orders at all. */
	bool bPlayerInput = false;
	bool bPaused = false;

	int32 SelectedId = -1;
	/** The turn the selection was made on, so a fresh turn starts afresh. */
	int32 SelectedSerial = -1;
	EAimMode AimMode = EAimMode::None;
	int32 AimSlot = -1;
	bool bSprinting = false;
	std::vector<std::pair<TMSim::FNode, double>> Reachable;

	bool bHaveHover = false;
	TMSim::FVec2 HoverPoint;
	int32 HoverUnitId = -1;

	/** The path last drawn, kept so the pathfinder is not asked every frame. */
	TMSim::FNode PathNode{ -9999, -9999 };
	std::vector<TMSim::FVec2> PathShown;

	/** Orders other than time applied so far, to notice when the board has changed. */
	int32 OrdersApplied = 0;
	int32 OrdersSeen = 0;

	FString Notice;
	float NoticeLeft = 0.0f;

	// ------------------------------------------------ what a person can look at
	//
	// The rest of the Godot HUD: a card for a clicked unit and the threat it
	// poses, the list of every unit, the log window, the Unit Guide, and fog of
	// war. All of it is looking, not playing: nothing here reaches the rules
	// except through questions that change nothing.

	/** A unit clicked on that is not taking orders, shown on its own card, or -1 (battle.gd:799-803). */
	int32 InspectedId = -1;

	/**
	 * Where an inspected enemy could walk this turn and how far its longest attack
	 * reaches from where it stands (battle.gd:512-527). Worked out again only when
	 * that unit moves or takes a turn.
	 */
	std::vector<TMSim::FNode> ThreatNodes;
	float ThreatReach = 0.0f;
	FString ThreatSignature;
	void UpdateThreat();

	/**
	 * The side this screen is played from, whose sight decides what is shown, or
	 * -1 to show everything (two people at one screen, or watching the computer).
	 */
	int32 ViewerTeam() const;
	/** Whether this screen may show that unit (battle.gd:302-303, _is_seen). */
	bool IsSeen(const TMSim::FUnit& Unit) const;
	/** Whether this screen may show what happens at a spot (battle.gd:306-307). */
	bool IsPointSeen(const TMSim::FVec2& Point) const;

	/** The log: shown or not, how many lines, and how far scrolled back. */
	bool bShowLog = true;
	bool bLogLarge = false;
	int32 LogScroll = 0;

	/** The list of every unit on the field (hud.gd:515-606). */
	bool bShowField = false;

	/** The Unit Guide: open, which class it shows, and which class the numbers are worked out against. */
	bool bGuideOpen = false;
	int32 GuideJob = 0;
	int32 GuideAgainst = 0;
	/** Opening the guide in a local battle pauses it, and closing it resumes (battle.gd:1039-1051). */
	bool bPausedByGuide = false;
	void ToggleGuide();

	UPROPERTY()
	TObjectPtr<ACameraActor> Watcher = nullptr;

	float NextCaptureAt = 0.0f;
	int32 Captured = 0;

	/** One unit's light flaring after it was struck. */
	struct FFlash
	{
		int32 UnitId = -1;
		float Age = 0.0f;
	};

	UPROPERTY()
	TArray<FTMFloater> Floaters;

	TArray<FFlash> Flashes;

	/** An ability's effect still playing, and for how long it has. */
	struct FPlayingVfx
	{
		TWeakObjectPtr<class UFXSystemComponent> Component;
		float Age = 0.0f;
	};
	TArray<FPlayingVfx> PlayingVfx;

	/** Every effect a class has named, loaded once; null for a path that named nothing. */
	UPROPERTY()
	TMap<FString, TObjectPtr<UObject>> LoadedVfx;

	/** How many effects have played, for the log at the end of a battle. */
	int32 EffectsPlayed = 0;



	UPROPERTY()
	TArray<FString> Log;

	/** The unit the battle is currently waiting on, or nullptr. */
	const TMSim::FUnit* WaitingOn() const;

	/** The first such unit on a side the computer plays, which is not the same. */
	const TMSim::FUnit* WaitingOnComputer() const;

	/** The rules. Plain C++, and deliberately unaware of everything above. */
	TMSim::FBattle Battle;

	/**
	 * One computer player per side, each with its own generator, so two computers
	 * at different difficulties can play each other. Not part of the rules, and
	 * holding nothing the rules need.
	 */
	TMSim::FAIPlayer Computers[2];

	/** Seconds still to wait before the computer gives its next order. */
	float ThinkRemainder = 0.0f;
	/** Who the last order was for, so a new turn gets the longer pause. */
	int32 ThinkingAbout = -1;
	/** Orders the computer has given, for the line printed when a battle ends. */
	int32 OrdersGiven = 0;
	/** Said once, so the result is not logged every frame after it is decided. */
	bool bSaidWon = false;
	/** Seconds since the battle was decided, for leaving an unattended run. */
	float DecidedFor = 0.0f;
	/** How many rising numbers have been put up, as evidence they are. */
	int32 NumbersShown = 0;

	UPROPERTY()
	TArray<TObjectPtr<USkeletalMeshComponent>> UnitVisuals;

	// ------------------------------------------------ bodies and animation
	// (TMBattleDirectorMotion.cpp). What each unit wears and how it moves, from
	// Content/Data/CharacterMap/characters.json. None of it is read by the rules:
	// a unit walks, swings and falls on screen because the rules said it moved,
	// hit and fell, never the other way round.

	/**
	 * The clips for one motion (TMSim::AnimMotions): what plays when it goes
	 * off -- one per slot, round -- and, for an ability with a cast time, what
	 * plays as it starts charging, what loops while it charges, and what plays
	 * when a charged one goes off.
	 */
	struct FTMMotionClips
	{
		TArray<UAnimSequence*> Release;
		UAnimSequence* Intro = nullptr;
		UAnimSequence* Windup = nullptr;
		UAnimSequence* CastRelease = nullptr;
	};

	/** One animation set: the clips for one skeleton. */
	struct FTMAnimSet
	{
		TMap<FString, FTMMotionClips> Motions;
		UAnimSequence* Idle = nullptr;
		UAnimSequence* Walk = nullptr;
		UAnimSequence* Run = nullptr;
		TArray<UAnimSequence*> Attack;
		UAnimSequence* Cast = nullptr;
		TArray<UAnimSequence*> Hit;
		TArray<UAnimSequence*> Death;
		UAnimSequence* Rise = nullptr;
		/** How fast a walk and a run cover the ground, in cm/s, to match the clips. */
		float WalkSpeed = 170.0f;
		float RunSpeed = 380.0f;
	};

	/** A body: a mesh, which way it faces, and the set it animates with. */
	struct FTMBody
	{
		USkeletalMesh* Mesh = nullptr;
		float Yaw = 0.0f;
		const FTMAnimSet* Animations = nullptr;
	};

	/** What one unit is doing on screen. */
	struct FTMMotion
	{
		const FTMBody* Body = nullptr;
		/** Where it is drawn, and which way it faces, in degrees. */
		FVector Shown = FVector::ZeroVector;
		float Yaw = 0.0f;
		/** The rest of a walk, as points on the board; empty when standing. */
		TArray<FVector> Path;
		bool bRun = false;
		/** Where the rules had it last frame, to notice it moving. */
		TMSim::FVec2 SimPos;
		/** The clip playing, and how long a one-off has left. */
		UAnimSequence* Playing = nullptr;
		float OneShotLeft = 0.0f;
		/** An action waiting for a walk to end, and the way to face for it. */
		UAnimSequence* Queued = nullptr;
		float QueuedYaw = 0.0f;
		/** Knocked out and lying down. */
		bool bDown = false;
		/** Its ability was charged before it went off, so the charged release plays. */
		bool bWasCasting = false;
	};

	/** A motion's clips in this set, or the nearest motion it has (heavy to melee, area to bolt...). */
	static const FTMMotionClips* FindMotion(const FTMAnimSet& Set, const FString& Motion);
	/** What a unit shows standing: its wind-up while it charges, its channel while it channels, or idle. */
	UAnimSequence* StandingClip(int32 Index) const;

	/** Reads the character map once per run. False, and said why, if it cannot. */
	bool LoadCharacterMap();
	const FTMBody* BodyFor(const TMSim::FUnit& Unit) const;
	/** Starts every unit standing where the rules put it. */
	void ResetMotion();
	/** Walks, one-offs and falls, a frame at a time. Game worlds only. */
	void AdvanceMotion(float DeltaSeconds);
	/** What the battle's events mean for the bodies: a swing, a flinch. */
	void AnimateEvents(const TMSim::FTickReport& Report);
	void Animate(int32 Index, UAnimSequence* Clip, bool bLoop);
	/** Where a unit is drawn now: part way along a walk, or where the rules have it. */
	FVector ShownAt(const TMSim::FUnit& Unit) const;

	bool bCharacterMapRead = false;
	TMap<FString, FTMAnimSet> AnimSets;
	TMap<FString, FTMBody> Bodies;
	TMap<FString, FString> LookBodies;
	TMap<FString, FString> ClassBodies;
	FString DefaultBody;
	/** Keeps every mesh and clip the map names loaded. */
	UPROPERTY()
	TArray<TObjectPtr<UObject>> CharacterAssets;
	TArray<FTMMotion> Motions;

	/** Left over from the last frame, so the clock runs at its own rate. */
	float TickRemainder = 0.0f;

	bool bBuilt = false;
};
