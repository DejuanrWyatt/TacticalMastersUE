// Replays: every battle kept as what it started from and the orders applied to
// it, and played back through the same door the orders first came through.
// Docs/design/feat-replays.md.
//
// The rules are deterministic (SimOrder.h): the same start and the same orders
// are the same battle, tick for tick. So a replay holds no positions, no
// health, no pictures -- only the start (classes, items, rule numbers, boss,
// seed) and the list of orders, time passing among them. Watching one builds
// the battle again from that start and submits the orders at the pace asked
// for. A checksum of the whole battle is written every minute of play and at
// the end, so a replay made before the rules or the class files changed says
// where it stops matching rather than quietly showing a different battle.

#include "TMBattleDirector.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#include "SimItem.h"
#include "SimOrderText.h"
#include "TMBattleHud.h"
#include "TMNet.h"

namespace TMReplay
{
	const TCHAR* const Format = TEXT("tactical-masters-replay");
	constexpr int32 Version = 1;
	/** Replays kept: the oldest go when there are more. */
	constexpr int32 Kept = 50;
	/** A checksum every minute of play. */
	constexpr int32 SumEveryTicks = 60 * TMSim::Pace::TicksPerSecond;
	/** The speeds the bar offers. */
	const float Speeds[4] = { 0.5f, 1.0f, 2.0f, 4.0f };
	/** Replays on one page of the list. */
	constexpr int32 PerPage = 8;

	FString Folder()
	{
		return FPaths::ProjectSavedDir() / TEXT("Replays");
	}

	FString Hex(uint64 Value)
	{
		return FString::Printf(TEXT("%016llx"), static_cast<unsigned long long>(Value));
	}

	uint64 FromHex(const FString& Text)
	{
		return FCString::Strtoui64(*Text, nullptr, 16);
	}

	/** "a 120": time passing, and how much. */
	bool IsTime(const FString& Step, int32& Ticks)
	{
		if (!Step.StartsWith(TEXT("a ")))
		{
			return false;
		}
		Ticks = FCString::Atoi(*Step.Mid(2));
		return true;
	}
}

// ================================================================ recording

void ATMBattleDirector::BeginRecording()
{
	// Called by BuildBattle just before Battle.Start: what the battle starts
	// from, as the rules hold it, rather than as the setup screen put it -- the
	// rule numbers after Developer Tools and the setup, the items after the
	// computer's picks and the points check.
	Recording = FTMReplay();
	Recording.MadeAt = FDateTime::Now().ToIso8601();
	Recording.Protocol = FTMNet::ProtocolVersion;
	Recording.MapId = UTF8_TO_TCHAR(Setup.MapId.c_str());
	Recording.ThemeId = Setup.ThemeId;
	Recording.Mode = bOnline ? FString(TEXT("online")) : Setup.Mode;
	for (int32 Team = 0; Team < 2; ++Team)
	{
		Recording.Sides[Team] = bOnline ? SideNames(Team)
			: ComputerPlays(Team) ? FString::Printf(TEXT("Computer (%s)"), *Setup.Difficulty[Team]) : FString(TEXT("Player"));
	}
	Recording.Seed = BattleSeed;
	for (const TMSim::FTuningKey& Key : TMSim::TuningKeys())
	{
		Recording.Tuning.Add(UTF8_TO_TCHAR(Key.Key), Battle.Tuning.*Key.Member);
	}
	for (const TMSim::FUnit& Unit : Battle.Units)
	{
		Recording.Jobs.Add(UTF8_TO_TCHAR(Unit.Job.c_str()));
		for (int32 Slot = 0; Slot < 3; ++Slot)
		{
			Recording.Gear.Add(Unit.Gear[Slot] ? FString(UTF8_TO_TCHAR(Unit.Gear[Slot]->Id.c_str())) : FString());
		}
	}
	Recording.BossJob = UTF8_TO_TCHAR(Battle.BossJob.c_str());
	RecordedMinutes = 0;
	bRecordingSaved = false;
}

void ATMBattleDirector::RecordApplied(const TMSim::FOrder& Order, const TMSim::FTickReport& Report)
{
	if (Order.Type == TMSim::EOrderType::Advance)
	{
		// Runs of time passing are one step: a battle is mostly waiting.
		int32 Before = 0;
		if (Recording.Steps.Num() > 0 && TMReplay::IsTime(Recording.Steps.Last(), Before))
		{
			Recording.Steps.Last() = FString::Printf(TEXT("a %d"), Before + Order.Ticks);
		}
		else
		{
			Recording.Steps.Add(FString::Printf(TEXT("a %d"), Order.Ticks));
		}
		// A whole minute passed in this step: the battle's checksum where it ended,
		// which playback stops at exactly to compare.
		if (Battle.TickCount >= (RecordedMinutes + 1) * TMReplay::SumEveryTicks)
		{
			RecordedMinutes = Battle.TickCount / TMReplay::SumEveryTicks;
			Recording.Sums.Emplace(Battle.TickCount, Battle.Checksum());
			// Split the step here, so the checksum's tick is a step's end.
			Recording.Steps.Add(TEXT("a 0"));
		}
	}
	else
	{
		Recording.Steps.Add(UTF8_TO_TCHAR(TMSim::OrderToText(Order).c_str()));
	}

	// The moments the timeline marks.
	for (const TMSim::FEvent& Event : Report.Events)
	{
		FTMReplayMark Mark;
		Mark.Tick = Battle.TickCount;
		Mark.Unit = Event.Unit;
		const TMSim::FUnit* Who = Battle.FindUnit(Event.Unit);
		Mark.Team = Who ? Who->Team : -1;
		switch (Event.Kind)
		{
		case TMSim::EEventKind::Knocked: Mark.Kind = TEXT("ko"); break;
		case TMSim::EEventKind::Revived: Mark.Kind = TEXT("revive"); break;
		case TMSim::EEventKind::Captured: Mark.Kind = TEXT("tower"); Mark.Team = Event.By; break;
		case TMSim::EEventKind::CampCleared: Mark.Kind = TEXT("camp"); Mark.Team = -1; break;
		case TMSim::EEventKind::Won: Mark.Kind = TEXT("won"); Mark.Team = Battle.Winner; break;
		default: continue;
		}
		// Monsters falling are the camps' business, marked when the camp is cleared.
		if (Mark.Kind == TEXT("ko") && Who && Who->bMonster)
		{
			continue;
		}
		Recording.Marks.Add(Mark);
	}
}

void ATMBattleDirector::SaveRecording()
{
	if (bRecordingSaved || bReplaying || Recording.Steps.Num() == 0)
	{
		return;
	}
	bRecordingSaved = true;
	Recording.Winner = Battle.Winner;
	Recording.Ticks = Battle.TickCount;
	Recording.FinalSum = Battle.Checksum();

	const FString Dir = TMReplay::Folder();
	IFileManager::Get().MakeDirectory(*Dir, true);
	const FString Name = FString::Printf(TEXT("%s_%s_%llu.tmreplay"), *FDateTime::Now().ToString(TEXT("%Y-%m-%d_%H-%M-%S")),
		*Recording.MapId, static_cast<unsigned long long>(Recording.Seed));
	const FString File = Dir / Name;
	if (!WriteReplay(Recording, File))
	{
		UE_LOG(LogTemp, Warning, TEXT("REPLAY could not be written to %s"), *File);
		return;
	}
	LastReplayFile = File;
	UE_LOG(LogTemp, Log, TEXT("REPLAY saved: %s (%d steps, %d ticks)"), *File, Recording.Steps.Num(), Recording.Ticks);

	// The newest Kept stay; the names start with the date, so they sort by age.
	TArray<FString> Found;
	IFileManager::Get().FindFiles(Found, *(Dir / TEXT("*.tmreplay")), true, false);
	Found.Sort();
	for (int32 Index = 0; Index + TMReplay::Kept < Found.Num(); ++Index)
	{
		IFileManager::Get().Delete(*(Dir / Found[Index]));
	}
}

// ================================================================ the file

bool ATMBattleDirector::WriteReplay(const FTMReplay& Replay, const FString& File)
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("format"), TMReplay::Format);
	Root->SetNumberField(TEXT("version"), TMReplay::Version);
	Root->SetNumberField(TEXT("protocol"), Replay.Protocol);
	Root->SetStringField(TEXT("made"), Replay.MadeAt);
	Root->SetStringField(TEXT("map"), Replay.MapId);
	Root->SetStringField(TEXT("theme"), Replay.ThemeId);
	Root->SetStringField(TEXT("mode"), Replay.Mode);
	TArray<TSharedPtr<FJsonValue>> Sides;
	Sides.Add(MakeShared<FJsonValueString>(Replay.Sides[0]));
	Sides.Add(MakeShared<FJsonValueString>(Replay.Sides[1]));
	Root->SetArrayField(TEXT("sides"), Sides);
	Root->SetStringField(TEXT("seed"), TMReplay::Hex(Replay.Seed));
	TSharedRef<FJsonObject> Tuning = MakeShared<FJsonObject>();
	for (const TPair<FString, double>& Pair : Replay.Tuning)
	{
		Tuning->SetNumberField(Pair.Key, Pair.Value);
	}
	Root->SetObjectField(TEXT("tuning"), Tuning);
	TArray<TSharedPtr<FJsonValue>> Units;
	for (int32 Index = 0; Index < Replay.Jobs.Num(); ++Index)
	{
		TSharedRef<FJsonObject> Unit = MakeShared<FJsonObject>();
		Unit->SetStringField(TEXT("class"), Replay.Jobs[Index]);
		TArray<TSharedPtr<FJsonValue>> Gear;
		for (int32 Slot = 0; Slot < 3; ++Slot)
		{
			const int32 At = Index * 3 + Slot;
			Gear.Add(MakeShared<FJsonValueString>(Replay.Gear.IsValidIndex(At) ? Replay.Gear[At] : FString()));
		}
		Unit->SetArrayField(TEXT("items"), Gear);
		Units.Add(MakeShared<FJsonValueObject>(Unit));
	}
	Root->SetArrayField(TEXT("units"), Units);
	Root->SetStringField(TEXT("boss"), Replay.BossJob);
	Root->SetStringField(TEXT("start"), TMReplay::Hex(Replay.StartSum));
	TArray<TSharedPtr<FJsonValue>> Steps;
	for (const FString& Step : Replay.Steps)
	{
		Steps.Add(MakeShared<FJsonValueString>(Step));
	}
	Root->SetArrayField(TEXT("orders"), Steps);
	TArray<TSharedPtr<FJsonValue>> Sums;
	for (const TPair<int32, uint64>& Sum : Replay.Sums)
	{
		Sums.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("%d %s"), Sum.Key, *TMReplay::Hex(Sum.Value))));
	}
	Root->SetArrayField(TEXT("sums"), Sums);
	TArray<TSharedPtr<FJsonValue>> Marks;
	for (const FTMReplayMark& Mark : Replay.Marks)
	{
		TSharedRef<FJsonObject> One = MakeShared<FJsonObject>();
		One->SetNumberField(TEXT("tick"), Mark.Tick);
		One->SetStringField(TEXT("kind"), Mark.Kind);
		One->SetNumberField(TEXT("unit"), Mark.Unit);
		One->SetNumberField(TEXT("team"), Mark.Team);
		Marks.Add(MakeShared<FJsonValueObject>(One));
	}
	Root->SetArrayField(TEXT("marks"), Marks);
	Root->SetNumberField(TEXT("winner"), Replay.Winner);
	Root->SetNumberField(TEXT("ticks"), Replay.Ticks);
	Root->SetStringField(TEXT("final"), TMReplay::Hex(Replay.FinalSum));

	FString Text;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
	if (!FJsonSerializer::Serialize(Root, Writer))
	{
		return false;
	}
	return FFileHelper::SaveStringToFile(Text, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

bool ATMBattleDirector::ReadReplay(const FString& File, FTMReplay& Out, FString& Problem)
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *File))
	{
		Problem = TEXT("The replay file could not be read.");
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		Problem = TEXT("The replay file is not a replay.");
		return false;
	}
	if (Root->GetStringField(TEXT("format")) != TMReplay::Format || static_cast<int32>(Root->GetNumberField(TEXT("version"))) != TMReplay::Version)
	{
		Problem = TEXT("The replay file is from another version of the replay format.");
		return false;
	}
	Out = FTMReplay();
	Out.Protocol = static_cast<int32>(Root->GetNumberField(TEXT("protocol")));
	Out.MadeAt = Root->GetStringField(TEXT("made"));
	Out.MapId = Root->GetStringField(TEXT("map"));
	Out.ThemeId = Root->GetStringField(TEXT("theme"));
	Out.Mode = Root->GetStringField(TEXT("mode"));
	const TArray<TSharedPtr<FJsonValue>>* Sides = nullptr;
	if (Root->TryGetArrayField(TEXT("sides"), Sides) && Sides->Num() == 2)
	{
		Out.Sides[0] = (*Sides)[0]->AsString();
		Out.Sides[1] = (*Sides)[1]->AsString();
	}
	Out.Seed = TMReplay::FromHex(Root->GetStringField(TEXT("seed")));
	const TSharedPtr<FJsonObject>* Tuning = nullptr;
	if (Root->TryGetObjectField(TEXT("tuning"), Tuning))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Tuning)->Values)
		{
			Out.Tuning.Add(Pair.Key, Pair.Value->AsNumber());
		}
	}
	const TArray<TSharedPtr<FJsonValue>>* Units = nullptr;
	if (Root->TryGetArrayField(TEXT("units"), Units))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Units)
		{
			const TSharedPtr<FJsonObject> Unit = Value->AsObject();
			if (!Unit.IsValid())
			{
				continue;
			}
			Out.Jobs.Add(Unit->GetStringField(TEXT("class")));
			const TArray<TSharedPtr<FJsonValue>>* Gear = nullptr;
			for (int32 Slot = 0; Slot < 3; ++Slot)
			{
				const bool bHas = Unit->TryGetArrayField(TEXT("items"), Gear) && Gear->IsValidIndex(Slot);
				Out.Gear.Add(bHas ? (*Gear)[Slot]->AsString() : FString());
			}
		}
	}
	Out.BossJob = Root->GetStringField(TEXT("boss"));
	Out.StartSum = TMReplay::FromHex(Root->GetStringField(TEXT("start")));
	const TArray<TSharedPtr<FJsonValue>>* Steps = nullptr;
	if (Root->TryGetArrayField(TEXT("orders"), Steps))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Steps)
		{
			Out.Steps.Add(Value->AsString());
		}
	}
	const TArray<TSharedPtr<FJsonValue>>* Sums = nullptr;
	if (Root->TryGetArrayField(TEXT("sums"), Sums))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Sums)
		{
			FString TickText;
			FString SumText;
			if (Value->AsString().Split(TEXT(" "), &TickText, &SumText))
			{
				Out.Sums.Emplace(FCString::Atoi(*TickText), TMReplay::FromHex(SumText));
			}
		}
	}
	const TArray<TSharedPtr<FJsonValue>>* Marks = nullptr;
	if (Root->TryGetArrayField(TEXT("marks"), Marks))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Marks)
		{
			const TSharedPtr<FJsonObject> One = Value->AsObject();
			if (!One.IsValid())
			{
				continue;
			}
			FTMReplayMark Mark;
			Mark.Tick = static_cast<int32>(One->GetNumberField(TEXT("tick")));
			Mark.Kind = One->GetStringField(TEXT("kind"));
			Mark.Unit = static_cast<int32>(One->GetNumberField(TEXT("unit")));
			Mark.Team = static_cast<int32>(One->GetNumberField(TEXT("team")));
			Out.Marks.Add(Mark);
		}
	}
	Out.Winner = static_cast<int32>(Root->GetNumberField(TEXT("winner")));
	Out.Ticks = static_cast<int32>(Root->GetNumberField(TEXT("ticks")));
	Out.FinalSum = TMReplay::FromHex(Root->GetStringField(TEXT("final")));
	if (Out.Jobs.Num() == 0 || Out.Steps.Num() == 0)
	{
		Problem = TEXT("The replay file holds no battle.");
		return false;
	}
	return true;
}

// ================================================================ the list

void ATMBattleDirector::OpenReplays()
{
	if (bReplaying)
	{
		LeaveReplay(false);
	}
	Screen = EScreen::Replays;
	bMenuOpen = false;
	ReplayListPage = 0;
	ReplayList.Reset();

	const FString Dir = TMReplay::Folder();
	TArray<FString> Found;
	IFileManager::Get().FindFiles(Found, *(Dir / TEXT("*.tmreplay")), true, false);
	Found.Sort([](const FString& A, const FString& B) { return B < A; });
	for (const FString& Name : Found)
	{
		FTMReplay Replay;
		FString Problem;
		FTMReplayEntry Entry;
		Entry.File = Dir / Name;
		if (ReadReplay(Entry.File, Replay, Problem))
		{
			Entry.MadeAt = Replay.MadeAt;
			Entry.MapId = Replay.MapId;
			Entry.Sides[0] = Replay.Sides[0];
			Entry.Sides[1] = Replay.Sides[1];
			Entry.Winner = Replay.Winner;
			Entry.Ticks = Replay.Ticks;
		}
		else
		{
			Entry.MadeAt = Name;
			Entry.MapId = Problem;
		}
		ReplayList.Add(Entry);
	}
}

// ================================================================ watching

void ATMBattleDirector::WatchReplay(const FString& File)
{
	FTMReplay Replay;
	FString Problem;
	if (File.IsEmpty())
	{
		// The battle just played: the recording itself, saved or not.
		Replay = Recording;
		Replay.Winner = Battle.Winner;
		Replay.Ticks = Battle.TickCount;
		Replay.FinalSum = Battle.Winner != -1 ? Battle.Checksum() : 0;
	}
	else if (!ReadReplay(File, Replay, Problem))
	{
		Tell(Problem);
		return;
	}
	if (Replay.Steps.Num() == 0)
	{
		Tell(TEXT("There is no battle to replay yet."));
		return;
	}
	// Leaving an online match behind: a replay is watched alone.
	if (bOnline || Net.IsValid())
	{
		LeaveOnline();
	}
	if (!bReplaying)
	{
		SetupBeforeReplay = Setup;
		bComputerPlayedBeforeReplay[0] = bComputerPlaysTeam0;
		bComputerPlayedBeforeReplay[1] = bComputerPlaysTeam1;
	}
	Watching = Replay;
	bReplaying = true;
	ReplayView = -1;
	ReplaySpeed = 1.0f;
	ReplayProblem.Reset();
	if (Replay.Protocol != FTMNet::ProtocolVersion)
	{
		ReplayProblem = FString::Printf(TEXT("Made with an older version of the rules (%d, now %d): it may not play the same."),
			Replay.Protocol, FTMNet::ProtocolVersion);
	}
	RestartReplay();
}

void ATMBattleDirector::RestartReplay()
{
	// The battle's start, as recorded: its map and classes go through the setup
	// (BuildBattle builds from it); its rules, items, boss and seed are put in
	// by ApplyReplayStart just before the battle starts.
	Setup.MapId = TCHAR_TO_UTF8(*Watching.MapId);
	Setup.ThemeId = Watching.ThemeId;
	Setup.Mode = TEXT("cpu");
	for (int32 Index = 0; Index < 8; ++Index)
	{
		Setup.Rosters[Index / 4][Index % 4] = Watching.Jobs.IsValidIndex(Index) ? TCHAR_TO_UTF8(*Watching.Jobs[Index]) : std::string();
	}
	// Nobody plays: everyone is watching.
	bComputerPlaysTeam0 = true;
	bComputerPlaysTeam1 = true;
	BattleSeed = Watching.Seed;
	Screen = EScreen::Battle;
	bMenuOpen = false;
	bPaused = false;
	bReplayPlaying = true;
	ReplayCursor = 0;
	ReplayCursorUsed = 0;
	ReplayRemainder = 0.0f;
	ReplaySumNext = 0;
	BuildBattle();
	Log.Reset();
	LogEntries.Reset();
	LogGroup = 0;
	OrdersGiven = 0;
	DecidedFor = 0.0f;
	bSaidWon = false;
	LogNote(FString::Printf(TEXT("Replay: %s. Blue: %s. Red: %s."), *Watching.MapId, *Watching.Sides[0], *Watching.Sides[1]));

	int32 Heroes = 0;
	for (const TMSim::FUnit& Each : Battle.Units)
	{
		Heroes += Each.bMonster ? 0 : 1;
	}
	if (Heroes < Watching.Jobs.Num())
	{
		ReplayProblem = TEXT("A class this battle had is no longer in the game: the replay can't be played.");
		bReplayPlaying = false;
	}
	else if (Battle.Checksum() != Watching.StartSum && Watching.StartSum != 0)
	{
		ReplayProblem = TEXT("The classes, items or map have changed since this battle: it can't play the same.");
	}
}

void ATMBattleDirector::ApplyReplayStart()
{
	// BuildBattle, just before Battle.Start, while watching.
	Battle.Tuning = TMSim::GameTuning();
	for (const TMSim::FTuningKey& Key : TMSim::TuningKeys())
	{
		if (const double* Value = Watching.Tuning.Find(UTF8_TO_TCHAR(Key.Key)))
		{
			Battle.Tuning.*Key.Member = *Value;
		}
	}
	for (int32 Index = 0; Index < static_cast<int32>(Battle.Units.size()); ++Index)
	{
		for (int32 Slot = 0; Slot < 3; ++Slot)
		{
			const int32 At = Index * 3 + Slot;
			const FString Id = Watching.Gear.IsValidIndex(At) ? Watching.Gear[At] : FString();
			Battle.Units[Index].Gear[Slot] = Id.IsEmpty() ? nullptr : TMSim::FindItem(TCHAR_TO_UTF8(*Id));
		}
	}
	Battle.BossJob = TCHAR_TO_UTF8(*Watching.BossJob);
	BattleSeed = Watching.Seed;
}

void ATMBattleDirector::LeaveReplay(bool bToList)
{
	if (!bReplaying)
	{
		return;
	}
	bReplaying = false;
	bReplayQuiet = false;
	ReplayProblem.Reset();
	Setup = SetupBeforeReplay;
	bComputerPlaysTeam0 = bComputerPlayedBeforeReplay[0];
	bComputerPlaysTeam1 = bComputerPlayedBeforeReplay[1];
	// The board behind the menus is the setup's again, not the replay's.
	BuildBattle();
	if (bToList)
	{
		OpenReplays();
	}
	else
	{
		OpenTitle();
	}
}

int32 ATMBattleDirector::PlayReplayStep(int32 Budget)
{
	if (ReplayCursor >= Watching.Steps.Num())
	{
		return 0;
	}
	const FString& Step = Watching.Steps[ReplayCursor];
	int32 Ticks = 0;
	if (TMReplay::IsTime(Step, Ticks))
	{
		// As much of this stretch of time as the budget allows, in steps the rules take.
		const int32 Left = Ticks - ReplayCursorUsed;
		const int32 Now = FMath::Min3(Left, Budget, TMSim::Pace::MaxAdvance);
		if (Now > 0)
		{
			bApplyingReplay = true;
			const FString Refused = Submit(TMSim::FOrder::MakeAdvance(Now));
			bApplyingReplay = false;
			if (!Refused.IsEmpty())
			{
				ReplayProblem = FString::Printf(TEXT("The replay stopped matching the battle: %s"), *Refused);
				ReplayCursor = Watching.Steps.Num();
				return 0;
			}
			ReplayCursorUsed += Now;
		}
		if (ReplayCursorUsed >= Ticks)
		{
			++ReplayCursor;
			ReplayCursorUsed = 0;
			// A step's end can be where a checksum was taken: compare.
			if (Watching.Sums.IsValidIndex(ReplaySumNext) && Watching.Sums[ReplaySumNext].Key == Battle.TickCount)
			{
				if (Watching.Sums[ReplaySumNext].Value != Battle.Checksum() && ReplayProblem.IsEmpty())
				{
					ReplayProblem = FString::Printf(TEXT("From %d:%02d this replay no longer matches the battle it recorded."),
						Battle.TickCount / TMSim::Pace::TicksPerSecond / 60, Battle.TickCount / TMSim::Pace::TicksPerSecond % 60);
				}
				++ReplaySumNext;
			}
		}
		return Now;
	}

	TMSim::FOrder Order;
	const std::string Bad = TMSim::OrderFromText(TCHAR_TO_UTF8(*Step), Order);
	++ReplayCursor;
	if (!Bad.empty())
	{
		ReplayProblem = FString::Printf(TEXT("The replay has an order this version can't read: %hs"), Bad.c_str());
		ReplayCursor = Watching.Steps.Num();
		return 0;
	}
	bApplyingReplay = true;
	const FString Refused = Submit(Order);
	bApplyingReplay = false;
	if (!Refused.IsEmpty())
	{
		ReplayProblem = FString::Printf(TEXT("The replay stopped matching the battle: %s"), *Refused);
		ReplayCursor = Watching.Steps.Num();
	}
	return 0;
}

void ATMBattleDirector::AdvanceReplay(float RealSeconds)
{
	if (!bReplayPlaying || ReplayCursor >= Watching.Steps.Num())
	{
		ReplayRemainder = 0.0f;
		return;
	}
	ReplayRemainder += RealSeconds * ReplaySpeed;
	int32 Budget = FMath::FloorToInt(ReplayRemainder * TMSim::Pace::TicksPerSecond);
	// A slow frame doesn't run a minute of battle at once.
	Budget = FMath::Min(Budget, FMath::CeilToInt(30 * ReplaySpeed));
	ReplayRemainder -= Budget / static_cast<float>(TMSim::Pace::TicksPerSecond);
	ReplayRemainder = FMath::Max(0.0f, ReplayRemainder);
	// Orders between the ticks are applied as they come; time takes the budget.
	int32 Guard = 0;
	while (ReplayCursor < Watching.Steps.Num() && Guard++ < 10000)
	{
		int32 Ticks = 0;
		const bool bTime = TMReplay::IsTime(Watching.Steps[ReplayCursor], Ticks);
		if (bTime && Ticks - ReplayCursorUsed > 0 && Budget <= 0)
		{
			break;
		}
		Budget -= PlayReplayStep(Budget);
	}
	if (ReplayCursor >= Watching.Steps.Num() && Watching.FinalSum != 0 && Battle.Winner != -1
		&& Battle.Checksum() != Watching.FinalSum && ReplayProblem.IsEmpty())
	{
		ReplayProblem = TEXT("The replay ended differently from the battle it recorded.");
	}
}

void ATMBattleDirector::SeekReplay(int32 ToTick)
{
	if (!bReplaying)
	{
		return;
	}
	const bool bWasPlaying = bReplayPlaying;
	ToTick = FMath::Clamp(ToTick, 0, FMath::Max(0, Watching.Ticks));
	if (ToTick < Battle.TickCount)
	{
		const FString Problem = ReplayProblem;
		RestartReplay();
		ReplayProblem = Problem;
	}
	// Played on quietly: the rules only, no blows, effects or sounds on the way.
	bReplayQuiet = true;
	int32 Guard = 0;
	while (ReplayCursor < Watching.Steps.Num() && Battle.TickCount < ToTick && Guard++ < 200000)
	{
		PlayReplayStep(ToTick - Battle.TickCount);
	}
	// And the orders given at that very tick, so a jump lands after them.
	while (ReplayCursor < Watching.Steps.Num() && Guard++ < 200000)
	{
		int32 Ticks = 0;
		if (TMReplay::IsTime(Watching.Steps[ReplayCursor], Ticks) && Ticks - ReplayCursorUsed > 0)
		{
			break;
		}
		PlayReplayStep(0);
	}
	bReplayQuiet = false;
	bReplayPlaying = bWasPlaying;
	RefreshVisuals();
}

void ATMBattleDirector::StepReplay(int32 Direction)
{
	if (!bReplaying)
	{
		return;
	}
	bReplayPlaying = false;
	// Where each order that wasn't time passing was given.
	TArray<int32> At;
	int32 Clock = 0;
	for (const FString& Step : Watching.Steps)
	{
		int32 Ticks = 0;
		if (TMReplay::IsTime(Step, Ticks))
		{
			Clock += Ticks;
		}
		else if (At.Num() == 0 || At.Last() != Clock)
		{
			At.Add(Clock);
		}
	}
	const int32 Now = Battle.TickCount;
	int32 Target = Direction < 0 ? 0 : Watching.Ticks;
	for (const int32 Given : At)
	{
		if (Direction < 0 && Given < Now)
		{
			Target = Given;
		}
		if (Direction > 0 && Given > Now)
		{
			Target = Given;
			break;
		}
	}
	SeekReplay(Target);
}

bool ATMBattleDirector::ReplayKey(const FKey& Key)
{
	if (!bReplaying || Screen != EScreen::Battle)
	{
		return false;
	}
	if (Key == EKeys::SpaceBar || Key == EKeys::P)
	{
		bReplayPlaying = !bReplayPlaying;
		return true;
	}
	if (Key == EKeys::Left || Key == EKeys::Right)
	{
		StepReplay(Key == EKeys::Left ? -1 : 1);
		return true;
	}
	if (Key == EKeys::One || Key == EKeys::Two || Key == EKeys::Three || Key == EKeys::Four)
	{
		const int32 Index = Key == EKeys::One ? 0 : Key == EKeys::Two ? 1 : Key == EKeys::Three ? 2 : 3;
		ReplaySpeed = TMReplay::Speeds[Index];
		return true;
	}
	if (Key == EKeys::Escape)
	{
		LeaveReplay(true);
		return true;
	}
	// Keys that would start or change a battle mean nothing while watching one.
	if (Key == EKeys::R || Key == EKeys::Enter)
	{
		return true;
	}
	return false;
}

bool ATMBattleDirector::PressReplayButton(const FTMHudButton& Button)
{
	switch (Button.Action)
	{
	case ETMHudAction::TitleReplays:
	case ETMHudAction::ReplayListBack:
		if (Button.Action == ETMHudAction::ReplayListBack)
		{
			OpenTitle();
		}
		else
		{
			OpenReplays();
		}
		return true;
	case ETMHudAction::ReplayWatch:
		if (Button.Value < 0)
		{
			// The battle just played: its saved file if it was saved, else as recorded.
			WatchReplay(LastReplayFile);
		}
		else if (ReplayList.IsValidIndex(Button.Value))
		{
			WatchReplay(ReplayList[Button.Value].File);
		}
		return true;
	case ETMHudAction::ReplayDelete:
		if (ReplayList.IsValidIndex(Button.Value))
		{
			IFileManager::Get().Delete(*ReplayList[Button.Value].File);
			const int32 Page = ReplayListPage;
			OpenReplays();
			ReplayListPage = FMath::Clamp(Page, 0, FMath::Max(0, (ReplayList.Num() - 1) / TMReplay::PerPage));
		}
		return true;
	case ETMHudAction::ReplayPage:
		ReplayListPage = FMath::Clamp(ReplayListPage + Button.Value, 0, FMath::Max(0, (ReplayList.Num() - 1) / TMReplay::PerPage));
		return true;
	case ETMHudAction::ReplayPlay:
		if (bReplaying && Battle.Winner != -1 && ReplayCursor >= Watching.Steps.Num())
		{
			SeekReplay(0);
			bReplayPlaying = true;
		}
		else
		{
			bReplayPlaying = !bReplayPlaying;
		}
		return true;
	case ETMHudAction::ReplaySpeed:
		ReplaySpeed = TMReplay::Speeds[FMath::Clamp(Button.Value, 0, 3)];
		return true;
	case ETMHudAction::ReplayStep:
		StepReplay(Button.Value);
		return true;
	case ETMHudAction::ReplaySeek:
		SeekReplay(FMath::RoundToInt(Watching.Ticks * FMath::Clamp(Button.Value, 0, 1000) / 1000.0f));
		return true;
	case ETMHudAction::ReplayView:
		ReplayView = FMath::Clamp(Button.Value, -1, 1);
		return true;
	case ETMHudAction::ReplayAgain:
		SeekReplay(0);
		bReplayPlaying = true;
		return true;
	case ETMHudAction::ReplayLeave:
		LeaveReplay(true);
		return true;
	// The battle report (TMBattleHudReport.cpp).
	case ETMHudAction::ReportTab:
		ReportTab = FMath::Clamp(Button.Value, 0, 3);
		ReportUnit = -1;
		return true;
	case ETMHudAction::ReportUnit:
		ReportUnit = Button.Value;
		return true;
	case ETMHudAction::ReportHide:
		bReportHidden = !bReportHidden;
		return true;
	case ETMHudAction::ReportMoment:
		// A few seconds before the moment, playing.
		if (!bReplaying)
		{
			WatchReplay(LastReplayFile);
		}
		if (bReplaying)
		{
			SeekReplay(FMath::Max(0, Button.Value - 5 * TMSim::Pace::TicksPerSecond));
			bReplayPlaying = true;
		}
		return true;
	default:
		return false;
	}
}
