// Foliage: grass and flowers on the ground units walk, bushes where rock stands
// and along the board's edge, and a forest in the land round it.
//
// All of it is instanced (one draw per kind, however many there are), placed
// from a stream seeded by the map and the theme so the same battle always
// grows the same, and none of it is read by the rules: grass is short enough
// to walk through, and nothing tall grows where a unit could stand, so what
// is seen never argues with what the rules say is there.
//
// A theme names what to grow (Content/Data/Themes, "foliage"): how dense,
// what colours, and, once a nature pack is in the project, which of its
// meshes ("kit"). Each kit mesh is fitted by its own bounds to the height the
// theme asks, stood on its bottom middle, so a pack's scale and pivots don't
// matter. Where a theme names none, or a named mesh isn't in the project,
// nothing of that kind grows (plain painted shapes used to stand in, and looked
// like placeholders).

#include "TMBattleDirector.h"
#include "Misc/App.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Math/RandomStream.h"

namespace
{
	/** Metres in from a tile's edge that grass keeps to, clear of the rounded banks. */
	constexpr float FoliageInset = 0.2f;
	/** The most kit trees round a board: each is a heavy mesh, where a stand-in is two shapes. */
	constexpr int32 FoliageKitTreeCap = 1500;

	/** True if the layer draws a pack's mesh, not one of the engine's basic shapes. */
	bool FoliageIsKit(const UHierarchicalInstancedStaticMeshComponent* Layer)
	{
		const UStaticMesh* Mesh = Layer ? Layer->GetStaticMesh() : nullptr;
		return Mesh && !Mesh->GetPathName().StartsWith(TEXT("/Engine/"));
	}

	/** A kit mesh Height centimetres tall, turned Yaw, stood with its bottom middle on Foot. */
	FTransform FoliageKitPlace(const UHierarchicalInstancedStaticMeshComponent* Layer, const FVector& Foot, float Height, float Yaw)
	{
		const FBox Bounds = Layer->GetStaticMesh()->GetBoundingBox();
		const double Scale = Height / FMath::Max(Bounds.GetSize().Z, 1.0);
		const FRotator Turn(0.0f, Yaw, 0.0f);
		const FVector Pivot(Bounds.GetCenter().X, Bounds.GetCenter().Y, Bounds.Min.Z);
		return FTransform(Turn, Foot - Turn.RotateVector(Pivot * Scale), FVector(Scale));
	}
}

UHierarchicalInstancedStaticMeshComponent* ATMBattleDirector::FoliageLayer(const FString& KitPath, const TCHAR* ShapeName, const FLinearColor& Colour, float CullMetres)
{
	UStaticMesh* Mesh = nullptr;
	bool bKit = false;
	if (!KitPath.IsEmpty())
	{
		TObjectPtr<UStaticMesh>* Known = KitMeshes.Find(KitPath);
		if (!Known)
		{
			UStaticMesh* Loaded = LoadObject<UStaticMesh>(nullptr, *KitPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
			if (!Loaded)
			{
				UE_LOG(LogTemp, Warning, TEXT("foliage kit: no mesh at %s; a simple shape stands in"), *KitPath);
			}
			Known = &KitMeshes.Add(KitPath, Loaded);
		}
		Mesh = *Known;
		bKit = Mesh != nullptr;
	}
	// Only a pack's own, textured meshes grow: the plain painted shapes that
	// once stood in (cones for grass and trees, balls for bushes) looked like
	// placeholders, so where a theme names no mesh -- or its mesh is missing --
	// nothing grows there at all.
	(void)ShapeName;
	if (!Mesh || !bKit)
	{
		return nullptr;
	}
	UHierarchicalInstancedStaticMeshComponent* Layer = NewObject<UHierarchicalInstancedStaticMeshComponent>(this, NAME_None, RF_Transient);
	Layer->SetMobility(EComponentMobility::Movable);
	Layer->SetupAttachment(RootComponent);
	Layer->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Layer->SetStaticMesh(Mesh);
	if (!bKit)
	{
		Layer->SetMaterial(0, Paint(Colour));
	}
	// The walk area, aims and rings are painted over the grass and trees too, as
	// League lays its range circles over everything (the human's ask, 2026-09-30):
	// on the ground alone, the grass hid them.
	Layer->SetReceivesDecals(true);
	const float Cull = CullMetres * TileSize;
	Layer->SetCullDistances(static_cast<int32>(Cull * 0.8f), static_cast<int32>(Cull));
	Layer->RegisterComponent();
	BoardProps.Add(Layer);
	return Layer;
}

void ATMBattleDirector::BuildFoliage(const FTMTheme& Theme)
{
	// Nothing to see in a headless copy, and the kit's meshes take minutes to build.
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
	const float Tile = TMSim::Ground::TileSize;          // metres
	const float Level = LevelCm();   // centimetres, as drawn
	FRandomStream Dice(GetTypeHash(FString(UTF8_TO_TCHAR(Setup.MapId.c_str()))) ^ GetTypeHash(Theme.Id) ^ 0x5eed);
	auto Count = [&Dice](float Expected)
	{
		// A whole number whose average is Expected.
		const int32 Whole = FMath::FloorToInt(Expected);
		return Whole + (Dice.FRand() < Expected - Whole ? 1 : 0);
	};
	// One layer per kit mesh when the theme names some; otherwise one per colour
	// of the stand-in shape. The camera frames the board from well over 100 m
	// away, so what grows on it is drawn that far out.
	auto MakeLayers = [this](const TArray<FString>& Kit, const TArray<FLinearColor>& Colours, const TCHAR* ShapeName, float CullMetres)
	{
		TArray<UHierarchicalInstancedStaticMeshComponent*> Out;
		const FLinearColor First = Colours.Num() > 0 ? Colours[0] : FLinearColor(0.3f, 0.5f, 0.2f);
		if (Kit.Num() > 0)
		{
			for (const FString& Path : Kit)
			{
				if (UHierarchicalInstancedStaticMeshComponent* Layer = FoliageLayer(Path, ShapeName, First, CullMetres))
				{
					Out.Add(Layer);
				}
			}
		}
		else
		{
			for (const FLinearColor& Colour : Colours)
			{
				if (UHierarchicalInstancedStaticMeshComponent* Layer = FoliageLayer(FString(), ShapeName, Colour, CullMetres))
				{
					Out.Add(Layer);
				}
			}
		}
		return Out;
	};

	TArray<UHierarchicalInstancedStaticMeshComponent*> Grass = MakeLayers(Theme.KitGrass, Theme.GrassColours, TEXT("Cone"), 240.0f);
	TArray<UHierarchicalInstancedStaticMeshComponent*> Flowers = Theme.FlowerDensity > 0.0f
		? MakeLayers(Theme.KitFlower, Theme.FlowerColours, TEXT("Sphere"), 200.0f) : TArray<UHierarchicalInstancedStaticMeshComponent*>();
	TArray<UHierarchicalInstancedStaticMeshComponent*> Bushes = Theme.BushDensity > 0.0f
		? MakeLayers(Theme.KitBush, { Theme.BushColour }, TEXT("Sphere"), 320.0f) : TArray<UHierarchicalInstancedStaticMeshComponent*>();
	bool bAnyKit = false;
	for (UHierarchicalInstancedStaticMeshComponent* Layer : Grass)
	{
		Layer->SetCastShadow(false);  // thousands of clumps: shade from the ground is enough
		bAnyKit |= FoliageIsKit(Layer);
	}
	for (UHierarchicalInstancedStaticMeshComponent* Layer : Flowers)
	{
		Layer->SetCastShadow(false);
		bAnyKit |= FoliageIsKit(Layer);
	}
	for (UHierarchicalInstancedStaticMeshComponent* Layer : Bushes)
	{
		bAnyKit |= FoliageIsKit(Layer);
	}
	const bool bKitGrass = Grass.Num() > 0 && FoliageIsKit(Grass[0]);

	// On the ground units walk: grass and flowers, clear of tile edges so none
	// hangs off a bank, and not on embers, springs, rock or water.
	for (int32 Y = 0; Y < Map.TilesY; ++Y)
	{
		for (int32 X = 0; X < Map.TilesX; ++X)
		{
			const int32 Index = Y * Map.TilesX + X;
			const int32 Height = Map.TileLevel(X, Y);
			if (Height <= 0 || Map.Covers[Index] != 0 || Map.Hazards[Index] != 0)
			{
				continue;
			}
			const float Top = Height * Level;
			auto Spot = [&]()
			{
				return FVector((X * Tile + FoliageInset + Dice.FRand() * (Tile - 2.0f * FoliageInset)) * M,
					(Y * Tile + FoliageInset + Dice.FRand() * (Tile - 2.0f * FoliageInset)) * M, Top);
			};
			// Stand-in grass is single blades, so many more of them than kit clumps.
			const int32 Blades = Grass.Num() > 0 ? Count(Theme.GrassDensity * Tile * Tile * (bKitGrass ? 1.0f : 6.0f)) : 0;
			for (int32 k = 0; k < Blades; ++k)
			{
				const FVector Foot = Spot();
				UHierarchicalInstancedStaticMeshComponent* Layer = Grass[Dice.RandHelper(Grass.Num())];
				if (FoliageIsKit(Layer))
				{
					Layer->AddInstance(FoliageKitPlace(Layer, Foot, Theme.KitGrassHeight * M * Dice.FRandRange(0.7f, 1.3f), Dice.FRandRange(0.0f, 360.0f)));
				}
				else
				{
					// A blade: a thin cone standing on its base, leaning a little.
					const float Tall = Theme.GrassHeight * M * Dice.FRandRange(0.6f, 1.3f);
					Layer->AddInstance(FTransform(FRotator(Dice.FRandRange(-12.0f, 12.0f), Dice.FRandRange(0.0f, 360.0f), Dice.FRandRange(-12.0f, 12.0f)),
						Foot + FVector(0, 0, Tall * 0.5f), FVector(0.05f, 0.05f, Tall / 100.0f)));
				}
			}
			const int32 Blooms = Flowers.Num() > 0 ? Count(Theme.FlowerDensity * Tile * Tile) : 0;
			for (int32 k = 0; k < Blooms; ++k)
			{
				const FVector Foot = Spot();
				UHierarchicalInstancedStaticMeshComponent* Layer = Flowers[Dice.RandHelper(Flowers.Num())];
				if (FoliageIsKit(Layer))
				{
					Layer->AddInstance(FoliageKitPlace(Layer, Foot, Theme.KitFlowerHeight * M * Dice.FRandRange(0.7f, 1.3f), Dice.FRandRange(0.0f, 360.0f)));
				}
				else
				{
					const float Size = Dice.FRandRange(0.06f, 0.1f);
					Layer->AddInstance(FTransform(FRotator::ZeroRotator, Foot + FVector(0, 0, Dice.FRandRange(0.12f, 0.25f) * M), FVector(Size, Size, Size * 0.7f)));
				}
			}
		}
	}

	// Bushes: round the rocks (no unit stands on a rock tile) and along the
	// board's edge, just outside it.
	if (Bushes.Num() > 0)
	{
		auto Bush = [&](const FVector& Foot)
		{
			UHierarchicalInstancedStaticMeshComponent* Layer = Bushes[Dice.RandHelper(Bushes.Num())];
			if (FoliageIsKit(Layer))
			{
				Layer->AddInstance(FoliageKitPlace(Layer, Foot, Theme.KitBushHeight * M * Dice.FRandRange(0.7f, 1.3f), Dice.FRandRange(0.0f, 360.0f)));
				return;
			}
			const float Size = Dice.FRandRange(0.6f, 1.2f);
			Layer->AddInstance(FTransform(FRotator(0.0f, Dice.FRandRange(0.0f, 360.0f), 0.0f), Foot + FVector(0, 0, Size * 35.0f),
				FVector(Size, Size * Dice.FRandRange(0.8f, 1.1f), Size * 0.7f)));
		};
		for (int32 Y = 0; Y < Map.TilesY; ++Y)
		{
			for (int32 X = 0; X < Map.TilesX; ++X)
			{
				if (Map.Covers[Y * Map.TilesX + X] == 0)
				{
					continue;
				}
				for (int32 k = Count(Theme.BushDensity * Tile * Tile * 2.0f); k > 0; --k)
				{
					Bush(FVector((X + Dice.FRand()) * Tile * M, (Y + Dice.FRand()) * Tile * M, Level));
				}
			}
		}
		const float BoardX = Map.TilesX * Tile;
		const float BoardY = Map.TilesY * Tile;
		const int32 EdgeBushes = Count(Theme.BushDensity * (BoardX + BoardY) * 2.0f * 2.5f);
		for (int32 k = 0; k < EdgeBushes; ++k)
		{
			// Somewhere on the band 1.5 to 4 m outside the edge.
			const float Along = Dice.FRand();
			const float Out = Dice.FRandRange(1.5f, 4.0f);
			const int32 Side = Dice.RandHelper(4);
			const FVector2D At = Side == 0 ? FVector2D(Along * BoardX, -Out) : Side == 1 ? FVector2D(Along * BoardX, BoardY + Out)
				: Side == 2 ? FVector2D(-Out, Along * BoardY) : FVector2D(BoardX + Out, Along * BoardY);
			Bush(FVector(At.X * M, At.Y * M, 0.0f));
		}
	}

	// The forest round the board: denser than the old scatter, and never on
	// the camera's side close enough to stand between it and the board.
	if (Theme.ForestDensity > 0.0f && Theme.Trees != TEXT("none"))
	{
		TArray<UHierarchicalInstancedStaticMeshComponent*> KitTrees;
		for (const FString& Path : Theme.KitTree)
		{
			UHierarchicalInstancedStaticMeshComponent* Layer = FoliageLayer(Path, TEXT("Cone"), Theme.Leaves, 600.0f);
			if (FoliageIsKit(Layer))
			{
				KitTrees.Add(Layer);
			}
		}
		const bool bKitTree = KitTrees.Num() > 0;
		bAnyKit |= bKitTree;
		const bool bRound = Theme.Trees == TEXT("round");
		const bool bDead = Theme.Trees == TEXT("dead");
		UHierarchicalInstancedStaticMeshComponent* Trunks = bKitTree ? nullptr : FoliageLayer(FString(), TEXT("Cylinder"), Theme.Trunk, 600.0f);
		UHierarchicalInstancedStaticMeshComponent* Crowns = bKitTree || bDead ? nullptr
			: FoliageLayer(FString(), bRound ? TEXT("Sphere") : TEXT("Cone"), Theme.Leaves, 600.0f);
		UHierarchicalInstancedStaticMeshComponent* Crowns2 = bKitTree || bDead ? nullptr
			: FoliageLayer(FString(), bRound ? TEXT("Sphere") : TEXT("Cone"), Theme.Leaves * 1.15f, 600.0f);
		const float BoardX = Map.TilesX * Tile;
		const float BoardY = Map.TilesY * Tile;
		const float Near = 5.0f;
		const float Far = 40.0f;
		const float Ring = (BoardX + 2.0f * Far) * (BoardY + 2.0f * Far) - BoardX * BoardY;
		const int32 Trees = FMath::Min(bKitTree ? FoliageKitTreeCap : 4000, Count(Theme.ForestDensity * Ring));
		for (int32 k = 0; k < Trees; ++k)
		{
			const FVector2D At(Dice.FRandRange(-Far, BoardX + Far), Dice.FRandRange(-Far, BoardY + Far));
			const float OffX = static_cast<float>(FMath::Max3(-At.X, At.X - BoardX, 0.0));
			const float OffY = static_cast<float>(FMath::Max3(-At.Y, At.Y - BoardY, 0.0));
			const bool bCameraSide = At.X < 0.0f || At.Y < 0.0f;
			if (FMath::Max(OffX, OffY) < (bCameraSide ? Near * 3.0f : Near))
			{
				continue;
			}
			const float Scale = Dice.FRandRange(0.8f, 1.6f);
			const FVector Foot(At.X * M, At.Y * M, 0.0f);
			const float Yaw = Dice.FRandRange(0.0f, 360.0f);
			if (bKitTree)
			{
				UHierarchicalInstancedStaticMeshComponent* Layer = KitTrees[Dice.RandHelper(KitTrees.Num())];
				Layer->AddInstance(FoliageKitPlace(Layer, Foot, Theme.KitTreeHeight * M * Scale * 0.8f, Yaw));
				continue;
			}
			if (Trunks)
			{
				Trunks->AddInstance(FTransform(FRotator(0.0f, Yaw, 0.0f), Foot + FVector(0, 0, 90.0f * Scale), FVector(0.28f, 0.28f, 1.8f) * Scale));
			}
			if (bDead)
			{
				continue;
			}
			if (Crowns)
			{
				Crowns->AddInstance(FTransform(FRotator(0.0f, Yaw, 0.0f), Foot + FVector(0, 0, (bRound ? 270.0f : 250.0f) * Scale),
					(bRound ? FVector(2.6f, 2.6f, 2.3f) : FVector(2.2f, 2.2f, 2.8f)) * Scale));
			}
			if (Crowns2 && !bRound)
			{
				Crowns2->AddInstance(FTransform(FRotator(0.0f, Yaw, 0.0f), Foot + FVector(0, 0, 380.0f * Scale), FVector(1.5f, 1.5f, 2.1f) * Scale));
			}
		}
	}
	int32 Total = 0;
	for (TObjectPtr<USceneComponent>& Prop : BoardProps)
	{
		if (const UHierarchicalInstancedStaticMeshComponent* Layer = Cast<UHierarchicalInstancedStaticMeshComponent>(Prop))
		{
			Total += Layer->GetInstanceCount();
		}
	}
	UE_LOG(LogTemp, Log, TEXT("foliage: %d instances, dressed as %s%s"), Total, *Theme.Id,
		bAnyKit ? TEXT(" (from its kit)") : TEXT(" (no kit: bare)"));
}
