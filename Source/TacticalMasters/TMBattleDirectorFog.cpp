// Fog of war, drawn on the ground.
//
// The rules already decide what a side can see (FBattle::CanSee: each unit's
// sight, blocked by rock and height, and any watchtower it holds), and units
// this screen's side cannot see are already hidden. This shows the rest:
//
//   - seen now: clear, with a line round the edge of it, and a gold line round
//     the edge of what the unit being ordered sees by itself;
//   - seen before, not now: greyed -- the ground and what stands on it, but no
//     units;
//   - never seen: dark. The shape of the ground still shows through, but
//     nothing on it: its rocks, hazards and watchtowers stay hidden until a
//     unit of this side has seen that spot.
//
// It is painted into a picture the size of the board and laid on the ground by
// a decal, as the walk and ability indicators are, with the same material
// (/Game/UI/M_GroundIndicator). Without the material there is no fog on the
// ground, and the units are hidden as before.
//
// Presentation only: nothing here is read by the rules. What is explored is
// this screen's memory, not the battle's.

#include "TMBattleDirector.h"
#include "TMLines.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/DecalComponent.h"
#include "Engine/Canvas.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace
{
	/** Metres across a fog cell. */
	constexpr float FogCell = 1.0f;
	/** Picture pixels to a metre of board. */
	constexpr float FogPpm = 12.0f;
	/** How dark each kind of ground is drawn: seen now, seen before, never seen. */
	constexpr float ClearAlpha = 0.0f;
	constexpr float GreyAlpha = 0.62f;
	constexpr float DarkAlpha = 0.9f;
	/** The post-process fog (M_FogOfWar), when it is there, over everything. */
	const TCHAR* const FogPostPath = TEXT("/Game/UI/M_FogOfWar.M_FogOfWar");
	/** The fog's own colour: a cold grey that reads as dusk rather than paint. */
	const FLinearColor FogShade(0.035f, 0.04f, 0.055f);
	/** The edge of what the side sees, and of what the unit being ordered sees. */
	const FLinearColor SideEdge(0.78f, 0.9f, 1.0f, 0.9f);
	const FLinearColor UnitEdge(1.0f, 0.84f, 0.35f, 1.0f);
	constexpr float SideEdgeWidth = 2.0f;
	constexpr float UnitEdgeWidth = 2.5f;

	/**
	 * The edges of a set of cells, smoothed across each square of four cell
	 * centres (marching squares), in picture pixels: a line where inside meets
	 * outside.
	 */
	[[maybe_unused]] void FogEdges(const TArray<uint8>& In, int32 W, int32 H, TArray<TPair<FVector2D, FVector2D>>& Out)
	{
		// Off the board counts as inside, so no line is drawn along the board's own edge.
		auto Has = [&](int32 X, int32 Y) { return X < 0 || Y < 0 || X >= W || Y >= H || In[Y * W + X] != 0; };
		auto At = [](float X, float Y) { return FVector2D((X + 0.5f) * FogCell * FogPpm, (Y + 0.5f) * FogCell * FogPpm); };
		for (int32 Y = -1; Y < H; ++Y)
		{
			for (int32 X = -1; X < W; ++X)
			{
				const bool A = Has(X, Y), B = Has(X + 1, Y), C = Has(X + 1, Y + 1), D = Has(X, Y + 1);
				const FVector2D T = At(X + 0.5f, Y), R = At(X + 1, Y + 0.5f), Bo = At(X + 0.5f, Y + 1), L = At(X, Y + 0.5f);
				switch ((A ? 8 : 0) | (B ? 4 : 0) | (C ? 2 : 0) | (D ? 1 : 0))
				{
				case 8: case 7: Out.Add({ T, L }); break;
				case 4: case 11: Out.Add({ T, R }); break;
				case 2: case 13: Out.Add({ R, Bo }); break;
				case 1: case 14: Out.Add({ L, Bo }); break;
				case 12: case 3: Out.Add({ L, R }); break;
				case 6: case 9: Out.Add({ T, Bo }); break;
				case 10: Out.Add({ T, R }); Out.Add({ Bo, L }); break;
				case 5: Out.Add({ L, T }); Out.Add({ R, Bo }); break;
				default: break;
				}
			}
		}
	}
}

int32 ATMBattleDirector::FogCellOf(const TMSim::FVec2& Point) const
{
	const int32 X = FMath::FloorToInt(Point.X / FogCell);
	const int32 Y = FMath::FloorToInt(Point.Y / FogCell);
	if (X < 0 || Y < 0 || X >= FogCellsX || Y >= FogCellsY)
	{
		return -1;
	}
	return Y * FogCellsX + X;
}

void ATMBattleDirector::MarkFogged(int32 From, const FVector& Where)
{
	// Worked out when the board is built, before the cells are: from metres.
	const TMSim::FVec2 Size = Battle.Map.SizeMeters();
	const int32 W = FMath::Max(1, FMath::CeilToInt(Size.X / FogCell));
	const int32 H = FMath::Max(1, FMath::CeilToInt(Size.Y / FogCell));
	const int32 X = FMath::FloorToInt(Where.X / TileSize / FogCell);
	const int32 Y = FMath::FloorToInt(Where.Y / TileSize / FogCell);
	if (X < 0 || Y < 0 || X >= W || Y >= H)
	{
		return;
	}
	for (int32 k = From; k < BoardProps.Num(); ++k)
	{
		if (BoardProps[k])
		{
			FogProps.Add(BoardProps[k].Get());
			FogPropCell.Add(Y * W + X);
		}
	}
}

UTextureRenderTarget2D* ATMBattleDirector::FilmOfSize(UTextureRenderTarget2D* Film, int32 W, int32 H, bool bClamp)
{
	W = FMath::Max(1, W);
	H = FMath::Max(1, H);
	if (Film && Film->SizeX == W && Film->SizeY == H)
	{
		return Film;
	}
	if (Film)
	{
		RetiredFilms.Add(Film);
		// Long out of use by the time a ninth replacement comes.
		while (RetiredFilms.Num() > 8)
		{
			RetiredFilms.RemoveAt(0);
		}
	}
	UTextureRenderTarget2D* Made = NewObject<UTextureRenderTarget2D>(this);
	Made->RenderTargetFormat = RTF_RGBA8;
	Made->ClearColor = FLinearColor::Transparent;
	if (bClamp)
	{
		Made->AddressX = TA_Clamp;
		Made->AddressY = TA_Clamp;
	}
	Made->InitAutoFormat(W, H);
	Made->UpdateResourceImmediate(true);
	return Made;
}

void ATMBattleDirector::BuildFog()
{
	const TMSim::FVec2 Size = Battle.Map.SizeMeters();
	FogCellsX = FMath::Max(1, FMath::CeilToInt(Size.X / FogCell));
	FogCellsY = FMath::Max(1, FMath::CeilToInt(Size.Y / FogCell));
	FogSeen.Init(0, FogCellsX * FogCellsY);
	FogExplored.Init(0, FogCellsX * FogCellsY);
	FogViewer = -2;
	FogSignature.Reset();

	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/UI/M_GroundIndicator.M_GroundIndicator"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	UMaterialInterface* Post = LoadObject<UMaterialInterface>(nullptr, FogPostPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if ((!Base && !Post) || !GetWorld() || !GetWorld()->IsGameWorld())
	{
		bFogDecal = false;
		bFogPost = false;
		return;
	}
	// Off the board reads as its nearest edge, not as the far side (clamped).
	FogFilm = FilmOfSize(FogFilm, FMath::CeilToInt(Size.X * FogPpm), FMath::CeilToInt(Size.Y * FogPpm), true);
	if (FogDecal)
	{
		if (UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(FogDecal->GetDecalMaterial()))
		{
			Mid->SetTextureParameterValue(TEXT("Paint"), FogFilm);
		}
	}

	// Over everything, when the post-process is there: the ground, trees, rocks
	// and walls all go dark out of sight, not only the ground under them.
	if (Post && Watcher)
	{
		if (!FogPost)
		{
			FogPost = UMaterialInstanceDynamic::Create(Post, this);
			if (UCameraComponent* Lens = Watcher->GetCameraComponent())
			{
				Lens->PostProcessSettings.AddBlendable(FogPost, 1.0f);
			}
		}
		// Where each spot of the board is in the world: its corner, and each axis
		// divided by its length, so a dot product gives the share along it.
		const FTransform Board = GetActorTransform();
		const FVector Corner = Board.TransformPosition(FVector::ZeroVector);
		const FVector AlongX = Board.TransformVector(FVector(Size.X * TileSize, 0.0f, 0.0f));
		const FVector AlongY = Board.TransformVector(FVector(0.0f, Size.Y * TileSize, 0.0f));
		FogPost->SetTextureParameterValue(TEXT("FogTex"), FogFilm);
		FogPost->SetVectorParameterValue(TEXT("BoardOrigin"), FLinearColor(Corner.X, Corner.Y, Corner.Z, 0.0f));
		const FVector AxisX = AlongX / FMath::Max(1.0, AlongX.SizeSquared());
		const FVector AxisY = AlongY / FMath::Max(1.0, AlongY.SizeSquared());
		FogPost->SetVectorParameterValue(TEXT("AxisX"), FLinearColor(AxisX.X, AxisX.Y, AxisX.Z, 0.0f));
		FogPost->SetVectorParameterValue(TEXT("AxisY"), FLinearColor(AxisY.X, AxisY.Y, AxisY.Z, 0.0f));
		FogPost->SetScalarParameterValue(TEXT("SeenBoost"), 0.0f);
		UKismetRenderingLibrary::ClearRenderTarget2D(this, FogFilm, FLinearColor::Transparent);
		bFogPost = true;
		bFogDecal = false;
		if (FogDecal)
		{
			FogDecal->SetVisibility(false);
		}
		return;
	}
	bFogPost = false;
	if (!Base)
	{
		bFogDecal = false;
		return;
	}
	if (!FogDecal)
	{
		FogDecal = NewObject<UDecalComponent>(this, NAME_None, RF_Transient);
		FogDecal->SetupAttachment(RootComponent);
		FogDecal->RegisterComponent();
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Base, this);
		Mid->SetTextureParameterValue(TEXT("Paint"), FogFilm);
		// Only the edge lines glow; the fog itself is dark and gives no light.
		Mid->SetScalarParameterValue(TEXT("Glow"), 0.5f);
		FogDecal->SetDecalMaterial(Mid);
		// Under the walk and ability indicators, which stay readable over it.
		FogDecal->SortOrder = 0;
	}
	// Laid exactly as the indicators are (BuildIndicators).
	const float Tall = 12.0f * LevelCm();
	FogDecal->SetRelativeLocation(FVector(Size.X * 0.5f * TileSize, Size.Y * 0.5f * TileSize, Tall * 0.5f));
	FogDecal->SetRelativeRotation(FRotator(-90.0f, 0.0f, IndicatorRoll));
	FogDecal->DecalSize = FVector(Tall * 0.5f + 50.0f, Size.Y * 0.5f * TileSize, Size.X * 0.5f * TileSize);
	FogDecal->MarkRenderStateDirty();
	UKismetRenderingLibrary::ClearRenderTarget2D(this, FogFilm, FLinearColor::Transparent);
	bFogDecal = true;
}

void ATMBattleDirector::AdvanceFog()
{
	if (!bFogPost && !bFogPostRetried && Watcher && GetWorld() && GetWorld()->IsGameWorld())
	{
		bFogPostRetried = true;
		BuildFog();
	}
	const int32 Cells = FogCellsX * FogCellsY;
	if (Cells <= 0 || FogSeen.Num() != Cells)
	{
		return;
	}
	const int32 Viewer = ViewerTeam();
	const bool bFog = Viewer >= 0 && Battle.Winner == -1 && Screen == EScreen::Battle;
	const TMSim::FUnit* Mine = SelectedUnit();
	if (Mine && (Mine->Team != Viewer || !Mine->IsAlive()))
	{
		Mine = nullptr;
	}

	// What it depends on, in a line: worked out again only when that changes.
	FString Signature = FString::Printf(TEXT("%d/%d/%d/%d"), bFog ? 1 : 0, Viewer, Mine ? Mine->Id : -1, Cells);
	if (bFog)
	{
		for (const TMSim::FUnit& Unit : Battle.Units)
		{
			if (Unit.IsAlive() && Unit.Team == Viewer)
			{
				const TMSim::FNode At = TMSim::FMap::NodeOf(Unit.Pos);
				Signature += FString::Printf(TEXT("|%d:%d,%d:%.1f"), Unit.Id, At.X, At.Y, Battle.SightOf(Unit));
			}
		}
		for (const TMSim::FWatchtower& Tower : Battle.Watchtowers)
		{
			Signature += FString::Printf(TEXT("|t%d"), Tower.Owner);
		}
		// Ground zones that see for this side (2026-10-04).
		for (const TMSim::FBattle::FZone& Zone : Battle.Zones)
		{
			const TMSim::FAbility* Laid = Battle.ZoneAbility(Zone);
			if (Zone.Team == Viewer && Laid && Laid->ZoneSight > 0.0f)
			{
				Signature += FString::Printf(TEXT("|z%d:%hs:%.1f,%.1f"), Zone.Owner, Zone.AbilityId.c_str(), Zone.Target.X, Zone.Target.Y);
			}
		}
	}
	// A tower just taken spreads its sight over TowerKindleSeconds, with its fire
	// (2026-10-02): sixteen steps, each redrawing only the spread on top of what
	// the side otherwise sees, which is kept from the step before.
	FString Steady = Signature;
	for (int32 i = 0; bFog && i < static_cast<int32>(Battle.Watchtowers.size()); ++i)
	{
		const float Kindle = TowerKindle(i);
		if (Battle.Watchtowers[static_cast<size_t>(i)].Owner == Viewer && Kindle < 1.0f)
		{
			Steady += FString::Printf(TEXT("|c%d"), i);
			Signature += FString::Printf(TEXT("|c%d:%d"), i, FMath::FloorToInt(Kindle * 16.0f));
		}
	}
	if (Signature == FogSignature)
	{
		return;
	}
	FogSignature = Signature;
	const bool bSteadyAgain = Steady != FogSteadySignature || FogSteadySeen.Num() != Cells;
	FogSteadySignature = Steady;
	if (bSteadyAgain)
	{
		FogSteadySeen.Init(0, Cells);
	}
	const bool bKindling = bFog && AnyTowerKindling(Viewer);

	// No fog: two people at one screen, nobody playing, or the battle is over.
	if (!bFog)
	{
		for (int32 k = 0; k < FogProps.Num(); ++k)
		{
			if (USceneComponent* Prop = FogProps[k].Get())
			{
				Prop->SetVisibility(true);
			}
		}
		if ((bFogDecal || bFogPost) && FogFilm)
		{
			UKismetRenderingLibrary::ClearRenderTarget2D(this, FogFilm, FLinearColor::Transparent);
		}
		if (FogPost)
		{
			FogPost->SetScalarParameterValue(TEXT("SeenBoost"), 0.0f);
		}
		return;
	}
	if (Viewer != FogViewer)
	{
		FogViewer = Viewer;
		FogExplored.Init(0, Cells);
	}

	// What the side sees now, cell by cell, from the middle of each; and what
	// the unit being ordered sees by itself.
	for (int32 Y = 0; Y < FogCellsY; ++Y)
	{
		for (int32 X = 0; X < FogCellsX; ++X)
		{
			const int32 Index = Y * FogCellsX + X;
			const TMSim::FVec2 Middle((X + 0.5f) * FogCell, (Y + 0.5f) * FogCell);
			if (bSteadyAgain)
			{
				FogSteadySeen[Index] = Battle.InBounds(Middle)
					&& (bKindling ? SeenWithoutKindling(Viewer, Middle) : Battle.CanSee(Viewer, Middle)) ? 1 : 0;
			}
			const bool bSees = FogSteadySeen[Index] || (bKindling && Battle.InBounds(Middle) && KindlingReaches(Viewer, Middle));
			FogSeen[Index] = bSees ? 1 : 0;
			FogExplored[Index] |= FogSeen[Index];
		}
	}

	// What stands on the board shows once its spot, or ground right next to it,
	// has been seen: rock blocks sight into itself, so its own cell may never be.
	auto NearExplored = [this](int32 Cell)
	{
		if (!FogExplored.IsValidIndex(Cell))
		{
			return true;
		}
		const int32 CX = Cell % FogCellsX;
		const int32 CY = Cell / FogCellsX;
		for (int32 DY = -2; DY <= 2; ++DY)
		{
			for (int32 DX = -2; DX <= 2; ++DX)
			{
				const int32 X = CX + DX;
				const int32 Y = CY + DY;
				if (X >= 0 && Y >= 0 && X < FogCellsX && Y < FogCellsY && FogExplored[Y * FogCellsX + X])
				{
					return true;
				}
			}
		}
		return false;
	};
	for (int32 k = 0; k < FogProps.Num(); ++k)
	{
		if (USceneComponent* Prop = FogProps[k].Get())
		{
			Prop->SetVisibility(NearExplored(FogPropCell[k]));
		}
	}

	if ((!bFogDecal && !bFogPost) || !FogFilm)
	{
		return;
	}
	if (FogPost)
	{
		// What is in sight is drawn a little brighter than it would be.
		FogPost->SetScalarParameterValue(TEXT("SeenBoost"), 0.12f);
	}
	UKismetRenderingLibrary::ClearRenderTarget2D(this, FogFilm, FLinearColor::Transparent);
	UCanvas* Canvas = nullptr;
	FVector2D CanvasSize;
	FDrawToRenderTargetContext Context;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, FogFilm, Canvas, CanvasSize, Context);
	if (!Canvas)
	{
		return;
	}

	// The shade, as a grid through the cells' corners, each corner as dark as
	// the cells around it on average, so the fog thins softly at its edges.
	auto Darkness = [&](int32 X, int32 Y)
	{
		const int32 Index = Y * FogCellsX + X;
		return FogSeen[Index] ? ClearAlpha : FogExplored[Index] ? GreyAlpha : DarkAlpha;
	};
	const int32 CW = FogCellsX + 1;
	TArray<float> Corner;
	Corner.SetNumZeroed(CW * (FogCellsY + 1));
	for (int32 Y = 0; Y <= FogCellsY; ++Y)
	{
		for (int32 X = 0; X <= FogCellsX; ++X)
		{
			float Sum = 0.0f;
			int32 Count = 0;
			// Four cells each way round the corner, not two: the fog thins over
			// two metres instead of one, so its edge is soft, not stepped.
			for (int32 DY = -2; DY <= 1; ++DY)
			{
				for (int32 DX = -2; DX <= 1; ++DX)
				{
					const int32 CX = X + DX;
					const int32 CY = Y + DY;
					if (CX >= 0 && CY >= 0 && CX < FogCellsX && CY < FogCellsY)
					{
						Sum += Darkness(CX, CY);
						++Count;
					}
				}
			}
			Corner[Y * CW + X] = Count > 0 ? Sum / Count : DarkAlpha;
		}
	}
	TArray<FCanvasUVTri> Tris;
	Tris.Reserve(FogCellsX * FogCellsY * 2);
	auto Vertex = [&](int32 X, int32 Y, FVector2D& Pos, FLinearColor& Colour)
	{
		Pos = FVector2D(X * FogCell * FogPpm, Y * FogCell * FogPpm);
		// The post-process reads how deep the fog is from red; the decal lays a
		// dark shade with that much opacity.
		Colour = bFogPost ? FLinearColor(Corner[Y * CW + X], 0.0f, 0.0f, 1.0f)
			: FLinearColor(FogShade.R, FogShade.G, FogShade.B, Corner[Y * CW + X]);
	};
	for (int32 Y = 0; Y < FogCellsY; ++Y)
	{
		for (int32 X = 0; X < FogCellsX; ++X)
		{
			// A cell clear at all four corners needs nothing drawn.
			const float A = Corner[Y * CW + X], B = Corner[Y * CW + X + 1], C = Corner[(Y + 1) * CW + X + 1], D = Corner[(Y + 1) * CW + X];
			if (A <= 0.0f && B <= 0.0f && C <= 0.0f && D <= 0.0f)
			{
				continue;
			}
			FCanvasUVTri T1;
			Vertex(X, Y, T1.V0_Pos, T1.V0_Color);
			Vertex(X + 1, Y, T1.V1_Pos, T1.V1_Color);
			Vertex(X + 1, Y + 1, T1.V2_Pos, T1.V2_Color);
			Tris.Add(T1);
			FCanvasUVTri T2;
			Vertex(X, Y, T2.V0_Pos, T2.V0_Color);
			Vertex(X + 1, Y + 1, T2.V1_Pos, T2.V1_Color);
			Vertex(X, Y + 1, T2.V2_Pos, T2.V2_Color);
			Tris.Add(T2);
		}
	}
	if (Tris.Num() > 0)
	{
		Canvas->K2_DrawTriangle(nullptr, Tris);
	}

	// No lines round what is seen: the human found them too bright (2026-09-30).
	// The fog's own dark and light say where sight ends.
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Context);
}
