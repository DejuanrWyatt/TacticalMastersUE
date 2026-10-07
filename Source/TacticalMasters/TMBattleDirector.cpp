#include "TMBattleDirector.h"
#include "TMDrawable.h"

#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/Engine.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "GameFramework/PlayerController.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Components/InputComponent.h"
#include "TMBattleHud.h"
#include "TMVfxStudio.h"
#include "TMSoundStudio.h"
#include "TMAnimStudio.h"
#include "TMRobotPlayer.h"
#include "TMSettings.h"
#include "TMEos.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Particles/ParticleSystem.h"
#include "Kismet/GameplayStatics.h"
#include "Particles/ParticleSystemComponent.h"
#include "NiagaraComponent.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/DateTime.h"

#include "SimOrderText.h"
#include "TMTextInput.h"
#include "TMViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "SimAbility.h"
#include "SimClassFile.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"

#include <algorithm>

ATMBattleDirector::ATMBattleDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ATMBattleDirector::BeginPlay()
{
	Super::BeginPlay();

	// Nothing that can't be drawn is ever unloaded: unloading one crashed the
	// renderer (TMDrawable.h, KeepBrokenLoaded).
	TMDrawable::WatchBrokenMaterials();

	// -tmvfxcatalog: no battle at all. The studio films every effect in the
	// project for the class creator, then ends the run itself.
	if (FParse::Param(FCommandLine::Get(), TEXT("tmvfxcatalog")))
	{
		if (UWorld* World = GetWorld())
		{
			World->SpawnActor<ATMVfxStudio>();
		}
		SetActorTickEnabled(false);
		return;
	}

	// -tmsoundcatalog: the same, for the sounds: each copied out as a .wav the
	// creator can play (TMSoundStudio.h).
	if (FParse::Param(FCommandLine::Get(), TEXT("tmsoundcatalog")))
	{
		if (UWorld* World = GetWorld())
		{
			World->SpawnActor<ATMSoundStudio>();
		}
		SetActorTickEnabled(false);
		return;
	}

	// -tmanimcatalog: the same, for the animation clips the character map
	// names, filmed on the bodies that wear them (TMAnimStudio.h).
	if (FParse::Param(FCommandLine::Get(), TEXT("tmanimcatalog")))
	{
		LoadCharacterMap();
		// Cast Studio's published picks are filmed too, so the creator can show them.
		LoadCastAnimation();
		if (UWorld* World = GetWorld())
		{
			if (ATMAnimStudio* Studio = World->SpawnActor<ATMAnimStudio>())
			{
				Studio->Begin(this);
			}
		}
		SetActorTickEnabled(false);
		return;
	}

	// -tmmap=crown_keep and -tmtheme=winter: the battle on that map, in that look,
	// without anyone at the setup screen.
	FString MapSwitch;
	if (FParse::Value(FCommandLine::Get(), TEXT("tmmap="), MapSwitch))
	{
		Setup.MapId = TCHAR_TO_UTF8(*MapSwitch);
		bBuilt = false;
	}
	FParse::Value(FCommandLine::Get(), TEXT("tmtheme="), Setup.ThemeId);

	// -tmroster=a,b,c,d: both sides field these four classes, by id, instead of
	// the default four. How a class made in the creator is watched in a real
	// battle without anyone at the setup screen.
	FString Roster;
	if (FParse::Value(FCommandLine::Get(), TEXT("tmroster="), Roster, false))
	{
		TArray<FString> Ids;
		Roster.ParseIntoArray(Ids, TEXT(","));
		for (int32 i = 0; i < 4 && i < Ids.Num(); ++i)
		{
			Setup.Rosters[0][i] = TCHAR_TO_UTF8(*Ids[i].TrimStartAndEnd());
			Setup.Rosters[1][i] = Setup.Rosters[0][i];
		}
		UE_LOG(LogTemp, Log, TEXT("both sides field %s"), *Roster);
		bBuilt = false;
	}

	// -tmhold=30 and -tmtime=180: the setup screen's other ways to win, for a
	// battle nobody sets up -- seconds holding the middle, and a time limit.
	double RuleSeconds = 0.0;
	if (FParse::Value(FCommandLine::Get(), TEXT("tmhold="), RuleSeconds))
	{
		Setup.CaptureSeconds = RuleSeconds;
		bBuilt = false;
	}
	if (FParse::Value(FCommandLine::Get(), TEXT("tmtime="), RuleSeconds))
	{
		Setup.BattleSeconds = RuleSeconds;
		bBuilt = false;
	}
	if (FParse::Value(FCommandLine::Get(), TEXT("tmplan="), RuleSeconds))
	{
		Setup.PlanningSeconds = RuleSeconds;
		bBuilt = false;
	}
	// -tmcamps=2: the neutral camps, as the setup screen's row (0 is none); -tmrandomboss.
	int32 Camps = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("tmcamps="), Camps))
	{
		Setup.CampLevel = FMath::Clamp(Camps, 0, 3);
		bOfferedCamps = true;
		bBuilt = false;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("tmrandomboss")))
	{
		Setup.bRandomBoss = true;
		bBuilt = false;
	}
	// -tmfriendlyfire=1: area blows hurt their own side too, as the setup screen's row.
	int32 FriendlyFire = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("tmfriendlyfire="), FriendlyFire))
	{
		Setup.bFriendlyFire = FriendlyFire != 0;
		bBuilt = false;
	}
	// -tmelements=1: element reactions, as the setup screen's row.
	int32 Elements = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("tmelements="), Elements))
	{
		Setup.bElements = Elements != 0;
		bOfferedElements = true;
		bBuilt = false;
	}
	// -tmtowers=4: how many watchtowers, as the setup screen's row (0 is none).
	int32 Towers = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("tmtowers="), Towers))
	{
		Setup.Watchtowers = FMath::Clamp(Towers, 0, 8);
		bOfferedTowers = true;  // what the command line says stands on the setup screen too
		bBuilt = false;
	}

	if (!bBuilt)
	{
		BuildBattle();
	}
	FrameTheBoard();

	// A headless run is a separate process, so the only way to ask it for pictures
	// is the command line: -tmcapture=15 takes one every fifteen seconds of battle.
	float Every = 0.0f;
	if (FParse::Value(FCommandLine::Get(), TEXT("tmcapture="), Every))
	{
		CaptureEverySeconds = Every;
		UE_LOG(LogTemp, Log, TEXT("taking a picture every %.1fs of battle"), Every);
	}

	// Nobody is there to click in an unattended run, so a side left to a person
	// would sit out every turn and the battle would never be decided -- and a
	// headless session that cannot end holds the DLL open. -tmwatch asks for the
	// same with a window, to watch the computer play itself.
	//
	// -tmplayblue keeps blue for a person even then. Nobody clicks, so blue's
	// turns run out and red wins, and the run still ends -- which is how pictures
	// of the player's HUD are taken without anyone at the machine.
	const bool bKeepBlue = FParse::Param(FCommandLine::Get(), TEXT("tmplayblue"));
	// -tmrobot: the robot playtester sits where a person would -- at the title
	// screen, with the mouse and keyboard -- and plays through the controls.
	if (FParse::Param(FCommandLine::Get(), TEXT("tmrobot")))
	{
		bRobotDriving = true;
		SetUpPlayerInput();
		Screen = EScreen::Title;
		if (UWorld* World = GetWorld())
		{
			World->SpawnActor<ATMRobotPlayer>();
		}
	}
	else if (bKeepBlue)
	{
		bComputerPlaysTeam0 = false;
		bComputerPlaysTeam1 = true;
		SetUpPlayerInput();
		if (FParse::Param(FCommandLine::Get(), TEXT("tmhudshots")))
		{
			HudShotsAt = 0.0f;
		}
	}
	else if (FApp::IsUnattended() || FParse::Param(FCommandLine::Get(), TEXT("tmwatch")))
	{
		bComputerPlaysTeam0 = true;
		bComputerPlaysTeam1 = true;
		UE_LOG(LogTemp, Log, TEXT("the computer plays both sides"));
		// -tmmenushots: pictures of the title and setup screens first, then the
		// battle as any unattended run plays it.
		if (FParse::Param(FCommandLine::Get(), TEXT("tmmenushots")))
		{
			Screen = EScreen::Title;
			MenuShotsAt = 0.0f;
		}
	}
	else
	{
		// Somebody is at this machine: they start at the title screen, with the
		// board built behind it and the clock stopped until a battle is started.
		SetUpPlayerInput();
		Screen = EScreen::Title;
	}
	// The online test (scripts\online-test.bat): -tmhost[=port] hosts the battle
	// the other switches set up, -tmjoin=address[:port] joins one, -tmnetbots
	// has the computer play this machine's side through the online path, and
	// -tmnetdesync makes the joiner's game differ once, to prove it is caught.
	// Through Epic (Docs/design/feat-online-eos.md): -tmeos with -tmhost offers a
	// join code too (-tmcode=ABCDEF picks it, for a test), -tmjoincode=ABCDEF joins
	// by one, and -tmeosuser=2 makes a second copy on this PC another device.
	bNetBots = FParse::Param(FCommandLine::Get(), TEXT("tmnetbots"));
	bNetDesync = FParse::Param(FCommandLine::Get(), TEXT("tmnetdesync"));
	FString NetSwitch;
	FString CodeSwitch;
	if (FParse::Param(FCommandLine::Get(), TEXT("tmeos")) || FParse::Value(FCommandLine::Get(), TEXT("tmjoincode="), CodeSwitch))
	{
		FTMEos::Get().Start(LocalName());
	}
	if (FParse::Value(FCommandLine::Get(), TEXT("tmjoincode="), CodeSwitch))
	{
		JoinCodeText = CodeSwitch;
		OfflineSetup = Setup;
		Screen = EScreen::Online;
		JoinByCode();
	}
	else if (FParse::Value(FCommandLine::Get(), TEXT("tmhost="), NetSwitch) || FParse::Param(FCommandLine::Get(), TEXT("tmhost")))
	{
		JoinPort = NetSwitch.IsNumeric() ? NetSwitch : FString::FromInt(FTMNet::DefaultPort);
		OfflineSetup = Setup;
		Setup.Mode = TEXT("online");
		Screen = EScreen::Setup;
		HostOnline();
	}
	else if (FParse::Value(FCommandLine::Get(), TEXT("tmjoin="), NetSwitch))
	{
		JoinAddress = NetSwitch;
		OfflineSetup = Setup;
		Screen = EScreen::Online;
		JoinOnline();
	}
	// Up even while watching: the turn order and the log are how a battle is read.
	ShowHud();
}

void ATMBattleDirector::ClearBattle()
{
	// What the feel kept from the last battle (TMBattleDirectorFeel.cpp).
	PredictedWalks.Reset();
	ClickMarks.Reset();
	QuickSlot = -1;
	BufferedKey = FKey();
	CloseUpUntil = 0.0;
	WalkPress = FTMWalkPress();
	// Not destroyed yet: hidden, stopped and set aside, destroyed a few seconds
	// on (RetiringVisuals, RetireOldVisuals).
	const double RetiredAt = FPlatformTime::Seconds();
	for (TObjectPtr<USkeletalMeshComponent>& Visual : UnitVisuals)
	{
		if (Visual)
		{
			Visual->SetVisibility(false);
			Visual->Stop();
			Visual->SetComponentTickEnabled(false);
			RetiringVisuals.Add(Visual);
			RetiringSince.Add(RetiredAt);
		}
	}
	UnitVisuals.Reset();
	for (TObjectPtr<UStaticMeshComponent>& Tile : TileVisuals)
	{
		if (Tile) { Tile->DestroyComponent(); }
	}
	TileVisuals.Reset();
	for (TObjectPtr<USceneComponent>& Prop : BoardProps)
	{
		if (Prop) { Prop->DestroyComponent(); }
	}
	BoardProps.Reset();
	TowerRoofs.Reset();
	TowerRoofOwner.Reset();
	Beacons.Reset();
	HazardParts.Reset();
	BoardLights.Reset();
	BoardLightBase.Reset();
	for (TObjectPtr<UPointLightComponent>& Light : ReadyLights)
	{
		if (Light) { Light->DestroyComponent(); }
	}
	ReadyLights.Reset();
	for (TObjectPtr<UPointLightComponent>& Light : StatusLights)
	{
		if (Light) { Light->DestroyComponent(); }
	}
	StatusLights.Reset();
	ClearBlows();
	ClearLooks();
	for (TObjectPtr<UTextRenderComponent>& Plate : Plates)
	{
		if (Plate) { Plate->DestroyComponent(); }
	}
	Plates.Reset();
	Battle.Units.clear();
	bBuilt = false;
	// A selection means nothing once the battle it was in is gone.
	Deselect();
	InspectedId = -1;
	ThreatSignature.Reset();
	ThreatNodes.clear();
	LogScroll = 0;
	bSaidWon = false;
	PendingAbility = FPendingAbility();
	// Plans were for that battle's units.
	Plans.Reset();
	GoTos.Reset();
	GoToEndUnit = -1;
	GoToHoverStops.clear();
	// Bars and marks over that battle's units.
	HpPops.Reset();
	BlowMarks.Reset();
	ReadySeen.Reset();
	bPlanMode = false;
	WayPoints.clear();
	// The camera rules' notes were about that battle's units (ids are used again).
	ReadyToastId = -1;
	PendingFollowId = -1;
	FollowArmedUntil = 0.0;
	bSnapToNext = false;
	CameraHeldWhy.Reset();
}

namespace
{
	/**
	 * Loads every class file in Content/Data/Classes into the rules, once per run.
	 * The files are Tactical Masters' own format, written by the class creator;
	 * the rules read them (SimClassFile.cpp) and this only finds them. A file the
	 * rules refuse is left out and said so, never half-loaded.
	 */
	void LoadClassFiles()
	{
		static bool bLoaded = false;
		if (bLoaded)
		{
			return;
		}
		bLoaded = true;
		const FString Dir = FPaths::ProjectContentDir() / TEXT("Data/Classes");
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(Dir / TEXT("*.tmclass.json")), true, false);
		Files.Sort();
		int32 Loaded = 0;
		for (const FString& File : Files)
		{
			FString Text;
			if (!FFileHelper::LoadFileToString(Text, *(Dir / File)))
			{
				UE_LOG(LogTemp, Warning, TEXT("could not read class file %s"), *File);
				continue;
			}
			const std::string Refused = TMSim::LoadClassFile(TCHAR_TO_UTF8(*Text));
			if (!Refused.empty())
			{
				UE_LOG(LogTemp, Warning, TEXT("class file %s left out: %hs"), *File, Refused.c_str());
				continue;
			}
			++Loaded;
		}
		UE_LOG(LogTemp, Log, TEXT("loaded %d of %d class files; %d classes in all"),
			Loaded, Files.Num(), static_cast<int32>(TMSim::AllJobs().size()));

		// The neutral monsters are class files too, kept apart so no side fields one
		// (Docs/design/feat-neutral-camps.md).
		const FString MonsterDir = FPaths::ProjectContentDir() / TEXT("Data/Monsters");
		TArray<FString> MonsterFiles;
		IFileManager::Get().FindFiles(MonsterFiles, *(MonsterDir / TEXT("*.tmclass.json")), true, false);
		MonsterFiles.Sort();
		for (const FString& File : MonsterFiles)
		{
			FString Text;
			if (!FFileHelper::LoadFileToString(Text, *(MonsterDir / File)))
			{
				continue;
			}
			const std::string Refused = TMSim::LoadClassFile(TCHAR_TO_UTF8(*Text));
			if (!Refused.empty())
			{
				UE_LOG(LogTemp, Warning, TEXT("monster file %s left out: %hs"), *File, Refused.c_str());
			}
		}
		UE_LOG(LogTemp, Log, TEXT("%d monsters"), static_cast<int32>(TMSim::AllMonsters().size()));
	}

	/**
	 * Every item file in Content/Data/Items, once per run (SimItem.h). One the
	 * rules refuse is left out and said so, as classes are.
	 */
	void LoadItemFiles()
	{
		static bool bLoaded = false;
		if (bLoaded)
		{
			return;
		}
		bLoaded = true;
		const FString Dir = FPaths::ProjectContentDir() / TEXT("Data/Items");
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(Dir / TEXT("*.tmitem.json")), true, false);
		Files.Sort();
		for (const FString& File : Files)
		{
			FString Text;
			const std::string Refused = FFileHelper::LoadFileToString(Text, *(Dir / File))
				? TMSim::LoadItemFile(TCHAR_TO_UTF8(*Text)) : std::string("could not be read");
			if (!Refused.empty())
			{
				UE_LOG(LogTemp, Warning, TEXT("item file %s left out: %hs"), *File, Refused.c_str());
			}
		}
		UE_LOG(LogTemp, Log, TEXT("%d items"), static_cast<int32>(TMSim::AllItems().size()));
	}

	/**
	 * Every map file in Content/Data/Maps, once per run. The rules read and check
	 * each (TMSim::ReadMapFile); one they refuse is left out and said so.
	 */
	void LoadMapFiles()
	{
		static bool bLoaded = false;
		if (bLoaded)
		{
			return;
		}
		bLoaded = true;
		const FString Dir = FPaths::ProjectContentDir() / TEXT("Data/Maps");
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(Dir / TEXT("*.tmmap.json")), true, false);
		Files.Sort();
		for (const FString& File : Files)
		{
			FString Text;
			TMSim::FMapDef Def;
			std::string Refused = FFileHelper::LoadFileToString(Text, *(Dir / File))
				? TMSim::ReadMapFile(TCHAR_TO_UTF8(*Text), Def) : std::string("could not be read");
			if (Refused.empty())
			{
				Refused = TMSim::RegisterMap(Def);
			}
			if (!Refused.empty())
			{
				UE_LOG(LogTemp, Warning, TEXT("map file %s left out: %hs"), *File, Refused.c_str());
			}
		}
		UE_LOG(LogTemp, Log, TEXT("%d maps"), static_cast<int32>(TMSim::AllMaps().size()));
	}
}

void ATMBattleDirector::BuildBattle()
{
	// The game's changes to the built-in classes (the Knight and the Archer),
	// before anything reads them; once only (TMSim::ApplyGameBalance).
	TMSim::ApplyGameBalance();
	LoadClassFiles();
	LoadItemFiles();
	LoadMapFiles();
	LoadThemes();
	ClearBattle();

	// Two sides of four, as the Godot game sets up, with the classes the setup
	// screen chose. They are the six built-in ones for now; the other 81 arrive
	// with the class importer.
	if (!bSetupChosen)
	{
		Setup.Difficulty[0] = ComputerSkill;
		Setup.Difficulty[1] = ComputerSkill;
	}

	// The map first: units stand on it, and the pathfinder needs it before
	// anything can be asked about where a unit could walk.
	const TMSim::FMapDef& MapDef = TMSim::FindMap(Setup.MapId);
	Battle.Map.BuildMirrored(MapDef.Top, MapDef.Grass);

	// Where the map puts the two sides, in metres. Blue as written, red at the
	// mirrored spots, which is how a symmetric map is laid out; the first of
	// each is the side's spawn point (map_data.gd:152).
	const TMSim::FVec2 BlueSpawns[4] = { MapDef.Spawns[0], MapDef.Spawns[1], MapDef.Spawns[2], MapDef.Spawns[3] };
	const TMSim::FVec2 Size = Battle.Map.SizeMeters();
	Battle.SpawnPoints[0] = BlueSpawns[0];
	Battle.SpawnPoints[1] = TMSim::FVec2(Size.X - BlueSpawns[0].X, Size.Y - BlueSpawns[0].Y);

	for (int32 Index = 0; Index < 8; ++Index)
	{
		const std::string& JobId = Setup.Rosters[Index < 4 ? 0 : 1][Index % 4];
		const TMSim::FJobDef* Job = TMSim::FindJob(JobId);
		if (!Job)
		{
			UE_LOG(LogTemp, Warning, TEXT("No class registered called %hs"), JobId.c_str());
			continue;
		}

		TMSim::FUnit Unit;
		Unit.Id = Index;
		Unit.Team = Index < 4 ? 0 : 1;
		Unit.Job = JobId;
		Unit.Stats = &Job->Stats;

		const TMSim::FVec2 Spawn = BlueSpawns[Index % 4];
		Unit.Pos = Unit.Team == 0 ? Spawn : TMSim::FVec2(Size.X - Spawn.X, Size.Y - Spawn.Y);
		// Which way it faces is set by the rules when the battle starts.

		// What it carries: the setup's choice, or the computer's own. The
		// computer shares its side's points out, a quarter each, the rest to
		// its first units; the picks are the rules' own (SuggestLoadout), so
		// both machines of a match would pick the same.
		const int32 Team = Unit.Team;
		if (PicksOwnItems(Team) && Setup.ItemBudget > 0)
		{
			const int32 Share = Setup.ItemBudget / 4 + (Index % 4 < Setup.ItemBudget % 4 ? 1 : 0);
			const std::vector<std::string> Picks = TMSim::SuggestLoadout(JobId, Share);
			for (int32 Slot = 0; Slot < 3; ++Slot)
			{
				Unit.Gear[Slot] = Slot < static_cast<int32>(Picks.size()) ? TMSim::FindItem(Picks[static_cast<size_t>(Slot)]) : nullptr;
			}
		}
		else
		{
			for (int32 Slot = 0; Slot < 3; ++Slot)
			{
				Unit.Gear[Slot] = TMSim::FindItem(Setup.Items[Team][Index % 4][Slot]);
			}
		}

		Battle.Units.push_back(Unit);
	}

	// The rule numbers chosen in Developer Tools, then the setup screen's own
	// three on top (dev_tools.gd:8: "saved and used by the next battle").
	Battle.Tuning = GameTuning();
	for (const TMSim::FTuningKey& Key : TMSim::TuningKeys())
	{
		// Online, the host's rule numbers, so both battles run on the same rules.
		const TMap<FString, double>& Rules = bOnline ? OnlineTuning : FTMSettings::Get().Tuning;
		if (const double* Saved = Rules.Find(UTF8_TO_TCHAR(Key.Key)))
		{
			Battle.Tuning.*Key.Member = FMath::Clamp(*Saved, Key.Low, Key.High);
		}
	}

	// How the battle can be won is a rule like any other, so it goes to the rules
	// with the battle rather than being watched for here.
	Battle.Tuning.CaptureSeconds = Setup.CaptureSeconds;
	Battle.Tuning.BattleSeconds = Setup.BattleSeconds;
	Battle.Tuning.PlanningSeconds = Setup.PlanningSeconds;
	// And how many watchtowers: the rules place them when the battle starts.
	Battle.Tuning.WatchtowerCount = Setup.Watchtowers;
	// And the item points, and whether what the units carry keeps to them. A
	// loadout the rules refuse goes into battle with nothing rather than as it is.
	Battle.Tuning.ItemBudget = Setup.ItemBudget;
	// And the neutral camps, with the map's own boss unless one is drawn at random.
	Battle.Tuning.CampLevel = Setup.CampLevel;
	Battle.Tuning.RandomBoss = Setup.bRandomBoss ? 1.0 : 0.0;
	Battle.Tuning.Elements = Setup.bElements ? 1.0 : 0.0;
	Battle.Tuning.FriendlyFire = Setup.bFriendlyFire ? 1.0 : 0.0;
	Battle.Tuning.CampRespawn = Setup.bCampRespawn ? 1.0 : 0.0;
	Battle.Tuning.BossHunt = Setup.bBossHunt ? 1.0 : 0.0;
	Battle.Tuning.BossClaim = Setup.bBossClaim ? 1.0 : 0.0;
	Battle.Tuning.TileMove = FMath::Clamp(Setup.TileMove, 0, 2);
	Battle.BossJob = MapDef.Boss;
	const std::string Loadout = Battle.LoadoutProblem();
	if (!Loadout.empty())
	{
		for (TMSim::FUnit& Each : Battle.Units)
		{
			Each.Gear[0] = Each.Gear[1] = Each.Gear[2] = nullptr;
		}
		Tell(FString::Printf(TEXT("Items left behind: %hs"), Loadout.c_str()));
	}
	PlaceId = -1;
	Battle.CaptureTicks[0] = 0;
	Battle.CaptureTicks[1] = 0;

	// A replay starts from what it recorded (TMBattleDirectorReplay.cpp); any
	// other battle is recorded from here.
	if (bReplaying)
	{
		ApplyReplayStart();
	}
	else
	{
		BeginRecording();
	}

	// The seed is part of the battle: the same seed and the same orders give the
	// same battle. Left alone it is the one the clock was checked against, so a
	// battle nobody set up is the one the Godot game plays.
	Battle.Start(BattleSeed);
	if (!bReplaying)
	{
		Recording.StartSum = Battle.Checksum();
	}
	// The battle report counts from here (TMBattleDirectorReport.cpp).
	ResetTallies();

	// Each computer gets its own generator, so a battle against it plays out the
	// same way twice -- its own and not the battle's, because a player thinking
	// harder must not change what the dice do. Godot leaves its computer's
	// generator unseeded (battle.gd:199), which only matters for easy and medium;
	// hard never draws from it.
	for (int32 Team = 0; Team < 2; ++Team)
	{
		Computers[Team].SetDifficulty(TCHAR_TO_UTF8(*Setup.Difficulty[Team]));
		Computers[Team].Rng.Seed(BattleSeed + Team);
	}
	ThinkingAbout = -1;
	ThinkRemainder = 0.0f;

	USkeletalMesh* Mesh = UnitMesh.LoadSynchronous();
	// Which body each class wears is data (TMBattleDirectorMotion.cpp); the
	// level's mesh is what a unit wears when the map has nothing for it.
	LoadCharacterMap();
	// A name no living component has. A rebuilt battle's parts must not take
	// the names of the old ones still being torn down: Unreal would build the
	// new one in the old one's memory while the old one's work is still in
	// flight -- a hero's hair and cloth being animated in the background, say --
	// and that crashed the game whenever a battle was started from the menu.
	auto Fresh = [this](UClass* Class, const TCHAR* Kind, int32 Id)
	{
		return MakeUniqueObjectName(this, Class, *FString::Printf(TEXT("%s_%d"), Kind, Id));
	};
	const bool bReadLater = GetWorld() && GetWorld()->IsGameWorld() && !FApp::IsUnattended();
	for (size_t i = 0; i < Battle.Units.size(); ++i)
	{
		USkeletalMeshComponent* Visual = NewObject<USkeletalMeshComponent>(
			this, Fresh(USkeletalMeshComponent::StaticClass(), TEXT("Unit"), Battle.Units[i].Id), RF_Transient);
		Visual->SetupAttachment(RootComponent);
		// No cloth, morph targets or material curves on any unit body (see WearBody).
		Visual->bDisableClothSimulation = true;
		Visual->bDisableMorphTarget = true;
		Visual->SetAllowAnimCurveEvaluation(false);
		// Its pose kept up to date even while the fog hides it, and no blur per
		// bone: a body shown again with no pose from the frame before tripped the
		// engine's skinning check (crash 2026-09-30, GPUSkinVertexFactory bPrevious).
		Visual->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		Visual->bPerBoneMotionBlur = false;
		Visual->RegisterComponent();
		// A hero not read yet is read in the background (LoadBodiesInBackground,
		// below) and the unit wears the level's mesh until it is in.
		const FTMBody* Body = BodyFor(Battle.Units[i]);
		if (Body && (IsBodyLoaded(*Body) || !bReadLater) && MeshOf(*Body))
		{
			WearBody(Visual, *Body);
		}
		else if (Mesh)
		{
			Visual->SetSkeletalMeshAsset(Mesh);
		}
		// The two sides tinted apart, until classes bring their own materials.
		Visual->SetCustomPrimitiveDataFloat(0, Battle.Units[i].Team == 0 ? 0.0f : 1.0f);
		// Clicks pass through bodies to the board. Units are picked by where they
		// appear on screen instead (PickUnderCursor), which does not depend on the
		// mesh having a physics asset.
		Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		UnitVisuals.Add(Visual);

		// Whose turn it is is said with light at their feet rather than by
		// lifting them off the ground, which only ever looked like a bug.
		UTextRenderComponent* Plate = NewObject<UTextRenderComponent>(
			this, Fresh(UTextRenderComponent::StaticClass(), TEXT("Plate"), Battle.Units[i].Id), RF_Transient);
		Plate->SetMobility(EComponentMobility::Movable);
		Plate->SetupAttachment(RootComponent);
		Plate->RegisterComponent();
		Plate->SetWorldSize(22.0f);
		Plate->SetHorizontalAlignment(EHTA_Center);
		Plates.Add(Plate);

		UPointLightComponent* Light = NewObject<UPointLightComponent>(
			this, Fresh(UPointLightComponent::StaticClass(), TEXT("Ready"), Battle.Units[i].Id), RF_Transient);
		Light->SetupAttachment(RootComponent);
		Light->RegisterComponent();
		Light->SetLightColor(ReadyColour);
		Light->SetIntensity(ReadyLightBrightness);
		Light->SetAttenuationRadius(220.0f);
		Light->SetCastShadows(false);
		Light->SetVisibility(false);
		ReadyLights.Add(Light);

		// A second light for what it carries: the glow of a burn, a shield, a freeze.
		UPointLightComponent* Glow = NewObject<UPointLightComponent>(
			this, Fresh(UPointLightComponent::StaticClass(), TEXT("Status"), Battle.Units[i].Id), RF_Transient);
		Glow->SetupAttachment(RootComponent);
		Glow->RegisterComponent();
		Glow->SetAttenuationRadius(180.0f);
		Glow->SetCastShadows(false);
		Glow->SetVisibility(false);
		StatusLights.Add(Glow);
	}

	BuildBoard();
	BuildCamps();
	BuildTurnRings();
	BuildIndicators();
	BuildFog();

	bBuilt = true;
	FitPets();
	ResetMotion();
	RefreshVisuals();
	LoadBodiesInBackground();

	UE_LOG(LogTemp, Log, TEXT("Battle built with %d units"), static_cast<int32>(Battle.Units.size()));
}

FVector ATMBattleDirector::WorldFromMetres(const TMSim::FVec2& Point, int Level) const
{
	// The rules think in metres and Unreal in centimetres, and a height level is
	// a fixed number of metres, so the board's shape comes out of the map rather
	// than being decided here.
	return FVector(Point.X * TileSize, Point.Y * TileSize, Level * LevelCm());
}

FVector ATMBattleDirector::WorldFor(const TMSim::FUnit& Unit) const
{
	const int Level = Battle.Map.NodeLevel(TMSim::FMap::NodeOf(Unit.Pos));
	return WorldFromMetres(Unit.Pos, Level);
}

void ATMBattleDirector::RefreshPlates()
{
	// Who each one is and how it is doing, over its head. Every unit wears the
	// same borrowed mesh, so without this the two sides are indistinguishable and
	// a match is unreadable however good the lighting gets.
	// While playing, the HUD draws each unit's name, health and statuses over
	// its head (ATMBattleHud::DrawOverheads); these plates are for the editor,
	// where there is no HUD.
	const bool bHudShows = GetWorld() && GetWorld()->IsGameWorld();
	for (int32 i = 0; i < Plates.Num() && i < static_cast<int32>(Battle.Units.size()); ++i)
	{
		if (!Plates[i])
		{
			continue;
		}
		if (bHudShows)
		{
			Plates[i]->SetVisibility(false);
			continue;
		}
		const TMSim::FUnit& Unit = Battle.Units[i];
		UTextRenderComponent* Plate = Plates[i];
		if (!Unit.IsAlive())
		{
			// A fallen unit still says so while it can be raised, then goes quiet.
			Plate->SetVisibility(Unit.IsKo() && IsSeen(Unit));
			if (Unit.IsKo())
			{
				Plate->SetText(FText::FromString(FString::Printf(TEXT("%hs  down %.0fs"),
					Unit.Job.c_str(), Unit.KoTicks / float(TMSim::Pace::TicksPerSecond))));
				Plate->SetTextRenderColor(FColor(140, 140, 150));
				Plate->SetRelativeLocation(WorldFor(Unit) + FVector(0.0f, 0.0f, 150.0f * UnitSize));
			}
			continue;
		}

		FString Line = FString::Printf(TEXT("%hs  %d"), Unit.Job.c_str(), Unit.Hp);
		if (Unit.IsCasting())
		{
			// What it is in the middle of, which is the thing worth reading.
			const TMSim::FAbility* Ability = Unit.Ability(Unit.Casting.Slot);
			Line += FString::Printf(TEXT("  casting %hs"),
				Ability ? Ability->Name.c_str() : "something");
		}
		else if (Unit.bReady)
		{
			Line += TEXT("  *");
		}
		for (const TMSim::FStatus& Status : Unit.Statuses)
		{
			Line += FString::Printf(TEXT(" [%hs]"), Status.Id.c_str());
		}
		Plate->SetText(FText::FromString(Line));
		// Blue and red, which is the only thing telling the sides apart until the
		// classes have their own materials.
		Plate->SetTextRenderColor(Unit.Team == 0 ? FColor(120, 180, 255)
			: (FTMSettings::Get().bColorblind ? FColor(255, 185, 80) : FColor(255, 130, 120)));
		// Fog of war: what this side cannot see, this screen does not show.
		Plate->SetVisibility(IsSeen(Unit));
		Plate->SetRelativeLocation(WorldFor(Unit) + FVector(0.0f, 0.0f, 150.0f * UnitSize));
	}
}

void ATMBattleDirector::RefreshVisuals()
{
	RefreshPlates();
	ApplyOutlines();
	RefreshTowers();
	for (int32 i = 0; i < UnitVisuals.Num() && i < static_cast<int32>(Battle.Units.size()); ++i)
	{
		if (!UnitVisuals[i])
		{
			continue;
		}
		const TMSim::FUnit& Unit = Battle.Units[i];
		const FVector Where = WorldFor(Unit);
		// In a game world the bodies walk, and AdvanceMotion puts them where they
		// are part way along. In the editor nothing advances them, so they stand
		// where the rules have them.
		const bool bMoving = GetWorld() && GetWorld()->IsGameWorld() && Motions.IsValidIndex(i);
		if (!bMoving)
		{
			UnitVisuals[i]->SetRelativeLocation(Where);
			// A Paragon mesh does not face along its actor's +X, hence the offset.
			const FTMBody* Body = Motions.IsValidIndex(i) ? Motions[i].Body : nullptr;
			const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Unit.Facing.Y, Unit.Facing.X)) + (Body ? Body->Yaw : 180.0f);
			UnitVisuals[i]->SetRelativeRotation(FRotator(0.0f, Yaw, 0.0f));
		}
		// A body that can fall lies where it fell while it can still be raised.
		const bool bCanFall = Motions.IsValidIndex(i) && Motions[i].Body && Motions[i].Body->Animations;
		const bool bStillFalling = Motions.IsValidIndex(i) && Motions[i].HeldBlows > 0;
		UnitVisuals[i]->SetVisibility((Unit.IsAlive() || bStillFalling || (bCanFall && Unit.IsKo())) && IsSeen(Unit));
		// Vanished (Smoke Bomb, Shadow Step): to the side that still sees it, only
		// its outline is drawn, a ghost of itself (v19 play test). The outline is
		// the custom-depth pass (ApplyOutlines), which needs no main pass.
		const bool bGhost = Unit.IsAlive() && Unit.HasStatus("veil");
		if (UnitVisuals[i]->bRenderInMainPass == bGhost)
		{
			UnitVisuals[i]->SetRenderInMainPass(!bGhost);
		}

		if (ReadyLights.IsValidIndex(i) && ReadyLights[i])
		{
			// Lit only while it flares from a blow (StepTicks): a light at the feet
			// whenever a turn was up washed out the very ground the unit would cross
			// (v19 play test), so readiness is shown without one (Indicators).
			ReadyLights[i]->SetRelativeLocation((bMoving ? Motions[i].Shown : Where) + FVector(0.0f, 0.0f, 55.0f));
			const int32 Index = i;
			ReadyLights[i]->SetVisibility(Unit.IsAlive() && IsSeen(Unit)
				&& Flashes.ContainsByPredicate([Index](const FFlash& Flash) { return Flash.UnitId == Index; }));
		}
	}
}

void ATMBattleDirector::StepTicks(int32 Ticks)
{
	if (!bBuilt)
	{
		BuildBattle();
	}

	// Time comes through the same door as everything else. It is the one order
	// nobody issues, but a battle is exactly the list of orders applied to it, so
	// if the clock went round some other way a recording of the orders would not
	// be enough to play the battle back.
	const int32 Most = TMSim::Pace::MaxAdvance;
	int32 Left = FMath::Max(0, Ticks);
	while (Left > 0)
	{
		const int32 Now = FMath::Min(Left, Most);
		Submit(TMSim::FOrder::MakeAdvance(Now));
		Left -= Now;
	}
}

FString ATMBattleDirector::DescribeBattle() const
{
	FString Text = FString::Printf(TEXT("tick %d, winner %d\n"), Battle.TickCount, Battle.Winner);
	for (const TMSim::FUnit& Unit : Battle.Units)
	{
		Text += FString::Printf(TEXT("  %d %-11hs team %d  at %6.2f,%6.2f  hp %3d  tg %4d/%d  ult %3d%s%s"),
			Unit.Id, Unit.Job.c_str(), Unit.Team, Unit.Pos.X, Unit.Pos.Y, Unit.Hp,
			Unit.Tg, TMSim::Pace::TgMax, Unit.Ult,
			Unit.bReady ? TEXT("  READY") : TEXT(""),
			Unit.bReady ? *FString::Printf(TEXT(" clock %d"), Unit.Clock) : TEXT(""));

		// A spell part-way out, drawn as it fills, because how long is left is the
		// only thing anyone watching a cast wants to know.
		if (Unit.IsCasting())
		{
			const int32 Total = FMath::Max(1, Unit.Casting.Total);
			const int32 Done = FMath::Clamp(((Total - Unit.Casting.Ticks) * 6) / Total, 0, 6);
			FString Bar;
			for (int32 Step = 0; Step < 6; ++Step)
			{
				Bar += Step < Done ? TEXT("#") : TEXT("-");
			}
			const TMSim::FAbility* Ability = Unit.Ability(Unit.Casting.Slot);
			Text += FString::Printf(TEXT("  casting %hs [%s] %.1fs"),
				Ability ? Ability->Name.c_str() : "something", *Bar,
				Unit.Casting.Ticks / float(TMSim::Pace::TicksPerSecond));
		}
		if (Unit.IsChanneling())
		{
			Text += FString::Printf(TEXT("  channelling (%d more)"), Unit.Channeling.Turns);
		}
		if (!Unit.IsAlive())
		{
			Text += Unit.IsKo()
				? FString::Printf(TEXT("  DOWN (%.1fs to raise)"),
					Unit.KoTicks / float(TMSim::Pace::TicksPerSecond))
				: FString(TEXT("  GONE"));
		}
		for (const TMSim::FStatus& Status : Unit.Statuses)
		{
			Text += FString::Printf(TEXT("  [%hs %d]"), Status.Id.c_str(), Status.Turns);
		}
		Text += TEXT("\n");
	}
	return Text;
}

FString ATMBattleDirector::MoveUnitTo(int32 UnitId, float MetresX, float MetresY, bool bSprint)
{
	if (!bBuilt)
	{
		BuildBattle();
	}
	// Snapped first: a unit stands on a navigation node, not wherever a click
	// happened to land.
	const TMSim::FVec2 To = Battle.TileSpot(TMSim::FVec2(MetresX, MetresY));

	const TMSim::FUnit* Unit = Battle.FindUnit(UnitId);
	if (!Unit)
	{
		return TEXT("No such unit.");
	}
	const FString Refused = Submit(TMSim::FOrder::MakeMove(UnitId, Unit->Serial, To, bSprint));
	if (Refused.IsEmpty())
	{
		UE_LOG(LogTemp, Log, TEXT("unit %d walked to %.2f, %.2f"), UnitId, To.X, To.Y);
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("unit %d cannot walk there: %s"), UnitId, *Refused);
	}
	return Refused;
}

FString ATMBattleDirector::Submit(const TMSim::FOrder& Order)
{
	// Checked, then applied, and never the other way round. A person's order
	// arrives from a click, the computer's out of TMSim, and one day a third
	// will arrive from the network; all three are strangers here and all three
	// are checked. It is also what makes a battle replayable: it is exactly the
	// list of orders that got through this function.
	//
	// Online (battle.gd:398-412), the joiner doesn't apply its own orders: it
	// asks the host, and applies what the host sends back. The host sends each
	// order it applies to the joiner first, so anything it sets off follows it.
	if (bOnline && !bOnlineHost && !bApplyingFromHost)
	{
		if (Order.Type == TMSim::EOrderType::Advance || Order.Type == TMSim::EOrderType::Tune)
		{
			return TEXT("Only the host moves time forward or changes the rules.");
		}
		if (bWaitingForHost)
		{
			return TEXT("Waiting for the host to answer the last order.");
		}
		const std::string Early = Battle.Validate(Order);
		if (!Early.empty())
		{
			return UTF8_TO_TCHAR(Early.c_str());
		}
		const std::string Line = TMSim::OrderToText(Order);
		bWaitingForHost = true;
		SendOnline(TEXT("req"), [&Line](FJsonObject& Message) { Message.SetStringField(TEXT("o"), UTF8_TO_TCHAR(Line.c_str())); });
		// A walk sets off now, on screen only, while the host's answer is on its way (2026-10-03).
		if (Order.Type == TMSim::EOrderType::Move)
		{
			PredictWalk(Order);
		}
		return FString();
	}
	// Watching a replay: only the replay's own orders go in.
	if (bReplaying && !bApplyingReplay)
	{
		return TEXT("This is a replay.");
	}
	const std::string Refused = Battle.Validate(Order);
	if (!Refused.empty())
	{
		return UTF8_TO_TCHAR(Refused.c_str());
	}
	if (bOnline && bOnlineHost)
	{
		const std::string Line = TMSim::OrderToText(Order);
		SendOnline(TEXT("cmd"), [&Line](FJsonObject& Message) { Message.SetStringField(TEXT("o"), UTF8_TO_TCHAR(Line.c_str())); });
	}

	TMSim::FTickReport Report;
	SnapshotForTally();
	if (!Battle.Apply(Order, Report))
	{
		// Accepted and then not applied would mean the two halves disagree,
		// which is the one thing that must never pass quietly.
		UE_LOG(LogTemp, Error,
			TEXT("an order for unit %d passed the rules and then did nothing"), Order.UnitId);
		return TEXT("The order could not be carried out.");
	}
	if (Order.Type != TMSim::EOrderType::Advance)
	{
		++OrdersApplied;
	}
	// Every applied order is the battle's replay (TMBattleDirectorReplay.cpp),
	// and what it did goes into the battle report (TMBattleDirectorReport.cpp).
	if (!bReplaying)
	{
		RecordApplied(Order, Report);
	}
	TallyEvents(Report);
	Narrate(Report);
	// Jumping along a replay: the rules only, nothing shown on the way.
	if (bReplayQuiet)
	{
		return FString();
	}
	ShowEvents(Report);
	RefreshVisuals();
	if (bOnline)
	{
		AfterOnlineApply(Order);
	}
	return FString();
}

FString ATMBattleDirector::NameOf(int32 UnitId) const
{
	if (const TMSim::FUnit* Unit = const_cast<TMSim::FBattle&>(Battle).FindUnit(UnitId))
	{
		// A monster or a pet by its name: there is only ever a handful of each.
		if (Unit->bMonster || Unit->PetOf >= 0)
		{
			const TMSim::FJobDef* Job = TMSim::FindJob(Unit->Job);
			return FString::Printf(TEXT("%hs"), Job ? Job->Name.c_str() : Unit->Job.c_str());
		}
		return FString::Printf(TEXT("%hs %d"), Unit->Job.c_str(), Unit->Id);
	}
	return FString::Printf(TEXT("unit %d"), UnitId);
}

void ATMBattleDirector::ShowEvents(const TMSim::FTickReport& Report)
{
	// Only while playing. In the editor the clock is stepped by hand and nothing
	// advances these, so they would pile up in the level and never fade; the log
	// is the readable layer there.
	if (!GetWorld() || !GetWorld()->IsGameWorld())
	{
		return;
	}
	// Statuses that have just ended play their expire first, before the hits
	// this tick brings (Cast Studio, TMBattleDirectorCast.cpp).
	CastNoticeEnds();
	AnimateEvents(Report);
	// What an ability did waits until it lands; the rest shows now
	// (TMBattleDirectorBlows.cpp).
	GatherBlows(Report);
}

void ATMBattleDirector::PlayVfx(const TMSim::FAbility& Ability, const FVector& Where)
{
	// Loaded the first time it is wanted and kept, so a battle loads each effect
	// once. A path that no longer names an effect -- a Fab pack removed since the
	// class was made -- is said once and then left alone: the ability still goes
	// off, it just shows nothing.
	const FString Path = UTF8_TO_TCHAR(Ability.VfxSystem.c_str());
	TObjectPtr<UObject>* Known = LoadedVfx.Find(Path);
	if (!Known)
	{
		UObject* System = FSoftObjectPath(Path).TryLoad();
		if (!System || !(System->IsA<UNiagaraSystem>() || System->IsA<UParticleSystem>()))
		{
			UE_LOG(LogTemp, Warning, TEXT("%hs names a particle effect that is not in the project: %s"),
				Ability.Id.c_str(), *Path);
			System = nullptr;
		}
		// One holding a material cooked without shaders is not played (TMDrawable.h).
		if (System && !TMDrawable::EffectUsable(Cast<UParticleSystem>(System), GetWorld()))
		{
			System = nullptr;
		}
		Known = &LoadedVfx.Add(Path, System);
	}
	if (!*Known)
	{
		return;
	}
	const FVector Size(Ability.VfxScale);
	UFXSystemComponent* Playing = nullptr;
	if (UNiagaraSystem* Niagara = Cast<UNiagaraSystem>(*Known))
	{
		Playing = UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, Niagara, Where, FRotator::ZeroRotator, Size);
	}
	else if (UParticleSystem* Cascade = Cast<UParticleSystem>(*Known))
	{
		Playing = UGameplayStatics::SpawnEmitterAtLocation(this, Cascade, Where, FRotator::ZeroRotator, Size);
	}
	if (Playing)
	{
		// Much of what Fab sells loops, made to sit on something for as long as
		// it is wanted, and would go on for ever. So every effect is switched
		// off after a while (AdvanceVfx); what it has already thrown out fades
		// as it would, and the component destroys itself once it is quiet.
		PlayingVfx.Add({ Playing, 0.0f });
		++EffectsPlayed;
	}
}

void ATMBattleDirector::AdvanceVfx(float DeltaSeconds)
{
	for (int32 i = PlayingVfx.Num() - 1; i >= 0; --i)
	{
		FPlayingVfx& Each = PlayingVfx[i];
		Each.Age += DeltaSeconds;
		if (!Each.Component.IsValid())
		{
			PlayingVfx.RemoveAtSwap(i);
		}
		else if (Each.Age >= VfxSeconds)
		{
			Each.Component->Deactivate();
			PlayingVfx.RemoveAtSwap(i);
		}
	}
}

void ATMBattleDirector::FrameTheBoard()
{
	// A camera on the board the director actually built, rather than wherever the
	// level happened to leave the view. High enough to read the whole board, low
	// enough that a unit is a character rather than a token.
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld() || Watcher)
	{
		return;
	}

	const TMSim::FVec2 Size = Battle.Map.SizeMeters();
	const float Span = FMath::Max(Size.X, Size.Y) * TileSize;
	const FVector Middle(Size.X * 0.5f * TileSize, Size.Y * 0.5f * TileSize, 0.0f);
	// Back along the diagonal and up, which is the angle the game is played from.
	const FVector Where = Middle
		+ FVector(-Span * CameraBack, -Span * CameraBack, Span * CameraHeight);

	FActorSpawnParameters How;
	How.ObjectFlags |= RF_Transient;  // a view, not part of the level
	Watcher = World->SpawnActor<ACameraActor>(Where, FRotator(CameraPitch, 45.0f, 0.0f), How);
	// Where the free camera starts: looking at the middle from where this puts it.
	CamTarget = CamWantTarget = Middle;
	CamYaw = 45.0f;
	CamPitch = CameraPitch;
	// A big map starts no further out than the wheel can reach.
	CamDistance = CamWantDistance = FMath::Min(16000.0f, static_cast<float>((Where - Middle).Size()));
	if (!Watcher)
	{
		return;
	}
	if (UCameraComponent* Lens = Watcher->GetCameraComponent())
	{
		Lens->SetFieldOfView(CameraFov);
		// A camera actor holds 16:9 by default, putting black bars round any
		// window of another shape. The HUD is drawn inside the bars but the
		// pointer is measured from the window's corner, so every button would
		// answer somewhere off to one side of where it is drawn. Fill the window.
		Lens->SetConstraintAspectRatio(false);
	}
	if (APlayerController* Player = World->GetFirstPlayerController())
	{
		// Cut rather than glide: there is nothing to glide from on the first frame.
		Player->SetViewTarget(Watcher);
	}
	AddOutlineToCamera();
	UE_LOG(LogTemp, Log, TEXT("camera framing a %.0f by %.0f m board"), Size.X, Size.Y);
}

void ATMBattleDirector::MaybeCapture()
{
	// Pictures of a battle nobody is sitting in front of. Off unless asked for,
	// because it writes a file every few seconds.
	if (CaptureEverySeconds <= 0.0f || !GetWorld() || !GetWorld()->IsGameWorld())
	{
		return;
	}
	const float Now = Battle.TickCount / static_cast<float>(TMSim::Pace::TicksPerSecond);
	// Either something just happened, or enough time has passed that a picture of
	// the board is worth having anyway.
	if (!bWorthSeeing && Now < NextCaptureAt)
	{
		return;
	}
	bWorthSeeing = false;
	NextCaptureAt = Now + CaptureEverySeconds;
	++Captured;
	// Asked for by name rather than through the HighResShot console command: that
	// command writes nothing in an unattended off-screen run, and says so nowhere.
	const FString Where = FPaths::ProjectSavedDir()
		/ TEXT("Match") / FString::Printf(TEXT("frame_%03d_%.0fs.png"), Captured, Now);
	// With the HUD in the picture: the turn order and the log are half of what a
	// picture of a battle is for.
	FScreenshotRequest::RequestScreenshot(Where, true, false);
	UE_LOG(LogTemp, Log, TEXT("CAPTURE %d at %.1fs -> %s"), Captured, Now, *Where);
}

void ATMBattleDirector::AdvanceFloaters(float DeltaSeconds)
{
	// The numbers rise, lean towards the camera so they can be read, and fade.
	// None of this is state: the rules neither know nor care that it happens.
	FRotator Towards = FRotator::ZeroRotator;
	if (const APlayerController* Viewer = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		FVector Eye;
		FRotator Look;
		Viewer->GetPlayerViewPoint(Eye, Look);
		Towards = FRotator(0.0f, Look.Yaw + 180.0f, 0.0f);
	}

	for (TObjectPtr<UTextRenderComponent>& Plate : Plates)
	{
		if (Plate)
		{
			Plate->SetWorldRotation(Towards);
		}
	}

	for (int32 i = Floaters.Num() - 1; i >= 0; --i)
	{
		FTMFloater& Floater = Floaters[i];
		Floater.Age += DeltaSeconds;
		if (!Floater.Text || Floater.Age >= FloaterSeconds)
		{
			if (Floater.Text)
			{
				Floater.Text->DestroyComponent();
			}
			Floaters.RemoveAt(i);
			continue;
		}
		const float Part = Floater.Age / FloaterSeconds;
		Floater.Text->AddLocalOffset(FVector(0.0f, 0.0f, 42.0f * DeltaSeconds));
		Floater.Text->SetWorldRotation(Towards);
		// Held at full strength for the first half, then gone by the end.
		const float Alpha = Part < 0.5f ? 1.0f : 1.0f - (Part - 0.5f) * 2.0f;
		Floater.Text->SetTextRenderColor(FColor(
			Floater.Text->TextRenderColor.R, Floater.Text->TextRenderColor.G,
			Floater.Text->TextRenderColor.B, static_cast<uint8>(Alpha * 255.0f)));
	}

	// Health bars that have had their seconds.
	const double PopNow = PopClock();
	for (auto It = HpPops.CreateIterator(); It; ++It)
	{
		if (PopNow - It.Value().ShownAt >= HpPopSeconds)
		{
			It.RemoveCurrent();
		}
	}

	// A struck unit's own light flares and settles, which is cheaper to read than
	// a number and says where to look.
	for (int32 i = Flashes.Num() - 1; i >= 0; --i)
	{
		FFlash& Flash = Flashes[i];
		Flash.Age += DeltaSeconds;
		const int32 Index = Flash.UnitId;
		if (!ReadyLights.IsValidIndex(Index) || !ReadyLights[Index])
		{
			Flashes.RemoveAt(i);
			continue;
		}
		UPointLightComponent* Light = ReadyLights[Index];
		if (Flash.Age >= FlashSeconds)
		{
			// Out again: the light is only for the flare.
			Light->SetLightColor(ReadyColour);
			Light->SetIntensity(ReadyLightBrightness);
			Light->SetVisibility(false);
			Flashes.RemoveAt(i);
			continue;
		}
		const float Left = 1.0f - Flash.Age / FlashSeconds;
		Light->SetVisibility(true);
		Light->SetLightColor(Flash.Colour);
		Light->SetIntensity(ReadyLightBrightness * (1.0f + 2.5f * Flash.Strength * Left * Left));
	}
}

void ATMBattleDirector::Narrate(const TMSim::FTickReport& Report)
{
	for (const TMSim::FEvent& Event : Report.Events)
	{
		FString Line;
		switch (Event.Kind)
		{
		case TMSim::EEventKind::BecameReady:
			Line = FString::Printf(TEXT("%s is ready"), *NameOf(Event.Unit));
			break;
		case TMSim::EEventKind::TimedOut:
			Line = FString::Printf(TEXT("%s ran out of time"), *NameOf(Event.Unit));
			// A buzz when one of this side's turns is lost (battle.gd:1060-1063).
			if (const TMSim::FUnit* Lost = Battle.FindUnit(Event.Unit))
			{
				if (bPlayerInput && !ComputerPlaysUnit(*Lost) && (!bOnline || UnitOwner(*Lost) == LocalPlayer))
				{
					PlayEventSound(TEXT("turnLost"), nullptr, 0.6f);
				}
			}
			break;
		case TMSim::EEventKind::CastStarted:
			Line = FString::Printf(TEXT("%s begins casting %hs (%.1fs)"), *NameOf(Event.Unit),
				Event.Id.c_str(), Event.Amount / float(TMSim::Pace::TicksPerSecond));
			break;
		case TMSim::EEventKind::CastFizzled:
			Line = FString::Printf(TEXT("%s's spell fizzles"), *NameOf(Event.Unit));
			break;
		case TMSim::EEventKind::Resolved:
			// A warned blow (2026-10-06): drawn now (2), landing now (1).
			Line = Event.Amount == 2 ? FString::Printf(TEXT("%s draws %hs: it lands as its next turn begins"), *NameOf(Event.Unit), Event.Id.c_str())
				: Event.Amount == 1 ? FString::Printf(TEXT("%s's %hs lands"), *NameOf(Event.Unit), Event.Id.c_str())
				: FString::Printf(TEXT("%s uses %hs"), *NameOf(Event.Unit), Event.Id.c_str());
			break;
		case TMSim::EEventKind::Hit:
			// A minus sign for damage and a plus for healing, which is the whole of
			// what a person needs to read a fight going past.
			Line = FString::Printf(TEXT("    %s %s%d"), *NameOf(Event.Unit),
				Harms(Event) ? TEXT("-") : TEXT("+"), Event.Amount);
			break;
		case TMSim::EEventKind::Evaded:
			Line = FString::Printf(TEXT("    %s evades"), *NameOf(Event.Unit));
			break;
		case TMSim::EEventKind::Grazed:
			Line = FString::Printf(TEXT("    graze on %s"), *NameOf(Event.Unit));
			break;
		case TMSim::EEventKind::Critical:
			Line = FString::Printf(TEXT("    critical on %s"), *NameOf(Event.Unit));
			break;
		case TMSim::EEventKind::Absorbed:
			Line = FString::Printf(TEXT("    %s's %hs soaks %d"), *NameOf(Event.Unit),
				Event.Id.c_str(), Event.Amount);
			break;
		case TMSim::EEventKind::StatusApplied:
		{
			const TMSim::FStatusDef* Def = TMSim::FindStatus(Event.Id);
			Line = FString::Printf(TEXT("    %s takes %hs"), *NameOf(Event.Unit), Def ? Def->Name : Event.Id.c_str());
			break;
		}
		case TMSim::EEventKind::GaugeChanged:
			Line = FString::Printf(TEXT("    %s gauge %+d%%"), *NameOf(Event.Unit), Event.Amount);
			break;
		case TMSim::EEventKind::Knocked:
			Line = FString::Printf(TEXT("    %s is knocked out!"), *NameOf(Event.Unit));
			break;
		case TMSim::EEventKind::Revived:
			Line = Event.Id == "reraise" ? FString::Printf(TEXT("%s rises again (Reraise)"), *NameOf(Event.Unit))
				: FString::Printf(TEXT("    %s is back on its feet"), *NameOf(Event.Unit));
			break;
		case TMSim::EEventKind::Reaction:
		{
			// Two things meeting on a unit (Docs/design/feat-status-effects.md).
			const FString Who = NameOf(Event.Unit);
			Line = Event.Id == "shock" ? FString::Printf(TEXT("    %s is Wet: the lightning stuns it!"), *Who)
				: Event.Id == "freeze" ? FString::Printf(TEXT("    %s is Wet: the ice freezes it solid!"), *Who)
				: Event.Id == "ignite" ? FString::Printf(TEXT("    %s is Oiled: the fire ignites it!"), *Who)
				: Event.Id == "douse" ? FString::Printf(TEXT("    %s's burning is put out"), *Who)
				: Event.Id == "thaw" ? FString::Printf(TEXT("    %s thaws"), *Who)
				: Event.Id == "dry" ? FString::Printf(TEXT("    %s dries off"), *Who)
				: Event.Id == "spread" ? FString::Printf(TEXT("    %s catches the plague"), *Who)
				: Event.Id == "bomb" ? FString::Printf(TEXT("    The bomb on %s goes off!"), *Who)
				: Event.Id == "echo" ? FString::Printf(TEXT("    %s's ability echoes"), *Who)
				: Event.Id == "retribution" ? FString::Printf(TEXT("    Retribution stuns %s"), *Who)
				: Event.Id == "ricochet" ? FString::Printf(TEXT("    it bounces from %s to %s"), *NameOf(Event.By), *Who)
				: Event.Id == "execute" ? FString::Printf(TEXT("    %s's Verdict: half its gauge back"), *Who)
				: FString::Printf(TEXT("    %s: %hs"), *Who, Event.Id.c_str());
			break;
		}
		case TMSim::EEventKind::Redirected:
			Line = Event.Id == "guard" ? FString::Printf(TEXT("    %s steps in front of %s"), *NameOf(Event.Unit), *NameOf(Event.By))
				: FString::Printf(TEXT("    %s's Reflect turns the spell back on %s"), *NameOf(Event.By), *NameOf(Event.Unit));
			break;
		case TMSim::EEventKind::Fled:
			Line = FString::Printf(TEXT("%s is Terrified and runs from %s"), *NameOf(Event.Unit), *NameOf(Event.By));
			break;
		case TMSim::EEventKind::CharmEnded:
			Line = FString::Printf(TEXT("    %s comes to its senses"), *NameOf(Event.Unit));
			break;
		case TMSim::EEventKind::Capturing:
			Line = FString::Printf(TEXT("%s spends its turn taking a watchtower (%d/%d)"), *NameOf(Event.Unit),
				Event.Amount, Event.By);
			break;
		case TMSim::EEventKind::Captured:
			Line = FString::Printf(TEXT("%s takes the watchtower for %s: its side sees far from it"), *NameOf(Event.Unit),
				Event.By == 0 ? TEXT("blue") : TEXT("red"));
			break;
		case TMSim::EEventKind::CampWarning:
			Line = FString::Printf(TEXT("A camp stirs: it wakes in %d s"), Event.Amount);
			if (Event.Slot >= 0 && Event.Slot < static_cast<int32>(Battle.Camps.size()))
			{
				static const TCHAR* Tiers[] = { TEXT("easy"), TEXT("medium"), TEXT("hard"), TEXT("boss") };
				Line = FString::Printf(TEXT("A%s %s camp stirs: it wakes in %d s"), Battle.Camps[Event.Slot].Tier == 0 ? TEXT("n") : TEXT(""),
					Tiers[FMath::Clamp(Battle.Camps[Event.Slot].Tier, 0, 3)], Event.Amount);
			}
			break;
		case TMSim::EEventKind::CampAwake:
			Line = TEXT("A camp wakes");
			break;
		case TMSim::EEventKind::CampCleared:
			Line = Event.Amount >= 0 ? TEXT("A camp is cleared: its loot lies on the ground") : TEXT("A camp is cleared");
			break;
		case TMSim::EEventKind::ItemTaken:
		case TMSim::EEventKind::ItemDropped:
		{
			const TMSim::FItemDef* Item = TMSim::FindItem(Event.Id);
			// Slot -1: to or from the side's stash (equipped, taken off, or back from the fallen);
			// otherwise a cache, and a side's unit's pick-up goes to the stash too.
			const TMSim::FUnit* Who = Battle.FindUnit(Event.Unit);
			const bool bSide = Who && !Who->bMonster;
			const TCHAR* Verb = Event.Kind == TMSim::EEventKind::ItemTaken
				? (Event.Slot < 0 ? TEXT("equips") : bSide ? TEXT("picks up") : TEXT("takes"))
				: (Event.Slot < 0 ? (Who && Who->IsAlive() ? TEXT("takes off") : TEXT("leaves to the stash")) : TEXT("leaves"));
			Line = FString::Printf(TEXT("%s %s the %hs"), *NameOf(Event.Unit), Verb, Item ? Item->Name.c_str() : Event.Id.c_str());
			// Picked up (often just by walking onto it): its name rises off the unit in its tier's colour.
			const TMSim::FUnit* Taker = Battle.FindUnit(Event.Unit);
			if (Item && Taker && Event.Kind == TMSim::EEventKind::ItemTaken && IsSeen(*Taker))
			{
				static const FColor TierTint[] = { FColor(215, 218, 225), FColor(110, 220, 120), FColor(120, 170, 255), FColor(255, 190, 70) };
				AddFloater(Event.Unit, FString::Printf(TEXT("+ %hs"), Item->Name.c_str()), TierTint[FMath::Clamp(static_cast<int32>(Item->Tier), 0, 3)], false);
			}
			break;
		}
		case TMSim::EEventKind::MonsterAlert:
			Line = Event.By >= 0 ? FString::Printf(TEXT("%s is roused by %s!"), *NameOf(Event.Unit), *NameOf(Event.By))
				: FString::Printf(TEXT("%s is roused!"), *NameOf(Event.Unit));
			break;
		case TMSim::EEventKind::Escaped:
			Line = FString::Printf(TEXT("%s escapes with its treasure"), *NameOf(Event.Unit));
			break;
		case TMSim::EEventKind::Tamed:
			Line = Event.Amount > 0 ? FString::Printf(TEXT("%s is tamed by %s for %d turns"), *NameOf(Event.Unit), *NameOf(Event.By), Event.Amount)
				: FString::Printf(TEXT("%s goes wild again"), *NameOf(Event.Unit));
			break;
		case TMSim::EEventKind::PhaseChanged:
			Line = FString::Printf(TEXT("%s grows desperate: phase %d!"), *NameOf(Event.Unit), Event.Amount + 1);
			break;
		case TMSim::EEventKind::Staggered:
			Line = FString::Printf(TEXT("    %s is staggered!"), *NameOf(Event.Unit));
			break;
		// Camps and bosses (2026-10-02, "Camps and Bosses Mockups").
		case TMSim::EEventKind::CampNoise:
			Line = Event.Amount >= TMSim::Camp::NoiseFull
				? FString::Printf(TEXT("%s's fighting rouses a camp: it wakes soon, and comes for them"), *NameOf(Event.Unit))
				: FString::Printf(TEXT("A camp stirs at the noise (%d of %d)"), Event.Amount, TMSim::Camp::NoiseFull);
			break;
		case TMSim::EEventKind::CleanKill:
			Line = FString::Printf(TEXT("    %s: a clean kill, +%d%% gauge"), *NameOf(Event.Unit), Event.Amount);
			if (const TMSim::FUnit* Killer = Battle.FindUnit(Event.Unit))
			{
				if (IsSeen(*Killer))
				{
					AddFloater(Event.Unit, FString::Printf(TEXT("Clean kill  +%d%% gauge"), Event.Amount), FColor(240, 207, 114), false);
				}
			}
			break;
		// Ground zones (2026-10-04): told only where this screen may see them.
		case TMSim::EEventKind::ZoneLaid:
		case TMSim::EEventKind::ZoneEnded:
		case TMSim::EEventKind::ZoneIgnited:
		{
			const TMSim::FUnit* Layer = Battle.FindUnit(Event.Unit);
			if (!IsPointSeen(Event.Where) && !(Layer && ViewerTeam() >= 0 && Layer->Team == ViewerTeam()))
			{
				break;
			}
			const TMSim::FAbility* Laid = TMSim::FindAbility(Event.Id);
			const FString Called = Laid ? FString(UTF8_TO_TCHAR(Laid->Name.c_str())) : FString(UTF8_TO_TCHAR(Event.Id.c_str()));
			Line = Event.Kind == TMSim::EEventKind::ZoneLaid
				? FString::Printf(TEXT("%s lays %s (%d turns)"), *NameOf(Event.Unit), *Called, Event.Amount)
				: Event.Kind == TMSim::EEventKind::ZoneIgnited
				? FString::Printf(TEXT("%s's %s catches fire"), *NameOf(Event.Unit), *Called)
				: FString::Printf(TEXT("%s's %s fades"), *NameOf(Event.Unit), *Called);
			break;
		}
		case TMSim::EEventKind::Hunting:
			Line = FString::Printf(TEXT("%s hunts %s!"), *NameOf(Event.Unit), *NameOf(Event.By));
			BossNews = Line;
			BossNewsAt = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0f;
			break;
		case TMSim::EEventKind::BossClaimed:
			Line = FString::Printf(TEXT("%s claims %s: the Boss's Boon, +10%% damage for %d turns"),
				Event.Amount == 0 ? TEXT("Blue") : TEXT("Red"), *NameOf(Event.Unit), TMSim::Camp::BoonTurns);
			BossNews = Line;
			BossNewsAt = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0f;
			break;
		case TMSim::EEventKind::ClaimShare:
		{
			const TMSim::FItemDef* Prize = TMSim::FindItem(Event.Id);
			Line = FString::Printf(TEXT("%s's share of %s earns the %hs (in its stash)"),
				Event.Amount == 0 ? TEXT("Blue") : TEXT("Red"), *NameOf(Event.Unit), Prize ? Prize->Name.c_str() : Event.Id.c_str());
			break;
		}
		case TMSim::EEventKind::Teleported:
			// Shown at once where it went, not walked there (AdvanceMotion).
			SnapUnits.Add(Event.Unit);
			// The new spells' moves (2026-10-05) say how.
			Line = Event.Id == "pet" ? FString::Printf(TEXT("%s calls up the %s"), *NameOf(Event.By), *NameOf(Event.Unit))
				: Event.Id == "dash" ? FString::Printf(TEXT("%s dashes"), *NameOf(Event.Unit))
				: Event.Id == "pull" ? FString::Printf(TEXT("    %s is dragged in"), *NameOf(Event.Unit))
				: Event.Id == "hook" ? FString::Printf(TEXT("    %s is hooked in"), *NameOf(Event.Unit))
				: Event.Id == "shove" ? FString::Printf(TEXT("    %s is shoved back"), *NameOf(Event.Unit))
				: Event.Id == "rally" ? FString::Printf(TEXT("    %s answers the rally"), *NameOf(Event.Unit))
				: Event.Id == "gate" ? FString::Printf(TEXT("%s steps through the gate"), *NameOf(Event.Unit))
				: Event.Id == "recall" ? FString::Printf(TEXT("%s is recalled"), *NameOf(Event.Unit))
				: Event.Id == "riptide" ? FString::Printf(TEXT("    %s trades places"), *NameOf(Event.Unit))
				: Event.Id == "behind" ? FString::Printf(TEXT("%s steps behind its target"), *NameOf(Event.Unit))
				: Event.Id == "vault" ? FString::Printf(TEXT("%s vaults over"), *NameOf(Event.Unit))
				: Event.Id == "charge" ? FString::Printf(TEXT("%s charges"), *NameOf(Event.Unit))
				: Event.Id == "leap" ? FString::Printf(TEXT("%s leaps"), *NameOf(Event.Unit))
				: Event.Id == "shadowhop" ? FString::Printf(TEXT("%s slips into cover"), *NameOf(Event.Unit))
				: FString::Printf(TEXT("%s blinks away"), *NameOf(Event.Unit));
			break;
		case TMSim::EEventKind::ShrineUsed:
			Line = FString::Printf(TEXT("%s draws power from the shrine"), *NameOf(Event.Unit));
			break;
		case TMSim::EEventKind::Saved:
			Line = FString::Printf(TEXT("    %s is saved (+%d)"), *NameOf(Event.Unit), Event.Amount);
			break;
		case TMSim::EEventKind::Won:
			Line = HowWon().IsEmpty()
				? (Event.Unit == TMSim::FBattle::Draw
					? FString(TEXT("Nobody is left standing."))
					: FString::Printf(TEXT("Team %d wins."), Event.Unit))
				: HowWon() + TEXT(".");
			break;
		default:
			break;  // walking and turns ending are not worth a line of their own
		}
		if (Line.IsEmpty())
		{
			continue;
		}
		LogEvent(Event, Line);
		Log.Add(FString::Printf(TEXT("%6.1fs  %s"),
			Battle.TickCount / float(TMSim::Pace::TicksPerSecond), *Line));
		UE_LOG(LogTemp, Log, TEXT("%s"), *Line);
	}
	const int32 Spare = Log.Num() - FMath::Max(1, LogLines);
	if (Spare > 0)
	{
		Log.RemoveAt(0, Spare, EAllowShrinking::No);
	}
	const int32 SpareEntries = LogEntries.Num() - FMath::Max(1, LogLines);
	if (SpareEntries > 0)
	{
		LogEntries.RemoveAt(0, SpareEntries, EAllowShrinking::No);
	}
}

FString ATMBattleDirector::LogName(int32 UnitId) const
{
	const TMSim::FUnit* Unit = const_cast<TMSim::FBattle&>(Battle).FindUnit(UnitId);
	if (!Unit)
	{
		return FString::Printf(TEXT("unit %d"), UnitId);
	}
	const TMSim::FJobDef* Job = TMSim::FindJob(Unit->Job);
	FString Name = Job ? FString(UTF8_TO_TCHAR(Job->Name.c_str())) : FString(UTF8_TO_TCHAR(Unit->Job.c_str()));
	if (Unit->bMonster)
	{
		return Name;
	}
	// Two of a class on one side: number them by their place on it.
	int32 Same = 0;
	int32 Place = 0;
	for (const TMSim::FUnit& Other : Battle.Units)
	{
		if (!Other.bMonster && Other.HomeTeam() == Unit->HomeTeam() && Other.Job == Unit->Job)
		{
			++Same;
			Place = Other.Id == Unit->Id ? Same : Place;
		}
	}
	return Same > 1 ? FString::Printf(TEXT("%s %d"), *Name, Place) : Name;
}

void ATMBattleDirector::LogNote(const FString& Line)
{
	Log.Add(Line);
	FTMLogEntry Entry;
	Entry.Kind = FTMLogEntry::EKind::System;
	Entry.Seconds = Battle.TickCount / float(TMSim::Pace::TicksPerSecond);
	Entry.Text = Line;
	Entry.Group = ++LogGroup;
	LogEntries.Add(MoveTemp(Entry));
}

void ATMBattleDirector::LogEvent(const TMSim::FEvent& Event, const FString& Plain)
{
	using EKind = FTMLogEntry::EKind;
	FTMLogEntry Entry;
	Entry.Seconds = Battle.TickCount / float(TMSim::Pace::TicksPerSecond);
	Entry.Unit = Event.Unit;
	Entry.Group = LogGroup;
	const TMSim::FUnit* Unit = Battle.FindUnit(Event.Unit);
	auto AbilityName = [&]() -> FString
	{
		const TMSim::FAbility* Ability = Unit && Event.Slot >= 0 ? Unit->Ability(Event.Slot) : nullptr;
		return Ability ? FString(UTF8_TO_TCHAR(Ability->Name.c_str())) : FString(UTF8_TO_TCHAR(Event.Id.c_str()));
	};
	// The last result line for this unit in the action now being told, to add a tag to.
	auto LastResult = [this](int32 Who) -> FTMLogEntry*
	{
		for (int32 i = LogEntries.Num() - 1; i >= 0 && LogEntries[i].Group == LogGroup; --i)
		{
			if (LogEntries[i].Kind == EKind::Result && LogEntries[i].Unit == Who)
			{
				return &LogEntries[i];
			}
		}
		return nullptr;
	};
	switch (Event.Kind)
	{
	case TMSim::EEventKind::Resolved:
		Entry.Kind = EKind::Action;
		Entry.Group = ++LogGroup;
		// A warned blow (2026-10-06): drawn, then landing a turn later.
		Entry.Verb = Event.Amount == 2 ? TEXT("draws") : Event.Amount == 1 ? TEXT("lands") : TEXT("uses");
		Entry.What = AbilityName();
		break;
	case TMSim::EEventKind::CastStarted:
		Entry.Kind = EKind::Note;
		Entry.Group = ++LogGroup;
		Entry.Text = FString::Printf(TEXT("starts casting %s  (%.1f s)"), *AbilityName(), Event.Amount / float(TMSim::Pace::TicksPerSecond));
		break;
	case TMSim::EEventKind::Hit:
	{
		const bool bHarm = Harms(Event);
		Entry.Kind = EKind::Result;
		Entry.Amount = FString::Printf(TEXT("%s%d"), bHarm ? TEXT("\u2212") : TEXT("+"), Event.Amount);
		Entry.Tone = bHarm ? 1 : 2;
		// A crit or a graze was told just before: it marks this hit.
		if (FTMLogEntry* Crit = LogEntries.Num() > 0 && LogEntries.Last().Kind == EKind::Result && LogEntries.Last().Unit == Event.Unit
			&& LogEntries.Last().Amount.IsEmpty() && (LogEntries.Last().Tags.Contains(TEXT("CRIT")) || LogEntries.Last().Tags.Contains(TEXT("GRAZE")))
			? &LogEntries.Last() : nullptr)
		{
			Crit->Amount = Entry.Amount;
			Crit->Tone = !bHarm ? 2 : Crit->Tags.Contains(TEXT("CRIT")) ? 3 : 1;
			return;
		}
		const TMSim::FUnit* Hitter = Battle.FindUnit(Event.By);
		if (bHarm && Hitter && Unit && Hitter->Id != Unit->Id && !Hitter->bMonster && Hitter->Team == Unit->Team)
		{
			Entry.Tags.Add(TEXT("ALLY"));
		}
		break;
	}
	case TMSim::EEventKind::Critical:
		Entry.Kind = EKind::Result;
		Entry.Tags.Add(TEXT("CRIT"));
		Entry.bKey = true;
		break;
	case TMSim::EEventKind::Evaded:
		Entry.Kind = EKind::Result;
		Entry.Amount = TEXT("dodged");
		Entry.Tone = 4;
		break;
	case TMSim::EEventKind::Grazed:
		// Marks the hit that follows, as a crit does.
		Entry.Kind = EKind::Result;
		Entry.Tags.Add(TEXT("GRAZE"));
		break;
	case TMSim::EEventKind::Absorbed:
		Entry.Kind = EKind::Result;
		Entry.Amount = FString::Printf(TEXT("%d soaked"), Event.Amount);
		Entry.Tone = 4;
		break;
	case TMSim::EEventKind::StatusApplied:
	{
		const TMSim::FStatusDef* Def = TMSim::FindStatus(Event.Id);
		const FString Name = FString(Def ? UTF8_TO_TCHAR(Def->Name) : UTF8_TO_TCHAR(Event.Id.c_str())).ToUpper();
		if (FTMLogEntry* Last = LastResult(Event.Unit))
		{
			Last->Tags.Add(Name);
			return;
		}
		Entry.Kind = EKind::Result;
		Entry.Tags.Add(Name);
		break;
	}
	case TMSim::EEventKind::Knocked:
		Entry.Kind = EKind::Ko;
		Entry.bKey = true;
		break;
	case TMSim::EEventKind::BecameReady:
	case TMSim::EEventKind::GaugeChanged:
	case TMSim::EEventKind::TimedOut:
		Entry.Kind = EKind::Turn;
		Entry.Group = ++LogGroup;
		Entry.Text = Event.Kind == TMSim::EEventKind::BecameReady ? FString(TEXT("is ready"))
			: Event.Kind == TMSim::EEventKind::TimedOut ? FString(TEXT("ran out of time"))
			: FString::Printf(TEXT("gauge %+d%%"), Event.Amount);
		break;
	case TMSim::EEventKind::Won:
		Entry.Kind = EKind::System;
		Entry.Group = ++LogGroup;
		Entry.Unit = -1;
		Entry.Text = Plain;
		Entry.bKey = true;
		break;
	case TMSim::EEventKind::CampWarning:
	case TMSim::EEventKind::CampAwake:
	case TMSim::EEventKind::CampCleared:
		Entry.Kind = EKind::System;
		Entry.Group = ++LogGroup;
		Entry.Unit = -1;
		Entry.Text = Plain;
		Entry.bKey = Event.Kind == TMSim::EEventKind::CampCleared;
		break;
	default:
	{
		// Everything else: the unit's name, then the plain line without it.
		Entry.Kind = EKind::Note;
		FString Rest = Plain.TrimStart();
		const FString Name = NameOf(Event.Unit);
		if (Unit && Rest.StartsWith(Name))
		{
			Rest = Rest.Mid(Name.Len()).TrimStart();
			Rest.RemoveFromStart(TEXT("'s "));
		}
		else
		{
			Entry.Unit = -1;
		}
		Entry.Text = Rest;
		// A note inside an action (a reaction, a guard) stays with it; others start their own line.
		if (!Plain.StartsWith(TEXT("    ")))
		{
			Entry.Group = ++LogGroup;
		}
		Entry.bKey = Event.Kind == TMSim::EEventKind::Captured || Event.Kind == TMSim::EEventKind::ItemTaken
			|| Event.Kind == TMSim::EEventKind::Revived || Event.Kind == TMSim::EEventKind::PhaseChanged
			|| Event.Kind == TMSim::EEventKind::Escaped || Event.Kind == TMSim::EEventKind::Tamed
			|| Event.Kind == TMSim::EEventKind::Hunting || Event.Kind == TMSim::EEventKind::BossClaimed
			|| Event.Kind == TMSim::EEventKind::ClaimShare;
		break;
	}
	}
	LogEntries.Add(MoveTemp(Entry));
}

FString ATMBattleDirector::BattleLog() const
{
	return FString::Join(Log, TEXT("\n"));
}

FString ATMBattleDirector::OrderAbility(int32 UnitId, int32 Slot, float MetresX, float MetresY,
	int32 FollowId)
{
	if (!bBuilt)
	{
		BuildBattle();
	}
	const TMSim::FUnit* Unit = Battle.FindUnit(UnitId);
	if (!Unit)
	{
		return TEXT("No such unit.");
	}
	// Aimed at a unit, the spell follows it; aimed at the ground it stays put. The
	// caller says which by passing an id or -1, exactly as a click would.
	const TMSim::FVec2 At(MetresX, MetresY);
	const FString Refused = Submit(
		TMSim::FOrder::MakeUseAbility(UnitId, Unit->Serial, Slot, At, FollowId));
	if (!Refused.IsEmpty())
	{
		UE_LOG(LogTemp, Log, TEXT("unit %d cannot use that: %s"), UnitId, *Refused);
	}
	return Refused;
}

FString ATMBattleDirector::OrderAbilityAt(int32 UnitId, int32 Slot, int32 TargetUnitId)
{
	if (!bBuilt)
	{
		BuildBattle();
	}
	const TMSim::FUnit* Target = Battle.FindUnit(TargetUnitId);
	if (!Target)
	{
		return TEXT("No such target.");
	}
	return OrderAbility(UnitId, Slot, Target->Pos.X, Target->Pos.Y, TargetUnitId);
}

bool ATMBattleDirector::ComputerPlays(int32 Team) const
{
	// The neutral monsters: the host plays them and sends their orders on, as it
	// sends everything; offline, this machine does.
	if (Team == 2)
	{
		return !bOnline || bOnlineHost;
	}
	// Online, a person on each machine; the computer only in the online test (-tmnetbots).
	if (bOnline)
	{
		return bNetBots && Team == LocalTeam;
	}
	return Team == 0 ? bComputerPlaysTeam0 : bComputerPlaysTeam1;
}

const TMSim::FUnit* ATMBattleDirector::WaitingOn() const
{
	for (const TMSim::FUnit& Unit : Battle.Units)
	{
		if (Unit.IsAlive() && Unit.bReady)
		{
			return &Unit;
		}
	}
	return nullptr;
}

const TMSim::FUnit* ATMBattleDirector::WaitingOnComputer() const
{
	// The first unit waiting on a side the computer plays -- not simply the first
	// unit waiting. Several units are ready at once, and time runs for all of
	// them, so a computer side that stood aside until every other side had
	// finished would let its own turns run out. That is exactly what happened:
	// eight units became ready and all eight timed out without acting.
	for (const TMSim::FUnit& Unit : Battle.Units)
	{
		if (Unit.IsAlive() && Unit.bReady && ComputerPlaysUnit(Unit))
		{
			return &Unit;
		}
	}
	return nullptr;
}

FString ATMBattleDirector::TakeComputerTurn(int32 UnitId)
{
	if (!bBuilt)
	{
		BuildBattle();
	}
	const TMSim::FUnit* Unit = Battle.FindUnit(UnitId);
	if (!Unit || !Unit->IsAlive())
	{
		return TEXT("No such unit.");
	}
	if (!Unit->bReady)
	{
		return TEXT("It isn't its turn.");
	}

	const TMSim::FOrder Order = Unit->Team == 2 ? Monsters.NextCommand(Battle, *Unit) : Computers[Unit->Team].NextCommand(Battle, *Unit);
	const FString Refused = Submit(Order);
	if (!Refused.IsEmpty())
	{
		// The computer asking for something the rules refuse is a fault in the
		// computer, not in the player who happens to be watching.
		UE_LOG(LogTemp, Warning, TEXT("the computer asked for something it cannot do: %s"), *Refused);
	}
	return Refused;
}

int32 ATMBattleDirector::PlayComputerTurns(int32 MaxOrders)
{
	if (!bBuilt)
	{
		BuildBattle();
	}
	int32 Given = 0;
	while (Given < MaxOrders)
	{
		const TMSim::FUnit* Unit = WaitingOnComputer();
		if (!Unit)
		{
			break;
		}
		const int32 UnitId = Unit->Id;
		if (!TakeComputerTurn(UnitId).IsEmpty())
		{
			// It cannot have this turn, so give the turn up rather than ask
			// again and again for the same refusal.
			Submit(TMSim::FOrder::MakeEndTurn(UnitId, Battle.FindUnit(UnitId)->Serial));
		}
		++Given;
	}
	return Given;
}

void ATMBattleDirector::EndUnitTurn(int32 UnitId)
{
	if (!bBuilt)
	{
		BuildBattle();
	}
	const TMSim::FUnit* Unit = Battle.FindUnit(UnitId);
	if (!Unit)
	{
		return;
	}
	const FString Refused = Submit(TMSim::FOrder::MakeEndTurn(UnitId, Unit->Serial));
	if (Refused.IsEmpty())
	{
		UE_LOG(LogTemp, Log, TEXT("unit %d ended its turn"), UnitId);
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("unit %d cannot end its turn: %s"), UnitId, *Refused);
	}
}

int32 ATMBattleDirector::ReachableCount(int32 UnitId) const
{
	// Const in spirit: the pathfinder scribbles on its own scratch arrays.
	ATMBattleDirector* Self = const_cast<ATMBattleDirector*>(this);
	if (const TMSim::FUnit* Unit = Self->Battle.FindUnit(UnitId))
	{
		return static_cast<int32>(Self->Battle.ReachableNodes(*Unit).size());
	}
	return 0;
}

void ATMBattleDirector::RetireOldVisuals()
{
	constexpr double KeepSeconds = 3.0;
	const double Now = FPlatformTime::Seconds();
	for (int32 i = RetiringVisuals.Num() - 1; i >= 0; --i)
	{
		if (!RetiringSince.IsValidIndex(i) || Now - RetiringSince[i] >= KeepSeconds)
		{
			if (USkeletalMeshComponent* Old = RetiringVisuals[i])
			{
				Old->DestroyComponent();
			}
			RetiringVisuals.RemoveAt(i);
			if (RetiringSince.IsValidIndex(i))
			{
				RetiringSince.RemoveAt(i);
			}
		}
	}
}

void ATMBattleDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	RetireOldVisuals();
	AdvanceClockWarning();

	// An ultimate slows the world for a beat (UltimateBeat), a heavy blow holds
	// it still for an instant (TMBattleDirectorFeel.cpp). Only the look of it:
	// the battle's clock below is given real time, so the beat costs nobody a
	// moment of their turn. Fast-forward is the one exception, and gives the
	// clock its share (ClockShare).
	float RealDelta = DeltaSeconds;
	float ClockShare = 1.0f;
	if (GetWorld() && GetWorld()->GetWorldSettings())
	{
		const float Dilation = GetWorld()->GetWorldSettings()->GetEffectiveTimeDilation();
		if (Dilation > 0.001f)
		{
			RealDelta = DeltaSeconds / Dilation;
		}
		if (GetWorld()->IsGameWorld())
		{
			ClockShare = UpdateDilation();
		}
	}
	// A close-up on an ultimate goes back to where the camera was.
	if (CloseUpUntil > 0.0 && FPlatformTime::Seconds() >= CloseUpUntil)
	{
		EndCloseUp();
	}
	// Click rings that have faded.
	while (ClickMarks.Num() > 0 && FPlatformTime::Seconds() - ClickMarks[0].Born > ClickMarkSeconds)
	{
		ClickMarks.RemoveAt(0);
	}

	// How smoothly it runs, every ten seconds of play: the average frame rate
	// and the longest frame. A frame over a tenth of a second is named on its own.
	if (GetWorld() && GetWorld()->IsGameWorld() && Screen == EScreen::Battle)
	{
		FrameWindow += RealDelta;
		++FrameCount;
		FrameWorst = FMath::Max(FrameWorst, RealDelta);
		if (RealDelta > 0.1f)
		{
			UE_LOG(LogTemp, Log, TEXT("SLOW frame: %.0f ms"), RealDelta * 1000.0f);
		}
		if (FrameWindow >= 10.0f)
		{
			UE_LOG(LogTemp, Log, TEXT("FRAMES: %.1f fps over %.0f s, worst %.0f ms"), FrameCount / FrameWindow, FrameWindow, FrameWorst * 1000.0f);
			FrameWindow = 0.0f;
			FrameCount = 0;
			FrameWorst = 0.0f;
		}
	}

	{ TM_SLOW("Floaters"); AdvanceFloaters(DeltaSeconds); }
	{ TM_SLOW("Vfx"); AdvanceVfx(DeltaSeconds); }
	{ TM_SLOW("Looks"); AdvanceLooks(DeltaSeconds); }
	{ TM_SLOW("Cast"); AdvanceCast(DeltaSeconds); }
	if (GetWorld() && GetWorld()->IsGameWorld())
	{
		{ TM_SLOW("Motion"); AdvanceMotion(DeltaSeconds); }
		{ TM_SLOW("TurnRings"); AdvanceTurnRings(); }
		{ TM_SLOW("Indicators"); AdvanceIndicators(); }
		{ TM_SLOW("AuraFx"); AdvanceAuraFx(); }
		{ TM_SLOW("Marks"); UpdateMarks(); }
		{ TM_SLOW("Fog"); AdvanceFog(); }
		{ TM_SLOW("CardPortraits"); AdvanceCardPortraits(); }
		{ TM_SLOW("Board"); AdvanceBoard(DeltaSeconds); }
		{ TM_SLOW("Camps"); RefreshCamps(DeltaSeconds); }
		{ TM_SLOW("Towers"); AdvanceTowers(DeltaSeconds); }
		{ TM_SLOW("Hazards"); AdvanceHazards(DeltaSeconds); }
		// The camera on real time: as quick in a slow beat or a fast-forward as ever.
		{ TM_SLOW("Camera"); UpdateCamera(RealDelta); }
		if (TunePendingFor >= 0.0f && DragSlider < 0)
		{
			TunePendingFor -= DeltaSeconds;
			if (TunePendingFor < 0.0f)
			{
				FlushTuning();
			}
		}
	}
	MaybeCapture();

	// Only while playing: in the editor the clock is stepped by hand, so a
	// battle sitting in a level does not quietly run on while it is being built.
	if (!bBuilt || !GetWorld() || !GetWorld()->IsGameWorld())
	{
		return;
	}

	NoticeLeft -= DeltaSeconds;
	FullscreenCooldown -= FMath::Min(DeltaSeconds, 0.25f);
	if (bFullscreenPending && FullscreenCooldown <= 0.0f)
	{
		bFullscreenPending = false;
		FullscreenCooldown = 1.0f;
		FTMSettings::Get().Apply();
	}
	AdvanceOnline(DeltaSeconds);
	AdvanceDraft(DeltaSeconds);

	// The pointer says what a click would do, on every screen (TMBattleDirectorFeel.cpp).
	if (bPlayerInput)
	{
		UpdateCursor();
	}
	// On the title and setup screens the board stands behind the menu with its
	// clock stopped; a battle begins only when one is started.
	if (Screen != EScreen::Battle)
	{
		TickRemainder = 0.0f;
		if (MenuShotsAt >= 0.0f)
		{
			// -tmmenushots: a picture of each screen, then on to the battle.
			const float Before = MenuShotsAt;
			MenuShotsAt += DeltaSeconds;
			auto Shoot = [this](const TCHAR* Name)
			{
				const FString Where = FPaths::ProjectSavedDir() / TEXT("Match") / Name;
				FScreenshotRequest::RequestScreenshot(Where, true, false);
				UE_LOG(LogTemp, Log, TEXT("CAPTURE %s"), *Where);
			};
			if (Before < 1.0f && MenuShotsAt >= 1.0f)
			{
				Shoot(TEXT("menu_title.png"));
			}
			else if (Before < 1.5f && MenuShotsAt >= 1.5f)
			{
				Setup.Mode = TEXT("cpu");
				OpenSetup();
			}
			else if (Before < 2.5f && MenuShotsAt >= 2.5f)
			{
				Shoot(TEXT("menu_setup.png"));
			}
			else if (Before < 3.0f && MenuShotsAt >= 3.0f)
			{
				// Opened a moment after the picture above, which is taken when the
				// frame is drawn: opened in the same frame, it was in the picture.
				PickerSlot = 0;
			}
			else if (Before < 3.5f && MenuShotsAt >= 3.5f)
			{
				// Closed a moment later: the picture is taken when the frame is drawn.
				Shoot(TEXT("menu_picker.png"));
			}
			else if (Before < 4.5f && MenuShotsAt >= 4.5f)
			{
				PickerSlot = -1;
				bOptionsOpen = true;
			}
			else if (Before < 5.0f && MenuShotsAt >= 5.0f)
			{
				Shoot(TEXT("menu_options.png"));
			}
			else if (Before < 5.5f && MenuShotsAt >= 5.5f)
			{
				bOptionsOpen = false;
				bDevToolsOpen = true;
			}
			else if (Before < 6.0f && MenuShotsAt >= 6.0f)
			{
				Shoot(TEXT("menu_devtools.png"));
			}
			else if (Before < 6.5f && MenuShotsAt >= 6.5f)
			{
				bDevToolsOpen = false;
			}
			else if (MenuShotsAt >= 7.0f)
			{
				MenuShotsAt = -1.0f;
				StartMatch(false);
			}
		}
		return;
	}

	if (bPlayerInput)
	{
		// While planning nobody has a turn, so there is no turn's selection to
		// keep; the unit being placed is kept apart from it.
		if (Battle.IsPlanning())
		{
			if (SelectedId != -1)
			{
				Deselect();
			}
		}
		else
		{
			PlaceId = -1;
			TM_SLOW("Selection");
			MaintainSelection();
			// Plans whose units' turns have begun, and Go Tos (TMBattleDirectorPlans.cpp);
			// first, any queued unit that has just seen an enemy is handed back.
			CancelQueuesOnSight();
			RunDuePlans();
			RunDueGoTos();
			// Online: walks shown before the host answered that it refused go back.
			if (PredictedWalks.Num() > 0)
			{
				SettlePredictions();
			}
			bHostRefused = false;
			// Online: what was pressed while the last order was with the host, now it has answered.
			if (bWasWaitingForHost && !bWaitingForHost)
			{
				ReplayBuffered();
			}
			bWasWaitingForHost = bWaitingForHost;
		}
		{ TM_SLOW("PickUnderCursor"); PickUnderCursor(); }
		UpdateWalkPress();
		{ TM_SLOW("HoverPath"); UpdateHoverPath(); }
		// The camera's own moves, by its rules (TMBattleDirectorFeel.cpp).
		UpdateFollow();
	}
	{ TM_SLOW("Threat"); UpdateThreat(); }

	if (HudShotsAt >= 0.0f)
	{
		// -tmhudshots: the panels a person opens by clicking, opened here on a
		// timer so a run nobody is at can take pictures of them. Real seconds,
		// not battle time, because the guide pauses the battle.
		// It waits for an enemy to come into sight, since a card is only shown
		// for a unit this side can see, and then holds the clock still (from
		// 14 s on) while the pictures are taken.
		if (HudShotsAt < 14.0f)
		{
			HudShotsAt += DeltaSeconds;
			if (HudShotsAt >= 14.0f)
			{
				for (const TMSim::FUnit& Unit : Battle.Units)
				{
					if (Unit.Team == 1 && Unit.IsAlive() && IsSeen(Unit) && InspectedId < 0)
					{
						InspectedId = Unit.Id;
					}
				}
				if (InspectedId < 0)
				{
					HudShotsAt = 13.0f;  // nobody in sight yet: look again in a second
				}
				else
				{
					bShowField = true;
					bLogLarge = true;
					bPaused = true;
				}
			}
		}
		const float Before = HudShotsAt;
		if (Before >= 14.0f)
		{
			HudShotsAt += DeltaSeconds;
		}
		auto Passed = [Before, this](float At) { return Before < At && HudShotsAt >= At; };
		if (Passed(15.5f))
		{
			CaptureNamed(TEXT("hud_panels.png"));
		}
		if (Passed(16.0f))
		{
			ToggleGuide();
		}
		if (Passed(17.5f))
		{
			CaptureNamed(TEXT("hud_guide.png"));
		}
		if (Passed(18.0f))
		{
			ToggleGuide();
			bPaused = false;
			HudShotsAt = -1.0f;
		}
	}

	// Paused, nothing moves: not the clock and not the computer. Nothing is
	// submitted for it either -- a pause is time not passing, and time only
	// passes by an Advance order. Online, both machines would have to agree to
	// one; there is no online play yet, so this is a local-only key for now.
	// The in-battle menu pauses the same way.
	//
	// Online, time can't stop (battle.gd:1030-1044): the menu stays open over a
	// battle that goes on. A match that can't go on stops for good.
	if ((bPaused || bMenuOpen) && !bOnline)
	{
		TickRemainder = 0.0f;
		return;
	}
	if (!OnlineStopped.IsEmpty())
	{
		TickRemainder = 0.0f;
		return;
	}
	// Heroes still loading: the battle waits for them, as for a pause. Online
	// the host's clock can't wait on this machine; the units there stand in the
	// plain mesh for a moment instead.
	if (BodyLoad.IsValid() && !bOnline)
	{
		TickRemainder = 0.0f;
		return;
	}
	// A replay moves by its own clock and its own orders; nobody plays.
	if (bReplaying)
	{
		TickRemainder = 0.0f;
		AdvanceReplay(RealDelta);
		return;
	}
	FirePendingAbility();

	// The rules run at a fixed rate whatever the frame rate is doing. That is
	// not a detail: the same battle has to play out the same way on both
	// machines in an online match, and on a replay.
	const float SecondsPerTick = 1.0f / static_cast<float>(TMSim::Pace::TicksPerSecond);
	TickRemainder += RealDelta * ClockShare;
	int32 Steps = 0;
	while (TickRemainder >= SecondsPerTick && Steps < 30)
	{
		TickRemainder -= SecondsPerTick;
		++Steps;
	}
	// Online, only the host moves time; the joiner's clock moves as the host's
	// Advance orders arrive (battle.gd:316-322).
	if (Steps > 0 && (!bOnline || bOnlineHost))
	{
		TM_SLOW("StepTicks");
		StepTicks(Steps);
	}

	// A battle run with nobody watching has to be able to finish. Without this a
	// headless run sits there for ever after the last unit falls, and the next
	// build cannot replace a DLL the dead session is still holding open.
	if (Battle.Winner != -1)
	{
		if (!bSaidWon)
		{
			bSaidWon = true;
			UE_LOG(LogTemp, Log,
				TEXT("BATTLE OVER: winner %d after %.1fs, %d orders, %d numbers shown, %d effects played"),
				Battle.Winner, Battle.TickCount / float(TMSim::Pace::TicksPerSecond),
				OrdersGiven, NumbersShown, EffectsPlayed);
			UE_LOG(LogTemp, Log, TEXT("%s"), *DescribeBattle());
			UE_LOG(LogTemp, Log, TEXT("FINAL CHECKSUM %llu at tick %d"), Battle.Checksum(), Battle.TickCount);
			DecidedFor = 0.0f;
			SaveRecording();
			// -tmreplaycheck: watch the replay just saved to its end, at once, and
			// say whether it is the same battle (Docs/design/feat-replays.md).
			if (FParse::Param(FCommandLine::Get(), TEXT("tmreplaycheck")) && !LastReplayFile.IsEmpty())
			{
				const uint64 Played = Battle.Checksum();
				const int32 PlayedTicks = Battle.TickCount;
				const FString File = LastReplayFile;
				WatchReplay(File);
				SeekReplay(Watching.Ticks);
				const bool bSame = Battle.Checksum() == Played && Battle.TickCount == PlayedTicks && ReplayProblem.IsEmpty();
				UE_LOG(LogTemp, Log, TEXT("REPLAY CHECK: %s (%d steps; tick %d of %d; %s)"), bSame ? TEXT("THE REPLAY IS THE SAME BATTLE") : TEXT("THE REPLAY DIFFERS"),
					Watching.Steps.Num(), Battle.TickCount, PlayedTicks, ReplayProblem.IsEmpty() ? TEXT("no problem said") : *ReplayProblem);
				LeaveReplay(false);
				if (FApp::IsUnattended())
				{
					FPlatformMisc::RequestExit(false);
				}
				return;
			}
		}
		// The robot decides when its session is over, not the first result.
		if (FApp::IsUnattended() && !bRobotDriving)
		{
			// A moment on the result before leaving, so a run taking pictures
			// gets one of the end of the battle -- it used to exit in the very
			// frame the battle was decided, before anything showed the result.
			const float Before = DecidedFor;
			DecidedFor += DeltaSeconds;
			if (CaptureEverySeconds > 0.0f && Before < 0.5f && DecidedFor >= 0.5f)
			{
				const FString Where = FPaths::ProjectSavedDir() / TEXT("Match") / TEXT("frame_end.png");
				FScreenshotRequest::RequestScreenshot(Where, true, false);
				UE_LOG(LogTemp, Log, TEXT("CAPTURE end -> %s"), *Where);
			}
			if (DecidedFor >= 1.5f)
			{
				FPlatformMisc::RequestExit(false);
			}
		}
		return;
	}

	// Whoever the battle is waiting on, if the computer is playing that side it
	// takes a moment to think and then gives one order. The pause is what makes
	// it readable: a side that emptied its whole turn into one frame would be
	// impossible to learn anything from.
	// A joiner's computer (-tmnetbots) waits for the host's answer like a person does.
	const TMSim::FUnit* Unit = bWaitingForHost ? nullptr : WaitingOnComputer();
	if (!Unit)
	{
		ThinkingAbout = -1;
		return;
	}
	if (ThinkingAbout != Unit->Id)
	{
		ThinkingAbout = Unit->Id;
		ThinkRemainder = ThinkSeconds(Unit->Team, true);
	}
	// On the clock's time: a slow beat or a hit-stop is only the look, and costs
	// the computer nothing; fast-forward hurries it as it hurries the clock.
	ThinkRemainder -= RealDelta * ClockShare;
	if (ThinkRemainder <= 0.0f)
	{
		TM_SLOW("ComputerTurn");
		const int32 UnitId = Unit->Id;
		if (!TakeComputerTurn(UnitId).IsEmpty())
		{
			EndUnitTurn(UnitId);
		}
		++OrdersGiven;
		ThinkRemainder = ThinkSeconds(Unit->Team, false);
	}
}

float ATMBattleDirector::ThinkSeconds(int32 Team, bool bFirst) const
{
	// Monsters don't deliberate: a short beat so their moves can be followed.
	if (Team < 0 || Team > 1)
	{
		return bFirst ? 0.35f : 0.45f;
	}
	return static_cast<float>(bFirst ? Computers[Team].Skill().Think : Computers[Team].Skill().Step);
}

// ============================================================ a person playing
//
// Follows the Godot game's battle.gd: select a ready unit, walk it, aim one of
// its four abilities, end its turn. Everything below only chooses which order
// to send; the rules decide whether it is allowed, and say why not.

namespace
{
	const TMSim::FUnit* FindIn(const TMSim::FBattle& Battle, int32 UnitId)
	{
		return UnitId >= 0 ? const_cast<TMSim::FBattle&>(Battle).FindUnit(UnitId) : nullptr;
	}

	/** Whether a unit is the kind of target this ability takes (battle.gd:894-897). */
	bool FitsTarget(const TMSim::FUnit& User, const TMSim::FAbility& Ability, const TMSim::FUnit& Target)
	{
		if (Ability.Target == TMSim::ETargetSide::KoAlly)
		{
			return Target.IsKo() && Target.Team == User.Team;
		}
		return Target.IsAlive() && ((Target.Team != User.Team) == (Ability.Target == TMSim::ETargetSide::Enemy));
	}

	/**
	 * Ready units first, then the least time left, then id (game_state.gd:768-775,
	 * _schedule_before). Every unit this is asked about is ready, and a ready
	 * unit's time left is its countdown.
	 */
	bool ActsSooner(const TMSim::FUnit* A, const TMSim::FUnit* B)
	{
		if (A->Clock != B->Clock)
		{
			return A->Clock < B->Clock;
		}
		return A->Id < B->Id;
	}
}

void ATMBattleDirector::SetUpPlayerInput()
{
	UWorld* World = GetWorld();
	APlayerController* Player = World ? World->GetFirstPlayerController() : nullptr;
	if (!Player)
	{
		return;
	}
	EnableInput(Player);
	if (!InputComponent)
	{
		return;
	}

	// The pointer stays on screen and free to leave the window: this is a game
	// played by clicking on the board, not by steering a camera.
	Player->bShowMouseCursor = true;
	FInputModeGameAndUI Mode;
	Mode.SetHideCursorDuringCapture(false);
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	Player->SetInputMode(Mode);

	// Every key a keyboard has, bound to keys directly rather than through input
	// assets (which would be editor work), so any of them can be given to an
	// action in Options. What a key does is looked up in FTMSettings each time.
	TArray<FKey> Every;
	EKeys::GetAllKeys(Every);
	for (const FKey& Key : Every)
	{
		if (!Key.IsValid() || Key == EKeys::AnyKey || Key.IsGamepadKey() || Key.IsTouch() || Key.IsAxis1D()
			|| Key.IsAxis2D() || Key.IsAxis3D() || (Key.IsMouseButton() && Key != EKeys::LeftMouseButton
				&& Key != EKeys::RightMouseButton && Key != EKeys::MiddleMouseButton
				&& Key != EKeys::MouseScrollUp && Key != EKeys::MouseScrollDown))
		{
			continue;
		}
		InputComponent->BindKey(Key, IE_Pressed, this, &ATMBattleDirector::OnKey);
	}
	// Let go too: the mouse's buttons end drags, and a Quick Cast key uses its
	// ability when it is let go (2026-10-03).
	for (const FKey& Key : Every)
	{
		if (!Key.IsValid() || Key == EKeys::AnyKey || Key.IsGamepadKey() || Key.IsTouch() || Key.IsAxis1D()
			|| Key.IsAxis2D() || Key.IsAxis3D() || Key == EKeys::MouseScrollUp || Key == EKeys::MouseScrollDown
			|| (Key.IsMouseButton() && Key != EKeys::LeftMouseButton && Key != EKeys::RightMouseButton && Key != EKeys::MiddleMouseButton))
		{
			continue;
		}
		InputComponent->BindKey(Key, IE_Released, this, &ATMBattleDirector::OnKeyUp);
	}
	bPlayerInput = true;
	// Typed text -- the address to join, chat -- goes past the key bindings (TMTextInput.h).
	if (!TextInput.IsValid() && FSlateApplication::IsInitialized())
	{
		TextInput = MakeShared<FTMTextInput>();
		FSlateApplication::Get().RegisterInputPreProcessor(TextInput);
		TWeakPtr<FTMTextInput> Weak = TextInput;
		UTMViewportClient::Typist = [Weak](TCHAR Character)
		{
			const TSharedPtr<FTMTextInput> Input = Weak.Pin();
			return Input.IsValid() && Input->TakeCharacter(Character);
		};
	}
	UE_LOG(LogTemp, Log, TEXT("taking orders from the mouse and keyboard"));
}

void ATMBattleDirector::OnKey(FKey Key)
{
	// battle.gd:699-739, _unhandled_input, with every key read through the
	// player's bindings (FTMSettings) rather than fixed.
	const FTMSettings& Keys = FTMSettings::Get();
	auto Is = [&Keys, &Key](ETMAction Action) { return Keys.Is(Key, Action); };
	const TMSim::FUnit* Sel = SelectedUnit();
	// Any key or click ends an ultimate's close-up at once (2026-10-03), and
	// holds the camera's own moves for a moment (camera rules A).
	EndCloseUp();
	LastInputAt = FPlatformTime::Seconds();

	// Options is waiting for a new key: this one is it, unless it is Esc.
	if (CaptureAction >= 0)
	{
		if (Key == EKeys::Escape)
		{
			CaptureAction = -1;
		}
		else if (!Key.IsMouseButton())
		{
			FTMSettings::Get().Rebind(static_cast<ETMAction>(CaptureAction), Key);
			CaptureAction = -1;
		}
		return;
	}
	if (Key == EKeys::LeftMouseButton)
	{
		OnClick();
		// The robot playtester clicks without letting go: a walk it pressed goes at once.
		if (bRobotDriving && WalkPress.bActive)
		{
			ReleaseWalk();
		}
		return;
	}
	if (Key == EKeys::RightMouseButton)
	{
		// A right click calls off a walk being pressed.
		WalkPress.bActive = false;
		bRightHeld = true;
		RightDragged = 0.0f;
		return;
	}
	if (Key == EKeys::MiddleMouseButton)
	{
		bMiddleHeld = true;
		return;
	}
	// Watching a replay, its own keys come first: play and pause, a step either way, speed, leave.
	if (!bOptionsOpen && !bDevToolsOpen && !bGuideOpen && ReplayKey(Key))
	{
		return;
	}
	// Edit layout: its key starts it and locks it again; Cancel locks it too.
	if (Screen == EScreen::Battle && (Is(ETMAction::EditLayout) || (bEditingLayout && Is(ETMAction::Cancel)))
		&& !bOptionsOpen && !bDevToolsOpen)
	{
		if (Is(ETMAction::EditLayout) || bEditingLayout)
		{
			ToggleLayout();
		}
		return;
	}
	// Options and Developer Tools cover the screen: Esc closes them.
	if (bOptionsOpen || bDevToolsOpen)
	{
		if (bOptionsOpen && (Key == EKeys::MouseScrollUp || Key == EKeys::MouseScrollDown))
		{
			OptionsScroll = FMath::Max(0.0f, OptionsScroll + (Key == EKeys::MouseScrollUp ? -80.0f : 80.0f));
			return;
		}
		if (Key == EKeys::Escape || Is(ETMAction::Cancel))
		{
			bOptionsOpen = false;
			bDevToolsOpen = false;
			FlushTuning();
			FTMSettings::Get().Save();
		}
		return;
	}
	// The wheel scrolls the log when the pointer is over it, and brings the
	// camera closer or further anywhere else (camera_rig.gd:88-93).
	if (Key == EKeys::MouseScrollUp || Key == EKeys::MouseScrollDown)
	{
		float X = 0.0f;
		float Y = 0.0f;
		const APlayerController* Player = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
		const ATMBattleHud* Hud = Player ? Cast<ATMBattleHud>(Player->GetHUD()) : nullptr;
		if (bEditingLayout && Hud && CursorPosition(X, Y))
		{
			for (int32 i = Hud->Movables.Num() - 1; i >= 0; --i)
			{
				if (Hud->Movables[i].Area.IsInside(FVector2D(X, Y)))
				{
					FTMSettings& Settings = FTMSettings::Get();
					const float Now = Settings.ScaleOf(*Hud->Movables[i].Id);
					Settings.LayoutScale.Add(Hud->Movables[i].Id, FMath::Clamp(Now + (Key == EKeys::MouseScrollUp ? 0.05f : -0.05f), 0.5f, 2.5f));
					Settings.Save();
					return;
				}
			}
		}
		if (bGuideOpen)
		{
			// The guide covers the screen: the wheel scrolls it, the list by rows
			// and a class's page by a little at a time. The HUD keeps both in range.
			const bool bUp = Key == EKeys::MouseScrollUp;
			if (bGuideDetail)
			{
				GuideDetailScroll = FMath::Max(0.0f, GuideDetailScroll + (bUp ? -90.0f : 90.0f));
			}
			else
			{
				// The Codex's pages by a row; the class and item lists by three.
				const int32 By = GuideTab >= 2 ? 1 : 3;
				GuideListScroll = FMath::Max(0, GuideListScroll + (bUp ? -By : By));
			}
		}
		else if (ItemPickerSlot >= 0)
		{
			// The item picker's grid is longer than the screen: the wheel scrolls it.
			ItemPickerScroll = FMath::Max(0, ItemPickerScroll + (Key == EKeys::MouseScrollUp ? -1 : 1));
		}
		else if (Screen == EScreen::Battle && Hud && CursorPosition(X, Y) && Hud->LogArea.bIsValid && Hud->LogArea.IsInside(FVector2D(X, Y)))
		{
			// The newest line is never more than a few turns of the wheel away.
			LogScroll = FMath::Clamp(LogScroll + (Key == EKeys::MouseScrollUp ? 1 : -1), 0, FMath::Max(0, Log.Num() - 1));
		}
		else if (!bGuideOpen && PickerSlot < 0)
		{
			// Toward the pointer, unless Options says the middle (TMBattleDirectorFeel.cpp).
			ZoomCamera(Key == EKeys::MouseScrollUp);
		}
		return;
	}
	// The Unit Guide covers the screen: only its key or Cancel closes it.
	if (bGuideOpen)
	{
		if (Is(ETMAction::Cancel) && bGuideDetail)
		{
			// Back from a class's page to the list, before closing.
			bGuideDetail = false;
			HideGuideModel();
		}
		else if (Is(ETMAction::UnitGuide) || Is(ETMAction::Cancel))
		{
			ToggleGuide();
		}
		else if (bGuideDetail && (Key == EKeys::Left || Key == EKeys::Right))
		{
			GuideStep(Key == EKeys::Left ? -1 : 1);
		}
		else if (Key == EKeys::Up || Key == EKeys::Down || Key == EKeys::PageUp || Key == EKeys::PageDown)
		{
			const int32 Way = Key == EKeys::Up || Key == EKeys::PageUp ? -1 : 1;
			const bool bPage = Key == EKeys::PageUp || Key == EKeys::PageDown;
			if (bGuideDetail)
			{
				GuideDetailScroll = FMath::Max(0.0f, GuideDetailScroll + Way * (bPage ? 600.0f : 90.0f));
			}
			else
			{
				GuideListScroll = FMath::Max(0, GuideListScroll + Way * (bPage ? FMath::Max(1, GuideListPage - 1) : 1));
			}
		}
		return;
	}
	// Away from a battle only the mouse and Cancel mean anything: Cancel steps
	// back from the setup to the title.
	if (Screen != EScreen::Battle)
	{
		if (Is(ETMAction::Cancel) && PickerSlot >= 0)
		{
			PickerSlot = -1;
		}
		else if (Is(ETMAction::Cancel) && ItemPickerSlot >= 0)
		{
			ItemPickerSlot = -1;
		}
		else if (Is(ETMAction::Cancel) && Screen == EScreen::Setup && Net.IsValid() && Net->IsHost())
		{
			// The host's rules, from its lobby: back to the lobby.
			OpenLobby();
			BroadcastLobby();
		}
		else if (Is(ETMAction::Cancel) && (Screen == EScreen::Lobby || Screen == EScreen::Draft))
		{
			LeaveOnline();
			OpenTitle();
		}
		else if (Is(ETMAction::Cancel) && Screen == EScreen::Setup && Setup.Mode == TEXT("online"))
		{
			Upnp.Reset();
			Net.Reset();
			OnlineStatus.Reset();
			OpenOnline();
		}
		else if (Is(ETMAction::Cancel) && Screen == EScreen::Setup)
		{
			OpenTitle();
		}
		else if (Is(ETMAction::Cancel) && Screen == EScreen::Replays)
		{
			OpenTitle();
		}
		else if (Is(ETMAction::Cancel) && Screen == EScreen::Online)
		{
			LeaveOnline();
			OpenTitle();
		}
		else if (Is(ETMAction::UnitGuide))
		{
			ToggleGuide();
		}
		return;
	}
	if (Is(ETMAction::EndTurn) && Battle.IsPlanning())
	{
		ReadyToFight();
		return;
	}
	if (Is(ETMAction::Cancel))
	{
		// Cancel drops an aim first; with nothing to drop it opens the menu
		// (battle.gd:1039-1051 pauses the game while a menu is open).
		if (WalkPress.bActive)
		{
			WalkPress.bActive = false;
		}
		else if (bTeamItemsOpen)
		{
			bTeamItemsOpen = false;
			StashPick = -1;
		}
		else if (bMenuOpen)
		{
			bMenuOpen = false;
		}
		else if (AimMode != EAimMode::None || !WayPoints.empty())
		{
			CancelAim();
		}
		else if (bPlanMode)
		{
			// Done planning; what was planned stays.
			StopPlanning();
			AutoSelect();
		}
		else if (Battle.Winner == -1)
		{
			bMenuOpen = true;
		}
		return;
	}
	if (bMenuOpen)
	{
		return;
	}
	// Online, with the last order still out with the host: an order key is kept
	// and pressed again the moment the answer comes (TMBattleDirectorFeel.cpp).
	const bool bOrderKey = Is(ETMAction::Ability1) || Is(ETMAction::Ability2) || Is(ETMAction::Ability3) || Is(ETMAction::Ability4)
		|| Key == EKeys::Five || Key == EKeys::Six || Key == EKeys::Seven || Is(ETMAction::Move) || Is(ETMAction::Sprint)
		|| Is(ETMAction::EndTurn) || Is(ETMAction::PlanTurn);
	if (bOrderKey && BufferWhileWaiting(Key, false))
	{
		return;
	}
	// An ability key: aims it. With Quick Cast on for it (Options), held it
	// aims, and letting go uses it where the pointer is (OnKeyUp); pressed
	// again while aiming it, it keeps the aim rather than dropping it.
	auto AbilityKey = [this, &Key](int32 Slot)
	{
		if (!FTMSettings::Get().bQuickCast[Slot])
		{
			SelectAbility(Slot);
			return;
		}
		QuickSlot = -1;
		if (!(AimMode == EAimMode::Ability && AimSlot == Slot))
		{
			SelectAbility(Slot);
		}
		if (AimMode == EAimMode::Ability && AimSlot == Slot)
		{
			QuickSlot = Slot;
			QuickKey = Key;
		}
	};
	if (Is(ETMAction::Ability1)) { AbilityKey(0); }
	else if (Is(ETMAction::Ability2)) { AbilityKey(1); }
	else if (Is(ETMAction::Ability3)) { AbilityKey(2); }
	else if (Is(ETMAction::Ability4)) { AbilityKey(3); }
	// The items' abilities, on the keys after the class's four.
	else if (Key == EKeys::Five) { SelectAbility(4); }
	else if (Key == EKeys::Six) { SelectAbility(5); }
	else if (Key == EKeys::Seven) { SelectAbility(6); }
	// Queued orders (TMBattleDirectorPlans.cpp). The waypoint key is only held
	// while clicking: pressed alone it does nothing.
	else if (Is(ETMAction::Waypoint)) {}
	// Fast-forward is read while held (WantsFastForward), not pressed.
	else if (Is(ETMAction::FastForward)) {}
	else if (Is(ETMAction::PlanTurn)) { PlanKey(); }
	else if (Is(ETMAction::PlanCancel)) { CancelQueueKey(); }
	else if (Is(ETMAction::Move))
	{
		// battle.gd:966-972, _toggle_move.
		if (!PlayerCanCommand(Sel))
		{
			return;
		}
		if (AimMode == EAimMode::Move && !bSprinting)
		{
			CancelAim();
		}
		else if (Sel->bMoved && !IsPlanningSelected())
		{
			Deny(TEXT("noMove"), FString::Printf(TEXT("%s has no movement left this turn."), *LogName(Sel->Id)));
		}
		else
		{
			EnterMoveMode(false);
		}
	}
	else if (Is(ETMAction::Sprint))
	{
		// battle.gd:977-987, _toggle_sprint: further than a walk, but it is the
		// unit's action for the turn.
		if (!PlayerCanCommand(Sel))
		{
			return;
		}
		if (AimMode == EAimMode::Move && bSprinting)
		{
			CancelAim();
			return;
		}
		if (Sel->bActed)
		{
			Deny(TEXT("noAction"), TEXT("Already used an ability this turn: no sprinting."));
			return;
		}
		if (Sel->bMoved && !IsPlanningSelected())
		{
			Deny(TEXT("noMove"), FString::Printf(TEXT("%s has no movement left this turn."), *LogName(Sel->Id)));
			return;
		}
		EnterMoveMode(true);
	}
	else if (Is(ETMAction::EndTurn))
	{
		if (PlayerCanOrder(Sel))
		{
			OrderSelected(TMSim::FOrder::MakeEndTurn(Sel->Id, Sel->Serial));
		}
	}
	else if (Is(ETMAction::Log))
	{
		// Shown, shown large, hidden.
		if (!bShowLog)
		{
			bShowLog = true;
			bLogLarge = false;
		}
		else if (!bLogLarge)
		{
			bLogLarge = true;
		}
		else
		{
			bShowLog = false;
		}
		LogScroll = 0;
	}
	else if (Is(ETMAction::UnitGuide))
	{
		ToggleGuide();
	}
	else if (Is(ETMAction::AutoRecenter))
	{
		// Round the three ways the camera may follow (2026-10-03).
		FTMSettings& Settings = FTMSettings::Get();
		Settings.CameraFollow = (Settings.CameraFollow + 1) % 3;
		Settings.bAutoRecenter = Settings.CameraFollow != 2;
		Settings.Save();
		static const TCHAR* const Ways[3] = { TEXT("always"), TEXT("when you're idle"), TEXT("never") };
		Tell(FString::Printf(TEXT("The camera follows to the next ready unit: %s (%s)."), Ways[Settings.CameraFollow],
			*Settings.KeyName(ETMAction::AutoRecenter)));
	}
	else if (Is(ETMAction::StatusBars))
	{
		bShowStatusBars = !bShowStatusBars;
		Tell(FString::Printf(TEXT("Status bars %s (%s)."), bShowStatusBars ? TEXT("shown") : TEXT("hidden"),
			*FTMSettings::Get().KeyName(ETMAction::StatusBars)));
	}
	else if (Is(ETMAction::NextUnit))
	{
		CycleReady();
	}
	else if (Is(ETMAction::CenterCamera))
	{
		CenterCamera();
	}
	else if (Is(ETMAction::Pause))
	{
		if (bOnline)
		{
			Tell(TEXT("Time can't stop in an online match."));
		}
		else if (Battle.Winner == -1)
		{
			bPaused = !bPaused;
			Tell(bPaused ? TEXT("Paused.") : TEXT("Resumed."));
		}
	}
	else if (Is(ETMAction::Chat) && bOnline)
	{
		StartTyping(ETypeField::Chat);
	}
	else if (Key == EKeys::R && Battle.Winner != -1)
	{
		// Rematch, only once a battle is decided, so a stray key cannot throw
		// one away; while it is being fought R raises the camera. Online, the
		// host takes everyone back to the lobby.
		if (bOnline)
		{
			RequestRematch();
		}
		else
		{
			StartMatch(true);
		}
	}
}

void ATMBattleDirector::OnKeyUp(FKey Key)
{
	LastInputAt = FPlatformTime::Seconds();
	// A Quick Cast key let go: its ability is used where the pointer is.
	if (QuickSlot >= 0 && Key == QuickKey)
	{
		QuickRelease();
		return;
	}
	// A walk pressed and now let go: walked, facing the way it was dragged.
	if (Key == EKeys::LeftMouseButton && WalkPress.bActive)
	{
		ReleaseWalk();
		return;
	}
	if (Key == EKeys::RightMouseButton)
	{
		bRightHeld = false;
		// A click rather than a drag: it drops an aim, as it always has -- unless it is
		// on the card of one of this player's units with something queued (its row on
		// the squad strip, its queue tab, its turn square): then it cancels that queue
		// ("Queued orders" B, 2026-10-06: "add right click card to cancel").
		if (RightDragged < 6.0f && Screen == EScreen::Battle && !bMenuOpen && !bGuideOpen && !bOptionsOpen && !bDevToolsOpen)
		{
			APlayerController* Clicker = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
			const ATMBattleHud* Hud = Clicker ? Cast<ATMBattleHud>(Clicker->GetHUD()) : nullptr;
			float CardX = 0.0f;
			float CardY = 0.0f;
			FTMHudButton Card;
			if (Hud && CursorPosition(CardX, CardY) && Hud->ButtonAt(FVector2D(CardX, CardY), Card)
				&& (Card.Action == ETMHudAction::PickUnit || Card.Action == ETMHudAction::QueueCancel) && Card.Value >= 0)
			{
				const TMSim::FUnit* Carded = Battle.FindUnit(Card.Value);
				if (Carded && PlayerCanPlan(Carded) && !QueueSummary(*Carded).IsEmpty())
				{
					CancelQueue(Card.Value, true);
					return;
				}
			}
			CancelAim();
		}
	}
	else if (Key == EKeys::MiddleMouseButton)
	{
		bMiddleHeld = false;
	}
	else if (Key == EKeys::LeftMouseButton && DragSlider >= 0)
	{
		DragSlider = -1;
		FTMSettings::Get().Save();
	}
	else if (Key == EKeys::LeftMouseButton && (!DragPanel.IsEmpty() || !ResizePanel.IsEmpty()))
	{
		DragPanel.Reset();
		ResizePanel.Reset();
		FTMSettings::Get().Save();
	}
	else if (Key == EKeys::LeftMouseButton && DragCard >= 0)
	{
		DropCard();
	}
}

bool ATMBattleDirector::PlayerCanOrder(const TMSim::FUnit* Unit) const
{
	// battle.gd:293-295, _commandable.
	if (bOnline && (Unit == nullptr || UnitOwner(*Unit) != LocalPlayer || bWaitingForHost || !OnlineStopped.IsEmpty()))
	{
		// Online, only this player's own units, and one order at a time (battle.gd:293-295).
		return false;
	}
	return Unit && Unit->IsAlive() && Unit->bReady && !ComputerPlaysUnit(*Unit)
		&& Battle.Winner == -1 && (bOnline || (!bPaused && !bMenuOpen)) && Screen == EScreen::Battle;
}

const TMSim::FUnit* ATMBattleDirector::SelectedUnit() const
{
	return FindIn(Battle, SelectedId);
}

void ATMBattleDirector::SelectUnit(int32 UnitId)
{
	// battle.gd:912-923. Walking is offered straight away, since it is what a
	// turn usually starts with.
	const TMSim::FUnit* Unit = FindIn(Battle, UnitId);
	if (!Unit)
	{
		return;
	}
	if (PendingAbility.UnitId >= 0 && PendingAbility.UnitId != UnitId)
	{
		PendingAbility = FPendingAbility();  // a different unit was picked: call the attack off
	}
	SelectedId = UnitId;
	SelectedSerial = Unit->Serial;
	bPlanMode = false;
	WayPoints.clear();
	AimMode = EAimMode::None;
	AimSlot = -1;
	bSprinting = false;
	PathNode = TMSim::FNode{ -9999, -9999 };
	if (!Unit->bMoved)
	{
		EnterMoveMode(false);
	}
}

void ATMBattleDirector::Deselect()
{
	SelectedId = -1;
	SelectedSerial = -1;
	bPlanMode = false;
	WayPoints.clear();
	AimMode = EAimMode::None;
	AimSlot = -1;
	bSprinting = false;
	Reachable.clear();
	PathShown.clear();
}

void ATMBattleDirector::AutoSelect()
{
	// battle.gd:933-937: this machine's ready unit with the least time left.
	const TMSim::FUnit* Best = nullptr;
	for (const TMSim::FUnit& Unit : Battle.Units)
	{
		if (PlayerCanOrder(&Unit) && !IsMarching(Unit.Id) && (!Best || ActsSooner(&Unit, Best)))
		{
			Best = &Unit;
		}
	}
	if (Best)
	{
		SelectUnit(Best->Id);
	}
}

void ATMBattleDirector::CycleReady()
{
	// battle.gd:940-951.
	std::vector<const TMSim::FUnit*> Ready;
	for (const TMSim::FUnit& Unit : Battle.Units)
	{
		// Marching units look after themselves, as Civilization III skips them.
		if (PlayerCanOrder(&Unit) && !IsMarching(Unit.Id))
		{
			Ready.push_back(&Unit);
		}
	}
	if (Ready.empty())
	{
		return;
	}
	std::sort(Ready.begin(), Ready.end(), ActsSooner);
	size_t Next = 0;
	for (size_t i = 0; i < Ready.size(); ++i)
	{
		if (Ready[i]->Id == SelectedId)
		{
			Next = (i + 1) % Ready.size();
		}
	}
	// A unit pointed out as ready (2026-10-03, ReadyToastId) is the one N goes to.
	for (size_t i = 0; i < Ready.size(); ++i)
	{
		if (Ready[i]->Id == ReadyToastId && ReadyToastId != SelectedId)
		{
			Next = i;
		}
	}
	ReadyToastId = -1;
	if (bPlanMode)
	{
		StopPlanning();
	}
	SelectUnit(Ready[Next]->Id);
	// Tab goes to the unit: the camera slides onto it, wherever it stands.
	CenterCamera();
}

void ATMBattleDirector::EnterMoveMode(bool bSprint)
{
	// battle.gd:954-963.
	const TMSim::FUnit* Unit = SelectedUnit();
	if (!Unit || Unit->bMoved || (bSprint && Unit->bActed))
	{
		return;
	}
	AimMode = EAimMode::Move;
	bSprinting = bSprint;
	AimSlot = -1;
	WayPoints.clear();
	Reachable = Battle.ReachableNodes(*Unit, bSprint);
	RefreshZoneShadow();
	PathNode = TMSim::FNode{ -9999, -9999 };
	PathShown.clear();
}

void ATMBattleDirector::SelectAbility(int32 Slot)
{
	// battle.gd:990-1006. The reason an ability cannot be used is the rules'
	// (AbilityBlockedReason), so the words are the same ones a refused order gets.
	const TMSim::FUnit* Unit = SelectedUnit();
	if (!PlayerCanCommand(Unit))
	{
		return;
	}
	if (AimMode == EAimMode::Ability && AimSlot == Slot)
	{
		CancelAim();
		return;
	}
	// Planned: as it will stand when the turn comes (TMBattleDirectorPlans.cpp).
	// Leaving the walk's aim first, so it stands where the planned walk ends.
	if (IsPlanningSelected() && AimMode == EAimMode::Move)
	{
		AimMode = EAimMode::None;
	}
	const FPlanStandIn Stand(*this);
	if (Unit->bActed)
	{
		Deny(TEXT("noAction"), TEXT("Already used an ability this turn."));
		return;
	}
	const std::string Blocked = Battle.AbilityBlockedReason(*Unit, Slot);
	if (!Blocked.empty())
	{
		Tell(UTF8_TO_TCHAR(Blocked.c_str()));
		return;
	}
	AimMode = EAimMode::Ability;
	AimSlot = Slot;
	bSprinting = false;
}

void ATMBattleDirector::CancelAim()
{
	// battle.gd:1009-1014.
	AimMode = EAimMode::None;
	AimSlot = -1;
	bSprinting = false;
	WayPoints.clear();
}

bool ATMBattleDirector::OrderSelected(const TMSim::FOrder& Order)
{
	// battle.gd:389-395, then what it does after the order lands (:468-485).
	// A person's own order for a unit on a Go To ends the Go To.
	if (!bGoToOrdering && GoTos.Contains(Order.UnitId))
	{
		CancelGoTo(Order.UnitId, false);
	}
	const FString Refused = Submit(Order);
	if (!Refused.IsEmpty())
	{
		Tell(Refused);
		return false;
	}
	// The waypoints were for this walk (2026-10-01).
	if (Order.Type == TMSim::EOrderType::Move && Order.UnitId == SelectedId)
	{
		WayPoints.clear();
	}
	const TMSim::FUnit* Unit = SelectedUnit();
	if (!Unit || Order.UnitId != Unit->Id)
	{
		return true;
	}
	if (!Unit->IsAlive() || !Unit->bReady || Battle.Winner != -1)
	{
		// The turn is over: the next ready unit is taken up, and the camera may
		// follow to it, by the camera rules (RequestFollow, 2026-10-03).
		Deselect();
		AutoSelect();
		if (Battle.Winner == -1 && SelectedId != -1)
		{
			RequestFollow(SelectedId);
		}
		bSnapToNext = false;
		FollowArmedUntil = 0.0;
		return true;
	}
	// Acted, and walked or started a cast: nothing is left to do with the turn,
	// so it is ended rather than left to run out.
	if (Unit->bActed && (Unit->bMoved || Unit->IsCasting()))
	{
		OrderSelected(TMSim::FOrder::MakeEndTurn(Unit->Id, Unit->Serial));
		return true;
	}
	// Walked, and no ability it could use now (Options, 2026-10-03): ended too.
	// Not for a unit on a Go To, which hands the turn back on purpose.
	// Nor for a plan's walk (its ability follows), nor beside a cache or a tower it could take.
	if (FTMSettings::Get().bAutoEndTurn && Order.Type == TMSim::EOrderType::Move && Unit->bMoved && !Unit->bActed
		&& !Unit->IsCasting() && !bGoToOrdering && PendingAbility.UnitId != Unit->Id && !Plans.Contains(Unit->Id)
		&& Battle.CacheNear(Unit->Pos) < 0 && CapturableTower(*Unit, nullptr) < 0)
	{
		bool bAnyUsable = false;
		for (int32 Slot = 0; Slot < TMSim::AbilitySlots && !bAnyUsable; ++Slot)
		{
			bAnyUsable = Unit->Ability(Slot) != nullptr && Battle.AbilityBlockedReason(*Unit, Slot).empty();
		}
		if (!bAnyUsable)
		{
			Tell(FString::Printf(TEXT("%s has nothing left to use: turn ended."), *LogName(Unit->Id)));
			OrderSelected(TMSim::FOrder::MakeEndTurn(Unit->Id, Unit->Serial));
			return true;
		}
	}
	AimMode = EAimMode::None;
	AimSlot = -1;
	bSprinting = false;
	if (!Unit->bMoved)
	{
		EnterMoveMode(false);
	}
	return true;
}

void ATMBattleDirector::MaintainSelection()
{
	// Time runs while a person thinks, so the unit they were ordering can time
	// out, be knocked down or be stunned under them (battle.gd:468-471, 484-485).
	const TMSim::FUnit* Unit = SelectedUnit();
	// Planning (TMBattleDirectorPlans.cpp): a waiting unit stays selected while
	// it is planned. A plan made inside a turn that has ended goes with it.
	if (Unit && bPlanMode && !Unit->bReady)
	{
		const FTMPlan* Plan = PlanOf(Unit->Id);
		if (!PlayerCanPlan(Unit) || (Plan && Plan->bThisTurn))
		{
			if (Plan && Plan->bThisTurn)
			{
				Plans.Remove(Unit->Id);
			}
			Deselect();
			Unit = nullptr;
		}
	}
	else if (Unit && bPlanMode && Unit->Serial != SelectedSerial)
	{
		// Its turn began while it was being planned: the plan runs (RunDuePlans),
		// or, with none, the turn is the player's to give.
		SelectUnit(Unit->Id);
		Unit = SelectedUnit();
	}
	else if (Unit && (!Unit->IsAlive() || !Unit->bReady || ComputerPlaysUnit(*Unit) || Battle.Winner != -1))
	{
		// Its turn used (or lost): the next one ready may get the camera, if it is
		// ready at once (2026-10-03, camera rules C: within 1.5 s, not whenever).
		bSnapToNext = Battle.Winner == -1;
		FollowArmedUntil = bSnapToNext ? FPlatformTime::Seconds() + 1.5 : 0.0;
		Deselect();
		Unit = nullptr;
	}
	else if (Unit && Unit->Serial != SelectedSerial)
	{
		// Its turn ended and a new one began (Relentless): start that one afresh.
		SelectUnit(Unit->Id);
		Unit = SelectedUnit();
	}
	// A turn of the player's began while they are busy with another unit --
	// ordering one, or planning a waiting one. Until 2026-10-03 it took the
	// selection and the camera; now it is pointed out (a word at the top and an
	// arrow at the edge of the screen, PointOut) and N or a click goes to it.
	// Noticed once per turn; a unit with a plan or a Go To of its own is left to it.
	int32 NewReady = -1;
	for (const TMSim::FUnit& Each : Battle.Units)
	{
		if (PlayerCanOrder(&Each))
		{
			const int64 Key = (static_cast<int64>(Each.Id) << 32) | static_cast<uint32>(Each.Serial);
			if (!ReadySeen.Contains(Key))
			{
				ReadySeen.Add(Key);
				NewReady = Plans.Contains(Each.Id) || GoTos.Contains(Each.Id) ? NewReady : Each.Id;
			}
		}
	}
	if (NewReady >= 0 && Unit && Unit->Id != NewReady)
	{
		PointOut(NewReady);
	}
	if (SelectedId == -1 && !bPaused)
	{
		AutoSelect();
		Unit = SelectedUnit();
		if (Unit)
		{
			// Right after one of yours ended its turn: the camera may follow (by
			// the rules, RequestFollow). Ready later, it is taken up but the
			// camera stays; off screen, it is pointed out.
			if (bSnapToNext && FPlatformTime::Seconds() <= FollowArmedUntil)
			{
				RequestFollow(Unit->Id);
			}
			else if (!OnScreenNow(*Unit))
			{
				PointOut(Unit->Id);
			}
			bSnapToNext = false;
			FollowArmedUntil = 0.0;
		}
	}
	// Somebody else moved or fell, so the ground this unit can reach has changed
	// (battle.gd:482-483).
	if (OrdersSeen != OrdersApplied)
	{
		OrdersSeen = OrdersApplied;
		if (Unit && AimMode == EAimMode::Move)
		{
			Reachable = Battle.ReachableVia(*Unit, WayPoints, bSprinting);
			RefreshZoneShadow();
			PathNode = TMSim::FNode{ -9999, -9999 };
		}
	}
}

void ATMBattleDirector::PickUnderCursor()
{
	// battle.gd:743-762, _pick.
	bHaveHover = false;
	HoverUnitId = -1;
	UWorld* World = GetWorld();
	APlayerController* Player = World ? World->GetFirstPlayerController() : nullptr;
	float MouseX = 0.0f;
	float MouseY = 0.0f;
	if (!Player || !CursorPosition(MouseX, MouseY))
	{
		return;
	}
	const FVector2D Mouse(MouseX, MouseY);

	// Units by where they appear on screen. The camera looks down at an angle, so
	// a ray through a unit's chest meets the board well behind its feet; asking
	// the screen is what makes clicking a unit land on that unit.
	//
	// v20 play test ("when units are close together it's hard to select
	// targets"): each standing unit is measured by how far the pointer is from
	// its whole body as drawn (feet to head), not from one point at its chest
	// (which, with units half as big again since v19, sat at their knees); a
	// pointer on two bodies goes to the one whose middle it is nearer; and while
	// an ability is aimed, a unit it can't be used on counts as further off, so
	// the one it can is chosen when they stand together.
	int32 Best = -1;
	double BestPixels = 40.0;
	// Whether the pointer is on the nearest unit's body itself, not only near it.
	bool bOnBody = false;
	const TMSim::FUnit* Aiming = AimMode == EAimMode::Ability ? SelectedUnit() : nullptr;
	const TMSim::FAbility* AimedAbility = Aiming ? Aiming->Ability(AimSlot) : nullptr;
	for (const TMSim::FUnit& Unit : Battle.Units)
	{
		if ((!Unit.IsAlive() && !Unit.IsKo()) || !IsSeen(Unit))
		{
			continue;
		}
		const FVector Drawn = ShownAt(Unit);
		double Pixels = 0.0;
		bool bOnThisBody = false;
		FVector2D Feet;
		FVector2D Head;
		if (Unit.IsAlive()
			&& Player->ProjectWorldLocationToScreen(GetActorTransform().TransformPosition(Drawn), Feet)
			&& Player->ProjectWorldLocationToScreen(GetActorTransform().TransformPosition(Drawn + FVector(0.0f, 0.0f, UnitHeadCm)), Head))
		{
			// The body as the screen shows it: feet to head, about a quarter as
			// wide as it is tall. Godot picks a unit by a ray meeting its body
			// (battle.gd:741-753), and this is that body, drawn flat.
			const FVector2D Along = Head - Feet;
			const double Length = FMath::Max(1.0, static_cast<double>(Along.Size()));
			const double T = FMath::Clamp(FVector2D::DotProduct(Mouse - Feet, Along) / (Length * Length), 0.0, 1.0);
			const double Across = FVector2D::Distance(Mouse, Feet + Along * T);
			bOnThisBody = Across <= Length * 0.25;
			// Off the body: how far from it. On it: how far from its middle line,
			// scaled down, so of two overlapping bodies the nearer middle wins.
			Pixels = bOnThisBody ? Across * 0.5 : Across;
		}
		else
		{
			// A fallen unit is lying down, and only its plate is shown.
			FVector2D OnScreen;
			if (!Player->ProjectWorldLocationToScreen(GetActorTransform().TransformPosition(Drawn + FVector(0.0f, 0.0f, 20.0f)), OnScreen))
			{
				continue;
			}
			Pixels = FVector2D::Distance(OnScreen, Mouse);
			bOnThisBody = !Unit.IsAlive() && Pixels <= 18.0;
		}
		if (AimedAbility && !CanAimAt(*Aiming, *AimedAbility, Unit))
		{
			Pixels += 60.0;
		}
		if (Pixels < BestPixels)
		{
			BestPixels = Pixels;
			Best = Unit.Id;
			bOnBody = bOnThisBody;
		}
	}

	// The board under the pointer, in the rules' metres.
	bool bGround = false;
	TMSim::FVec2 Ground;
	FHitResult Hit;
	if (Player->GetHitResultAtScreenPosition(Mouse, ECC_Visibility, false, Hit))
	{
		const FVector Local = GetActorTransform().InverseTransformPosition(Hit.ImpactPoint);
		const TMSim::FVec2 Point(static_cast<float>(Local.X / TileSize), static_cast<float>(Local.Y / TileSize));
		if (Battle.InBounds(Point))
		{
			bGround = true;
			Ground = Point;
		}
	}

	// A unit is pointed at when the pointer is on its body, as Godot's ray stops
	// at the body (battle.gd:741-753); otherwise it is the ground there, and a
	// unit standing on that spot (below). Nearness on screen alone is not
	// enough: it read the ground behind a body, so a click to pick up another
	// unit walked the selected one there, and it snapped a blast aimed between
	// two enemies onto whichever stood nearer (both found by the robot
	// playtester).
	if (Best >= 0 && (!bGround || bOnBody))
	{
		bHaveHover = true;
		HoverUnitId = Best;
		HoverPoint = FindIn(Battle, Best)->Pos;
		TetherAim();
		return;
	}
	if (bGround)
	{
		bHaveHover = true;
		HoverPoint = Ground;
		// Tile movement (v20 play test): a walk, or a unit being placed, aims at the
		// tile's spot, wherever on the tile the pointer is.
		HoverGround = Ground;
		if (Battle.TilesOn() && AimMode != EAimMode::Ability)
		{
			HoverPoint = Battle.TileSpot(Ground);
			// A unit on the tile is pointed at, wherever on its tile the pointer is.
			if (const TMSim::FUnit* OnTile = Battle.UnitNear(HoverPoint, 0.5f))
			{
				if (IsSeen(*OnTile))
				{
					HoverUnitId = OnTile->Id;
				}
			}
		}
		// On the ground, a unit is pointed at only if it stands on that spot, as
		// in Godot (battle.gd:756-758). Merely near it on screen would make a
		// click that places a unit, or walks one, pick up the neighbour instead
		// (found by the robot playtester, placing units while planning).
		if (const TMSim::FUnit* Near = Battle.UnitNear(Ground, 0.5f))
		{
			if (IsSeen(*Near))
			{
				HoverUnitId = Near->Id;
			}
		}
		TetherAim();
	}
}

bool ATMBattleDirector::CanAimAt(const TMSim::FUnit& User, const TMSim::FAbility& Ability, const TMSim::FUnit& Target) const
{
	switch (Ability.Target)
	{
	case TMSim::ETargetSide::Enemy:
		return Target.IsAlive() && Target.Team != User.Team;
	case TMSim::ETargetSide::Ally:
		return Target.IsAlive() && Target.Team == User.Team;
	case TMSim::ETargetSide::KoAlly:
		return Target.IsKo() && Target.Team == User.Team;
	default:
		return true;
	}
}

void ATMBattleDirector::TetherAim()
{
	if (!bHaveHover || AimMode != EAimMode::Ability || Battle.IsPlanning())
	{
		return;
	}
	const TMSim::FUnit* Unit = SelectedUnit();
	if (!PlayerCanCommand(Unit))
	{
		return;
	}
	const FPlanStandIn Stand(*this);
	const TMSim::FAbility* Ability = Unit->Ability(AimSlot);
	if (!Ability || Ability->MaxRange <= 0.0f)
	{
		return;
	}
	const std::string Shape = TMSim::ShapeOf(*Ability);
	if (Shape == "global")
	{
		return;
	}
	// Where it could be used from: where it stands, and -- if it has not walked
	// yet this turn -- anywhere it can still walk to (the walk into range a
	// click out of range orders). A cone only points a way, so it is kept to
	// its reach from the unit itself. Worked out again only when the board
	// may have changed.
	const bool bCanWalk = !Unit->bMoved && !Unit->IsCasting() && Shape != "cone";
	const FString Key = FString::Printf(TEXT("%d/%d/%d/%d/%d/%d/%.2f,%.2f"), Unit->Id, Unit->Serial, AimSlot,
		bCanWalk ? 1 : 0, OrdersApplied, Battle.TickCount, Unit->Pos.X, Unit->Pos.Y);
	if (Key != TetherKey)
	{
		TetherKey = Key;
		TetherFrom.clear();
		TetherFrom.push_back(Unit->Pos);
		if (bCanWalk)
		{
			for (const std::pair<TMSim::FNode, double>& Entry : Battle.ReachableNodes(*Unit))
			{
				TetherFrom.push_back(TMSim::FMap::NodePos(Entry.first));
			}
		}
	}
	// The nearest of those spots to the pointer; the legal area is the ground
	// within range of any of them, so the nearest legal point to the pointer
	// lies toward the nearest one.
	TMSim::FVec2 From = TetherFrom[0];
	float Nearest = HoverPoint.DistanceTo(From);
	for (const TMSim::FVec2& Spot : TetherFrom)
	{
		const float Distance = HoverPoint.DistanceTo(Spot);
		if (Distance < Nearest)
		{
			Nearest = Distance;
			From = Spot;
		}
	}
	if (Nearest <= Ability->MaxRange)
	{
		return;
	}
	// Pulled in by just over half a node's diagonal, so the snapped aim point
	// (Aim snaps to the half-metre grid) is still in range.
	const float Reach = FMath::Max(Ability->MaxRange - 0.36f, 0.0f);
	TMSim::FVec2 Tethered = From + (HoverPoint - From).Normalized() * Reach;
	if (!Battle.InBounds(Tethered))
	{
		Tethered = From;
	}
	HoverPoint = Tethered;
	// Whoever was under the pointer is out of reach; whoever stands on the
	// tethered spot is aimed at instead.
	HoverUnitId = -1;
	if (const TMSim::FUnit* Near = Battle.UnitNear(Tethered, 0.5f))
	{
		if (IsSeen(*Near))
		{
			HoverUnitId = Near->Id;
		}
	}
}

ATMBattleDirector::FAim ATMBattleDirector::Aim()
{
	// battle.gd:810-833, _aim. Pointing at a unit aims at that unit, and a cast
	// follows it; pointing at the ground aims at that spot. Planned, from where
	// the planned walk ends (FPlanStandIn).
	const FPlanStandIn Stand(*this);
	FAim Out;
	const TMSim::FUnit* Unit = SelectedUnit();
	const TMSim::FAbility* Ability = Unit ? Unit->Ability(AimSlot) : nullptr;
	if (!Ability)
	{
		return Out;
	}
	if (Ability->MaxRange == 0.0f)
	{
		// Centred on the user: click the user, or anywhere in the area.
		Out.bHave = true;
		Out.Point = Unit->Pos;
		Out.Follow = Unit->Id;
		const float Near = FMath::Max(Ability->Aoe, 1.0f) + 0.5f;
		if (!bHaveHover || HoverPoint.DistanceTo(Unit->Pos) > Near)
		{
			Out.Why = FString::Printf(TEXT("Click on %hs to use %hs."), Unit->Job.c_str(), Ability->Name.c_str());
			return Out;
		}
	}
	else
	{
		if (!bHaveHover)
		{
			return Out;
		}
		Out.bHave = true;
		Out.Point = TMSim::FMap::Snap(HoverPoint);
		const TMSim::FUnit* Target = FindIn(Battle, HoverUnitId);
		if (Target && FitsTarget(*Unit, *Ability, *Target))
		{
			Out.Point = Target->Pos;
			Out.Follow = Target->Id;
		}
		else if (Ability->Target == TMSim::ETargetSide::KoAlly)
		{
			Out.Why = TEXT("Pick a knocked-out ally.");
			return Out;
		}
	}
	// Range, sight and line of sight are the rules' to judge, and Godot's own
	// checks here (:827-832) are the same ones ValidateAbility makes. Asking the
	// rules means the reason shown is exactly the reason an order would get.
	const std::string Refused = Battle.ValidateAbility(Unit->Id, AimSlot, Out.Point, Out.Follow);
	Out.bOk = Refused.empty();
	Out.Why = UTF8_TO_TCHAR(Refused.c_str());
	return Out;
}

void ATMBattleDirector::OnClick()
{
	// battle.gd:765-803, _on_click. A click on the HUD is the HUD's, not the board's.
	UWorld* World = GetWorld();
	APlayerController* Player = World ? World->GetFirstPlayerController() : nullptr;
	float MouseX = 0.0f;
	float MouseY = 0.0f;
	if (Player && CursorPosition(MouseX, MouseY))
	{
		FTMHudButton Button;
		const ATMBattleHud* Hud = Cast<ATMBattleHud>(Player->GetHUD());
		if (Hud && Hud->ButtonAt(FVector2D(MouseX, MouseY), Button))
		{
			UE_LOG(LogTemp, Log, TEXT("CLICK %.0f,%.0f on action %d value %d"), MouseX, MouseY, static_cast<int32>(Button.Action), Button.Value);
			PressHudButton(Button);
			return;
		}
		// A click that found no button, and what was nearest: menus that seem not
		// to answer are found this way.
		UE_LOG(LogTemp, Log, TEXT("CLICK %.0f,%.0f on no button: %s"), MouseX, MouseY, Hud ? *Hud->DescribeMiss(FVector2D(MouseX, MouseY)) : TEXT("no HUD"));
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("CLICK with no pointer position"));
	}
	// Behind a menu or the guide, or while the screen is being arranged, the
	// board takes no clicks.
	if (Screen != EScreen::Battle || bMenuOpen || bGuideOpen || bEditingLayout)
	{
		return;
	}
	// Online, with the last order still out with the host: kept, and clicked
	// again the moment the answer comes (TMBattleDirectorFeel.cpp).
	if (!Battle.IsPlanning() && BufferWhileWaiting(EKeys::LeftMouseButton, true))
	{
		return;
	}
	PickUnderCursor();
	if (!bHaveHover)
	{
		return;
	}
	// Where a click is taken, a ring and a tick (TMBattleDirectorFeel.cpp): gold
	// for a walk, orange for a Go To, violet for an ability, red refused.
	const FLinearColor WalkGold(0.95f, 0.78f, 0.35f);
	const FLinearColor GoToOrange(1.0f, 0.6f, 0.2f);
	const FLinearColor RefusedRed(1.0f, 0.3f, 0.25f);
	// While planning a click puts the picked-up unit down instead of ordering
	// it; a click on another of this side's units picks that one up instead
	// (battle.gd:771-781).
	if (Battle.IsPlanning())
	{
		const int32 Team = PlanningTeam();
		if (Team == -1 || Battle.PlanningDone[Team])
		{
			return;
		}
		const TMSim::FUnit* Clicked = FindIn(Battle, HoverUnitId);
		if (Clicked && Clicked->Team == Team)
		{
			PlaceId = Clicked->Id;
			return;
		}
		if (const TMSim::FUnit* Placing = FindIn(Battle, PlaceId))
		{
			const FString Refused = Submit(TMSim::FOrder::MakePlace(Placing->Id, Placing->Serial, TMSim::FMap::Snap(HoverPoint)));
			if (!Refused.IsEmpty())
			{
				Tell(Refused);
			}
		}
		return;
	}
	const TMSim::FUnit* Unit = SelectedUnit();
	const bool bPlanning = IsPlanningSelected();
	if (PlayerCanCommand(Unit))
	{
		if (AimMode == EAimMode::Move)
		{
			const TMSim::FNode Node = TMSim::FMap::NodeOf(HoverPoint);
			const bool bReachable = std::any_of(Reachable.begin(), Reachable.end(),
				[&Node](const std::pair<TMSim::FNode, double>& Entry) { return Entry.first == Node; });
			if (bReachable && Node != TMSim::FMap::NodeOf(WalkStart(*Unit))
				&& !(Battle.TilesOn() && Node == TMSim::FBattle::TileNode(WalkStart(*Unit))))
			{
				// Held: a waypoint on the way (2026-10-01). Planning: the plan's
				// walk. Otherwise the walk, now, by any waypoints set.
				const TMSim::FVec2 To = TMSim::FMap::NodePos(Node);
				if (WayPointHeld())
				{
					AddWayPoint(To);
					Acknowledge(To, WalkGold);
				}
				else
				{
					// Pressed here; walked when let go, facing the way it was dragged
					// meanwhile (2026-10-03, ReleaseWalk in TMBattleDirectorFeel.cpp).
					WalkPress = FTMWalkPress();
					WalkPress.bActive = true;
					WalkPress.To = To;
					WalkPress.Mouse = FVector2D(MouseX, MouseY);
					WalkPress.UnitId = Unit->Id;
					WalkPress.Serial = Unit->Serial;
					WalkPress.bPlanning = bPlanning;
				}
				return;
			}
			// Past the walk area, on open ground: a Go To, there over as many
			// turns as it takes (2026-10-01, TMBattleDirectorPlans.cpp).
			if (!bReachable && HoverUnitId < 0 && !WayPointHeld() && Battle.Map.NodeLevel(Node) > 0)
			{
				const bool bSet = SetGoTo(Unit->Id, TMSim::FMap::NodePos(Node));
				Acknowledge(TMSim::FMap::NodePos(Node), bSet ? GoToOrange : RefusedRed, bSet);
				return;
			}
		}
		else if (AimMode == EAimMode::None && bHaveHover && Unit->bMoved && Unit->bReady && !bPlanning && HoverUnitId < 0 && Battle.Map.NodeLevel(TMSim::FMap::NodeOf(HoverPoint)) > 0)
		{
			// Clicked on open ground with its walk spent (2026-10-03): heard, and said.
			Deny(TEXT("noMove"), FString::Printf(TEXT("%s has no movement left this turn."), *LogName(Unit->Id)));
			Acknowledge(HoverPoint, RefusedRed, false);
			return;
		}
		else if (AimMode == EAimMode::Ability)
		{
			ClickAbility(*Unit, bPlanning);
			return;
		}
	}
	// Anyone else of this machine's who is ready: take them up instead. Anyone
	// else at all: open or close its card. Nobody: close it (battle.gd:796-803).
	const TMSim::FUnit* Clicked = FindIn(Battle, HoverUnitId);
	if (Clicked && Clicked != Unit && PlayerCanOrder(Clicked))
	{
		SelectUnit(Clicked->Id);
	}
	else if (Clicked && Clicked != Unit && !Clicked->bReady && PlayerCanPlan(Clicked))
	{
		// One of this machine's, waiting for its turn: plan it (2026-10-01).
		StartPlanning(Clicked->Id);
	}
	else if (Clicked && Clicked != Unit && IsSeen(*Clicked))
	{
		InspectedId = InspectedId == Clicked->Id ? -1 : Clicked->Id;
	}
	else if (!Clicked)
	{
		InspectedId = -1;
	}
}

void ATMBattleDirector::Deny(const TCHAR* Event, const FString& Why)
{
	// One a quarter-second at most: a held key or quick clicks are one refusal.
	const float Now = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0f;
	if (Now - LastDenyAt >= 0.25f)
	{
		LastDenyAt = Now;
		PlayEventSound(Event, nullptr, 0.8f);
	}
	Tell(Why);
}

void ATMBattleDirector::Tell(const FString& What)
{
	Notice = What;
	NoticeLeft = 3.0f;
	UE_LOG(LogTemp, Log, TEXT("player: %s"), *What);
}

int32 ATMBattleDirector::CapturableTower(const TMSim::FUnit& Unit, FString* WhyNot) const
{
	// The nearest tower in reach first: that is the one a person standing
	// between two means. If it cannot be taken, the rules say why.
	const int32 Near = Battle.TowerNear(Unit.Pos);
	if (Near < 0)
	{
		if (WhyNot)
		{
			*WhyNot = Battle.Watchtowers.empty() ? FString(TEXT("There are no watchtowers in this battle."))
				: FString(TEXT("Stand next to a watchtower to take it."));
		}
		return -1;
	}
	const std::string Refused = Battle.ValidateCapture(Unit.Id, Near);
	if (!Refused.empty())
	{
		if (WhyNot)
		{
			*WhyNot = UTF8_TO_TCHAR(Refused.c_str());
		}
		return -1;
	}
	return Near;
}

void ATMBattleDirector::CaptureTower()
{
	const TMSim::FUnit* Unit = SelectedUnit();
	if (!PlayerCanOrder(Unit))
	{
		return;
	}
	FString WhyNot;
	const int32 Tower = CapturableTower(*Unit, &WhyNot);
	if (Tower < 0)
	{
		Tell(WhyNot);
		return;
	}
	// Through the same door as any order: checked, sent online, recorded.
	OrderSelected(TMSim::FOrder::MakeCapture(Unit->Id, Unit->Serial, Tower));
}

FVector ATMBattleDirector::BoardPoint(const TMSim::FVec2& Point, float Lift) const
{
	const int Level = Battle.Map.NodeLevel(TMSim::FMap::NodeOf(Point));
	return GetActorTransform().TransformPosition(WorldFromMetres(Point, Level) + FVector(0.0f, 0.0f, Lift));
}

bool ATMBattleDirector::ClosestSpotInRange(const TMSim::FUnit& Unit, int32 Slot, const TMSim::FVec2& Point,
	TMSim::FVec2& OutSpot, double& OutWalk)
{
	// battle.gd:860-874, _closest_spot_in_range: of everywhere it can walk this
	// turn, the spot it could use the ability from -- in range, and in sight of
	// the target where the ability needs that -- that is the least walk away.
	// Godot runs through its reachable nodes in the order its search reached
	// them; this runs through them in the order ReachableNodes gives, so between
	// two spots exactly as far to walk it may pick the other. Either is a walk
	// the person asked for, and nothing here is part of the rules.
	const TMSim::FAbility* Ability = Unit.Ability(Slot);
	if (!Ability)
	{
		return false;
	}
	bool bFound = false;
	OutWalk = TNumericLimits<double>::Max();
	for (const std::pair<TMSim::FNode, double>& Entry : Battle.ReachableNodes(Unit))
	{
		const TMSim::FVec2 Spot = TMSim::FMap::NodePos(Entry.first);
		if (!Battle.InAbilityRange(Unit, Slot, Spot, Point))
		{
			continue;
		}
		// A zone that sees (2026-10-04) is thrown: it needs no line.
		const bool bLobbed = Ability->LaysZone() && Ability->ZoneSight > 0.0f;
		if (!bLobbed && TMSim::NeedsLineOfSight(*Ability) && !Battle.HasLineOfSight(Spot, Point))
		{
			continue;
		}
		if (Entry.second < OutWalk)
		{
			OutWalk = Entry.second;
			OutSpot = Spot;
			bFound = true;
		}
	}
	return bFound;
}

bool ATMBattleDirector::WalkIntoRange(const TMSim::FUnit& Unit, const TMSim::FVec2& Point, int32 Follow)
{
	// battle.gd:838-857. The unit is on its way while time runs on. Godot aims
	// at the spot the target was standing on, so if it has moved by then the
	// blow lands on empty ground; here ("Attack Out of Range Mockups" A,
	// 2026-10-06) a unit clicked on is followed, and aimed at where it stands
	// when the walk ends (FirePendingAbility).
	if (Unit.bMoved || Unit.IsCasting())
	{
		return false;
	}
	// Held on to before the walk is ordered: ordering picks the unit's next
	// step, which clears the ability that was chosen.
	const int32 Slot = AimSlot;
	TMSim::FVec2 Spot;
	double Walk = 0.0;
	if (!ClosestSpotInRange(Unit, Slot, Point, Spot, Walk))
	{
		return false;
	}
	const int32 UnitId = Unit.Id;
	const int32 Serial = Unit.Serial;
	const FString Refused = Submit(TMSim::FOrder::MakeMove(UnitId, Serial, Spot));
	if (!Refused.IsEmpty())
	{
		Tell(Refused);
		return true;
	}
	PendingAbility.UnitId = UnitId;
	PendingAbility.Serial = Serial;
	PendingAbility.Slot = Slot;
	PendingAbility.Target = Point;
	PendingAbility.Follow = Follow;
	PendingAbility.bAfterWalk = true;
	AimMode = EAimMode::None;
	AimSlot = -1;
	Reachable.clear();
	PathShown.clear();
	return true;
}

void ATMBattleDirector::FirePendingAbility()
{
	// battle.gd:877-892. Once the walk has ended on screen -- Godot waits the
	// walk's own time -- the same ability, aimed where the target stood.
	if (PendingAbility.UnitId < 0)
	{
		return;
	}
	int32 Index = INDEX_NONE;
	for (int32 i = 0; i < static_cast<int32>(Battle.Units.size()); ++i)
	{
		Index = Battle.Units[i].Id == PendingAbility.UnitId ? i : Index;
	}
	const TMSim::FUnit* Unit = Index != INDEX_NONE ? &Battle.Units[Index] : nullptr;
	if (!Unit || !Unit->IsAlive() || Unit->Serial != PendingAbility.Serial)
	{
		PendingAbility = FPendingAbility();  // its turn ended on the way over
		return;
	}
	if (Motions.IsValidIndex(Index) && (Motions[Index].Path.Num() > 0 || Motions[Index].Queued))
	{
		return;  // still walking
	}
	// The walk not applied yet (online, still with the host), or the host not
	// done with the last order: it waits.
	if (bWaitingForHost || (PendingAbility.bAfterWalk && !Unit->bMoved))
	{
		return;
	}
	const FPendingAbility Order = PendingAbility;
	PendingAbility = FPendingAbility();
	// Aimed at a unit (a click on it, a plan's): at where it stands now.
	TMSim::FVec2 Target = Order.Target;
	const TMSim::FUnit* Followed = Order.Follow >= 0 ? FindIn(Battle, Order.Follow) : nullptr;
	if (Followed)
	{
		Target = Followed->Pos;
	}
	const TMSim::FOrder Use = TMSim::FOrder::MakeUseAbility(Order.UnitId, Order.Serial, Order.Slot, Target, Order.Follow);
	// "Attack Out of Range Mockups" B (2026-10-06): checked on arrival. If the
	// target has stepped out of reach (or died, or gone into the fog), no blow
	// is thrown at empty ground: the order is dropped, the action is not spent,
	// and the unit's turn goes on for the player to choose again.
	const std::string Refused = Battle.Validate(Use);
	if (!Refused.empty())
	{
		const TMSim::FAbility* Ability = Unit->Ability(Order.Slot);
		FString Why = UTF8_TO_TCHAR(Refused.c_str());
		Why.RemoveFromEnd(TEXT("."));
		if (Refused == OutOfRange && Followed)
		{
			Why = FString::Printf(TEXT("%s moved out of reach"), *NameOf(Followed->Id));
		}
		Tell(FString::Printf(TEXT("%hs held: %s. Action kept."), Ability ? Ability->Name.c_str() : "Ability", *Why));
		return;
	}
	OrderSelected(Use);
	// Not the selected unit (a plan run while the player orders another): its
	// turn is ended here, as OrderSelected does for the selected one.
	const TMSim::FUnit* After = FindIn(Battle, Order.UnitId);
	if (After && After->Id != SelectedId && After->IsAlive() && After->bReady && After->Serial == Order.Serial
		&& After->bActed && (After->bMoved || After->IsCasting()) && PlayerCanOrder(After))
	{
		Submit(TMSim::FOrder::MakeEndTurn(After->Id, After->Serial));
	}
}

void ATMBattleDirector::UpdateHoverPath()
{
	// The HUD draws the way there; it is worked out here, and only when the spot
	// under the pointer changes, because it is a search over the whole grid.
	// While a walk is pressed and dragged for its facing, the way stays put.
	if (WalkPress.bActive)
	{
		return;
	}
	const TMSim::FUnit* Unit = SelectedUnit();
	const FTMPlan* Planned = Unit && IsPlanningSelected() ? PlanOf(Unit->Id) : nullptr;
	if (PlayerCanCommand(Unit) && AimMode == EAimMode::Ability && bHaveHover && !(Planned && Planned->bWalk))
	{
		// Out of range, but usable from somewhere it can walk: show that walk
		// (battle.gd:1438-1447). Beyond this turn's reach (2026-10-03): the way
		// into range, turn by turn, as a Go To shows it.
		const TMSim::FNode Node = TMSim::FMap::NodeOf(HoverPoint);
		if (Node == PathNode)
		{
			return;
		}
		PathNode = Node;
		PathShown.clear();
		GoToHoverStops.clear();
		GoToHoverMetres = 0.0;
		bAbilityHoverGoTo = false;
		const FAim Where = Aim();
		TMSim::FVec2 Spot;
		double Walk = 0.0;
		if (!Where.bOk && Where.Why == UTF8_TO_TCHAR(OutOfRange))
		{
			if (!Unit->bMoved && !Unit->IsCasting() && ClosestSpotInRange(*Unit, AimSlot, Where.Point, Spot, Walk))
			{
				PathShown = Battle.PathTo(*Unit, TMSim::FMap::NodeOf(Spot), false);
			}
			else
			{
				PathShown = ApproachRoute(*Unit, AimSlot, Where.Point, &GoToHoverMetres);
				GoToHoverStops = SplitRoute(*Unit, PathShown);
				bAbilityHoverGoTo = !GoToHoverStops.empty();
			}
		}
		return;
	}
	bAbilityHoverGoTo = false;
	if (!PlayerCanCommand(Unit) || AimMode != EAimMode::Move || !bHaveHover)
	{
		GoToHoverStops.clear();
		PathShown.clear();
		PathNode = TMSim::FNode{ -9999, -9999 };
		ZoneStopBy = -1;
		ZoneGhost.clear();
		TankLanes.clear();
		TankLanesFor = -1;
		return;
	}
	const TMSim::FNode Node = TMSim::FMap::NodeOf(HoverPoint);
	if (Node == PathNode)
	{
		return;
	}
	PathNode = Node;
	PathShown.clear();
	ZoneStopBy = -1;
	ZoneGhost.clear();
	TankLanes.clear();
	TankLanesFor = -1;
	const bool bReachable = std::any_of(Reachable.begin(), Reachable.end(),
		[&Node](const std::pair<TMSim::FNode, double>& Entry) { return Entry.first == Node; });
	GoToHoverStops.clear();
	GoToHoverMetres = 0.0;
	if (bReachable)
	{
		PathShown = Battle.PathVia(*Unit, WayPoints, Node, bSprinting);
		// A tank's walk ("Zone of Control Mockups" D): which ways to the back line
		// it would cut standing there, against the enemies this side can see.
		if (Battle.Tuning.ZoneOfControl >= 1.0 && TMSim::FBattle::HoldsTheLine(*Unit))
		{
			std::vector<int> Enemies;
			std::vector<int> BackLine;
			for (const TMSim::FUnit& Each : Battle.Units)
			{
				if (!Each.IsAlive())
				{
					continue;
				}
				if (Each.Team != Unit->Team && IsSeen(Each))
				{
					Enemies.push_back(Each.Id);
				}
				else if (Each.Team == Unit->Team && Each.Id != Unit->Id && !TMSim::FBattle::HoldsTheLine(Each))
				{
					BackLine.push_back(Each.Id);
				}
			}
			if (!Enemies.empty() && !BackLine.empty())
			{
				TankLanes = Battle.TankLanes(Unit->Id, TMSim::FMap::NodePos(Node), Enemies, BackLine);
				TankLanesFor = Unit->Id;
			}
		}
	}
	else if (std::any_of(ZoneShadow.begin(), ZoneShadow.end(), [&Node](const TMSim::FNode& Each) { return Each == Node; }))
	{
		// Ground a tank's zone takes away ("Zone of Control Mockups" B): where a
		// walk that way would end, and which tank ends it. A click still makes the
		// Go To, which goes round.
		const std::vector<TMSim::FVec2> Ghost = Battle.PathIgnoringZones(*Unit, WayPoints, Node, bSprinting);
		const TMSim::FVec2 Start = WalkStart(*Unit);
		for (size_t i = 1; i < Ghost.size() && ZoneStopBy < 0; ++i)
		{
			for (const TMSim::FUnit& Tank : Battle.Units)
			{
				if (Tank.IsAlive() && Tank.Team != Unit->Team && TMSim::FBattle::HoldsTheLine(Tank)
					&& static_cast<double>(Start.DistanceTo(Tank.Pos)) >= Battle.Tuning.EngageRadius
					&& static_cast<double>(Ghost[i].DistanceTo(Tank.Pos)) < Battle.Tuning.EngageRadius)
				{
					ZoneStopBy = Tank.Id;
					ZoneStopAt = Ghost[i];
					ZoneGhost.assign(Ghost.begin() + static_cast<std::ptrdiff_t>(i), Ghost.end());
					break;
				}
			}
		}
		if (HoverUnitId < 0 && Battle.Map.NodeLevel(Node) > 0)
		{
			PathShown = Battle.RouteTo(*Unit, WayPoints, Node, &GoToHoverMetres);
			GoToHoverStops = SplitRoute(*Unit, PathShown);
		}
	}
	else if (HoverUnitId < 0 && Battle.Map.NodeLevel(Node) > 0)
	{
		// Past the walk area: the Go To a click would give, turn by turn.
		PathShown = Battle.RouteTo(*Unit, WayPoints, Node, &GoToHoverMetres);
		GoToHoverStops = SplitRoute(*Unit, PathShown);
	}
}

void ATMBattleDirector::ShowHud()
{
	UWorld* World = GetWorld();
	if (APlayerController* Player = World ? World->GetFirstPlayerController() : nullptr)
	{
		// Put up from here rather than by a game mode, so the level needs no
		// game mode asset made in the editor for it.
		Player->ClientSetHUD(ATMBattleHud::StaticClass());
	}
}

void ATMBattleDirector::PressHudButton(const FTMHudButton& Button)
{
	// Every button clicks (audio.gd:126-128).
	PlayEventSound(TEXT("click"), nullptr, 0.5f);
	// The HUD's buttons do what their keys do (hud.gd's action bar calls the same
	// functions the key bindings do).
	const TMSim::FUnit* Unit = SelectedUnit();
	switch (Button.Action)
	{
	case ETMHudAction::Move:
		OnKey(EKeys::SpaceBar);
		break;
	case ETMHudAction::Sprint:
		OnKey(EKeys::LeftShift);
		break;
	case ETMHudAction::Ability:
		SelectAbility(Button.Value);
		break;
	case ETMHudAction::EndTurn:
		if (PlayerCanOrder(Unit))
		{
			OrderSelected(TMSim::FOrder::MakeEndTurn(Unit->Id, Unit->Serial));
		}
		break;
	case ETMHudAction::Capture:
		CaptureTower();
		break;
	// Queued orders (TMBattleDirectorPlans.cpp): the plan strip's buttons.
	case ETMHudAction::PlanGo:
		PlanKey();
		break;
	// A unit's whole queue, from its plan strip, its card on the squad strip or the x at its walk's end.
	case ETMHudAction::QueueCancel:
		if (Button.Value >= 0)
		{
			CancelQueue(Button.Value, true);
		}
		else if (Unit)
		{
			CancelQueue(Unit->Id, true);
		}
		break;
	// Go To's strip: walk and end (0) or walk and wait (1), and cancel.
	case ETMHudAction::GoToMode:
		if (Unit)
		{
			if (FTMGoTo* Order = GoTos.Find(Unit->Id))
			{
				Order->bWaitForMe = Button.Value == 1;
			}
		}
		break;
	case ETMHudAction::GoToCancel:
		if (Unit)
		{
			CancelGoTo(Unit->Id, true);
		}
		break;
	case ETMHudAction::Take:
		TakeItem(Button.Value);
		break;
	case ETMHudAction::TakeOpen:
		// The team items screen: every unit of the side, what it wears, and the stash.
		bTeamItemsOpen = !bTeamItemsOpen;
		StashPick = -1;
		break;
	case ETMHudAction::Drop:
		DropItem(Button.Value);
		break;
	case ETMHudAction::StashPick:
		StashPick = Button.Value < 0 || StashPick == Button.Value ? -1 : Button.Value;
		break;
	case ETMHudAction::EquipSlot:
		EquipItem(Button.Value);
		break;
	case ETMHudAction::ReadyGo:
	{
		// The "is ready" note's Go: that unit, and the camera to it, as N does.
		const TMSim::FUnit* Ready = FindIn(Battle, Button.Value);
		ReadyToastId = -1;
		if (Ready && PlayerCanOrder(Ready))
		{
			if (bPlanMode)
			{
				StopPlanning();
			}
			if (Ready->Id != SelectedId)
			{
				SelectUnit(Ready->Id);
			}
			CenterCamera();
		}
		break;
	}
	case ETMHudAction::PickUnit:
	{
		// A chip on the turn order: take that unit up if it can be ordered
		// (battle.gd:902-910, _on_chip_pressed).
		const TMSim::FUnit* Picked = FindIn(Battle, Button.Value);
		// Twice in quick succession: the camera goes to it (v19 play test). The
		// first click has already done what one click does.
		const double ClickAt = FPlatformTime::Seconds();
		const bool bDouble = Button.Value == LastPickId && ClickAt - LastPickAt < 0.4;
		LastPickId = Button.Value;
		LastPickAt = bDouble ? -1.0 : ClickAt;
		if (bDouble)
		{
			if (Picked && (Picked->IsAlive() || Picked->IsKo()) && IsSeen(*Picked))
			{
				CenterCameraOn(*Picked);
			}
			break;
		}
		if (Picked && PlayerCanOrder(Picked) && Picked->Id != SelectedId)
		{
			SelectUnit(Picked->Id);
		}
		else if (Picked && Picked->Id != SelectedId && !Picked->bReady && PlayerCanPlan(Picked))
		{
			// One of this machine's, waiting: plan its next turn (2026-10-01).
			StartPlanning(Picked->Id);
		}
		else if (Picked && Picked->Id != SelectedId && IsSeen(*Picked))
		{
			// Anyone else: its card, as a click on it on the board would give.
			InspectedId = InspectedId == Picked->Id ? -1 : Picked->Id;
		}
		break;
	}
	case ETMHudAction::NewBattle:
		OnKey(EKeys::R);
		break;
	// The corner buttons (hud.gd:608-623) and the cards.
	case ETMHudAction::ToggleLog:
		bShowLog = !bShowLog;
		LogScroll = 0;
		break;
	case ETMHudAction::LogTab:
		LogTab = FMath::Clamp(Button.Value, 0, 3);
		LogScroll = 0;
		break;
	case ETMHudAction::GrowLog:
		bLogLarge = !bLogLarge;
		break;
	case ETMHudAction::ToggleField:
		bShowField = !bShowField;
		break;
	case ETMHudAction::ToggleGuide:
		ToggleGuide();
		break;
	case ETMHudAction::Pause:
		OnKey(EKeys::P);
		break;
	case ETMHudAction::OpenMenu:
		if (Battle.Winner == -1)
		{
			bMenuOpen = true;
		}
		break;
	case ETMHudAction::CloseCard:
		InspectedId = -1;
		break;
	case ETMHudAction::GuideJob:
		// A class's own page: its hero, its stats, every ability in full.
		GuideJob = Button.Value;
		bGuideDetail = true;
		GuideDetailScroll = 0.0f;
		break;
	case ETMHudAction::GuideBack:
		bGuideDetail = false;
		HideGuideModel();
		break;
	case ETMHudAction::GuideStep:
		GuideStep(Button.Value);
		break;
	case ETMHudAction::GuideRole:
		GuideRole = Button.Value;
		GuideListScroll = 0;
		break;
	case ETMHudAction::GuideScroll:
		GuideListScroll = FMath::Max(0, GuideListScroll + Button.Value * FMath::Max(1, GuideListPage - 1));
		break;
	case ETMHudAction::GuideTurn:
		GuideModelTurn(Button.Value * 45.0f);
		break;
	case ETMHudAction::GuideTab:
		GuideTab = Button.Value;
		GuideListScroll = 0;
		bGuideDetail = false;
		HideGuideModel();
		break;
	case ETMHudAction::GuideAgainst:
		GuideAgainst = (GuideAgainst + 1) % FMath::Max(1, static_cast<int32>(TMSim::AllJobs().size()));
		break;
	default:
		PressMenuButton(Button);
		// Every choice on the setup screen is kept for the next session.
		if (Screen == EScreen::Setup && Setup.Mode != TEXT("online"))
		{
			SaveSetupChoices();
		}
		// The host changed a setting on the lobby screen (v20): everyone told.
		if (Screen == EScreen::Lobby && Net.IsValid() && Net->IsHost())
		{
			BroadcastLobby();
		}
		break;
	}
}

void ATMBattleDirector::SaveSetupChoices()
{
	TMap<FString, FString>& Out = FTMSettings::Get().LastSetup;
	Out.Reset();
	auto Number = [&Out](const TCHAR* Name, double Value) { Out.Add(Name, FString::SanitizeFloat(Value)); };
	for (int32 Team = 0; Team < 2; ++Team)
	{
		TArray<FString> Ids;
		for (int32 i = 0; i < 4; ++i)
		{
			Ids.Add(UTF8_TO_TCHAR(Setup.Rosters[Team][i].c_str()));
		}
		Out.Add(FString::Printf(TEXT("roster%d"), Team), FString::Join(Ids, TEXT(",")));
		Out.Add(FString::Printf(TEXT("difficulty%d"), Team), Setup.Difficulty[Team]);
	}
	Number(TEXT("player_team"), Setup.PlayerTeam);
	Number(TEXT("random_seed"), Setup.bRandomSeed ? 1 : 0);
	Number(TEXT("capture_seconds"), Setup.CaptureSeconds);
	Number(TEXT("battle_seconds"), Setup.BattleSeconds);
	Number(TEXT("planning_seconds"), Setup.PlanningSeconds);
	Number(TEXT("watchtowers"), Setup.Watchtowers);
	Number(TEXT("item_budget"), Setup.ItemBudget);
	Number(TEXT("camps"), Setup.CampLevel);
	Number(TEXT("random_boss"), Setup.bRandomBoss ? 1 : 0);
	Number(TEXT("elements"), Setup.bElements ? 1 : 0);
	Number(TEXT("friendly_fire"), Setup.bFriendlyFire ? 1 : 0);
	Number(TEXT("unique_classes"), Setup.bUniqueClasses ? 1 : 0);
	Number(TEXT("camp_respawn"), Setup.bCampRespawn ? 1 : 0);
	Number(TEXT("boss_hunt"), Setup.bBossHunt ? 1 : 0);
	Number(TEXT("boss_claim"), Setup.bBossClaim ? 1 : 0);
	Number(TEXT("tile_move"), Setup.TileMove);
	Out.Add(TEXT("map"), UTF8_TO_TCHAR(Setup.MapId.c_str()));
	Out.Add(TEXT("theme"), Setup.ThemeId);
	FTMSettings::Get().Save();
}

void ATMBattleDirector::RestoreSetupChoices()
{
	const TMap<FString, FString>& In = FTMSettings::Get().LastSetup;
	if (In.Num() == 0)
	{
		return;
	}
	auto Number = [&In](const TCHAR* Name, double Fallback)
	{
		const FString* Found = In.Find(Name);
		return Found ? FCString::Atod(**Found) : Fallback;
	};
	for (int32 Team = 0; Team < 2; ++Team)
	{
		if (const FString* Roster = In.Find(FString::Printf(TEXT("roster%d"), Team)))
		{
			TArray<FString> Ids;
			Roster->ParseIntoArray(Ids, TEXT(","));
			// Only classes the game still has: one deleted since is left as it was.
			for (int32 i = 0; i < 4 && i < Ids.Num(); ++i)
			{
				const std::string Id = TCHAR_TO_UTF8(*Ids[i]);
				if (TMSim::FindJob(Id))
				{
					Setup.Rosters[Team][i] = Id;
				}
			}
		}
		if (const FString* Level = In.Find(FString::Printf(TEXT("difficulty%d"), Team)))
		{
			if (*Level == TEXT("easy") || *Level == TEXT("medium") || *Level == TEXT("hard"))
			{
				Setup.Difficulty[Team] = *Level;
			}
		}
	}
	// How to play is not restored: the title screen has just chosen it.
	Setup.PlayerTeam = FMath::Clamp(static_cast<int32>(Number(TEXT("player_team"), Setup.PlayerTeam)), 0, 1);
	Setup.bRandomSeed = Number(TEXT("random_seed"), Setup.bRandomSeed ? 1 : 0) != 0.0;
	Setup.CaptureSeconds = FMath::Max(0.0, Number(TEXT("capture_seconds"), Setup.CaptureSeconds));
	Setup.BattleSeconds = FMath::Max(0.0, Number(TEXT("battle_seconds"), Setup.BattleSeconds));
	Setup.PlanningSeconds = FMath::Max(0.0, Number(TEXT("planning_seconds"), Setup.PlanningSeconds));
	Setup.Watchtowers = FMath::Clamp(static_cast<int32>(Number(TEXT("watchtowers"), Setup.Watchtowers)), 0, 8);
	Setup.ItemBudget = FMath::Clamp(static_cast<int32>(Number(TEXT("item_budget"), Setup.ItemBudget)), 0, 20);
	Setup.CampLevel = FMath::Clamp(static_cast<int32>(Number(TEXT("camps"), Setup.CampLevel)), 0, 3);
	Setup.bRandomBoss = Number(TEXT("random_boss"), 0) != 0.0;
	Setup.bElements = Number(TEXT("elements"), 1) != 0.0;
	Setup.bFriendlyFire = Number(TEXT("friendly_fire"), 0) != 0.0;
	Setup.bUniqueClasses = Number(TEXT("unique_classes"), 0) != 0.0;
	Setup.bCampRespawn = Number(TEXT("camp_respawn"), 0) != 0.0;
	Setup.bBossHunt = Number(TEXT("boss_hunt"), 0) != 0.0;
	Setup.bBossClaim = Number(TEXT("boss_claim"), 0) != 0.0;
	Setup.TileMove = FMath::Clamp(static_cast<int32>(Number(TEXT("tile_move"), 0)), 0, 2);
	if (const FString* Map = In.Find(TEXT("map")))
	{
		const std::string Id = TCHAR_TO_UTF8(**Map);
		if (TMSim::HasMap(Id))
		{
			Setup.MapId = Id;
		}
	}
	if (const FString* Theme = In.Find(TEXT("theme")))
	{
		Setup.ThemeId = *Theme;
	}
	// What was chosen stands: the first-time offers do not overwrite it.
	bOfferedTowers = bOfferedItems = bOfferedCamps = bOfferedElements = true;
	bBuilt = false;
}
// ================================================================ match flow

void ATMBattleDirector::StartMatch(bool bNewSeed)
{
	// Who plays each side (game_config.gd:219-231, ai_teams).
	if (Setup.Mode == TEXT("hotseat") || Setup.Mode == TEXT("online"))
	{
		// Online, ComputerPlays answers from LocalTeam instead.
		bComputerPlaysTeam0 = false;
		bComputerPlaysTeam1 = false;
	}
	else if (Setup.Mode == TEXT("cpu"))
	{
		bComputerPlaysTeam0 = true;
		bComputerPlaysTeam1 = true;
	}
	else
	{
		bComputerPlaysTeam0 = Setup.PlayerTeam != 0;
		bComputerPlaysTeam1 = Setup.PlayerTeam != 1;
	}

	// The seed is chosen here, by the view, and then handed to the rules like
	// everything else about the battle: a replay needs only it and the orders.
	// A fresh one is a small number so it can be read off the screen and typed
	// back in to play the same battle again.
	if (!Setup.bRandomSeed)
	{
		BattleSeed = Setup.FixedSeed;
	}
	else if (bNewSeed)
	{
		uint64 Mixed = static_cast<uint64>(FDateTime::UtcNow().GetTicks());
		Mixed ^= Mixed >> 31;
		Mixed *= 0x9E3779B97F4A7C15ull;
		Mixed ^= Mixed >> 29;
		BattleSeed = 1 + Mixed % 999999;
	}

	bSetupChosen = true;
	Screen = EScreen::Battle;
	bMenuOpen = false;
	bPaused = false;
	BuildBattle();
	Log.Reset();
	LogEntries.Reset();
	LogGroup = 0;
	OrdersGiven = 0;
	DecidedFor = 0.0f;

	auto Who = [this](int32 Team)
	{
		if (bOnline)
		{
			return SideNames(Team);
		}
		return ComputerPlays(Team)
			? FString::Printf(TEXT("the computer (%s)"), *Setup.Difficulty[Team])
			: FString(TEXT("a person"));
	};
	const FString Opening = FString::Printf(TEXT("Seed %llu. Blue: %s. Red: %s."),
		BattleSeed, *Who(0), *Who(1));
	LogNote(Opening);
	UE_LOG(LogTemp, Log, TEXT("%s"), *Opening);
	// A war horn as the battle begins.
	PlayEventSound(TEXT("battleStart"), nullptr, 0.7f);
}

void ATMBattleDirector::OpenTitle()
{
	if (bReplaying)
	{
		// Puts the setup back and comes back here.
		LeaveReplay(false);
		return;
	}
	Screen = EScreen::Title;
	bMenuOpen = false;
	bPaused = false;
	// A fresh board behind the menu rather than a battle frozen part-way.
	BuildBattle();
}

void ATMBattleDirector::OpenSetup()
{
	Screen = EScreen::Setup;
	bMenuOpen = false;
	bPaused = false;
	// The choices made last time, even in an earlier session (2026-10-01).
	if (!bRestoredSetup)
	{
		bRestoredSetup = true;
		RestoreSetupChoices();
	}
	// Watchtowers on by default for a person setting up a battle, but only the
	// first time: after that the row keeps whatever they chose. A battle nobody
	// set up has none, so it is still the battle the Godot game plays.
	if (!bOfferedTowers)
	{
		bOfferedTowers = true;
		Setup.Watchtowers = 2;
	}
	// Likewise items: 6 points a side the first time.
	if (!bOfferedItems)
	{
		bOfferedItems = true;
		Setup.ItemBudget = 6;
	}
	// And the camps: standard, with the map's own boss.
	if (!bOfferedCamps)
	{
		bOfferedCamps = true;
		Setup.CampLevel = 2;
	}
	// And element reactions, on.
	if (!bOfferedElements)
	{
		bOfferedElements = true;
		Setup.bElements = true;
	}
	ItemPickerSlot = -1;
	BuildBattle();
	// Random's next teams, their heroes loading while this screen is read.
	for (int32 Team = 0; Team < 2; ++Team)
	{
		if (NextRandom[Team].empty())
		{
			RollNextRandomTeam(Team);
		}
	}
}

void ATMBattleDirector::RandomTeam(int32 Team)
{
	// The team rolled ahead (RollNextRandomTeam), whose heroes have been
	// loading in the background since, then the next one rolled and loading.
	if (NextRandom[Team].size() != 4)
	{
		RollNextRandomTeam(Team);
	}
	for (int32 Slot = 0; Slot < 4; ++Slot)
	{
		Setup.Rosters[Team][Slot] = NextRandom[Team][Slot];
	}
	DedupeRosters();
	// Built before the next is rolled: rolling lets go of this team's early
	// load, and by then the battle holds its heroes itself.
	BuildBattle();
	RollNextRandomTeam(Team);
}

void ATMBattleDirector::PressMenuButton(const FTMHudButton& Button)
{
	static const TCHAR* Levels[3] = { TEXT("easy"), TEXT("medium"), TEXT("hard") };
	// The replay bar and the Replays screen (TMBattleDirectorReplay.cpp).
	if (PressReplayButton(Button))
	{
		return;
	}
	// Watching a replay, the battle menu's choices are about the replay.
	if (bReplaying)
	{
		if (Button.Action == ETMHudAction::MenuRestart)
		{
			bMenuOpen = false;
			SeekReplay(0);
			return;
		}
		if (Button.Action == ETMHudAction::MenuSetup || Button.Action == ETMHudAction::MenuTitle)
		{
			LeaveReplay(false);
			if (Button.Action == ETMHudAction::MenuSetup)
			{
				OpenSetup();
			}
			return;
		}
	}
	switch (Button.Action)
	{
	// The title screen (main_menu.gd:67-69): every way to play goes through setup.
	case ETMHudAction::TitleVsComputer:
		Setup.Mode = TEXT("ai");
		OpenSetup();
		break;
	case ETMHudAction::TitleTwoPlayers:
		Setup.Mode = TEXT("hotseat");
		OpenSetup();
		break;
	case ETMHudAction::TitleWatch:
		Setup.Mode = TEXT("cpu");
		OpenSetup();
		break;
	case ETMHudAction::Quit:
		UKismetSystemLibrary::QuitGame(this, GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr,
			EQuitPreference::Quit, false);
		break;

	// The setup screen (battle_setup.gd).
	case ETMHudAction::SetupClass:
		// With every class loaded there are far too many to step through, so a
		// slot opens the picker (class_picker.gd).
		PickerSlot = Button.Value;
		break;
	case ETMHudAction::PickerChoose:
	{
		const std::vector<const TMSim::FJobDef*>& Jobs = TMSim::AllJobs();
		// Random (v20): any class it could have, of the role shown.
		if (Button.Value == -2 && PickerSlot >= 0)
		{
			const std::string Rolled = RandomClassFor(PickerSlot / 4, PickerSlot % 4, TSet<FString>(), PickerRole);
			for (int32 j = 0; j < static_cast<int32>(Jobs.size()); ++j)
			{
				if (Jobs[j]->Id == Rolled)
				{
					FTMHudButton Chosen = Button;
					Chosen.Value = j;
					PressMenuButton(Chosen);
					return;
				}
			}
			Tell(TEXT("No class left to roll for that slot."));
			break;
		}
		if (Screen == EScreen::Lobby && PickerSlot >= 0 && Button.Value >= 0 && Button.Value < static_cast<int32>(Jobs.size()))
		{
			LobbyPick(PickerSlot, Jobs[Button.Value]->Id);
		}
		else if (PickerSlot >= 0 && Button.Value >= 0 && Button.Value < static_cast<int32>(Jobs.size())
			&& !(Setup.bUniqueClasses && ClassTaken(Jobs[Button.Value]->Id, PickerSlot / 4, PickerSlot % 4)))
		{
			Setup.Rosters[PickerSlot / 4][PickerSlot % 4] = Jobs[Button.Value]->Id;
			BuildBattle();
		}
		PickerSlot = -1;
		break;
	}
	case ETMHudAction::PickerRole:
		PickerRole = Button.Value;
		break;
	case ETMHudAction::PickerClose:
		PickerSlot = -1;
		break;
	case ETMHudAction::SetupItem:
		// In the lobby, only the slots of a player's own units (and the host the computer's).
		if (Screen == EScreen::Lobby && !LobbyMayPick(LocalPlayer, (Button.Value / 12) * 4 + (Button.Value % 12) / 3))
		{
			break;
		}
		if (!PicksOwnItems(Button.Value / 12))
		{
			ItemPickerSlot = Button.Value;
			ItemPickerScroll = 0;
		}
		break;
	case ETMHudAction::ItemChoose:
		ChooseItem(ItemPickerSlot, Button.Value);
		break;
	case ETMHudAction::ItemTier:
		ItemPickerTier = Button.Value;
		ItemPickerScroll = 0;
		break;
	case ETMHudAction::ItemClose:
		ItemPickerSlot = -1;
		break;
	case ETMHudAction::SetupItemBudget:
	{
		// 0, 3, 6, 9, 12, round. Whatever no longer fits comes off, last slot first.
		Setup.ItemBudget = Setup.ItemBudget >= 12 ? 0 : Setup.ItemBudget + 3;
		for (int32 Team = 0; Team < 2; ++Team)
		{
			for (int32 Code = 11; Code >= 0 && ItemPointsSpent(Team) > Setup.ItemBudget; --Code)
			{
				Setup.Items[Team][Code / 3][Code % 3].clear();
			}
		}
		BuildBattle();
		break;
	}
	case ETMHudAction::SetupRandom:
		RandomTeam(Button.Value);
		break;
	case ETMHudAction::SetupDefault:
	{
		const char* Default[4] = { "knight", "archer", "black_mage", "white_mage" };
		for (int32 Slot = 0; Slot < 4; ++Slot)
		{
			Setup.Rosters[Button.Value][Slot] = Default[Slot];
		}
		DedupeRosters();
		BuildBattle();
		break;
	}
	case ETMHudAction::SetupSide:
		Setup.PlayerTeam = 1 - Setup.PlayerTeam;
		break;
	case ETMHudAction::SetupDifficulty:
	{
		FString& Current = Setup.Difficulty[Button.Value];
		int32 At = 2;
		for (int32 i = 0; i < 3; ++i)
		{
			if (Current == Levels[i])
			{
				At = i;
			}
		}
		Current = Levels[(At + 1) % 3];
		break;
	}
	case ETMHudAction::SetupSeed:
		Setup.bRandomSeed = !Setup.bRandomSeed;
		// Fixing the seed keeps the one just played, so a battle worth seeing again
		// can be seen again.
		if (!Setup.bRandomSeed)
		{
			Setup.FixedSeed = BattleSeed;
		}
		break;
	case ETMHudAction::SetupVictory:
	{
		// Round the three the Godot setup offers (battle_setup.gd:40).
		const double Choices[3] = { 0.0, 30.0, 60.0 };
		int32 At = 0;
		for (int32 i = 0; i < 3; ++i)
		{
			if (Setup.CaptureSeconds == Choices[i])
			{
				At = i;
			}
		}
		Setup.CaptureSeconds = Choices[(At + 1) % 3];
		break;
	}
	case ETMHudAction::SetupTime:
	{
		const double Choices[4] = { 0.0, 180.0, 300.0, 600.0 };
		int32 At = 0;
		for (int32 i = 0; i < 4; ++i)
		{
			if (Setup.BattleSeconds == Choices[i])
			{
				At = i;
			}
		}
		Setup.BattleSeconds = Choices[(At + 1) % 4];
		break;
	}
	case ETMHudAction::SetupMap:
	{
		const std::vector<TMSim::FMapDef>& Maps = TMSim::AllMaps();
		int32 At = 0;
		for (int32 i = 0; i < static_cast<int32>(Maps.size()); ++i)
		{
			At = Maps[i].Id == Setup.MapId ? i : At;
		}
		Setup.MapId = Maps[(At + 1) % Maps.size()].Id;
		// Rebuilt now: nothing in a running game rebuilds a battle marked unbuilt,
		// and the board and every menu stopped drawing until one was.
		BuildBattle();
		break;
	}
	case ETMHudAction::SetupTheme:
	{
		// The map's own look first, then each theme.
		LoadThemes();
		int32 At = ThemeIds.IndexOfByKey(Setup.ThemeId);
		Setup.ThemeId = At + 1 < ThemeIds.Num() ? ThemeIds[At + 1] : FString();
		BuildBattle();
		break;
	}
	case ETMHudAction::SetupPlanning:
	{
		const double Choices[4] = { 0.0, 30.0, 60.0, 90.0 };
		int32 At = 0;
		for (int32 i = 0; i < 4; ++i)
		{
			if (Setup.PlanningSeconds == Choices[i])
			{
				At = i;
			}
		}
		Setup.PlanningSeconds = Choices[(At + 1) % 4];
		break;
	}
	case ETMHudAction::SetupTowers:
		// Round every count the rules allow, none to eight.
		Setup.Watchtowers = (Setup.Watchtowers + 1) % 9;
		BuildBattle();
		break;
	case ETMHudAction::SetupCamps:
		Setup.CampLevel = (Setup.CampLevel + 1) % 4;
		BuildBattle();
		break;
	case ETMHudAction::SetupBoss:
		Setup.bRandomBoss = !Setup.bRandomBoss;
		BuildBattle();
		break;
	case ETMHudAction::SetupElements:
		Setup.bElements = !Setup.bElements;
		BuildBattle();
		break;
	case ETMHudAction::SetupCampRespawn:
		Setup.bCampRespawn = !Setup.bCampRespawn;
		BuildBattle();
		break;
	case ETMHudAction::SetupBossHunt:
		Setup.bBossHunt = !Setup.bBossHunt;
		BuildBattle();
		break;
	case ETMHudAction::SetupBossClaim:
		Setup.bBossClaim = !Setup.bBossClaim;
		BuildBattle();
		break;
	case ETMHudAction::SetupFriendlyFire:
		Setup.bFriendlyFire = !Setup.bFriendlyFire;
		BuildBattle();
		break;
	case ETMHudAction::SetupTileMove:
		Setup.TileMove = (Setup.TileMove + 1) % 3;
		BuildBattle();
		break;
	case ETMHudAction::SetupUniqueClasses:
		Setup.bUniqueClasses = !Setup.bUniqueClasses;
		DedupeRosters();
		BuildBattle();
		if (Screen == EScreen::Lobby && Net.IsValid() && Net->IsHost())
		{
			BroadcastLobby();
		}
		break;
	case ETMHudAction::PlanningReady:
		ReadyToFight();
		break;
	case ETMHudAction::ToggleLayout:
		ToggleLayout();
		break;
	case ETMHudAction::LayoutGrab:
	{
		const APlayerController* Player = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
		const ATMBattleHud* Hud = Player ? Cast<ATMBattleHud>(Player->GetHUD()) : nullptr;
		float X = 0.0f;
		float Y = 0.0f;
		if (Hud && Hud->Movables.IsValidIndex(Button.Value) && CursorPosition(X, Y))
		{
			DragPanel = Hud->Movables[Button.Value].Id;
			DragFrom = FVector2D(X, Y);
			DragStartMin = Hud->Movables[Button.Value].Area.Min;
			const FVector2D* Moved = FTMSettings::Get().Layout.Find(DragPanel);
			DragHomeMin = DragStartMin - (Moved ? *Moved : FVector2D::ZeroVector) * Hud->Scale();
		}
		break;
	}
	case ETMHudAction::LayoutResize:
	{
		const APlayerController* Player = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
		const ATMBattleHud* Hud = Player ? Cast<ATMBattleHud>(Player->GetHUD()) : nullptr;
		float X = 0.0f;
		float Y = 0.0f;
		if (Hud && Hud->Movables.IsValidIndex(Button.Value) && CursorPosition(X, Y))
		{
			ResizePanel = Hud->Movables[Button.Value].Id;
			DragFrom = FVector2D(X, Y);
			ResizeStartScale = FTMSettings::Get().ScaleOf(*ResizePanel);
			ResizeStartWidth = FMath::Max(20.0f, static_cast<float>(Hud->Movables[Button.Value].Area.GetSize().X));
		}
		break;
	}
	case ETMHudAction::LayoutGrid:
		FTMSettings::Get().bLayoutGrid = !FTMSettings::Get().bLayoutGrid;
		FTMSettings::Get().Save();
		break;
	case ETMHudAction::LayoutGridSize:
	{
		// Finer for more precision, coarser for quick lining up.
		const float Sizes[4] = { 5.0f, 10.0f, 20.0f, 40.0f };
		int32 At = 2;
		for (int32 i = 0; i < 4; ++i)
		{
			if (FMath::IsNearlyEqual(FTMSettings::Get().GridSize, Sizes[i]))
			{
				At = i;
			}
		}
		FTMSettings::Get().GridSize = Sizes[(At + 1) % 4];
		FTMSettings::Get().Save();
		break;
	}
	case ETMHudAction::LayoutCard:
		DragCard = Button.Value;
		break;
	case ETMHudAction::LayoutReset:
		FTMSettings::Get().ResetLayout();
		break;
	case ETMHudAction::OptionAutoRecenter:
	{
		// Always, When I'm idle, Never (2026-10-03); with no way given, the next.
		FTMSettings& Settings = FTMSettings::Get();
		Settings.CameraFollow = Button.Value >= 0 && Button.Value <= 2 ? Button.Value : (Settings.CameraFollow + 1) % 3;
		Settings.bAutoRecenter = Settings.CameraFollow != 2;
		Settings.Save();
		break;
	}
	case ETMHudAction::OptionQuickCast:
		if (Button.Value >= 0 && Button.Value < 4)
		{
			FTMSettings::Get().bQuickCast[Button.Value] = !FTMSettings::Get().bQuickCast[Button.Value];
			FTMSettings::Get().Save();
		}
		break;
	case ETMHudAction::OptionFeel:
		if (bool* On = ATMBattleHud::FeelOption(Button.Value))
		{
			*On = !*On;
			FTMSettings::Get().Save();
		}
		break;
	case ETMHudAction::OptionTurnSquares:
		FTMSettings::Get().bTurnSquares = !FTMSettings::Get().bTurnSquares;
		FTMSettings::Get().Save();
		break;
	case ETMHudAction::OptionSquadStrip:
		FTMSettings::Get().bSquadStrip = !FTMSettings::Get().bSquadStrip;
		FTMSettings::Get().Save();
		break;
	case ETMHudAction::OpenOptions:
		bOptionsOpen = true;
		OptionsScroll = 0.0f;
		bDevToolsOpen = false;
		break;
	case ETMHudAction::OpenDevTools:
		bDevToolsOpen = true;
		bOptionsOpen = false;
		break;
	case ETMHudAction::CloseOverlay:
		bOptionsOpen = false;
		bDevToolsOpen = false;
		CaptureAction = -1;
		FlushTuning();
		FTMSettings::Get().Save();
		break;
	case ETMHudAction::Slider:
		DragSlider = Button.Value;
		break;
	case ETMHudAction::OptionUiScale:
	{
		// options_menu.gd:64, the four sizes.
		const float Sizes[4] = { 0.9f, 1.0f, 1.15f, 1.3f };
		int32 At = 1;
		for (int32 i = 0; i < 4; ++i)
		{
			if (FMath::IsNearlyEqual(FTMSettings::Get().UiScale, Sizes[i]))
			{
				At = i;
			}
		}
		FTMSettings::Get().UiScale = Sizes[(At + 1) % 4];
		FTMSettings::Get().Save();
		break;
	}
	case ETMHudAction::OptionFullscreen:
		// Put into effect from Tick, at most once a second (bFullscreenPending).
		FTMSettings::Get().bFullscreen = !FTMSettings::Get().bFullscreen;
		FTMSettings::Get().Save();
		bFullscreenPending = true;
		break;
	case ETMHudAction::OptionColorblind:
		FTMSettings::Get().bColorblind = !FTMSettings::Get().bColorblind;
		FTMSettings::Get().Save();
		RefreshPlates();
		break;
	case ETMHudAction::RebindAction:
		CaptureAction = Button.Value;
		break;
	case ETMHudAction::ResetOptions:
		FTMSettings::Get().ResetOptions();
		RefreshPlates();
		break;
	case ETMHudAction::DevReset:
	{
		const TMSim::FTuning Defaults = GameTuning();
		const TMSim::FTuningKey& Key = TMSim::TuningKeys()[static_cast<size_t>(Button.Value)];
		SetSlider(SliderTuning + Button.Value, Defaults.*Key.Member);
		FTMSettings::Get().Tuning.Remove(UTF8_TO_TCHAR(Key.Key));
		FTMSettings::Get().Save();
		break;
	}
	case ETMHudAction::DevResetAll:
	{
		const TMSim::FTuning Defaults = GameTuning();
		for (int32 i = 0; i < static_cast<int32>(TMSim::TuningKeys().size()); ++i)
		{
			if (!OnSetupScreen(i))
			{
				SetSlider(SliderTuning + i, Defaults.*TMSim::TuningKeys()[static_cast<size_t>(i)].Member);
				FTMSettings::Get().Tuning.Remove(UTF8_TO_TCHAR(TMSim::TuningKeys()[static_cast<size_t>(i)].Key));
			}
		}
		FTMSettings::Get().Save();
		break;
	}
	case ETMHudAction::SetupStart:
		if (Setup.Mode == TEXT("online") && Net.IsValid() && Net->IsHost())
		{
			// The rules changed from the lobby: back to it, everyone told.
			OpenLobby();
			BroadcastLobby();
		}
		else if (Setup.Mode == TEXT("online"))
		{
			HostOnline();
		}
		else
		{
			StartMatch(true);
		}
		break;
	case ETMHudAction::SetupBack:
		if (Setup.Mode == TEXT("online") && Net.IsValid() && Net->IsHost())
		{
			OpenLobby();
			BroadcastLobby();
		}
		else if (Setup.Mode == TEXT("online"))
		{
			// Back to hosting or joining, no longer hosting.
			Upnp.Reset();
			Net.Reset();
			OnlineStatus.Reset();
			OpenOnline();
		}
		else
		{
			OpenTitle();
		}
		break;

	// Playing online (main_menu.gd:79-230).
	case ETMHudAction::TitleOnline:
		OpenOnline();
		break;
	case ETMHudAction::OnlineHost:
		// The host chooses the battle on the setup screen first.
		if (Setup.Mode != TEXT("online"))
		{
			OfflineSetup = Setup;
		}
		Setup.Mode = TEXT("online");
		OpenSetup();
		break;
	case ETMHudAction::OnlineJoin:
		JoinOnline();
		break;
	case ETMHudAction::OnlineField:
		StartTyping(static_cast<ETypeField>(Button.Value));
		break;
	case ETMHudAction::OnlineBack:
		LeaveOnline();
		OpenTitle();
		break;
	case ETMHudAction::OnlineJoinCode:
		JoinByCode();
		break;
	case ETMHudAction::OnlineAdvanced:
		bOnlineAdvanced = !bOnlineAdvanced;
		break;
	case ETMHudAction::LobbyCopyCode:
		CopyOnlineCode();
		break;

	// The lobby and the draft (TMBattleDirectorLobby.cpp, TMBattleDirectorDraft.cpp).
	case ETMHudAction::LobbySide:
		LobbySide(Button.Value);
		break;
	case ETMHudAction::LobbyReady:
		LobbyReady();
		break;
	case ETMHudAction::LobbyStart:
		LobbyStart();
		break;
	case ETMHudAction::LobbySettings:
		if (IsHostInLobby())
		{
			OpenSetup();
		}
		break;
	case ETMHudAction::LobbyLeave:
		LeaveOnline();
		OpenTitle();
		break;
	case ETMHudAction::LobbyRandom:
		if (Screen == EScreen::Lobby && !Setup.bDraft)
		{
			LobbyRandom();
		}
		break;
	case ETMHudAction::LobbySlot:
		if (Screen == EScreen::Lobby && !Setup.bDraft && LobbyMayPick(LocalPlayer, Button.Value))
		{
			PickerSlot = Button.Value;
		}
		break;
	case ETMHudAction::DraftChoose:
	{
		const std::vector<const TMSim::FJobDef*>& Jobs = TMSim::AllJobs();
		if (Screen == EScreen::Draft && Button.Value == -2)
		{
			// Random (v20): any class still free, of the role shown.
			TArray<FString> Free;
			const char* Roles[4] = { "tank", "damage", "support", "special" };
			for (const TMSim::FJobDef* Job : Jobs)
			{
				const FString Id = UTF8_TO_TCHAR(Job->Id.c_str());
				if (!DraftUsed(Id) && (PickerRole < 0 || PickerRole > 3 || TMSim::JobHasRole(Job->Id, Roles[PickerRole])))
				{
					Free.Add(Id);
				}
			}
			if (Free.Num() > 0)
			{
				DraftChoose(Free[FMath::RandRange(0, Free.Num() - 1)]);
			}
		}
		else if (Screen == EScreen::Draft && Button.Value >= 0 && Button.Value < static_cast<int32>(Jobs.size()))
		{
			DraftChoose(UTF8_TO_TCHAR(Jobs[Button.Value]->Id.c_str()));
		}
		else if (Screen == EScreen::Draft && Button.Value == -1 && DraftStepIsBan(Draft.Step))
		{
			DraftChoose(FString());  // let the ban go
		}
		break;
	}
	case ETMHudAction::DraftToggle:
		if (IsHostInLobby())
		{
			Setup.bDraft = !Setup.bDraft;
			BroadcastLobby();
		}
		break;
	case ETMHudAction::DraftTimer:
		if (IsHostInLobby())
		{
			// Off, 20, 30, 60 seconds a choice, in turn.
			Setup.DraftSeconds = Setup.DraftSeconds == 0 ? 20 : Setup.DraftSeconds == 20 ? 30 : Setup.DraftSeconds == 30 ? 60 : 0;
			BroadcastLobby();
		}
		break;

	// The menu inside a battle, and the end of one.
	case ETMHudAction::MenuResume:
		bMenuOpen = false;
		break;
	case ETMHudAction::MenuRestart:
		// The same battle again from the start: same classes, same seed. Not online:
		// a battle both are in can't be taken back by one of them.
		if (!bOnline)
		{
			StartMatch(false);
		}
		break;
	case ETMHudAction::MenuSetup:
		if (!bOnline)
		{
			OpenSetup();
		}
		break;
	case ETMHudAction::MenuTitle:
		// Leaving an online match closes the connection (battle.gd:1100).
		if (bOnline || Net.IsValid())
		{
			LeaveOnline();
		}
		OpenTitle();
		break;
	default:
		break;
	}
}
// ============================================================ what can be seen

int32 ATMBattleDirector::ViewerTeam() const
{
	// One person against the computer sees what their side sees. Two people at
	// one screen, or nobody playing at all, see everything (battle.gd viewer_team).
	if (Screen != EScreen::Battle)
	{
		return -1;
	}
	// A replay sees what its watcher chose: everything, or one side's fog.
	if (bReplaying)
	{
		return ReplayView;
	}
	// Online, each player sees through their own side's eyes (battle.gd:141-142).
	if (bOnline)
	{
		return LocalTeam;
	}
	if (ComputerPlays(0) == ComputerPlays(1))
	{
		return -1;
	}
	return ComputerPlays(0) ? 1 : 0;
}

bool ATMBattleDirector::IsSeen(const TMSim::FUnit& Unit) const
{
	const int32 Viewer = ViewerTeam();
	if (Viewer < 0 || Battle.Winner != -1 || Unit.Team == Viewer)
	{
		return true;
	}
	// While a tower just taken is catching, its spreading sight, not all of it at once.
	if (AnyTowerKindling(Viewer))
	{
		return (SeenWithoutKindling(Viewer, Unit.Pos) || KindlingReaches(Viewer, Unit.Pos)) && !Battle.Hidden(Viewer, Unit);
	}
	return Battle.CanSeeUnit(Viewer, Unit);
}

bool ATMBattleDirector::IsPointSeen(const TMSim::FVec2& Point) const
{
	const int32 Viewer = ViewerTeam();
	if (Viewer < 0 || Battle.Winner != -1)
	{
		return true;
	}
	if (AnyTowerKindling(Viewer))
	{
		return SeenWithoutKindling(Viewer, Point) || KindlingReaches(Viewer, Point);
	}
	return Battle.CanSee(Viewer, Point);
}

void ATMBattleDirector::UpdateThreat()
{
	// A card stays open only for a unit that is still there to be looked at, and
	// is not the one being ordered (hud.gd _update_inspect, battle.gd:494-497).
	const TMSim::FUnit* Unit = FindIn(Battle, InspectedId);
	if (Unit && (((!Unit->IsAlive()) && !Unit->IsKo()) || !IsSeen(*Unit) || Unit->Id == SelectedId))
	{
		InspectedId = -1;
		Unit = nullptr;
	}

	// The threat is drawn only for an enemy of whoever is looking.
	const TMSim::FUnit* Selected = SelectedUnit();
	const int32 AllyTeam = Selected ? Selected->Team : (ViewerTeam() >= 0 ? ViewerTeam() : 0);
	if (!Unit || !Unit->IsAlive() || Unit->Team == AllyTeam)
	{
		ThreatSignature.Reset();
		ThreatNodes.clear();
		ThreatReach = 0.0f;
		return;
	}
	const FString Signature = FString::Printf(TEXT("%d:%.2f,%.2f:%d:%d"),
		Unit->Id, Unit->Pos.X, Unit->Pos.Y, Unit->Serial, Unit->bMoved ? 1 : 0);
	if (Signature == ThreatSignature)
	{
		return;
	}
	ThreatSignature = Signature;
	ThreatNodes.clear();
	for (const std::pair<TMSim::FNode, double>& Entry : Battle.ReachableNodes(*Unit))
	{
		ThreatNodes.push_back(Entry.first);
	}
	ThreatReach = 0.0f;
	for (int32 Slot = 0; Slot < 4; ++Slot)
	{
		const TMSim::FAbility* Ability = Unit->Ability(Slot);
		if (Ability && Ability->Effect == TMSim::EEffect::Damage)
		{
			ThreatReach = FMath::Max(ThreatReach, Ability->MaxRange);
		}
	}
}

TArray<int32> ATMBattleDirector::GuideShown() const
{
	static const char* const Roles[4] = { "tank", "damage", "support", "special" };
	TArray<int32> Out;
	const std::vector<const TMSim::FJobDef*>& Jobs = TMSim::AllJobs();
	for (int32 j = 0; j < static_cast<int32>(Jobs.size()); ++j)
	{
		if (GuideRole < 0 || GuideRole > 3 || TMSim::JobHasRole(Jobs[j]->Id, Roles[GuideRole]))
		{
			Out.Add(j);
		}
	}
	return Out;
}

void ATMBattleDirector::GuideStep(int32 By)
{
	// The next or previous class in the list as it is filtered, round and round.
	const TArray<int32> Shown = GuideShown();
	if (Shown.Num() == 0)
	{
		return;
	}
	const int32 At = Shown.Find(GuideJob);
	const int32 Next = At == INDEX_NONE ? 0 : (At + By % Shown.Num() + Shown.Num()) % Shown.Num();
	GuideJob = Shown[Next];
	GuideDetailScroll = 0.0f;
	// Keep the list scrolled to it, for coming back.
	GuideListScroll = FMath::Max(0, Next - GuideListPage / 2);
}

void ATMBattleDirector::ToggleGuide()
{
	bGuideOpen = !bGuideOpen;
	if (!bGuideOpen)
	{
		bGuideDetail = false;
		HideGuideModel();
	}
	// A local battle stops while the guide is read, and starts again when it is
	// closed -- unless it was already paused, which stays as it was.
	if (Screen == EScreen::Battle && Battle.Winner == -1)
	{
		if (bGuideOpen && !bPaused)
		{
			bPaused = true;
			bPausedByGuide = true;
		}
		else if (!bGuideOpen && bPausedByGuide)
		{
			bPaused = false;
			bPausedByGuide = false;
		}
	}
	// Open on the class being looked at, if there is one.
	if (bGuideOpen)
	{
		const TMSim::FUnit* Looking = SelectedUnit();
		if (!Looking)
		{
			Looking = FindIn(Battle, InspectedId);
		}
		if (Looking)
		{
			const std::vector<const TMSim::FJobDef*>& Jobs = TMSim::AllJobs();
			for (size_t i = 0; i < Jobs.size(); ++i)
			{
				if (Jobs[i]->Id == Looking->Job)
				{
					// Straight to that class's page.
					GuideJob = static_cast<int32>(i);
					bGuideDetail = true;
					GuideDetailScroll = 0.0f;
				}
			}
		}
	}
}
FString ATMBattleDirector::HowWon() const
{
	// Read back from the rules' own state rather than remembered as it happened,
	// so a loaded or replayed battle says the same (game_state.gd:1196, 1556).
	if (Battle.Winner == -1)
	{
		return FString();
	}
	const TCHAR* Side = Battle.Winner == 0 ? TEXT("Blue") : TEXT("Red");
	if (Battle.Tuning.BattleSeconds > 0.0
		&& Battle.TickCount >= TMSim::RoundToInt(Battle.Tuning.BattleSeconds * TMSim::Pace::TicksPerSecond))
	{
		return Battle.Winner == TMSim::FBattle::Draw
			? FString(TEXT("Time! It's a draw"))
			: FString::Printf(TEXT("Time! %s wins on health"), Side);
	}
	if (Battle.Tuning.CaptureSeconds > 0.0 && Battle.Winner != TMSim::FBattle::Draw
		&& Battle.CaptureTicks[Battle.Winner] >= TMSim::RoundToInt(Battle.Tuning.CaptureSeconds * TMSim::Pace::TicksPerSecond))
	{
		return FString::Printf(TEXT("%s has held the middle"), Side);
	}
	return FString();
}

bool ATMBattleDirector::CursorPosition(float& X, float& Y) const
{
	if (bRobotDriving)
	{
		X = static_cast<float>(RobotCursor.X);
		Y = static_cast<float>(RobotCursor.Y);
		return RobotCursor.X >= 0.0;
	}
	const UWorld* World = GetWorld();
	const APlayerController* Player = World ? World->GetFirstPlayerController() : nullptr;
	return Player && Player->GetMousePosition(X, Y);
}

void ATMBattleDirector::UpdateCamera(float DeltaSeconds)
{
	const UWorld* World = GetWorld();
	APlayerController* Player = World ? World->GetFirstPlayerController() : nullptr;
	if (!Watcher || !Player)
	{
		return;
	}
	const FTMSettings& Settings = FTMSettings::Get();
	float X = 0.0f;
	float Y = 0.0f;
	const bool bHaveCursor = CursorPosition(X, Y);
	const FVector2D Cursor(X, Y);
	const FVector2D Moved = bHaveCursor && LastCursor.X >= 0.0 ? Cursor - LastCursor : FVector2D::ZeroVector;
	LastCursor = bHaveCursor ? Cursor : FVector2D(-1.0, -1.0);

	// A slider being dragged in Options or Developer Tools follows the pointer.
	if (DragSlider >= 0)
	{
		const ATMBattleHud* Hud = Cast<ATMBattleHud>(Player->GetHUD());
		const FBox2D* Area = Hud ? Hud->SliderAreas.Find(DragSlider) : nullptr;
		double Low = 0.0;
		double High = 1.0;
		double Step = 0.0;
		if (Area && bHaveCursor && SliderRange(DragSlider, Low, High, Step))
		{
			const double Along = FMath::Clamp((Cursor.X - Area->Min.X) / FMath::Max(1.0, Area->GetSize().X), 0.0, 1.0);
			double Value = Low + Along * (High - Low);
			if (Step > 0.0)
			{
				Value = Low + FMath::RoundToDouble((Value - Low) / Step) * Step;
			}
			SetSlider(DragSlider, FMath::Clamp(Value, Low, High));
		}
		return;
	}

	// A panel being dragged in Edit layout follows the pointer, remembered in
	// 1080p pixels so it keeps its place at any size or UI scale.
	if (!DragPanel.IsEmpty() && bHaveCursor)
	{
		// Where its corner would go; on the grid, the nearest line crossing.
		const ATMBattleHud* Hud = Cast<ATMBattleHud>(Player->GetHUD());
		const float Scale = Hud ? FMath::Max(0.1f, Hud->Scale()) : 1.0f;
		FVector2D Corner = DragStartMin + (Cursor - DragFrom);
		if (Settings.bLayoutGrid)
		{
			const double Step = FMath::Max(4.0, Settings.GridSize * Scale);
			Corner.X = FMath::RoundToDouble(Corner.X / Step) * Step;
			Corner.Y = FMath::RoundToDouble(Corner.Y / Step) * Step;
		}
		FTMSettings::Get().Layout.Add(DragPanel, (Corner - DragHomeMin) / Scale);
	}
	if (!ResizePanel.IsEmpty() && bHaveCursor)
	{
		// Wider by as much as the grip has been pulled, in steps of 5%.
		const float Wanted = ResizeStartScale * (ResizeStartWidth + static_cast<float>(Cursor.X - DragFrom.X)) / ResizeStartWidth;
		FTMSettings::Get().LayoutScale.Add(ResizePanel, FMath::Clamp(FMath::RoundToFloat(Wanted * 20.0f) / 20.0f, 0.5f, 2.5f));
	}

	// Nothing moves the camera behind a menu, or while a key is being chosen.
	const bool bFree = bPlayerInput && !bMenuOpen && !bGuideOpen && !bOptionsOpen && !bDevToolsOpen
		&& CaptureAction < 0 && PickerSlot < 0 && Screen == EScreen::Battle;
	if (bFree)
	{
		auto Held = [&Settings, Player](ETMAction Action)
		{
			for (const FKey& Key : Settings.Keys(Action))
			{
				if (Player->IsInputKeyDown(Key))
				{
					return true;
				}
			}
			return false;
		};
		const FVector Forward = FRotator(0.0f, CamYaw, 0.0f).Vector();
		const FVector Right = FRotator(0.0f, CamYaw + 90.0f, 0.0f).Vector();
		FVector Pan = FVector::ZeroVector;
		if (Held(ETMAction::CamForward)) { Pan += Forward; }
		if (Held(ETMAction::CamBack)) { Pan -= Forward; }
		if (Held(ETMAction::CamRight)) { Pan += Right; }
		if (Held(ETMAction::CamLeft)) { Pan -= Right; }
		// 8 m/s at the usual distance, faster when far out (camera_rig.gd:57-70).
		CamWantTarget += Pan.GetSafeNormal() * 800.0f * Settings.CameraSpeed * DeltaSeconds * (CamDistance / 2200.0f);
		if (Held(ETMAction::CamUp)) { CamWantTarget.Z += 400.0f * DeltaSeconds; }
		if (Held(ETMAction::CamDown)) { CamWantTarget.Z -= 400.0f * DeltaSeconds; }
		const float Turn = 103.0f * Settings.CameraSpeed * DeltaSeconds;
		if (Held(ETMAction::CamRotateLeft)) { CamYaw -= Turn; }
		if (Held(ETMAction::CamRotateRight)) { CamYaw += Turn; }
		// Right-drag turns and tilts; middle-drag slides (camera_rig.gd:94-101).
		if (bRightHeld)
		{
			RightDragged += static_cast<float>(Moved.Size());
			CamYaw += static_cast<float>(Moved.X) * 0.344f;
			CamPitch = FMath::Clamp(CamPitch - static_cast<float>(Moved.Y) * 0.344f, -85.0f, -10.0f);
		}
		if (bMiddleHeld)
		{
			const float K = CamDistance * 0.0015f;
			CamWantTarget += (-Right * static_cast<float>(Moved.X) + Forward * static_cast<float>(Moved.Y)) * K;
		}
		// The pointer resting at the edge of the window pans that way (Options;
		// 2026-10-03), gently at first and up to the keys' speed over a third of a
		// second. Not while dragging, nor in Edit layout.
		int32 ViewW = 0;
		int32 ViewH = 0;
		Player->GetViewportSize(ViewW, ViewH);
		FVector2D Edge = FVector2D::ZeroVector;
		FTMHudButton EdgeButton;
		const ATMBattleHud* EdgeHud = Cast<ATMBattleHud>(Player->GetHUD());
		if (Settings.bEdgePan && bHaveCursor && !bRightHeld && !bMiddleHeld && !bEditingLayout && DragCard < 0 && ViewW > 0 && ViewH > 0
			&& X >= 0.0f && Y >= 0.0f && X < ViewW && Y < ViewH && !(EdgeHud && EdgeHud->ButtonAt(Cursor, EdgeButton)))
		{
			constexpr float Margin = 8.0f;
			Edge.X = X <= Margin ? -1.0 : (X >= ViewW - 1 - Margin ? 1.0 : 0.0);
			Edge.Y = Y <= Margin ? -1.0 : (Y >= ViewH - 1 - Margin ? 1.0 : 0.0);
		}
		// Moving the camera yourself is your hands on the controls (camera rules A).
		if (!Pan.IsZero() || !Edge.IsZero() || Held(ETMAction::CamUp) || Held(ETMAction::CamDown)
			|| Held(ETMAction::CamRotateLeft) || Held(ETMAction::CamRotateRight))
		{
			LastInputAt = FPlatformTime::Seconds();
		}
		if (!Edge.IsZero())
		{
			EdgeHeld = FMath::Min(EdgeHeld + DeltaSeconds, 1.0f);
			const float Ease = FMath::InterpEaseInOut(0.15f, 1.0f, FMath::Clamp(EdgeHeld / 0.35f, 0.0f, 1.0f), 2.0f);
			const FVector Way = (Right * static_cast<float>(Edge.X) - Forward * static_cast<float>(Edge.Y)).GetSafeNormal();
			CamWantTarget += Way * 800.0f * Settings.CameraSpeed * DeltaSeconds * (CamDistance / 2200.0f) * Ease;
		}
		else
		{
			EdgeHeld = 0.0f;
		}
		// Not so far off the board that it is lost.
		const TMSim::FVec2 Size = Battle.Map.SizeMeters();
		CamWantTarget.X = FMath::Clamp(CamWantTarget.X, -400.0, Size.X * TileSize + 400.0);
		CamWantTarget.Y = FMath::Clamp(CamWantTarget.Y, -400.0, Size.Y * TileSize + 400.0);
		CamWantTarget.Z = FMath::Clamp(CamWantTarget.Z, -200.0, 1500.0);
	}
	CamDistance = FMath::Lerp(CamDistance, CamWantDistance, FMath::Min(1.0f, DeltaSeconds * 10.0f));
	CamTarget = FMath::Lerp(CamTarget, CamWantTarget, FMath::Min(1.0f, DeltaSeconds * 8.0f));
	ApplyCamera();
}

void ATMBattleDirector::ApplyCamera()
{
	if (!Watcher)
	{
		return;
	}
	const FRotator Looking(CamPitch, CamYaw, 0.0f);
	// JoltOffset: a heavy blow shakes the camera for a moment (AdvanceLooks).
	Watcher->SetActorLocationAndRotation(CamTarget - Looking.Vector() * CamDistance + JoltOffset, Looking);
}

void ATMBattleDirector::CenterCamera()
{
	const TMSim::FUnit* On = SelectedUnit();
	if (!On)
	{
		for (const TMSim::FUnit& Unit : Battle.Units)
		{
			if (Unit.IsAlive() && Unit.bReady && !ComputerPlaysUnit(Unit) && (!bOnline || UnitOwner(Unit) == LocalPlayer))
			{
				On = &Unit;
				break;
			}
		}
	}
	if (On)
	{
		CenterCameraOn(*On);
	}
}

void ATMBattleDirector::CenterCameraOn(const TMSim::FUnit& Unit)
{
	const FVector Where = GetActorTransform().TransformPosition(ShownAt(Unit));
	// During an ultimate's close-up, this is where the camera goes back to.
	if (CloseUpUntil > 0.0)
	{
		CloseUpReturnTarget = Where;
		return;
	}
	CamWantTarget = Where;
}

bool ATMBattleDirector::OnSetupScreen(int32 TuningIndex)
{
	const std::string Key = TMSim::TuningKeys()[static_cast<size_t>(TuningIndex)].Key;
	// The setup screen owns these: a Dev Tools slider for one would be overridden
	// when the battle is set up (2026-10-01: camps, boss, elements, friendly fire
	// and respawn joined them; they are kept between sessions with the setup).
	return Key == "capture_seconds" || Key == "battle_seconds" || Key == "planning_seconds"
		|| Key == "watchtower_count" || Key == "item_budget" || Key == "camps" || Key == "random_boss"
		|| Key == "elements" || Key == "friendly_fire" || Key == "camp_respawn" || Key == "boss_hunt" || Key == "boss_claim" || Key == "tile_move";
}

int32 ATMBattleDirector::ItemPointsSpent(int32 Team) const
{
	int32 Spent = 0;
	for (int32 Unit = 0; Unit < 4; ++Unit)
	{
		for (int32 Slot = 0; Slot < 3; ++Slot)
		{
			if (const TMSim::FItemDef* Item = TMSim::FindItem(Setup.Items[Team][Unit][Slot]))
			{
				Spent += Item->Cost;
			}
		}
	}
	return Spent;
}

bool ATMBattleDirector::PicksOwnItems(int32 Team) const
{
	return Setup.Mode == TEXT("cpu") || (Setup.Mode == TEXT("ai") && Setup.PlayerTeam != Team);
}

FString ATMBattleDirector::ItemChoiceProblem(int32 SlotCode, const std::string& ItemId) const
{
	if (SlotCode < 0 || SlotCode >= 24)
	{
		return TEXT("No such item slot.");
	}
	if (ItemId.empty())
	{
		return FString();
	}
	const TMSim::FItemDef* Item = TMSim::FindItem(ItemId);
	if (!Item)
	{
		return TEXT("No such item.");
	}
	const int32 Team = SlotCode / 12;
	const int32 Unit = (SlotCode % 12) / 3;
	const int32 Slot = SlotCode % 3;
	if (Item->Cost <= 0)
	{
		return FString::Printf(TEXT("%hs can't be bought: it's only found, from the epic camp."), Item->Name.c_str());
	}
	for (int32 Other = 0; Other < 3; ++Other)
	{
		if (Other != Slot && Setup.Items[Team][Unit][Other] == Item->Id)
		{
			return FString::Printf(TEXT("That unit already carries %hs."), Item->Name.c_str());
		}
	}
	const TMSim::FItemDef* Before = TMSim::FindItem(Setup.Items[Team][Unit][Slot]);
	const int32 Left = Setup.ItemBudget - ItemPointsSpent(Team) + (Before ? Before->Cost : 0);
	if (Item->Cost > Left)
	{
		return FString::Printf(TEXT("%hs costs %d points; this side has %d left."), Item->Name.c_str(), Item->Cost, Left);
	}
	return FString();
}

void ATMBattleDirector::ChooseItem(int32 SlotCode, int32 ItemIndex)
{
	if (SlotCode < 0 || SlotCode >= 24)
	{
		return;
	}
	const std::vector<const TMSim::FItemDef*>& All = TMSim::AllItems();
	const std::string Id = ItemIndex >= 0 && ItemIndex < static_cast<int32>(All.size()) ? All[static_cast<size_t>(ItemIndex)]->Id : std::string();
	const FString Problem = ItemChoiceProblem(SlotCode, Id);
	if (!Problem.IsEmpty())
	{
		Tell(Problem);
		return;
	}
	ItemPickerSlot = -1;
	// Online, in the lobby: the host's to set, and everyone told (v20).
	if (Screen == EScreen::Lobby)
	{
		LobbyItem(SlotCode, Id);
		return;
	}
	Setup.Items[SlotCode / 12][(SlotCode % 12) / 3][SlotCode % 3] = Id;
	BuildBattle();
}

bool ATMBattleDirector::SliderRange(int32 Id, double& Low, double& High, double& Step) const
{
	if (Id == SliderCameraSpeed)
	{
		Low = 0.5;
		High = 2.0;
		Step = 0.05;
		return true;
	}
	if (Id == SliderSfxVolume || Id == SliderVoiceVolume)
	{
		Low = 0.0;
		High = 1.0;
		Step = 0.05;
		return true;
	}
	if (Id == SliderDamageText)
	{
		Low = 0.75;
		High = 3.0;
		Step = 0.05;
		return true;
	}
	if (Id == SliderOverhead || Id == SliderStatusIcons)
	{
		Low = 0.6;
		High = Id == SliderOverhead ? 2.5 : 3.0;
		Step = 0.05;
		return true;
	}
	const int32 Index = Id - SliderTuning;
	if (Index >= 0 && Index < static_cast<int32>(TMSim::TuningKeys().size()))
	{
		const TMSim::FTuningKey& Key = TMSim::TuningKeys()[static_cast<size_t>(Index)];
		Low = Key.Low;
		High = Key.High;
		Step = Key.Step;
		return true;
	}
	return false;
}

double ATMBattleDirector::SliderValue(int32 Id) const
{
	if (Id == SliderCameraSpeed)
	{
		return FTMSettings::Get().CameraSpeed;
	}
	if (Id == SliderOverhead)
	{
		return FTMSettings::Get().OverheadScale;
	}
	if (Id == SliderStatusIcons)
	{
		return FTMSettings::Get().StatusIconScale;
	}
	if (Id == SliderSfxVolume)
	{
		return FTMSettings::Get().SfxVolume;
	}
	if (Id == SliderVoiceVolume)
	{
		return FTMSettings::Get().VoiceVolume;
	}
	if (Id == SliderDamageText)
	{
		return FTMSettings::Get().DamageTextScale;
	}
	const int32 Index = Id - SliderTuning;
	if (Index < 0 || Index >= static_cast<int32>(TMSim::TuningKeys().size()))
	{
		return 0.0;
	}
	const TMSim::FTuningKey& Key = TMSim::TuningKeys()[static_cast<size_t>(Index)];
	if (const double* Saved = FTMSettings::Get().Tuning.Find(UTF8_TO_TCHAR(Key.Key)))
	{
		return *Saved;
	}
	const TMSim::FTuning Defaults = GameTuning();
	return Defaults.*Key.Member;
}

TMSim::FTuning ATMBattleDirector::GameTuning()
{
	// The rules' defaults are Godot's, so the port can be checked against it;
	// the game plays the newer rules on top (2026-10-01: Armor and Resist take
	// a share, one Evasion that dodges or grazes; Docs/design/feat-defense.md).
	// Kept in the rules (TMSim::GameTuning) so the class lab measures classes on
	// the same rules.
	return TMSim::GameTuning();
}

void ATMBattleDirector::SetSlider(int32 Id, double Value)
{
	FTMSettings& Settings = FTMSettings::Get();
	if (Id == SliderCameraSpeed)
	{
		Settings.CameraSpeed = static_cast<float>(Value);
		return;
	}
	if (Id == SliderOverhead)
	{
		Settings.OverheadScale = static_cast<float>(Value);
		return;
	}
	if (Id == SliderStatusIcons)
	{
		Settings.StatusIconScale = static_cast<float>(Value);
		return;
	}
	if (Id == SliderSfxVolume)
	{
		Settings.SfxVolume = static_cast<float>(Value);
		return;
	}
	if (Id == SliderVoiceVolume)
	{
		Settings.VoiceVolume = static_cast<float>(Value);
		return;
	}
	if (Id == SliderDamageText)
	{
		Settings.DamageTextScale = static_cast<float>(Value);
		return;
	}
	const int32 Index = Id - SliderTuning;
	if (Index < 0 || Index >= static_cast<int32>(TMSim::TuningKeys().size()))
	{
		return;
	}
	const TMSim::FTuningKey& Key = TMSim::TuningKeys()[static_cast<size_t>(Index)];
	Settings.Tuning.Add(UTF8_TO_TCHAR(Key.Key), Value);
	// Kept for the next battle; and, in one being fought here, sent to the
	// rules as an order once the dragging stops (dev_tools.gd:8-10, 106-113).
	if (Screen == EScreen::Battle && Battle.Winner == -1)
	{
		TunePending.Add(Index, Value);
		TunePendingFor = 0.25f;
	}
}

void ATMBattleDirector::FlushTuning()
{
	TunePendingFor = -1.0f;
	if (TunePending.Num() == 0)
	{
		return;
	}
	std::vector<std::pair<int, double>> Values;
	for (const TPair<int32, double>& Entry : TunePending)
	{
		Values.push_back({ Entry.Key, Entry.Value });
	}
	TunePending.Reset();
	if (bOnline)
	{
		// battle.gd:150-152, 1226: the host's rule numbers hold for the match.
		Tell(TEXT("The rules can't change during an online match: the change is kept for the next battle."));
		return;
	}
	if (Screen == EScreen::Battle && Battle.Winner == -1)
	{
		const FString Refused = Submit(TMSim::FOrder::MakeTune(Values));
		Tell(Refused.IsEmpty() ? FString(TEXT("Developer Tools: rule numbers updated.")) : Refused);
	}
}

void ATMBattleDirector::ToggleLayout()
{
	bEditingLayout = !bEditingLayout;
	DragPanel.Reset();
	ResizePanel.Reset();
	DragCard = -1;
	if (bEditingLayout)
	{
		CancelAim();
	}
	else
	{
		FTMSettings::Get().Save();
		Tell(TEXT("Layout locked."));
	}
}

void ATMBattleDirector::DropCard()
{
	// hud.gd:386-400: where it is dropped among its side's squares is its new place.
	const int32 Moving = DragCard;
	DragCard = -1;
	const TMSim::FUnit* Unit = Battle.FindUnit(Moving);
	const UWorld* World = GetWorld();
	const APlayerController* Player = World ? World->GetFirstPlayerController() : nullptr;
	const ATMBattleHud* Hud = Player ? Cast<ATMBattleHud>(Player->GetHUD()) : nullptr;
	float X = 0.0f;
	float Y = 0.0f;
	if (!Unit || !Hud || !CursorPosition(X, Y))
	{
		return;
	}
	TArray<TPair<double, int32>> Row;
	for (const TPair<int32, FBox2D>& Square : Hud->SquareAreas)
	{
		const TMSim::FUnit* Other = Battle.FindUnit(Square.Key);
		if (Other && Other->Team == Unit->Team && Other->Id != Moving)
		{
			Row.Add(TPair<double, int32>(Square.Value.GetCenter().X, Square.Key));
		}
	}
	Row.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B) { return A.Key < B.Key; });
	TArray<int32>& Order = FTMSettings::Get().CardOrder[Unit->Team];
	Order.Reset();
	bool bPlaced = false;
	for (const TPair<double, int32>& Entry : Row)
	{
		if (!bPlaced && X < Entry.Key)
		{
			Order.Add(Moving);
			bPlaced = true;
		}
		Order.Add(Entry.Value);
	}
	if (!bPlaced)
	{
		Order.Add(Moving);
	}
	FTMSettings::Get().Save();
}

int32 ATMBattleDirector::PlanningTeam() const
{
	// Nobody watching places anybody: the computer's side is left where it
	// spawned, as in Godot, whose computer player never places or says it is
	// ready -- so against the computer the planning time always runs out.
	if (!bPlayerInput || Screen != EScreen::Battle)
	{
		return -1;
	}
	if (Setup.Mode == TEXT("ai"))
	{
		return Setup.PlayerTeam;
	}
	if (Setup.Mode == TEXT("cpu"))
	{
		return -1;
	}
	if (bOnline)
	{
		return OnlineStopped.IsEmpty() ? LocalTeam : -1;
	}
	// Two people at one machine: Godot places for blue only (battle.gd:351).
	return 0;
}

void ATMBattleDirector::ReadyToFight()
{
	const int32 Team = PlanningTeam();
	if (Battle.IsPlanning() && Team != -1)
	{
		Submit(TMSim::FOrder::MakeReady(Team));
		PlaceId = -1;
	}
}

void ATMBattleDirector::CaptureNamed(const TCHAR* Name)
{
	const FString Where = FPaths::ProjectSavedDir() / TEXT("Match") / Name;
	FScreenshotRequest::RequestScreenshot(Where, true, false);
	UE_LOG(LogTemp, Log, TEXT("CAPTURE %s"), *Where);
}
