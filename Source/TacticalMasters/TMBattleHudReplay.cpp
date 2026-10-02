// The Replays screen and the bar along the bottom while a replay is watched
// ("Battle Report Mockups" 4). The replays themselves: TMBattleDirectorReplay.cpp.

#include "TMBattleHud.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"

#include "SimMap.h"
#include "TMBattleDirector.h"
#include "TMBattleHudStyle.h"

using namespace TMHudStyle;

namespace TMReplayHud
{
	FString Clock(int32 Ticks)
	{
		const int32 Seconds = FMath::Max(0, Ticks) / TMSim::Pace::TicksPerSecond;
		return FString::Printf(TEXT("%d:%02d"), Seconds / 60, Seconds % 60);
	}

	FString MapName(const FString& Id)
	{
		const std::string Key = TCHAR_TO_UTF8(*Id);
		return TMSim::HasMap(Key) ? FString(UTF8_TO_TCHAR(TMSim::FindMap(Key).Name.c_str())) : Id;
	}

	FString Result(int32 Winner)
	{
		return Winner == 0 ? FString(TEXT("Blue won")) : Winner == 1 ? FString(TEXT("Red won"))
			: Winner == TMSim::FBattle::Draw ? FString(TEXT("A draw")) : FString(TEXT("Not decided"));
	}

	FString When(const FString& Iso)
	{
		FDateTime At;
		return FDateTime::ParseIso8601(*Iso, At) ? At.ToString(TEXT("%Y-%m-%d %H:%M")) : Iso;
	}

	FLinearColor MarkColour(const ATMBattleDirector::FTMReplayMark& Mark)
	{
		if (Mark.Kind == TEXT("revive"))
		{
			return FLinearColor(0.44f, 0.83f, 0.6f);
		}
		if (Mark.Kind == TEXT("camp"))
		{
			return FLinearColor(0.78f, 0.6f, 1.0f);
		}
		if (Mark.Kind == TEXT("won"))
		{
			return Gold;
		}
		// A fall or a tower in the colour of the side it went against or to.
		return Mark.Team == 0 || Mark.Team == 1 ? TeamColour(Mark.Kind == TEXT("ko") ? 1 - Mark.Team : Mark.Team) : Gold;
	}

	FString MarkWords(const FString& Who, const ATMBattleDirector::FTMReplayMark& Mark)
	{
		const FString At = Clock(Mark.Tick);
		if (Mark.Kind == TEXT("ko"))
		{
			return FString::Printf(TEXT("%s  %s falls"), *At, *Who);
		}
		if (Mark.Kind == TEXT("revive"))
		{
			return FString::Printf(TEXT("%s  %s is raised"), *At, *Who);
		}
		if (Mark.Kind == TEXT("tower"))
		{
			return FString::Printf(TEXT("%s  a watchtower is taken"), *At);
		}
		if (Mark.Kind == TEXT("camp"))
		{
			return FString::Printf(TEXT("%s  a camp is cleared"), *At);
		}
		return FString::Printf(TEXT("%s  the battle is decided"), *At);
	}
}

void ATMBattleHud::DrawReplayBar(ATMBattleDirector& From)
{
	if (!From.bReplaying)
	{
		return;
	}
	UFont* Font = GEngine->GetMediumFont();
	const float H = 118.0f * S;
	const float X = 24.0f * S;
	const float W = Canvas->ClipX - 48.0f * S;
	const float Y = Canvas->ClipY - H - 16.0f * S;
	Panel(X, Y, W, H, FLinearColor(0.04f, 0.05f, 0.09f, 0.94f), FLinearColor(1.0f, 0.82f, 0.35f, 0.45f), 1.0f);
	// Clicks on the bar's empty parts go nowhere else (the last button drawn wins, so this goes first).
	AddButton(X, Y, W, H, ETMHudAction::OverlayBlock, -1);

	// The timeline: how far along, the moments marked, and anywhere on it to jump to.
	const int32 Total = FMath::Max(1, From.ReplayTotalTicks());
	const float TX = X + 20.0f * S;
	const float TW = W - 40.0f * S;
	const float TY = Y + 22.0f * S;
	const float Fraction = FMath::Clamp(From.Battle.TickCount / static_cast<float>(Total), 0.0f, 1.0f);
	DrawRect(FLinearColor(0.17f, 0.2f, 0.27f, 1.0f), TX, TY, TW, 6.0f * S);
	DrawRect(Gold, TX, TY, TW * Fraction, 6.0f * S);
	DrawRect(Gold, TX + TW * Fraction - 4.0f * S, TY - 6.0f * S, 8.0f * S, 18.0f * S);
	for (const ATMBattleDirector::FTMReplayMark& Mark : From.Watching.Marks)
	{
		const float MX = TX + TW * FMath::Clamp(Mark.Tick / static_cast<float>(Total), 0.0f, 1.0f);
		const float Size = 9.0f * S;
		DrawRect(TMReplayHud::MarkColour(Mark), MX - Size * 0.5f, TY - 12.0f * S, Size, Size);
		AddTip(MX - Size, TY - 16.0f * S, Size * 2.0f, Size + 6.0f * S, TMReplayHud::MarkWords(Mark.Unit >= 0 ? From.NameOf(Mark.Unit) : FString(), Mark));
	}
	// A hundred strips along it, each a jump to its place.
	const int32 Strips = 100;
	for (int32 Index = 0; Index < Strips; ++Index)
	{
		AddButton(TX + TW * Index / Strips, TY - 8.0f * S, TW / Strips, 22.0f * S, ETMHudAction::ReplaySeek, (Index * 1000 + 500) / Strips);
	}

	// The buttons under it.
	const float BY = Y + 52.0f * S;
	const float BH = 44.0f * S;
	const float Gap = 8.0f * S;
	float BX = X + 20.0f * S;
	const bool bOver = From.Battle.Winner != -1 && From.ReplayCursor >= From.Watching.Steps.Num();
	MenuButton(BX, BY, 52.0f * S, BH, TEXT("|<"), ETMHudAction::ReplayStep, -1);
	BX += 52.0f * S + Gap;
	MenuButton(BX, BY, 110.0f * S, BH, bOver ? TEXT("Again") : From.bReplayPlaying ? TEXT("Pause") : TEXT("Play"), ETMHudAction::ReplayPlay, -1, true);
	BX += 110.0f * S + Gap;
	MenuButton(BX, BY, 52.0f * S, BH, TEXT(">|"), ETMHudAction::ReplayStep, 1);
	BX += 52.0f * S + Gap * 3.0f;
	static const TCHAR* SpeedNames[4] = { TEXT("1/2x"), TEXT("1x"), TEXT("2x"), TEXT("4x") };
	static const float Speeds[4] = { 0.5f, 1.0f, 2.0f, 4.0f };
	for (int32 Index = 0; Index < 4; ++Index)
	{
		MenuButton(BX, BY, 58.0f * S, BH, SpeedNames[Index], ETMHudAction::ReplaySpeed, Index, FMath::IsNearlyEqual(From.ReplaySpeed, Speeds[Index]));
		BX += 58.0f * S + 4.0f * S;
	}
	BX += Gap * 2.0f;
	const FString Time = FString::Printf(TEXT("%s / %s"), *TMReplayHud::Clock(From.Battle.TickCount), *TMReplayHud::Clock(Total));
	const FVector2D TimeSize = TextSize(Time, Font, 0.7f * S);
	Text(Time, BX, BY + (BH - TimeSize.Y) * 0.5f, TextColour, Font, 0.7f * S);
	BX += TimeSize.X + Gap * 3.0f;

	// Whose eyes: everything, or one side's fog.
	static const TCHAR* Views[3] = { TEXT("Everything"), TEXT("Blue's fog"), TEXT("Red's fog") };
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const int32 Value = Index - 1;
		MenuButton(BX, BY, 118.0f * S, BH, Views[Index], ETMHudAction::ReplayView, Value, From.ReplayView == Value);
		BX += 118.0f * S + 4.0f * S;
	}
	const float LW = 170.0f * S;
	MenuButton(X + W - LW - 20.0f * S, BY, LW, BH, TEXT("Leave replay  (Esc)"), ETMHudAction::ReplayLeave);

	// What it is, and the keys, on the bar's top edge.
	const FString Title = FString::Printf(TEXT("REPLAY   %s   Blue: %s   Red: %s   Space plays or pauses, Left and Right step, 1-4 speed"),
		*TMReplayHud::MapName(From.Watching.MapId), *From.Watching.Sides[0], *From.Watching.Sides[1]);
	Text(Title, X + 20.0f * S, Y + H - 20.0f * S, Dim, Font, 0.46f * S);

	// Why it may not be the battle it recorded.
	if (!From.ReplayProblem.IsEmpty())
	{
		const FVector2D Size = TextSize(From.ReplayProblem, Font, 0.62f * S);
		Panel(X, Y - Size.Y - 22.0f * S, Size.X + 32.0f * S, Size.Y + 14.0f * S, FLinearColor(0.12f, 0.04f, 0.04f, 0.92f), Urgent, 1.0f);
		Text(From.ReplayProblem, X + 16.0f * S, Y - Size.Y - 15.0f * S, Urgent, Font, 0.62f * S);
	}
}

void ATMBattleHud::DrawReplays(ATMBattleDirector& From)
{
	DrawRect(FLinearColor(0.02f, 0.03f, 0.06f, 0.7f), 0.0f, 0.0f, Canvas->ClipX, Canvas->ClipY);
	UFont* Font = GEngine->GetMediumFont();
	UFont* Big = GEngine->GetLargeFont();
	const float PW = FMath::Min(1200.0f * S, Canvas->ClipX - 40.0f * S);
	const float Row = 70.0f * S;
	const int32 PerPage = 8;
	const float PH = 210.0f * S + PerPage * Row;
	const float PX = (Canvas->ClipX - PW) * 0.5f;
	const float PY = FMath::Max(20.0f * S, (Canvas->ClipY - PH) * 0.5f);
	Panel(PX, PY, PW, PH, FLinearColor(0.05f, 0.06f, 0.1f, 0.96f), FLinearColor(0.4f, 0.45f, 0.55f, 0.6f), 1.0f);
	Text(TEXT("REPLAYS"), PX + 30.0f * S, PY + 22.0f * S, Gold, Big, 0.9f * S);
	Text(TEXT("Every battle that is decided is kept here, the newest 50. A replay plays the battle again from its orders."),
		PX + 30.0f * S, PY + 70.0f * S, Dim, Font, 0.55f * S);

	const int32 Count = From.ReplayList.Num();
	const int32 Pages = FMath::Max(1, (Count + PerPage - 1) / PerPage);
	const int32 Page = FMath::Clamp(From.ReplayListPage, 0, Pages - 1);
	float Y = PY + 110.0f * S;
	if (Count == 0)
	{
		Text(TEXT("No replays yet. Finish a battle and it appears here."), PX + 30.0f * S, Y + 20.0f * S, TextColour, Font, 0.66f * S);
	}
	for (int32 Index = Page * PerPage; Index < FMath::Min(Count, (Page + 1) * PerPage); ++Index)
	{
		const ATMBattleDirector::FTMReplayEntry& Entry = From.ReplayList[Index];
		const bool bOver = FBox2D(FVector2D(PX + 20.0f * S, Y), FVector2D(PX + PW - 20.0f * S, Y + Row - 6.0f * S)).IsInside(MousePoint());
		Panel(PX + 20.0f * S, Y, PW - 40.0f * S, Row - 6.0f * S,
			bOver ? FLinearColor(0.12f, 0.15f, 0.22f, 0.95f) : FLinearColor(0.08f, 0.1f, 0.15f, 0.9f));
		const FString Head = FString::Printf(TEXT("%s   %s"), *TMReplayHud::When(Entry.MadeAt), *TMReplayHud::MapName(Entry.MapId));
		Text(Head, PX + 36.0f * S, Y + 8.0f * S, TextColour, Font, 0.62f * S);
		const FString Under = FString::Printf(TEXT("Blue: %s   Red: %s   %s after %s"), *Entry.Sides[0], *Entry.Sides[1],
			*TMReplayHud::Result(Entry.Winner), *TMReplayHud::Clock(Entry.Ticks));
		const FLinearColor UnderColour = Entry.Winner == 0 || Entry.Winner == 1 ? TeamColour(Entry.Winner) : Dim;
		Text(Under, PX + 36.0f * S, Y + 36.0f * S, UnderColour, Font, 0.52f * S);
		const float BW = 120.0f * S;
		MenuButton(PX + PW - 40.0f * S - BW * 2.0f - 10.0f * S, Y + 10.0f * S, BW, Row - 26.0f * S, TEXT("Watch"), ETMHudAction::ReplayWatch, Index, true);
		MenuButton(PX + PW - 40.0f * S - BW, Y + 10.0f * S, BW, Row - 26.0f * S, TEXT("Delete"), ETMHudAction::ReplayDelete, Index);
		Y += Row;
	}

	const float BY = PY + PH - 70.0f * S;
	MenuButton(PX + 30.0f * S, BY, 160.0f * S, 48.0f * S, TEXT("Back"), ETMHudAction::ReplayListBack);
	if (Pages > 1)
	{
		const FString Where = FString::Printf(TEXT("Page %d of %d"), Page + 1, Pages);
		const FVector2D Size = TextSize(Where, Font, 0.6f * S);
		const float CX = PX + PW * 0.5f;
		ChoiceButton(CX - Size.X * 0.5f - 140.0f * S, BY, 120.0f * S, 48.0f * S, TEXT("Newer"), ETMHudAction::ReplayPage, -1, Page > 0);
		Text(Where, CX - Size.X * 0.5f, BY + (48.0f * S - Size.Y) * 0.5f, TextColour, Font, 0.6f * S);
		ChoiceButton(CX + Size.X * 0.5f + 20.0f * S, BY, 120.0f * S, 48.0f * S, TEXT("Older"), ETMHudAction::ReplayPage, 1, Page < Pages - 1);
	}
}
