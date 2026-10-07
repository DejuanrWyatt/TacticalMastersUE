// Tile movement's walk area as plates (2026-10-06, the human's pick of the "Walk
// Squares on Raised Ground" mockups, D, with small badges).
//
// The squares used to be painted on the ground decal: one picture laid over the
// board from straight above, so on raised ground a square bent with the smoothed
// slope and hung down cliff faces. Here each tile the unit can walk to is its own
// thin plate at that tile's own height (tilted with a ramp), lifted a little over
// a faint shadow and edged, so every square is the same size and shape wherever it
// stands. Tiles above or below the unit's own get a small "+1" / "-2" badge, drawn
// by the HUD (ATMBattleHud::DrawPlateSteps) from PlateSteps.
//
// 2026-10-06 (the human's pick D of "Sharper lines", for "some contrast with the
// ground"): each plate's edge is white, thin, with a thin dark line just outside it.
//
// Presentation only: nothing here touches the rules.

#include "TMBattleDirector.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace TMPlates
{
	/** Metres in from a tile's edge, so neighbouring plates read as separate. */
	constexpr float Inset = 0.16f;
	/** Centimetres a plate floats over its ground, and its shadow. */
	constexpr float Lift = 6.0f;
	constexpr float ShadowLift = 1.0f;
	/** Centimetres: an edge's width and thickness. */
	constexpr float EdgeWidth = 2.5f;
	constexpr float EdgeThick = 1.2f;
	/** Centimetres: the dark line outside the edge, and how far under the edge it sits. */
	constexpr float RimWidth = 2.0f;
	constexpr float RimDrop = 0.4f;
	/** The edge: white, a touch below full so it doesn't bloom into a blur. */
	const FLinearColor EdgeWhite(0.88f, 0.92f, 0.92f);
}

void ATMBattleDirector::HideMovePlates()
{
	for (UInstancedStaticMeshComponent* Each : { PlateFills.Get(), PlateEdges.Get(), PlateShadows.Get(), PlateRims.Get() })
	{
		if (Each)
		{
			Each->SetVisibility(false);
		}
	}
	PlateSteps.Reset();
	bPlatesShown = false;
}

void ATMBattleDirector::ShowMovePlates(const TMSim::FUnit& Unit, const FString& Key)
{
	using namespace TMPlates;
	if (!PlateFills)
	{
		UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		if (!Plane || !Cube)
		{
			return;
		}
		auto Make = [this](const TCHAR* Name, UStaticMesh* Mesh)
		{
			UInstancedStaticMeshComponent* Made = NewObject<UInstancedStaticMeshComponent>(this,
				MakeUniqueObjectName(this, UInstancedStaticMeshComponent::StaticClass(), Name), RF_Transient);
			Made->SetupAttachment(RootComponent);
			Made->SetMobility(EComponentMobility::Movable);
			Made->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Made->SetCastShadow(false);
			Made->SetReceivesDecals(false);
			Made->RegisterComponent();
			Made->SetStaticMesh(Mesh);
			Made->SetVisibility(false);
			return Made;
		};
		PlateShadows = Make(TEXT("PlateShadow"), Plane);
		PlateFills = Make(TEXT("PlateFill"), Plane);
		PlateRims = Make(TEXT("PlateRim"), Cube);
		PlateEdges = Make(TEXT("PlateEdge"), Cube);
		PlateShadows->SetMaterial(0, GlowShared(FLinearColor::Black, 0.3f));
		PlateRims->SetMaterial(0, GlowShared(FLinearColor::Black, 0.55f));
		PlateKey.Reset();
	}
	// The walk's colour: teal, orange while sprinting (as the decal's).
	const FLinearColor Colour = bSprinting ? FLinearColor(1.0f, 0.62f, 0.2f) : FLinearColor(0.3f, 0.95f, 0.9f);
	PlateFills->SetMaterial(0, GlowShared(Colour, 0.2f));
	// The edge white whatever the walk's colour (the fill says teal or sprint orange).
	PlateEdges->SetMaterial(0, GlowShared(EdgeWhite, 0.95f));
	PlateShadows->SetVisibility(true);
	PlateFills->SetVisibility(true);
	PlateRims->SetVisibility(true);
	PlateEdges->SetVisibility(true);
	bPlatesShown = true;
	if (Key == PlateKey)
	{
		return;
	}
	PlateKey = Key;
	PlateShadows->ClearInstances();
	PlateFills->ClearInstances();
	PlateRims->ClearInstances();
	PlateEdges->ClearInstances();
	PlateSteps.Reset();

	const TMSim::FMap& Map = Battle.Map;
	const float Tile = TMSim::Ground::TileSize;
	const int32 Base = Map.NodeLevel(TMSim::FMap::NodeOf(Unit.Pos));
	const FQuat Across(FRotator(0.0f, 90.0f, 0.0f));
	for (const std::pair<TMSim::FNode, double>& Entry : Reachable)
	{
		if (!(TMSim::FBattle::TileNode(TMSim::FMap::NodePos(Entry.first)) == Entry.first))
		{
			continue;  // where it stands, off its tile's spot
		}
		const int32 Own = Map.NodeLevel(Entry.first);
		const float TX = static_cast<float>(Entry.first.X / TMSim::Ground::NodesPerTile) * Tile;
		const float TY = static_cast<float>(Entry.first.Y / TMSim::Ground::NodesPerTile) * Tile;
		const float X0 = TX + Inset, Y0 = TY + Inset, X1 = TX + Tile - Inset, Y1 = TY + Tile - Inset;
		// Each corner's height, from the ground a unit stands on there: equal on a
		// flat tile, rising across a ramp.
		auto CornerCm = [&](float X, float Y)
		{
			const int32 Level = Map.NodeLevel(TMSim::FMap::NodeOf(TMSim::FVec2(X, Y)));
			return static_cast<float>(Level > 0 ? Level : Own) * LevelCm();
		};
		const float H00 = CornerCm(X0, Y0), H10 = CornerCm(X1, Y0), H11 = CornerCm(X1, Y1), H01 = CornerCm(X0, Y1);
		const float SpanX = (X1 - X0) * TileSize;
		const float SpanY = (Y1 - Y0) * TileSize;
		const float RiseX = ((H10 + H11) - (H00 + H01)) * 0.5f;
		const float RiseY = ((H01 + H11) - (H00 + H10)) * 0.5f;
		const float Pitch = FMath::RadiansToDegrees(FMath::Atan2(RiseX, SpanX));
		const float Roll = -FMath::RadiansToDegrees(FMath::Atan2(RiseY, SpanY));
		const FQuat Tilt(FRotator(Pitch, 0.0f, Roll));
		const float LongX = FMath::Sqrt(SpanX * SpanX + RiseX * RiseX);
		const float LongY = FMath::Sqrt(SpanY * SpanY + RiseY * RiseY);
		const float GroundCm = (H00 + H10 + H11 + H01) * 0.25f;
		const FVector Middle((X0 + X1) * 0.5f * TileSize, (Y0 + Y1) * 0.5f * TileSize, GroundCm);

		// Its shadow on the ground, a little wider; the plate over it; its four edges.
		PlateShadows->AddInstance(FTransform(Tilt, Middle + FVector(0.0f, 0.0f, ShadowLift),
			FVector((LongX + 4.0f) / 100.0f, (LongY + 4.0f) / 100.0f, 1.0f)));
		const FVector Plate = Middle + Tilt.GetUpVector() * Lift;
		PlateFills->AddInstance(FTransform(Tilt, Plate, FVector(LongX / 100.0f, LongY / 100.0f, 1.0f)));
		const FVector AlongX = Tilt.RotateVector(FVector(LongX * 0.5f - EdgeWidth * 0.5f, 0.0f, 0.0f));
		const FVector AlongY = Tilt.RotateVector(FVector(0.0f, LongY * 0.5f - EdgeWidth * 0.5f, 0.0f));
		const FVector EdgeX(LongX / 100.0f, EdgeWidth / 100.0f, EdgeThick / 100.0f);
		const FVector EdgeY(LongY / 100.0f, EdgeWidth / 100.0f, EdgeThick / 100.0f);
		PlateEdges->AddInstance(FTransform(Tilt, Plate + AlongY, EdgeX));
		PlateEdges->AddInstance(FTransform(Tilt, Plate - AlongY, EdgeX));
		PlateEdges->AddInstance(FTransform(Tilt * Across, Plate + AlongX, EdgeY));
		PlateEdges->AddInstance(FTransform(Tilt * Across, Plate - AlongX, EdgeY));
		// The dark line just outside each edge, a hair lower, its pieces long enough to meet at the corners.
		const FVector Low = Plate - Tilt.GetUpVector() * RimDrop;
		const FVector OutX = Tilt.RotateVector(FVector(LongX * 0.5f + RimWidth * 0.5f, 0.0f, 0.0f));
		const FVector OutY = Tilt.RotateVector(FVector(0.0f, LongY * 0.5f + RimWidth * 0.5f, 0.0f));
		const FVector RimX((LongX + 2.0f * RimWidth) / 100.0f, RimWidth / 100.0f, EdgeThick / 100.0f);
		const FVector RimY((LongY + 2.0f * RimWidth) / 100.0f, RimWidth / 100.0f, EdgeThick / 100.0f);
		PlateRims->AddInstance(FTransform(Tilt, Low + OutY, RimX));
		PlateRims->AddInstance(FTransform(Tilt, Low - OutY, RimX));
		PlateRims->AddInstance(FTransform(Tilt * Across, Low + OutX, RimY));
		PlateRims->AddInstance(FTransform(Tilt * Across, Low - OutX, RimY));

		// Above or below the unit's own ground: a badge for the HUD (not on a ramp's slope).
		if (Own != Base && FMath::IsNearlyEqual(H00, H11) && FMath::IsNearlyEqual(H10, H01))
		{
			PlateSteps.Add(TPair<FVector, int32>(Plate, Own - Base));
		}
	}
}
