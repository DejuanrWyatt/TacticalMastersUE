// The online lobby and the draft, as screens (Docs/design/feat-lobby.md).
//
// The lobby: Blue's and Red's columns, the players on each and the four units
// they share, Join buttons to change side, and at the bottom the host's
// settings and Start, or a joined player's Ready. The draft: the order along
// the top, each side's bans and picks, and every class in a grid to ban or
// pick from when it is this player's turn.

#include "TMBattleHud.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"

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
	const float PW = FMath::Min(Canvas->ClipX - 40.0f * S, 1240.0f * S);
	const float PH = FMath::Min(Canvas->ClipY - 40.0f * S, 820.0f * S);
	const float PX = (Canvas->ClipX - PW) * 0.5f;
	const float PY = (Canvas->ClipY - PH) * 0.5f;
	Panel(PX, PY, PW, PH, FLinearColor(0.05f, 0.06f, 0.1f, 0.95f), Gold, 1.5f);
	Text(TEXT("Lobby"), PX + 30.0f * S, PY + 18.0f * S, Gold, Big, 0.9f * S);
	const FString Rules = bHost ? From.LobbyRules() : From.LobbyRulesLine;
	Text(FString::Printf(TEXT("%d of 4 players   ·   %s"), From.Players.Num(), *Rules), PX + 30.0f * S, PY + 66.0f * S, Dim, Font, 0.55f * S);

	// The two sides.
	const float ColGap = 30.0f * S;
	const float ColW = (PW - 60.0f * S - ColGap) * 0.5f;
	const float ColTop = PY + 104.0f * S;
	const float ColH = 470.0f * S;
	for (int32 Team = 0; Team < 2; ++Team)
	{
		const float CX = PX + 30.0f * S + Team * (ColW + ColGap);
		Panel(CX, ColTop, ColW, ColH, TeamFill(Team) * FLinearColor(1, 1, 1, 0.35f), TeamColour(Team), 1.5f);
		Text(SideName(Team), CX + 18.0f * S, ColTop + 12.0f * S, TeamColour(Team), Big, 0.7f * S);
		const bool bMine = From.Players.IsValidIndex(From.LocalPlayer) && From.Players[From.LocalPlayer].Team == Team;
		if (!bMine)
		{
			MenuButton(CX + ColW - 150.0f * S, ColTop + 12.0f * S, 132.0f * S, 36.0f * S, FString::Printf(TEXT("Join %s"), SideName(Team)),
				ETMHudAction::LobbySide, Team);
		}

		// Its players.
		float Y = ColTop + 64.0f * S;
		int32 OnSide = 0;
		for (int32 i = 0; i < From.Players.Num(); ++i)
		{
			const ATMBattleDirector::FTMOnlinePlayer& Player = From.Players[i];
			if (Player.Team != Team)
			{
				continue;
			}
			++OnSide;
			Panel(CX + 14.0f * S, Y, ColW - 28.0f * S, 40.0f * S, FLinearColor(0.08f, 0.1f, 0.15f, 0.9f),
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
			Text(Name, CX + 28.0f * S, Y + 9.0f * S, TextColour, Font, 0.6f * S);
			const FString Ready = i == 0 ? FString() : Player.bReady ? FString(TEXT("Ready")) : FString(TEXT("Not ready"));
			const FVector2D ReadySize = TextSize(Ready, Font, 0.55f * S);
			Text(Ready, CX + ColW - 28.0f * S - ReadySize.X, Y + 10.0f * S, Player.bReady ? Gold : Dim, Font, 0.55f * S);
			Y += 46.0f * S;
		}
		if (OnSide == 0)
		{
			Text(TEXT("Nobody yet: the computer plays this side."), CX + 28.0f * S, Y + 9.0f * S, Dim, Font, 0.55f * S);
			Y += 46.0f * S;
		}

		// Its four units: the class, and who orders it.
		Y = ColTop + 64.0f * S + 4.0f * 46.0f * S + 10.0f * S;
		Text(From.Setup.bDraft ? TEXT("Units (classes come from the draft)") : TEXT("Units (click yours to change its class)"),
			CX + 18.0f * S, Y, Dim, Font, 0.5f * S);
		Y += 24.0f * S;
		for (int32 Slot = 0; Slot < 4; ++Slot)
		{
			const int32 Code = Team * 4 + Slot;
			const int32 Holder = From.SlotOwner(Team, Slot);
			const FString Who = Holder < 0 ? FString(TEXT("computer")) : Holder == From.LocalPlayer ? FString(TEXT("you")) : From.Players[Holder].Name;
			const FString Class = From.Setup.bDraft ? FString(TEXT("drafted")) : ClassName(From.Setup.Rosters[Team][Slot]);
			const FString Label = FString::Printf(TEXT("%d.  %s"), Slot + 1, *Class);
			const float RX = CX + 14.0f * S;
			const float RW = ColW - 28.0f * S;
			const float RH = 38.0f * S;
			if (!From.Setup.bDraft && From.LobbyMayPick(From.LocalPlayer, Code))
			{
				MenuButton(RX, Y, RW, RH, Label + TEXT("   ·   ") + Who, ETMHudAction::LobbySlot, Code, Holder == From.LocalPlayer);
			}
			else
			{
				Panel(RX, Y, RW, RH, FLinearColor(0.07f, 0.08f, 0.11f, 0.9f), FLinearColor(0.3f, 0.32f, 0.38f, 0.6f), 1.0f);
				Text(Label, RX + 14.0f * S, Y + 8.0f * S, TextColour, Font, 0.58f * S);
				const FVector2D WhoSize = TextSize(Who, Font, 0.55f * S);
				Text(Who, RX + RW - 14.0f * S - WhoSize.X, Y + 9.0f * S, Dim, Font, 0.55f * S);
			}
			Y += RH + 6.0f * S;
		}
	}

	// The host's settings, which the others only see.
	float Y = ColTop + ColH + 18.0f * S;
	const float BH = 46.0f * S;
	if (bHost)
	{
		float BX = PX + 30.0f * S;
		MenuButton(BX, Y, 200.0f * S, BH, TEXT("Battle settings"), ETMHudAction::LobbySettings, -1, false, TEXT("map, rules, items"));
		BX += 210.0f * S;
		MenuButton(BX, Y, 200.0f * S, BH, From.Setup.bDraft ? TEXT("Draft: on") : TEXT("Draft: off"), ETMHudAction::DraftToggle, -1,
			From.Setup.bDraft, TEXT("bans and serpentine picks"));
		BX += 210.0f * S;
		if (From.Setup.bDraft)
		{
			MenuButton(BX, Y, 200.0f * S, BH, From.Setup.DraftSeconds > 0 ? FString::Printf(TEXT("Pick timer: %d s"), From.Setup.DraftSeconds)
				: FString(TEXT("Pick timer: off")), ETMHudAction::DraftTimer, -1, false, TEXT("seconds for each choice"));
		}
	}
	else
	{
		Text(From.Setup.bDraft ? (From.Setup.DraftSeconds > 0 ? FString::Printf(TEXT("Draft on: %d s for each ban and pick."), From.Setup.DraftSeconds)
			: FString(TEXT("Draft on, no pick timer."))) : FString(TEXT("No draft: each player chooses their own units' classes.")),
			PX + 30.0f * S, Y + 12.0f * S, Dim, Font, 0.55f * S);
	}
	Y += BH + 14.0f * S;
	for (const FString& Line : { From.OnlineStatus, bHost ? From.OnlineAddresses : FString(), bHost ? From.OnlineRouter : FString() })
	{
		if (!Line.IsEmpty())
		{
			Text(Line, PX + 30.0f * S, Y, Line == From.OnlineStatus ? TextColour : Dim, Font, 0.5f * S);
			Y += 22.0f * S;
		}
	}

	// Leave, and Start or Ready.
	const float BY = PY + PH - 64.0f * S;
	MenuButton(PX + 30.0f * S, BY, 160.0f * S, BH, TEXT("Leave  (Esc)"), ETMHudAction::LobbyLeave);
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
