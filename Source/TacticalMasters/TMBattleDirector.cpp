#include "TMBattleDirector.h"

#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"

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

void ATMBattleDirector::RefreshVisuals()
{
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

	TMSim::FTickReport Report;
	Battle.Advance(FMath::Max(0, Ticks), Report);

	for (int32 Id : Report.BecameReady)
	{
		UE_LOG(LogTemp, Log, TEXT("tick %d: unit %d is ready"), Battle.TickCount, Id);
	}
	for (int32 Id : Report.TimedOut)
	{
		UE_LOG(LogTemp, Log, TEXT("tick %d: unit %d ran out of time"), Battle.TickCount, Id);
	}

	RefreshVisuals();
}

FString ATMBattleDirector::DescribeBattle() const
{
	FString Text = FString::Printf(TEXT("tick %d\n"), Battle.TickCount);
	for (const TMSim::FUnit& Unit : Battle.Units)
	{
		Text += FString::Printf(TEXT("  %d %-11hs team %d  at %6.2f,%6.2f  hp %3d  tg %4d/%d%s%s\n"),
			Unit.Id, Unit.Job.c_str(), Unit.Team, Unit.Pos.X, Unit.Pos.Y, Unit.Hp,
			Unit.Tg, TMSim::Pace::TgMax,
			Unit.bReady ? TEXT("  READY") : TEXT(""),
			Unit.bReady ? *FString::Printf(TEXT(" clock %d"), Unit.Clock) : TEXT(""));
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
	RefreshVisuals();
	return FString();
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
		const TMSim::FUnit* Unit = WaitingOn();
		if (!Unit || !ComputerPlays(Unit->Team))
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

	// Whoever the battle is waiting on, if the computer is playing that side it
	// takes a moment to think and then gives one order. The pause is what makes
	// it readable: a side that emptied its whole turn into one frame would be
	// impossible to learn anything from.
	const TMSim::FUnit* Unit = WaitingOn();
	if (!Unit || !ComputerPlays(Unit->Team))
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
		ThinkRemainder = static_cast<float>(Computer.Skill().Step);
	}
}
