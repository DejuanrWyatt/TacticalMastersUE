#include "TMBattleDirector.h"

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
#include "TMAnimStudio.h"
#include "TMRobotPlayer.h"
#include "TMSettings.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Particles/ParticleSystem.h"
#include "Kismet/GameplayStatics.h"
#include "Particles/ParticleSystemComponent.h"
#include "NiagaraComponent.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/DateTime.h"

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

	// -tmanimcatalog: the same, for the animation clips the character map
	// names, filmed on the bodies that wear them (TMAnimStudio.h).
	if (FParse::Param(FCommandLine::Get(), TEXT("tmanimcatalog")))
	{
		LoadCharacterMap();
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
	// Up even while watching: the turn order and the log are how a battle is read.
	ShowHud();
}

void ATMBattleDirector::ClearBattle()
{
	for (TObjectPtr<USkeletalMeshComponent>& Visual : UnitVisuals)
	{
		if (Visual)
		{
			Visual->DestroyComponent();
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
	LoadClassFiles();
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
	Battle.Map.BuildMirrored(MapDef.Top);

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

		Battle.Units.push_back(Unit);
	}

	// The rule numbers chosen in Developer Tools, then the setup screen's own
	// three on top (dev_tools.gd:8: "saved and used by the next battle").
	Battle.Tuning = TMSim::FTuning();
	for (const TMSim::FTuningKey& Key : TMSim::TuningKeys())
	{
		if (const double* Saved = FTMSettings::Get().Tuning.Find(UTF8_TO_TCHAR(Key.Key)))
		{
			Battle.Tuning.*Key.Member = FMath::Clamp(*Saved, Key.Low, Key.High);
		}
	}

	// How the battle can be won is a rule like any other, so it goes to the rules
	// with the battle rather than being watched for here.
	Battle.Tuning.CaptureSeconds = Setup.CaptureSeconds;
	Battle.Tuning.BattleSeconds = Setup.BattleSeconds;
	Battle.Tuning.PlanningSeconds = Setup.PlanningSeconds;
	PlaceId = -1;
	Battle.CaptureTicks[0] = 0;
	Battle.CaptureTicks[1] = 0;

	// The seed is part of the battle: the same seed and the same orders give the
	// same battle. Left alone it is the one the clock was checked against, so a
	// battle nobody set up is the one the Godot game plays.
	Battle.Start(BattleSeed);

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
	for (size_t i = 0; i < Battle.Units.size(); ++i)
	{
		const FName Name = *FString::Printf(TEXT("Unit_%d"), Battle.Units[i].Id);
		USkeletalMeshComponent* Visual = NewObject<USkeletalMeshComponent>(this, Name, RF_Transient);
		Visual->SetupAttachment(RootComponent);
		Visual->RegisterComponent();
		const FTMBody* Body = BodyFor(Battle.Units[i]);
		if (Body && Body->Mesh)
		{
			Visual->SetSkeletalMeshAsset(Body->Mesh);
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
			this, *FString::Printf(TEXT("Plate_%d"), Battle.Units[i].Id), RF_Transient);
		Plate->SetMobility(EComponentMobility::Movable);
		Plate->SetupAttachment(RootComponent);
		Plate->RegisterComponent();
		Plate->SetWorldSize(22.0f);
		Plate->SetHorizontalAlignment(EHTA_Center);
		Plates.Add(Plate);

		UPointLightComponent* Light = NewObject<UPointLightComponent>(
			this, *FString::Printf(TEXT("Ready_%d"), Battle.Units[i].Id), RF_Transient);
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
			this, *FString::Printf(TEXT("Status_%d"), Battle.Units[i].Id), RF_Transient);
		Glow->SetupAttachment(RootComponent);
		Glow->RegisterComponent();
		Glow->SetAttenuationRadius(180.0f);
		Glow->SetCastShadows(false);
		Glow->SetVisibility(false);
		StatusLights.Add(Glow);
	}

	BuildBoard();
	BuildTurnRings();
	BuildIndicators();

	bBuilt = true;
	ResetMotion();
	RefreshVisuals();

	UE_LOG(LogTemp, Log, TEXT("Battle built with %d units"), static_cast<int32>(Battle.Units.size()));
}

FVector ATMBattleDirector::WorldFromMetres(const TMSim::FVec2& Point, int Level) const
{
	// The rules think in metres and Unreal in centimetres, and a height level is
	// a fixed number of metres, so the board's shape comes out of the map rather
	// than being decided here.
	return FVector(Point.X * TileSize, Point.Y * TileSize, Level * TMSim::Ground::LevelHeight * TileSize);
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
				Plate->SetRelativeLocation(WorldFor(Unit) + FVector(0.0f, 0.0f, 150.0f));
			}
			continue;
		}

		FString Line = FString::Printf(TEXT("%hs  %d"), Unit.Job.c_str(), Unit.Hp);
		if (Unit.IsCasting())
		{
			// What it is in the middle of, which is the thing worth reading.
			const TMSim::FAbility* Ability = TMSim::JobAbility(Unit.Job, Unit.Casting.Slot);
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
		Plate->SetRelativeLocation(WorldFor(Unit) + FVector(0.0f, 0.0f, 150.0f));
	}
}

void ATMBattleDirector::RefreshVisuals()
{
	RefreshPlates();
	ApplyOutlines();
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

		if (ReadyLights.IsValidIndex(i) && ReadyLights[i])
		{
			ReadyLights[i]->SetRelativeLocation((bMoving ? Motions[i].Shown : Where) + FVector(0.0f, 0.0f, 55.0f));
			ReadyLights[i]->SetVisibility(Unit.bReady && Unit.IsAlive() && IsSeen(Unit));
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
			const TMSim::FAbility* Ability = TMSim::JobAbility(Unit.Job, Unit.Casting.Slot);
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
	const TMSim::FVec2 To = TMSim::FMap::Snap(TMSim::FVec2(MetresX, MetresY));

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
	const std::string Refused = Battle.Validate(Order);
	if (!Refused.empty())
	{
		return UTF8_TO_TCHAR(Refused.c_str());
	}

	TMSim::FTickReport Report;
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
	Narrate(Report);
	ShowEvents(Report);
	RefreshVisuals();
	return FString();
}

FString ATMBattleDirector::NameOf(int32 UnitId) const
{
	if (const TMSim::FUnit* Unit = const_cast<TMSim::FBattle&>(Battle).FindUnit(UnitId))
	{
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
	CamDistance = CamWantDistance = static_cast<float>((Where - Middle).Size());
	if (!Watcher)
	{
		return;
	}
	if (UCameraComponent* Lens = Watcher->GetCameraComponent())
	{
		Lens->SetFieldOfView(CameraFov);
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
			// Back to whose turn it is, which is what the light is normally for.
			const TMSim::FUnit* Unit = Battle.FindUnit(Index);
			Light->SetLightColor(ReadyColour);
			Light->SetIntensity(ReadyLightBrightness);
			Light->SetVisibility(Unit && Unit->bReady && Unit->IsAlive() && IsSeen(*Unit));
			Flashes.RemoveAt(i);
			continue;
		}
		const float Left = 1.0f - Flash.Age / FlashSeconds;
		Light->SetVisibility(true);
		Light->SetLightColor(Flash.Colour);
		Light->SetIntensity(ReadyLightBrightness * (1.0f + 2.5f * Left));
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
			break;
		case TMSim::EEventKind::CastStarted:
			Line = FString::Printf(TEXT("%s begins casting %hs (%.1fs)"), *NameOf(Event.Unit),
				Event.Id.c_str(), Event.Amount / float(TMSim::Pace::TicksPerSecond));
			break;
		case TMSim::EEventKind::CastFizzled:
			Line = FString::Printf(TEXT("%s's spell fizzles"), *NameOf(Event.Unit));
			break;
		case TMSim::EEventKind::Resolved:
			Line = FString::Printf(TEXT("%s uses %hs"), *NameOf(Event.Unit), Event.Id.c_str());
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
		case TMSim::EEventKind::Critical:
			Line = FString::Printf(TEXT("    critical on %s"), *NameOf(Event.Unit));
			break;
		case TMSim::EEventKind::Absorbed:
			Line = FString::Printf(TEXT("    %s's %hs soaks %d"), *NameOf(Event.Unit),
				Event.Id.c_str(), Event.Amount);
			break;
		case TMSim::EEventKind::StatusApplied:
			Line = FString::Printf(TEXT("    %s takes %hs"), *NameOf(Event.Unit), Event.Id.c_str());
			break;
		case TMSim::EEventKind::GaugeChanged:
			Line = FString::Printf(TEXT("    %s gauge %+d%%"), *NameOf(Event.Unit), Event.Amount);
			break;
		case TMSim::EEventKind::Knocked:
			Line = FString::Printf(TEXT("    %s is knocked out!"), *NameOf(Event.Unit));
			break;
		case TMSim::EEventKind::Revived:
			Line = FString::Printf(TEXT("    %s is back on its feet"), *NameOf(Event.Unit));
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
		Log.Add(FString::Printf(TEXT("%6.1fs  %s"),
			Battle.TickCount / float(TMSim::Pace::TicksPerSecond), *Line));
		UE_LOG(LogTemp, Log, TEXT("%s"), *Line);
	}
	const int32 Spare = Log.Num() - FMath::Max(1, LogLines);
	if (Spare > 0)
	{
		Log.RemoveAt(0, Spare, EAllowShrinking::No);
	}
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
		if (Unit.IsAlive() && Unit.bReady && ComputerPlays(Unit.Team))
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

	const TMSim::FOrder Order = Computers[Unit->Team].NextCommand(Battle, *Unit);
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

void ATMBattleDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// An ultimate slows the world for a beat (UltimateBeat). Only the look of it:
	// the battle's clock below is given real time, so the beat costs nobody a
	// moment of their turn.
	float RealDelta = DeltaSeconds;
	if (GetWorld() && GetWorld()->GetWorldSettings())
	{
		const float Dilation = GetWorld()->GetWorldSettings()->GetEffectiveTimeDilation();
		if (Dilation > 0.01f)
		{
			RealDelta = DeltaSeconds / Dilation;
		}
		if (SlowUntil > 0.0 && FPlatformTime::Seconds() >= SlowUntil)
		{
			SlowUntil = 0.0;
			UGameplayStatics::SetGlobalTimeDilation(this, 1.0f);
		}
	}

	AdvanceFloaters(DeltaSeconds);
	AdvanceVfx(DeltaSeconds);
	if (GetWorld() && GetWorld()->IsGameWorld())
	{
		AdvanceMotion(DeltaSeconds);
		AdvanceTurnRings();
		AdvanceIndicators();
		AdvanceCardPortraits();
		AdvanceBoard(DeltaSeconds);
		UpdateCamera(DeltaSeconds);
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
			MaintainSelection();
		}
		PickUnderCursor();
		UpdateHoverPath();
	}
	UpdateThreat();

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
	if (bPaused || bMenuOpen)
	{
		TickRemainder = 0.0f;
		return;
	}
	FirePendingAbility();

	// The rules run at a fixed rate whatever the frame rate is doing. That is
	// not a detail: the same battle has to play out the same way on both
	// machines in an online match, and on a replay.
	const float SecondsPerTick = 1.0f / static_cast<float>(TMSim::Pace::TicksPerSecond);
	TickRemainder += RealDelta;
	int32 Steps = 0;
	while (TickRemainder >= SecondsPerTick && Steps < 30)
	{
		TickRemainder -= SecondsPerTick;
		++Steps;
	}
	if (Steps > 0)
	{
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
			DecidedFor = 0.0f;
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
	const TMSim::FUnit* Unit = WaitingOnComputer();
	if (!Unit)
	{
		ThinkingAbout = -1;
		return;
	}
	if (ThinkingAbout != Unit->Id)
	{
		ThinkingAbout = Unit->Id;
		ThinkRemainder = static_cast<float>(Computers[Unit->Team].Skill().Think);
	}
	ThinkRemainder -= DeltaSeconds;
	if (ThinkRemainder <= 0.0f)
	{
		const int32 UnitId = Unit->Id;
		if (!TakeComputerTurn(UnitId).IsEmpty())
		{
			EndUnitTurn(UnitId);
		}
		++OrdersGiven;
		ThinkRemainder = static_cast<float>(Computers[Unit->Team].Skill().Step);
	}
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
	for (const FKey& Button : { EKeys::LeftMouseButton, EKeys::RightMouseButton, EKeys::MiddleMouseButton })
	{
		InputComponent->BindKey(Button, IE_Released, this, &ATMBattleDirector::OnKeyUp);
	}
	bPlayerInput = true;
	UE_LOG(LogTemp, Log, TEXT("taking orders from the mouse and keyboard"));
}

void ATMBattleDirector::OnKey(FKey Key)
{
	// battle.gd:699-739, _unhandled_input, with every key read through the
	// player's bindings (FTMSettings) rather than fixed.
	const FTMSettings& Keys = FTMSettings::Get();
	auto Is = [&Keys, &Key](ETMAction Action) { return Keys.Is(Key, Action); };
	const TMSim::FUnit* Sel = SelectedUnit();

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
		return;
	}
	if (Key == EKeys::RightMouseButton)
	{
		bRightHeld = true;
		RightDragged = 0.0f;
		return;
	}
	if (Key == EKeys::MiddleMouseButton)
	{
		bMiddleHeld = true;
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
		if (Screen == EScreen::Battle && Hud && CursorPosition(X, Y) && Hud->LogArea.bIsValid && Hud->LogArea.IsInside(FVector2D(X, Y)))
		{
			// The newest line is never more than a few turns of the wheel away.
			LogScroll = FMath::Clamp(LogScroll + (Key == EKeys::MouseScrollUp ? 1 : -1), 0, FMath::Max(0, Log.Num() - 1));
		}
		else if (!bGuideOpen && PickerSlot < 0)
		{
			CamWantDistance = FMath::Clamp(CamWantDistance * (Key == EKeys::MouseScrollUp ? 0.9f : 1.1f), 400.0f, 8000.0f);
		}
		return;
	}
	// The Unit Guide covers the screen: only its key or Cancel closes it.
	if (bGuideOpen)
	{
		if (Is(ETMAction::UnitGuide) || Is(ETMAction::Cancel))
		{
			ToggleGuide();
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
		else if (Is(ETMAction::Cancel) && Screen == EScreen::Setup)
		{
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
		if (bMenuOpen)
		{
			bMenuOpen = false;
		}
		else if (AimMode != EAimMode::None)
		{
			CancelAim();
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
	if (Is(ETMAction::Ability1)) { SelectAbility(0); }
	else if (Is(ETMAction::Ability2)) { SelectAbility(1); }
	else if (Is(ETMAction::Ability3)) { SelectAbility(2); }
	else if (Is(ETMAction::Ability4)) { SelectAbility(3); }
	else if (Is(ETMAction::Move))
	{
		// battle.gd:966-972, _toggle_move.
		if (!PlayerCanOrder(Sel))
		{
			return;
		}
		if (AimMode == EAimMode::Move && !bSprinting)
		{
			CancelAim();
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
		if (!PlayerCanOrder(Sel))
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
			Tell(TEXT("Already used an ability this turn: no sprinting."));
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
		if (Battle.Winner == -1)
		{
			bPaused = !bPaused;
			Tell(bPaused ? TEXT("Paused.") : TEXT("Resumed."));
		}
	}
	else if (Key == EKeys::R && Battle.Winner != -1)
	{
		// Rematch, only once a battle is decided, so a stray key cannot throw
		// one away; while it is being fought R raises the camera.
		StartMatch(true);
	}
}

void ATMBattleDirector::OnKeyUp(FKey Key)
{
	if (Key == EKeys::RightMouseButton)
	{
		bRightHeld = false;
		// A click rather than a drag: it drops an aim, as it always has.
		if (RightDragged < 6.0f && Screen == EScreen::Battle && !bMenuOpen && !bGuideOpen && !bOptionsOpen && !bDevToolsOpen)
		{
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
	return Unit && Unit->IsAlive() && Unit->bReady && !ComputerPlays(Unit->Team)
		&& Battle.Winner == -1 && !bPaused && !bMenuOpen && Screen == EScreen::Battle;
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
		if (PlayerCanOrder(&Unit) && (!Best || ActsSooner(&Unit, Best)))
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
		if (PlayerCanOrder(&Unit))
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
	SelectUnit(Ready[Next]->Id);
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
	Reachable = Battle.ReachableNodes(*Unit, bSprint);
	PathNode = TMSim::FNode{ -9999, -9999 };
	PathShown.clear();
}

void ATMBattleDirector::SelectAbility(int32 Slot)
{
	// battle.gd:990-1006. The reason an ability cannot be used is the rules'
	// (AbilityBlockedReason), so the words are the same ones a refused order gets.
	const TMSim::FUnit* Unit = SelectedUnit();
	if (!PlayerCanOrder(Unit))
	{
		return;
	}
	if (AimMode == EAimMode::Ability && AimSlot == Slot)
	{
		CancelAim();
		return;
	}
	if (Unit->bActed)
	{
		Tell(TEXT("Already used an ability this turn."));
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
}

void ATMBattleDirector::OrderSelected(const TMSim::FOrder& Order)
{
	// battle.gd:389-395, then what it does after the order lands (:468-485).
	const FString Refused = Submit(Order);
	if (!Refused.IsEmpty())
	{
		Tell(Refused);
		return;
	}
	const TMSim::FUnit* Unit = SelectedUnit();
	if (!Unit || Order.UnitId != Unit->Id)
	{
		return;
	}
	if (!Unit->IsAlive() || !Unit->bReady || Battle.Winner != -1)
	{
		Deselect();
		AutoSelect();
		return;
	}
	// Acted, and walked or started a cast: nothing is left to do with the turn,
	// so it is ended rather than left to run out.
	if (Unit->bActed && (Unit->bMoved || Unit->IsCasting()))
	{
		OrderSelected(TMSim::FOrder::MakeEndTurn(Unit->Id, Unit->Serial));
		return;
	}
	AimMode = EAimMode::None;
	AimSlot = -1;
	bSprinting = false;
	if (!Unit->bMoved)
	{
		EnterMoveMode(false);
	}
}

void ATMBattleDirector::MaintainSelection()
{
	// Time runs while a person thinks, so the unit they were ordering can time
	// out, be knocked down or be stunned under them (battle.gd:468-471, 484-485).
	const TMSim::FUnit* Unit = SelectedUnit();
	if (Unit && (!Unit->IsAlive() || !Unit->bReady || ComputerPlays(Unit->Team) || Battle.Winner != -1))
	{
		Deselect();
		Unit = nullptr;
	}
	else if (Unit && Unit->Serial != SelectedSerial)
	{
		// Its turn ended and a new one began (Relentless): start that one afresh.
		SelectUnit(Unit->Id);
		Unit = SelectedUnit();
	}
	if (SelectedId == -1 && !bPaused)
	{
		AutoSelect();
		Unit = SelectedUnit();
	}
	// Somebody else moved or fell, so the ground this unit can reach has changed
	// (battle.gd:482-483).
	if (OrdersSeen != OrdersApplied)
	{
		OrdersSeen = OrdersApplied;
		if (Unit && AimMode == EAimMode::Move)
		{
			Reachable = Battle.ReachableNodes(*Unit, bSprinting);
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
	int32 Best = -1;
	double BestPixels = 36.0;
	// Whether the pointer is on the nearest unit's body itself, not only near it.
	bool bOnBody = false;
	for (const TMSim::FUnit& Unit : Battle.Units)
	{
		if ((!Unit.IsAlive() && !Unit.IsKo()) || !IsSeen(Unit))
		{
			continue;
		}
		// A fallen unit is lying down, and only its plate is shown.
		const float Up = Unit.IsAlive() ? 90.0f : 20.0f;
		FVector2D OnScreen;
		const FVector Drawn = ShownAt(Unit);
		if (Player->ProjectWorldLocationToScreen(
			GetActorTransform().TransformPosition(Drawn + FVector(0.0f, 0.0f, Up)), OnScreen))
		{
			const double Pixels = FVector2D::Distance(OnScreen, Mouse);
			if (Pixels < BestPixels)
			{
				BestPixels = Pixels;
				Best = Unit.Id;
				// The body as the screen shows it: feet to head, about a quarter
				// as wide as it is tall. Godot picks a unit by a ray meeting its
				// body (battle.gd:741-753), and this is that body, drawn flat.
				FVector2D Feet;
				FVector2D Head;
				bOnBody = false;
				if (Unit.IsAlive()
					&& Player->ProjectWorldLocationToScreen(GetActorTransform().TransformPosition(Drawn), Feet)
					&& Player->ProjectWorldLocationToScreen(GetActorTransform().TransformPosition(Drawn + FVector(0.0f, 0.0f, 180.0f)), Head))
				{
					const FVector2D Along = Head - Feet;
					const double Length = Along.Size();
					if (Length > 1.0)
					{
						const double T = FMath::Clamp(FVector2D::DotProduct(Mouse - Feet, Along) / (Length * Length), 0.0, 1.0);
						bOnBody = FVector2D::Distance(Mouse, Feet + Along * T) <= Length * 0.25;
					}
				}
				else if (!Unit.IsAlive())
				{
					bOnBody = Pixels <= 18.0;
				}
			}
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
		return;
	}
	if (bGround)
	{
		bHaveHover = true;
		HoverPoint = Ground;
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
	}
}

ATMBattleDirector::FAim ATMBattleDirector::Aim()
{
	// battle.gd:810-833, _aim. Pointing at a unit aims at that unit, and a cast
	// follows it; pointing at the ground aims at that spot.
	FAim Out;
	const TMSim::FUnit* Unit = SelectedUnit();
	const TMSim::FAbility* Ability = Unit ? TMSim::JobAbility(Unit->Job, AimSlot) : nullptr;
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
			PressHudButton(Button);
			return;
		}
	}
	// Behind a menu or the guide, or while the screen is being arranged, the
	// board takes no clicks.
	if (Screen != EScreen::Battle || bMenuOpen || bGuideOpen || bEditingLayout)
	{
		return;
	}
	PickUnderCursor();
	if (!bHaveHover)
	{
		return;
	}
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
	if (PlayerCanOrder(Unit))
	{
		if (AimMode == EAimMode::Move)
		{
			const TMSim::FNode Node = TMSim::FMap::NodeOf(HoverPoint);
			const bool bReachable = std::any_of(Reachable.begin(), Reachable.end(),
				[&Node](const std::pair<TMSim::FNode, double>& Entry) { return Entry.first == Node; });
			if (bReachable && Node != TMSim::FMap::NodeOf(Unit->Pos))
			{
				OrderSelected(TMSim::FOrder::MakeMove(Unit->Id, Unit->Serial, TMSim::FMap::NodePos(Node), bSprinting));
				return;
			}
		}
		else if (AimMode == EAimMode::Ability)
		{
			const FAim Where = Aim();
			if (Where.bOk)
			{
				OrderSelected(TMSim::FOrder::MakeUseAbility(Unit->Id, Unit->Serial, AimSlot, Where.Point, Where.Follow));
			}
			else if (Where.Why == UTF8_TO_TCHAR(OutOfRange) && WalkIntoRange(*Unit, Where.Point))
			{
				// Walking there first; the ability goes off on arrival (battle.gd:791).
			}
			else if (!Where.Why.IsEmpty())
			{
				Tell(Where.Why);
			}
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
	else if (Clicked && Clicked != Unit && IsSeen(*Clicked))
	{
		InspectedId = InspectedId == Clicked->Id ? -1 : Clicked->Id;
	}
	else if (!Clicked)
	{
		InspectedId = -1;
	}
}

void ATMBattleDirector::Tell(const FString& What)
{
	Notice = What;
	NoticeLeft = 3.0f;
	UE_LOG(LogTemp, Log, TEXT("player: %s"), *What);
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
	const TMSim::FAbility* Ability = TMSim::JobAbility(Unit.Job, Slot);
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
		if (TMSim::NeedsLineOfSight(*Ability) && !Battle.HasLineOfSight(Spot, Point))
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

bool ATMBattleDirector::WalkIntoRange(const TMSim::FUnit& Unit, const TMSim::FVec2& Point)
{
	// battle.gd:838-857. The unit is on its way while time runs on, so the
	// ability goes off at the spot the target was standing on: if it has moved
	// by then, the blow lands on empty ground.
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
	const FPendingAbility Order = PendingAbility;
	PendingAbility = FPendingAbility();
	OrderSelected(TMSim::FOrder::MakeUseAbility(Order.UnitId, Order.Serial, Order.Slot, Order.Target, -1));
}

void ATMBattleDirector::UpdateHoverPath()
{
	// The HUD draws the way there; it is worked out here, and only when the spot
	// under the pointer changes, because it is a search over the whole grid.
	const TMSim::FUnit* Unit = SelectedUnit();
	if (PlayerCanOrder(Unit) && AimMode == EAimMode::Ability && bHaveHover && !Unit->bMoved)
	{
		// Out of range, but usable from somewhere it can walk: show that walk
		// (battle.gd:1438-1447).
		const TMSim::FNode Node = TMSim::FMap::NodeOf(HoverPoint);
		if (Node == PathNode)
		{
			return;
		}
		PathNode = Node;
		PathShown.clear();
		const FAim Where = Aim();
		TMSim::FVec2 Spot;
		double Walk = 0.0;
		if (!Where.bOk && Where.Why == UTF8_TO_TCHAR(OutOfRange) && ClosestSpotInRange(*Unit, AimSlot, Where.Point, Spot, Walk))
		{
			PathShown = Battle.PathTo(*Unit, TMSim::FMap::NodeOf(Spot), false);
		}
		return;
	}
	if (!PlayerCanOrder(Unit) || AimMode != EAimMode::Move || !bHaveHover)
	{
		PathShown.clear();
		PathNode = TMSim::FNode{ -9999, -9999 };
		return;
	}
	const TMSim::FNode Node = TMSim::FMap::NodeOf(HoverPoint);
	if (Node == PathNode)
	{
		return;
	}
	PathNode = Node;
	PathShown.clear();
	const bool bReachable = std::any_of(Reachable.begin(), Reachable.end(),
		[&Node](const std::pair<TMSim::FNode, double>& Entry) { return Entry.first == Node; });
	if (bReachable)
	{
		PathShown = Battle.PathTo(*Unit, Node, bSprinting);
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
	case ETMHudAction::PickUnit:
	{
		// A chip on the turn order: take that unit up if it can be ordered
		// (battle.gd:902-910, _on_chip_pressed).
		const TMSim::FUnit* Picked = FindIn(Battle, Button.Value);
		if (Picked && PlayerCanOrder(Picked) && Picked->Id != SelectedId)
		{
			SelectUnit(Picked->Id);
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
		GuideJob = Button.Value;
		break;
	case ETMHudAction::GuideAgainst:
		GuideAgainst = (GuideAgainst + 1) % FMath::Max(1, static_cast<int32>(TMSim::AllJobs().size()));
		break;
	default:
		PressMenuButton(Button);
		break;
	}
}
// ================================================================ match flow

void ATMBattleDirector::StartMatch(bool bNewSeed)
{
	// Who plays each side (game_config.gd:219-231, ai_teams).
	if (Setup.Mode == TEXT("hotseat"))
	{
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
	OrdersGiven = 0;
	DecidedFor = 0.0f;

	auto Who = [this](int32 Team)
	{
		return ComputerPlays(Team)
			? FString::Printf(TEXT("the computer (%s)"), *Setup.Difficulty[Team])
			: FString(TEXT("a person"));
	};
	const FString Opening = FString::Printf(TEXT("Seed %llu. Blue: %s. Red: %s."),
		BattleSeed, *Who(0), *Who(1));
	Log.Add(Opening);
	UE_LOG(LogTemp, Log, TEXT("%s"), *Opening);
}

void ATMBattleDirector::OpenTitle()
{
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
	BuildBattle();
}

void ATMBattleDirector::RandomTeam(int32 Team)
{
	// class_list.gd:62-77, sensible_team: a class for each wanted role, preferring
	// one not already picked so the team is not four of a kind. The dice here are
	// the menu's own, nothing to do with a battle.
	static FRandomStream Pick(static_cast<int32>(FDateTime::UtcNow().GetTicks() & 0x7FFFFFFF));
	const char* Wanted[4] = { "tank", "damage", "damage", "support" };
	std::vector<std::string> Chosen;
	for (const char* WantedRole : Wanted)
	{
		std::vector<std::string> Fresh;
		std::vector<std::string> Any;
		for (const TMSim::FJobDef* Job : TMSim::AllJobs())
		{
			if (!TMSim::JobHasRole(Job->Id, WantedRole))
			{
				continue;
			}
			Any.push_back(Job->Id);
			if (std::find(Chosen.begin(), Chosen.end(), Job->Id) == Chosen.end())
			{
				Fresh.push_back(Job->Id);
			}
		}
		const std::vector<std::string>& From = !Fresh.empty() ? Fresh : Any;
		if (From.empty())
		{
			Chosen.push_back(TMSim::AllJobs().front()->Id);
			continue;
		}
		Chosen.push_back(From[Pick.RandRange(0, static_cast<int32>(From.size()) - 1)]);
	}
	for (int32 Slot = 0; Slot < 4; ++Slot)
	{
		Setup.Rosters[Team][Slot] = Chosen[Slot];
	}
	BuildBattle();
}

void ATMBattleDirector::PressMenuButton(const FTMHudButton& Button)
{
	static const TCHAR* Levels[3] = { TEXT("easy"), TEXT("medium"), TEXT("hard") };
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
		if (PickerSlot >= 0 && Button.Value >= 0 && Button.Value < static_cast<int32>(Jobs.size()))
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
		bBuilt = false;
		break;
	}
	case ETMHudAction::SetupTheme:
	{
		// The map's own look first, then each theme.
		LoadThemes();
		int32 At = ThemeIds.IndexOfByKey(Setup.ThemeId);
		Setup.ThemeId = At + 1 < ThemeIds.Num() ? ThemeIds[At + 1] : FString();
		bBuilt = false;
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
	case ETMHudAction::OptionTurnSquares:
		FTMSettings::Get().bTurnSquares = !FTMSettings::Get().bTurnSquares;
		FTMSettings::Get().Save();
		break;
	case ETMHudAction::OpenOptions:
		bOptionsOpen = true;
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
		FTMSettings::Get().bFullscreen = !FTMSettings::Get().bFullscreen;
		FTMSettings::Get().Save();
		FTMSettings::Get().Apply();
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
		const TMSim::FTuning Defaults;
		const TMSim::FTuningKey& Key = TMSim::TuningKeys()[static_cast<size_t>(Button.Value)];
		SetSlider(SliderTuning + Button.Value, Defaults.*Key.Member);
		FTMSettings::Get().Tuning.Remove(UTF8_TO_TCHAR(Key.Key));
		FTMSettings::Get().Save();
		break;
	}
	case ETMHudAction::DevResetAll:
	{
		const TMSim::FTuning Defaults;
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
		StartMatch(true);
		break;
	case ETMHudAction::SetupBack:
		OpenTitle();
		break;

	// The menu inside a battle, and the end of one.
	case ETMHudAction::MenuResume:
		bMenuOpen = false;
		break;
	case ETMHudAction::MenuRestart:
		// The same battle again from the start: same classes, same seed.
		StartMatch(false);
		break;
	case ETMHudAction::MenuSetup:
		OpenSetup();
		break;
	case ETMHudAction::MenuTitle:
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
	if (Screen != EScreen::Battle || ComputerPlays(0) == ComputerPlays(1))
	{
		return -1;
	}
	return ComputerPlays(0) ? 1 : 0;
}

bool ATMBattleDirector::IsSeen(const TMSim::FUnit& Unit) const
{
	const int32 Viewer = ViewerTeam();
	return Viewer < 0 || Battle.Winner != -1 || Unit.Team == Viewer || Battle.CanSee(Viewer, Unit.Pos);
}

bool ATMBattleDirector::IsPointSeen(const TMSim::FVec2& Point) const
{
	const int32 Viewer = ViewerTeam();
	return Viewer < 0 || Battle.Winner != -1 || Battle.CanSee(Viewer, Point);
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
		const TMSim::FAbility* Ability = TMSim::JobAbility(Unit->Job, Slot);
		if (Ability && Ability->Effect == TMSim::EEffect::Damage)
		{
			ThreatReach = FMath::Max(ThreatReach, Ability->MaxRange);
		}
	}
}

void ATMBattleDirector::ToggleGuide()
{
	bGuideOpen = !bGuideOpen;
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
					GuideJob = static_cast<int32>(i);
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
	Watcher->SetActorLocationAndRotation(CamTarget - Looking.Vector() * CamDistance, Looking);
}

void ATMBattleDirector::CenterCamera()
{
	const TMSim::FUnit* On = SelectedUnit();
	if (!On)
	{
		for (const TMSim::FUnit& Unit : Battle.Units)
		{
			if (Unit.IsAlive() && Unit.bReady && !ComputerPlays(Unit.Team))
			{
				On = &Unit;
				break;
			}
		}
	}
	if (On)
	{
		CamWantTarget = GetActorTransform().TransformPosition(ShownAt(*On));
	}
}

bool ATMBattleDirector::OnSetupScreen(int32 TuningIndex)
{
	const std::string Key = TMSim::TuningKeys()[static_cast<size_t>(TuningIndex)].Key;
	return Key == "capture_seconds" || Key == "battle_seconds" || Key == "planning_seconds";
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
	const TMSim::FTuning Defaults;
	return Defaults.*Key.Member;
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
