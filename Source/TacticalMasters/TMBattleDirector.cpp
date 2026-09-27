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

#include "SimAbility.h"

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
