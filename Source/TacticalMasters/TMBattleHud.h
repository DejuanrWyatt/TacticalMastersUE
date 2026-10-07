// What a person reads while playing: whose turn is coming, the unit they are
// ordering, its abilities, what an aim would do, and what has just happened.
//
// Laid out as the Godot game's hud.gd lays it out (hud.gd:2-13): the turn order
// along the top, the selected unit's card bottom left, the action bar bottom
// centre with the hover preview above it, and the combat log on the left.
//
// Drawn on the canvas every frame rather than built from widgets, so it needs no
// assets made in the editor and nothing in it is left out of a shipping build.
// It decides nothing and holds nothing: it reads the director, and a click on
// one of its buttons is handed back to the director to turn into an order.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"

#include "TMSettings.h"
#include <string>

#include "TMBattleHud.generated.h"

class ATMBattleDirector;
class UFont;

namespace TMSim
{
	struct FUnit;
	struct FItemDef;
	struct FVec2;
	struct FAbility;
	struct FJobDef;
	class FBattle;
	struct FOdds;
}

/** Something on the HUD that does something when clicked. */
enum class ETMHudAction : uint8
{
	None,
	Move,
	Sprint,
	Ability,
	EndTurn,
	PickUnit,
	/** The "is ready" note's Go: that unit (Value), and the camera to it (2026-10-03). */
	ReadyGo,
	NewBattle,

	// The title screen.
	TitleVsComputer,
	TitleTwoPlayers,
	TitleWatch,
	Quit,
	// Online (main_menu.gd:79-230): the title's button, then host or join.
	// OnlineField's value is the field to type in (ATMBattleDirector::ETypeField).
	// OnlineJoin joins by address, OnlineJoinCode by a join code (Docs/design/feat-online-eos.md);
	// OnlineAdvanced shows or hides joining by address; LobbyCopyCode copies the join code.
	TitleOnline,
	OnlineHost,
	OnlineJoin,
	OnlineField,
	OnlineBack,
	OnlineJoinCode,
	OnlineAdvanced,
	LobbyCopyCode,
	// The lobby (TMBattleHudLobby.cpp). LobbySide's value is the side; LobbySlot's
	// the slot code (team * 4 + slot); DraftChoose's the class's index, -1 to let a ban go,
	// -2 one at random (as is PickerChoose's -2). LobbyRandom rolls the player's own classes.
	LobbySide,
	LobbyReady,
	LobbyStart,
	LobbySettings,
	LobbyLeave,
	LobbySlot,
	DraftChoose,
	DraftToggle,
	DraftTimer,
	LobbyRandom,
	// Tile movement, a setup option (v20 play test): free, four ways, eight ways, round.
	SetupTileMove,

	// The setup screen. SetupClass's value is team * 4 + slot; the others' is the team.
	SetupClass,
	SetupRandom,
	SetupDefault,
	SetupSide,
	SetupDifficulty,
	SetupSeed,
	SetupVictory,
	SetupTime,
	SetupPlanning,
	SetupMap,
	SetupTheme,
	PlanningReady,
	SetupStart,
	SetupBack,

	// The menu inside a battle, and the end of one.
	MenuResume,
	MenuRestart,
	MenuSetup,
	MenuTitle,

	// The corner buttons, the log, the cards and the Unit Guide. GuideJob's value is the class's index.
	ToggleLog,
	GrowLog,
	ToggleField,
	ToggleGuide,
	Pause,
	OpenMenu,
	CloseCard,
	GuideJob,
	GuideAgainst,

	// The class picker on the setup screen. PickerChoose's value is the class's index, PickerRole's the role's (-1 for all).
	PickerChoose,
	PickerRole,
	PickerClose,

	// Options and Developer Tools. Slider's value is the slider's id; RebindAction's
	// the action; DevReset's the rule number's index.
	OpenOptions,
	OpenDevTools,
	CloseOverlay,
	Slider,
	OptionUiScale,
	OptionFullscreen,
	OptionColorblind,
	RebindAction,
	ResetOptions,
	DevReset,
	DevResetAll,
	OptionTurnSquares,
	OptionSquadStrip,
	OptionAutoRecenter,
	/** Quick Cast on or off for an ability key; Value is its slot, 0-3 (2026-10-03). */
	OptionQuickCast,
	/** One of the feel options on or off; Value says which (ATMBattleHud::FeelOption). */
	OptionFeel,

	// Edit layout. LayoutGrab's value is the panel's place in this frame's list;
	// LayoutCard's is the unit whose turn square is being dragged.
	ToggleLayout,
	LayoutGrab,
	LayoutCard,
	LayoutReset,
	// LayoutResize's value is the panel's place in this frame's list.
	LayoutResize,
	LayoutGrid,
	LayoutGridSize,

	// Watchtowers (not Godot's): the setup screen's count, and spending a turn taking one.
	SetupTowers,
	Capture,

	// Neutral camps (Docs/design/feat-neutral-camps.md): the setup screen's camps and boss rows;
	// TakeOpen opens the team items screen; Take's value is the item's place in the cache in reach;
	// Drop's and EquipSlot's unit id x 4 + gear slot; StashPick's the item's place in the stash.
	SetupCamps,
	SetupBoss,
	SetupElements,
	SetupFriendlyFire,
	SetupUniqueClasses,
	SetupCampRespawn,
	SetupBossHunt,
	SetupBossClaim,
	TakeOpen,
	/** The combat log's tab: 0 All, 1 Combat, 2 Mine, 3 Key. */
	LogTab,
	/** Nothing: catches clicks on a panel's empty part so they don't reach what is under it. */
	OverlayBlock,
	Take,
	Drop,
	StashPick,
	EquipSlot,

	// The Unit Guide's list and class page. GuideStep's and GuideScroll's values are -1 or +1
	// (a class, a page); GuideRole's the role's index (-1 for all); GuideTurn's which way to turn the hero.
	GuideBack,
	GuideStep,
	GuideRole,
	GuideScroll,
	GuideTurn,

	// Items (Docs/design/feat-neutral-camps.md). SetupItem's value is team * 12 + slot * 3 + item slot;
	// ItemChoose's the item's index in TMSim::AllItems() (-1 empties the slot); ItemTier's the tier (-1 all);
	// GuideTab's 0 for classes, 1 for items, 2-7 the Codex's other pages (TMBattleHudCodex.cpp).
	SetupItem,
	SetupItemBudget,
	ItemChoose,
	ItemTier,
	ItemClose,
	GuideTab,

	// Queued orders (2026-10-01): the plan strip's Go (or Plan, or Done), Undo and Clear.
	PlanGo,
	/** Cancels a unit's whole queue: Value is the unit's id (2026-10-06, "Queued orders" B and C). */
	QueueCancel,
	// Go To (2026-10-01): GoToMode's value 0 walks and ends each turn, 1 walks and waits.
	GoToMode,
	GoToCancel,

	// Replays (TMBattleDirectorReplay.cpp). ReplayWatch's value is the replay's place in the list,
	// -1 for the battle just played; ReplaySpeed's the speed's index; ReplayStep's -1 or +1;
	// ReplaySeek's where on the timeline, in thousandths; ReplayView's -1 (all) or the side;
	// ReplayPage's -1 or +1; ReplayDelete's the replay's place in the list.
	TitleReplays,
	ReplayWatch,
	ReplayPlay,
	ReplaySpeed,
	ReplayStep,
	ReplaySeek,
	ReplayView,
	ReplayLeave,
	ReplayAgain,
	ReplayPage,
	ReplayDelete,
	ReplayListBack,

	// The battle report (TMBattleHudReport.cpp). ReportTab's value is the tab; ReportUnit's the unit id
	// (-1 back to the tables); ReportMoment's the tick to watch the replay from.
	ReportTab,
	ReportUnit,
	ReportHide,
	ReportMoment,
};

/** Words shown when the pointer rests on part of the HUD: how a number is worked out. */
struct FTMHudTip
{
	FBox2D Area;
	FString Text;
	/** An ability's card instead of the words (AbilityCard): the unit and its slot, or -1. */
	int32 CardUnit = -1;
	int32 CardSlot = -1;
};

struct FTMHudButton
{
	FBox2D Area;
	ETMHudAction Action = ETMHudAction::None;
	/** The ability slot, or the unit id for PickUnit. */
	int32 Value = -1;
};

UCLASS()
class TACTICALMASTERS_API ATMBattleHud : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

	/** The button under a point on the screen, if any: so a click there is not also a click on the board. */
	bool ButtonAt(const FVector2D& Point, FTMHudButton& Out) const;
	/** For the click log: the nearest button to a point that hit none, and the canvas size. */
	FString DescribeMiss(const FVector2D& Point) const;
	/** A button on screen now, by what it does (Value -2: any value). For the robot playtester. */
	bool FindButton(ETMHudAction Action, int32 Value, FTMHudButton& Out) const;
	/** The feel options (Options), by OptionFeel's value; null for none. */
	static bool* FeelOption(int32 Which);

private:
	ATMBattleDirector* FindDirector();

	// The parts, top to bottom.
	void DrawBoardAids(ATMBattleDirector& Director);
	/**
	 * A damaging blow's odds on one unit ("Open Odds Mockups" A): its health and
	 * where each outcome leaves it, a bar split hit / crit / graze / dodge by
	 * chance, the damage of each, and the chance it falls. Centred on CX, its
	 * foot at Bottom; returns its height.
	 */
	float OddsCard(const TMSim::FUnit& Target, const TMSim::FOdds& Odds, bool bFriend, const FString& Extra, float CX, float Bottom);
	/**
	 * An enemy pointed at ("Open Odds Mockups" C): its best blow on each of your
	 * units on its next turn, as lines and odds, and a card listing them; and
	 * while you plan a walk, which enemies reach where it ends.
	 */
	void DrawThreats(ATMBattleDirector& Director);
	/**
	 * Words for the zones of control ("Zone of Control Mockups" A, B, D) while a
	 * walk is aimed: who holds the line, the cost of breaking away, where a walk
	 * into a zone ends, and, for a tank, the ways to your back line it would cut.
	 */
	void DrawZoneWords(ATMBattleDirector& Director);
	void DrawTurnOrder(ATMBattleDirector& Director);
	void DrawLog(ATMBattleDirector& Director);
	/**
	 * Your squad down the left edge ("Squad Strip Mockups" C): a health ring and
	 * status icons with turns left for each; the unit acting now, and one pointed
	 * at here, on its square or on the board, opens to its full row.
	 */
	void DrawSquadStrip(ATMBattleDirector& Director);
	void DrawUnitCard(ATMBattleDirector& Director);
	void DrawActionBar(ATMBattleDirector& Director);
	/** Queued orders (2026-10-01): the selected unit's plan as steps, with Go, Undo and Clear, above Above. Returns the new top. */
	float DrawPlanStrip(ATMBattleDirector& Director, const TMSim::FUnit& Unit, float Above);
	/** A Go To's strip: turns left, how each ends, Keep going when stopped, Cancel. */
	float DrawGoToStrip(ATMBattleDirector& Director, const TMSim::FUnit& Unit, float Above);
	/** Each Go To's turn numbers on the ground, where each turn's walk ends. */
	void DrawGoToMarks(ATMBattleDirector& Director);
	/** Where clicks were taken, the walk's ghost, fast-forward (TMBattleHudFeel.cpp, 2026-10-03). */
	void DrawFeel(ATMBattleDirector& Director);
	/** A tick box with its words after it; it answers as Action, Value. */
	void CheckBox(float X, float Y, float H, bool bOn, const FString& Words, ETMHudAction Action, int32 Value, const FString& Tip);

	void DrawBanners(ATMBattleDirector& Director);
	void DrawTitle(ATMBattleDirector& Director);
	/** The Replays screen, and the bar along the bottom while one is watched (TMBattleHudReplay.cpp). */
	void DrawReplays(ATMBattleDirector& Director);
	void DrawReplayBar(ATMBattleDirector& Director);
	/** The battle report at the end of a battle: the MVP, each unit's numbers, the moments (TMBattleHudReport.cpp). */
	void DrawBattleReport(ATMBattleDirector& Director, const FString& Line, const FLinearColor& Colour);
	/** What a unit carried at the end, as item badges with their tips (the battle report). */
	void ReportGear(const TMSim::FUnit& Unit, float X, float Y, float Size);
	void DrawSetup(ATMBattleDirector& Director);
	/** Host or join a match against another machine (main_menu.gd:79-230). */
	void DrawOnline(ATMBattleDirector& From);
	/** The online lobby and the draft (TMBattleHudLobby.cpp). */
	void DrawLobby(ATMBattleDirector& From);
	void DrawDraft(ATMBattleDirector& From);
	/** A box to type in: click to type, and a caret while typing. */
	void TextField(float X, float Y, float W, float H, const FString& Value, const FString& Placeholder, bool bTyping, int32 Field);
	void DrawBattleMenu(ATMBattleDirector& Director);
	void DrawCornerButtons(ATMBattleDirector& Director);
	void DrawField(ATMBattleDirector& Director);
	void DrawInspectCard(ATMBattleDirector& Director);
	void DrawGuide(ATMBattleDirector& Director);
	/** Every class, a row each, filtered by role and scrolled; a row opens its page. */
	void DrawGuideList(ATMBattleDirector& Director, float PX, float PY, float PW, float PH);
	/** One class's page: its hero turning, its stats, and every ability in full. Scrolls. */
	void DrawGuideDetail(ATMBattleDirector& Director, float PX, float PY, float PW, float PH);
	/** A thin scroll bar: where the view is in the whole. */
	void ScrollBar(float X, float Y, float H, float Shown, float Whole, float At);
	/** Words broken into lines no wider than Width. */
	TArray<FString> Wrap(const FString& What, UFont* Font, float Scale, float Width);
	void DrawOptions(ATMBattleDirector& Director);
	/** Options or Developer Tools, if open, over everything else. */
	void DrawOverlays(ATMBattleDirector& Director);
	/** The turn order as a fixed square per unit (hud.gd:318-470). */
	void DrawTurnSquares(ATMBattleDirector& Director);
	/** Handles over every movable panel, and the bar that says how (hud.gd:203-240). */
	void DrawLayoutEditing(ATMBattleDirector& Director);
	/** How far the player has moved this panel, in this screen's pixels. */
	FVector2D Nudge(const TCHAR* Id) const;
	/** Says a panel was drawn here and may be moved in Edit layout. */
	void Movable(const TCHAR* Id, const TCHAR* Label, float X, float Y, float W, float H);
	void DrawDevTools(ATMBattleDirector& Director);
	/** A slider with its track, fill and knob, answering the pointer over all of it. */
	void Slider(float X, float Y, float W, float H, int32 Id, double Value, double Low, double High);
	void DrawClassPicker(ATMBattleDirector& Director);
	/**
	 * A class at a glance, for choosing it (v19 play test): name, roles, its
	 * numbers, and its four abilities with what each does. In the class picker
	 * and the draft, for the class under the pointer.
	 */
	void DrawClassCard(const TMSim::FJobDef& Job, float X, float Y, float W, float H);
	/** Every item that can go in one setup slot, by tier, with its cost and what it does. */
	void DrawItemPicker(ATMBattleDirector& Director);
	/** The items panel over the action bar: the cache in reach to take from, and the unit's own to leave. */
	void DrawTakePicker(ATMBattleDirector& Director);
	/** The team items screen: the side's units, what each wears, open slots, and the stash (TMBattleHudPanels.cpp). */
	void DrawTeamItems(ATMBattleDirector& Director);
	/** The Unit Guide's Items page: every item by tier, scrolled like the class list. */
	void DrawGuideItems(ATMBattleDirector& Director, float PX, float PY, float PW, float PH, float Top);
	/** The Codex's other pages (TMBattleHudCodex.cpp): how to play, keywords, statuses, ground, objectives, controls. */
	void DrawCodexPage(ATMBattleDirector& Director, float PX, float PY, float PW, float PH, float Top);
	/** An item as its icon (Content/Data/Icons/items), framed in its tier's colour; an empty slot as a dim +. Adds its tooltip. */
	void ItemBadge(const TMSim::FItemDef* Item, float X, float Y, float Size, bool bTip = true);
	/** An item's own icon, or none (then its initials are drawn). */
	class UTexture2D* ItemIcon(const TMSim::FItemDef& Item);
	/** What a unit carries, as small icons in a row from X: the width used. */
	float GearRow(const TMSim::FUnit& Unit, float X, float Y, float Size);
	/** The words in the world -- numbers that fly off a blow, camp names, what lies in a cache -- drawn white-ish with a dark outline. */
	void DrawWorldWords(ATMBattleDirector& Director);
	/**
	 * Over a unit casting (v19 play test, "Battle Indicator Alternatives" Cast A):
	 * the ability's icon and name, the seconds left and a thick bar, its bottom
	 * edge centred on (X, Bottom). Its height.
	 */
	/**
	 * A cast in progress over the caster's head; returns its height. It shrinks to a
	 * small chip (icon, seconds, bar) a moment after the cast starts, unless bFull.
	 */
	float CastCard(const TMSim::FUnit& Unit, float X, float Bottom, float Scale, bool bFull = false);
	/**
	 * A tank's zone of control as shields circling its edge (v19 play test,
	 * "Battle Indicator Alternatives" Tank B): every enemy tank in sight, and
	 * one of yours under the pointer.
	 */
	void DrawZoneShields(ATMBattleDirector& Director);
	/**
	 * A gold READY tag over every unit in sight whose turn is up, in place of
	 * the light at its feet the v19 play test found too bright ("Battle
	 * Indicator Alternatives" Ready D; nothing drawn on the ground).
	 */
	void DrawReadyMarks(ATMBattleDirector& Director);
	/** Cast B: a sigil under each caster, and a banner for an enemy cast that will catch yours. */
	void DrawCastWarnings(ATMBattleDirector& Director);
	/** What an item does, in one line: "+12 max HP, +10% ability damage". */
	static FString ItemSummary(const TMSim::FItemDef& Item);
	static FLinearColor TierColour(int32 Tier);
	void DrawTooltip(ATMBattleDirector* Director = nullptr);
	/** A bar while heroes, shaders or graphics pipelines are still loading. */
	void DrawLoading(ATMBattleDirector& Director);

	/** Who is casting what at this unit, as a line per caster (hud.gd:1123-1150). Returns the height used. */
	float DrawIncoming(ATMBattleDirector& Director, const TMSim::FUnit& Unit, float X, float Y, float W);
	/**
	 * A unit's panel, Atlas Reactor's way: its portrait, health, statuses, Turn
	 * Gauge and ultimate meter, bottom left for an ally and bottom right for an
	 * enemy, which also shows its abilities.
	 */
	void DrawUnitPanel(ATMBattleDirector& Director, const TMSim::FUnit& Unit, bool bRight);
	/** Each unit's name, health and statuses over its head. */
	void DrawOverheads(ATMBattleDirector& Director);
	/** What is left of a unit's turn, two joined round tokens in front of its ring: the move and the action. */
	void TurnPips(const TMSim::FUnit& Unit, float CX, float Top, float Zoom);
	/** An ability cooling down, and on which of its owner's coming turns it is back (1 = the next). */
	struct FTMBack
	{
		int32 Slot = -1;
		int32 Turn = 0;
		/** The ability being aimed: where its cooldown would put it if used now. */
		bool bPreview = false;
	};
	/** Every ability of the unit cooling down, and the one being aimed (2026-10-02, "Cooldown Ghost Chip Mockups"). */
	TArray<FTMBack> ComingBack(ATMBattleDirector& From, const TMSim::FUnit& Unit) const;
	/** About how many seconds until the unit's Turn-th coming turn (1 = the next). */
	float TurnInSeconds(const TMSim::FBattle& Battle, const TMSim::FUnit& Unit, int32 Turn) const;
	/** The unit is pointed at, on the board or on its chip, square or pin: the others light up with it. */
	bool TurnLinked(ATMBattleDirector& From, int32 UnitId) const;
	/** Under a turn square: the unit's next three turns as ghost squares, with what comes back on each. */
	void DrawComingTurns(ATMBattleDirector& From, const TMSim::FUnit& Unit, float X, float Top, bool bLeftward);
	/**
	 * The boss bar ("Camps and Bosses Mockups" B, C, D): an awake boss in sight,
	 * its health (split by each side's share with the claim on), its wind-up and
	 * stagger, whom it hunts and remembers, and what it has just announced.
	 */
	void DrawBossBar(ATMBattleDirector& From);
	/** One of ours just in a fight: its health over its head for a few seconds, the lost part draining. */
	void PopBar(ATMBattleDirector& Director, const TMSim::FUnit& Unit, float CentreX, float Bottom);
	/** A row of status chips. Returns the width used. Right to left from X when bLeftward. */
	/** Most > 0 draws at most that many, the last as "+N" for the rest. */
	float StatusChips(const TMSim::FUnit& Unit, float X, float Y, float Size, bool bLeftward, bool bTips, int32 Most = 0);
	/** An ability's icon tile: coloured when it can be used, grey with the turns left when it cannot. */
	void AbilityTile(ATMBattleDirector& Director, const TMSim::FUnit& Unit, int32 Slot, float X, float Y, float Size, bool bButton);
	/** An aura that is on: two lights chasing each other round the tile's edge, in its colour (the human's choice "B", 2026-09-30). */
	void AuraTrail(float X, float Y, float W, float H, const FLinearColor& Colour);
	/** A slanted bar, the shape Atlas Reactor's are. */
	void Slant(float X, float Y, float W, float H, const FLinearColor& Colour, float Skew);
	/** A small picture of an ability's shape (TMSim::ShapeOf) in a Size-wide badge: a crosshair, a disc, a cone... */
	void ShapeBadge(const std::string& Shape, float X, float Y, float Size, const FLinearColor& Colour);
	void Bar(float X, float Y, float W, float H, float Fraction, const FLinearColor& Fill, const FLinearColor& Back, float Skew = 0.0f);
	void Picture(class UTexture* Texture, float X, float Y, float W, float H, const FLinearColor& Tint = FLinearColor::White);

	/** One of the Godot game's icons (Content/Data/Icons), loaded once; the grey copy for what cannot be used. */
	class UTexture2D* Icon(const FString& Name, bool bGrey);
	class UTexture2D* AbilityIcon(const TMSim::FAbility& Ability, bool bGrey);
	class UTexture2D* ClassIcon(const TMSim::FUnit& Unit);

	// How numbers are worked out, in words (game_state.gd:952-1016).
	FString ExplainTurn(ATMBattleDirector& Director, const TMSim::FUnit& Unit) const;
	FString ExplainCountdown(ATMBattleDirector& Director, const TMSim::FUnit& Unit) const;
	FString ExplainMove(ATMBattleDirector& Director, const TMSim::FUnit& Unit) const;
	FString ExplainSight(ATMBattleDirector& Director, const TMSim::FUnit& Unit) const;
	FString ExplainAbility(ATMBattleDirector& Director, const TMSim::FUnit& Unit, int32 Slot) const;
	/**
	 * An ability's card ("Ability Info Mockups" D with A's chips, 2026-10-05): its
	 * fields as chips, its statuses and a sentence, or with bDetail every field on a
	 * row and each status explained. DrawH 0 only measures; its size comes back.
	 */
	FVector2D AbilityCard(ATMBattleDirector& Director, const TMSim::FUnit& Unit, int32 Slot, float X, float Y, bool bDetail, float DrawH, const FString& Note);
	/** A tip that shows an ability's card under the pointer. */
	void AddAbilityTip(float X, float Y, float W, float H, const TMSim::FUnit& Unit, int32 Slot);
	/** Alt is held: ability cards show the whole of it. */
	bool AltHeld() const;
	static FString BuffText(const TMSim::FUnit& Unit);

	void AddTip(float X, float Y, float W, float H, const FString& Tip);

	/** A button on a menu: a label, an optional second line, and a highlight under the pointer. */
	void MenuButton(float X, float Y, float W, float H, const FString& Label, ETMHudAction Action, int32 Value = -1,
		bool bPrimary = false, const FString& Detail = FString());
	/** MenuButton, or the same greyed and not pressable when bEnabled is false. */
	void ChoiceButton(float X, float Y, float W, float H, const FString& Label, ETMHudAction Action, int32 Value, bool bEnabled);
	/** Where the pointer is, or off the screen. */
	FVector2D MousePoint() const;

	// Drawing helpers, in pixels already scaled.
	void Panel(float X, float Y, float W, float H, const FLinearColor& Fill, const FLinearColor& Edge = FLinearColor::Transparent, float Thickness = 1.0f);
	void Text(const FString& What, float X, float Y, const FLinearColor& Colour, UFont* Font, float Scale = 1.0f, bool bShadow = true);
	/** Text with a dark outline Edge pixels thick all round, so it reads over anything. */
	void OutlinedText(const FString& What, float X, float Y, const FLinearColor& Colour, UFont* Font, float Scale, float Edge);
	FVector2D TextSize(const FString& What, UFont* Font, float Scale = 1.0f);
	void Gauge(float X, float Y, float W, float H, float Fraction, const FLinearColor& Fill, const FString& Label);
	void AddButton(float X, float Y, float W, float H, ETMHudAction Action, int32 Value = -1);

	/** A spot on the board to the screen. False if it is behind the camera. */
	bool ToScreen(ATMBattleDirector& Director, const TMSim::FVec2& Point, float Lift, FVector2D& Out) const;
	/** A ring lying on the board. */
	void BoardRing(ATMBattleDirector& Director, const TMSim::FVec2& Centre, float RadiusMetres, const FLinearColor& Colour, float Thickness);

	/** A class's display name ("Black Mage"), falling back to its id. */
	static FString JobName(const TMSim::FUnit& Unit);

	TWeakObjectPtr<ATMBattleDirector> Director;

	UPROPERTY()
	TMap<FString, TObjectPtr<class UTexture2D>> Icons;
	/** Names with no picture, asked for once. */
	TSet<FString> NoIcon;

	/** Pixels per design pixel: the layout is drawn for 1080 lines and scaled. */
	float S = 1.0f;
	/** The screen's own scale; S is this times the size of the panel being drawn. */
	float BaseS = 1.0f;

	/**
	 * While a panel is drawn, everything in it is drawn at its own size: S is
	 * scaled for the panel, and put back when it is done. Its anchor to its
	 * edge or corner is worked out in the scaled S too, so a bigger action bar
	 * still sits at the bottom of the screen.
	 */
	struct FPanelScale
	{
		ATMBattleHud& Hud;
		float Saved;
		FPanelScale(ATMBattleHud& In, const TCHAR* Id) : Hud(In), Saved(In.S) { In.S = In.BaseS * FTMSettings::Get().ScaleOf(Id); }
		~FPanelScale() { Hud.S = Saved; }
	};

	TArray<FTMHudButton> Buttons;

public:
	/** Where each slider was drawn this frame, for dragging it. */
	TMap<int32, FBox2D> SliderAreas;
	/** Where the log was drawn this frame: the wheel scrolls it there and zooms elsewhere. */
	FBox2D LogArea = FBox2D(ForceInit);

	/** A panel that can be moved in Edit layout, as drawn this frame. */
	struct FTMMovable
	{
		FString Id;
		FString Label;
		FBox2D Area;
	};
	TArray<FTMMovable> Movables;
	/** Where each turn square was drawn this frame, by unit id, for reordering. */
	TMap<int32, FBox2D> SquareAreas;
	/** When each unit's turn came, in real seconds, for the flash its card gives (DrawTurnSquares). */
	TMap<int32, float> ReadySince;
	/** Screen pixels to a 1080p pixel's worth, as this frame was drawn -- before any panel's own size. */
	float Scale() const { return BaseS; }

private:
	TArray<FTMHudTip> Tips;

	/** Where each turn chip is drawn, so it glides rather than jumps (hud.gd:1297-1311). */
	TMap<int32, FVector2D> ChipPlace;
	/** The unit whose chip, square or cooldown pin the pointer is on this frame, or -1. */
	int32 HoverLink = -1;
	TMap<int32, float> ChipScale;

	/** The bottom of the log window, so the field list can sit under it. */
	float LogBottom = 0.0f;
	/** The pointer was on the log last frame: it opens from its few newest lines (v20 play test, less text). */
	bool bLogHovered = false;
	/** Your unit whose turn comes next while none of yours is ready, for its turn chip to glow, or -1. */
	int32 NextOwnUnit(ATMBattleDirector& From, float* Seconds = nullptr) const;
	/** The bottom of the squad strip, or 0 when it is off, so the field list can sit under it. */
	float SquadBottom = 0.0f;
	/** The squad row the pointer was on last frame, which stays open while it is. */
	int32 SquadHoverId = -1;

	/** The top of the action bar, so the preview and notices can sit on it. */
	float ActionBarTop = 0.0f;
	/** The action bar's dark strip, left and right (both 0 when it is not drawn), for the slim line along its top. */
	float ActionBarLeft = 0.0f;
	float ActionBarRight = 0.0f;
	/** The top and right edge of the enemy panel this frame (0 when none), so the boss bar can sit on it. */
	float InspectTop = 0.0f;
	float InspectRight = 0.0f;
	/** The boss whose bar was shown last frame and where, so the bar stays while the pointer is on it. */
	int32 BossBarUnitId = -1;
	FBox2D BossBarRect = FBox2D(ForceInit);
	/**
	 * The odds of the blow being aimed, on each unit it would reach ("Hit Preview
	 * Mockups" B, 2026-10-06): worked out in DrawBoardAids, drawn on the unit's own
	 * overhead bar in DrawOverheads. Cleared every frame.
	 */
	struct FTMAimOdds
	{
		float Hit = 0.0f, Crit = 0.0f, Graze = 0.0f, Dodge = 0.0f, Ko = 0.0f;
		int32 HitAmount = 0, CritAmount = 0, GrazeAmount = 0;
		int32 Evade = 0, CritChance = 0;
	};
	TMap<int32, FTMAimOdds> AimOdds;
	FString AimOddsExtra;
	/**
	 * Each unit's statuses as last seen (",burn,slow,") and until when its chips
	 * show beside its ring (2026-10-06: statuses only on highlighted units, or for a
	 * moment after one is put on).
	 */
	TMap<int32, TPair<FString, double>> StatusSeen;
};
