#include "TMBattleDirector.h"

#include "Components/SkeletalMeshComponent.h"
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

		// Facing each other across the board, a side to each end.
		const float Row = Unit.Team == 0 ? -2.5f : 2.5f;
		const float Column = static_cast<float>(Index % 4) - 1.5f;
		Unit.Pos = TMSim::FVec2(Row, Column);
		Unit.Facing = TMSim::FVec2(Unit.Team == 0 ? 1.0f : -1.0f, 0.0f);

		Battle.Units.push_back(Unit);
	}

	// The same seed the clock was checked against, so what happens here is what
	// the Godot game does.
	Battle.Start(12345);

	USkeletalMesh* Mesh = UnitMesh.LoadSynchronous();
	for (size_t i = 0; i < Battle.Units.size(); ++i)
	{
		const FName Name = *FString::Printf(TEXT("Unit_%d"), Battle.Units[i].Id);
		USkeletalMeshComponent* Visual = NewObject<USkeletalMeshComponent>(this, Name);
		Visual->SetupAttachment(RootComponent);
		Visual->RegisterComponent();
		if (Mesh)
		{
			Visual->SetSkeletalMeshAsset(Mesh);
		}
		// The two sides tinted apart, until classes bring their own materials.
		Visual->SetCustomPrimitiveDataFloat(0, Battle.Units[i].Team == 0 ? 0.0f : 1.0f);
		UnitVisuals.Add(Visual);
	}

	bBuilt = true;
	RefreshVisuals();

	UE_LOG(LogTemp, Log, TEXT("Battle built with %d units"), static_cast<int32>(Battle.Units.size()));
}

FVector ATMBattleDirector::WorldFor(const TMSim::FUnit& Unit) const
{
	// A unit that is READY stands a little proud of the board, so whose turn it
	// is can be seen without reading anything.
	const float Lift = Unit.bReady ? ReadyLift : 0.0f;
	return FVector(Unit.Pos.X * TileSize, Unit.Pos.Y * TileSize, BoardHeight + Lift);
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
		UnitVisuals[i]->SetRelativeLocation(WorldFor(Unit));
		// A Paragon mesh does not face along its actor's +X, hence the offset.
		const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Unit.Facing.Y, Unit.Facing.X)) + 180.0f;
		UnitVisuals[i]->SetRelativeRotation(FRotator(0.0f, Yaw, 0.0f));
		UnitVisuals[i]->SetVisibility(Unit.IsAlive());
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
		Text += FString::Printf(TEXT("  %d %-11hs team %d  tg %4d/%d%s%s\n"),
			Unit.Id, Unit.Job.c_str(), Unit.Team, Unit.Tg, TMSim::Pace::TgMax,
			Unit.bReady ? TEXT("  READY") : TEXT(""),
			Unit.bReady ? *FString::Printf(TEXT(" clock %d"), Unit.Clock) : TEXT(""));
	}
	return Text;
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
}
