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
#include "DrawDebugHelpers.h"

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
	if (FApp::IsUnattended() || FParse::Param(FCommandLine::Get(), TEXT("tmwatch")))
	{
		bComputerPlaysTeam0 = true;
		bComputerPlaysTeam1 = true;
		UE_LOG(LogTemp, Log, TEXT("the computer plays both sides"));
	}
	else if (!bComputerPlaysTeam0 || !bComputerPlaysTeam1)
	{
		SetUpPlayerInput();
	}
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
	bSaidWon = false;
}

void ATMBattleDirector::BuildBattle()
{
	ClearBattle();

	// Two sides of four, as the Godot game sets up. The classes are the
	// built-in ones for now; the other 81 arrive with the class importer.
	const char* Roster[8] =
	{
		"knight", "archer", "black_mage", "white_mage",
		"knight", "archer", "black_mage", "white_mage"
	};

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
		const TMSim::FJobDef* Job = TMSim::FindJob(Roster[Index]);
		if (!Job)
		{
			UE_LOG(LogTemp, Warning, TEXT("No class registered called %hs"), Roster[Index]);
			continue;
		}

		TMSim::FUnit Unit;
		Unit.Id = Index;
		Unit.Team = Index < 4 ? 0 : 1;
		Unit.Job = Roster[Index];
		Unit.Stats = &Job->Stats;

		const TMSim::FVec2 Spawn = BlueSpawns[Index % 4];
		Unit.Pos = Unit.Team == 0 ? Spawn : TMSim::FVec2(Size.X - Spawn.X, Size.Y - Spawn.Y);
		// Facing the middle, as a unit does when a battle opens.
		const TMSim::FVec2 Middle(Size.X * 0.5f, Size.Y * 0.5f);
		Unit.Facing = (Middle - Unit.Pos).Normalized();

		Battle.Units.push_back(Unit);
	}

	// The same seed the clock was checked against, so what happens here is what
	// the Godot game does.
	Battle.Start(12345);

	// The computer gets its own generator off the same seed, so a battle against
	// it plays out the same way twice. Its own, and not the battle's, because a
	// player thinking harder must not change what the dice do.
	Computer.SetDifficulty(TCHAR_TO_UTF8(*ComputerSkill));
	Computer.Rng.Seed(12345);
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
			Plate->SetVisibility(Unit.IsKo());
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
		Plate->SetVisibility(true);
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
		UnitVisuals[i]->SetVisibility(Unit.IsAlive());

		if (ReadyLights.IsValidIndex(i) && ReadyLights[i])
		{
			ReadyLights[i]->SetRelativeLocation(Where + FVector(0.0f, 0.0f, 55.0f));
			ReadyLights[i]->SetVisibility(Unit.bReady && Unit.IsAlive());
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
		if (!Unit)
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
	FScreenshotRequest::RequestScreenshot(Where, false, false);
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
			Light->SetVisibility(Unit && Unit->bReady && Unit->IsAlive());
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

	Computer.SetDifficulty(TCHAR_TO_UTF8(*ComputerSkill));
	const TMSim::FOrder Order = Computer.NextCommand(Battle, *Unit);
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

	if (bPlayerInput)
	{
		MaintainSelection();
		PickUnderCursor();
		DrawPlayerAids();
		DrawPlayerPanel(DeltaSeconds);
	}

	// Paused, nothing moves: not the clock and not the computer. Nothing is
	// submitted for it either -- a pause is time not passing, and time only
	// passes by an Advance order. Online, both machines would have to agree to
	// one; there is no online play yet, so this is a local-only key for now.
	if (bPaused)
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
			if (FApp::IsUnattended())
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
		ThinkRemainder = static_cast<float>(Computer.Skill().Think);
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
		ThinkRemainder = static_cast<float>(Computer.Skill().Step);
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

	const uint64 PanelKey = 0x544D0001;
	const uint64 NoticeKey = 0x544D0002;
	const uint64 LogKey = 0x544D0003;
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
		EKeys::P, EKeys::R
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
	}
	else if (Key == EKeys::RightMouseButton || Key == EKeys::Escape)
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
			BuildBattle();
			Log.Reset();
			OrdersGiven = 0;
			Tell(TEXT("A new battle."));
		}
	}
}

bool ATMBattleDirector::PlayerCanOrder(const TMSim::FUnit* Unit) const
{
	// battle.gd:293-295, _commandable.
	return Unit && Unit->IsAlive() && Unit->bReady && !ComputerPlays(Unit->Team)
		&& Battle.Winner == -1 && !bPaused;
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
		if (!Unit.IsAlive() && !Unit.IsKo())
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
			HoverUnitId = Near->Id;
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
	// battle.gd:765-803, _on_click.
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
	// Anyone else of this machine's who is ready: take them up instead.
	const TMSim::FUnit* Clicked = FindIn(Battle, HoverUnitId);
	if (Clicked && Clicked != Unit && PlayerCanOrder(Clicked))
	{
		SelectUnit(Clicked->Id);
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

void ATMBattleDirector::DrawPlayerAids()
{
	// Drawn with the engine's debug lines: quick, and enough to play with. They
	// are compiled out of a shipping build, so the proper board markings belong
	// with the HUD slice (feat-hud-forecast) before anything ships.
#if ENABLE_DRAW_DEBUG
	UWorld* World = GetWorld();
	const TMSim::FUnit* Unit = SelectedUnit();
	if (!World || !PlayerCanOrder(Unit))
	{
		return;
	}
	const FVector Flat(1.0f, 0.0f, 0.0f);
	const FVector Across(0.0f, 1.0f, 0.0f);

	// Whose orders these are.
	DrawDebugCircle(World, BoardPoint(Unit->Pos, 6.0f), 0.45f * TileSize, 32, FColor(90, 220, 255),
		false, -1.0f, 0, 3.0f, Flat, Across, false);

	if (AimMode == EAimMode::Move)
	{
		// Every spot it can walk to, and the way to the one under the pointer.
		const FColor Spot = bSprinting ? FColor(255, 170, 60) : FColor(80, 160, 255);
		for (const std::pair<TMSim::FNode, double>& Entry : Reachable)
		{
			DrawDebugPoint(World, BoardPoint(TMSim::FMap::NodePos(Entry.first)), 7.0f, Spot, false, -1.0f, 0);
		}
		if (bHaveHover)
		{
			const TMSim::FNode Node = TMSim::FMap::NodeOf(HoverPoint);
			if (Node != PathNode)
			{
				PathNode = Node;
				PathShown.clear();
				const bool bReachable = std::any_of(Reachable.begin(), Reachable.end(),
					[&Node](const std::pair<TMSim::FNode, double>& Entry) { return Entry.first == Node; });
				if (bReachable)
				{
					PathShown = Battle.PathTo(*Unit, Node, bSprinting);
				}
			}
			for (size_t i = 1; i < PathShown.size(); ++i)
			{
				DrawDebugLine(World, BoardPoint(PathShown[i - 1], 10.0f), BoardPoint(PathShown[i], 10.0f),
					FColor(255, 230, 90), false, -1.0f, 0, 4.0f);
			}
		}
	}
	else if (AimMode == EAimMode::Ability)
	{
		const TMSim::FAbility* Ability = TMSim::JobAbility(Unit->Job, AimSlot);
		if (!Ability)
		{
			return;
		}
		// How far it reaches, and the ring it cannot be used inside.
		const FColor Reach(200, 200, 215);
		if (Ability->MaxRange > 0.0f)
		{
			DrawDebugCircle(World, BoardPoint(Unit->Pos, 8.0f), Ability->MaxRange * TileSize, 72, Reach,
				false, -1.0f, 0, 2.0f, Flat, Across, false);
		}
		if (Ability->MinRange > 0.0f)
		{
			DrawDebugCircle(World, BoardPoint(Unit->Pos, 8.0f), Ability->MinRange * TileSize, 48, FColor(150, 90, 90),
				false, -1.0f, 0, 2.0f, Flat, Across, false);
		}

		const FAim Where = Aim();
		if (!Where.bHave)
		{
			return;
		}
		const float Radius = FMath::Max(Ability->Aoe, TMSim::Ground::HitRadius) * TileSize;
		DrawDebugCircle(World, BoardPoint(Where.Point, 12.0f), Radius, 48,
			Where.bOk ? FColor(110, 255, 140) : FColor(255, 90, 80), false, -1.0f, 0, 3.0f, Flat, Across, false);
		if (!Where.bOk)
		{
			return;
		}

		// The forecast: who it would reach and what it would do to each, from the
		// same Preview the rules resolve with. The miss chance is the rules' too.
		// It does not say who a cast will have reached by the time it lands.
		const std::vector<TMSim::FHit> Hits = Battle.Preview(*Unit, AimSlot, Unit->Pos, Where.Point);
		for (const TMSim::FHit& Hit : Hits)
		{
			const TMSim::FUnit* Target = FindIn(Battle, Hit.UnitId);
			if (!Target)
			{
				continue;
			}
			FString Text;
			FColor Tint = FColor::White;
			switch (Ability->Effect)
			{
			case TMSim::EEffect::Damage:
			{
				Text = FString::Printf(TEXT("-%d"), Hit.Amount);
				const int32 Miss = Battle.EvadeChance(*Target, *Ability, Unit);
				if (Miss > 0)
				{
					Text += FString::Printf(TEXT("  %d%% miss"), Miss);
				}
				Tint = FColor(255, 120, 95);
				break;
			}
			case TMSim::EEffect::Heal:
				Text = FString::Printf(TEXT("+%d"), Hit.Amount);
				Tint = FColor(120, 255, 135);
				break;
			case TMSim::EEffect::Revive:
				Text = FString::Printf(TEXT("up with %d"), Hit.Amount);
				Tint = FColor(255, 242, 153);
				break;
			default:
				Text = Ability->HasStatus() ? FString(UTF8_TO_TCHAR(Ability->StatusId.c_str())) : FString(TEXT("affected"));
				Tint = FColor(224, 153, 255);
				break;
			}
			DrawDebugString(World, BoardPoint(Target->Pos, 225.0f), Text, nullptr, Tint, -1.0f, true, 1.3f);
		}
	}
#endif
}

void ATMBattleDirector::DrawPlayerPanel(float DeltaSeconds)
{
	// A plain text panel until the HUD slice gives this a proper screen. It uses
	// the engine's on-screen messages, which a shipping build leaves out.
	if (!GEngine)
	{
		return;
	}
	const float Tps = static_cast<float>(TMSim::Pace::TicksPerSecond);
	FString Panel;

	if (Battle.Winner != -1)
	{
		if (Battle.Winner == TMSim::FBattle::Draw)
		{
			Panel = TEXT("Nobody is left standing.");
		}
		else
		{
			Panel = ComputerPlays(Battle.Winner) ? TEXT("The computer wins.") : TEXT("You win!");
		}
		Panel += TEXT("\nR: another battle");
	}
	else if (const TMSim::FUnit* Unit = SelectedUnit())
	{
		Panel = FString::Printf(TEXT("%hs %d   hp %d/%d   %.1fs to act%s%s"),
			Unit->Job.c_str(), Unit->Id, Unit->Hp, Unit->MaxHp(), Unit->Clock / Tps,
			Unit->bMoved ? TEXT("   walked") : TEXT(""),
			Unit->bActed ? TEXT("   acted") : TEXT(""));
		for (int32 Slot = 0; Slot < 4; ++Slot)
		{
			const TMSim::FAbility* Ability = TMSim::JobAbility(Unit->Job, Slot);
			if (!Ability)
			{
				continue;
			}
			const std::string Blocked = Battle.AbilityBlockedReason(*Unit, Slot);
			Panel += FString::Printf(TEXT("\n  %d  %hs%s%hs%s"), Slot + 1, Ability->Name.c_str(),
				Blocked.empty() ? TEXT("") : TEXT("   -- "), Blocked.c_str(),
				AimMode == EAimMode::Ability && AimSlot == Slot ? TEXT("   <") : TEXT(""));
		}
		switch (AimMode)
		{
		case EAimMode::Move:
			Panel += bSprinting
				? TEXT("\nSprinting: click an orange dot. No ability after.")
				: TEXT("\nClick a blue dot to walk there.");
			break;
		case EAimMode::Ability:
			Panel += TEXT("\nClick a target. Green ring: it can be used there.");
			break;
		default:
			Panel += Unit->bMoved ? TEXT("\n1-4 to use an ability, or Enter to end the turn.")
				: TEXT("\nSpace to walk, 1-4 for an ability, Enter to end the turn.");
			break;
		}
		if (bPaused)
		{
			Panel += TEXT("\nPAUSED -- P to carry on");
		}
	}
	else
	{
		// Nobody of ours is ready: say who is next and when.
		const TMSim::FUnit* Next = nullptr;
		int32 Soonest = 0;
		for (const TMSim::FUnit& Candidate : Battle.Units)
		{
			if (!Candidate.IsAlive() || ComputerPlays(Candidate.Team))
			{
				continue;
			}
			const int32 Ticks = Battle.TicksToReady(Candidate);
			if (!Next || Ticks < Soonest)
			{
				Next = &Candidate;
				Soonest = Ticks;
			}
		}
		Panel = Next
			? FString::Printf(TEXT("Waiting: %hs %d is up in %.1fs"), Next->Job.c_str(), Next->Id, Soonest / Tps)
			: FString(TEXT("Watching."));
		if (bPaused)
		{
			Panel += TEXT("\nPAUSED -- P to carry on");
		}
	}
	Panel += TEXT("\n\nSpace walk   Shift sprint   1-4 ability   Enter end turn   Tab next   Esc cancel   P pause");
	GEngine->AddOnScreenDebugMessage(PanelKey, 0.0f, FColor(235, 235, 245), Panel, false, FVector2D(1.2f, 1.2f));

	NoticeLeft -= DeltaSeconds;
	if (NoticeLeft > 0.0f && !Notice.IsEmpty())
	{
		GEngine->AddOnScreenDebugMessage(NoticeKey, 0.0f, FColor(255, 215, 90), Notice, false, FVector2D(1.2f, 1.2f));
	}

	// The last few lines of the fight, so it can be followed without the log file.
	const int32 Shown = 8;
	TArray<FString> Recent;
	for (int32 i = FMath::Max(0, Log.Num() - Shown); i < Log.Num(); ++i)
	{
		Recent.Add(Log[i]);
	}
	if (Recent.Num() > 0)
	{
		GEngine->AddOnScreenDebugMessage(LogKey, 0.0f, FColor(170, 175, 190), FString::Join(Recent, TEXT("\n")));
	}
}
