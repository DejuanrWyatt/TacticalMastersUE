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
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/DateTime.h"

#include "SimAbility.h"

#include <algorithm>

ATMBattleDirector::ATMBattleDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ATMBattleDirector::BeginPlay()
{
	Super::BeginPlay();
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
	if (bKeepBlue)
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
	for (TObjectPtr<UPointLightComponent>& Light : ReadyLights)
	{
		if (Light) { Light->DestroyComponent(); }
	}
	ReadyLights.Reset();
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
}

void ATMBattleDirector::BuildBattle()
{
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
	Battle.Map.BuildMirrored(TMSim::HighlandsRows());

	// Where the maps put the two sides, in metres. Blue as written, red at the
	// mirrored spots, which is how a symmetric map is laid out.
	const TMSim::FVec2 BlueSpawns[4] =
	{
		TMSim::FVec2(2.75f, 4.75f), TMSim::FVec2(0.75f, 8.75f),
		TMSim::FVec2(4.75f, 6.75f), TMSim::FVec2(2.75f, 10.75f)
	};
	const TMSim::FVec2 Size = Battle.Map.SizeMeters();

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
		// Facing the middle, as a unit does when a battle opens.
		const TMSim::FVec2 Middle(Size.X * 0.5f, Size.Y * 0.5f);
		Unit.Facing = (Middle - Unit.Pos).Normalized();

		Battle.Units.push_back(Unit);
	}

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
	for (size_t i = 0; i < Battle.Units.size(); ++i)
	{
		const FName Name = *FString::Printf(TEXT("Unit_%d"), Battle.Units[i].Id);
		USkeletalMeshComponent* Visual = NewObject<USkeletalMeshComponent>(this, Name, RF_Transient);
		Visual->SetupAttachment(RootComponent);
		Visual->RegisterComponent();
		if (Mesh)
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
	}

	BuildBoard();

	bBuilt = true;
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

void ATMBattleDirector::BuildBoard()
{
	UStaticMesh* Mesh = TileMesh.LoadSynchronous();
	if (!Mesh)
	{
		Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube"));
	}
	if (!Mesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("No tile mesh; the board will not be built"));
		return;
	}

	const float TileMetres = TMSim::Ground::TileSize;
	for (int32 Y = 0; Y < Battle.Map.TilesY; ++Y)
	{
		for (int32 X = 0; X < Battle.Map.TilesX; ++X)
		{
			const int Level = Battle.Map.TileLevel(X, Y);
			if (Level <= 0)
			{
				continue;  // water and rock: nothing to stand on
			}

			UStaticMeshComponent* Tile = NewObject<UStaticMeshComponent>(
				this, *FString::Printf(TEXT("Tile_%d_%d"), X, Y),
				RF_Transient);
			// Movable, and set before attaching. A static component refuses to
			// attach to a movable parent outright -- it does not warn and carry
			// on, it aborts the attach -- so a static tile built here ends up
			// unparented, and the board comes apart when the director moves.
			//
			// Transient too, along with the units and their lights. All of it is
			// built from the simulation whenever a battle starts, so saving it
			// into the level only leaves a stale copy to be loaded and thrown
			// away next time -- which is exactly what the flood of attach
			// warnings on load turned out to be.
			Tile->SetMobility(EComponentMobility::Movable);
			Tile->SetupAttachment(RootComponent);
			Tile->RegisterComponent();
			Tile->SetStaticMesh(Mesh);

			// A column from the ground up to this tile's height, so there is no
			// daylight under the edge of a raised one.
			const float TopMetres = Level * TMSim::Ground::LevelHeight;
			const FVector Centre(
				(X + 0.5f) * TileMetres * TileSize,
				(Y + 0.5f) * TileMetres * TileSize,
				TopMetres * TileSize * 0.5f);
			Tile->SetRelativeLocation(Centre);
			// The engine cube is 100 units across; a hair under a tile leaves a
			// seam, so the grid reads without anything drawn on it.
			const float Across = TileMetres * TileSize / 100.0f;
			Tile->SetRelativeScale3D(FVector(Across * 0.98f, Across * 0.98f, TopMetres * TileSize / 100.0f));
			TileVisuals.Add(Tile);
		}
	}
	UE_LOG(LogTemp, Log, TEXT("Board built: %d tiles"), TileVisuals.Num());
}

void ATMBattleDirector::RefreshPlates()
{
	// Who each one is and how it is doing, over its head. Every unit wears the
	// same borrowed mesh, so without this the two sides are indistinguishable and
	// a match is unreadable however good the lighting gets.
	for (int32 i = 0; i < Plates.Num() && i < static_cast<int32>(Battle.Units.size()); ++i)
	{
		if (!Plates[i])
		{
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
		Plate->SetTextRenderColor(Unit.Team == 0 ? FColor(120, 180, 255) : FColor(255, 130, 120));
		// Fog of war: what this side cannot see, this screen does not show.
		Plate->SetVisibility(IsSeen(Unit));
		Plate->SetRelativeLocation(WorldFor(Unit) + FVector(0.0f, 0.0f, 150.0f));
	}
}

void ATMBattleDirector::RefreshVisuals()
{
	RefreshPlates();
	for (int32 i = 0; i < UnitVisuals.Num() && i < static_cast<int32>(Battle.Units.size()); ++i)
	{
		if (!UnitVisuals[i])
		{
			continue;
		}
		const TMSim::FUnit& Unit = Battle.Units[i];
		const FVector Where = WorldFor(Unit);
		UnitVisuals[i]->SetRelativeLocation(Where);
		// A Paragon mesh does not face along its actor's +X, hence the offset.
		const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Unit.Facing.Y, Unit.Facing.X)) + 180.0f;
		UnitVisuals[i]->SetRelativeRotation(FRotator(0.0f, Yaw, 0.0f));
		UnitVisuals[i]->SetVisibility(Unit.IsAlive() && IsSeen(Unit));

		if (ReadyLights.IsValidIndex(i) && ReadyLights[i])
		{
			ReadyLights[i]->SetRelativeLocation(Where + FVector(0.0f, 0.0f, 55.0f));
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

	for (const TMSim::FEvent& Event : Report.Events)
	{
		FString What;
		FColor Tint = FColor::White;
		switch (Event.Kind)
		{
		case TMSim::EEventKind::Hit:
			// Damage has somebody behind it; healing and the ground do not, which
			// is the difference between a number going down and one going up.
			if (Event.By >= 0)
			{
				What = FString::Printf(TEXT("-%d"), Event.Amount);
				Tint = FColor(255, 115, 90);
			}
			else
			{
				What = FString::Printf(TEXT("+%d"), Event.Amount);
				Tint = FColor(115, 255, 128);
			}
			break;
		case TMSim::EEventKind::Evaded:
			What = TEXT("miss");
			Tint = FColor(215, 224, 255);
			break;
		case TMSim::EEventKind::Critical:
			What = TEXT("critical!");
			Tint = FColor(255, 217, 77);
			break;
		case TMSim::EEventKind::Absorbed:
			What = FString::Printf(TEXT("soaked %d"), Event.Amount);
			Tint = FColor(153, 217, 255);
			break;
		case TMSim::EEventKind::StatusApplied:
			What = UTF8_TO_TCHAR(Event.Id.c_str());
			Tint = FColor(224, 153, 255);
			break;
		case TMSim::EEventKind::Knocked:
			What = TEXT("down");
			Tint = FColor(255, 77, 77);
			break;
		case TMSim::EEventKind::Revived:
			What = TEXT("up again");
			Tint = FColor(255, 242, 153);
			break;
		default:
			break;
		}
		if (What.IsEmpty())
		{
			continue;
		}

		const TMSim::FUnit* Unit = Battle.FindUnit(Event.Unit);
		// Nothing rises off a unit this side cannot see (battle.gd:450-452).
		if (!Unit || !IsSeen(*Unit))
		{
			continue;
		}
		// Above the head, and nudged along by however many are already in flight
		// for this unit, so two numbers in the same instant do not sit on top of
		// one another.
		int32 Stacked = 0;
		for (const FTMFloater& Other : Floaters)
		{
			if (Other.UnitId == Event.Unit)
			{
				++Stacked;
			}
		}
		const FVector Where = WorldFor(*Unit)
			+ FVector(0.0f, 0.0f, 190.0f + Stacked * 26.0f);

		UTextRenderComponent* Text = NewObject<UTextRenderComponent>(this, NAME_None, RF_Transient);
		Text->SetMobility(EComponentMobility::Movable);
		Text->SetupAttachment(RootComponent);
		Text->RegisterComponent();
		Text->SetText(FText::FromString(What));
		Text->SetTextRenderColor(Tint);
		Text->SetWorldSize(FloaterSize);
		Text->SetHorizontalAlignment(EHTA_Center);
		Text->SetRelativeLocation(Where);
		// Facing the camera is a per-frame job; billboarded below in Tick.
		Floaters.Add({ Text, Event.Unit, 0.0f });
		++NumbersShown;

		if (Event.Kind == TMSim::EEventKind::Hit && Event.By >= 0)
		{
			Flashes.Add({ Event.Unit, 0.0f });
			// Worth a picture: a timed capture almost never lands on the second
			// and a half a number is up for, so the interesting frames were all
			// of eight people standing about.
			bWorthSeeing = true;
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
		Light->SetLightColor(FLinearColor(1.0f, 0.25f, 0.2f));
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
				Event.By >= 0 ? TEXT("-") : TEXT("+"), Event.Amount);
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
			Line = Event.Unit == TMSim::FBattle::Draw
				? FString(TEXT("Nobody is left standing."))
				: FString::Printf(TEXT("Team %d wins."), Event.Unit);
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

	AdvanceFloaters(DeltaSeconds);
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
			else if (MenuShotsAt >= 3.0f)
			{
				MenuShotsAt = -1.0f;
				StartMatch(false);
			}
		}
		return;
	}

	if (bPlayerInput)
	{
		MaintainSelection();
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

	// The rules run at a fixed rate whatever the frame rate is doing. That is
	// not a detail: the same battle has to play out the same way on both
	// machines in an online match, and on a replay.
	const float SecondsPerTick = 1.0f / static_cast<float>(TMSim::Pace::TicksPerSecond);
	TickRemainder += DeltaSeconds;
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
				TEXT("BATTLE OVER: winner %d after %.1fs, %d orders, %d numbers shown"),
				Battle.Winner, Battle.TickCount / float(TMSim::Pace::TicksPerSecond),
				OrdersGiven, NumbersShown);
			UE_LOG(LogTemp, Log, TEXT("%s"), *DescribeBattle());
			DecidedFor = 0.0f;
		}
		if (FApp::IsUnattended())
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

	// The Godot game's default keys (keybinds.gd:17-30), less the camera and the
	// windows this port does not have yet. Bound to keys directly rather than
	// through input assets, which would be editor work.
	const FKey Keys[] =
	{
		EKeys::LeftMouseButton, EKeys::RightMouseButton,
		EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four,
		EKeys::SpaceBar, EKeys::LeftShift, EKeys::Enter, EKeys::Tab, EKeys::Escape,
		EKeys::P, EKeys::R, EKeys::L, EKeys::U, EKeys::MouseScrollUp, EKeys::MouseScrollDown
	};
	for (const FKey& Key : Keys)
	{
		InputComponent->BindKey(Key, IE_Pressed, this, &ATMBattleDirector::OnKey);
	}
	bPlayerInput = true;
	UE_LOG(LogTemp, Log, TEXT("taking orders from the mouse and keyboard"));
}

void ATMBattleDirector::OnKey(FKey Key)
{
	// battle.gd:699-739, _unhandled_input.
	const TMSim::FUnit* Sel = SelectedUnit();
	if (Key == EKeys::LeftMouseButton)
	{
		OnClick();
		return;
	}
	// The Unit Guide covers the screen: only U or Esc closes it.
	if (bGuideOpen)
	{
		if (Key == EKeys::U || Key == EKeys::Escape)
		{
			ToggleGuide();
		}
		return;
	}
	// Away from a battle only the mouse and Esc mean anything: Esc steps back
	// from the setup to the title.
	if (Screen != EScreen::Battle)
	{
		if (Key == EKeys::Escape && Screen == EScreen::Setup)
		{
			OpenTitle();
		}
		else if (Key == EKeys::U)
		{
			ToggleGuide();
		}
		return;
	}
	if (Key == EKeys::Escape)
	{
		// Esc cancels an aim first; with nothing to cancel it opens the menu
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
	if (Key == EKeys::RightMouseButton)
	{
		CancelAim();
	}
	else if (Key == EKeys::One) { SelectAbility(0); }
	else if (Key == EKeys::Two) { SelectAbility(1); }
	else if (Key == EKeys::Three) { SelectAbility(2); }
	else if (Key == EKeys::Four) { SelectAbility(3); }
	else if (Key == EKeys::SpaceBar)
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
	else if (Key == EKeys::LeftShift)
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
	else if (Key == EKeys::Enter)
	{
		if (PlayerCanOrder(Sel))
		{
			OrderSelected(TMSim::FOrder::MakeEndTurn(Sel->Id, Sel->Serial));
		}
	}
	else if (Key == EKeys::L)
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
	else if (Key == EKeys::U)
	{
		ToggleGuide();
	}
	else if (Key == EKeys::MouseScrollUp || Key == EKeys::MouseScrollDown)
	{
		// The wheel scrolls the log back through the fight; the newest line is
		// never more than a few turns of the wheel away.
		LogScroll = FMath::Clamp(LogScroll + (Key == EKeys::MouseScrollUp ? 1 : -1), 0, FMath::Max(0, Log.Num() - 1));
	}
	else if (Key == EKeys::Tab)
	{
		CycleReady();
	}
	else if (Key == EKeys::P)
	{
		if (Battle.Winner == -1)
		{
			bPaused = !bPaused;
			Tell(bPaused ? TEXT("Paused.") : TEXT("Resumed."));
		}
	}
	else if (Key == EKeys::R)
	{
		// Only once a battle is decided, so a stray key cannot throw one away.
		if (Battle.Winner != -1)
		{
			StartMatch(true);
		}
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
	if (!Player || !Player->GetMousePosition(MouseX, MouseY))
	{
		return;
	}
	const FVector2D Mouse(MouseX, MouseY);

	// Units by where they appear on screen. The camera looks down at an angle, so
	// a ray through a unit's chest meets the board well behind its feet; asking
	// the screen is what makes clicking a unit land on that unit.
	int32 Best = -1;
	double BestPixels = 36.0;
	for (const TMSim::FUnit& Unit : Battle.Units)
	{
		if ((!Unit.IsAlive() && !Unit.IsKo()) || !IsSeen(Unit))
		{
			continue;
		}
		// A fallen unit is lying down, and only its plate is shown.
		const float Up = Unit.IsAlive() ? 90.0f : 20.0f;
		FVector2D OnScreen;
		if (Player->ProjectWorldLocationToScreen(
			GetActorTransform().TransformPosition(WorldFor(Unit) + FVector(0.0f, 0.0f, Up)), OnScreen))
		{
			const double Pixels = FVector2D::Distance(OnScreen, Mouse);
			if (Pixels < BestPixels)
			{
				BestPixels = Pixels;
				Best = Unit.Id;
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

	// Walking wants the ground under the pointer; aiming and picking want the unit.
	if (Best >= 0 && !(AimMode == EAimMode::Move && bGround))
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
		if (Best >= 0)
		{
			HoverUnitId = Best;
		}
		else if (const TMSim::FUnit* Near = Battle.UnitNear(Ground, 0.5f))
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
	if (Player && Player->GetMousePosition(MouseX, MouseY))
	{
		FTMHudButton Button;
		const ATMBattleHud* Hud = Cast<ATMBattleHud>(Player->GetHUD());
		if (Hud && Hud->ButtonAt(FVector2D(MouseX, MouseY), Button))
		{
			PressHudButton(Button);
			return;
		}
	}
	// Behind a menu or the guide, the board takes no clicks.
	if (Screen != EScreen::Battle || bMenuOpen || bGuideOpen)
	{
		return;
	}
	PickUnderCursor();
	if (!bHaveHover)
	{
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
			else if (!Where.Why.IsEmpty())
			{
				// Godot walks into range and fires on arrival when the target is
				// merely too far (battle.gd:791, _walk_into_range). Not ported yet:
				// here the person walks there first and aims again.
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

void ATMBattleDirector::UpdateHoverPath()
{
	// The HUD draws the way there; it is worked out here, and only when the spot
	// under the pointer changes, because it is a search over the whole grid.
	const TMSim::FUnit* Unit = SelectedUnit();
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
	{
		// Each click moves a slot on to the next class.
		const int32 Team = Button.Value / 4;
		const int32 Slot = Button.Value % 4;
		const std::vector<const TMSim::FJobDef*>& Jobs = TMSim::AllJobs();
		size_t Next = 0;
		for (size_t i = 0; i < Jobs.size(); ++i)
		{
			if (Jobs[i]->Id == Setup.Rosters[Team][Slot])
			{
				Next = (i + 1) % Jobs.size();
			}
		}
		Setup.Rosters[Team][Slot] = Jobs[Next]->Id;
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
void ATMBattleDirector::CaptureNamed(const TCHAR* Name)
{
	const FString Where = FPaths::ProjectSavedDir() / TEXT("Match") / Name;
	FScreenshotRequest::RequestScreenshot(Where, true, false);
	UE_LOG(LogTemp, Log, TEXT("CAPTURE %s"), *Where);
}
