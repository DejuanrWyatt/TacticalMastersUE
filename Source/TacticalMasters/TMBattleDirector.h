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
#include "TMNet.h"
#include "TMUpnp.h"

#include "TMBattleDirector.generated.h"

class USkeletalMesh;
class USoundBase;
class UAnimSequence;
class USkeletalMeshComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UPointLightComponent;
class UTextRenderComponent;
class ACameraActor;
struct FRandomStream;

/**
 * Times the block it stands in, and says in the log when that took longer
 * than a frame can spare: "SLOW Fog: 23.4 ms". For finding what makes the
 * battle lag (2026-09-30); costs a clock read each way.
 */
struct FTMSlow
{
	const TCHAR* What;
	double Start;
	double LimitMs;
	explicit FTMSlow(const TCHAR* InWhat, double InLimitMs = 4.0)
		: What(InWhat), Start(FPlatformTime::Seconds()), LimitMs(InLimitMs)
	{
	}
	~FTMSlow()
	{
		const double Ms = (FPlatformTime::Seconds() - Start) * 1000.0;
		if (Ms > LimitMs)
		{
			UE_LOG(LogTemp, Log, TEXT("SLOW %s: %.1f ms"), What, Ms);
		}
	}
};
#define TM_SLOW(Name) const FTMSlow ANONYMOUS_VARIABLE(Slow)(TEXT(Name))

/** A cache's chest as it is drawn (TMBattleDirectorChest.cpp): what moves on it. */
USTRUCT()
struct FTMChest
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<USceneComponent> Root = nullptr;

	/** The crystal on its lid, which turns and bobs; none on a common chest. */
	UPROPERTY()
	TObjectPtr<USceneComponent> Crystal = nullptr;

	UPROPERTY()
	TObjectPtr<class UPointLightComponent> Light = nullptr;

	/** The epic chest's rising sparks. */
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Sparks;

	int32 Tier = 0;
	float Phase = 0.0f;
	float LightBase = 0.0f;
};

/** A watchtower as it is drawn (TMBattleDirectorTower.cpp): its fire, which takes the holder's colour. */
USTRUCT()
struct FTMTower
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<USceneComponent> Root = nullptr;

	/** The flames that rise from the bowl, over and over. */
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Flames;

	UPROPERTY()
	TObjectPtr<class UPointLightComponent> Light = nullptr;

	/** The fire's colour, and its hot heart's. */
	UPROPERTY()
	TObjectPtr<class UMaterialInstanceDynamic> Fire = nullptr;
	UPROPERTY()
	TObjectPtr<class UMaterialInstanceDynamic> Core = nullptr;

	/** Where the coals are, from its foot. */
	FVector Bowl = FVector::ZeroVector;
	/** Who it is painted for: 0, 1, -1 nobody, -2 not yet. */
	int32 Holder = -2;
	float Phase = 0.0f;
	float LightBase = 0.0f;
};

/** A moving part of a hazard ground (TMBattleDirectorHazards.cpp): a flame, an ember, a wisp of steam, a mote. */
struct FTMHazardPart
{
	/** In BoardProps too, which keeps it. */
	TWeakObjectPtr<UStaticMeshComponent> Piece;
	/** Where it rests, from the board's root, in centimetres. */
	FVector Home = FVector::ZeroVector;
	uint8 Kind = 0;
	/** 0..1, so no two move together. */
	float Phase = 0.0f;
	/** Across, in metres. */
	float Size = 0.0f;
};

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

	// How it is drawn (2026-10-01, the human's "Combat Text Mockups"): none of it is state.
	/** Times the usual size: a crit half again as big, a tick smaller. */
	float Scale = 1.0f;
	/** Drawn twice, a hair apart: heavier. */
	bool bBold = false;
	/** Pops in at 140% and settles: a critical strike. */
	bool bPop = false;
	/** A pale ember edge and glow, so a burn's dark red still reads on a dark field. */
	bool bEmber = false;
	/** In exactly its colour; the older words (items, sleep, ability names) are lightened. */
	bool bExact = false;
	/** A small word after it, as "graze". */
	FString Tag;
	FColor TagTint = FColor::White;
};

/** How a floater looks, when it is not the plain kind. */
struct FTMFloatLook
{
	float Scale = 1.0f;
	bool bBold = false;
	bool bPop = false;
	bool bEmber = false;
	FString Tag;
	FColor TagTint = FColor::White;
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
	/** How far the Options page is scrolled, in 1080p pixels (the HUD keeps it in range). */
	float OptionsScroll = 0.0f;
	bool bDevToolsOpen = false;
	/** The action waiting for its new key, or -1. */
	int32 CaptureAction = -1;
	/** The slider being dragged, or -1. 0 is the camera speed; 100 + i is rule number i. */
	int32 DragSlider = -1;
	static constexpr int32 SliderCameraSpeed = 0;
	static constexpr int32 SliderOverhead = 1;
	static constexpr int32 SliderStatusIcons = 2;
	static constexpr int32 SliderSfxVolume = 3;
	static constexpr int32 SliderVoiceVolume = 4;
	static constexpr int32 SliderDamageText = 5;
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
	/**
	 * A height step as drawn, in centimetres: taller than the rules' 0.7 m
	 * (ViewLevelScale), so hills and cliffs read from the camera's height. Only
	 * the look: the rules count steps, never centimetres.
	 */
	static constexpr float ViewLevelScale = 1.8f;
	float LevelCm() const { return TMSim::Ground::LevelHeight * ViewLevelScale * TileSize; }

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

	enum class EScreen : uint8 { Title, Setup, Battle, Online, Lobby, Draft, Replays };

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
		/**
		 * How many watchtowers the battle starts with, placed at random in
		 * mirrored pairs by the rules (Docs/design/feat-objectives.md). Not
		 * Godot's; 0 is none, which is Godot's battle, and is what a battle
		 * nobody set up gets. The setup screen offers 2 the first time it opens.
		 */
		int32 Watchtowers = 0;
		/**
		 * Points each side may spend on items, and what each unit carries
		 * (item ids, "" for an empty slot), by side, unit and slot
		 * (Docs/design/feat-neutral-camps.md). 0 points is none, Godot's battle;
		 * the setup screen offers 6 the first time it opens. A side the computer
		 * plays picks its own.
		 */
		int32 ItemBudget = 0;
		std::string Items[2][4][3];
		/**
		 * Neutral camps (Docs/design/feat-neutral-camps.md): 0 off, 1 light,
		 * 2 standard, 3 wild; and whether the boss is drawn at random rather
		 * than the map's own. 0 is Godot's battle; the setup screen offers
		 * standard the first time it opens.
		 */
		int32 CampLevel = 0;
		bool bRandomBoss = false;
		/** Element hits leave their mark (Docs/design/feat-status-effects.md): off in Godot's battle, offered on. */
		bool bElements = false;
		/** Area blows hurt their caster's own side too (FTuning::FriendlyFire): off unless chosen. */
		bool bFriendlyFire = false;
		/** Cleared camps wake again later (FTuning::CampRespawn): off by default since 2026-10-01. */
		bool bCampRespawn = false;
		uint64 FixedSeed = 12345;
		/** The map, by id (TMSim::FindMap), and the look it is dressed in: empty for the map's own. */
		std::string MapId = "highlands";
		FString ThemeId;
		/**
		 * Online: the classes are drafted, bans and all, rather than chosen in the
		 * lobby (TMBattleDirectorDraft.cpp); and seconds for each choice, 0 for no
		 * clock. Set by the host in the lobby.
		 */
		bool bDraft = false;
		int32 DraftSeconds = 30;
	};

	FMatchSetup Setup;
	/** Whether the setup screen has offered its first watchtowers yet (OpenSetup). */
	bool bOfferedTowers = false;
	/** Likewise its first item points, and its first camps. */
	bool bOfferedItems = false;
	bool bOfferedCamps = false;
	bool bOfferedElements = false;
	/** The last setup on this machine has been put back (OpenSetup; FTMSettings::LastSetup). */
	bool bRestoredSetup = false;
	/** The setup screen's choices into the settings file, and back. */
	void SaveSetupChoices();
	void RestoreSetupChoices();
	/** Points a side has spent on items in the setup. */
	int32 ItemPointsSpent(int32 Team) const;
	/** Whether this side picks its own items: the computer does. */
	bool PicksOwnItems(int32 Team) const;
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

	// ------------------------------------------------ online (TMBattleDirectorOnline.cpp)
	// Two players, each on their own machine (Docs/design/feat-online.md). The
	// host plays blue and is the referee; the joiner plays red.

	/** A match against another machine is being played (or has just ended). */
	bool bOnline = false;
	/** This machine hosts: it moves time and checks the joiner's orders. */
	bool bOnlineHost = false;
	/** The side this machine plays online. */
	int32 LocalTeam = 0;
	/** The joiner has an order out with the host, and gives no other until it is answered (battle.gd:295). */
	bool bWaitingForHost = false;
	/** Set when an online match can't go on -- out of sync, the other player gone -- and why. */
	FString OnlineStopped;
	/** What the online and setup screens say is happening: hosting, connecting, refused. */
	FString OnlineStatus;
	/** The host's addresses on this network, and what the router said about the port. */
	FString OnlineAddresses;
	FString OnlineRouter;
	/** This player and the other have asked for a rematch (net.gd:139-143). */
	bool bWantRematch = false;
	bool bOpponentWantsRematch = false;
	/** Typed fields: the host to join, the port, and a chat line. */
	enum class ETypeField : uint8 { None, Address, Port, Chat };
	ETypeField Typing = ETypeField::None;
	FString JoinAddress;
	FString JoinPort = TEXT("7777");
	FString ChatLine;

	/** The online screen: host or join. */
	void OpenOnline();
	/** Starts listening, with the setup on screen as the battle to play. */
	void HostOnline();
	/** Starts connecting to JoinAddress:JoinPort. */
	void JoinOnline();
	/** Leaves: closes the connection and forgets the match. */
	void LeaveOnline();
	void RequestRematch();
	void StartTyping(ETypeField Field);
	bool IsTyping() const;
	/** A hosted or joined match is waiting for its opponent, or for the host's battle. */
	bool IsWaitingOnline() const { return Net.IsValid() && !bOnline; }
	/** One of the buttons on the title, setup or in-battle menu. */
	void PressMenuButton(const struct FTMHudButton& Button);
	/** The setup slot a class is being picked for (team * 4 + slot), or -1 (class_picker.gd). */
	int32 PickerSlot = -1;
	/** The role the picker shows, as an index into tank, damage, support, special, or -1 for every class. */
	int32 PickerRole = -1;
	/** The item slot being filled (team * 12 + unit * 3 + slot), or -1; and the tier the item picker shows (-1 all). */
	int32 ItemPickerSlot = -1;
	int32 ItemPickerTier = -1;
	/** Rows of the item picker's grid scrolled past (the mouse wheel); the HUD keeps it in range. */
	int32 ItemPickerScroll = 0;
	/** Puts an item in a setup slot, or says why not. */
	void ChooseItem(int32 SlotCode, int32 ItemIndex);
	/** The Unit Guide's page: 0 the classes, 1 the items. */
	int32 GuideTab = 0;

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

	/** The rule numbers a battle in the game starts from: the rules' own, with the game's newer rules turned on. */
	static TMSim::FTuning GameTuning();

	/** Sends an order for the selected unit and takes the next step after it. */
	void OrderSelected(const TMSim::FOrder& Order);

	/** Keeps the selection honest as time runs: turns end, units fall. */
	void MaintainSelection();
	/** A unit of the player's used its turn: the camera goes to the next one ready, when there is one. */
	bool bSnapToNext = false;

	/** Works out what is under the mouse: a spot on the board, and a unit. */
	void PickUnderCursor();

	/**
	 * While an ability is being aimed, keeps the aim inside what could legally
	 * be ordered: within its range of where the unit stands, or of anywhere it
	 * can still walk this turn (a walk into range). Pointing further away pulls
	 * the aim back to the nearest legal spot, so the reticle cannot be dragged
	 * across the board. Global abilities and those centred on the user are left
	 * alone. Only the aim moves; the mouse pointer stays free for the HUD.
	 */
	void TetherAim();
	/** Where the aimed ability could be used from, and the board it was worked out for. */
	std::vector<TMSim::FVec2> TetherFrom;
	FString TetherKey;

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

	/**
	 * An ability ordered from out of range: the unit walks to the nearest spot
	 * it can be used from, and it goes off on arrival (battle.gd:109-113,
	 * 838-892, _walk_into_range).
	 */
	struct FPendingAbility
	{
		int32 UnitId = -1;
		int32 Serial = -1;
		int32 Slot = -1;
		TMSim::FVec2 Target;
		/** A unit to aim at where it stands when the walk ends (a plan's), or -1 for Target. */
		int32 Follow = -1;
		/** Not before the walk ordered with it has been applied (online: the host's answer). */
		bool bAfterWalk = false;
	};
	FPendingAbility PendingAbility;
	/** The reachable spot this ability could be used from with the shortest walk; false if none. */
	bool ClosestSpotInRange(const TMSim::FUnit& Unit, int32 Slot, const TMSim::FVec2& Point, TMSim::FVec2& OutSpot, double& OutWalk);
	bool WalkIntoRange(const TMSim::FUnit& Unit, const TMSim::FVec2& Point);
	/** Uses the ability the unit walked over for, once its walk has ended on screen. */
	void FirePendingAbility();
	static constexpr const char* OutOfRange = "That target is out of range.";

	// ---------------------------------------------- queued orders (2026-10-01)
	// (TMBattleDirectorPlans.cpp; Docs/design/feat-move-queue.md) A plan is a
	// walk, by waypoints if it has any, and an ability aimed from where that
	// walk ends. Made for one of the player's units while it waits, it is
	// carried out the moment its turn begins; made inside a turn (the plan key),
	// on Go. It is this machine's alone: what leaves it is the same orders a
	// person clicking would give, checked by the rules at that moment.
	struct FTMPlan
	{
		/** The unit's serial when it was planned: a plan runs on a later one, or on Go. */
		int32 Serial = -1;
		/** Made inside the unit's turn: for that turn's Go only, dropped if the turn ends without it. */
		bool bThisTurn = false;
		bool bWalk = false;
		bool bSprint = false;
		std::vector<TMSim::FVec2> Via;
		TMSim::FVec2 To;
		/** The way drawn on the ground, and its length. */
		std::vector<TMSim::FVec2> Path;
		double Metres = 0.0;
		int32 Slot = -1;
		TMSim::FVec2 Target;
		int32 Follow = -1;
		bool HasAbility() const { return Slot >= 0; }
		bool IsEmpty() const { return !bWalk && Slot < 0; }
	};
	TMap<int32, FTMPlan> Plans;
	/** The selected unit is being planned, not ordered. */
	bool bPlanMode = false;
	/** Waypoints clicked so far for the walk being aimed, planned or not. */
	std::vector<TMSim::FVec2> WayPoints;
	/** Ready turns already noticed, as id and serial, so a new one is noticed once. */
	TSet<int64> ReadySeen;

	/** Whether this machine may plan this unit: one of its own, alive, played by a person. */
	bool PlayerCanPlan(const TMSim::FUnit* Unit) const;
	/** Whether the selected unit takes the aim keys and board clicks: ordered, or planned. */
	bool PlayerCanCommand(const TMSim::FUnit* Unit) const;
	bool IsPlanningSelected() const;
	const FTMPlan* PlanOf(int32 UnitId) const { return Plans.Find(UnitId); }
	void StartPlanning(int32 UnitId);
	void StopPlanning();
	/** The plan key: plan the selected ready unit's turn; with a plan, go; planning a waiting unit, done. */
	void PlanKey();
	/** The plan's last step back: a waypoint, then its ability, then its walk. */
	void UndoPlanStep();
	void ClearPlan(int32 UnitId);
	void PlanWalk(const TMSim::FVec2& To);
	void PlanAbility(int32 Slot, const TMSim::FVec2& Target, int32 Follow);
	/** Out of range while planning: the walk to the nearest spot it can be used from, then it. */
	bool PlanWalkIntoRange(const TMSim::FUnit& Unit, const TMSim::FVec2& Point);
	/** Carries a plan out now. False, and the plan dropped with a word why, if the rules refuse it. */
	bool RunPlan(int32 UnitId);
	/** Each frame: a plan whose unit's turn has begun runs. */
	void RunDuePlans();
	/** Whether a key held for waypoints is down. */
	bool WayPointHeld() const;
	/** Adds a waypoint to the walk being aimed: false, with a word why, if it can't be. */
	bool AddWayPoint(const TMSim::FVec2& Point);
	/** The walk area for the walk being aimed: on from the last waypoint, with what is left. */
	void RefreshReachable();
	/** Where the walk being aimed starts from: the unit, or its last waypoint. */
	TMSim::FVec2 WalkStart(const TMSim::FUnit& Unit) const;
	/** A unit's name for a word on screen: its class, as the log has it. */
	FString PlanName(const TMSim::FUnit& Unit) const;

	// ------------------------------------------------- Go To (2026-10-01)
	// (TMBattleDirectorPlans.cpp; Docs/design/feat-move-queue.md, "Go To") A
	// click beyond the walk area sends the unit there over as many turns as it
	// takes, as Civilization III's Go To does: each turn it walks as far as its
	// move allows along the way, then ends the turn (or, asked to, hands it
	// over), and the turn it arrives on is the player's. It stops, and hands
	// the turn back, when an enemy comes into sight, when it has been hurt, or
	// when there is no way left. This machine's only: each step is an ordinary
	// walk, sent and checked as one.
	struct FTMGoTo
	{
		TMSim::FVec2 Dest;
		/** Waypoints still to pass, in order. */
		std::vector<TMSim::FVec2> Via;
		/** Walk, then the turn is the player's; false: walk and end the turn. */
		bool bWaitForMe = false;
		/** The turn it last walked on: one walk a turn. */
		int32 LastSerial = -1;
		/** Its health, and the enemies in sight, when it last walked or was told to go. */
		int32 Hp = 0;
		TSet<int32> Seen;
		/** Stopped on its turn StoppedSerial, and why: the plan key carries on, anything else ends it. */
		bool bStopped = false;
		int32 StoppedSerial = -1;
		FString Why;
		/** The way left, and where each turn's walk will end (the last is Dest). */
		std::vector<TMSim::FVec2> Route;
		std::vector<TMSim::FVec2> Stops;
	};
	TMap<int32, FTMGoTo> GoTos;
	/** A walk-and-end step: the turn is ended once its walk has been applied. */
	int32 GoToEndUnit = -1;
	int32 GoToEndSerial = -1;
	/** Set while the Go To gives its own orders, which don't cancel it as a person's do. */
	bool bGoToOrdering = false;
	/** The way to the spot under the pointer, past the walk area: where each turn ends, and its length. */
	std::vector<TMSim::FVec2> GoToHoverStops;
	double GoToHoverMetres = 0.0;

	const FTMGoTo* GoToOf(int32 UnitId) const { return GoTos.Find(UnitId); }
	/** On its way and needing nobody: not stopped, walking and ending its turns. */
	bool IsMarching(int32 UnitId) const { const FTMGoTo* G = GoTos.Find(UnitId); return G && !G->bStopped && !G->bWaitForMe; }
	/** Sends the unit there; this turn's walk at once if it can. False, with a word why, if there's no way. */
	bool SetGoTo(int32 UnitId, const TMSim::FVec2& Dest);
	/** This turn's walk along the way, unless something stopped it (bResume: it was told to carry on). */
	bool StepGoTo(int32 UnitId, bool bResume);
	void StopGoTo(int32 UnitId, const FString& Why);
	void CancelGoTo(int32 UnitId, bool bTell);
	/** Each frame: the turn a walk-and-end step is ending, and Go Tos whose units' turns have begun. */
	void RunDueGoTos();
	/** The other side's units this side can see now. */
	TSet<int32> EnemiesInSight(const TMSim::FUnit& Unit) const;
	/** Where each turn's walk ends along the way, with the unit's move; the last is the end of the way. */
	std::vector<TMSim::FVec2> SplitRoute(const TMSim::FUnit& Unit, const std::vector<TMSim::FVec2>& Route) const;

	/**
	 * While the selected unit is being planned, it stands for the length of
	 * this where its planned walk ends and as at the start of a turn (ready,
	 * cooldowns a turn on), so aiming, ranges, previews and the ability buttons
	 * read as they will then. Put back exactly as it was when this goes out of
	 * scope; nothing the rules keep is touched in between. Nests.
	 */
	class FPlanStandIn
	{
	public:
		explicit FPlanStandIn(ATMBattleDirector& Director);
		~FPlanStandIn();
		FPlanStandIn(const FPlanStandIn&) = delete;
		FPlanStandIn& operator=(const FPlanStandIn&) = delete;
	private:
		TMSim::FUnit* Unit = nullptr;
		TMSim::FVec2 Pos;
		bool bReady = false;
		bool bMoved = false;
		bool bActed = false;
		int Cooldowns[TMSim::AbilitySlots] = {};
	};

	// ------------------------------------------------ indicators on the ground
	// (TMBattleDirectorIndicators.cpp): the walk area and ability shapes painted
	// into a picture that a decal lays on the board, under the units.
	void BuildIndicators();
	void AdvanceIndicators();
	void PaintMoveArea(void* Painter, const TMSim::FUnit& Unit);
	void PaintAbility(void* Painter, const TMSim::FUnit& Unit, const FAim& Where);
	/** The plans of this machine's units (dashed walks, ghosts, aims), and the waypoints being set. */
	void PaintPlans(void* Painter);
	/** What PaintPlans would paint, in a line, so the ground is painted again only when it changes. */
	FString PlanSignature() const;
	/** Whether the decal is up; without its material the HUD draws outlines instead. */
	bool bIndicatorDecal = false;
	/** Turns the picture on the board if a decal's axes come out otherwise. */
	float IndicatorRoll = 0.0f;
	FString IndicatorSignature;
	/** The walk area's edge, in metres, as painted: the HUD draws it again over
	 *  the scenery, which can stand over the ground and hide the decal. */
	TArray<TPair<FVector2D, FVector2D>> MoveEdgeMetres;
	UPROPERTY()
	TObjectPtr<class UDecalComponent> IndicatorDecal = nullptr;
	UPROPERTY()
	TObjectPtr<class UTextureRenderTarget2D> IndicatorFilm = nullptr;

	// ------------------------------------------------ fog of war on the ground
	// (TMBattleDirectorFog.cpp): what this screen's side sees now is clear and
	// edged with a line; ground it has seen before is greyed; ground it has
	// never seen is dark, its shape still there but nothing on it shown.
	void BuildFog();
	void AdvanceFog();
	/** The board's cells (FogCell metres across) this side can see now, and has ever seen. */
	TArray<uint8> FogSeen;
	TArray<uint8> FogExplored;
	int32 FogCellsX = 0;
	int32 FogCellsY = 0;
	/** The side the explored cells belong to; a change of side starts them again. */
	int32 FogViewer = -2;
	FString FogSignature;
	bool bFogDecal = false;
	/** The fog drawn over everything by a post-process (M_FogOfWar), not the decal. */
	bool bFogPost = false;
	/** The board can be built before the camera is: the post-process is tried again once it is. */
	bool bFogPostRetried = false;
	UPROPERTY()
	TObjectPtr<class UMaterialInstanceDynamic> FogPost = nullptr;
	UPROPERTY()
	TObjectPtr<class UDecalComponent> FogDecal = nullptr;
	UPROPERTY()
	TObjectPtr<class UTextureRenderTarget2D> FogFilm = nullptr;
	/**
	 * Films replaced when a battle on a board of another size was built: kept
	 * a while, never resized in place. Resizing a render target that a decal or
	 * the post-process is drawing with left the renderer reading a texture that
	 * was gone, and crashed it (2026-10-01, SetShaderParameters, both DX11 and DX12).
	 */
	UPROPERTY()
	TArray<TObjectPtr<class UTextureRenderTarget2D>> RetiredFilms;
	/** A film of this size: the same one when it already is, else a new one (the old retired). */
	class UTextureRenderTarget2D* FilmOfSize(class UTextureRenderTarget2D* Film, int32 W, int32 H, bool bClamp);
	/** Things on the board that stay hidden until their cell has been seen: rocks, hazards, towers. */
	TArray<TWeakObjectPtr<class USceneComponent>> FogProps;
	TArray<int32> FogPropCell;
	/** Marks the board pieces made since BoardProps held From as hidden until the spot (in centimetres) is seen. */
	void MarkFogged(int32 From, const FVector& Where);
	/** The fog cell a spot on the board falls in, or -1 off it. */
	int32 FogCellOf(const TMSim::FVec2& Point) const;

	/** Works out the way to the spot under the pointer, only when that spot changes. */
	void UpdateHoverPath();

	/** A click that landed on the HUD rather than the board. */
	void PressHudButton(const struct FTMHudButton& Button);

	/** Puts the battle HUD up for this machine's player. */
	void ShowHud();

	/** The HUD reads the selection, the aim and the log straight from here. */
	friend class ATMBattleHud;
	friend class ATMRobotPlayer;
	friend class ATMAnimStudio;

	/** Something the person should read: why an order was refused, mostly. */
	void Tell(const FString& What);

	/**
	 * The selected unit spends its turn at the nearest watchtower within reach,
	 * or is told why it cannot. What the Capture button does.
	 */
	void CaptureTower();
	/** The watchtower the selected unit could capture now, or -1; and why not, if not. */
	int32 CapturableTower(const TMSim::FUnit& Unit, FString* WhyNot = nullptr) const;
	/** The watchtower under the pointer (its index), or -1 (TMBattleDirectorIndicators.cpp). */
	int32 HoveredTower() const;
	/** The towers' roofs in the colour of whoever holds each. */
	void RefreshTowers();

	// ------------------------------------------------ neutral camps (TMBattleDirectorCamps.cpp)

	/** Each camp's marker on the ground, its label, and each cache's chest. */
	void BuildCamps();
	/** Markers, countdowns, temperament signs and chests, as the rules and the fog have them. */
	void RefreshCamps(float DeltaSeconds);
	/** Which camps each side has seen the spot of: their tier shows only then. Bit per side. */
	TArray<uint8> CampSeen;
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> CampRings;
	UPROPERTY()
	TArray<TObjectPtr<class UTextRenderComponent>> CampLabels;
	UPROPERTY()
	TArray<TObjectPtr<USceneComponent>> CacheChests;
	/** What moves on each chest; the chest itself is in CacheChests. */
	UPROPERTY()
	TArray<FTMChest> Chests;
	float ChestClock = 0.0f;
	/** A Runic Coffer at Foot, in the look of the tier of the best item it holds (TMBattleDirectorChest.cpp). */
	USceneComponent* MakeChest(const FVector& Foot, int32 Tier, float Yaw, int32 Seed);
	/** The chests' crystals turn, their lights breathe, the epic one's sparks rise. */
	void AdvanceChests(float DeltaSeconds);
	/** Each watchtower's beacon; and the clock its flames dance to. */
	UPROPERTY()
	TArray<FTMTower> Beacons;
	float TowerClock = 0.0f;
	/** Frames over the last ten seconds, for the log's FRAMES line: how many, how long the worst. */
	float FrameWindow = 0.0f;
	int32 FrameCount = 0;
	float FrameWorst = 0.0f;
	/** A Signal Beacon at Foot (TMBattleDirectorTower.cpp), its fire unlit until painted. */
	void MakeTower(const FVector& Foot, int32 Seed);
	/** The fire in the holder's colour, or embers for nobody. */
	void PaintTower(int32 Index, int32 Holder);
	/** The flames rise and sway, the light flickers. */
	void AdvanceTowers(float DeltaSeconds);
	/** An unlit colour, for what glows (the chests' runes and crystals). */
	class UMaterialInstanceDynamic* GlowPaint(const FLinearColor& Colour);
	/** The tier each camp's ring is painted, so it is painted only on a change (-2 unpainted). */
	TArray<int32> CampRingTier;
	/** The cache the selected unit could take from now, or -1; and why not, if not. */
	int32 TakeableCache(const TMSim::FUnit& Unit, FString* WhyNot = nullptr) const;
	/** Picks up an item lying within the selected unit's reach into its side's stash: Code is its place in the cache. */
	void TakeItem(int32 Code);
	/** Takes a worn item off into the stash, spending that unit's turn: Code is unit id x 4 + slot. */
	void DropItem(int32 Code);
	/** Puts the stash item picked (StashPick) on a unit: Code is unit id x 4 + slot. */
	void EquipItem(int32 Code);
	/** The side whose items the team items screen shows and manages here. */
	int32 ItemsTeam() const;
	/** Whether this machine may put items on that side's units (its own side; offline, a side a person plays). */
	bool MayManageItems(int32 Team) const;
	/**
	 * The team items screen (2026-10-01): every unit of the side, what each
	 * wears and its open slots, and the side's stash, in one place. The
	 * stash item picked to equip next, by its place in the stash, or -1.
	 */
	bool bTeamItemsOpen = false;
	int32 StashPick = -1;
	/** Kept for the old items panel's callers: -1 closed. Unused now. */
	int32 TakePickerCache = -1;
	/** A monster's name as the view says it, with what it is doing. */
	FString MonsterLine(const TMSim::FUnit& Unit) const;
	/** Units that moved without walking (a camp waking, a blink): shown there at once. */
	TSet<int32> SnapUnits;

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
	/** The health and status bars over the units: shown, or hidden with Tab (ETMAction::StatusBars). */
	bool bShowStatusBars = true;
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
	/** Showing one class's page rather than the list of every class. */
	bool bGuideDetail = false;
	/** The list's first row shown, how many rows fit (set by the HUD, for paging), and its role filter (-1: all). */
	int32 GuideListScroll = 0;
	int32 GuideListPage = 10;
	int32 GuideRole = -1;
	/** How far a class's page is scrolled down, in design pixels (the HUD clamps it). */
	float GuideDetailScroll = 0.0f;
	/** The classes the list shows, in order, with its role filter. */
	TArray<int32> GuideShown() const;
	/** Back to the list, or on to the next or previous class in it. */
	void GuideStep(int32 By);

	UPROPERTY()
	TObjectPtr<ACameraActor> Watcher = nullptr;

	float NextCaptureAt = 0.0f;
	int32 Captured = 0;

	/** One unit's light flaring after it was struck. */
	struct FFlash
	{
		int32 UnitId = -1;
		float Age = 0.0f;
		/** Red for a wound, blue for a soak, green for healing, gold for a return. */
		FLinearColor Colour = FLinearColor(1.0f, 0.25f, 0.2f);
	};

	UPROPERTY()
	TArray<FTMFloater> Floaters;

	/**
	 * One of this side's units that just took or dealt damage, or was healed: its
	 * health bar shows over its head for HpPopSeconds, the part it lost draining
	 * away, flashing, over HpDrainSeconds (2026-10-01, "Combat Text Mockups" B and C).
	 * Kept in health as shown, which can trail the rules while a blow is in flight.
	 */
	struct FTMHpPop
	{
		double ShownAt = 0.0;
		int32 Hp = 0;
		int32 TrailFrom = 0;
		double TrailAt = -1.0;
		int32 HealFrom = 0;
		double HealAt = -1.0;
	};
	TMap<int32, FTMHpPop> HpPops;
	static constexpr double HpPopSeconds = 3.0;
	static constexpr double HpDrainSeconds = 2.0;
	static constexpr double HpHealGlowSeconds = 0.9;
	/** Real seconds, so slowed time on a big hit does not hold the bar up. */
	double PopClock() const;
	/** Its health bar is up: its lost part draining, or just shown. Change < 0 lost, > 0 gained, 0 dealt a blow. */
	void PopHealth(int32 UnitId, int32 Change);
	/** Where the drained part has got to, in health. */
	float PopTrail(const FTMHpPop& Pop, double Now) const;
	/** A Critical or Grazed waits for the Hit after it: 1 critical, 2 graze. */
	TMap<int32, uint8> BlowMarks;

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

	/**
	 * The combat log as the HUD draws it (2026-10-01, the human chose design A
	 * with C's tabs): one entry per line, an action followed by what it did to
	 * each unit, so names can wear their side's colour and numbers their own.
	 * Log above keeps the same fight as plain text, for BattleLog() and the tests.
	 */
	struct FTMLogEntry
	{
		enum class EKind : uint8
		{
			/** A unit's action: Unit uses What. */
			Action,
			/** What an action did to one unit (Unit): Amount, Tags. */
			Result,
			/** A unit knocked out. */
			Ko,
			/** Anything else worth a line: Unit (or none) and Text. */
			Note,
			/** Turns coming round, gauges: shown only on the All tab. */
			Turn,
			/** Not about a unit: chat, players, the battle's start and end. */
			System,
		};
		EKind Kind = EKind::Note;
		float Seconds = 0.0f;
		int32 Unit = -1;
		/** Which action it belongs to: an Action starts one, its Results share it. */
		int32 Group = 0;
		FString Verb;
		FString What;
		/** Note and System: the whole line, after the unit's name if there is one. */
		FString Text;
		FString Amount;
		/** 0 plain, 1 harm, 2 healing, 3 critical harm, 4 dim (a miss). */
		uint8 Tone = 0;
		TArray<FString> Tags;
		/** A moment the Key tab keeps: a knockout, a crit, a capture, a camp, an item. */
		bool bKey = false;
	};
	TArray<FTMLogEntry> LogEntries;
	int32 LogGroup = 0;
	/** The log's tab: 0 All, 1 Combat, 2 Mine, 3 Key moments. */
	int32 LogTab = 1;
	/** A line for the log that isn't a unit's doing: chat, players coming and going. */
	void LogNote(const FString& Line);
	/** One event of the rules as the log's entries (Narrate has written its plain line). */
	void LogEvent(const TMSim::FEvent& Event, const FString& Plain);
	/** A unit's name as the log shows it: its class, numbered only when its side has two of one. */
	FString LogName(int32 UnitId) const;

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
	/** The neutral monsters' player (team 2): on the host, or offline. */
	TMSim::FNeutralPlayer Monsters;
	/** Seconds the computer waits before a unit's first order and between its orders. */
	float ThinkSeconds(int32 Team, bool bFirst) const;

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
		/** How far into the release the blow lands, as a share of the clip; below zero, the motion's usual. */
		float Impact = -1.0f;
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
		/**
		 * Whether the hero's cloth is simulated ("cloth" in the map, true unless
		 * said). Terra's flies apart -- her shield panels thrown out as long
		 * slabs -- when the game switches clips, so hers is drawn skinned.
		 */
		bool bCloth = true;
		/** How fast a walk and a run cover the ground, in cm/s, to match the clips. */
		float WalkSpeed = 170.0f;
		float RunSpeed = 380.0f;
		/**
		 * The clips a set may have for reactions, statuses and moments --
		 * hitFront, evade, deathBack, stunned, victory and the rest (ExtraKeys) --
		 * and "idle:<motion>" for an idle that suits a class whose first ability
		 * has that motion. Every one is optional.
		 */
		TMap<FString, TArray<UAnimSequence*>> Extras;
		/** One of the named extras, or null; with several, Pick chooses among them. */
		UAnimSequence* Extra(const TCHAR* Key, int32 Pick = 0) const
		{
			const TArray<UAnimSequence*>* Found = Extras.Find(Key);
			return Found && Found->Num() > 0 ? (*Found)[FMath::Abs(Pick) % Found->Num()] : nullptr;
		}
	};

	/** A body: a mesh, which way it faces, and the set it animates with. */
	struct FTMBody
	{
		/**
		 * Loaded the first time a unit wears it (MeshOf), not when the map is
		 * read: with every hero's skins in the map, most bodies go unworn in
		 * any one battle, and a hero's mesh brings its textures with it.
		 */
		mutable USkeletalMesh* Mesh = nullptr;
		FString MeshPath;
		float Yaw = 0.0f;
		/** Its clips: loaded with the mesh, the first time a unit wears it (MeshOf). */
		mutable const FTMAnimSet* Animations = nullptr;
		/** The animation set's name; a skin is a body named "<set>_<skin>". */
		FString SetName;
		/** How big it is drawn: a hero built as a giant is brought nearer everyone else's size. */
		float Scale = 1.0f;
	};
	/** The body's mesh, and its animation set, loaded now if they have not been yet. */
	USkeletalMesh* MeshOf(const FTMBody& Body);
	/** An animation set by name, its clips loaded the first time it is asked for. */
	const FTMAnimSet* SetOf(const FString& Name);
	/** Puts a unit's mesh into the body: its mesh, its size, and whether its cloth moves. */
	void WearBody(class USkeletalMeshComponent* Visual, const FTMBody& Body, bool bNoCloth = false) const;
	/** When the guide's hero last changed body, in real seconds: changes come at most every quarter second. */
	double ShowcaseSwappedAt = -1.0;
	/**
	 * Fullscreen, switched from Options: applied from Tick, at most once a
	 * second, never in the middle of a click (2026-10-01: clicking it fast
	 * crashed the renderer).
	 */
	bool bFullscreenPending = false;
	float FullscreenCooldown = 0.0f;
	/** Whether the body's mesh and clips are already in memory, so wearing it costs nothing. */
	bool IsBodyLoaded(const FTMBody& Body) const;

	// ------------------------------------------------ loading in the background
	// (TMBattleDirectorLoading.cpp). A hero's mesh, textures and clips take
	// seconds to read; read on the game thread they froze the screen, one hero
	// after another. They are read by Unreal's loader in the background instead,
	// all at once, with a bar saying how far along it is.
	/** Starts the bodies the battle's units wear loading; true if any had to be. */
	bool LoadBodiesInBackground();
	/** What wearing these bodies would still have to read from disk. */
	void UnreadPaths(const TArray<const FTMBody*>& Wanted, TArray<struct FSoftObjectPath>& Paths) const;
	/** Every effect the battle may play: the look table's and each unit's abilities' own (TMBattleDirectorAbilityFx.cpp). */
	void LookAssetPaths(TArray<struct FSoftObjectPath>& Paths) const;
	/** Every sound the sound map names (Content/Data/Sounds/sounds.json). */
	void SoundAssetPaths(TArray<struct FSoftObjectPath>& Paths) const;
	/** Once they are in: every unit put into its own body. */
	void DressUnits();
	/** A random team rolled ahead of the click, its heroes loaded while the setup screen is read. */
	void RollNextRandomTeam(int32 Team);
	/** What is still loading, for the HUD: a line to say and how far along. False when nothing is. */
	bool LoadingProgress(FString& What, float& Fraction) const;
	/** The bodies for the battle, while they load; the next random teams' heroes, likewise. */
	TSharedPtr<struct FStreamableHandle> BodyLoad;

	// The Unit Guide's turntable (TMBattleDirectorShowcase.cpp): a class's hero on
	// its own, far above the board and seen only by its camera, turning slowly.
public:
	/** The hero filmed this frame, or null: bOutLoading while it is still being read. */
	class UTextureRenderTarget2D* GuideModel(const std::string& JobId, bool& bOutLoading);
	/** Turns the hero by hand, on top of its own slow turn. */
	void GuideModelTurn(float Degrees);
	/** Puts it away when the guide or its page closes. */
	void HideGuideModel();
private:
	UPROPERTY()
	TObjectPtr<USkeletalMeshComponent> ShowcaseBody = nullptr;
	UPROPERTY()
	TObjectPtr<class USceneCaptureComponent2D> ShowcaseCamera = nullptr;
	UPROPERTY()
	TObjectPtr<class UTextureRenderTarget2D> ShowcaseFilm = nullptr;
	UPROPERTY()
	TObjectPtr<class UPointLightComponent> ShowcaseLight = nullptr;
	/** The class it is dressed as, and the hero being read for it. */
	FString ShowcaseJob;
	FString ShowcaseLoadFor;
	TSharedPtr<struct FStreamableHandle> ShowcaseLoad;
	/** How far round it has turned, how tall it stands, and when it was last filmed. */
	float ShowcaseYaw = 0.0f;
	float ShowcaseHeight = 190.0f;
	double ShowcaseLastTime = -1.0;
	TSharedPtr<struct FStreamableHandle> AheadLoad[2];
	std::vector<std::string> NextRandom[2];
	/** The most shaders seen waiting at once, so the bar can say how far along they are. */
	mutable int32 ShaderJobsPeak = 0;
	mutable int32 PipelinesPeak = 0;

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
		/**
		 * How far over it has tipped, 0 to 1, when its body has no clip for a
		 * fall (Muriel flies, and never needed one): it topples instead.
		 */
		float Tipped = 0.0f;
		/** Seconds since its last footstep (SoundStep). */
		float StepClock = 0.0f;
		/** Its ability was charged before it went off, so the charged release plays. */
		bool bWasCasting = false;
		/** The release it last began, or will begin when its walk ends. */
		UAnimSequence* LastRelease = nullptr;
		/**
		 * Blows on their way that will knock it down or raise it. Until they
		 * land it stays as it was, so nobody falls before the arrow reaches them.
		 */
		int32 HeldBlows = 0;
		/** The fall chosen for it by the blow that felled it: backwards when struck from the front. */
		UAnimSequence* DeathClip = nullptr;
		/** The last blow landing was a critical one, for a heavier reaction. */
		bool bCritPending = false;
		/** A shove from a blow or a dodge: which way, how far, and how long ago. */
		FVector JoltDir = FVector::ZeroVector;
		float JoltSize = 0.0f;
		float JoltAge = -1.0f;
		/** Seconds on screen, for anything that bobs or pulses. */
		float Clock = 0.0f;
		/** A burning unit winces now and then. */
		float NextWince = 0.0f;
		/** How fast its clips play: slowed, or frozen still. */
		float PlayRate = 1.0f;
	};

	// ------------------------------------------------ how blows land
	// (TMBattleDirectorBlows.cpp). The rules settle a whole ability in an
	// instant; the view spreads it out. The numbers, the flinches and the falls
	// wait for the swing to connect, or for the arrow or bolt to arrive.

	/** Something thrown: an arrow, a bolt of magic, a stone. */
	struct FTMShot
	{
		TWeakObjectPtr<UStaticMeshComponent> Mesh;
		TWeakObjectPtr<class UPointLightComponent> Glow;
		/** The particle it flies as, when its look has one (TMBattleDirectorAbilityFx.cpp). */
		TWeakObjectPtr<class UFXSystemComponent> Trail;
		FVector From = FVector::ZeroVector;
		FVector To = FVector::ZeroVector;
		float Flight = 0.3f;
		float Arc = 0.0f;
		float Age = 0.0f;
	};

	/** One ability that went off, and everything it did, waiting to land. */
	struct FTMBlow
	{
		int32 Caster = -1;
		const TMSim::FAbility* Ability = nullptr;
		FString Motion;
		/** What it did, in the order the rules said, shown when it lands. */
		TArray<TMSim::FEvent> Events;
		TMSim::FVec2 Aim;
		/** The release it waits on, and how long after that begins it connects. */
		UAnimSequence* Release = nullptr;
		float ImpactAt = 0.0f;
		bool bStarted = false;
		bool bLaunched = false;
		float Since = 0.0f;
		float Age = 0.0f;
		TArray<FTMShot> Shots;
		/** Units whose fall or rise waits for this. */
		TArray<int32> Held;
		/** Its slot, and whether its launch has been heard (SoundBlowStarts). */
		int32 Slot = 0;
		bool bSounded = false;
	};
	TArray<FTMBlow> Blows;

	/** The reactions, statuses and moments a set may name clips for. */
	static const TArray<FString>& ExtraKeys();
	/** Whether a Hit took health away: damage, a burn, a bleed. */
	static bool Harms(const TMSim::FEvent& Event);
	/** Sorts a report's events into blows to land later and the rest to show now. */
	void GatherBlows(const TMSim::FTickReport& Report);
	void AdvanceBlows(float DeltaSeconds);
	void LaunchShots(FTMBlow& Blow);
	void LandBlow(FTMBlow& Blow);
	void ClearBlows();
	/** One event made visible: its number, its light, its effect, the body's reaction. */
	void ShowOne(const TMSim::FEvent& Event, const TMSim::FAbility* Ability, int32 CasterId, TArray<int32>* ShownOn);
	void React(const TMSim::FEvent& Event, int32 CasterId);
	void Jolt(int32 Index, const FVector& Dir, float Size);
	void AddFloater(int32 UnitId, const FString& What, const FColor& Tint, bool bCount = true, const FTMFloatLook* Look = nullptr);
	/** Front, back, left or right of a unit, as seen from a point. */
	FString SideOf(int32 Index, const FVector& From) const;
	/** Glows, bobbing, slowing and freezing, from the statuses a unit carries. */
	void ShowStatuses(int32 Index, float DeltaSeconds);
	/** An ultimate: the world slows for a beat, and its name rises in gold. */
	void UltimateBeat(const TMSim::FEvent& Event);
	/** The battle is decided and every blow has landed: the winners celebrate. */
	void Celebrate();
	bool bCelebrated = false;
	/** When the slow beat of an ultimate ends, in platform seconds; 0 when none. */
	double SlowUntil = 0.0;

	// ------------------------------------------------------ ability looks
	// (TMBattleDirectorAbilityFx.cpp): what an ability is made of -- fire,
	// frost, a blade -- read from its name, and the effects that show it: a
	// flare as it is cast, what flies, the hit on each target, the burst on the
	// ground, a flash of its colour and a jolt of the camera. Only the look.
public:
	struct FTMLook
	{
		/** Index into the look table; the last is plain steel. */
		int32 Flavour = 0;
		/** For plain steel: 0 blade, 1 blunt, 2 point, 3 thrown stone. */
		uint8 Weapon = 0;
		/** Thrown at its target from afar (a single target or a line). */
		bool bRanged = false;
		/** Lands on an area: a circle, a cone, a line, a burst round the user. */
		bool bArea = false;
		bool bMagic = false;
	};
	const FTMLook& LookOf(const TMSim::FAbility& Ability);
	/** The look's name ("fire", "steel"...), for the log and tests. */
	static const TCHAR* LookName(const FTMLook& Look);
	static FLinearColor LookColour(const FTMLook& Look);
	/** Units under the pointer or in the aim, outlined red (TMBattleDirectorLooks.cpp). */
	TSet<int32> MarkedUnits;
	void UpdateMarks();
	/** The outline post-process, kept so the hover colour can follow who is marked. */
	UPROPERTY()
	TObjectPtr<class UMaterialInstanceDynamic> OutlineMid = nullptr;
	/** What a thrown spell flies as, carried by Carrier; null when its look has none. */
	class UFXSystemComponent* LookShot(const FTMLook& Look, class USceneComponent* Carrier);
private:
	TMap<FString, FTMLook> Looks;
	/** Plays an effect so it is about WantCm across (SizeCm is how big it was filmed). */
	class UFXSystemComponent* PlayFx(const TCHAR* Path, const FVector& Local, float WantCm, float SizeCm,
		class USceneComponent* AttachTo = nullptr);
	/** The swing begins: a flare of its colour in the user's hands, or an aura for a buff. */
	void LookCast(const FTMBlow& Blow);
	/** It lands: a burst on the ground it covers, a flash, the camera jolts. */
	void LookLand(const FTMBlow& Blow);
	/** On one unit it touched: the hit, the heal, the buff. */
	void LookOn(const TMSim::FAbility& Ability, const TMSim::FEvent& Event, bool bCritical, bool bArea);
	/** A light of the look's colour that flares and fades. */
	void Pulse(const FVector& Local, const FLinearColor& Colour, float Brightness, float Radius, float Seconds);
	/** The camera jolts: 1 is a heavy blow. */
	void Jolt(float Strength);
	void AdvanceLooks(float DeltaSeconds);
	struct FTMPulse
	{
		TWeakObjectPtr<class UPointLightComponent> Light;
		float Age = 0.0f;
		float Life = 0.4f;
		float Peak = 0.0f;
	};
	TArray<FTMPulse> Pulses;
	float JoltLeft = 0.0f;
	float JoltStrength = 0.0f;
	float JoltClock = 0.0f;
	FVector JoltOffset = FVector::ZeroVector;
	/** With -tmcapture, a picture a moment after a blow lands, when its effects are up. */
	float LookCaptureIn = -1.0f;

	UPROPERTY()
	TArray<TObjectPtr<class UPointLightComponent>> StatusLights;
	UPROPERTY()
	TObjectPtr<UStaticMesh> ShotSphere = nullptr;
	UPROPERTY()
	TObjectPtr<UStaticMesh> ShotRod = nullptr;
	UPROPERTY()
	TObjectPtr<class UMaterialInterface> ShotMaterial = nullptr;

	/** A motion's clips in this set, or the nearest motion it has (heavy to melee, area to bolt...). */
	static const FTMMotionClips* FindMotion(const FTMAnimSet& Set, const FString& Motion);
	/** What a unit shows standing: its wind-up while it charges, its channel while it channels, or idle. */
	UAnimSequence* StandingClip(int32 Index) const;

	/** Reads the character map once per run. False, and said why, if it cannot. */
	bool LoadCharacterMap();
	const FTMBody* BodyFor(const TMSim::FUnit& Unit) const;
	const FTMBody* BodyForJob(const std::string& JobId) const;
	/** Starts every unit standing where the rules put it. */
	void ResetMotion();
	/** Walks, one-offs and falls, a frame at a time. Game worlds only. */
	void AdvanceMotion(float DeltaSeconds);
	/** What the battle's events mean for the bodies: a swing, a flinch. */
	void AnimateEvents(const TMSim::FTickReport& Report);
	/** Plays a clip on a unit; a one-off can be cut short after MaxSeconds (0: all of it). */
	void Animate(int32 Index, UAnimSequence* Clip, bool bLoop, float MaxSeconds = 0.0f);
	/** Where a unit is drawn now: part way along a walk, or where the rules have it. */
	FVector ShownAt(const TMSim::FUnit& Unit) const;

	// Online, behind the scenes.
	TUniquePtr<FTMNet> Net;
	TUniquePtr<FTMUpnp> Upnp;
	TSharedPtr<class FTMTextInput> TextInput;
	/** An order from the host is being applied: it goes straight to the rules, not back out. */
	bool bApplyingFromHost = false;
	/** The host's checksums by tick, until this battle reaches each (battle.gd:657-671). */
	TMap<int32, uint64> HostSums;
	int32 LastSumTick = 0;
	/** The rule numbers of the match, from the host: Developer Tools as the host had them. */
	TMap<FString, double> OnlineTuning;
	/** The setup from before going online, put back on leaving. */
	FMatchSetup OfflineSetup;
	/** -tmnetbots: the computer plays this machine's side, through the online path. -tmnetdesync: the joiner's game is made to differ once. */
	bool bNetBots = false;
	bool bNetDesync = false;
	bool bDesyncDone = false;
	void AdvanceOnline(float DeltaSeconds);
	void OnNetMessage(const FJsonObject& Message, int32 From);
	void StartOnlineAsHost();
	FString StartOnlineFrom(const FJsonObject& Start);
	/** The host's check on a joiner's order, on top of the rules' (battle.gd:634-646). "" to play it. */
	FString RefereeCheck(const TMSim::FOrder& Order, int32 Player) const;
	void AfterOnlineApply(const TMSim::FOrder& Order);
	void CheckHostSums();
	void StopOnline(const FString& Why);
	void ReportOutOfSync(const FString& Detail);
	void SendOnline(const TCHAR* Kind, TFunctionRef<void(FJsonObject&)> Fill);
	void SendChat();
	void TypedDone(bool bSubmit);

	// ------------------------------------------------ the lobby (TMBattleDirectorLobby.cpp)
	// Up to four players choose their sides and classes before an online battle
	// (Docs/design/feat-lobby.md). The host keeps the lobby; the others ask it.

	/** A player in an online match: the host, or one who joined. */
	struct FTMOnlinePlayer
	{
		/** The connection it came by (FTMNet's peer id): -1 for the host itself. */
		int32 Peer = -1;
		FString Name;
		/** 0 blue, 1 red. */
		int32 Team = 0;
		bool bReady = false;
		/** Still connected: one who leaves mid-battle stays listed, and the computer plays their units. */
		bool bPresent = true;
	};
	/** Everyone in the match, the host first, in order of joining. */
	TArray<FTMOnlinePlayer> Players;
	/** Which of Players this machine is. */
	int32 LocalPlayer = 0;
	/** By unit id (0-3 blue, 4-7 red): the player who orders it, -1 the computer (on the host). */
	TArray<int32> UnitPlayer;
	/** What a player who joined is told of the host's setup: the map's name and the rules in a line. */
	FString LobbyMap;
	FString LobbyRulesLine;
	/** This machine's name, as the others see it. */
	FString LocalName() const;
	int32 PlayerOfPeer(int32 Peer) const;
	/** The player who orders a side's slot: its players share the four in order of joining. -1 the computer. */
	int32 SlotOwner(int32 Team, int32 Slot) const;
	int32 UnitOwner(const TMSim::FUnit& Unit) const;
	/** Whether the computer plays this unit on this machine: ComputerPlays, but online by who owns it. */
	bool ComputerPlaysUnit(const TMSim::FUnit& Unit) const;
	/** Who plays a side, in words: "you and Sam", "the computer". */
	FString SideNames(int32 Team) const;
	/** UnitPlayer from the lobby's sides, as a battle starts. */
	void AssignUnits();
	void OpenLobby();
	/** The host opens its lobby, with itself as the first player. */
	void StartLobby();
	bool IsHostInLobby() const;
	void LobbyArrive(int32 Peer, const FString& Name);
	void LobbyDepart(int32 Peer, const FString& Why);
	void BroadcastLobby();
	void ApplyLobby(const FJsonObject& Message);
	FString LobbyRules() const;
	bool LobbyCanStart(FString* WhyNot = nullptr) const;
	void LobbyStart();
	/** After a battle: the host takes everyone back to the lobby. */
	void BackToLobby();
	void LobbySide(int32 Team);
	void LobbyReady();
	/** A class for a slot (team * 4 + slot): the host's own, or asked of it. */
	void LobbyPick(int32 SlotCode, const std::string& JobId);
	bool LobbyMayPick(int32 Player, int32 SlotCode) const;
	void LobbyApplyPick(int32 Player, int32 SlotCode, const std::string& JobId);
	/** The lobby's messages: true if it was one. */
	bool OnLobbyMessage(const FString& Kind, const FJsonObject& Message, int32 From);

	// ------------------------------------------------ the draft (TMBattleDirectorDraft.cpp)

	struct FTMDraftState
	{
		/** The next step of the order (TMDraftOrder::Steps); past the end, done. */
		int32 Step = 0;
		/** Each side's bans, and picks in slot order, by class id. */
		TArray<FString> Bans[2];
		TArray<FString> Picks[2];
		/** Seconds left for this choice; the computer's pause; the finished draft's showing. */
		float Left = 0.0f;
		float Computer = 0.0f;
		float Show = 0.0f;
	};
	FTMDraftState Draft;
	/** The draft has run for the battle about to start. */
	bool bDraftDone = false;
	int32 DraftSteps() const;
	bool DraftStepIsBan(int32 Step) const;
	int32 DraftStepTeam(int32 Step) const;
	bool DraftDone() const;
	bool DraftUsed(const FString& JobId) const;
	/** Who makes this step's choice: a player, -2 any player on the side (a ban), -1 the computer. */
	int32 DraftChooser(int32 Step) const;
	bool DraftMayChoose(int32 Player) const;
	void StartDraft();
	void BroadcastDraft();
	void DraftApply(int32 Player, int32 Step, const FString& JobId);
	FString DraftComputerChoice(int32 Team) const;
	void AdvanceDraft(float DeltaSeconds);
	/** This machine's player chooses (a click on the draft screen). */
	void DraftChoose(const FString& JobId);
	bool OnDraftMessage(const FString& Kind, const FJsonObject& Message, int32 From);
	void EndPlay(const EEndPlayReason::Type Reason) override;

	// Sound (TMBattleDirectorSound.cpp; Content/Data/Sounds/sounds.json).
	bool bSoundsRead = false;
	TMap<FString, TArray<FString>> SoundTakes;
	TMap<FString, TPair<FString, FString>> MotionSounds;
	TMap<FString, TPair<FString, FString>> AbilitySounds;
	TMap<FString, FString> EventSounds;
	/** Each hero's voice lines by role, by animation set. */
	TMap<FString, TMap<FString, FString>> Voices;
	UPROPERTY()
	TMap<FString, TObjectPtr<USoundBase>> LoadedSounds;
	void LoadSounds();
	USoundBase* SoundAt(const FString& Path);
	bool CanSound() const;
	/** A named sound: at a place on the board, or everywhere if At is null. */
	void PlaySound(const FString& Name, const FVector* At, float Volume = 1.0f);
	void PlayEventSound(const TCHAR* Event, const FVector* At, float Volume = 1.0f);
	/** One of a unit's hero's voice lines, this often (0 to 1). */
	void PlayVoice(int32 Index, const TCHAR* Line, float Chance);
	/** An ability's sounds: as it goes off, and where it lands. */
	TPair<FString, FString> SoundsOf(const TMSim::FAbility* Ability, const FString& Motion);
	void SoundBlowStarts(const FTMBlow& Blow, int32 Slot);
	void SoundBlowLands(const FTMBlow& Blow);
	void SoundStep(int32 Index, float DeltaSeconds);

	bool bCharacterMapRead = false;
	/**
	 * Every set as the map writes it, and the sets loaded so far. Loaded only
	 * when worn: thirty heroes' clips at once ran the editor out of memory, and
	 * a battle wears eight at most. AnimSets is given room for every set when
	 * the map is read, so adding one never moves the others (bodies point at them).
	 */
	TMap<FString, TSharedPtr<FJsonObject>> SetSources;
	TMap<FString, FTMAnimSet> AnimSets;
	void BuildAnimSet(const FJsonObject& Set, FTMAnimSet& Out);
	TMap<FString, FTMBody> Bodies;
	TMap<FString, FString> LookBodies;
	TMap<FString, FString> ClassBodies;
	FString DefaultBody;
	/** Keeps every mesh and clip the map names loaded. */
	UPROPERTY()
	TArray<TObjectPtr<UObject>> CharacterAssets;
	TArray<FTMMotion> Motions;

	// ------------------------------------------------ the board and its look
	// (TMBattleDirectorBoard.cpp). The ground the rules give, dressed in a theme
	// from Content/Data/Themes: colours for the ground by height, rock, water,
	// embers and springs, the land around the board, and the sun, sky and fog.
	// None of it is read by the rules.

	struct FTMTheme
	{
		FString Id;
		FString Name;
		/** The top of the ground at each height, from level 1 up; the highest is used above it. */
		TArray<FLinearColor> Tops;
		FLinearColor Side = FLinearColor(0.35f, 0.33f, 0.3f);
		/** How much each tile's colour wanders, so the ground is not a chessboard. */
		float Jitter = 0.05f;
		FLinearColor Rock = FLinearColor(0.45f, 0.43f, 0.4f);
		/** pillars, boulders or crystals. */
		FString RockStyle = TEXT("boulders");
		FLinearColor Water = FLinearColor(0.15f, 0.4f, 0.55f);
		float WaterOpacity = 0.75f;
		/** Water that gives off light: lava, a glowing marsh. */
		bool bWaterGlows = false;
		FLinearColor Embers = FLinearColor(0.2f, 0.08f, 0.05f);
		FLinearColor EmberGlow = FLinearColor(1.0f, 0.4f, 0.1f);
		FLinearColor Spring = FLinearColor(0.2f, 0.6f, 0.55f);
		FLinearColor SpringGlow = FLinearColor(0.4f, 1.0f, 0.9f);
		FLinearColor Outside = FLinearColor(0.25f, 0.35f, 0.18f);
		/** What grows around the board: pine, round, dead or none; and how much of it. */
		FString Trees = TEXT("pine");
		int32 TreeCount = 60;
		FLinearColor Leaves = FLinearColor(0.12f, 0.3f, 0.12f);
		FLinearColor Trunk = FLinearColor(0.3f, 0.2f, 0.12f);
		int32 RockCount = 25;
		float SunPitch = -40.0f;
		float SunYaw = -40.0f;
		float SunIntensity = 8.0f;
		FLinearColor SunColour = FLinearColor(1.0f, 0.96f, 0.88f);
		float SkyIntensity = 4.0f;
		float FogDensity = 0.02f;
		FLinearColor FogColour = FLinearColor(0.45f, 0.55f, 0.7f);

		/**
		 * Meshes to build with instead of the basic shapes -- from a Fab pack,
		 * say (Tools/add_env_kit.py writes these). Each is a list of object
		 * paths; one is picked per piece. Any left empty is built from shapes.
		 */
		TArray<FString> KitTop;      // the walkable ground's surface, stretched to a tile
		TArray<FString> KitRock;     // what stands on a rock tile
		TArray<FString> KitTree;     // what grows around the board
		TArray<FString> KitBoulder;  // rocks around the board
		TArray<FString> KitCliff;    // rock faces on the tall steps between heights
		TArray<FString> KitStructure; // ruins and towers: on some rock tiles, and round the board
		/** How much of a tile a rock fills, how tall a tree stands, how big a boulder is, in metres. */
		float KitRockFill = 0.9f;
		float KitTreeHeight = 6.0f;
		float KitBoulderSize = 2.0f;

		// Light (theme "light"): how the board is lit, beyond sun, sky and fog.
		/** Night: the sun is a pale moon, and torches burn round the board. */
		bool bNight = false;
		/** Degrees a minute the sun turns round the board, so shadows move through a battle; 0 holds it still. */
		float SunDrift = 1.5f;
		/** Torches round the board and on each watchtower, and how bright. 0 is none. */
		float TorchIntensity = 0.0f;
		FLinearColor TorchColour = FLinearColor(1.0f, 0.62f, 0.3f);
		/** Exposure, in stops above the engine's own: dark themes stay dark instead of being brightened back. */
		float Exposure = 0.0f;
		/** Light shafts in the fog. */
		bool bVolumetricFog = true;

		// Foliage (theme "foliage", TMBattleDirectorFoliage.cpp): per square metre of
		// ground it can grow on, and its colours. Kit lists name meshes from a pack
		// (a Fab nature pack, say); empty, simple shapes stand in.
		float GrassDensity = 1.2f;
		TArray<FLinearColor> GrassColours;
		float GrassHeight = 0.35f;
		float FlowerDensity = 0.15f;
		TArray<FLinearColor> FlowerColours;
		float BushDensity = 0.25f;
		FLinearColor BushColour = FLinearColor(0.16f, 0.33f, 0.14f);
		/** Trees per square metre in the ring of land round the board. */
		float ForestDensity = 0.03f;
		TArray<FString> KitGrass;
		TArray<FString> KitFlower;
		TArray<FString> KitBush;
		/** Metres tall, each kit mesh fitted by its own bounds. */
		float KitGrassHeight = 0.4f;
		float KitFlowerHeight = 0.35f;
		float KitBushHeight = 1.0f;
	};
	/**
	 * The ground as one smooth mesh from the tiles' heights (TMBattleDirectorGround.cpp):
	 * no seams, rounded banks, colours blended. False if it could not be built.
	 */
	bool BuildSmoothGround(const FTMTheme& Theme);

	/**
	 * A mesh from a theme's kit, fitted by its own bounds: its footprint to
	 * Footprint (stretched, or uniformly), or its height to Height, standing on
	 * Foot. Null if the mesh is not in the project (said once).
	 */
	UStaticMeshComponent* KitPiece(const FString& Path, const FVector& Foot, float Footprint, float Height, float Yaw, bool bStretch, float MinHeight = 0.0f);
	/** Rock faces on the tall steps between heights, and structures round the board (TMBattleDirectorCliffs.cpp). */
	void DressCliffs(const FTMTheme& Theme);
	void DressStructures(const FTMTheme& Theme);
	/** The theme's rocks, or Paragon's when it names none: never plain painted balls. */
	static const TArray<FString>& RockKit(const FTMTheme& Theme);
	/** A structure for this rock tile instead of a rock, now and then: null when not. */
	UStaticMeshComponent* MaybeStructure(const FTMTheme& Theme, const FVector& Foot, float Tile, const FRandomStream& Dice);
	UPROPERTY()
	TMap<FString, TObjectPtr<UStaticMesh>> KitMeshes;

	/** Reads every theme file once per run. */
	void LoadThemes();
	/** The theme the next or current battle is dressed in. */
	const FTMTheme& ActiveTheme() const;
	/** Every theme, in the order the setup screen offers them. */
	TArray<FString> ThemeIds;
	TMap<FString, FTMTheme> Themes;
	/** Sun, sky and fog, from the level's own lights, set to the theme. */
	void ApplyThemeLighting();
	/** Embers flicker and lava breathes, torches gutter, and the sun moves. */
	void AdvanceBoard(float DeltaSeconds);
	/** Grass, flowers and bushes on the board and a forest round it, as the theme says. */
	void BuildFoliage(const FTMTheme& Theme);
	/** An instanced mesh for one kind of foliage: the kit's, or a simple shape of one colour. */
	class UHierarchicalInstancedStaticMeshComponent* FoliageLayer(const FString& KitPath, const TCHAR* Shape, const FLinearColor& Colour, float CullMetres);
	/** The sun as the theme set it, to turn from. */
	TWeakObjectPtr<class ADirectionalLight> SunActor;
	float SunBaseYaw = 0.0f;
	float SunBasePitch = -45.0f;
	/** A colour on the engine's basic shape material, made once per colour. */
	class UMaterialInstanceDynamic* Paint(const FLinearColor& Colour);
	/** A basic shape on the board: Cube, Sphere, Cylinder or Cone, in centimetres. */
	UStaticMeshComponent* Shape(const TCHAR* Name, const FVector& Where, const FVector& Size, const FRotator& Turn, const FLinearColor& Colour);

	UPROPERTY()
	TArray<TObjectPtr<USceneComponent>> BoardProps;
	/** Each watchtower's roof and flag, which take the colour of the side holding it (in BoardProps too). */
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> TowerRoofs;
	/** Who held each tower when its roof was last painted, so it is painted only on a change. */
	TArray<int32> TowerRoofOwner;
	UPROPERTY()
	TArray<TObjectPtr<class UPointLightComponent>> BoardLights;
	TArray<float> BoardLightBase;
	UPROPERTY()
	TMap<uint32, TObjectPtr<class UMaterialInstanceDynamic>> Paints;
	UPROPERTY()
	TObjectPtr<class UTextureRenderTarget2D> WhitePixels = nullptr;
	bool bThemesRead = false;
	/** A burning ground or a healing spring at Foot (the top of its tile), in the theme's look (TMBattleDirectorHazards.cpp). */
	void BuildHazard(const FTMTheme& Theme, const FVector& Foot, int32 Hazard, int32 Seed);
	/** The flames, embers, steam and motes of the hazards move. */
	void AdvanceHazards(float DeltaSeconds);
	/** An unlit colour shared by every piece that glows it, a little see-through below 1. */
	class UMaterialInstanceDynamic* GlowShared(const FLinearColor& Colour, float Opacity = 1.0f);
	TArray<FTMHazardPart> HazardParts;
	float HazardClock = 0.0f;
	UPROPERTY()
	TMap<uint32, TObjectPtr<class UMaterialInstanceDynamic>> GlowShades;

	// ------------------------------------------------ how units look to the player
	// (TMBattleDirectorLooks.cpp): allies blue and enemies red, a ring under each
	// unit that fills with its Turn Gauge, an outline, and a live portrait for
	// the HUD's unit panels.

public:
	/**
	 * The side the player is on: one person's side against the computer; with
	 * two people at one screen, the side whose unit is being ordered; blue
	 * while nobody is.
	 */
	int32 FriendTeam() const;
	bool IsFriend(const TMSim::FUnit& Unit) const { return Unit.Team == FriendTeam(); }
	/**
	 * A picture of a unit's head and shoulders, filmed this frame, for the HUD.
	 * Side 0 is the ally panel's camera and 1 the enemy's, so both can show at once.
	 */
	class UTextureRenderTarget2D* PortraitOf(int32 Side, const TMSim::FUnit& Unit);
	/** A small portrait of a unit for its turn card, refreshed a few units a frame. */
	class UTextureRenderTarget2D* CardPortrait(const TMSim::FUnit& Unit) const;

private:
	void BuildTurnRings();
	void AdvanceTurnRings();
	void ClearLooks();
	/** Stencil values for the outline: 1 for an ally, 2 for an enemy. */
	void ApplyOutlines();
	/** The team outline, if its material is in the project (Docs/TeamOutline.md). */
	void AddOutlineToCamera();

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> TurnRings;
	UPROPERTY()
	TArray<TObjectPtr<class UTextureRenderTarget2D>> TurnRingTargets;
	UPROPERTY()
	TArray<TObjectPtr<class UMaterialInstanceDynamic>> TurnRingPaint;
	/** How full each ring was last drawn, so it is redrawn only when that changes. */
	TArray<float> TurnRingDrawn;
	UPROPERTY()
	TObjectPtr<class USceneCaptureComponent2D> PortraitCamera0 = nullptr;
	UPROPERTY()
	TObjectPtr<class USceneCaptureComponent2D> PortraitCamera1 = nullptr;
	UPROPERTY()
	TObjectPtr<class UTextureRenderTarget2D> PortraitFilm0 = nullptr;
	UPROPERTY()
	TObjectPtr<class UTextureRenderTarget2D> PortraitFilm1 = nullptr;
	bool bOutlineAdded = false;
	/** Refreshes two units' card portraits a frame, in turn. */
	void AdvanceCardPortraits();
	/** Points a portrait camera at a unit's head and shoulders. */
	void FramePortrait(class USceneCaptureComponent2D* Camera, int32 Index);
	UPROPERTY()
	TObjectPtr<class USceneCaptureComponent2D> CardCamera = nullptr;
	UPROPERTY()
	TArray<TObjectPtr<class UTextureRenderTarget2D>> CardFilms;
	int32 CardNext = 0;

	/** Left over from the last frame, so the clock runs at its own rate. */
	float TickRemainder = 0.0f;

	bool bBuilt = false;

	// ------------------------------------------------ replays (TMBattleDirectorReplay.cpp)
	// Every battle is kept as the orders that were applied to it, with what it
	// started from: the same start and the same orders are the same battle
	// (SimOrder.h). Saved under Saved/Replays when a battle is decided, and
	// played back through Submit like any other order, at the speed asked for.
	// Docs/design/feat-replays.md.

public:
	/** One moment worth marking on a replay's timeline: a fall, a revive, a tower, a cleared camp. */
	struct FTMReplayMark
	{
		int32 Tick = 0;
		/** "ko", "revive", "tower", "camp", "won". */
		FString Kind;
		int32 Unit = -1;
		int32 Team = -1;
	};
	/** What a battle started from and every order applied to it. */
	struct FTMReplay
	{
		FString MadeAt;
		int32 Protocol = 0;
		FString MapId;
		FString ThemeId;
		FString Mode;
		FString Sides[2];
		uint64 Seed = 0;
		/** Every rule number, by its TuningKeys() key, as the battle started. */
		TMap<FString, double> Tuning;
		/** Units 0-7: class and the three items worn at the start. */
		TArray<FString> Jobs;
		TArray<FString> Gear;
		FString BossJob;
		uint64 StartSum = 0;
		/**
		 * The orders in the order applied: an order's text (TMSim::OrderToText),
		 * or "a N" for N ticks of time passing (runs of Advance joined up).
		 */
		TArray<FString> Steps;
		/** The battle's checksum at whole minutes of play, to say where a replay parted. */
		TArray<TPair<int32, uint64>> Sums;
		TArray<FTMReplayMark> Marks;
		int32 Winner = -1;
		int32 Ticks = 0;
		uint64 FinalSum = 0;
	};
	/** A saved replay as the list shows it: read from its file's header. */
	struct FTMReplayEntry
	{
		FString File;
		FString MadeAt;
		FString MapId;
		FString Sides[2];
		int32 Winner = -1;
		int32 Ticks = 0;
	};

	/** A replay is being watched rather than a battle played. */
	bool bReplaying = false;
	/** Watching: playing (or paused by the replay's own button). */
	bool bReplayPlaying = true;
	/** Watching: 0.5, 1, 2 or 4 times as fast. */
	float ReplaySpeed = 1.0f;
	/** Watching: -1 sees everything, 0 or 1 through that side's fog. */
	int32 ReplayView = -1;
	/** The replay being watched, and the one recorded for the battle on the board. */
	FTMReplay Watching;
	FTMReplay Recording;
	/** Where the last decided battle's replay was saved; empty until one was. */
	FString LastReplayFile;
	/** Saved replays, newest first, for the Replays screen. */
	TArray<FTMReplayEntry> ReplayList;
	int32 ReplayListPage = 0;
	/** Said over a replay that could not be played, or stopped matching. */
	FString ReplayProblem;

	/** The Replays screen: reads Saved/Replays. */
	void OpenReplays();
	/** Watches a saved replay (a file under Saved/Replays), or the battle just played (empty). */
	void WatchReplay(const FString& File);
	/** Stops watching, back to the Replays screen (or the title). */
	void LeaveReplay(bool bToList);
	/** The whole battle's length in ticks, and where the replay is in it. */
	int32 ReplayTotalTicks() const { return Watching.Ticks; }
	/** Jumps to a tick: forward by playing on quietly, back by starting again. */
	void SeekReplay(int32 ToTick);
	/** To the previous or next order that wasn't time passing. */
	void StepReplay(int32 Direction);
	/** A HUD button of the replay bar or the Replays screen. */
	bool PressReplayButton(const struct FTMHudButton& Button);
	/** A key while watching. True when it was the replay's. */
	bool ReplayKey(const FKey& Key);

private:
	/** Starts recording the battle just built (BuildBattle, after Battle.Start). */
	void BeginRecording();
	/** Notes an order Submit applied, and what it set off. */
	void RecordApplied(const TMSim::FOrder& Order, const TMSim::FTickReport& Report);
	/** Writes the decided battle to Saved/Replays and keeps the newest 50. */
	void SaveRecording();
	/** BuildBattle, while watching: the replay's rules, items, boss and seed, before the battle starts. */
	void ApplyReplayStart();
	/** Moves a replay on by real seconds (Tick). */
	void AdvanceReplay(float RealSeconds);
	/** Applies the replay's next step, or as much of its time as Budget allows. Returns ticks used. */
	int32 PlayReplayStep(int32 Budget);
	/** Builds the replay's battle afresh at its start. */
	void RestartReplay();
	static bool WriteReplay(const FTMReplay& Replay, const FString& File);
	static bool ReadReplay(const FString& File, FTMReplay& Out, FString& Problem);
	/** Where the replay is: the next step to apply, and ticks of it already played. */
	int32 ReplayCursor = 0;
	int32 ReplayCursorUsed = 0;
	float ReplayRemainder = 0.0f;
	/** Fast-forwarding to a point: the steps show no blows or effects. */
	bool bReplayQuiet = false;
	/** Applying one of the replay's own orders (Submit lets only these through while watching). */
	bool bApplyingReplay = false;
	/** The next checksum to compare, while watching. */
	int32 ReplaySumNext = 0;
	/** Whole minutes recorded so far, for Recording.Sums. */
	int32 RecordedMinutes = 0;
	/** The setup before watching, put back when the replay is left. */
	FMatchSetup SetupBeforeReplay;
	bool bComputerPlayedBeforeReplay[2] = { false, false };
	/** The decided battle has been saved (once). */
	bool bRecordingSaved = false;

	// ------------------------------------------------ the battle report (TMBattleDirectorReport.cpp)
	// Everything each unit did, tallied from the battle's events as they happen
	// (so a replay tallies the same), scored for the MVP, and shown when the
	// battle is decided ("Battle Report Mockups"; Docs/design/feat-battle-report.md).

public:
	struct FTMUnitTally
	{
		int32 Damage = 0;
		int32 Taken = 0;
		/** Damage that never landed: Armor or Resist's share, dodges and grazes, Protect or Shell, shields. */
		int32 Mitigated = 0;
		int32 Healing = 0;
		int32 Kills = 0;
		int32 Assists = 0;
		int32 Deaths = 0;
		int32 Monsters = 0;
		int32 Bosses = 0;
		int32 Buffs = 0;
		int32 Debuffs = 0;
		/** Enemy turns lost to Stun, Sleep, Taunt, Root, Charm and the like. */
		int32 Control = 0;
		int32 Revives = 0;
		int32 Towers = 0;
		int32 Biggest = 0;
		int32 Crits = 0;
		/** Damage taken in an ally's place (Guard). */
		int32 Guarded = 0;
		/** Damage dealt by each ability, by name; and damage taken from each unit, by id. */
		TMap<FString, int32> ByAbility;
		TMap<int32, int32> TakenFrom;
	};
	/** By unit id; the eight units of the two sides. */
	TMap<int32, FTMUnitTally> Tallies;
	/** A unit's points for the MVP, kept to the tenth. */
	static double ScoreOf(const FTMUnitTally& Tally);
	/** The MVP: the most points; a tie to fewer falls, then more damage. -1 before anything happened. */
	int32 MvpId() const;
	/** The report's tab: 0 overview, 1 damage, 2 support, 3 control; the unit opened in it, or -1. */
	int32 ReportTab = 0;
	int32 ReportUnit = -1;
	/** The report is put away to look at the board. */
	bool bReportHidden = false;

private:
	void ResetTallies();
	/** Hp of every unit before an order is applied, so healing counts only what was missing. */
	void SnapshotForTally();
	void TallyEvents(const TMSim::FTickReport& Report);
	TMap<int32, int32> TallyHp;
	/** Who last hurt each unit (its killer when it falls), and who helped, with the tick. */
	TMap<int32, int32> LastHurtBy;
	TMap<int32, TMap<int32, int32>> HelpedAgainst;
	/** Who put each status on each unit ("unit:status"), so a burn's ticks are its caster's. */
	TMap<FString, int32> StatusFrom;
	/** Within one report: shield soaked before the hit, a graze or a guard, per unit. */
	TMap<int32, int32> PendingSoak;
	TSet<int32> PendingGraze;
	TSet<int32> PendingGuard;
};
