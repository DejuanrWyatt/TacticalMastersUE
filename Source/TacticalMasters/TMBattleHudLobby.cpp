// The online lobby and the draft, as screens (Docs/design/feat-lobby.md).
//
// The lobby: Blue's and Red's columns, the players on each and the four units
// they share with their items, Join buttons to change side, every battle
// setting (the host's to change), and Start, or a joined player's Ready. The draft: the order along
// the top, each side's bans and picks, and every class in a grid to ban or
// pick from when it is this player's turn.

#include "TMBattleHud.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "HAL/PlatformTime.h"
#include "Misc/ScopeExit.h"

#include "TMBattleDirector.h"
#include "TMBattleHudStyle.h"
#include "TMNet.h"

#include "SimAbility.h"
#include "SimMap.h"

using namespace TMHudStyle;

namespace TMLobbyHud
{
	const TCHAR* SideName(int32 Team)
	{
		return Team == 0 ? TEXT("Blue") : TEXT("Red");
	}

	FString ClassName(const std::string& Id)
	{
		const TMSim::FJobDef* Job = TMSim::FindJob(Id);
		return Job ? FString(UTF8_TO_TCHAR(Job->Name.c_str())) : FString(TEXT("-"));
	}

	FString ClassName(const FString& Id)
	{
		return ClassName(std::string(TCHAR_TO_UTF8(*Id)));
	}
}

void ATMBattleHud::DrawLobby(ATMBattleDirector& From)
{
	using namespace TMLobbyHud;
	DrawRect(FLinearColor(0.02f, 0.03f, 0.06f, 0.6f), 0.0f, 0.0f, Canvas->ClipX, Canvas->ClipY);
	UFont* Font = GEngine->GetMediumFont();
	UFont* Big = GEngine->GetLargeFont();
	const bool bHost = From.Net.IsValid() && From.Net->IsHost();
	// The whole lobby always fits (v20: it holds every setting now), shrunk to
	// the window as the setup screen is.
	const float WasS = S;
	S = FMath::Min(S, FMath::Min((Canvas->ClipY - 16.0f) / 1040.0f, (Canvas->ClipX - 16.0f) / 1240.0f));
	ON_SCOPE_EXIT
	{
		S = WasS;
	};
	const float PW = 1240.0f * S;
	const float PH = 1040.0f * S;
	const float PX = (Canvas->ClipX - PW) * 0.5f;
	const float PY = FMath::Max(8.0f * S, (Canvas->ClipY - PH) * 0.5f);
	Panel(PX, PY, PW, PH, FLinearColor(0.05f, 0.06f, 0.1f, 0.95f), Gold, 1.5f);
	Text(TEXT("Lobby"), PX + 30.0f * S, PY + 16.0f * S, Gold, Big, 0.9f * S);
	Text(FString::Printf(TEXT("%d of 4 players"), From.Players.Num()), PX + 30.0f * S, PY + 62.0f * S, Dim, Font, 0.55f * S);
	// The join code, big, with Copy: what the host sends friends (Docs/design/feat-online-eos.md).
	if (!From.OnlineCode.IsEmpty())
	{
		const float CopyW = 120.0f * S;
		const float CopyH = 44.0f * S;
		const float CopyX = PX + PW - 30.0f * S - CopyW;
		const float CodeY = PY + 22.0f * S;
		const bool bCopied = FPlatformTime::Seconds() - From.CodeCopiedAt < 2.0;
		MenuButton(CopyX, CodeY, CopyW, CopyH, bCopied ? TEXT("Copied") : TEXT("Copy"), ETMHudAction::LobbyCopyCode, -1, false);
		AddTip(CopyX, CodeY, CopyW, CopyH, TEXT("Puts the join code on the clipboard, to paste to your friends. They type it on Play Online to join."));
		const FVector2D CodeSize = TextSize(From.OnlineCode, Big, 0.85f * S);
		const float CodeX = CopyX - 18.0f * S - CodeSize.X;
		Text(From.OnlineCode, CodeX, CodeY + (CopyH - CodeSize.Y) * 0.5f, Gold, Big, 0.85f * S);
		const FString Label = TEXT("Join code");
		const FVector2D LabelSize = TextSize(Label, Font, 0.55f * S);
		Text(Label, CodeX - 14.0f * S - LabelSize.X, CodeY + (CopyH - LabelSize.Y) * 0.5f, Dim, Font, 0.55f * S);
	}

	// The two sides: players, then the four units with their classes and items.
	const bool bItems = From.Setup.ItemBudget > 0;
	const float ColGap = 30.0f * S;
	const float ColW = (PW - 60.0f * S - ColGap) * 0.5f;
	const float ColTop = PY + 96.0f * S;
	const float ColH = 470.0f * S;
	for (int32 Team = 0; Team < 2; ++Team)
	{
		const float CX = PX + 30.0f * S + Team * (ColW + ColGap);
		Panel(CX, ColTop, ColW, ColH, TeamFill(Team) * FLinearColor(1, 1, 1, 0.35f), TeamColour(Team), 1.5f);
		Text(SideName(Team), CX + 18.0f * S, ColTop + 10.0f * S, TeamColour(Team), Big, 0.7f * S);
		const bool bMine = From.Players.IsValidIndex(From.LocalPlayer) && From.Players[From.LocalPlayer].Team == Team;
		if (!bMine)
		{
			MenuButton(CX + ColW - 150.0f * S, ColTop + 10.0f * S, 132.0f * S, 36.0f * S, FString::Printf(TEXT("Join %s"), SideName(Team)),
				ETMHudAction::LobbySide, Team);
		}
		else if (bItems)
		{
			const FString Points = FString::Printf(TEXT("Items %d / %d points"), From.ItemPointsSpent(Team), From.Setup.ItemBudget);
			const FVector2D PointsSize = TextSize(Points, Font, 0.5f * S);
			Text(Points, CX + ColW - 18.0f * S - PointsSize.X, ColTop + 18.0f * S, Dim, Font, 0.5f * S);
		}

		// Its players.
		float Y = ColTop + 56.0f * S;
		int32 OnSide = 0;
		for (int32 i = 0; i < From.Players.Num(); ++i)
		{
			const ATMBattleDirector::FTMOnlinePlayer& Player = From.Players[i];
			if (Player.Team != Team)
			{
				continue;
			}
			++OnSide;
			Panel(CX + 14.0f * S, Y, ColW - 28.0f * S, 36.0f * S, FLinearColor(0.08f, 0.1f, 0.15f, 0.9f),
				i == From.LocalPlayer ? Gold : FLinearColor(0.35f, 0.4f, 0.5f, 0.7f), i == From.LocalPlayer ? 1.5f : 1.0f);
			FString Name = Player.Name;
			if (i == 0)
			{
				Name += TEXT("   (host)");
			}
			if (i == From.LocalPlayer)
			{
				Name += TEXT("   - you");
			}
			Text(Name, CX + 28.0f * S, Y + 7.0f * S, TextColour, Font, 0.58f * S);
			const FString Ready = i == 0 ? FString() : Player.bReady ? FString(TEXT("Ready")) : FString(TEXT("Not ready"));
			const FVector2D ReadySize = TextSize(Ready, Font, 0.55f * S);
			Text(Ready, CX + ColW - 28.0f * S - ReadySize.X, Y + 8.0f * S, Player.bReady ? Gold : Dim, Font, 0.55f * S);
			Y += 40.0f * S;
		}
		if (OnSide == 0)
		{
			Text(TEXT("Nobody yet: the computer plays this side."), CX + 28.0f * S, Y + 8.0f * S, Dim, Font, 0.55f * S);
		}

		// Its four units: the class, who orders it, and (with item points) its three items.
		Y = ColTop + 56.0f * S + 4.0f * 40.0f * S + 8.0f * S;
		Text(From.Setup.bDraft ? TEXT("Units (classes come from the draft)")
			: bItems ? TEXT("Units (click yours: the class, or an item slot to buy)") : TEXT("Units (click yours to change its class)"),
			CX + 18.0f * S, Y, Dim, Font, 0.5f * S);
		Y += 26.0f * S;
		const float Box = 38.0f * S;
		const float ItemsW = bItems ? 3.0f * (Box + 6.0f * S) : 0.0f;
		for (int32 Slot = 0; Slot < 4; ++Slot)
		{
			const int32 Code = Team * 4 + Slot;
			const int32 Holder = From.SlotOwner(Team, Slot);
			const bool bMay = From.LobbyMayPick(From.LocalPlayer, Code);
			const FString Who = Holder < 0 ? FString(TEXT("computer")) : Holder == From.LocalPlayer ? FString(TEXT("you")) : From.Players[Holder].Name;
			const FString Class = From.Setup.bDraft ? FString(TEXT("drafted")) : ClassName(From.Setup.Rosters[Team][Slot]);
			const FString Label = FString::Printf(TEXT("%d.  %s"), Slot + 1, *Class);
			const float RX = CX + 14.0f * S;
			const float RW = ColW - 28.0f * S - ItemsW;
			const float RH = 44.0f * S;
			if (!From.Setup.bDraft && bMay)
			{
				MenuButton(RX, Y, RW, RH, Label + TEXT("   ·   ") + Who, ETMHudAction::LobbySlot, Code, Holder == From.LocalPlayer);
			}
			else
			{
				Panel(RX, Y, RW, RH, FLinearColor(0.07f, 0.08f, 0.11f, 0.9f), FLinearColor(0.3f, 0.32f, 0.38f, 0.6f), 1.0f);
				Text(Label, RX + 14.0f * S, Y + 10.0f * S, TextColour, Font, 0.58f * S);
				const FVector2D WhoSize = TextSize(Who, Font, 0.55f * S);
				Text(Who, RX + RW - 14.0f * S - WhoSize.X, Y + 11.0f * S, Dim, Font, 0.55f * S);
			}
			for (int32 Item = 0; Item < 3 && bItems; ++Item)
			{
				const float BX = RX + RW + 6.0f * S + Item * (Box + 6.0f * S);
				const float BY = Y + (RH - Box) * 0.5f;
				const TMSim::FItemDef* Carried = TMSim::FindItem(From.Setup.Items[Team][Slot][Item]);
				ItemBadge(Carried, BX, BY, Box, Carried != nullptr);
				if (bMay)
				{
					if (!Carried)
					{
						AddTip(BX, BY, Box, Box, TEXT("An empty item slot: click to buy an item with your side's points."));
					}
					AddButton(BX, BY, Box, Box, ETMHudAction::SetupItem, Team * 12 + Slot * 3 + Item);
				}
			}
			Y += RH + 6.0f * S;
		}
	}

	// Every battle setting (v20 play test): the host's to change, the others' to read.
	const TArray<ATMBattleDirector::FTMSettingRow> Rows = bHost ? From.LobbySettingRows() : From.LobbySettingsTold;
	float Y = ColTop + ColH + 16.0f * S;
	Text(bHost ? TEXT("Battle settings   (click to change)") : TEXT("Battle settings   (the host's)"), PX + 30.0f * S, Y, Gold, Font, 0.58f * S);
	Y += 30.0f * S;
	if (Rows.Num() == 0)
	{
		Text(From.LobbyRulesLine, PX + 30.0f * S, Y, Dim, Font, 0.55f * S);
	}
	const int32 Across = 3;
	const float CellGap = 14.0f * S;
	const float CellW = (PW - 60.0f * S - (Across - 1) * CellGap) / Across;
	const float LabelW = 124.0f * S;
	const float RowH = 40.0f * S;
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		const ATMBattleDirector::FTMSettingRow& Row = Rows[Index];
		const float X = PX + 30.0f * S + (Index % Across) * (CellW + CellGap);
		const float RowY = Y + (Index / Across) * (RowH + 4.0f * S);
		Text(Row.Label, X, RowY + 9.0f * S, Dim, Font, 0.52f * S);
		if (bHost)
		{
			MenuButton(X + LabelW, RowY, CellW - LabelW, RowH - 4.0f * S, Row.Value, static_cast<ETMHudAction>(Row.Action));
			if (!Row.Tip.IsEmpty())
			{
				AddTip(X, RowY, CellW, RowH, Row.Tip);
			}
		}
		else
		{
			Panel(X + LabelW, RowY, CellW - LabelW, RowH - 4.0f * S, FLinearColor(0.07f, 0.08f, 0.11f, 0.9f), FLinearColor(0.3f, 0.32f, 0.38f, 0.6f), 1.0f);
			Text(Row.Value, X + LabelW + 12.0f * S, RowY + 8.0f * S, TextColour, Font, 0.54f * S);
		}
	}
	Y += ((Rows.Num() + Across - 1) / Across) * (RowH + 4.0f * S) + 8.0f * S;
	for (const FString& Line : { From.OnlineStatus, bHost ? From.OnlineAddresses : FString(), bHost ? From.OnlineRouter : FString() })
	{
		if (!Line.IsEmpty() && Y < PY + PH - 90.0f * S)
		{
			Text(Line, PX + 30.0f * S, Y, Line == From.OnlineStatus ? TextColour : Dim, Font, 0.5f * S);
			Y += 22.0f * S;
		}
	}

	// Leave, Random, and Start or Ready.
	const float BH = 46.0f * S;
	const float BY = PY + PH - 64.0f * S;
	MenuButton(PX + 30.0f * S, BY, 160.0f * S, BH, TEXT("Leave  (Esc)"), ETMHudAction::LobbyLeave);
	if (!From.Setup.bDraft)
	{
		MenuButton(PX + 200.0f * S, BY, 220.0f * S, BH, TEXT("Random classes"), ETMHudAction::LobbyRandom, -1, false, TEXT("for each of your units"));
	}
	const float BW = 240.0f * S;
	if (bHost)
	{
		FString WhyNot;
		const bool bCan = From.LobbyCanStart(&WhyNot);
		if (bCan)
		{
			MenuButton(PX + PW - BW - 30.0f * S, BY, BW, BH, From.Setup.bDraft ? TEXT("Start the draft") : TEXT("Start Battle"),
				ETMHudAction::LobbyStart, -1, true);
		}
		else
		{
			ChoiceButton(PX + PW - BW - 30.0f * S, BY, BW, BH, TEXT("Start"), ETMHudAction::LobbyStart, -1, false);
			const FVector2D Size = TextSize(WhyNot, Font, 0.5f * S);
			Text(WhyNot, PX + PW - 30.0f * S - Size.X, BY - 26.0f * S, Dim, Font, 0.5f * S);
		}
	}
	else
	{
		const bool bReady = From.Players.IsValidIndex(From.LocalPlayer) && From.Players[From.LocalPlayer].bReady;
		MenuButton(PX + PW - BW - 30.0f * S, BY, BW, BH, bReady ? TEXT("Ready (click to wait)") : TEXT("Ready"), ETMHudAction::LobbyReady, -1, !bReady,
			bReady ? TEXT("waiting for the host to start") : TEXT("tell the host you are set"));
	}
}

void ATMBattleHud::DrawDraft(ATMBattleDirector& From)
{
	using namespace TMLobbyHud;
	DrawRect(FLinearColor(0.02f, 0.03f, 0.06f, 0.7f), 0.0f, 0.0f, Canvas->ClipX, Canvas->ClipY);
	UFont* Font = GEngine->GetMediumFont();
	UFont* Big = GEngine->GetLargeFont();
	const float PW = FMath::Min(Canvas->ClipX - 40.0f * S, 1700.0f * S);
	const float PH = Canvas->ClipY - 40.0f * S;
	const float PX = (Canvas->ClipX - PW) * 0.5f;
	const float PY = 20.0f * S;
	Panel(PX, PY, PW, PH, FLinearColor(0.05f, 0.06f, 0.1f, 0.96f), Gold, 1.5f);

	const int32 Step = From.Draft.Step;
	const bool bDone = From.DraftDone();
	const int32 StepTeam = From.DraftStepTeam(Step);
	const bool bBan = From.DraftStepIsBan(Step);
	const bool bMyTurn = From.DraftMayChoose(From.LocalPlayer);

	// What is happening now, and the clock.
	FString Now;
	FLinearColor NowColour = Gold;
	if (bDone)
	{
		Now = TEXT("The draft is done. The battle is starting...");
	}
	else
	{
		const int32 Chooser = From.DraftChooser(Step);
		const FString Who = bMyTurn ? FString(TEXT("Your turn")) : Chooser == -1 ? FString(TEXT("The computer"))
			: Chooser >= 0 && From.Players.IsValidIndex(Chooser) ? From.Players[Chooser].Name : FString::Printf(TEXT("%s"), SideName(StepTeam));
		Now = bBan ? FString::Printf(TEXT("%s: %s bans a class"), *Who, SideName(StepTeam))
			: FString::Printf(TEXT("%s: %s picks unit %d"), *Who, SideName(StepTeam), From.Draft.Picks[FMath::Clamp(StepTeam, 0, 1)].Num() + 1);
		NowColour = bMyTurn ? Gold : TeamColour(StepTeam);
	}
	Text(TEXT("Draft"), PX + 24.0f * S, PY + 14.0f * S, Gold, Big, 0.85f * S);
	Text(Now, PX + 170.0f * S, PY + 22.0f * S, NowColour, Big, 0.6f * S);
	if (!bDone && From.Setup.DraftSeconds > 0)
	{
		const FString Clock = FString::Printf(TEXT("%.0f"), FMath::CeilToFloat(From.Draft.Left));
		const FVector2D ClockSize = TextSize(Clock, Big, 0.9f * S);
		Text(Clock, PX + PW - 24.0f * S - ClockSize.X, PY + 12.0f * S, From.Draft.Left < 6.0f ? Urgent : TextColour, Big, 0.9f * S);
	}

	// The order, one chip a step: done steps faded, this one outlined.
	float ChipX = PX + 24.0f * S;
	const float ChipY = PY + 70.0f * S;
	const float ChipW = 70.0f * S;
	for (int32 i = 0; i < From.DraftSteps(); ++i)
	{
		const int32 T = From.DraftStepTeam(i);
		const bool bPast = i < Step;
		Panel(ChipX, ChipY, ChipW, 28.0f * S, TeamFill(T) * FLinearColor(1, 1, 1, bPast ? 0.25f : 0.7f),
			i == Step ? Gold : TeamColour(T) * FLinearColor(1, 1, 1, bPast ? 0.3f : 0.8f), i == Step ? 2.0f : 1.0f);
		const FString Label = From.DraftStepIsBan(i) ? TEXT("ban") : TEXT("pick");
		const FVector2D Size = TextSize(Label, Font, 0.45f * S);
		Text(Label, ChipX + (ChipW - Size.X) * 0.5f, ChipY + (28.0f * S - Size.Y) * 0.5f, bPast ? Dim : TextColour, Font, 0.45f * S);
		ChipX += ChipW + 4.0f * S;
		// A little more room between the phases.
		if (i == 3 || i == 7 || i == 9)
		{
			ChipX += 12.0f * S;
		}
	}

	// The sides, at the left and right: bans, then picks with who orders them.
	const float SideW = 300.0f * S;
	const float SideTop = ChipY + 46.0f * S;
	const float SideH = PH - (SideTop - PY) - 24.0f * S;
	for (int32 Team = 0; Team < 2; ++Team)
	{
		const float SX = Team == 0 ? PX + 24.0f * S : PX + PW - 24.0f * S - SideW;
		Panel(SX, SideTop, SideW, SideH, TeamFill(Team) * FLinearColor(1, 1, 1, 0.3f), TeamColour(Team), 1.5f);
		Text(FString::Printf(TEXT("%s  -  %s"), SideName(Team), *From.SideNames(Team)), SX + 14.0f * S, SideTop + 10.0f * S, TeamColour(Team), Font, 0.62f * S);
		float Y = SideTop + 44.0f * S;
		Text(TEXT("Bans"), SX + 14.0f * S, Y, Dim, Font, 0.5f * S);
		Y += 22.0f * S;
		for (int32 b = 0; b < 3; ++b)
		{
			const bool bHas = b < From.Draft.Bans[Team].Num();
			Panel(SX + 14.0f * S, Y, SideW - 28.0f * S, 30.0f * S, FLinearColor(0.12f, 0.05f, 0.05f, 0.8f), FLinearColor(0.5f, 0.25f, 0.25f, 0.7f), 1.0f);
			Text(bHas ? ClassName(From.Draft.Bans[Team][b]) : FString(TEXT("-")), SX + 26.0f * S, Y + 5.0f * S, bHas ? Urgent : Dim, Font, 0.55f * S);
			Y += 34.0f * S;
		}
		Y += 14.0f * S;
		Text(TEXT("Picks"), SX + 14.0f * S, Y, Dim, Font, 0.5f * S);
		Y += 22.0f * S;
		for (int32 Slot = 0; Slot < 4; ++Slot)
		{
			const bool bHas = Slot < From.Draft.Picks[Team].Num();
			const bool bNext = !bDone && !bBan && StepTeam == Team && Slot == From.Draft.Picks[Team].Num();
			const int32 Holder = From.SlotOwner(Team, Slot);
			const FString Who = Holder < 0 ? FString(TEXT("computer")) : Holder == From.LocalPlayer ? FString(TEXT("you")) : From.Players[Holder].Name;
			Panel(SX + 14.0f * S, Y, SideW - 28.0f * S, 50.0f * S, FLinearColor(0.08f, 0.1f, 0.15f, 0.9f),
				bNext ? Gold : FLinearColor(0.35f, 0.4f, 0.5f, 0.7f), bNext ? 2.0f : 1.0f);
			Text(bHas ? ClassName(From.Draft.Picks[Team][Slot]) : FString(TEXT("...")), SX + 26.0f * S, Y + 5.0f * S, bHas ? TextColour : Dim, Font, 0.62f * S);
			Text(Who, SX + 26.0f * S, Y + 28.0f * S, Dim, Font, 0.45f * S);
			Y += 56.0f * S;
		}
	}

	// Every class, in the middle: taken or banned ones greyed, the rest to choose on this player's turn.
	const std::vector<const TMSim::FJobDef*>& Jobs = TMSim::AllJobs();
	const char* Roles[4] = { "tank", "damage", "support", "special" };
	const float GX = PX + 24.0f * S + SideW + 20.0f * S;
	const float GW = PW - 2.0f * (24.0f * S + SideW + 20.0f * S);
	float FX = GX;
	const float FY = SideTop;
	MenuButton(FX, FY, 90.0f * S, 32.0f * S, TEXT("All"), ETMHudAction::PickerRole, -1, From.PickerRole < 0);
	FX += 96.0f * S;
	for (int32 r = 0; r < 4; ++r)
	{
		MenuButton(FX, FY, 100.0f * S, 32.0f * S, FString(UTF8_TO_TCHAR(Roles[r])), ETMHudAction::PickerRole, r, From.PickerRole == r);
		FX += 106.0f * S;
	}
	if (bMyTurn && bBan)
	{
		MenuButton(GX + GW - 140.0f * S, FY, 140.0f * S, 32.0f * S, TEXT("Skip this ban"), ETMHudAction::DraftChoose, -1);
	}
	if (bMyTurn)
	{
		// v20 play test: a class at random, of the role shown.
		const float RX = GX + GW - (bBan ? 290.0f : 140.0f) * S;
		MenuButton(RX, FY, 140.0f * S, 32.0f * S, bBan ? TEXT("Random ban") : TEXT("Random pick"), ETMHudAction::DraftChoose, -2);
	}
	const int32 Columns = FMath::Max(2, FMath::FloorToInt(GW / (190.0f * S)));
	const float Gap = 6.0f * S;
	const float CellW = (GW - (Columns - 1) * Gap) / Columns;
	const float CellH = 40.0f * S;
	float Y = FY + 44.0f * S;
	int32 Column = 0;
	// The class under the pointer, in full, under the grid (v19 play test).
	const float CardH = FMath::Min(260.0f * S, PH * 0.32f);
	const float CardY = PY + PH - 24.0f * S - CardH;
	const FVector2D Mouse = MousePoint();
	const TMSim::FJobDef* Shown = nullptr;
	for (int32 j = 0; j < static_cast<int32>(Jobs.size()); ++j)
	{
		const TMSim::FJobDef& Job = *Jobs[j];
		if (From.PickerRole >= 0 && !TMSim::JobHasRole(Job.Id, Roles[From.PickerRole]))
		{
			continue;
		}
		if (Y + CellH > CardY - 10.0f * S)
		{
			break;
		}
		if (FBox2D(FVector2D(GX + Column * (CellW + Gap), Y), FVector2D(GX + Column * (CellW + Gap) + CellW, Y + CellH)).IsInside(Mouse))
		{
			Shown = &Job;
		}
		const FString Id = UTF8_TO_TCHAR(Job.Id.c_str());
		const FString Name = UTF8_TO_TCHAR(Job.Name.c_str());
		const float X = GX + Column * (CellW + Gap);
		const bool bUsed = From.DraftUsed(Id);
		if (bUsed)
		{
			Panel(X, Y, CellW, CellH, FLinearColor(0.06f, 0.06f, 0.08f, 0.9f), FLinearColor(0.25f, 0.25f, 0.3f, 0.5f), 1.0f);
			const FVector2D Size = TextSize(Name, Font, 0.58f * S);
			Text(Name, X + (CellW - Size.X) * 0.5f, Y + (CellH - Size.Y) * 0.5f, FLinearColor(0.5f, 0.5f, 0.55f, 0.5f), Font, 0.58f * S);
		}
		else
		{
			FString RoleText;
			for (const std::string& JobRole : Job.Roles)
			{
				RoleText += (RoleText.IsEmpty() ? TEXT("") : TEXT(", ")) + FString(UTF8_TO_TCHAR(JobRole.c_str()));
			}
			if (bMyTurn)
			{
				MenuButton(X, Y, CellW, CellH, Name, ETMHudAction::DraftChoose, j, false, RoleText);
			}
			else
			{
				Panel(X, Y, CellW, CellH, FLinearColor(0.1f, 0.12f, 0.17f, 0.9f), FLinearColor(0.35f, 0.4f, 0.5f, 0.6f), 1.0f);
				const FVector2D Size = TextSize(Name, Font, 0.58f * S);
				Text(Name, X + (CellW - Size.X) * 0.5f, Y + (CellH - Size.Y) * 0.5f, TextColour, Font, 0.58f * S);
			}
			AddTip(X, Y, CellW, CellH, FString::Printf(TEXT("%hs  (%s)\nHP %d  Armor %d  Resist %d  Evasion %d%%  Speed %d  Move %d"),
				Job.Name.c_str(), *RoleText, Job.Stats.Get(TMSim::EStat::Hp), Job.Stats.Get(TMSim::EStat::AttDef),
				Job.Stats.Get(TMSim::EStat::MagDef), ClassEvasion(Job.Stats), Job.Stats.Get(TMSim::EStat::Speed), Job.Stats.Get(TMSim::EStat::Move)));
		}
		if (++Column == Columns)
		{
			Column = 0;
			Y += CellH + Gap;
		}
	}
	if (Shown)
	{
		DrawClassCard(*Shown, GX, CardY, GW, CardH);
	}
	else
	{
		Text(TEXT("Rest the pointer on a class to see its abilities."), GX, CardY + 8.0f * S, Dim, Font, 0.5f * S);
	}
}
