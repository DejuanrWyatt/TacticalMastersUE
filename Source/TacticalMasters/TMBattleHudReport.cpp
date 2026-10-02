// The battle report at the end of a battle ("Battle Report Mockups" 1-3): the
// MVP and why, every unit's numbers by tab, one unit's battle, and the moments
// to watch again. The numbers: TMBattleDirectorReport.cpp.

#include "TMBattleHud.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"

#include "SimAbility.h"
#include "SimMap.h"
#include "TMBattleDirector.h"
#include "TMBattleHudStyle.h"
#include "TMSettings.h"

using namespace TMHudStyle;

namespace TMReportHud
{
	FString Number(int32 Value)
	{
		return FText::AsNumber(Value).ToString();
	}

	FString Clock(int32 Ticks)
	{
		const int32 Seconds = FMath::Max(0, Ticks) / TMSim::Pace::TicksPerSecond;
		return FString::Printf(TEXT("%d:%02d"), Seconds / 60, Seconds % 60);
	}

	/** S for the MVP, A within 15% of its points, B within 30%, C the rest. */
	FString Grade(double Score, double Best, bool bMvp)
	{
		return bMvp ? FString(TEXT("S")) : Score >= Best * 0.85 ? FString(TEXT("A")) : Score >= Best * 0.7 ? FString(TEXT("B")) : FString(TEXT("C"));
	}

	FLinearColor GradeColour(const FString& Grade)
	{
		return Grade == TEXT("S") ? Gold : Grade == TEXT("A") ? FLinearColor(0.44f, 0.83f, 0.6f)
			: Grade == TEXT("B") ? FLinearColor(0.62f, 0.85f, 1.0f) : Dim;
	}

	FString ReportInitials(const FString& Name)
	{
		TArray<FString> Words;
		Name.ParseIntoArray(Words, TEXT(" "));
		FString Out;
		for (const FString& Word : Words)
		{
			Out += Word.Left(1).ToUpper();
		}
		return Out.Left(2).Len() == 2 ? Out.Left(2) : Name.Left(2).ToUpper();
	}

	FString RoleWord(const TMSim::FUnit& Unit)
	{
		const TMSim::FJobDef* Job = TMSim::FindJob(Unit.Job);
		return Job && !Job->Roles.empty() ? FString(UTF8_TO_TCHAR(Job->Roles[0].c_str())) : FString();
	}

	/** One column: its heading, the value shown, and the number its bar is measured by (-1 for none). */
	struct FColumn
	{
		const TCHAR* Heading;
		TFunction<FString(const ATMBattleDirector::FTMUnitTally&, double)> Shown;
		TFunction<int32(const ATMBattleDirector::FTMUnitTally&)> Measure;
	};
}

void ATMBattleHud::DrawBattleReport(ATMBattleDirector& From, const FString& Line, const FLinearColor& Colour)
{
	using namespace TMReportHud;
	UFont* Font = GEngine->GetMediumFont();
	UFont* Big = GEngine->GetLargeFont();
	// The whole report fits the window, whatever the UI scale.
	const float WasS = S;
	S = FMath::Min(S, FMath::Min((Canvas->ClipY - 24.0f) / 900.0f, (Canvas->ClipX - 24.0f) / 1440.0f));
	ON_SCOPE_EXIT
	{
		S = WasS;
	};
	DrawRect(FLinearColor(0.02f, 0.03f, 0.06f, 0.6f), 0.0f, 0.0f, Canvas->ClipX, Canvas->ClipY);
	const float PW = 1440.0f * S;
	const float PH = 880.0f * S;
	const float PX = (Canvas->ClipX - PW) * 0.5f;
	const float PY = (Canvas->ClipY - PH) * 0.5f;
	Panel(PX, PY, PW, PH, FLinearColor(0.05f, 0.07f, 0.11f, 0.97f), Colour, 2.0f);
	AddButton(PX, PY, PW, PH, ETMHudAction::OverlayBlock, -1);

	// The units of the two sides, and the best of them.
	TArray<const TMSim::FUnit*> Sides[2];
	for (const TMSim::FUnit& Unit : From.Battle.Units)
	{
		if (From.Tallies.Contains(Unit.Id) && (Unit.Team == 0 || Unit.Team == 1))
		{
			Sides[Unit.Team].Add(&Unit);
		}
	}
	const int32 Mvp = From.MvpId();
	const double Best = Mvp >= 0 ? FMath::Max(0.1, ATMBattleDirector::ScoreOf(From.Tallies[Mvp])) : 1.0;
	auto ScoreOf = [&From](int32 Id) { return ATMBattleDirector::ScoreOf(From.Tallies[Id]); };
	for (int32 Team = 0; Team < 2; ++Team)
	{
		Sides[Team].Sort([&ScoreOf](const TMSim::FUnit& A, const TMSim::FUnit& B) { return ScoreOf(A.Id) > ScoreOf(B.Id); });
	}

	// The head: the result, the map and time, and where to go.
	const float Pad = 28.0f * S;
	Text(Line, PX + Pad, PY + 18.0f * S, Colour, Big, 1.1f * S);
	const FString MapName = UTF8_TO_TCHAR(TMSim::FindMap(From.Setup.MapId).Name.c_str());
	const FString How = From.HowWon();
	const FString Sub = FString::Printf(TEXT("%s   %s   %s%s"), *MapName, *Clock(From.Battle.TickCount),
		How.IsEmpty() ? TEXT("") : *(How + TEXT("   ")), From.bReplaying ? TEXT("replay") : *FString::Printf(TEXT("seed %llu"), From.BattleSeed));
	Text(Sub, PX + Pad, PY + 72.0f * S, Dim, Font, 0.62f * S);

	const float BH = 44.0f * S;
	const float BY = PY + 22.0f * S;
	float BX = PX + PW - Pad;
	auto Right = [this, &BX, BY, BH](float W, const FString& Label, ETMHudAction Action, int32 Value, bool bPrimary)
	{
		BX -= W;
		MenuButton(BX, BY, W, BH, Label, Action, Value, bPrimary);
		BX -= 10.0f * S;
	};
	if (From.bPlayerInput)
	{
		Right(150.0f * S, TEXT("Main menu"), ETMHudAction::MenuTitle, -1, false);
		if (From.bReplaying)
		{
			Right(130.0f * S, TEXT("Replays"), ETMHudAction::ReplayLeave, -1, false);
			Right(160.0f * S, TEXT("Watch again"), ETMHudAction::ReplayAgain, -1, true);
		}
		else if (From.bOnline)
		{
			const bool bHost = From.Net.IsValid() && From.Net->IsHost();
			if (bHost)
			{
				Right(200.0f * S, TEXT("Back to lobby  (R)"), ETMHudAction::NewBattle, -1, true);
			}
			Right(160.0f * S, TEXT("Watch replay"), ETMHudAction::ReplayWatch, -1, false);
		}
		else
		{
			Right(160.0f * S, TEXT("Change setup"), ETMHudAction::MenuSetup, -1, false);
			Right(150.0f * S, TEXT("Rematch  (R)"), ETMHudAction::NewBattle, -1, false);
			Right(160.0f * S, TEXT("Watch replay"), ETMHudAction::ReplayWatch, -1, true);
		}
		Right(140.0f * S, TEXT("See the board"), ETMHudAction::ReportHide, -1, false);
	}

	const float Top = PY + 110.0f * S;
	const float LeftW = 360.0f * S;

	// ---- the MVP
	if (Mvp >= 0)
	{
		const TMSim::FUnit* Star = From.Battle.FindUnit(Mvp);
		const ATMBattleDirector::FTMUnitTally& T = From.Tallies[Mvp];
		const float CX = PX + Pad;
		const float CH = 440.0f * S;
		Panel(CX, Top, LeftW, CH, FLinearColor(1.0f, 0.82f, 0.35f, 0.07f), FLinearColor(1.0f, 0.82f, 0.35f, 0.5f), 1.0f);
		const FString Name = Star ? JobName(*Star) : FString();
		const float Ring = 76.0f * S;
		Panel(CX + 18.0f * S, Top + 18.0f * S, Ring, Ring, FLinearColor(0.12f, 0.16f, 0.24f, 1.0f), Gold, 2.0f);
		const FString Letters = ReportInitials(Name);
		const FVector2D LetterSize = TextSize(Letters, Big, 0.8f * S);
		Text(Letters, CX + 18.0f * S + (Ring - LetterSize.X) * 0.5f, Top + 18.0f * S + (Ring - LetterSize.Y) * 0.5f, TextColour, Big, 0.8f * S);
		Text(TEXT("MVP"), CX + Ring + 34.0f * S, Top + 20.0f * S, Gold, Font, 0.55f * S);
		Text(Name, CX + Ring + 34.0f * S, Top + 40.0f * S, TextColour, Big, 0.72f * S);
		if (Star)
		{
			Text(FString(Star->Team == 0 ? TEXT("Blue") : TEXT("Red")) + (RoleWord(*Star).IsEmpty() ? FString() : FString(TEXT(" · ")) + RoleWord(*Star)),
				CX + Ring + 34.0f * S, Top + 76.0f * S, TeamColour(Star->Team), Font, 0.55f * S);
		}
		Text(TEXT("S"), CX + LeftW - 48.0f * S, Top + 16.0f * S, Gold, Big, 1.0f * S);
		Text(FString::Printf(TEXT("%.1f"), ATMBattleDirector::ScoreOf(T)), CX + LeftW - 60.0f * S, Top + 66.0f * S, Dim, Font, 0.52f * S);

		// Where the points came from, biggest first.
		struct FPart { const TCHAR* Label; double Points; };
		TArray<FPart> Parts = {
			{ TEXT("Damage"), T.Damage * 0.1 }, { TEXT("Damage taken"), T.Taken * 0.04 }, { TEXT("Mitigated"), T.Mitigated * 0.03 },
			{ TEXT("Healing"), T.Healing * 0.1 }, { TEXT("Knock-outs"), T.Kills * 12.0 }, { TEXT("Assists"), T.Assists * 5.0 },
			{ TEXT("Knocked out"), T.Deaths * -10.0 }, { TEXT("Monsters"), T.Monsters * 4.0 }, { TEXT("Boss"), T.Bosses * 15.0 },
			{ TEXT("Buffs"), T.Buffs * 2.0 }, { TEXT("Debuffs"), T.Debuffs * 2.0 }, { TEXT("Control"), T.Control * 3.0 },
			{ TEXT("Revives"), T.Revives * 10.0 }, { TEXT("Towers"), T.Towers * 8.0 } };
		Parts.RemoveAll([](const FPart& Part) { return FMath::Abs(Part.Points) < 0.5; });
		Parts.Sort([](const FPart& A, const FPart& B) { return A.Points > B.Points; });
		double Most = 1.0;
		for (const FPart& Part : Parts)
		{
			Most = FMath::Max(Most, FMath::Abs(Part.Points));
		}
		// Two short lines (2026-10-02: one long line ran out of the card).
		const FString SaidDamage = FString::Printf(TEXT("%s damage   %s taken   %s mitigated   %s healed"),
			*Number(T.Damage), *Number(T.Taken), *Number(T.Mitigated), *Number(T.Healing));
		const FString SaidFights = FString::Printf(TEXT("%d knock-out%s   %d assist%s   %d fell"),
			T.Kills, T.Kills == 1 ? TEXT("") : TEXT("s"), T.Assists, T.Assists == 1 ? TEXT("") : TEXT("s"), T.Deaths);
		Text(SaidDamage, CX + 18.0f * S, Top + 106.0f * S, TextColour, Font, 0.48f * S);
		Text(SaidFights, CX + 18.0f * S, Top + 128.0f * S, TextColour, Font, 0.48f * S);
		float RY = Top + 162.0f * S;
		for (int32 Index = 0; Index < FMath::Min(Parts.Num(), 7); ++Index)
		{
			const FPart& Part = Parts[Index];
			Text(Part.Label, CX + 18.0f * S, RY, Dim, Font, 0.5f * S);
			const float BarX = CX + 140.0f * S;
			const float BarW = LeftW - 210.0f * S;
			DrawRect(FLinearColor(1.0f, 1.0f, 1.0f, 0.06f), BarX, RY + 6.0f * S, BarW, 8.0f * S);
			DrawRect(Part.Points < 0.0 ? FLinearColor(0.69f, 0.48f, 0.48f) : Gold, BarX, RY + 6.0f * S, BarW * FMath::Abs(Part.Points) / Most, 8.0f * S);
			Text(FString::Printf(TEXT("%+.0f"), Part.Points), CX + LeftW - 56.0f * S, RY, TextColour, Font, 0.5f * S);
			RY += 30.0f * S;
		}
		if (Star)
		{
			const float GearY = Top + CH - 50.0f * S;
			Text(TEXT("Carried"), CX + 18.0f * S, GearY + 8.0f * S, Dim, Font, 0.5f * S);
			ReportGear(*Star, CX + 140.0f * S, GearY, 34.0f * S);
		}
		AddTip(CX, Top, LeftW, CH - 56.0f * S, TEXT("Points: 1 per 10 damage, 0.4 per 10 taken, 0.3 per 10 mitigated, 1 per 10 healed; 12 a knock-out, 5 an assist (help in the minute before), -10 knocked out; 4 a monster, 15 a boss; 2 a buff or debuff, 3 a turn of control, 10 a revive, 8 a tower."));
	}

	// ---- the moments, to watch again
	{
		const TArray<ATMBattleDirector::FTMReplayMark>& Marks = From.bReplaying ? From.Watching.Marks : From.Recording.Marks;
		const float MY = Top + 456.0f * S;
		const float MH = PY + PH - Pad - MY;
		Panel(PX + Pad, MY, LeftW, MH, FLinearColor(0.03f, 0.04f, 0.07f, 0.9f), FLinearColor(1.0f, 1.0f, 1.0f, 0.08f), 1.0f);
		Text(TEXT("MOMENTS   click to watch"), PX + Pad + 16.0f * S, MY + 12.0f * S, Dim, Font, 0.48f * S);
		float RY = MY + 42.0f * S;
		int32 Shown = 0;
		for (const ATMBattleDirector::FTMReplayMark& Mark : Marks)
		{
			if (Mark.Kind == TEXT("won") || RY > MY + MH - 30.0f * S)
			{
				continue;
			}
			const TMSim::FUnit* Marked = Mark.Unit >= 0 ? From.Battle.FindUnit(Mark.Unit) : nullptr;
			const FString Who = Marked ? FString::Printf(TEXT("%s (%s)"), *JobName(*Marked), Marked->Team == 0 ? TEXT("Blue") : TEXT("Red")) : FString(TEXT("A unit"));
			const FString What = Mark.Kind == TEXT("ko") ? FString::Printf(TEXT("%s falls"), *Who)
				: Mark.Kind == TEXT("revive") ? FString::Printf(TEXT("%s is raised"), *Who)
				: Mark.Kind == TEXT("tower") ? FString(TEXT("A watchtower is taken")) : FString(TEXT("A camp is cleared"));
			const bool bOver = FBox2D(FVector2D(PX + Pad, RY - 4.0f * S), FVector2D(PX + Pad + LeftW, RY + 24.0f * S)).IsInside(MousePoint());
			Text(Clock(Mark.Tick), PX + Pad + 16.0f * S, RY, Gold, Font, 0.52f * S);
			Text(What, PX + Pad + 70.0f * S, RY, bOver ? TextColour : Dim, Font, 0.52f * S);
			AddButton(PX + Pad, RY - 4.0f * S, LeftW, 28.0f * S, ETMHudAction::ReportMoment, Mark.Tick);
			RY += 28.0f * S;
			++Shown;
		}
		if (Shown == 0)
		{
			Text(TEXT("Nobody fell."), PX + Pad + 16.0f * S, RY, Dim, Font, 0.52f * S);
		}
	}

	// ---- the tables, or one unit's battle
	const float RX = PX + Pad + LeftW + 20.0f * S;
	const float RW = PX + PW - Pad - RX;
	static const TCHAR* Tabs[4] = { TEXT("Overview"), TEXT("Damage"), TEXT("Support"), TEXT("Control") };
	float TX = RX;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		MenuButton(TX, Top, 130.0f * S, 40.0f * S, Tabs[Index], ETMHudAction::ReportTab, Index, From.ReportTab == Index && From.ReportUnit < 0);
		TX += 136.0f * S;
	}
	Text(TEXT("Click a unit for its whole battle"), RX + RW - TextSize(TEXT("Click a unit for its whole battle"), Font, 0.5f * S).X, Top + 12.0f * S, Dim, Font, 0.5f * S);

	const ATMBattleDirector::FTMUnitTally* Opened = From.ReportUnit >= 0 ? From.Tallies.Find(From.ReportUnit) : nullptr;
	if (Opened)
	{
		const TMSim::FUnit* Unit = From.Battle.FindUnit(From.ReportUnit);
		const ATMBattleDirector::FTMUnitTally& T = *Opened;
		const float UY = Top + 56.0f * S;
		const float UH = PY + PH - Pad - UY;
		Panel(RX, UY, RW, UH, FLinearColor(0.03f, 0.04f, 0.07f, 0.9f), FLinearColor(1.0f, 1.0f, 1.0f, 0.08f), 1.0f);
		const FString Name = Unit ? JobName(*Unit) : FString();
		const double Points = ATMBattleDirector::ScoreOf(T);
		const FString G = Grade(Points, Best, From.ReportUnit == Mvp);
		Text(Name, RX + 20.0f * S, UY + 16.0f * S, Unit ? TeamColour(Unit->Team) : TextColour, Big, 0.7f * S);
		if (Unit)
		{
			ReportGear(*Unit, RX + 40.0f * S + TextSize(Name, Big, 0.7f * S).X, UY + 16.0f * S, 36.0f * S);
		}
		Text(FString::Printf(TEXT("%s   %.1f points"), *G, Points), RX + 20.0f * S, UY + 54.0f * S, GradeColour(G), Font, 0.58f * S);
		MenuButton(RX + RW - 180.0f * S, UY + 16.0f * S, 160.0f * S, 40.0f * S, TEXT("Back to the tables"), ETMHudAction::ReportUnit, -1);
		// Tiles: the numbers that matter.
		struct FTile { FString Value; const TCHAR* Label; };
		const FTile Tiles[8] = {
			{ Number(T.Damage), TEXT("damage") }, { FString::Printf(TEXT("%d / %d / %d"), T.Kills, T.Assists, T.Deaths), TEXT("knock-outs / assists / fell") },
			{ Number(T.Taken), TEXT("damage taken") }, { Number(T.Mitigated), TEXT("damage mitigated") },
			{ Number(T.Healing), TEXT("healing") }, { FString::Printf(TEXT("%d / %d"), T.Buffs, T.Debuffs), TEXT("buffs / debuffs") },
			{ FString::Printf(TEXT("%d"), T.Control), TEXT("turns of control") }, { FString::Printf(TEXT("%d + %d"), T.Monsters, T.Bosses), TEXT("monsters + bosses") } };
		const float TW = (RW - 40.0f * S - 3.0f * 10.0f * S) / 4.0f;
		for (int32 Index = 0; Index < 8; ++Index)
		{
			const float X = RX + 20.0f * S + (Index % 4) * (TW + 10.0f * S);
			const float Y = UY + 96.0f * S + (Index / 4) * 78.0f * S;
			Panel(X, Y, TW, 68.0f * S, FLinearColor(0.07f, 0.09f, 0.14f, 0.95f), FLinearColor(1.0f, 1.0f, 1.0f, 0.06f), 1.0f);
			Text(Tiles[Index].Value, X + 12.0f * S, Y + 8.0f * S, TextColour, Big, 0.6f * S);
			Text(Tiles[Index].Label, X + 12.0f * S, Y + 42.0f * S, Dim, Font, 0.46f * S);
		}
		// Damage by ability, and taken from whom.
		auto Bars = [this, Font](float X, float Y, float W, const TCHAR* Heading, TArray<TPair<FString, int32>> Rows, const FLinearColor& Colour)
		{
			Text(Heading, X, Y, Dim, Font, 0.48f * S);
			Rows.Sort([](const TPair<FString, int32>& A, const TPair<FString, int32>& B) { return A.Value > B.Value; });
			int32 Most = 1;
			for (const TPair<FString, int32>& Row : Rows)
			{
				Most = FMath::Max(Most, Row.Value);
			}
			float RowY = Y + 28.0f * S;
			for (int32 Index = 0; Index < FMath::Min(Rows.Num(), 6); ++Index)
			{
				Text(Rows[Index].Key, X, RowY, TextColour, Font, 0.52f * S);
				const FString Value = FText::AsNumber(Rows[Index].Value).ToString();
				Text(Value, X + W - TextSize(Value, Font, 0.52f * S).X, RowY, TextColour, Font, 0.52f * S);
				DrawRect(FLinearColor(1.0f, 1.0f, 1.0f, 0.06f), X, RowY + 24.0f * S, W, 6.0f * S);
				DrawRect(Colour, X, RowY + 24.0f * S, W * Rows[Index].Value / Most, 6.0f * S);
				RowY += 42.0f * S;
			}
			if (Rows.Num() == 0)
			{
				Text(TEXT("None."), X, RowY, Dim, Font, 0.52f * S);
			}
		};
		TArray<TPair<FString, int32>> Dealt;
		for (const TPair<FString, int32>& Pair : T.ByAbility)
		{
			Dealt.Emplace(Pair.Key, Pair.Value);
		}
		TArray<TPair<FString, int32>> Taken;
		for (const TPair<int32, int32>& Pair : T.TakenFrom)
		{
			const TMSim::FUnit* Source = From.Battle.FindUnit(Pair.Key);
			Taken.Emplace(Source ? JobName(*Source) : FString(TEXT("the ground")), Pair.Value);
		}
		const float ColW = (RW - 60.0f * S) * 0.5f;
		const float By = UY + 270.0f * S;
		Bars(RX + 20.0f * S, By, ColW, TEXT("DAMAGE BY ABILITY"), Dealt, Unit ? TeamColour(Unit->Team) : Gold);
		Bars(RX + 40.0f * S + ColW, By, ColW, TEXT("DAMAGE TAKEN, FROM"), Taken, Unit ? TeamColour(1 - Unit->Team) : Urgent);
		return;
	}

	// The columns of each tab.
	using FTally = ATMBattleDirector::FTMUnitTally;
	auto Int = [](int32 V) { return V > 0 ? FText::AsNumber(V).ToString() : FString(TEXT("-")); };
	TArray<FColumn> Columns;
	switch (From.ReportTab)
	{
	case 1:
		Columns = {
			{ TEXT("Damage"), [Int](const FTally& T, double) { return Int(T.Damage); }, [](const FTally& T) { return T.Damage; } },
			{ TEXT("Biggest hit"), [Int](const FTally& T, double) { return Int(T.Biggest); }, [](const FTally&) { return -1; } },
			{ TEXT("Crits"), [Int](const FTally& T, double) { return Int(T.Crits); }, [](const FTally&) { return -1; } },
			{ TEXT("Taken"), [Int](const FTally& T, double) { return Int(T.Taken); }, [](const FTally& T) { return T.Taken; } },
			{ TEXT("Mitigated"), [Int](const FTally& T, double) { return Int(T.Mitigated); }, [](const FTally& T) { return T.Mitigated; } },
			{ TEXT("Knock-outs"), [Int](const FTally& T, double) { return Int(T.Kills); }, [](const FTally&) { return -1; } } };
		break;
	case 2:
		Columns = {
			{ TEXT("Healing"), [Int](const FTally& T, double) { return Int(T.Healing); }, [](const FTally& T) { return T.Healing; } },
			{ TEXT("Revives"), [Int](const FTally& T, double) { return Int(T.Revives); }, [](const FTally&) { return -1; } },
			{ TEXT("Buffs on allies"), [Int](const FTally& T, double) { return Int(T.Buffs); }, [](const FTally& T) { return T.Buffs; } },
			{ TEXT("Taken for allies"), [Int](const FTally& T, double) { return Int(T.Guarded); }, [](const FTally& T) { return T.Guarded; } },
			{ TEXT("Towers"), [Int](const FTally& T, double) { return Int(T.Towers); }, [](const FTally&) { return -1; } },
			{ TEXT("Assists"), [Int](const FTally& T, double) { return Int(T.Assists); }, [](const FTally&) { return -1; } } };
		break;
	case 3:
		Columns = {
			{ TEXT("Debuffs landed"), [Int](const FTally& T, double) { return Int(T.Debuffs); }, [](const FTally& T) { return T.Debuffs; } },
			{ TEXT("Turns of control"), [Int](const FTally& T, double) { return Int(T.Control); }, [](const FTally& T) { return T.Control; } },
			{ TEXT("Monsters"), [Int](const FTally& T, double) { return Int(T.Monsters); }, [](const FTally&) { return -1; } },
			{ TEXT("Bosses"), [Int](const FTally& T, double) { return Int(T.Bosses); }, [](const FTally&) { return -1; } },
			{ TEXT("Knocked out"), [Int](const FTally& T, double) { return Int(T.Deaths); }, [](const FTally&) { return -1; } },
			{ TEXT("Assists"), [Int](const FTally& T, double) { return Int(T.Assists); }, [](const FTally&) { return -1; } } };
		break;
	default:
		Columns = {
			{ TEXT("K / A / D"), [](const FTally& T, double) { return FString::Printf(TEXT("%d / %d / %d"), T.Kills, T.Assists, T.Deaths); }, [](const FTally&) { return -1; } },
			{ TEXT("Damage"), [Int](const FTally& T, double) { return Int(T.Damage); }, [](const FTally& T) { return T.Damage; } },
			{ TEXT("Taken"), [Int](const FTally& T, double) { return Int(T.Taken); }, [](const FTally& T) { return T.Taken; } },
			{ TEXT("Mitigated"), [Int](const FTally& T, double) { return Int(T.Mitigated); }, [](const FTally& T) { return T.Mitigated; } },
			{ TEXT("Healing"), [Int](const FTally& T, double) { return Int(T.Healing); }, [](const FTally& T) { return T.Healing; } },
			{ TEXT("Score"), [](const FTally& T, double) { return FString::Printf(TEXT("%.1f"), ATMBattleDirector::ScoreOf(T)); }, [](const FTally&) { return -1; } } };
		break;
	}
	// Each bar against the battle's most of that column.
	TArray<int32> Most;
	for (const FColumn& Column : Columns)
	{
		int32 M = 1;
		for (const TPair<int32, FTally>& Pair : From.Tallies)
		{
			M = FMath::Max(M, Column.Measure(Pair.Value));
		}
		Most.Add(M);
	}

	const float NameW = 230.0f * S;
	const float GearW = 96.0f * S;
	const float ColW = (RW - NameW - GearW - 40.0f * S) / Columns.Num();
	const float RowH = 50.0f * S;
	float Y = Top + 56.0f * S;
	for (int32 Team = 0; Team < 2; ++Team)
	{
		const float TH = 76.0f * S + Sides[Team].Num() * RowH;
		Panel(RX, Y, RW, TH, FLinearColor(0.03f, 0.04f, 0.07f, 0.9f), FLinearColor(1.0f, 1.0f, 1.0f, 0.08f), 1.0f);
		int32 Damage = 0, Healing = 0, Kills = 0, Monsters = 0;
		for (const TMSim::FUnit* Unit : Sides[Team])
		{
			const FTally& T = From.Tallies[Unit->Id];
			Damage += T.Damage;
			Healing += T.Healing;
			Kills += T.Kills;
			Monsters += T.Monsters + T.Bosses;
		}
		const bool bWon = From.Battle.Winner == Team;
		Text(FString::Printf(TEXT("%s%s"), Team == 0 ? TEXT("Blue") : TEXT("Red"), bWon ? TEXT("   won") : TEXT("")), RX + 16.0f * S, Y + 10.0f * S, TeamColour(Team), Font, 0.62f * S);
		const FString Sum = FString::Printf(TEXT("%s damage   %s healing   %d knock-outs   %d monsters"), *Number(Damage), *Number(Healing), Kills, Monsters);
		Text(Sum, RX + RW - 16.0f * S - TextSize(Sum, Font, 0.5f * S).X, Y + 14.0f * S, Dim, Font, 0.5f * S);
		// The headings.
		Text(TEXT("Items"), RX + 16.0f * S + NameW, Y + 44.0f * S, Dim, Font, 0.44f * S);
		float CX = RX + 16.0f * S + NameW + GearW;
		for (const FColumn& Column : Columns)
		{
			Text(Column.Heading, CX, Y + 44.0f * S, Dim, Font, 0.44f * S);
			CX += ColW;
		}
		float RowY = Y + 70.0f * S;
		for (const TMSim::FUnit* Unit : Sides[Team])
		{
			const FTally& T = From.Tallies[Unit->Id];
			const bool bStar = Unit->Id == Mvp;
			const bool bOver = FBox2D(FVector2D(RX, RowY), FVector2D(RX + RW, RowY + RowH)).IsInside(MousePoint());
			if (bStar || bOver)
			{
				DrawRect(bStar ? FLinearColor(1.0f, 0.82f, 0.35f, 0.07f) : FLinearColor(1.0f, 1.0f, 1.0f, 0.04f), RX + 2.0f, RowY, RW - 4.0f, RowH);
			}
			const double Points = ATMBattleDirector::ScoreOf(T);
			const FString G = Grade(Points, Best, bStar);
			Text(G, RX + 16.0f * S, RowY + 10.0f * S, GradeColour(G), Big, 0.6f * S);
			Text(JobName(*Unit), RX + 50.0f * S, RowY + 6.0f * S, TextColour, Font, 0.6f * S);
			Text(RoleWord(*Unit) + (Unit->IsAlive() ? FString() : FString(TEXT("   down"))), RX + 50.0f * S, RowY + 28.0f * S, Dim, Font, 0.44f * S);
			ReportGear(*Unit, RX + 16.0f * S + NameW, RowY + 11.0f * S, 26.0f * S);
			CX = RX + 16.0f * S + NameW + GearW;
			for (int32 Index = 0; Index < Columns.Num(); ++Index)
			{
				const FColumn& Column = Columns[Index];
				Text(Column.Shown(T, Points), CX, RowY + 8.0f * S, TextColour, Font, 0.58f * S);
				const int32 Measure = Column.Measure(T);
				if (Measure > 0)
				{
					DrawRect(FLinearColor(1.0f, 1.0f, 1.0f, 0.06f), CX, RowY + 34.0f * S, ColW - 20.0f * S, 4.0f * S);
					DrawRect(TeamColour(Team), CX, RowY + 34.0f * S, (ColW - 20.0f * S) * Measure / Most[Index], 4.0f * S);
					// The battle's best of this column, marked.
					if (Measure == Most[Index])
					{
						DrawRect(Gold, CX - 6.0f * S, RowY + 14.0f * S, 3.0f * S, 14.0f * S);
					}
				}
				CX += ColW;
			}
			AddButton(RX, RowY, RW, RowH, ETMHudAction::ReportUnit, Unit->Id);
			RowY += RowH;
		}
		Y += TH + 14.0f * S;
	}
}

void ATMBattleHud::ReportGear(const TMSim::FUnit& Unit, float X, float Y, float Size)
{
	// Its items at the end of the battle, each with its tip; a dash when it had none.
	float Across = 0.0f;
	for (const TMSim::FItemDef* Carried : Unit.Gear)
	{
		if (Carried)
		{
			ItemBadge(Carried, X + Across, Y, Size, true);
			Across += Size + 4.0f * S;
		}
	}
	if (Across == 0.0f)
	{
		Text(TEXT("-"), X, Y + Size * 0.15f, Dim, GEngine->GetMediumFont(), 0.5f * S);
	}
}
