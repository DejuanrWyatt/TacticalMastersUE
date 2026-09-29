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
	struct FVec2;
	struct FAbility;
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
	NewBattle,

	// The title screen.
	TitleVsComputer,
	TitleTwoPlayers,
	TitleWatch,
	Quit,
	// Online (main_menu.gd:79-230): the title's button, then host or join.
	// OnlineField's value is the field to type in (ATMBattleDirector::ETypeField).
	TitleOnline,
	OnlineHost,
	OnlineJoin,
	OnlineField,
	OnlineBack,

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
};

/** Words shown when the pointer rests on part of the HUD: how a number is worked out. */
struct FTMHudTip
{
	FBox2D Area;
	FString Text;
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

private:
	ATMBattleDirector* FindDirector();

	// The parts, top to bottom.
	void DrawBoardAids(ATMBattleDirector& Director);
	void DrawTurnOrder(ATMBattleDirector& Director);
	void DrawLog(ATMBattleDirector& Director);
	void DrawUnitCard(ATMBattleDirector& Director);
	void DrawActionBar(ATMBattleDirector& Director);
	void DrawBanners(ATMBattleDirector& Director);
	void DrawTitle(ATMBattleDirector& Director);
	void DrawSetup(ATMBattleDirector& Director);
	/** Host or join a match against another machine (main_menu.gd:79-230). */
	void DrawOnline(ATMBattleDirector& From);
	/** A box to type in: click to type, and a caret while typing. */
	void TextField(float X, float Y, float W, float H, const FString& Value, const FString& Placeholder, bool bTyping, int32 Field);
	void DrawBattleMenu(ATMBattleDirector& Director);
	void DrawCornerButtons(ATMBattleDirector& Director);
	void DrawField(ATMBattleDirector& Director);
	void DrawInspectCard(ATMBattleDirector& Director);
	void DrawGuide(ATMBattleDirector& Director);
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
	void DrawTooltip();
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
	/** A row of status chips. Returns the width used. Right to left from X when bLeftward. */
	float StatusChips(const TMSim::FUnit& Unit, float X, float Y, float Size, bool bLeftward, bool bTips);
	/** An ability's icon tile: coloured when it can be used, grey with the turns left when it cannot. */
	void AbilityTile(ATMBattleDirector& Director, const TMSim::FUnit& Unit, int32 Slot, float X, float Y, float Size, bool bButton);
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
	static FString BuffText(const TMSim::FUnit& Unit);

	void AddTip(float X, float Y, float W, float H, const FString& Tip);

	/** A button on a menu: a label, an optional second line, and a highlight under the pointer. */
	void MenuButton(float X, float Y, float W, float H, const FString& Label, ETMHudAction Action, int32 Value = -1,
		bool bPrimary = false, const FString& Detail = FString());
	/** Where the pointer is, or off the screen. */
	FVector2D MousePoint() const;

	// Drawing helpers, in pixels already scaled.
	void Panel(float X, float Y, float W, float H, const FLinearColor& Fill, const FLinearColor& Edge = FLinearColor::Transparent, float Thickness = 1.0f);
	void Text(const FString& What, float X, float Y, const FLinearColor& Colour, UFont* Font, float Scale = 1.0f, bool bShadow = true);
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
	TMap<int32, float> ChipScale;

	/** The bottom of the log window, so the field list can sit under it. */
	float LogBottom = 0.0f;

	/** The top of the action bar, so the preview and notices can sit on it. */
	float ActionBarTop = 0.0f;
};
