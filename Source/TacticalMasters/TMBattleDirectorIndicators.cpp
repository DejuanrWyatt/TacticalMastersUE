// Indicators on the ground, as League of Legends draws them: the area a unit
// can walk, an ability's range, and the shape it would strike, painted onto the
// board under the units.
//
// One decal covers the whole board. What it shows is a picture the size of the
// board, painted here with the canvas -- a filled, glowing area for a walk, a
// range circle, a ring, a cone, a line, a charge -- and projected straight down,
// so it follows every step and slope. Only the ground's surface takes it (the
// units, cliff faces, rock and trees do not), so everything stands on top of it.
// The picture is painted again only when what it shows changes.
//
// 2026-10-06 (v26 play test, "hard to see the movement ground indicators"): v26's
// fade on steep faces also faded the marks off every grass blade, which stands
// nearly upright, so in tall grass the walk area all but vanished. The fade now
// works only near cliffs: a second picture, the cliff mask, is white within a
// metre of every tall step (as TMBattleDirectorCliffs.cpp faces them with rock)
// and the material fades by steepness only where it is white (the human's pick 2).
//
// It needs /Game/UI/M_GroundIndicator (Tools/make_indicator_material.py).
// Without it the HUD draws the old outlines instead. Nothing here is read by
// the rules; the shapes are drawn from the rules' own numbers (InShape).

#include "TMBattleDirector.h"

#include "Components/DecalComponent.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

#include "SimAbility.h"
#include "TMLines.h"

// Named, not anonymous, so its names stay out of the files a unity build puts after it.
namespace TMIndicatorPaint
{
	/** Picture pixels to a metre of board, as everything here is measured. */
	constexpr float Ppm = 24.0f;
	/**
	 * 2026-10-06 ("sharper lines ... too blurry at certain angles"): the picture is
	 * made this many times finer than Ppm. Everything is still measured at 24 to a
	 * metre and scaled up as it is drawn (FPainter::Flush), so every line, ring and
	 * fill keeps its size on the ground, only drawn with three times the pixels.
	 */
	constexpr float Supersample = 3.0f;
	const FLinearColor Teal(0.3f, 0.95f, 0.9f);
	const FLinearColor Sprint(1.0f, 0.62f, 0.2f);
	const FLinearColor Refused(1.0f, 0.32f, 0.28f);
	const FLinearColor PathGold(1.0f, 0.88f, 0.35f);
	/** Zones of control (2026-10-02): an enemy tank's, and your own. */
	const FLinearColor ZoneOrange(1.0f, 0.54f, 0.36f);
	const FLinearColor ZoneBlue(0.43f, 0.63f, 1.0f);
	/** Width of the line round the walk area, in picture pixels (24 to a metre): as thin as
	 *  the picture allows (2026-10-01: "as thin as possible"), and no glow round it. */
	constexpr float MoveEdgeWidth = 1.0f;
	/** Width of the walk's path line, in picture pixels; no glow round it either. */
	constexpr float PathWidth = 1.25f;
	/** Width of every aiming line -- a cone's edges, a lane, a charge, a ring where a
	 *  blow lands -- in picture pixels: thin, and no glow (2026-10-01); a little firmer
	 *  with its dark rim (2026-10-06, the human's pick C: "a 2 px edge, a dark rim"). */
	constexpr float AimWidth = 1.75f;
	/**
	 * 2026-10-06 ("all outlines ... sharper", pick C): every edge -- a ring, a line, the
	 * walk's outline -- gets a thin dark line each side of it, so it stops at a clear
	 * edge on bright grass as well as on dark ground. In picture pixels, and how dark.
	 */
	constexpr float RimWidth = 0.9f;
	constexpr float RimOpacity = 0.45f;
	/** How much of a fill's own opacity is kept: nearly none (2026-10-03). */
	constexpr float GroundFillOpacity = 0.3f;

	/** Painting into the picture, in metres. */
	struct FPainter
	{
		UCanvas* Canvas = nullptr;
		TArray<FCanvasUVTri> Tris;
		/** The edges' dark rims: drawn first, under everything, so no edge is cut by another's rim. */
		TArray<FCanvasUVTri> RimTris;

		FVector2D P(const TMSim::FVec2& M) const { return FVector2D(M.X * Ppm, M.Y * Ppm); }
		FVector2D P(double X, double Y) const { return FVector2D(X * Ppm, Y * Ppm); }

		void Tri(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FLinearColor& Colour)
		{
			FCanvasUVTri T;
			T.V0_Pos = A; T.V1_Pos = B; T.V2_Pos = C;
			T.V0_Color = T.V1_Color = T.V2_Color = Colour;
			Tris.Add(T);
		}
		/**
		 * A fill's colour, nearly see-through (2026-10-03: "the brightness of the
		 * fill-in ground effects makes it hard to understand the targeting").
		 * Fills are the faint colours (under half opaque); dots, rims and lines
		 * keep theirs, so the shapes still read by their edges.
		 */
		static FLinearColor Faint(const FLinearColor& Colour)
		{
			return Colour.A < 0.5f ? FLinearColor(Colour.R, Colour.G, Colour.B, Colour.A * GroundFillOpacity) : Colour;
		}
		/** A convex shape, as a fan from its first corner. */
		void Poly(const TArray<FVector2D>& Pts, const FLinearColor& Colour)
		{
			const FLinearColor Fill = Faint(Colour);
			for (int32 i = 1; i + 1 < Pts.Num(); ++i)
			{
				Tri(Pts[0], Pts[i], Pts[i + 1], Fill);
			}
		}
		/** An edge (bright, thin) gets a dark rim; a fill (faint) or a broad band does not. */
		static bool IsEdge(const FLinearColor& Colour, float Width)
		{
			return Colour.A >= 0.5f && Width <= 4.0f;
		}
		void RimQuad(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FVector2D& D, float Strength)
		{
			FCanvasUVTri T;
			T.V0_Color = T.V1_Color = T.V2_Color = FLinearColor(0.0f, 0.0f, 0.0f, RimOpacity * FMath::Min(1.0f, Strength));
			T.V0_Pos = A; T.V1_Pos = B; T.V2_Pos = C;
			RimTris.Add(T);
			T.V0_Pos = A; T.V1_Pos = C; T.V2_Pos = D;
			RimTris.Add(T);
		}
		void Flush()
		{
			if (Tris.Num() > 0 || RimTris.Num() > 0)
			{
				TArray<FCanvasUVTri> All = MoveTemp(RimTris);
				All.Append(Tris);
				for (FCanvasUVTri& T : All)
				{
					T.V0_Pos *= Supersample;
					T.V1_Pos *= Supersample;
					T.V2_Pos *= Supersample;
				}
				Canvas->K2_DrawTriangle(nullptr, All);
				Tris.Reset();
				RimTris.Reset();
			}
		}
		/** An aiming line: once a glow, now a thin solid line, batched with the
		 *  rest (2026-10-01: "reduce the width of targeting lines"). The width
		 *  each caller asks for is kept only as a hint and no longer used. */
		void Glow(const FVector2D& A, const FVector2D& B, const FLinearColor& Colour, float /*Width*/ = 3.0f)
		{
			Thin(A, B, AimWidth, FLinearColor(Colour.R, Colour.G, Colour.B, 0.95f));
		}
		/** A thin line as two triangles, batched with the rest: far cheaper than a
		 *  line drawn on its own, which is what made repainting lag. */
		void Thin(const FVector2D& A, const FVector2D& B, float Width, const FLinearColor& Colour)
		{
			const FVector2D Along = B - A;
			const double Length = Along.Size();
			if (Length < 1e-3)
			{
				return;
			}
			const FVector2D Across = FVector2D(-Along.Y, Along.X) / Length * (Width * 0.5);
			if (IsEdge(Colour, Width))
			{
				// Its rim: wider by RimWidth each side, and a little longer, to close the joints.
				const FVector2D Wide = Across * ((Width * 0.5 + RimWidth) / (Width * 0.5));
				const FVector2D Reach = Along / Length * (RimWidth * 0.5);
				RimQuad(A - Reach + Wide, B + Reach + Wide, B + Reach - Wide, A - Reach - Wide, Colour.A);
			}
			Tri(A + Across, B + Across, B - Across, Colour);
			Tri(A + Across, B - Across, A - Across, Colour);
		}
		/** A plain line, solid, with no glow round it. */
		void Line(const FVector2D& A, const FVector2D& B, const FLinearColor& Colour, float Width)
		{
			Flush();
			Canvas->K2_DrawLine(A * Supersample, B * Supersample, Width * Supersample, FLinearColor(Colour.R, Colour.G, Colour.B, 1.0f));
		}
		void Arc(const FVector2D& Centre, float Radius, float From, float To, const FLinearColor& Colour, float Width = 3.0f)
		{
			const int32 Steps = FMath::Max(8, FMath::CeilToInt(FMath::Abs(To - From) / 5.0f));
			for (int32 i = 0; i < Steps; ++i)
			{
				const float A0 = FMath::DegreesToRadians(FMath::Lerp(From, To, static_cast<float>(i) / Steps));
				const float A1 = FMath::DegreesToRadians(FMath::Lerp(From, To, static_cast<float>(i + 1) / Steps));
				Glow(Centre + FVector2D(FMath::Cos(A0), FMath::Sin(A0)) * Radius, Centre + FVector2D(FMath::Cos(A1), FMath::Sin(A1)) * Radius, Colour, Width);
			}
		}
		void Disc(const FVector2D& Centre, float Radius, const FLinearColor& InColour, float From = 0.0f, float To = 360.0f)
		{
			const FLinearColor Colour = Faint(InColour);
			const int32 Steps = FMath::Max(12, FMath::CeilToInt(FMath::Abs(To - From) / 5.0f));
			for (int32 i = 0; i < Steps; ++i)
			{
				const float A0 = FMath::DegreesToRadians(FMath::Lerp(From, To, static_cast<float>(i) / Steps));
				const float A1 = FMath::DegreesToRadians(FMath::Lerp(From, To, static_cast<float>(i + 1) / Steps));
				Tri(Centre, Centre + FVector2D(FMath::Cos(A0), FMath::Sin(A0)) * Radius, Centre + FVector2D(FMath::Cos(A1), FMath::Sin(A1)) * Radius, Colour);
			}
		}
		/** A thick band: a ring's edge, as filled quads, so it can be translucent. */
		void Band(const FVector2D& Centre, float Inner, float Outer, const FLinearColor& Colour)
		{
			const int32 Steps = 72;
			const bool bRim = IsEdge(Colour, Outer - Inner);
			const float RimIn = FMath::Max(0.0f, Inner - RimWidth);
			const float RimOut = Outer + RimWidth;
			for (int32 i = 0; i < Steps; ++i)
			{
				const float A0 = UE_TWO_PI * i / Steps;
				const float A1 = UE_TWO_PI * (i + 1) / Steps;
				const FVector2D D0(FMath::Cos(A0), FMath::Sin(A0));
				const FVector2D D1(FMath::Cos(A1), FMath::Sin(A1));
				if (bRim)
				{
					RimQuad(Centre + D0 * RimIn, Centre + D0 * RimOut, Centre + D1 * RimOut, Centre + D1 * RimIn, Colour.A);
				}
				Tri(Centre + D0 * Inner, Centre + D0 * Outer, Centre + D1 * Outer, Colour);
				Tri(Centre + D0 * Inner, Centre + D1 * Outer, Centre + D1 * Inner, Colour);
			}
		}
	};

	FLinearColor Alpha(const FLinearColor& C, float A) { return FLinearColor(C.R, C.G, C.B, A); }

	/** The walk area last worked out (PaintMoveArea), what it was for, and its edge in metres. */
	TArray<FCanvasUVTri> MoveAreaTris;
	TArray<FCanvasUVTri> MoveAreaRims;
	FString MoveAreaKey;
	TArray<TPair<FVector2D, FVector2D>> MoveAreaEdges;

}

void ATMBattleDirector::BuildIndicators()
{
	using namespace TMIndicatorPaint;
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/UI/M_GroundIndicator.M_GroundIndicator"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Base || !GetWorld() || !GetWorld()->IsGameWorld())
	{
		bIndicatorDecal = false;
		return;
	}
	const TMSim::FVec2 Size = Battle.Map.SizeMeters();
	// Never resized in place while the decal draws with it (FilmOfSize).
	IndicatorFilm = FilmOfSize(IndicatorFilm, FMath::CeilToInt(Size.X * Ppm * Supersample), FMath::CeilToInt(Size.Y * Ppm * Supersample), false);
	if (IndicatorDecal)
	{
		if (UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(IndicatorDecal->GetDecalMaterial()))
		{
			Mid->SetTextureParameterValue(TEXT("Paint"), IndicatorFilm);
			PaintCliffMask(Mid);
		}
	}
	if (!IndicatorDecal)
	{
		IndicatorDecal = NewObject<UDecalComponent>(this, NAME_None, RF_Transient);
		IndicatorDecal->SetupAttachment(RootComponent);
		IndicatorDecal->RegisterComponent();
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Base, this);
		Mid->SetTextureParameterValue(TEXT("Paint"), IndicatorFilm);
		Mid->SetScalarParameterValue(TEXT("Glow"), 0.35f);
		PaintCliffMask(Mid);
		IndicatorDecal->SetDecalMaterial(Mid);
		// Over the fog of war (TMBattleDirectorFog.cpp), so aims read in the dark.
		IndicatorDecal->SortOrder = 1;
	}
	// Straight down over the whole board, deep enough for its highest ground.
	// A decal projects along its own X; turned to point down, its Y spans the
	// board's X and its Z the board's Y.
	const float Tall = 12.0f * LevelCm();
	IndicatorDecal->SetRelativeLocation(FVector(Size.X * 0.5f * TileSize, Size.Y * 0.5f * TileSize, Tall * 0.5f));
	IndicatorDecal->SetRelativeRotation(FRotator(-90.0f, 0.0f, IndicatorRoll));
	IndicatorDecal->DecalSize = FVector(Tall * 0.5f + 50.0f, Size.Y * 0.5f * TileSize, Size.X * 0.5f * TileSize);
	IndicatorDecal->MarkRenderStateDirty();
	IndicatorSignature.Reset();
	MoveAreaKey.Reset();
	PlateKey.Reset();
	bIndicatorDecal = true;
	for (TObjectPtr<USkeletalMeshComponent>& Body : UnitVisuals)
	{
		if (Body)
		{
			Body->SetReceivesDecals(false);
		}
	}
}

void ATMBattleDirector::PaintCliffMask(UMaterialInstanceDynamic* Mid)
{
	// Eight pixels to a metre is plenty: it only says "near a cliff", and the
	// decal's sampling softens its edge over a few centimetres.
	constexpr float CliffMaskPpm = 8.0f;
	/** Metres either side of a cliff's edge line where steep faces fade: the rock stands
	 *  0.28 m out (CliffDepth) and the bank under it reaches a little further. */
	constexpr float MaskReach = 0.9f;
	/** As TMBattleDirectorCliffs.cpp: a step this tall or taller is faced with rock. */
	constexpr int32 MaskCliffSteps = 2;
	if (!Mid)
	{
		return;
	}
	const TMSim::FMap& Map = Battle.Map;
	const TMSim::FVec2 Size = Map.SizeMeters();
	CliffMaskFilm = FilmOfSize(CliffMaskFilm, FMath::CeilToInt(Size.X * CliffMaskPpm), FMath::CeilToInt(Size.Y * CliffMaskPpm), true);
	UKismetRenderingLibrary::ClearRenderTarget2D(this, CliffMaskFilm, FLinearColor::Black);
	Mid->SetTextureParameterValue(TEXT("Cliffs"), CliffMaskFilm);
	if (Map.TilesX <= 0 || Map.TilesY <= 0 || Map.Covers.size() < static_cast<size_t>(Map.TilesX * Map.TilesY))
	{
		return;
	}
	UCanvas* Canvas = nullptr;
	FVector2D CanvasSize;
	FDrawToRenderTargetContext Context;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, CliffMaskFilm, Canvas, CanvasSize, Context);
	if (!Canvas)
	{
		return;
	}
	// A tile's height as the ground draws it (as TMBattleDirectorCliffs.cpp): its steps; rock a step; water none.
	auto Steps = [&Map](int32 X, int32 Y)
	{
		const int32 S = Map.TileLevel(X, Y);
		return S > 0 ? S : (Map.Covers[Y * Map.TilesX + X] != 0 ? 1 : 0);
	};
	const float Tile = TMSim::Ground::TileSize;
	auto Box = [&](float X0, float Y0, float X1, float Y1)
	{
		FCanvasTileItem Item(FVector2D(X0 * CliffMaskPpm, Y0 * CliffMaskPpm), FVector2D((X1 - X0) * CliffMaskPpm, (Y1 - Y0) * CliffMaskPpm), FLinearColor::White);
		Item.BlendMode = SE_BLEND_Opaque;
		Canvas->DrawItem(Item);
	};
	int32 Edges = 0;
	for (int32 Y = 0; Y < Map.TilesY; ++Y)
	{
		for (int32 X = 0; X < Map.TilesX; ++X)
		{
			const int32 Here = Steps(X, Y);
			if (X + 1 < Map.TilesX && FMath::Abs(Here - Steps(X + 1, Y)) >= MaskCliffSteps)
			{
				const float EdgeX = (X + 1) * Tile;
				Box(EdgeX - MaskReach, Y * Tile - MaskReach, EdgeX + MaskReach, (Y + 1) * Tile + MaskReach);
				++Edges;
			}
			if (Y + 1 < Map.TilesY && FMath::Abs(Here - Steps(X, Y + 1)) >= MaskCliffSteps)
			{
				const float EdgeY = (Y + 1) * Tile;
				Box(X * Tile - MaskReach, EdgeY - MaskReach, (X + 1) * Tile + MaskReach, EdgeY + MaskReach);
				++Edges;
			}
		}
	}
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Context);
	UE_LOG(LogTemp, Log, TEXT("indicators: cliff mask, %d tall edges"), Edges);
}

void ATMBattleDirector::AdvanceIndicators()
{
	using namespace TMIndicatorPaint;
	if (!bIndicatorDecal || !IndicatorFilm)
	{
		return;
	}
	// What would be shown, in a line: painted again only when it changes.
	const TMSim::FUnit* Unit = SelectedUnit();
	const bool bShow = Screen == EScreen::Battle && PlayerCanCommand(Unit) && !Battle.IsPlanning();
	FAim Where;
	if (bShow && AimMode == EAimMode::Ability)
	{
		Where = Aim();
	}
	// The rings that are part of the ground -- the middle's while holding it can
	// win, in the colour of whoever holds it (0 grey, 1 blue, 2 red) -- painted
	// with the rest so they lie over the land too. A watchtower has no ring of its
	// own any more; the one under the pointer shows its reach as thin white lines
	// (2026-10-01).
	const int32 TowerHovered = HoveredTower();
	FString Rings = TowerHovered >= 0 ? FString::Printf(TEXT("w%d"), TowerHovered) : FString();
	// Monsters' wind-ups, as they fill (2026-10-02).
	Rings += CastSignature();
	// Zones of control (2026-10-02).
	Rings += ZoneSignature();
	// Auras and buffs (v20 play test).
	Rings += AuraSignature();
	// Ground zones (2026-10-04).
	Rings += GroundZoneSignature();
	// And a ring at the edge of each hazard tile, so it reads as "this tile does
	// something" before its look is made out: orange burns, cyan mends.
	for (const int Each : Battle.Map.Hazards)
	{
		if (Each != 0)
		{
			Rings += TEXT("h");
			break;
		}
	}
	int32 MiddleHolder = -1;
	if (Battle.Tuning.CaptureSeconds > 0.0)
	{
		int32 Standing[2] = { 0, 0 };
		const TMSim::FVec2 Middle = Battle.CapturePoint();
		for (const TMSim::FUnit& Each : Battle.Units)
		{
			if (Each.IsAlive() && (Each.Team == 0 || Each.Team == 1) && static_cast<double>(Each.Pos.DistanceTo(Middle)) <= TMSim::FBattle::CaptureRadius)
			{
				++Standing[Each.Team];
			}
		}
		MiddleHolder = (Standing[0] > 0) == (Standing[1] > 0) ? 0 : (Standing[0] > 0 ? 1 : 2);
		Rings += FString::Printf(TEXT("m%d"), MiddleHolder);
	}
	const bool bBattle = Screen == EScreen::Battle;
	// Queued orders (2026-10-01): every plan of this machine's, and the waypoints being set.
	const FString Planned = bBattle ? PlanSignature() : FString();
	const FString Signature = Rings + Planned + (!bShow ? FString(TEXT("-"))
		: FString::Printf(TEXT("%d/%d/%d/%d/%d/%d/%.2f,%.2f/%d/%d/%d"), Unit->Id, Unit->Serial, static_cast<int32>(AimMode), AimSlot,
			bSprinting ? 1 : 0, static_cast<int32>(Reachable.size()), Where.Point.X, Where.Point.Y, Where.bHave ? 1 : 0, Where.bOk ? 1 : 0,
			static_cast<int32>(PathShown.size()) * 1000 + (PathShown.empty() ? 0 : TMSim::FMap::NodeOf(PathShown.back()).X * 50 + TMSim::FMap::NodeOf(PathShown.back()).Y)));
	if (Signature == IndicatorSignature)
	{
		return;
	}
	IndicatorSignature = Signature;
	MoveEdgeMetres.Reset();
	// Tile movement's plates come back with the walk area, if it is painted (PaintMoveArea).
	HideMovePlates();

	UKismetRenderingLibrary::ClearRenderTarget2D(this, IndicatorFilm, FLinearColor::Transparent);
	if (!bShow && (!bBattle || (Rings.IsEmpty() && Planned.IsEmpty())))
	{
		return;
	}
	UCanvas* Canvas = nullptr;
	FVector2D Size;
	FDrawToRenderTargetContext Context;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, IndicatorFilm, Canvas, Size, Context);
	if (!Canvas)
	{
		return;
	}
	FPainter Paint;
	Paint.Canvas = Canvas;
	if (bBattle)
	{
		auto Holder = [](int32 Who)
		{
			return Who == 1 ? FLinearColor(0.35f, 0.62f, 1.0f) : Who == 2 ? FLinearColor(1.0f, 0.38f, 0.32f) : FLinearColor(0.85f, 0.87f, 0.92f);
		};
		auto GroundRing = [&](const TMSim::FVec2& Centre, float Radius, const FLinearColor& Colour)
		{
			// A faint fill and a sharp rim, like the range of an ability (2026-10-06: the soft band inside it gone).
			Paint.Disc(Paint.P(Centre), Radius * Ppm, Alpha(Colour, 0.08f));
			Paint.Band(Paint.P(Centre), Radius * Ppm - 2.0f, Radius * Ppm, Alpha(Colour, 0.9f));
		};
		if (TowerHovered >= 0 && TowerHovered < static_cast<int32>(Battle.Watchtowers.size()))
		{
			// Just the lines, thin and white, with nothing lit or filled inside:
			// how far the tower sees for whoever holds it, and, fainter, how near
			// a unit must stand to take it.
			const TMSim::FWatchtower& Tower = Battle.Watchtowers[static_cast<size_t>(TowerHovered)];
			auto Outline = [&](float Radius, float Opacity)
			{
				const int32 Steps = FMath::Clamp(FMath::CeilToInt(Radius * 12.0f), 48, 360);
				const FVector2D Middle = Paint.P(Tower.Pos);
				for (int32 i = 0; i < Steps; ++i)
				{
					const float A0 = UE_TWO_PI * i / Steps;
					const float A1 = UE_TWO_PI * (i + 1) / Steps;
					Paint.Thin(Middle + FVector2D(FMath::Cos(A0), FMath::Sin(A0)) * Radius * Ppm,
						Middle + FVector2D(FMath::Cos(A1), FMath::Sin(A1)) * Radius * Ppm, MoveEdgeWidth, FLinearColor(1.0f, 1.0f, 1.0f, Opacity));
				}
			};
			Outline(static_cast<float>(Battle.Tuning.WatchtowerSight), 0.9f);
			Outline(static_cast<float>(TMSim::Watchtower::Reach), 0.55f);
		}
		if (MiddleHolder >= 0)
		{
			GroundRing(Battle.CapturePoint(), static_cast<float>(TMSim::FBattle::CaptureRadius), Holder(MiddleHolder));
		}
		const TMSim::FMap& Land = Battle.Map;
		const float Step = TMSim::Ground::TileSize;
		for (int32 TY = 0; TY < Land.TilesY; ++TY)
		{
			for (int32 TX = 0; TX < Land.TilesX; ++TX)
			{
				const int32 Hazard = Land.Hazards[TY * Land.TilesX + TX];
				if (Hazard == 0 || Land.TileLevel(TX, TY) <= 0)
				{
					continue;
				}
				const FLinearColor Cue = Hazard < 0 ? FLinearColor(1.0f, 0.5f, 0.15f) : FLinearColor(0.35f, 0.95f, 1.0f);
				const FVector2D Middle = Paint.P((TX + 0.5) * Step, (TY + 0.5) * Step);
				const float Edge = Step * 0.45f * Ppm;
				Paint.Band(Middle, Edge - 1.75f, Edge, Alpha(Cue, 0.85f));
			}
		}
		PaintGroundZones(&Paint);
		PaintAuras(&Paint);
		PaintCasts(&Paint);
		PaintZones(&Paint, bShow && AimMode == EAimMode::Move);
	}
	if (bBattle && !Planned.IsEmpty())
	{
		PaintPlans(&Paint);
	}
	if (!bShow)
	{
		Paint.Flush();
		UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Context);
		return;
	}
	if (AimMode == EAimMode::Move)
	{
		PaintMoveArea(&Paint, *Unit);
		PaintZoneShadow(&Paint);
	}
	else if (AimMode == EAimMode::Ability)
	{
		// A unit being planned aims from where its planned walk ends (FPlanStandIn).
		const FPlanStandIn Stand(*this);
		PaintAbility(&Paint, *Unit, Where);
	}
	// The way there, whether a walk or a walk into range: a thin line, and a
	// small dot where it ends. Coloured by what it meets (2026-10-03): gold;
	// orange where it sets off from inside an enemy's reach (breaking away costs
	// extra); red where it ends in a tank's zone (the zone stops it there). Its
	// corners are cut, as the walk itself cuts them (AdvanceMotion).
	if (!PathShown.empty())
	{
		const int32 Mine = FriendTeam();
		const double Reach = Battle.Tuning.EngageRadius;
		const bool bCosts = Reach > 0.0 && Battle.Tuning.EngageCost > 0.0;
		const bool bStops = Reach > 0.0 && Battle.Tuning.ZoneOfControl >= 1.0;
		std::vector<const TMSim::FUnit*> Foes;
		for (const TMSim::FUnit& Each : Battle.Units)
		{
			if (Each.IsAlive() && Each.Team != Mine && IsSeen(Each))
			{
				Foes.push_back(&Each);
			}
		}
		auto NearFoe = [&Foes, Reach](const TMSim::FVec2& Point, bool bTanksOnly)
		{
			for (const TMSim::FUnit* Foe : Foes)
			{
				if ((!bTanksOnly || TMSim::FBattle::HoldsTheLine(*Foe)) && static_cast<double>(Point.DistanceTo(Foe->Pos)) <= Reach + 0.01)
				{
					return true;
				}
			}
			return false;
		};
		const FLinearColor StopRed(1.0f, 0.32f, 0.26f);
		const int32 Legs = static_cast<int32>(PathShown.size()) - 1;
		FLinearColor Last = PathGold;
		for (int32 k = 0; k < Legs; ++k)
		{
			const TMSim::FVec2& A = PathShown[static_cast<size_t>(k)];
			const TMSim::FVec2& B = PathShown[static_cast<size_t>(k + 1)];
			const FLinearColor Colour = bStops && k + 1 == Legs && NearFoe(B, true) ? StopRed
				: (bCosts && NearFoe(A, false) ? ZoneOrange : PathGold);
			// This leg from a quarter along to three quarters along (its ends where
			// the path does), then the cut corner into the next.
			const FVector2D From = Paint.P(k == 0 ? A : A + (B - A) * 0.25f);
			const FVector2D To = Paint.P(k + 1 == Legs ? B : A + (B - A) * 0.75f);
			Paint.Thin(From, To, PathWidth, Alpha(Colour, 0.95f));
			if (k + 1 < Legs)
			{
				const TMSim::FVec2& C = PathShown[static_cast<size_t>(k + 2)];
				Paint.Thin(To, Paint.P(B + (C - B) * 0.25f), PathWidth, Alpha(Colour, 0.95f));
			}
			Last = Colour;
		}
		Paint.Disc(Paint.P(PathShown.back()), 0.12f * Ppm, Alpha(Last, 0.9f));
	}
	Paint.Flush();
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Context);
}

FString ATMBattleDirector::PlanSignature() const
{
	FString Out;
	for (const TPair<int32, FTMPlan>& Pair : Plans)
	{
		const FTMPlan& Plan = Pair.Value;
		Out += FString::Printf(TEXT("p%d:%d:%.2f,%.2f:%d:%d:%.2f,%.2f:%d;"), Pair.Key, Plan.bWalk ? 1 : 0, Plan.To.X, Plan.To.Y,
			static_cast<int32>(Plan.Via.size()), Plan.Slot, Plan.Target.X, Plan.Target.Y, Plan.Follow);
	}
	for (const TMSim::FVec2& Point : WayPoints)
	{
		Out += FString::Printf(TEXT("w%.2f,%.2f;"), Point.X, Point.Y);
	}
	// Go Tos: where each is going, how far it has to go, and which is lit.
	for (const TPair<int32, FTMGoTo>& Pair : GoTos)
	{
		const FTMGoTo& Order = Pair.Value;
		Out += FString::Printf(TEXT("g%d:%.2f,%.2f:%d:%d:%d:%.2f,%.2f;"), Pair.Key, Order.Dest.X, Order.Dest.Y, static_cast<int32>(Order.Route.size()),
			static_cast<int32>(Order.Stops.size()), GoToShown(Pair.Key) ? 1 : 0,
			Order.Stops.empty() ? 0.0 : Order.Stops.front().X, Order.Stops.empty() ? 0.0 : Order.Stops.front().Y);
	}
	if (!GoToHoverStops.empty())
	{
		Out += FString::Printf(TEXT("h%d:%.2f,%.2f;"), static_cast<int32>(GoToHoverStops.size()), GoToHoverStops.back().X, GoToHoverStops.back().Y);
	}
	return Out;
}

void ATMBattleDirector::PaintPlans(void* Painter)
{
	using namespace TMIndicatorPaint;
	FPainter& Paint = *static_cast<FPainter*>(Painter);
	// Planned: dashed, so it reads as "to come", in the walk's gold; where the
	// unit will stand, a ring; what it will aim at, a violet line from there.
	const FLinearColor Aimed(0.82f, 0.65f, 1.0f);
	auto Dashed = [&Paint](const FVector2D& A, const FVector2D& B, const FLinearColor& Colour)
	{
		const FVector2D Along = B - A;
		const double Length = Along.Size();
		if (Length < 0.01)
		{
			return;
		}
		const double Dash = 0.4 * Ppm;
		const double Gap = 0.25 * Ppm;
		for (double At = 0.0; At < Length; At += Dash + Gap)
		{
			const double End = FMath::Min(Length, At + Dash);
			Paint.Thin(A + Along * (At / Length), A + Along * (End / Length), PathWidth, Colour);
		}
	};
	for (const TPair<int32, FTMPlan>& Pair : Plans)
	{
		const FTMPlan& Plan = Pair.Value;
		const TMSim::FUnit* Unit = const_cast<TMSim::FBattle&>(Battle).FindUnit(Pair.Key);
		if (!Unit || !Unit->IsAlive())
		{
			continue;
		}
		TMSim::FVec2 Stands = Unit->Pos;
		if (Plan.bWalk)
		{
			for (size_t i = 1; i < Plan.Path.size(); ++i)
			{
				Dashed(Paint.P(Plan.Path[i - 1]), Paint.P(Plan.Path[i]), Alpha(PathGold, 0.8f));
			}
			for (const TMSim::FVec2& Point : Plan.Via)
			{
				Paint.Disc(Paint.P(Point), 0.1f * Ppm, Alpha(PathGold, 0.85f));
			}
			Stands = Plan.To;
			Paint.Disc(Paint.P(Stands), 0.45f * Ppm, Alpha(PathGold, 0.08f));
			Paint.Band(Paint.P(Stands), 0.45f * Ppm - AimWidth, 0.45f * Ppm, Alpha(PathGold, 0.85f));
		}
		if (Plan.HasAbility())
		{
			TMSim::FVec2 Target = Plan.Target;
			if (const TMSim::FUnit* Followed = Plan.Follow >= 0 ? const_cast<TMSim::FBattle&>(Battle).FindUnit(Plan.Follow) : nullptr)
			{
				Target = Followed->Pos;
			}
			Dashed(Paint.P(Stands), Paint.P(Target), Alpha(Aimed, 0.85f));
			const TMSim::FAbility* Ability = Unit->Ability(Plan.Slot);
			const float Reach = Ability && Ability->Aoe > 0.0f ? Ability->Aoe : 0.35f;
			Paint.Disc(Paint.P(Target), Reach * Ppm, Alpha(Aimed, 0.08f));
			Paint.Band(Paint.P(Target), Reach * Ppm - AimWidth, Reach * Ppm, Alpha(Aimed, 0.85f));
		}
	}
	// Each Go To's road (2026-10-01): dashed blue, a ring where each turn's walk
	// ends and a gold one at the end. The numbers are the HUD's
	// (ATMBattleHud::DrawGoToMarks). Only while its unit is pointed at
	// (2026-10-03: "hidden unless the unit is hovered over").
	const FLinearColor GoBlue(0.44f, 0.66f, 1.0f);
	for (const TPair<int32, FTMGoTo>& Pair : GoTos)
	{
		const FTMGoTo& Order = Pair.Value;
		const TMSim::FUnit* Unit = const_cast<TMSim::FBattle&>(Battle).FindUnit(Pair.Key);
		if (!Unit || !Unit->IsAlive() || !GoToShown(Pair.Key))
		{
			continue;
		}
		const float Strength = 0.85f;
		TMSim::FVec2 Last = Unit->Pos;
		for (const TMSim::FVec2& Point : Order.Route)
		{
			Dashed(Paint.P(Last), Paint.P(Point), Alpha(GoBlue, Strength));
			Last = Point;
		}
		for (size_t i = 0; i < Order.Stops.size(); ++i)
		{
			const bool bEnd = i + 1 == Order.Stops.size();
			const float Ring = (bEnd ? 0.45f : 0.3f) * Ppm;
			Paint.Disc(Paint.P(Order.Stops[i]), Ring, Alpha(bEnd ? PathGold : GoBlue, 0.08f));
			Paint.Band(Paint.P(Order.Stops[i]), Ring - AimWidth, Ring, Alpha(bEnd ? PathGold : GoBlue, Strength));
		}
	}
	// A Go To under the pointer: its turn ends, before the click.
	for (size_t i = 0; i < GoToHoverStops.size(); ++i)
	{
		const bool bEnd = i + 1 == GoToHoverStops.size();
		const float Ring = (bEnd ? 0.45f : 0.3f) * Ppm;
		Paint.Band(Paint.P(GoToHoverStops[i]), Ring - AimWidth, Ring, Alpha(bEnd ? PathGold : GoBlue, 0.8f));
	}

	// The waypoints of the walk being aimed: a dot and a ring each.
	for (const TMSim::FVec2& Point : WayPoints)
	{
		Paint.Disc(Paint.P(Point), 0.12f * Ppm, Alpha(PathGold, 0.95f));
		Paint.Band(Paint.P(Point), 0.28f * Ppm - AimWidth, 0.28f * Ppm, Alpha(PathGold, 0.9f));
	}
}

void ATMBattleDirector::PaintMoveArea(void* Painter, const TMSim::FUnit& Unit)
{
	using namespace TMIndicatorPaint;
	// The ground it can reach, filled and edged, with the edge cut smooth
	// across each half-metre square (marching squares on the node centres), so
	// it reads as an area rather than a staircase.
	FPainter& Paint = *static_cast<FPainter*>(Painter);
	const TMSim::FMap& Map = Battle.Map;
	const FLinearColor Colour = bSprinting ? Sprint : Teal;
	// Whatever was painted before (the rings) goes down first, so only the area is kept.
	Paint.Flush();
	// The area only changes with the unit, its sprint and where everyone stands;
	// the path over it changes as the pointer moves. So the area is worked out
	// once and kept, and each repaint for a new path only puts it down again.
	FString Key = FString::Printf(TEXT("%d/%d/%d/%d"), Unit.Id, Unit.Serial, bSprinting ? 1 : 0, static_cast<int32>(Reachable.size()));
	for (const TMSim::FUnit& Each : Battle.Units)
	{
		Key += FString::Printf(TEXT("/%.1f,%.1f"), Each.Pos.X, Each.Pos.Y);
	}
	// On from the last waypoint, with what is left (2026-10-01).
	for (const TMSim::FVec2& Point : WayPoints)
	{
		Key += FString::Printf(TEXT("|%.2f,%.2f"), Point.X, Point.Y);
	}
	if (Key == MoveAreaKey)
	{
		Paint.Tris.Append(MoveAreaTris);
		Paint.RimTris.Append(MoveAreaRims);
		Paint.Flush();
		MoveEdgeMetres = MoveAreaEdges;
		if (Battle.TilesOn())
		{
			ShowMovePlates(Unit, Key);
		}
		return;
	}
	MoveAreaKey.Reset();
	TArray<uint8> Inside;
	Inside.SetNumZeroed(Map.NavX * Map.NavY);
	int32 MinX = Map.NavX, MinY = Map.NavY, MaxX = -1, MaxY = -1;
	for (const std::pair<TMSim::FNode, double>& Entry : Reachable)
	{
		Inside[Map.NodeIndex(Entry.first)] = 1;
		MinX = FMath::Min(MinX, Entry.first.X);
		MinY = FMath::Min(MinY, Entry.first.Y);
		MaxX = FMath::Max(MaxX, Entry.first.X);
		MaxY = FMath::Max(MaxY, Entry.first.Y);
	}
	if (MaxX < 0)
	{
		MoveAreaTris.Reset();
		MoveAreaRims.Reset();
		MoveAreaEdges.Reset();
		MoveAreaKey = Key;
		return;
	}
	// Tile movement (v20 play test, "Tile-based movement" A and B): each tile it
	// can walk to, as a square with a thin edge, Tactics style. Since 2026-10-06 the
	// squares are plates on each tile's own top (ShowMovePlates), not painted here:
	// painted from above they bent and hung down cliffs on raised ground. Their
	// edges are still worked out, for MoveEdgeMetres.
	if (Battle.TilesOn())
	{
		const float Tile = TMSim::Ground::TileSize;
		const float Inset = 0.1f;
		TArray<TPair<FVector2D, FVector2D>> Squares;
		for (const std::pair<TMSim::FNode, double>& Entry : Reachable)
		{
			if (!(TMSim::FBattle::TileNode(TMSim::FMap::NodePos(Entry.first)) == Entry.first))
			{
				continue;  // where it stands, off its tile's spot
			}
			const float X0 = (Entry.first.X / TMSim::Ground::NodesPerTile) * Tile + Inset;
			const float Y0 = (Entry.first.Y / TMSim::Ground::NodesPerTile) * Tile + Inset;
			const float X1 = X0 + Tile - 2.0f * Inset;
			const float Y1 = Y0 + Tile - 2.0f * Inset;
			const FVector2D A = Paint.P(X0, Y0), B = Paint.P(X1, Y0), C = Paint.P(X1, Y1), D = Paint.P(X0, Y1);
			for (const TPair<FVector2D, FVector2D>& Side : { TPair<FVector2D, FVector2D>(A, B), TPair<FVector2D, FVector2D>(B, C),
				TPair<FVector2D, FVector2D>(C, D), TPair<FVector2D, FVector2D>(D, A) })
			{
				Squares.Add(Side);
			}
		}
		MoveAreaTris = Paint.Tris;
		MoveAreaRims = Paint.RimTris;
		MoveAreaKey = Key;
		Paint.Flush();
		ShowMovePlates(Unit, Key);
		for (const TPair<FVector2D, FVector2D>& Piece : Squares)
		{
			MoveEdgeMetres.Add({ Piece.Key / Ppm, Piece.Value / Ppm });
		}
		MoveAreaEdges = MoveEdgeMetres;
		return;
	}
	// A spot another unit stands on is still inside the area around it, so a
	// unit standing within reach is not ringed off.
	for (const TMSim::FUnit& Other : Battle.Units)
	{
		if (&Other == &Unit || !Other.IsAlive())
		{
			continue;
		}
		const TMSim::FNode At = TMSim::FMap::NodeOf(Other.Pos);
		for (int32 DY = -2; DY <= 2; ++DY)
		{
			for (int32 DX = -2; DX <= 2; ++DX)
			{
				const TMSim::FNode N{ At.X + DX, At.Y + DY };
				if (N.X <= MinX || N.Y <= MinY || N.X >= MaxX || N.Y >= MaxY || Inside[Map.NodeIndex(N)])
				{
					continue;
				}
				if (Other.Pos.DistanceTo(TMSim::FMap::NodePos(N)) >= TMSim::Ground::UnitSpacing)
				{
					continue;
				}
				int32 Around = 0;
				const TMSim::FNode Near[4] = { { N.X + 1, N.Y }, { N.X - 1, N.Y }, { N.X, N.Y + 1 }, { N.X, N.Y - 1 } };
				for (const TMSim::FNode& M : Near)
				{
					Around += Inside[Map.NodeIndex(M)] ? 1 : 0;
				}
				if (Around >= 2)
				{
					Inside[Map.NodeIndex(N)] = 2;
				}
			}
		}
	}
	auto In = [&](int32 X, int32 Y) { return X >= 0 && Y >= 0 && X < Map.NavX && Y < Map.NavY && Inside[Y * Map.NavX + X] != 0; };
	auto At = [&](float X, float Y) { return Paint.P(X * 0.5 + 0.25, Y * 0.5 + 0.25); };
	// Faint, so the models standing inside it still read (2026-10-01: "reduce
	// brightness of movement radius inner circle").
	const FLinearColor Fill = Alpha(Colour, 0.035f);
	TArray<TPair<FVector2D, FVector2D>> Edges;
	for (int32 Y = MinY - 1; Y <= MaxY; ++Y)
	{
		for (int32 X = MinX - 1; X <= MaxX; ++X)
		{
			const bool A = In(X, Y), B = In(X + 1, Y), C = In(X + 1, Y + 1), D = In(X, Y + 1);
			const FVector2D PA = At(X, Y), PB = At(X + 1, Y), PC = At(X + 1, Y + 1), PD = At(X, Y + 1);
			const FVector2D T = At(X + 0.5f, Y), R = At(X + 1, Y + 0.5f), Bo = At(X + 0.5f, Y + 1), L = At(X, Y + 0.5f);
			switch ((A ? 8 : 0) | (B ? 4 : 0) | (C ? 2 : 0) | (D ? 1 : 0))
			{
			case 15: Paint.Poly({ PA, PB, PC, PD }, Fill); break;
			case 8: Paint.Poly({ PA, T, L }, Fill); Edges.Add({ T, L }); break;
			case 4: Paint.Poly({ PB, R, T }, Fill); Edges.Add({ T, R }); break;
			case 2: Paint.Poly({ PC, Bo, R }, Fill); Edges.Add({ R, Bo }); break;
			case 1: Paint.Poly({ PD, L, Bo }, Fill); Edges.Add({ L, Bo }); break;
			case 12: Paint.Poly({ PA, PB, R, L }, Fill); Edges.Add({ L, R }); break;
			case 6: Paint.Poly({ T, PB, PC, Bo }, Fill); Edges.Add({ T, Bo }); break;
			case 3: Paint.Poly({ L, R, PC, PD }, Fill); Edges.Add({ L, R }); break;
			case 9: Paint.Poly({ PA, T, Bo, PD }, Fill); Edges.Add({ T, Bo }); break;
			case 10: Paint.Poly({ PA, T, R, PC, Bo, L }, Fill); Edges.Add({ T, R }); Edges.Add({ Bo, L }); break;
			case 5: Paint.Poly({ T, PB, R, Bo, PD, L }, Fill); Edges.Add({ L, T }); Edges.Add({ R, Bo }); break;
			case 14: Paint.Poly({ PA, PB, PC, Bo, L }, Fill); Edges.Add({ L, Bo }); break;
			case 13: Paint.Poly({ PA, PB, R, Bo, PD }, Fill); Edges.Add({ R, Bo }); break;
			case 11: Paint.Poly({ PA, T, R, PC, PD }, Fill); Edges.Add({ T, R }); break;
			case 7: Paint.Poly({ T, PB, PC, PD, L }, Fill); Edges.Add({ T, L }); break;
			default: break;
			}
		}
	}
	// The edge as smooth curves, not a staircase of half-metre squares, with a
	// soft glow: wide and faint fading in to a thin bright line, as League of
	// Legends draws its range rings.
	// Relaxed over a few squares, then rounded: the staircase of half-metre
	// squares becomes a curve (TMLines.h).
	const TArray<TPair<FVector2D, FVector2D>> Curve = TMLines::SmoothEdges(Edges, 10, 2);
	// One thin line, no glow (2026-10-01), drawn with the fill in one batch.
	for (const TPair<FVector2D, FVector2D>& Piece : Curve)
	{
		Paint.Thin(Piece.Key, Piece.Value, MoveEdgeWidth, Alpha(Colour, 0.95f));
	}
	MoveAreaTris = Paint.Tris;
	MoveAreaRims = Paint.RimTris;
	MoveAreaKey = Key;
	Paint.Flush();
	for (const TPair<FVector2D, FVector2D>& Piece : Curve)
	{
		MoveEdgeMetres.Add({ Piece.Key / Ppm, Piece.Value / Ppm });
	}
	MoveAreaEdges = MoveEdgeMetres;
}

void ATMBattleDirector::PaintAbility(void* Painter, const TMSim::FUnit& Unit, const FAim& Where)
{
	using namespace TMIndicatorPaint;
	FPainter& Paint = *static_cast<FPainter*>(Painter);
	const TMSim::FAbility* Ability = Unit.Ability(AimSlot);
	if (!Ability)
	{
		return;
	}
	const std::string Shape = TMSim::ShapeOf(*Ability);
	const FVector2D Me = Paint.P(Unit.Pos);
	const FLinearColor Colour = !Where.bHave || Where.bOk || Where.Why == UTF8_TO_TCHAR(OutOfRange) ? Teal : Refused;

	// How far it reaches: a faint disc with a bright rim, and the ring it
	// cannot be used inside.
	if (Ability->MaxRange > 0.0f && Shape != "cone")
	{
		const float Reach = Ability->MaxRange * Ppm;
		Paint.Disc(Me, Reach, Alpha(Teal, 0.07f));
		Paint.Band(Me, Reach - AimWidth, Reach, Alpha(Teal, 0.85f));
		if (Ability->MinRange > 0.0f)
		{
			Paint.Band(Me, Ability->MinRange * Ppm - AimWidth, Ability->MinRange * Ppm, Alpha(Refused, 0.6f));
		}
	}
	if (!Where.bHave && Ability->MaxRange > 0.0f && Shape != "global")
	{
		return;
	}
	const FVector2D Aim = Paint.P(Where.Point);
	const FVector2D Toward = (Aim - Me).GetSafeNormal();
	const FVector2D Across(-Toward.Y, Toward.X);
	const float Deg = FMath::RadiansToDegrees(FMath::Atan2(Toward.Y, Toward.X));

	if (Shape == "cone")
	{
		// A wedge out to its reach, edged, with a double arc across its end.
		const float Reach = Ability->MaxRange * Ppm;
		const float Half = Ability->Angle * 0.5f;
		Paint.Disc(Me, Reach, Alpha(Colour, 0.1f), Deg - Half, Deg + Half);
		const FVector2D EdgeA = Me + FVector2D(FMath::Cos(FMath::DegreesToRadians(Deg - Half)), FMath::Sin(FMath::DegreesToRadians(Deg - Half))) * Reach;
		const FVector2D EdgeB = Me + FVector2D(FMath::Cos(FMath::DegreesToRadians(Deg + Half)), FMath::Sin(FMath::DegreesToRadians(Deg + Half))) * Reach;
		Paint.Glow(Me, EdgeA, Colour);
		Paint.Glow(Me, EdgeB, Colour);
		Paint.Arc(Me, Reach, Deg - Half, Deg + Half, Colour, 4.0f);
		Paint.Arc(Me, Reach * 0.9f, Deg - Half * 0.95f, Deg + Half * 0.95f, Colour, 2.0f);
	}
	else if (Shape == "line")
	{
		// A skillshot: a lane to where it is aimed, and a chevron at its end.
		const float Width = FMath::Max(Ability->Aoe, 0.6f) * Ppm;
		const FVector2D End = Aim;
		Paint.Poly({ Me + Across * Width, End + Across * Width, End - Across * Width, Me - Across * Width }, Alpha(Colour, 0.09f));
		Paint.Glow(Me + Across * Width, End + Across * Width, Colour, 2.5f);
		Paint.Glow(Me - Across * Width, End - Across * Width, Colour, 2.5f);
		Paint.Glow(End - Toward * Width * 1.2f + Across * Width, End, Colour, 3.5f);
		Paint.Glow(End - Toward * Width * 1.2f - Across * Width, End, Colour, 3.5f);
	}
	else if (Shape == "vector")
	{
		// A charge: a bar to where it lands, capped at both ends, a mark midway.
		const float Cap = FMath::Max(Ability->Aoe, 0.6f) * Ppm;
		Paint.Glow(Me, Aim, Colour, 3.5f);
		Paint.Glow(Aim + Across * Cap, Aim - Across * Cap, Colour, 4.0f);
		Paint.Glow(Me + Across * Cap * 0.6f, Me - Across * Cap * 0.6f, Colour, 3.0f);
		const FVector2D Mid = (Me + Aim) * 0.5f;
		Paint.Band(Mid, Cap * 0.5f - AimWidth, Cap * 0.5f, Alpha(Colour, 0.8f));
	}
	else if (Shape == "global")
	{
		// Everyone of that side: a mark under each.
		for (const TMSim::FUnit& Other : Battle.Units)
		{
			const bool bFits = Other.IsAlive() && (Other.Team != Unit.Team) == (Ability->Target == TMSim::ETargetSide::Enemy);
			if (bFits && IsSeen(Other))
			{
				Paint.Band(Paint.P(Other.Pos), 0.62f * Ppm - AimWidth, 0.62f * Ppm, Alpha(Colour, 0.85f));
			}
		}
	}
	else
	{
		// A missile for a single target: a line from the hand to it.
		if (Ability->Aoe <= 0.0f && Ability->MaxRange > 0.0f)
		{
			const FVector2D Tip = Aim - Toward * 0.45f * Ppm;
			Paint.Glow(Me + Toward * 0.4f * Ppm, Tip, Colour, 2.5f);
			Paint.Glow(Tip - Toward * 0.35f * Ppm + Across * 0.2f * Ppm, Tip, Colour, 3.0f);
			Paint.Glow(Tip - Toward * 0.35f * Ppm - Across * 0.2f * Ppm, Tip, Colour, 3.0f);
		}
		// Where it lands: a ring with an inner ring and three spokes.
		const float Radius = FMath::Max(Ability->Aoe, TMSim::Ground::HitRadius) * Ppm;
		Paint.Disc(Aim, Radius, Alpha(Colour, 0.09f));
		Paint.Band(Aim, Radius - AimWidth, Radius, Alpha(Colour, 0.95f));
		if (Ability->Aoe > 0.0f)
		{
			Paint.Band(Aim, Radius * 0.86f - AimWidth, Radius * 0.86f, Alpha(Colour, 0.5f));
			for (int32 k = 0; k < 3; ++k)
			{
				const float A = FMath::DegreesToRadians(90.0f + 120.0f * k);
				Paint.Glow(Aim + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Radius * 0.12f,
					Aim + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Radius * 0.84f, Colour, 1.5f);
			}
			Paint.Disc(Aim, 0.12f * Ppm, Alpha(Colour, 0.9f));
		}
	}
}

FString ATMBattleDirector::CastSignature() const
{
	// Painted again as each wind-up fills by a sixteenth, starts, ends or moves.
	FString Out;
	for (const TMSim::FUnit& Unit : Battle.Units)
	{
		if (Unit.IsAlive() && !Unit.bOffBoard && Unit.IsCasting() && IsSeen(Unit))
		{
			const int32 Total = FMath::Max(1, Unit.Casting.Total);
			Out += FString::Printf(TEXT("c%d/%d/%d/%.1f,%.1f/%.1f,%.1f"), Unit.Id, Unit.Casting.Slot,
				((Total - Unit.Casting.Ticks) * 16) / Total, Unit.Pos.X, Unit.Pos.Y, Unit.Casting.Target.X, Unit.Casting.Target.Y);
		}
	}
	return Out;
}

void ATMBattleDirector::PaintCasts(void* Painter)
{
	// "Camps and Bosses Mockups" B: a monster's wind-up drawn where it will land,
	// hatched faint and filling from its middle as the cast runs out, edged in
	// purple; round a boss, a gold arc behind it, where three hits break it.
	using namespace TMIndicatorPaint;
	FPainter& Paint = *static_cast<FPainter*>(Painter);
	const FLinearColor Wind(0.69f, 0.49f, 1.0f);
	const FLinearColor BackGold(0.94f, 0.81f, 0.45f);
	for (const TMSim::FUnit& Unit : Battle.Units)
	{
		// Every caster in sight since the v19 play test (Cast A), not only a monster.
		if (!Unit.IsAlive() || Unit.bOffBoard || !Unit.IsCasting() || !IsSeen(Unit))
		{
			continue;
		}
		const TMSim::FAbility* Ability = Unit.Ability(Unit.Casting.Slot);
		if (!Ability)
		{
			continue;
		}
		TMSim::FVec2 Aim = Unit.Casting.Target;
		if (Unit.Casting.FollowId >= 0)
		{
			if (const TMSim::FUnit* Followed = Battle.FindUnit(Unit.Casting.FollowId))
			{
				Aim = Followed->Pos;
			}
		}
		const float Done = FMath::Clamp(1.0f - static_cast<float>(Unit.Casting.Ticks) / FMath::Max(1, Unit.Casting.Total), 0.0f, 1.0f);
		const std::string Shape = TMSim::ShapeOf(*Ability);
		const FVector2D Me = Paint.P(Unit.Pos);
		const FVector2D At = Paint.P(Aim);
		const FVector2D Toward = (At - Me).GetSafeNormal();
		const FVector2D Across(-Toward.Y, Toward.X);
		const float Deg = FMath::RadiansToDegrees(FMath::Atan2(Toward.Y, Toward.X));
		if (Shape == "cone")
		{
			const float Reach = Ability->MaxRange * Ppm;
			const float Half = Ability->Angle * 0.5f;
			Paint.Disc(Me, Reach, Alpha(Wind, 0.12f), Deg - Half, Deg + Half);
			Paint.Disc(Me, Reach * Done, Alpha(Wind, 0.28f), Deg - Half, Deg + Half);
			const FVector2D EdgeA = Me + FVector2D(FMath::Cos(FMath::DegreesToRadians(Deg - Half)), FMath::Sin(FMath::DegreesToRadians(Deg - Half))) * Reach;
			const FVector2D EdgeB = Me + FVector2D(FMath::Cos(FMath::DegreesToRadians(Deg + Half)), FMath::Sin(FMath::DegreesToRadians(Deg + Half))) * Reach;
			Paint.Thin(Me, EdgeA, 3.0f, Alpha(Wind, 0.95f));
			Paint.Thin(Me, EdgeB, 3.0f, Alpha(Wind, 0.95f));
			Paint.Arc(Me, Reach, Deg - Half, Deg + Half, Wind, 3.0f);
		}
		else if (Shape == "line")
		{
			const float Width = FMath::Max(Ability->Aoe, 0.6f) * Ppm;
			Paint.Poly({ Me + Across * Width, At + Across * Width, At - Across * Width, Me - Across * Width }, Alpha(Wind, 0.12f));
			const FVector2D Reached = Me + (At - Me) * Done;
			Paint.Poly({ Me + Across * Width, Reached + Across * Width, Reached - Across * Width, Me - Across * Width }, Alpha(Wind, 0.28f));
			Paint.Thin(Me + Across * Width, At + Across * Width, 3.0f, Alpha(Wind, 0.95f));
			Paint.Thin(Me - Across * Width, At - Across * Width, 3.0f, Alpha(Wind, 0.95f));
		}
		else
		{
			// A circle: where it lands, or round the caster; one target gets a ring of its own size.
			const FVector2D Centre = Shape == "self" ? Me : At;
			const float Radius = FMath::Max(Ability->Aoe, TMSim::Ground::HitRadius) * Ppm;
			// Dashes from the caster to where it lands (Cast A), stopping at the rim.
			const float Gap = FVector2D::Distance(Me, Centre) - Radius;
			if (Shape != "self" && Gap > 8.0f)
			{
				const FVector2D Way = (Centre - Me).GetSafeNormal();
				for (float Along = 0.0f; Along < Gap; Along += 14.0f)
				{
					Paint.Thin(Me + Way * Along, Me + Way * FMath::Min(Along + 8.0f, Gap), 2.5f, Alpha(Wind, 0.85f));
				}
			}
			Paint.Disc(Centre, Radius, Alpha(Wind, 0.12f));
			Paint.Disc(Centre, Radius * Done, Alpha(Wind, 0.28f));
			Paint.Band(Centre, Radius - 3.0f, Radius, Alpha(Wind, 0.95f));
			// The time running out, round the rim, from the top.
			Paint.Band(Centre, Radius + 2.0f, Radius + 6.0f, Alpha(Wind, 0.25f));
			const int32 Steps = FMath::Max(1, FMath::CeilToInt(72.0f * Done));
			for (int32 i = 0; i < Steps; ++i)
			{
				const float A0 = -UE_HALF_PI + UE_TWO_PI * i / 72.0f;
				const float A1 = -UE_HALF_PI + UE_TWO_PI * FMath::Min(i + 1.0f, 72.0f * Done) / 72.0f;
				const FVector2D D0(FMath::Cos(A0), FMath::Sin(A0));
				const FVector2D D1(FMath::Cos(A1), FMath::Sin(A1));
				Paint.Tri(Centre + D0 * (Radius + 2.0f), Centre + D0 * (Radius + 6.0f), Centre + D1 * (Radius + 6.0f), Alpha(Wind, 0.95f));
				Paint.Tri(Centre + D0 * (Radius + 2.0f), Centre + D1 * (Radius + 6.0f), Centre + D1 * (Radius + 2.0f), Alpha(Wind, 0.95f));
			}
		}
		// A boss: the arc behind it to strike from (its back, as the rules judge a flank).
		const TMSim::FMonsterInfo* Info = Unit.MonsterInfo();
		if (Info && Info->Tier >= 3)
		{
			const float Back = FMath::RadiansToDegrees(FMath::Atan2(-Unit.Facing.Y, -Unit.Facing.X));
			const float Ring = 1.4f * Ppm;
			for (int32 k = 0; k < 3; ++k)
			{
				Paint.Arc(Me, Ring + k * 1.5f, Back - 55.0f, Back + 55.0f, BackGold, 3.0f);
			}
		}
	}
}

int32 ATMBattleDirector::HoveredTower() const
{
	// The pointer is on a tower if it is near the line from its foot to its fire
	// on screen (the tower is tall and thin, and nothing can be clicked on it),
	// or the ground under the pointer is right at its foot.
	if (Screen != EScreen::Battle || Battle.Watchtowers.empty())
	{
		return -1;
	}
	float MouseX = 0.0f;
	float MouseY = 0.0f;
	APlayerController* Player = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	const bool bMouse = Player && CursorPosition(MouseX, MouseY);
	const FVector2D Mouse(MouseX, MouseY);
	int32 Best = -1;
	double BestAway = 18.0;  // screen pixels
	for (int32 i = 0; i < static_cast<int32>(Battle.Watchtowers.size()); ++i)
	{
		if (bHaveHover && static_cast<double>(HoverPoint.DistanceTo(Battle.Watchtowers[static_cast<size_t>(i)].Pos)) < 1.2)
		{
			return i;
		}
		if (!bMouse || !Beacons.IsValidIndex(i) || !Beacons[i].Root)
		{
			continue;
		}
		const FVector Foot = Beacons[i].Root->GetComponentLocation();
		FVector2D A;
		FVector2D B;
		if (!Player->ProjectWorldLocationToScreen(Foot, A) || !Player->ProjectWorldLocationToScreen(Foot + FVector(0.0f, 0.0f, 760.0f), B))
		{
			continue;
		}
		const double Away = FMath::PointDistToSegment(FVector(Mouse, 0.0), FVector(A, 0.0), FVector(B, 0.0));
		if (Away < BestAway)
		{
			BestAway = Away;
			Best = i;
		}
	}
	return Best;
}

// ------------------------------------------------------- zones of control

FString ATMBattleDirector::ZoneSignature() const
{
	// What PaintZones draws, in a line: the seen tanks where they stand, and
	// while walking, every seen enemy and how much ground zones take away.
	if (Battle.Tuning.EngageRadius <= 0.0)
	{
		return FString();
	}
	const bool bMoving = AimMode == EAimMode::Move;
	FString Out = TEXT("z");
	for (const TMSim::FUnit& Each : Battle.Units)
	{
		if (Each.IsAlive() && IsSeen(Each)
			&& ((Battle.Tuning.ZoneOfControl >= 1.0 && TMSim::FBattle::HoldsTheLine(Each)) || (bMoving && Each.Team != FriendTeam())))
		{
			Out += FString::Printf(TEXT("%d:%.1f,%.1f;"), Each.Id, Each.Pos.X, Each.Pos.Y);
		}
	}
	if (bMoving)
	{
		Out += FString::Printf(TEXT("s%d"), static_cast<int32>(ZoneShadow.size()));
	}
	return Out;
}

int32 ATMBattleDirector::BoonsAndBanes(const TMSim::FUnit& Unit)
{
	int32 Out = 0;
	for (const TMSim::FBuff& Buff : Unit.Buffs)
	{
		if (Buff.Turns > 0 && Buff.Amount != 0)
		{
			Out |= Buff.Amount > 0 ? 1 : 2;
		}
	}
	for (const TMSim::FStatus& Status : Unit.Statuses)
	{
		if (const TMSim::FStatusDef* Def = TMSim::FindStatus(Status.Id))
		{
			// Hidden (Vanish) shows by the fading body, not a ring that gives it away.
			if (!Def->bHidden)
			{
				Out |= Def->bHarmful ? 2 : 1;
			}
		}
	}
	return Out;
}

FString ATMBattleDirector::AuraSignature() const
{
	FString Out;
	for (const TMSim::FUnit& Each : Battle.Units)
	{
		if (!Each.IsAlive() || Each.bOffBoard || !IsSeen(Each))
		{
			continue;
		}
		const int32 Marks = BoonsAndBanes(Each);
		if (Marks != 0)
		{
			Out += FString::Printf(TEXT("a%d:%d:%.1f,%.1f;"), Each.Id, Marks, Each.Pos.X, Each.Pos.Y);
		}
	}
	return Out;
}

void ATMBattleDirector::PaintAuras(void* Painter)
{
	using namespace TMIndicatorPaint;
	FPainter& Paint = *static_cast<FPainter*>(Painter);
	const FLinearColor Boon(0.62f, 1.0f, 0.45f);
	const FLinearColor Bane(0.78f, 0.45f, 1.0f);
	for (const TMSim::FUnit& Each : Battle.Units)
	{
		if (!Each.IsAlive() || Each.bOffBoard || !IsSeen(Each))
		{
			continue;
		}
		const FVector2D Middle = Paint.P(Each.Pos);
		// Auras are effects round their owners now (AdvanceAuraFx, v21 play test),
		// not their reach painted on the ground.
		// Its boons and banes: a thin ring at its feet, and four arrows on it.
		const int32 Marks = BoonsAndBanes(Each);
		for (int32 Kind = 0; Kind < 2; ++Kind)
		{
			if ((Marks & (1 << Kind)) == 0)
			{
				continue;
			}
			const bool bBane = Kind == 1;
			const FLinearColor Colour = bBane ? Bane : Boon;
			// A bane's ring just inside a boon's, so a unit with both shows both.
			const float Radius = (bBane ? 0.62f : 0.78f) * Ppm;
			Paint.Band(Middle, Radius - 1.5f, Radius, Alpha(Colour, 0.9f));
			for (int32 k = 0; k < 4; ++k)
			{
				const float Angle = UE_HALF_PI * k + UE_PI * 0.25f;
				const FVector2D Out(FMath::Cos(Angle), FMath::Sin(Angle));
				const FVector2D Side(-Out.Y, Out.X);
				// The arrow's point: outward for a boon, inward for a bane.
				const FVector2D Tip = Middle + Out * (bBane ? Radius - 5.0f : Radius + 5.0f);
				const FVector2D Back = Middle + Out * (bBane ? Radius + 1.0f : Radius - 1.0f);
				Paint.Thin(Back + Side * 4.0f, Tip, 1.5f, Alpha(Colour, 0.95f));
				Paint.Thin(Back - Side * 4.0f, Tip, 1.5f, Alpha(Colour, 0.95f));
			}
		}
	}
}

bool ATMBattleDirector::GroundZoneShown(const TMSim::FBattle::FZone& Zone) const
{
	const int32 Viewer = ViewerTeam();
	// A recall mark (2026-10-05) is its own side's business.
	const TMSim::FAbility* Laid = Battle.ZoneAbility(Zone);
	if (Laid && Laid->Special == "recall" && Viewer >= 0 && Zone.Team != Viewer)
	{
		return false;
	}
	return Battle.ZoneLive(Zone) && (Viewer < 0 || Zone.Team == Viewer || IsPointSeen(Zone.Target));
}

FLinearColor ATMBattleDirector::GroundZoneColour(const TMSim::FAbility& Ability, bool bIgnited)
{
	// A warned blow (2026-10-06): red-orange, the colour of something about to land.
	if (Ability.Special == "warned")
	{
		return FLinearColor(1.0f, 0.36f, 0.16f);
	}
	const std::string& Element = TMSim::ElementOf(Ability);
	if (bIgnited || Element == "fire")
	{
		return FLinearColor(1.0f, 0.5f, 0.18f);
	}
	if (Ability.bZoneHide)
	{
		return FLinearColor(0.66f, 0.69f, 0.76f);
	}
	// 2026-10-05: gates violet, a recall mark pale gold, ice that speeds its side cyan.
	if (Ability.bZonePortal)
	{
		return FLinearColor(0.72f, 0.5f, 1.0f);
	}
	if (Ability.Special == "recall")
	{
		return FLinearColor(1.0f, 0.88f, 0.6f);
	}
	if (Ability.Target == TMSim::ETargetSide::Ally)
	{
		return FLinearColor(0.55f, 0.95f, 1.0f);
	}
	if (Ability.ZoneSight > 0.0f)
	{
		return Ability.HasStatus() ? FLinearColor(1.0f, 0.97f, 0.78f) : FLinearColor(1.0f, 0.85f, 0.42f);
	}
	if (Element == "lightning")
	{
		return FLinearColor(0.62f, 0.74f, 1.0f);
	}
	if (Element == "ice")
	{
		return FLinearColor(0.8f, 0.96f, 1.0f);
	}
	if (Element == "water")
	{
		return FLinearColor(0.32f, 0.62f, 1.0f);
	}
	if (Ability.StatusId == "decay")
	{
		return FLinearColor(0.64f, 0.92f, 0.32f);
	}
	if (Ability.StatusId == "root")
	{
		return FLinearColor(0.56f, 0.82f, 0.34f);
	}
	if (Ability.StatusId == "silence")
	{
		return FLinearColor(0.78f, 0.56f, 1.0f);
	}
	if (Ability.StatusId == "oiled")
	{
		return FLinearColor(0.82f, 0.64f, 0.34f);
	}
	return FLinearColor(0.9f, 0.9f, 0.92f);
}

FString ATMBattleDirector::GroundZoneSignature() const
{
	FString Out;
	for (const TMSim::FBattle::FZone& Zone : Battle.Zones)
	{
		if (GroundZoneShown(Zone))
		{
			Out += FString::Printf(TEXT("g%d:%hs:%d:%d:%.1f,%.1f;"), Zone.Owner, Zone.AbilityId.c_str(), Zone.Turns, Zone.bIgnited ? 1 : 0,
				Zone.Target.X, Zone.Target.Y);
		}
	}
	return Out;
}

void ATMBattleDirector::PaintGroundZones(void* Painter)
{
	// The "area denial" mockups (2026-10-04): the ground itself, tinted, so it
	// reads as a place before it is read as a number.
	using namespace TMIndicatorPaint;
	FPainter& Paint = *static_cast<FPainter*>(Painter);
	for (const TMSim::FBattle::FZone& Zone : Battle.Zones)
	{
		const TMSim::FAbility* Ability = Battle.ZoneAbility(Zone);
		if (!Ability || !GroundZoneShown(Zone))
		{
			continue;
		}
		const FLinearColor Colour = GroundZoneColour(*Ability, Zone.bIgnited);
		const bool bTar = !Zone.bIgnited && Ability->bZoneFlammable;
		const bool bSees = Ability->ZoneSight > 0.0f;
		const std::string Shape = TMSim::ShapeOf(*Ability);
		// A pair of gates (2026-10-05): a mouth at each end, joined by a dotted thread.
		if (Ability->bZonePortal)
		{
			const FVector2D Near = Paint.P(Zone.From);
			const FVector2D Far = Paint.P(Zone.Target);
			for (const FVector2D& Mouth : { Near, Far })
			{
				Paint.Disc(Mouth, 1.0f * Ppm, Alpha(Colour, 0.45f));
				Paint.Band(Mouth, 1.0f * Ppm - 2.0f, 1.0f * Ppm, Alpha(Colour, 0.95f));
			}
			const FVector2D Along = Far - Near;
			const float Length = static_cast<float>(Along.Size());
			for (float Dot = 1.2f * Ppm; Dot < Length - 1.2f * Ppm; Dot += 0.5f * Ppm)
			{
				Paint.Disc(Near + Along / Length * Dot, 0.06f * Ppm, Alpha(Colour, 0.8f));
			}
			continue;
		}
		if (Shape == "line" || Shape == "vector")
		{
			const FVector2D From = Paint.P(Zone.From);
			const FVector2D To = Paint.P(Zone.Target);
			const FVector2D Toward = (To - From).GetSafeNormal();
			const FVector2D Across(-Toward.Y, Toward.X);
			const float Width = FMath::Max(Ability->Aoe, 0.6f) * Ppm;
			// Tar is dark on the ground, with a rim in its colour.
			const FLinearColor Fill = bTar ? FLinearColor(0.06f, 0.05f, 0.04f, 0.49f) : Alpha(Colour, 0.45f);
			Paint.Poly({ From + Across * Width, To + Across * Width, To - Across * Width, From - Across * Width }, Fill);
			Paint.Thin(From + Across * Width, To + Across * Width, 1.6f, Alpha(Colour, 0.9f));
			Paint.Thin(From - Across * Width, To - Across * Width, 1.6f, Alpha(Colour, 0.9f));
			// A warned blow (2026-10-06): hazard stripes across it, so it reads as
			// "about to land here" and not as ground that lasts.
			if (Ability->Special == "warned")
			{
				const float Length = static_cast<float>(FVector2D::Distance(From, To));
				for (float Along = 0.35f * Ppm; Along < Length; Along += 0.7f * Ppm)
				{
					const FVector2D A = From + Toward * Along + Across * Width;
					const FVector2D B = From + Toward * FMath::Min(Length, Along + 0.5f * Ppm) - Across * Width;
					Paint.Thin(A, B, 2.2f, Alpha(Colour, 0.8f));
				}
			}
			continue;
		}
		const float Radius = (bSees ? FMath::Max(Ability->ZoneSight, Ability->Aoe) : Ability->Aoe) * Ppm;
		const FVector2D Middle = Paint.P(Zone.Target);
		Paint.Disc(Middle, Radius, Alpha(Colour, bSees ? 0.25f : 0.45f));
		if (bSees)
		{
			// Sight: a dashed rim, as the eye's reach rather than a place to keep out of.
			const int32 Steps = FMath::Max(24, FMath::CeilToInt(UE_TWO_PI * Radius / 7.0f));
			for (int32 k = 0; k < Steps; k += 2)
			{
				const float A0 = UE_TWO_PI * k / Steps;
				const float A1 = UE_TWO_PI * (k + 1) / Steps;
				Paint.Thin(Middle + FVector2D(FMath::Cos(A0), FMath::Sin(A0)) * Radius,
					Middle + FVector2D(FMath::Cos(A1), FMath::Sin(A1)) * Radius, 1.8f, Alpha(Colour, 0.95f));
			}
		}
		else if (Ability->Special == "warned")
		{
			// A warned blow over a circle: its rim, and hazard stripes across it.
			Paint.Band(Middle, Radius - 2.4f, Radius, Alpha(Colour, 0.95f));
			for (float Offset = -Radius + 0.35f * Ppm; Offset < Radius; Offset += 0.7f * Ppm)
			{
				const float Half = FMath::Sqrt(FMath::Max(0.0f, Radius * Radius - Offset * Offset));
				Paint.Thin(Middle + FVector2D(Offset - Half * 0.5f, -Half), Middle + FVector2D(Offset + Half * 0.5f, Half), 2.2f, Alpha(Colour, 0.7f));
			}
		}
		else
		{
			Paint.Band(Middle, Radius - 1.8f, Radius, Alpha(Colour, 0.92f));
			// An inner ring for ground that does harm, so it reads as dangerous.
			if (Zone.bIgnited || Ability->ZonePercent > 0.0f)
			{
				Paint.Band(Middle, Radius * 0.7f - 1.2f, Radius * 0.7f, Alpha(Colour, 0.55f));
			}
		}
	}
}

void ATMBattleDirector::RefreshZoneShadow()
{
	ZoneShadow.clear();
	const TMSim::FUnit* Unit = SelectedUnit();
	if (Unit && AimMode == EAimMode::Move)
	{
		ZoneShadow = Battle.ZoneShadow(*Unit, WayPoints, bSprinting);
	}
}

void ATMBattleDirector::PaintZones(void* Painter, bool bMoving)
{
	// "Zone of Control Mockups" A (2026-10-02): every tank this side can see wears
	// its zone on the ground, bold and orange for the enemy's (a walk into it
	// ends there), quieter and blue for your own. A tank in the fog draws
	// nothing. While a walk is aimed, a thin ring round every enemy in sight is
	// its reach (breaking away from it costs extra), and a tank being walked
	// shows its zone where the walk ends (D).
	using namespace TMIndicatorPaint;
	FPainter& Paint = *static_cast<FPainter*>(Painter);
	const float Radius = static_cast<float>(Battle.Tuning.EngageRadius) * Ppm;
	if (Radius <= 0.0f)
	{
		return;
	}
	const bool bZones = Battle.Tuning.ZoneOfControl >= 1.0;
	const int32 Mine = FriendTeam();
	auto Dashed = [&Paint, Radius](const FVector2D& Middle, const FLinearColor& Colour, float Width, float Dash)
	{
		const int32 Steps = FMath::Max(16, FMath::CeilToInt(UE_TWO_PI * Radius / Dash));
		for (int32 i = 0; i < Steps; i += 2)
		{
			const float A0 = UE_TWO_PI * i / Steps;
			const float A1 = UE_TWO_PI * (i + 1) / Steps;
			Paint.Thin(Middle + FVector2D(FMath::Cos(A0), FMath::Sin(A0)) * Radius,
				Middle + FVector2D(FMath::Cos(A1), FMath::Sin(A1)) * Radius, Width, Colour);
		}
	};
	auto Solid = [&Paint, Radius](const FVector2D& Middle, const FLinearColor& Colour, float Width)
	{
		const int32 Steps = 48;
		for (int32 i = 0; i < Steps; ++i)
		{
			const float A0 = UE_TWO_PI * i / Steps;
			const float A1 = UE_TWO_PI * (i + 1) / Steps;
			Paint.Thin(Middle + FVector2D(FMath::Cos(A0), FMath::Sin(A0)) * Radius,
				Middle + FVector2D(FMath::Cos(A1), FMath::Sin(A1)) * Radius, Width, Colour);
		}
	};
	for (const TMSim::FUnit& Each : Battle.Units)
	{
		if (!Each.IsAlive() || !IsSeen(Each))
		{
			continue;
		}
		const FVector2D Middle = Paint.P(Each.Pos);
		const bool bEnemy = Each.Team != Mine;
		if (bZones && TMSim::FBattle::HoldsTheLine(Each))
		{
			// v19 play test: the zone reads by its shields now, circling its edge
			// (ATMBattleHud::DrawZoneShields), an enemy tank's while it is in sight
			// and one of yours under the pointer. On the ground only a faint edge.
			if (bEnemy)
			{
				Paint.Disc(Middle, Radius, Alpha(ZoneOrange, 0.05f));
				Dashed(Middle, Alpha(ZoneOrange, 0.5f), 1.5f, 6.0f);
			}
		}
		else if (bMoving && bEnemy && Battle.Tuning.EngageCost > 0.0)
		{
			Solid(Middle, FLinearColor(1.0f, 1.0f, 1.0f, 0.35f), 1.0f);
		}
	}
	// A tank's zone where its walk would end.
	const TMSim::FUnit* Unit = SelectedUnit();
	if (bMoving && bZones && Unit && TMSim::FBattle::HoldsTheLine(*Unit) && !PathShown.empty() && TankLanesFor == Unit->Id)
	{
		const FVector2D Middle = Paint.P(PathShown.back());
		Paint.Disc(Middle, Radius, Alpha(ZoneBlue, 0.12f));
		Dashed(Middle, Alpha(ZoneBlue, 0.95f), 2.5f, 9.0f);
	}
}

void ATMBattleDirector::PaintZoneShadow(void* Painter)
{
	// "Zone of Control Mockups" B: the ground the enemy's tanks take away from
	// this walk, hatched over the walk area so its notch has a reason.
	using namespace TMIndicatorPaint;
	FPainter& Paint = *static_cast<FPainter*>(Painter);
	const float Half = TMSim::Ground::NavStep * 0.5f * Ppm;
	for (const TMSim::FNode& Node : ZoneShadow)
	{
		const FVector2D Middle = Paint.P(TMSim::FMap::NodePos(Node));
		TArray<FVector2D> Square;
		Square.Add(Middle + FVector2D(-Half, -Half));
		Square.Add(Middle + FVector2D(Half, -Half));
		Square.Add(Middle + FVector2D(Half, Half));
		Square.Add(Middle + FVector2D(-Half, Half));
		Paint.Poly(Square, Alpha(ZoneOrange, 0.12f));
		Paint.Thin(Middle + FVector2D(-Half, Half), Middle + FVector2D(Half, -Half), 1.5f, Alpha(ZoneOrange, 0.55f));
	}
}
