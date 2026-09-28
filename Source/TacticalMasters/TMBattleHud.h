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

#include "TMBattleHud.generated.h"

class ATMBattleDirector;
class UFont;

namespace TMSim
{
	struct FUnit;
	struct FVec2;
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

	// The setup screen. SetupClass's value is team * 4 + slot; the others' is the team.
	SetupClass,
	SetupRandom,
	SetupDefault,
	SetupSide,
	SetupDifficulty,
	SetupSeed,
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
	void DrawBattleMenu(ATMBattleDirector& Director);
	void DrawCornerButtons(ATMBattleDirector& Director);
	void DrawField(ATMBattleDirector& Director);
	void DrawInspectCard(ATMBattleDirector& Director);
	void DrawGuide(ATMBattleDirector& Director);
	void DrawClassPicker(ATMBattleDirector& Director);
	void DrawTooltip();

	/** Who is casting what at this unit, as a line per caster (hud.gd:1123-1150). Returns the height used. */
	float DrawIncoming(ATMBattleDirector& Director, const TMSim::FUnit& Unit, float X, float Y, float W);
	/** A unit's gauges and numbers, as the selected-unit card and the inspect card both show them. Returns the height used. */
	float DrawUnitBody(ATMBattleDirector& Director, const TMSim::FUnit& Unit, float X, float Y, float W);

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

	/** Pixels per design pixel: the layout is drawn for 1080 lines and scaled. */
	float S = 1.0f;

	TArray<FTMHudButton> Buttons;
	TArray<FTMHudTip> Tips;

	/** Where each turn chip is drawn, so it glides rather than jumps (hud.gd:1297-1311). */
	TMap<int32, FVector2D> ChipPlace;
	TMap<int32, float> ChipScale;

	/** The bottom of the log window, so the field list can sit under it. */
	float LogBottom = 0.0f;

	/** The top of the action bar, so the preview and notices can sit on it. */
	float ActionBarTop = 0.0f;
};
