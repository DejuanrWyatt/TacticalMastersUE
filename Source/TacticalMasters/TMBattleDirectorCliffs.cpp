// Cliffs and structures: height made to look like height.
//
// The smooth ground (TMBattleDirectorGround.cpp) rounds every step between
// heights into a grassy bank, which from the camera read as gentle bumps. Here
// every tall step -- two heights or more, one a unit cannot simply walk up --
// is faced with rock: a cliff mesh from the theme's kit along the edge between
// the two tiles, as tall as the drop, set into the bank so it covers it. And
// a structure -- a ruined pillar, a wall's end, a tower -- stands on some of
// the rock tiles instead of a boulder, and a few larger ones stand in the land
// round the board.
//
// Presentation only: the rules still see the same tiles, heights and rock.
// Cliffs sit between tiles, clear of every spot a unit can stand (the nearest
// is a quarter metre from a tile's edge), and structures only where rock
// already stands, which nobody can enter.

#include "TMBattleDirector.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Math/RandomStream.h"
#include "Misc/App.h"

namespace
{
	/** Paragon's Monolith rock faces: used when a theme names no cliffs of its own. */
	const TCHAR* const DefaultCliffs[] =
	{
		TEXT("/Game/ParagonProps/Monolith/Rocks/Meshes/SM_RockNordic_01.SM_RockNordic_01"),
		TEXT("/Game/ParagonProps/Monolith/Rocks/Meshes/SM_RockNordic_02.SM_RockNordic_02"),
		TEXT("/Game/ParagonProps/Monolith/Rocks/Meshes/SM_RockNordic_03.SM_RockNordic_03"),
		TEXT("/Game/ParagonProps/Monolith/Rocks/Meshes/SM_RockNordic_07.SM_RockNordic_07"),
	};
	/** Paragon's ruins: used when a theme names no structures of its own. */
	const TCHAR* const DefaultStructures[] =
	{
		TEXT("/Game/ParagonProps/Agora/Props/Meshes/SM_Angelsjon_TriPillar.SM_Angelsjon_TriPillar"),
		TEXT("/Game/ParagonProps/Monolith/Dusk/Meshes/SM_Dusk_WallA_Endcap01.SM_Dusk_WallA_Endcap01"),
		TEXT("/Game/ParagonProps/Monolith/Dusk/Meshes/SM_Dusk_WallA_Endcap02.SM_Dusk_WallA_Endcap02"),
		TEXT("/Game/ParagonProps/Agora/Rocks/Meshes/SM_Angelsjon_Cylinder.SM_Angelsjon_Cylinder"),
	};
	/** How often a rock tile carries a structure instead of a rock. */
	constexpr float StructureShare = 0.22f;
	/** Steps of height a unit walks up; anything taller is a cliff. */
	constexpr int32 CliffSteps = 2;
	/** How far a cliff face stands out from the edge into each tile, in metres. */
	constexpr float CliffDepth = 0.28f;

	TArray<FString> CliffKitOr(const TArray<FString>& Named, const TCHAR* const* Defaults, int32 Count)
	{
		if (Named.Num() > 0)
		{
			return Named;
		}
		TArray<FString> Out;
		for (int32 i = 0; i < Count; ++i)
		{
			Out.Add(Defaults[i]);
		}
		return Out;
	}
}

const TArray<FString>& ATMBattleDirector::RockKit(const FTMTheme& Theme)
{
	static const TArray<FString> Paragon = CliffKitOr(TArray<FString>(), DefaultCliffs, static_cast<int32>(UE_ARRAY_COUNT(DefaultCliffs)));
	return Theme.KitRock.Num() > 0 ? Theme.KitRock : Paragon;
}

UStaticMeshComponent* ATMBattleDirector::MaybeStructure(const FTMTheme& Theme, const FVector& Foot, float Tile, const FRandomStream& Dice)
{
	if (Dice.FRand() >= StructureShare)
	{
		return nullptr;
	}
	const TArray<FString> Kinds = CliffKitOr(Theme.KitStructure, DefaultStructures, static_cast<int32>(UE_ARRAY_COUNT(DefaultStructures)));
	// As wide as the tile, and at least three metres tall, so it plainly blocks
	// what is behind it as the rock it stands for does.
	return KitPiece(Kinds[Dice.RandHelper(Kinds.Num())], Foot, Tile * 0.85f, 0.0f, 90.0f * Dice.RandHelper(4), false, 3.0f * TileSize);
}

void ATMBattleDirector::DressCliffs(const FTMTheme& Theme)
{
	if (!FApp::CanEverRender())
	{
		return;
	}
	const TMSim::FMap& Map = Battle.Map;
	if (Map.TilesX <= 0 || Map.TilesY <= 0)
	{
		return;
	}
	const float M = TileSize;
	const float Tile = TMSim::Ground::TileSize * M;
	const float Level = LevelCm();

	// One instanced layer per cliff mesh.
	TArray<UHierarchicalInstancedStaticMeshComponent*> CliffLayers;
	TArray<FBox> Bounds;
	for (const FString& Path : CliffKitOr(Theme.KitCliff, DefaultCliffs, static_cast<int32>(UE_ARRAY_COUNT(DefaultCliffs))))
	{
		TObjectPtr<UStaticMesh>* Known = KitMeshes.Find(Path);
		if (!Known)
		{
			UStaticMesh* Loaded = LoadObject<UStaticMesh>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
			if (!Loaded)
			{
				UE_LOG(LogTemp, Warning, TEXT("cliffs: no mesh at %s"), *Path);
			}
			Known = &KitMeshes.Add(Path, Loaded);
		}
		if (UStaticMesh* Mesh = *Known)
		{
			UHierarchicalInstancedStaticMeshComponent* Layer = NewObject<UHierarchicalInstancedStaticMeshComponent>(this, NAME_None, RF_Transient);
			Layer->SetMobility(EComponentMobility::Movable);
			Layer->SetupAttachment(RootComponent);
			Layer->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Layer->SetStaticMesh(Mesh);
			Layer->SetReceivesDecals(true);  // the ground's paint drapes over its rock (2026-09-30)
			Layer->RegisterComponent();
			BoardProps.Add(Layer);
			CliffLayers.Add(Layer);
			Bounds.Add(Mesh->GetBoundingBox());
		}
	}
	if (CliffLayers.Num() == 0)
	{
		return;
	}

	// A tile's height as the ground draws it: its steps; rock a step; water none.
	auto Steps = [&Map](int32 X, int32 Y)
	{
		const int32 S = Map.TileLevel(X, Y);
		return S > 0 ? S : (Map.Covers[Y * Map.TilesX + X] != 0 ? 1 : 0);
	};
	const FRandomStream Dice(GetTypeHash(FString(UTF8_TO_TCHAR(Setup.MapId.c_str()))) ^ 0xc11ff);
	int32 Faces = 0;
	auto Face = [&](const FVector& Middle, float Yaw, float Drop, float Along)
	{
		// Split a long edge into pieces about a tile long, each a different rock.
		const int32 Pieces = FMath::Max(1, FMath::RoundToInt(Along / Tile));
		const FVector Way = FRotator(0.0f, Yaw + 90.0f, 0.0f).Vector();
		for (int32 p = 0; p < Pieces; ++p)
		{
			const int32 k = Dice.RandHelper(CliffLayers.Num());
			const FVector Size = Bounds[k].GetSize().ComponentMax(FVector(1.0));
			const float Length = Along / Pieces * Dice.FRandRange(1.05f, 1.25f);
			// Its widest side along the edge, its narrowest into the tiles.
			const bool bWideX = Size.X >= Size.Y;
			const float Wide = bWideX ? Size.X : Size.Y;
			const float Thin = bWideX ? Size.Y : Size.X;
			const float Tall = Drop * Dice.FRandRange(1.0f, 1.12f);
			const FVector Scale = bWideX
				? FVector(Length / Wide, 2.0f * CliffDepth * M / Thin, Tall / Size.Z)
				: FVector(2.0f * CliffDepth * M / Thin, Length / Wide, Tall / Size.Z);
			const float Turn = Yaw + (bWideX ? 90.0f : 0.0f) + (Dice.FRand() < 0.5f ? 180.0f : 0.0f);
			const FRotator Rotation(0.0f, Turn, 0.0f);
			const FVector Pivot(Bounds[k].GetCenter().X, Bounds[k].GetCenter().Y, Bounds[k].Min.Z);
			const FVector Foot = Middle + Way * ((p + 0.5f) / Pieces - 0.5f) * Along - FVector(0.0f, 0.0f, 0.12f * Level);
			CliffLayers[k]->AddInstance(FTransform(Rotation, Foot - Rotation.RotateVector(Pivot * Scale), Scale));
			++Faces;
		}
	};
	for (int32 Y = 0; Y < Map.TilesY; ++Y)
	{
		for (int32 X = 0; X < Map.TilesX; ++X)
		{
			const int32 Here = Steps(X, Y);
			// Each edge once: to the east and to the south.
			if (X + 1 < Map.TilesX)
			{
				const int32 There = Steps(X + 1, Y);
				if (FMath::Abs(Here - There) >= CliffSteps)
				{
					const FVector Middle((X + 1) * Tile, (Y + 0.5f) * Tile, FMath::Min(Here, There) * Level);
					Face(Middle, 0.0f, FMath::Abs(Here - There) * Level, Tile);
				}
			}
			if (Y + 1 < Map.TilesY)
			{
				const int32 There = Steps(X, Y + 1);
				if (FMath::Abs(Here - There) >= CliffSteps)
				{
					const FVector Middle((X + 0.5f) * Tile, (Y + 1) * Tile, FMath::Min(Here, There) * Level);
					Face(Middle, 90.0f, FMath::Abs(Here - There) * Level, Tile);
				}
			}
		}
	}
	UE_LOG(LogTemp, Log, TEXT("cliffs: %d rock faces on tall steps"), Faces);
}

void ATMBattleDirector::DressStructures(const FTMTheme& Theme)
{
	if (!FApp::CanEverRender())
	{
		return;
	}
	const TMSim::FMap& Map = Battle.Map;
	const float M = TileSize;
	const FVector2D Board(Map.TilesX * TMSim::Ground::TileSize * M, Map.TilesY * TMSim::Ground::TileSize * M);
	const TArray<FString> Kinds = CliffKitOr(Theme.KitStructure, DefaultStructures, static_cast<int32>(UE_ARRAY_COUNT(DefaultStructures)));
	const FRandomStream Dice(GetTypeHash(FString(UTF8_TO_TCHAR(Setup.MapId.c_str()))) ^ 0x5da7);
	// A few big ruins in the land round the board, on the far sides from the
	// camera so they frame it without ever standing in front of it.
	const int32 Count = 6 + Map.TilesX / 8;
	int32 Placed = 0;
	for (int32 Try = 0; Try < Count * 6 && Placed < Count; ++Try)
	{
		const bool bFarX = Dice.FRand() < 0.5f;
		const float Out = Dice.FRandRange(6.0f, 22.0f) * M;
		const FVector2D Spot = bFarX
			? FVector2D(Board.X + Out, Dice.FRandRange(0.0f, Board.Y))
			: FVector2D(Dice.FRandRange(0.0f, Board.X), Board.Y + Out);
		if (KitPiece(Kinds[Dice.RandHelper(Kinds.Num())], FVector(Spot.X, Spot.Y, 0.0f), 0.0f, Dice.FRandRange(5.0f, 9.0f) * M,
			Dice.FRandRange(0.0f, 360.0f), false))
		{
			++Placed;
		}
	}
	UE_LOG(LogTemp, Log, TEXT("structures: %d round the board"), Placed);
}
