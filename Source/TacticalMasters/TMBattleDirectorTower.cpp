// The watchtower: the Signal Beacon, chosen by the human from six mockups
// (2026-09-30: "b"). A round stone tower banded with iron, flaring at its top
// into a ledge, with a great iron fire bowl on four legs. Nobody's: embers
// smoulder in the bowl. Held: the fire roars up in the holder's colour, blue
// or red (orange with the colour-blind option), and lights the ground round it.
//
// Half as wide as the mockup, because units stand right up against a tower to
// take it (Watchtower::Reach) and walk past it; half as tall again, so it
// stands over the trees. From its top it sees its whole radius, nothing in the
// way (FBattle::TowerSees).
//
// The stone and iron are drawn into vertex colours (TMPropMesh.h) and shown
// with M_GroundVertex; the fire is drawn unlit (GlowPaint), so no new material.
// The flames are a handful of stretched spheres that rise, sway and shrink,
// over and over (AdvanceTowers). Presentation only: the rules know a tower as
// a spot and who holds it.

#include "TMBattleDirector.h"
#include "TMPropMesh.h"
#include "TMSettings.h"

#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"

// Named, not anonymous: in a unity build an anonymous namespace's names reach
// every file compiled after this one (2026-10-01: 'Across' and 'Up' hid others' locals).
namespace TMTowerLook
{
	using namespace TMProp;

	/** Mockup metres across into centimetres here: half as wide (see above). */
	constexpr float Across = 50.0f;
	/** Mockup metres up into centimetres here: taller than the mockup, so the
	 *  beacon stands well over the trees and reads from across the board (2026-09-30). */
	constexpr float Up = 150.0f;
	/** How many flames burn in one bowl. */
	constexpr int32 Flames = 14;
	const FColor Iron(62, 63, 68);

	FLinearColor HolderColour(int32 Holder)
	{
		if (Holder == 0)
		{
			return FLinearColor(0.15f, 0.42f, 1.0f);
		}
		if (Holder == 1)
		{
			return FTMSettings::Get().bColorblind ? FLinearColor(1.0f, 0.6f, 0.1f) : FLinearColor(1.0f, 0.22f, 0.14f);
		}
		return FLinearColor(1.0f, 0.42f, 0.12f);  // embers
	}
}

void ATMBattleDirector::MakeTower(const FVector& Foot, int32 Seed)
{
	using namespace TMTowerLook;
	USceneComponent* Root = NewObject<USceneComponent>(this, NAME_None, RF_Transient);
	Root->SetMobility(EComponentMobility::Movable);
	Root->SetupAttachment(RootComponent);
	Root->RegisterComponent();
	Root->SetRelativeLocation(Foot);
	BoardProps.Add(Root);

	auto Z = [](float Metres) { return Metres * Up; };
	auto R = [](float Metres) { return Metres * Across; };

	// The stone: a footing, the shaft, the ledge it flares into.
	FPropMesh Stone;
	Stone.Blocks = 2.2f;
	Stone.Step = 2.5f;
	Stone.Frustum(FVector(0, 0, 0), R(0.9f), R(0.82f), Z(0.4f), 20, ESurface::DarkStone, Iron, Seed, 0.0f);
	Stone.Frustum(FVector(0, 0, Z(0.4f)), R(0.66f), R(0.52f), Z(3.2f), 24, ESurface::Stone, Iron, Seed + 1, 0.0f);
	Stone.Frustum(FVector(0, 0, Z(3.6f)), R(0.55f), R(0.72f), Z(0.3f), 24, ESurface::DarkStone, Iron, Seed + 2, 0.0f);
	UMaterialInterface* Vertex = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/UI/M_GroundVertex.M_GroundVertex"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	UMaterialInterface* Plain = Vertex ? Vertex : static_cast<UMaterialInterface*>(Paint(FLinearColor(0.2f, 0.19f, 0.18f)));
	BoardProps.Add(Stone.Make(this, Root, Plain));

	// The iron: two bands round the shaft, the bowl's legs, the bowl.
	FPropMesh Metal;
	Metal.Step = 2.5f;
	for (const float At : { 1.3f, 2.4f })
	{
		const float Radius = FMath::Lerp(R(0.66f), R(0.52f), (At - 0.4f) / 3.2f) + 1.2f;
		Metal.Frustum(FVector(0, 0, Z(At)), Radius, Radius, Z(0.1f), 24, ESurface::Metal, Iron, Seed + 3, 0.0f);
	}
	for (int32 i = 0; i < 4; ++i)
	{
		const float A = i * PI * 0.5f + PI * 0.25f;
		Metal.Post(FVector(FMath::Cos(A), FMath::Sin(A), 0.0f) * R(0.42f) + FVector(0, 0, Z(3.85f)), 3.0f, Z(0.6f), Iron, Seed + 4, 0.0f);
	}
	Metal.Frustum(FVector(0, 0, Z(4.3f)), R(0.35f), R(0.78f), Z(0.38f), 24, ESurface::Metal, Iron, Seed + 5, 0.0f);
	BoardProps.Add(Metal.Make(this, Root, Plain));

	FTMTower Made;
	Made.Root = Root;
	Made.Holder = -2;  // set on the first refresh
	Made.Phase = Hash(Seed, 5, 2) * 6.28f;
	Made.Bowl = FVector(0, 0, Z(4.66f));
	Made.Fire = GlowPaint(HolderColour(-1) * 2.5f);
	Made.Core = GlowPaint(FLinearColor(3.0f, 2.6f, 2.0f));

	// The coals filling the bowl.
	UStaticMesh* Disc = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UStaticMesh* Ball = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	auto Part = [&](UStaticMesh* Mesh, UMaterialInterface* Material)
	{
		UStaticMeshComponent* Piece = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
		Piece->SetMobility(EComponentMobility::Movable);
		Piece->SetupAttachment(Root);
		Piece->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Piece->RegisterComponent();
		Piece->SetStaticMesh(Mesh);
		Piece->SetMaterial(0, Material);
		Piece->SetCastShadow(false);
		Piece->SetReceivesDecals(false);
		BoardProps.Add(Piece);
		return Piece;
	};
	if (Disc)
	{
		UStaticMeshComponent* Coals = Part(Disc, Made.Fire.Get());
		Coals->SetRelativeLocation(FVector(0, 0, Z(4.6f)));
		Coals->SetRelativeScale3D(FVector(R(0.7f) * 2.0f, R(0.7f) * 2.0f, 6.0f) / 100.0f);
	}
	if (Ball)
	{
		for (int32 i = 0; i < Flames; ++i)
		{
			// Every third flame a hot white heart.
			Made.Flames.Add(Part(Ball, i % 3 == 0 ? Made.Core.Get() : Made.Fire.Get()));
		}
	}

	// The fire's light, on the stone and the ground round it.
	UPointLightComponent* Light = NewObject<UPointLightComponent>(this, NAME_None, RF_Transient);
	Light->SetupAttachment(Root);
	Light->RegisterComponent();
	Light->SetRelativeLocation(FVector(0, 0, Z(5.0f)));
	Light->SetCastShadows(false);
	Light->SetAttenuationRadius(650.0f);
	BoardProps.Add(Light);
	Made.Light = Light;
	Beacons.Add(Made);
}

void ATMBattleDirector::PaintTower(int32 Index, int32 Holder)
{
	if (!Beacons.IsValidIndex(Index))
	{
		return;
	}
	FTMTower& Tower = Beacons[Index];
	// Taken during the battle: the fire catches over two seconds, and the side's
	// sight spreads from it with the flames (the human's ask, 2026-10-02). The
	// first painting, as the board is built, is lit at once.
	const bool bHeld = Holder == 0 || Holder == 1;
	Tower.KindledAt = bHeld && Tower.Holder != -2 && Tower.Holder != Holder ? TowerClock : -100.0f;
	Tower.Holder = Holder;
	LightTower(Tower, TowerKindle(Index));
}

void ATMBattleDirector::LightTower(FTMTower& Tower, float Kindle)
{
	using namespace TMTowerLook;
	const bool bHeld = Tower.Holder == 0 || Tower.Holder == 1;
	const float K = bHeld ? Kindle : 0.0f;
	const FLinearColor Colour = HolderColour(Tower.Holder);
	if (Tower.Fire)
	{
		const float Glow = FMath::Lerp(1.2f, 3.0f, K);
		Tower.Fire->SetVectorParameterValue(TEXT("TintColorAndOpacity"), FLinearColor(Colour.R * Glow, Colour.G * Glow, Colour.B * Glow, 1.0f));
	}
	if (Tower.Core)
	{
		const FLinearColor Hot = FMath::Lerp(Colour, FLinearColor::White, 0.65f) * FMath::Lerp(1.4f, 4.0f, K);
		Tower.Core->SetVectorParameterValue(TEXT("TintColorAndOpacity"), FLinearColor(Hot.R, Hot.G, Hot.B, 1.0f));
	}
	if (Tower.Light)
	{
		Tower.Light->SetLightColor(Colour);
		// A flare as it catches, settling to the held fire's light.
		Tower.LightBase = FMath::Lerp(2500.0f, 14000.0f, K) * (1.0f + 0.5f * FMath::Sin(K * PI));
		Tower.Light->SetIntensity(Tower.LightBase);
		Tower.Light->SetAttenuationRadius(FMath::Lerp(260.0f, 700.0f, K));
	}
}

float ATMBattleDirector::TowerKindle(int32 Index) const
{
	if (!Beacons.IsValidIndex(Index))
	{
		return 1.0f;
	}
	const FTMTower& Tower = Beacons[Index];
	if (Tower.Holder != 0 && Tower.Holder != 1)
	{
		return 1.0f;
	}
	const float Part = FMath::Clamp((TowerClock - Tower.KindledAt) / TowerKindleSeconds, 0.0f, 1.0f);
	return Part * Part * (3.0f - 2.0f * Part);  // eased in and out
}

bool ATMBattleDirector::AnyTowerKindling(int32 Team) const
{
	for (int32 i = 0; i < Beacons.Num() && i < static_cast<int32>(Battle.Watchtowers.size()); ++i)
	{
		if (Battle.Watchtowers[static_cast<size_t>(i)].Owner == Team && TowerKindle(i) < 1.0f)
		{
			return true;
		}
	}
	return false;
}

bool ATMBattleDirector::SeenWithoutKindling(int32 Team, const TMSim::FVec2& Point) const
{
	// As FBattle::CanSee, the towers still catching left out.
	for (const TMSim::FUnit& Unit : Battle.Units)
	{
		if (Unit.IsAlive() && Unit.Team == Team && Unit.Pos.DistanceTo(Point) <= Battle.SightOf(Unit) && Battle.HasLineOfSight(Unit.Pos, Point))
		{
			return true;
		}
	}
	for (int32 i = 0; i < static_cast<int32>(Battle.Watchtowers.size()); ++i)
	{
		const TMSim::FWatchtower& Tower = Battle.Watchtowers[static_cast<size_t>(i)];
		if (Tower.Owner == Team && TowerKindle(i) >= 1.0f && Battle.TowerSees(Tower, Point))
		{
			return true;
		}
	}
	return false;
}

bool ATMBattleDirector::KindlingReaches(int32 Team, const TMSim::FVec2& Point) const
{
	for (int32 i = 0; i < static_cast<int32>(Battle.Watchtowers.size()); ++i)
	{
		const TMSim::FWatchtower& Tower = Battle.Watchtowers[static_cast<size_t>(i)];
		const float Kindle = TowerKindle(i);
		if (Tower.Owner == Team && Kindle < 1.0f && Battle.TowerSees(Tower, Point)
			&& static_cast<double>(Tower.Pos.DistanceTo(Point)) <= Battle.Tuning.WatchtowerSight * Kindle)
		{
			return true;
		}
	}
	return false;
}

void ATMBattleDirector::AdvanceTowers(float DeltaSeconds)
{
	using namespace TMTowerLook;
	TowerClock += DeltaSeconds;
	for (int32 Index = 0; Index < Beacons.Num(); ++Index)
	{
		FTMTower& Tower = Beacons[Index];
		if (!Tower.Root)
		{
			continue;
		}
		const bool bHeld = Tower.Holder == 0 || Tower.Holder == 1;
		const float T = TowerClock + Tower.Phase;
		// Held: tall flames, quick. Nobody's: low embers, slow. Just taken: the
		// embers grow into the held fire over two seconds, in step with the
		// side's sight spreading from it (AdvanceFog).
		const float Kindle = bHeld ? TowerKindle(Index) : 0.0f;
		const bool bCatching = bHeld && TowerClock - Tower.KindledAt < TowerKindleSeconds + 0.5f;
		if (bCatching)
		{
			LightTower(Tower, Kindle);
		}
		const float Rise = FMath::Lerp(0.22f, 1.25f, Kindle);
		const float Speed = FMath::Lerp(0.35f, 0.9f, Kindle);
		const float Width = FMath::Lerp(0.45f, 1.0f, Kindle);
		Tower.FlameTime = FMath::Fmod(Tower.FlameTime + DeltaSeconds * Speed, 1000.0f);
		for (int32 i = 0; i < Tower.Flames.Num(); ++i)
		{
			UStaticMeshComponent* Flame = Tower.Flames[i];
			if (!Flame)
			{
				continue;
			}
			// Each flame lives a moment from the coals up, then begins again.
			const float Life = FMath::Fmod(Tower.FlameTime + Tower.Phase + i / static_cast<float>(Flames), 1.0f);
			const bool bHeart = i % 3 == 0;
			const float Angle = i * 2.4f;
			const float Out = (bHeart ? 0.08f : 0.18f + (i % 4) * 0.08f) * Across * Width;
			const float Sway = FMath::Sin(T * 5.0f + i) * 4.0f * Width;
			const FVector At = Tower.Bowl + FVector(FMath::Cos(Angle) * Out + Sway, FMath::Sin(Angle) * Out, Life * Rise * Up * (bHeart ? 0.6f : 1.0f));
			Flame->SetRelativeLocation(At);
			// Fat at the coals, a thin tongue as it rises, gone at the top.
			const float Fade = FMath::Sin(FMath::Min(1.0f, Life * 1.15f) * PI);
			const float Thick = (bHeart ? 0.22f : 0.3f) * Across * Width * (1.0f - 0.55f * Life) * Fade;
			const float Tall = Thick * FMath::Lerp(1.4f, 2.6f, Kindle);
			Flame->SetRelativeScale3D(FVector(Thick, Thick, Tall) * 2.0f / 100.0f);
		}
		if (Tower.Light)
		{
			// Flicker.
			const float Flicker = 0.82f + 0.1f * FMath::Sin(T * 13.0f) + 0.08f * FMath::Sin(T * 7.3f + 1.0f);
			Tower.Light->SetIntensity(Tower.LightBase * Flicker);
		}
	}
}
