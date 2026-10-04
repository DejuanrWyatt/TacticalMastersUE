// Options and Developer Tools, drawn over whatever screen they were opened from.
//
// Options (options_menu.gd): camera speed, UI scale, fullscreen, colour-blind
// team colours, and every key, each rebindable. Developer Tools (dev_tools.gd):
// a slider for every rule number, with Godot's own words for what it does;
// kept for the next battle, and in a battle fought here sent to the rules as
// an order once the dragging stops.

#include "TMBattleHud.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"

#include "TMBattleDirector.h"
#include "TMBattleHudStyle.h"
#include "TMSettings.h"

#include "SimAbility.h"

using namespace TMHudStyle;

void ATMBattleHud::Slider(float X, float Y, float W, float H, int32 Id, double Value, double Low, double High)
{
	const float Along = High > Low ? static_cast<float>(FMath::Clamp((Value - Low) / (High - Low), 0.0, 1.0)) : 0.0f;
	const float TrackH = FMath::Max(4.0f, H * 0.25f);
	const float TrackY = Y + (H - TrackH) * 0.5f;
	DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.55f), X, TrackY, W, TrackH);
	DrawRect(Gold * FLinearColor(1.0f, 1.0f, 1.0f, 0.8f), X, TrackY, W * Along, TrackH);
	const float Knob = H * 0.7f;
	DrawRect(TextColour, X + W * Along - Knob * 0.25f, Y + (H - Knob) * 0.5f, Knob * 0.5f, Knob);
	// The whole height answers the pointer, not only the thin track.
	AddButton(X, Y, W, H, ETMHudAction::Slider, Id);
	SliderAreas.Add(Id, FBox2D(FVector2D(X, Y), FVector2D(X + W, Y + H)));
}

bool* ATMBattleHud::FeelOption(int32 Which)
{
	FTMSettings& Settings = FTMSettings::Get();
	switch (Which)
	{
	case 0: return &Settings.bFastEnemyTurns;
	case 1: return &Settings.bAutoEndTurn;
	case 2: return &Settings.bCloseUps;
	case 3: return &Settings.bZoomToCursor;
	case 4: return &Settings.bEdgePan;
	case 5: return &Settings.bLeadCamera;
	case 6: return &Settings.bCameraHeldNote;
	default: return nullptr;
	}
}

void ATMBattleHud::CheckBox(float X, float Y, float H, bool bOn, const FString& Words, ETMHudAction Action, int32 Value, const FString& Tip)
{
	UFont* Font = GEngine->GetMediumFont();
	const float Box = H * 0.62f;
	const float BY = Y + (H - Box) * 0.5f;
	// A dark square with a light rim; ticked, a gold square inside it.
	DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.55f), X, BY, Box, Box);
	const FLinearColor Rim = bOn ? Gold : TextColour * FLinearColor(1.0f, 1.0f, 1.0f, 0.7f);
	const float T = FMath::Max(1.0f, 2.0f * S);
	DrawRect(Rim, X, BY, Box, T);
	DrawRect(Rim, X, BY + Box - T, Box, T);
	DrawRect(Rim, X, BY, T, Box);
	DrawRect(Rim, X + Box - T, BY, T, Box);
	if (bOn)
	{
		const float Inset = Box * 0.24f;
		DrawRect(Gold, X + Inset, BY + Inset, Box - Inset * 2.0f, Box - Inset * 2.0f);
	}
	const FVector2D Size = TextSize(Words, Font, 0.5f * S);
	Text(Words, X + Box + 8.0f * S, Y + (H - Size.Y) * 0.5f, bOn ? Gold : TextColour, Font, 0.5f * S);
	// The box and its words both answer.
	AddButton(X, Y, Box + 12.0f * S + Size.X, H, Action, Value);
	if (!Tip.IsEmpty())
	{
		AddTip(X, Y, Box + 12.0f * S + Size.X, H, Tip);
	}
}

void ATMBattleHud::DrawOptions(ATMBattleDirector& From)
{
	FTMSettings& Settings = FTMSettings::Get();
	UFont* Font = GEngine->GetMediumFont();
	UFont* Big = GEngine->GetLargeFont();
	DrawRect(FLinearColor(0.02f, 0.03f, 0.05f, 0.92f), 0.0f, 0.0f, Canvas->ClipX, Canvas->ClipY);
	const float PX = 60.0f * S;
	// The page is taller than a small window: the wheel scrolls it under a
	// fixed title row (2026-10-01: "cannot scroll down in options menu").
	const float Top = 110.0f * S;
	const float Scrolled = From.OptionsScroll * S;
	float Y = Top - Scrolled;

	// Game.
	Text(TEXT("Game"), PX, Y, Gold, Font, 0.8f * S);
	Y += 36.0f * S;
	const float LabelW = 480.0f * S;
	const float RowH = 40.0f * S;
	const float ControlW = 320.0f * S;
	auto Label = [&](const FString& Name, float At, float LX, const FString& Tip)
	{
		Text(Name, LX, At + 8.0f * S, TextColour, Font, 0.62f * S);
		if (!Tip.IsEmpty())
		{
			AddTip(LX, At, LabelW, RowH, Tip);
		}
	};
	Label(TEXT("Camera speed"), Y, PX, TEXT("How fast the keys pan and turn the camera."));
	Slider(PX + LabelW, Y + 6.0f * S, ControlW, RowH - 12.0f * S, ATMBattleDirector::SliderCameraSpeed, Settings.CameraSpeed, 0.5, 2.0);
	Text(FString::Printf(TEXT("%.2f"), Settings.CameraSpeed), PX + LabelW + ControlW + 16.0f * S, Y + 8.0f * S, TextColour, Font, 0.62f * S);
	Y += RowH + 8.0f * S;
	Label(TEXT("Overhead bar size"), Y, PX, TEXT("How big each unit's name and health bar are drawn over its head."));
	Slider(PX + LabelW, Y + 6.0f * S, ControlW, RowH - 12.0f * S, ATMBattleDirector::SliderOverhead, Settings.OverheadScale, 0.6, 2.5);
	Text(FString::Printf(TEXT("%.2fx"), Settings.OverheadScale), PX + LabelW + ControlW + 16.0f * S, Y + 8.0f * S, TextColour, Font, 0.62f * S);
	Y += RowH + 8.0f * S;
	Label(TEXT("Status icon size"), Y, PX, TEXT("How big the status icons under each unit's health bar are drawn."));
	Slider(PX + LabelW, Y + 6.0f * S, ControlW, RowH - 12.0f * S, ATMBattleDirector::SliderStatusIcons, Settings.StatusIconScale, 0.6, 3.0);
	Text(FString::Printf(TEXT("%.2fx"), Settings.StatusIconScale), PX + LabelW + ControlW + 16.0f * S, Y + 8.0f * S, TextColour, Font, 0.62f * S);
	Y += RowH + 8.0f * S;
	Label(TEXT("UI scale"), Y, PX, TEXT("How big the menus and battle panels are drawn."));
	MenuButton(PX + LabelW, Y, 160.0f * S, RowH, FString::Printf(TEXT("%.0f%%"), Settings.UiScale * 100.0f), ETMHudAction::OptionUiScale);
	Y += RowH + 8.0f * S;
	Label(TEXT("Fullscreen"), Y, PX, FString());
	MenuButton(PX + LabelW, Y, 160.0f * S, RowH, Settings.bFullscreen ? TEXT("On") : TEXT("Off"), ETMHudAction::OptionFullscreen);
	Y += RowH + 8.0f * S;
	Label(TEXT("Colorblind team colors (blue / orange)"), Y, PX, TEXT("Red becomes orange, for red-green colour blindness."));
	MenuButton(PX + LabelW, Y, 160.0f * S, RowH, Settings.bColorblind ? TEXT("On") : TEXT("Off"), ETMHudAction::OptionColorblind);
	Y += RowH + 8.0f * S;
	Label(TEXT("Turn order as fixed squares instead of sliding bars"), Y, PX,
		TEXT("A square per unit, filling as its turn comes, instead of chips sliding along two bars. Move and reorder them in Edit layout."));
	MenuButton(PX + LabelW, Y, 160.0f * S, RowH, Settings.bTurnSquares ? TEXT("On") : TEXT("Off"), ETMHudAction::OptionTurnSquares);
	Y += RowH + 8.0f * S;
	Label(TEXT("Squad strip down the left edge"), Y, PX,
		TEXT("Your units with their health, statuses (and turns left on each) and cooldowns. The one acting opens to its full row; so does one you point at."));
	MenuButton(PX + LabelW, Y, 160.0f * S, RowH, Settings.bSquadStrip ? TEXT("On") : TEXT("Off"), ETMHudAction::OptionSquadStrip);
	Y += RowH + 8.0f * S;
	Label(FString::Printf(TEXT("Camera follows to the next ready unit  (%s cycles)"), *Settings.KeyName(ETMAction::AutoRecenter)), Y, PX,
		TEXT("When one of your units ends its turn and the next is ready at once, the camera may slide to it (only if it is off screen). Always: at once, but never while you are aiming, planning or dragging. When I'm idle: also not within 1.5 s of a key or a click. Never: only you move it. A unit that becomes ready while you are busy with another never takes it: it is pointed out instead."));
	{
		static const TCHAR* const Ways[3] = { TEXT("Always"), TEXT("When I'm idle"), TEXT("Never") };
		const float WayW[3] = { 110.0f * S, 170.0f * S, 100.0f * S };
		float WX = PX + LabelW;
		for (int32 Way = 0; Way < 3; ++Way)
		{
			MenuButton(WX, Y, WayW[Way], RowH, Ways[Way], ETMHudAction::OptionAutoRecenter, Way, Settings.CameraFollow == Way);
			WX += WayW[Way] + 6.0f * S;
		}
	}
	Y += RowH + 8.0f * S;
	// How the battle feels to play (2026-10-03, TMBattleDirectorFeel.cpp).
	struct FFeelRow
	{
		FString Name;
		const TCHAR* Tip;
	};
	const FFeelRow FeelRows[] =
	{
		{ FString::Printf(TEXT("Fast-forward the other side's turns  (hold %s any time)"), *Settings.KeyName(ETMAction::FastForward)),
			TEXT("While none of your units is ready, the battle runs three times as fast: the enemy's turns, the waits. It never runs fast while one of yours has a turn. Not online, where time is the host's.") },
		{ TEXT("End a unit's turn by itself when it has nothing left to use"),
			TEXT("A unit that has walked and has no ability it could use now (all cooling down or blocked) ends its turn, rather than leaving you to press End turn. Not beside a chest or a tower it could take. Online, for the host only for now.") },
		{ TEXT("Close-ups on ultimates"),
			TEXT("The camera closes in on whoever uses an ultimate and what it is aimed at, for a moment, then goes back. Any key or click skips it.") },
		{ TEXT("Zoom toward the pointer"),
			TEXT("The mouse wheel zooms toward what the pointer is on. Off, it zooms toward the middle of the screen.") },
		{ TEXT("Pan at the edge of the window"),
			TEXT("Resting the pointer at the edge of the window pans the camera that way, gently at first.") },
		{ TEXT("Camera goes ahead of a walk to the edge of the screen"),
			TEXT("A walk whose end is near the edge of the screen, or off it, brings the camera halfway along.") },
		{ TEXT("Say \"Camera held\" when the camera waits for you"),
			TEXT("When the camera wanted to follow but you were busy, a small note says so; it follows once you are done, if that is soon.") },
	};
	for (int32 Row = 0; Row < static_cast<int32>(UE_ARRAY_COUNT(FeelRows)); ++Row)
	{
		const bool* On = FeelOption(Row);
		Label(FeelRows[Row].Name, Y, PX, FeelRows[Row].Tip);
		MenuButton(PX + LabelW, Y, 160.0f * S, RowH, On && *On ? TEXT("On") : TEXT("Off"), ETMHudAction::OptionFeel, Row);
		Y += RowH + 8.0f * S;
	}
	Label(TEXT("Sound effects volume"), Y, PX, TEXT("How loud swings, spells, hits, footsteps and the menus are."));
	Slider(PX + LabelW, Y + 6.0f * S, ControlW, RowH - 12.0f * S, ATMBattleDirector::SliderSfxVolume, Settings.SfxVolume, 0.0, 1.0);
	Text(FString::Printf(TEXT("%.0f%%"), Settings.SfxVolume * 100.0f), PX + LabelW + ControlW + 16.0f * S, Y + 8.0f * S, TextColour, Font, 0.62f * S);
	Y += RowH + 8.0f * S;
	Label(TEXT("Voices volume"), Y, PX, TEXT("How loud the heroes are: their efforts, cries and cheers."));
	Slider(PX + LabelW, Y + 6.0f * S, ControlW, RowH - 12.0f * S, ATMBattleDirector::SliderVoiceVolume, Settings.VoiceVolume, 0.0, 1.0);
	Text(FString::Printf(TEXT("%.0f%%"), Settings.VoiceVolume * 100.0f), PX + LabelW + ControlW + 16.0f * S, Y + 8.0f * S, TextColour, Font, 0.62f * S);
	Y += RowH + 8.0f * S;
	Label(TEXT("Damage number size"), Y, PX, TEXT("How big the damage and healing numbers rising off units are."));
	Slider(PX + LabelW, Y + 6.0f * S, ControlW, RowH - 12.0f * S, ATMBattleDirector::SliderDamageText, Settings.DamageTextScale, 0.75, 3.0);
	Text(FString::Printf(TEXT("%.2fx"), Settings.DamageTextScale), PX + LabelW + ControlW + 16.0f * S, Y + 8.0f * S, TextColour, Font, 0.62f * S);
	Y += RowH + 16.0f * S;

	// Controls.
	Text(TEXT("Controls"), PX, Y, Gold, Font, 0.8f * S);
	Y += 32.0f * S;
	Text(TEXT("Click a key, then press the new key (Esc cancels). A key already used by another action is swapped between the two."),
		PX, Y, Dim, Font, 0.55f * S);
	Y += 30.0f * S;
	const int32 Count = static_cast<int32>(ETMAction::Count);
	const int32 PerColumn = (Count + 1) / 2;
	const float ColumnW = (Canvas->ClipX - PX * 2.0f) * 0.5f;
	const float KeyRowH = 34.0f * S;
	for (int32 i = 0; i < Count; ++i)
	{
		const ETMAction Action = static_cast<ETMAction>(i);
		const float CX = PX + (i / PerColumn) * ColumnW;
		const float CY = Y + (i % PerColumn) * (KeyRowH + 4.0f * S);
		Text(FTMSettings::Info(Action).Label, CX, CY + 6.0f * S, TextColour, Font, 0.55f * S);
		FString Names;
		for (const FKey& Key : Settings.Keys(Action))
		{
			Names += (Names.IsEmpty() ? TEXT("") : TEXT(" / ")) + Key.GetDisplayName(false).ToString();
		}
		MenuButton(CX + 280.0f * S, CY, 240.0f * S, KeyRowH, From.CaptureAction == i ? FString(TEXT("Press a key...")) : Names,
			ETMHudAction::RebindAction, i, From.CaptureAction == i);
		// Quick Cast, next to each ability's key (2026-10-03).
		const int32 Slot = i - static_cast<int32>(ETMAction::Ability1);
		if (Slot >= 0 && Slot < 4)
		{
			CheckBox(CX + 532.0f * S, CY, KeyRowH, Settings.bQuickCast[Slot], TEXT("Quick Cast"), ETMHudAction::OptionQuickCast, Slot,
				TEXT("Quick Cast: hold the key to aim the ability, let go to use it where the pointer is -- no click. Let go over a button to put it down. Off: the key aims, a click uses it."));
		}
	}
	Y += PerColumn * (KeyRowH + 4.0f * S) + 12.0f * S;
	Text(TEXT("Mouse (fixed): left-click select / move / target · right-click cancel · right-drag rotate and tilt camera · middle-drag pan · wheel zoom (or scroll the log)"),
		PX, Y, Dim, Font, 0.52f * S);
	Y += 40.0f * S;

	// Keep the scroll in range, now the page's height is known.
	const float Whole = Y + Scrolled - Top;
	const float Shown = Canvas->ClipY - Top;
	const float Most = FMath::Max(0.0f, (Whole - Shown) / S);
	From.OptionsScroll = FMath::Clamp(From.OptionsScroll, 0.0f, Most);
	if (Most > 0.0f)
	{
		ScrollBar(Canvas->ClipX - 24.0f * S, Top, Shown - 10.0f * S, Shown, Whole, Scrolled);
	}

	// The title row, over whatever has scrolled under it; it catches clicks there too.
	DrawRect(FLinearColor(0.02f, 0.03f, 0.05f, 1.0f), 0.0f, 0.0f, Canvas->ClipX, Top - 6.0f * S);
	AddButton(0.0f, 0.0f, Canvas->ClipX, Top - 6.0f * S, ETMHudAction::OverlayBlock, -1);
	const float TitleY = 40.0f * S;
	Text(TEXT("Options"), PX, TitleY, Gold, Big, 1.0f * S);
	if (Most > 0.0f)
	{
		Text(TEXT("Scroll with the mouse wheel"), PX + 220.0f * S, TitleY + 22.0f * S, Dim, Font, 0.5f * S);
	}
	const float BW = 200.0f * S;
	MenuButton(Canvas->ClipX - PX - BW, TitleY, BW, 44.0f * S, TEXT("Close  (Esc)"), ETMHudAction::CloseOverlay, -1, true);
	MenuButton(Canvas->ClipX - PX - BW * 2.0f - 12.0f * S, TitleY, BW, 44.0f * S, TEXT("Reset to defaults"), ETMHudAction::ResetOptions);
}

void ATMBattleHud::DrawDevTools(ATMBattleDirector& From)
{
	UFont* Font = GEngine->GetMediumFont();
	UFont* Big = GEngine->GetLargeFont();
	DrawRect(FLinearColor(0.02f, 0.03f, 0.05f, 0.94f), 0.0f, 0.0f, Canvas->ClipX, Canvas->ClipY);
	const float PX = 50.0f * S;
	float Y = 36.0f * S;
	Text(TEXT("Developer Tools"), PX, Y, Gold, Big, 1.0f * S);
	const float BW = 200.0f * S;
	MenuButton(Canvas->ClipX - PX - BW, Y, BW, 44.0f * S, TEXT("Close  (Esc)"), ETMHudAction::CloseOverlay, -1, true);
	MenuButton(Canvas->ClipX - PX - BW * 2.0f - 12.0f * S, Y, BW, 44.0f * S, TEXT("Reset all"), ETMHudAction::DevResetAll);
	Y += 60.0f * S;
	const bool bLive = From.Screen == ATMBattleDirector::EScreen::Battle && From.Battle.Winner == -1;
	Text(bLive
			? TEXT("Kept for the next battle, and applied to this one at once: each change goes to the rules as a recorded order, so a replay has it too.")
			: TEXT("Kept for the next battle, and after the game is closed. Rest the pointer on a name for what it does. Victory, time limits, watchtowers, items, camps, elements and friendly fire are on the battle setup screen, which is kept too."),
		PX, Y, Dim, Font, 0.55f * S);
	Y += 34.0f * S;

	// Every rule number but the three the setup screen owns, in two columns.
	TArray<int32> Rows;
	for (int32 i = 0; i < static_cast<int32>(TMSim::TuningKeys().size()); ++i)
	{
		if (!ATMBattleDirector::OnSetupScreen(i))
		{
			Rows.Add(i);
		}
	}
	const int32 PerColumn = (Rows.Num() + 1) / 2;
	const float ColumnW = (Canvas->ClipX - PX * 2.0f) * 0.5f;
	const float RowH = FMath::Min(34.0f * S, (Canvas->ClipY - Y - 60.0f * S) / FMath::Max(1, PerColumn));
	// The game's defaults, not the rules' Godot ones, so only real changes show gold.
	const TMSim::FTuning Defaults = TMSim::GameTuning();
	for (int32 Row = 0; Row < Rows.Num(); ++Row)
	{
		const int32 Index = Rows[Row];
		const TMSim::FTuningKey& Key = TMSim::TuningKeys()[static_cast<size_t>(Index)];
		const float CX = PX + (Row / PerColumn) * ColumnW;
		const float CY = Y + (Row % PerColumn) * RowH;
		const int32 Id = ATMBattleDirector::SliderTuning + Index;
		const double Value = From.SliderValue(Id);
		const double Default = Defaults.*Key.Member;
		const bool bChanged = FMath::Abs(Value - Default) > 1e-9;
		Text(UTF8_TO_TCHAR(Key.Label), CX, CY + 4.0f * S, bChanged ? Gold : TextColour, Font, 0.52f * S);
		AddTip(CX, CY, 260.0f * S, RowH, FString::Printf(TEXT("%hs  Default %s."), Key.Desc,
			*FString::SanitizeFloat(Default)));
		const float SliderX = CX + 270.0f * S;
		const float SliderW = ColumnW - 270.0f * S - 170.0f * S;
		Slider(SliderX, CY + RowH * 0.15f, SliderW, RowH * 0.7f, Id, Value, Key.Low, Key.High);
		const FString Shown = Key.Step >= 1.0 ? FString::Printf(TEXT("%.0f"), Value) : FString::Printf(TEXT("%.2f"), Value);
		Text(Shown, SliderX + SliderW + 10.0f * S, CY + 4.0f * S, bChanged ? Gold : TextColour, Font, 0.52f * S);
		MenuButton(SliderX + SliderW + 80.0f * S, CY + RowH * 0.1f, 70.0f * S, RowH * 0.8f, TEXT("Reset"), ETMHudAction::DevReset, Index);
	}
	Y += PerColumn * RowH + 14.0f * S;
	Text(FString::Printf(TEXT("Classes: %d, made in the class creator (E:\\TacticsClassCreator) and read from Content/Data/Classes when the game starts."),
		static_cast<int32>(TMSim::AllJobs().size())), PX, Y, Dim, Font, 0.52f * S);
}
