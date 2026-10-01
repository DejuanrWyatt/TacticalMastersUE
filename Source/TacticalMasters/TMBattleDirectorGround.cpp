// The ground as one smooth surface, rather than a box per tile.
//
// The rules think in 2 m tiles with heights in steps, and the board used to be
// drawn that way: a column and a slab per tile, a hair apart, each a slightly
// different colour. It read as a grid of blocks, at odds with units that walk
// and aim in any direction. Here the same heights are laid as a single mesh:
//
//   - a vertex every 25 cm; inside a tile the ground is flat at its height,
//     and where two heights meet it rounds over in a short, steep bank;
//   - the line where tiles meet wanders a little, so no edge runs dead straight;
//   - its colour is the theme's, blended across tile edges, shaded by slope
//     (a bank shows the theme's side colour) and mottled by a slow noise
//     instead of a chequer of tiles;
//   - the board's own edge rolls down to the land around it.
//
// Where a unit can stand -- the centre of each half-metre node -- the ground is
// exactly the height the rules give it, so units stand on it as before. The
// old tile pieces are still there, hidden: a click on the board is found by
// tracing into them (PickUnderCursor), and the rules never see any of this.
//
// Needs /Game/UI/M_GroundVertex (Tools/make_ground_material.py) for its
// colours; without it the ground is one colour, but still smooth.

#include "TMBattleDirector.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"

namespace
{
	/** Metres between vertices: half a node, so every node centre is a vertex. */
	constexpr float GroundStep = 0.25f;
	constexpr int32 GroundPerTile = 8;
	/** Vertices of bank beyond the board's edge, rolling down to the land around. */
	constexpr int32 GroundRim = 8;
	/** How far a tile edge wanders, in metres: less than a quarter node, so node centres never move. */
	constexpr float EdgeWander = 0.09f;
	/** The ground's colours are drawn this much darker than the theme writes them... */
	constexpr float GroundShade = 0.8f;
	/** ...and anything brighter than this (in linear light) is pressed down towards it. */
	constexpr float GroundBrightest = 0.3f;

	float GroundHash(int32 X, int32 Y, uint32 Seed)
	{
		uint32 H = static_cast<uint32>(X) * 374761393u + static_cast<uint32>(Y) * 668265263u + Seed * 2246822519u;
		H = (H ^ (H >> 13)) * 1274126177u;
		H ^= H >> 16;
		return static_cast<float>(H & 0xffffffu) / static_cast<float>(0xffffffu);
	}

	/** Smooth value noise in 0..1, about one bump per unit. */
	float GroundNoise(float X, float Y, uint32 Seed)
	{
		const int32 X0 = FMath::FloorToInt(X);
		const int32 Y0 = FMath::FloorToInt(Y);
		const float FX = X - X0;
		const float FY = Y - Y0;
		const float SX = FX * FX * (3.0f - 2.0f * FX);
		const float SY = FY * FY * (3.0f - 2.0f * FY);
		const float A = FMath::Lerp(GroundHash(X0, Y0, Seed), GroundHash(X0 + 1, Y0, Seed), SX);
		const float B = FMath::Lerp(GroundHash(X0, Y0 + 1, Seed), GroundHash(X0 + 1, Y0 + 1, Seed), SX);
		return FMath::Lerp(A, B, SY);
	}
}

bool ATMBattleDirector::BuildSmoothGround(const FTMTheme& Theme)
{
	const TMSim::FMap& Map = Battle.Map;
	if (Map.TilesX <= 0 || Map.TilesY <= 0)
	{
		return false;
	}
	const float M = TileSize;  // centimetres to a metre
	const float Level = LevelCm();
	const uint32 Seed = GetTypeHash(FString(UTF8_TO_TCHAR(Setup.MapId.c_str()))) ^ GetTypeHash(Theme.Id);
	auto TopColour = [&Theme](int32 Height)
	{
		return Theme.Tops.Num() > 0 ? Theme.Tops[FMath::Clamp(Height - 1, 0, Theme.Tops.Num() - 1)] : Theme.Side;
	};

	// Each tile's height in centimetres and its colour, as the old pieces had them.
	auto TileLook = [&](int32 TX, int32 TY, float& Height, FLinearColor& Colour)
	{
		TX = FMath::Clamp(TX, 0, Map.TilesX - 1);
		TY = FMath::Clamp(TY, 0, Map.TilesY - 1);
		const int32 Index = TY * Map.TilesX + TX;
		const int32 Steps = Map.TileLevel(TX, TY);
		if (Steps > 0)
		{
			Height = Steps * Level;
			Colour = TopColour(Steps);
			if (Map.Hazards[Index] < 0)
			{
				// Darkened; the scorch and the fire stand on it (TMBattleDirectorHazards.cpp).
				Colour = FMath::Lerp(Colour, Theme.Embers, 0.4f);
			}
		}
		else if (Map.Covers[Index] != 0)
		{
			// Rock stands on a floor a step up.
			Height = Level;
			Colour = FMath::Lerp(TopColour(1), Theme.Side, 0.35f);
		}
		else
		{
			// A water bed, under the water's surface.
			Height = 20.0f;
			Colour = Theme.Water * 0.35f;
		}
	};

	// The grid, with a rim round the board.
	const int32 NX = Map.TilesX * GroundPerTile + 2 * GroundRim + 1;
	const int32 NY = Map.TilesY * GroundPerTile + 2 * GroundRim + 1;
	TArray<float> Heights;
	TArray<FLinearColor> Colours;
	TArray<float> Outside;  // 0 on the board, rising to 1 at the rim's far edge
	Heights.SetNumZeroed(NX * NY);
	Colours.SetNumZeroed(NX * NY);
	Outside.SetNumZeroed(NX * NY);
	const int32 BoardX = Map.TilesX * GroundPerTile;
	const int32 BoardY = Map.TilesY * GroundPerTile;
	for (int32 J = 0; J < NY; ++J)
	{
		for (int32 I = 0; I < NX; ++I)
		{
			// Where it is on the board, held to the board's edge for the rim.
			const int32 BI = FMath::Clamp(I - GroundRim, 0, BoardX);
			const int32 BJ = FMath::Clamp(J - GroundRim, 0, BoardY);
			// The tiles it touches: one inside a tile, two on an edge, four at a
			// corner. Their average is where heights meet.
			const bool bEdgeX = BI % GroundPerTile == 0;
			const bool bEdgeY = BJ % GroundPerTile == 0;
			const int32 TX = BI / GroundPerTile;
			const int32 TY = BJ / GroundPerTile;
			float Sum = 0.0f;
			FLinearColor Blend(0, 0, 0, 0);
			int32 Count = 0;
			for (int32 DY = bEdgeY ? -1 : 0; DY <= 0; ++DY)
			{
				for (int32 DX = bEdgeX ? -1 : 0; DX <= 0; ++DX)
				{
					const int32 X = TX + DX;
					const int32 Y = TY + DY;
					if (X < 0 || Y < 0 || X >= Map.TilesX || Y >= Map.TilesY)
					{
						continue;
					}
					float H = 0.0f;
					FLinearColor C;
					TileLook(X, Y, H, C);
					Sum += H;
					Blend += C;
					++Count;
				}
			}
			const int32 Index = J * NX + I;
			Heights[Index] = Count > 0 ? Sum / Count : 0.0f;
			Colours[Index] = Count > 0 ? Blend / static_cast<float>(Count) : Theme.Outside;
			// Beyond the board: rolling down to the land, over the rim.
			const float Out = FMath::Max(FMath::Abs(static_cast<float>(I - GroundRim - BI)), FMath::Abs(static_cast<float>(J - GroundRim - BJ)));
			Outside[Index] = FMath::Clamp(Out / GroundRim, 0.0f, 1.0f);
		}
	}

	// Rounded where tiles meet: the vertices on tile edges are eased toward
	// their neighbours, which rounds corners; node centres are never touched.
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		const TArray<float> Before = Heights;
		for (int32 J = 1; J < NY - 1; ++J)
		{
			for (int32 I = 1; I < NX - 1; ++I)
			{
				const int32 BI = I - GroundRim;
				const int32 BJ = J - GroundRim;
				const bool bOnBoard = BI >= 0 && BJ >= 0 && BI <= BoardX && BJ <= BoardY;
				if (!bOnBoard || (BI % GroundPerTile != 0 && BJ % GroundPerTile != 0))
				{
					continue;
				}
				float Sum = 0.0f;
				for (int32 DY = -1; DY <= 1; ++DY)
				{
					for (int32 DX = -1; DX <= 1; ++DX)
					{
						Sum += Before[(J + DY) * NX + I + DX];
					}
				}
				Heights[J * NX + I] = FMath::Lerp(Before[J * NX + I], Sum / 9.0f, 0.6f);
			}
		}
	}

	// The rim: a rounded bank down from the board's edge to just under the land.
	for (int32 Index = 0; Index < NX * NY; ++Index)
	{
		const float T = Outside[Index];
		if (T > 0.0f)
		{
			const float Ease = T * T * (3.0f - 2.0f * T);
			Heights[Index] = FMath::Lerp(Heights[Index], -3.0f, Ease);
			Colours[Index] = FMath::Lerp(FMath::Lerp(Colours[Index], Theme.Side, 0.5f), Theme.Outside, Ease);
		}
	}

	// The mesh: positions (tile edges wandering a little), normals from the
	// slope, colours shaded by slope and mottled.
	TArray<FVector> Points;
	TArray<FVector> Normals;
	TArray<FVector2D> Uvs;
	TArray<FLinearColor> Tints;
	TArray<FProcMeshTangent> Tangents;
	Points.SetNumUninitialized(NX * NY);
	Normals.SetNumUninitialized(NX * NY);
	Uvs.SetNumUninitialized(NX * NY);
	Tints.SetNumUninitialized(NX * NY);
	const float Cm = GroundStep * M;
	const float CliffSlope = Level / (2.0f * Cm);
	for (int32 J = 0; J < NY; ++J)
	{
		for (int32 I = 0; I < NX; ++I)
		{
			const int32 Index = J * NX + I;
			const float X = (I - GroundRim) * GroundStep;
			const float Y = (J - GroundRim) * GroundStep;
			// Only the edge vertices wander, and only along the ground.
			const int32 BI = I - GroundRim;
			const int32 BJ = J - GroundRim;
			float OX = 0.0f;
			float OY = 0.0f;
			if (BI % GroundPerTile == 0 || BJ % GroundPerTile == 0)
			{
				OX = (GroundNoise(X * 0.9f, Y * 0.9f, Seed) - 0.5f) * 2.0f * EdgeWander;
				OY = (GroundNoise(X * 0.9f + 31.7f, Y * 0.9f + 17.3f, Seed) - 0.5f) * 2.0f * EdgeWander;
			}
			Points[Index] = FVector((X + OX) * M, (Y + OY) * M, Heights[Index]);
			Uvs[Index] = FVector2D(X * 0.5f, Y * 0.5f);

			const float Left = Heights[J * NX + FMath::Max(I - 1, 0)];
			const float Right = Heights[J * NX + FMath::Min(I + 1, NX - 1)];
			const float Down = Heights[FMath::Max(J - 1, 0) * NX + I];
			const float Up = Heights[FMath::Min(J + 1, NY - 1) * NX + I];
			const FVector Normal = FVector(Left - Right, Down - Up, 2.0f * Cm).GetSafeNormal();
			Normals[Index] = Normal;

			// A bank shows the theme's side colour; flat ground its top colour,
			// mottled slowly so no two stretches are quite the same.
			const float Slope = FMath::Clamp(FVector2D(Right - Left, Up - Down).Size() / (2.0f * Cm) / FMath::Max(CliffSlope, 0.01f), 0.0f, 1.0f);
			FLinearColor Colour = FMath::Lerp(Colours[Index], Theme.Side, Slope * 0.75f * (1.0f - Outside[Index]));
			const float Mottle = 1.0f + (GroundNoise(X * 0.35f, Y * 0.35f, Seed + 7) - 0.5f) * Theme.Jitter * 5.0f
				+ (GroundNoise(X * 1.7f, Y * 1.7f, Seed + 11) - 0.5f) * Theme.Jitter * 1.5f;
			Colour = Colour * Mottle;
			// Calmer than the theme's colours as written, which in full sun read
			// as glare: a little darker overall, and the brightest ground (snow,
			// pale stone) held well down, so units and effects stand out on it.
			Colour = Colour * GroundShade;
			const float Lum = Colour.R * 0.3f + Colour.G * 0.59f + Colour.B * 0.11f;
			if (Lum > GroundBrightest)
			{
				Colour = Colour * ((GroundBrightest + (Lum - GroundBrightest) * 0.25f) / Lum);
			}
			Colour.A = 1.0f;
			Tints[Index] = Colour;
		}
	}
	TArray<int32> Triangles;
	Triangles.Reserve((NX - 1) * (NY - 1) * 6);
	for (int32 J = 0; J < NY - 1; ++J)
	{
		for (int32 I = 0; I < NX - 1; ++I)
		{
			const int32 A = J * NX + I;
			const int32 B = A + 1;
			const int32 C = A + NX + 1;
			const int32 D = A + NX;
			// Split each square along whichever diagonal follows the ground, so
			// banks do not come out saw-toothed.
			if (FMath::Abs(Heights[A] - Heights[C]) <= FMath::Abs(Heights[B] - Heights[D]))
			{
				Triangles.Append({ A, B, C, A, C, D });
			}
			else
			{
				// The order UKismetProceduralMeshLibrary's grids use, facing up.
				Triangles.Append({ A, B, D, B, C, D });
			}
		}
	}

	// Every triangle is laid both ways round, so the ground is solid from any
	// angle: from low down, past a cliff or over the board's edge, the camera
	// could see through the backs of steep banks into the void beneath.
	const int32 OneSide = Triangles.Num();
	Triangles.Reserve(OneSide * 2);
	for (int32 k = 0; k < OneSide; k += 3)
	{
		Triangles.Append({ Triangles[k], Triangles[k + 2], Triangles[k + 1] });
	}

	UProceduralMeshComponent* GroundMesh = NewObject<UProceduralMeshComponent>(this, NAME_None, RF_Transient);
	GroundMesh->SetMobility(EComponentMobility::Movable);
	GroundMesh->SetupAttachment(RootComponent);
	GroundMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GroundMesh->bUseAsyncCooking = true;
	GroundMesh->RegisterComponent();
	GroundMesh->CreateMeshSection_LinearColor(0, Points, Triangles, Normals, Uvs, Tints, Tangents, false);
	// The walk and ability indicators and the fog of war lie on it.
	GroundMesh->SetReceivesDecals(true);
	UMaterialInterface* Vertex = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/UI/M_GroundVertex.M_GroundVertex"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (Vertex)
	{
		GroundMesh->SetMaterial(0, Vertex);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("no /Game/UI/M_GroundVertex (Tools/make_ground_material.py): the ground is one colour"));
		GroundMesh->SetMaterial(0, Paint(TopColour(1)));
	}
	BoardProps.Add(GroundMesh);

	// Water: one sheet over the whole board at the water's height. Dry ground is
	// higher, so it shows only over the water beds, following their banks.
	bool bWater = false;
	for (int32 Index = 0; Index < Map.TilesX * Map.TilesY && !bWater; ++Index)
	{
		bWater = Map.Heights[Index] <= 0 && Map.Covers[Index] == 0;
	}
	UMaterialInterface* Clear = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/Widget3DPassThrough_Translucent.Widget3DPassThrough_Translucent"));
	if (bWater && Clear)
	{
		const FVector2D Board(Map.TilesX * TMSim::Ground::TileSize * M, Map.TilesY * TMSim::Ground::TileSize * M);
		if (UStaticMeshComponent* Sheet = Shape(TEXT("Plane"), FVector(Board.X * 0.5f, Board.Y * 0.5f, Level * 0.62f),
			FVector(Board.X, Board.Y, 100.0f), FRotator::ZeroRotator, Theme.Water))
		{
			if (!WhitePixels)
			{
				WhitePixels = NewObject<UTextureRenderTarget2D>(this);
				WhitePixels->RenderTargetFormat = RTF_RGBA8;
				WhitePixels->ClearColor = FLinearColor::White;
				WhitePixels->InitAutoFormat(4, 4);
				WhitePixels->UpdateResourceImmediate(true);
			}
			UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Clear, this);
			Mid->SetTextureParameterValue(TEXT("SlateUI"), WhitePixels);
			Mid->SetVectorParameterValue(TEXT("TintColorAndOpacity"), FLinearColor(Theme.Water.R, Theme.Water.G, Theme.Water.B, Theme.WaterOpacity));
			Sheet->SetMaterial(0, Mid);
		}
	}
	UE_LOG(LogTemp, Log, TEXT("smooth ground: %d by %d vertices"), NX, NY);
	return true;
}
