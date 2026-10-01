// The treasure chest: the Runic Coffer, one look for each tier of what it holds.
//
// Chosen by the human from three mockups (2026-09-30: "B"). A squat coffer of
// cut stone on a dark plinth, a metal post at each corner, three runes carved
// on its long sides and a crystal standing on its lid. The tier of the best
// item inside sets the look, as the mockup sheet has it:
//
//   common    iron posts, runes cut dark into the stone, a stone knob on top;
//   uncommon  bronze posts, the runes and a crystal glowing green;
//   rare      steel posts, blue runes and crystal, a gem on every post;
//   epic      gold posts, gold glow, the crystal pulsing and sparks rising.
//
// No new material: the stone and metal are drawn into the vertex colours of a
// mesh made here (blocks and mortar, mottling, worn edges, shade near the
// ground) and shown with M_GroundVertex, as the ground is; what glows is drawn
// unlit with the same see-through material the water uses, with a light of its
// colour. The crystal turns and bobs and the sparks rise in AdvanceChests.
// Presentation only: the rules know a cache as a spot and a list of items.

#include "TMBattleDirector.h"

#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "TMPropMesh.h"

namespace
{
	/** A tier's look: post metal, rune and crystal colour, and how strongly it glows. */
	struct FChestLook
	{
		FColor Trim;
		FLinearColor Accent;
		float Glow;
	};
	const FChestLook ChestLooks[] =
	{
		{ FColor(118, 118, 122), FLinearColor(0.78f, 0.8f, 0.85f), 0.0f },
		{ FColor(168, 112, 58), FLinearColor(0.35f, 0.88f, 0.47f), 0.6f },
		{ FColor(158, 178, 206), FLinearColor(0.37f, 0.6f, 1.0f), 1.0f },
		{ FColor(235, 182, 72), FLinearColor(1.0f, 0.77f, 0.31f), 1.6f },
	};
	/** How big a mockup unit is, in centimetres: the coffer is about two thirds of a metre long. */
	constexpr float Unit = 62.0f;

	using namespace TMProp;
	using FChestMesh = TMProp::FPropMesh;
}

UMaterialInstanceDynamic* ATMBattleDirector::GlowPaint(const FLinearColor& Colour)
{
	// Unlit, as the water is drawn: a colour that does not darken in shade, and
	// over 1 so the bloom picks it up.
	UMaterialInterface* Clear = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/Widget3DPassThrough_Translucent.Widget3DPassThrough_Translucent"));
	if (!Clear)
	{
		return Paint(Colour);
	}
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
	Mid->SetVectorParameterValue(TEXT("TintColorAndOpacity"), FLinearColor(Colour.R, Colour.G, Colour.B, 1.0f));
	return Mid;
}

USceneComponent* ATMBattleDirector::MakeChest(const FVector& Foot, int32 Tier, float Yaw, int32 Seed)
{
	const FChestLook& Look = ChestLooks[FMath::Clamp(Tier, 0, 3)];
	USceneComponent* Root = NewObject<USceneComponent>(this, NAME_None, RF_Transient);
	Root->SetMobility(EComponentMobility::Movable);
	Root->SetupAttachment(RootComponent);
	Root->RegisterComponent();
	Root->SetRelativeLocation(Foot);
	Root->SetRelativeRotation(FRotator(0.0f, Yaw, 0.0f));
	BoardProps.Add(Root);

	// The stone and the metal, in the coffer's own centimetres (front is +Y).
	auto At = [](float X, float Up, float Front) { return FVector(X, Front, Up) * Unit; };
	auto Span = [](float X, float Up, float Front) { return FVector(X, Front, Up) * Unit; };
	FChestMesh Body;
	Body.Box(At(0.0f, 0.05f, 0.0f), Span(1.08f, 0.1f, 0.72f), ESurface::DarkStone, Look.Trim, Seed, 0.0f);
	Body.Box(At(0.0f, 0.27f, 0.0f), Span(0.98f, 0.36f, 0.62f), ESurface::Stone, Look.Trim, Seed + 1, 0.0f);
	Body.Box(At(0.0f, 0.49f, 0.0f), Span(1.06f, 0.08f, 0.7f), ESurface::DarkStone, Look.Trim, Seed + 2, 0.0f);
	Body.Box(At(0.0f, 0.555f, 0.0f), Span(0.82f, 0.05f, 0.5f), ESurface::Stone, Look.Trim, Seed + 3, 0.0f);
	if (Look.Glow <= 0.0f)
	{
		Body.Box(At(0.0f, 0.61f, 0.0f), Span(0.12f, 0.06f, 0.12f), ESurface::DarkStone, Look.Trim, Seed + 4, 0.0f);
	}
	for (int32 X = -1; X <= 1; X += 2)
	{
		for (int32 Z = -1; Z <= 1; Z += 2)
		{
			Body.Post(At(0.48f * X, 0.04f, 0.3f * Z), 0.05f * Unit, 0.52f * Unit, Look.Trim, Seed + 6, 0.0f);
		}
	}
	UMaterialInterface* Vertex = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/UI/M_GroundVertex.M_GroundVertex"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	BoardProps.Add(Body.Make(this, Root, Vertex ? Vertex : static_cast<UMaterialInterface*>(Paint(FLinearColor(0.2f, 0.19f, 0.18f)))));

	// The runes, on both long sides: glowing in the tier's colour, or cut dark.
	UMaterialInterface* Rune = Look.Glow > 0.0f ? static_cast<UMaterialInterface*>(GlowPaint(Look.Accent * (1.2f + 1.4f * Look.Glow)))
		: static_cast<UMaterialInterface*>(Paint(FLinearColor(0.03f, 0.03f, 0.03f)));
	static const float Glyphs[3][3][4] =
	{
		{ { -0.03f, 0.0f, 0.012f, 0.16f }, { 0.0f, 0.05f, 0.08f, 0.012f }, { 0.02f, -0.04f, 0.012f, 0.08f } },
		{ { 0.0f, 0.0f, 0.012f, 0.16f }, { -0.03f, 0.03f, 0.06f, 0.012f }, { 0.03f, -0.03f, 0.06f, 0.012f } },
		{ { -0.02f, 0.0f, 0.012f, 0.14f }, { 0.02f, 0.0f, 0.012f, 0.14f }, { 0.0f, 0.06f, 0.06f, 0.012f } },
	};
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	for (int32 Side = -1; Side <= 1; Side += 2)
	{
		for (int32 k = 0; k < 3; ++k)
		{
			const float GX = (k - 1) * 0.28f * Side;
			for (const auto& Bar : Glyphs[k])
			{
				UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
				Part->SetMobility(EComponentMobility::Movable);
				Part->SetupAttachment(Root);
				Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Part->RegisterComponent();
				Part->SetStaticMesh(Cube);
				Part->SetRelativeLocation(At(GX + Bar[0] * Side, 0.28f + Bar[1], 0.314f * Side));
				// Twice as bold as the mockup's, to read from the camera.
				Part->SetRelativeScale3D(Span(Bar[2] * 1.8f, Bar[3] * 1.15f, 0.008f) / 100.0f);
				Part->SetMaterial(0, Rune);
				Part->SetCastShadow(false);
				Part->SetReceivesDecals(false);
				BoardProps.Add(Part);
			}
		}
	}

	FTMChest Made;
	Made.Root = Root;
	Made.Tier = Tier;
	Made.Phase = Hash(Seed, 3, 9) * 6.28f;
	if (Look.Glow > 0.0f)
	{
		// The crystal on the lid, and a gem on each post for rare and epic.
		FChestMesh Stone;
		Stone.Crystal(FVector::ZeroVector, 0.075f * Unit, 0.12f * Unit);
		USceneComponent* Holder = NewObject<USceneComponent>(this, NAME_None, RF_Transient);
		Holder->SetMobility(EComponentMobility::Movable);
		Holder->SetupAttachment(Root);
		Holder->RegisterComponent();
		Holder->SetRelativeLocation(At(0.0f, 0.72f, 0.0f));
		BoardProps.Add(Holder);
		UProceduralMeshComponent* Gem = Stone.Make(this, Holder, GlowPaint(Look.Accent * (1.5f + 1.2f * Look.Glow)));
		Gem->SetCastShadow(false);
		BoardProps.Add(Gem);
		Made.Crystal = Holder;
		if (Look.Glow >= 1.0f)
		{
			FChestMesh Gems;
			for (int32 X = -1; X <= 1; X += 2)
			{
				for (int32 Z = -1; Z <= 1; Z += 2)
				{
					Gems.Crystal(At(0.48f * X, 0.6f, 0.3f * Z), 0.035f * Unit, 0.05f * Unit);
				}
			}
			UProceduralMeshComponent* Small = Gems.Make(this, Root, GlowPaint(Look.Accent * (1.2f + Look.Glow)));
			Small->SetCastShadow(false);
			BoardProps.Add(Small);
		}
		// Its light, so the glow falls on the stone and the ground round it.
		UPointLightComponent* Light = NewObject<UPointLightComponent>(this, NAME_None, RF_Transient);
		Light->SetupAttachment(Root);
		Light->RegisterComponent();
		Light->SetRelativeLocation(At(0.0f, 0.75f, 0.0f));
		Light->SetLightColor(Look.Accent);
		Light->SetIntensity(1800.0f + 2600.0f * Look.Glow);
		Light->SetAttenuationRadius(140.0f + 70.0f * Look.Glow);
		Light->SetCastShadows(false);
		BoardProps.Add(Light);
		Made.Light = Light;
		Made.LightBase = Light->Intensity;
	}
	if (Tier >= 3 && Cube)
	{
		// Epic: sparks of gold rising round it.
		UMaterialInterface* Spark = GlowPaint(FLinearColor(3.0f, 2.4f, 1.2f));
		for (int32 i = 0; i < 12; ++i)
		{
			UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
			Part->SetMobility(EComponentMobility::Movable);
			Part->SetupAttachment(Root);
			Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Part->RegisterComponent();
			Part->SetStaticMesh(Cube);
			Part->SetMaterial(0, Spark);
			Part->SetCastShadow(false);
			Part->SetReceivesDecals(false);
			BoardProps.Add(Part);
			Made.Sparks.Add(Part);
		}
	}
	Chests.Add(Made);
	return Root;
}

void ATMBattleDirector::AdvanceChests(float DeltaSeconds)
{
	ChestClock += DeltaSeconds;
	for (FTMChest& Chest : Chests)
	{
		if (!Chest.Root || !Chest.Root->IsVisible())
		{
			continue;
		}
		const float T = ChestClock + Chest.Phase;
		if (Chest.Crystal)
		{
			// The crystal turns slowly and rises and falls a little.
			Chest.Crystal->SetRelativeLocation(FVector(0.0f, 0.0f, (0.72f + 0.025f * FMath::Sin(T * 1.8f)) * Unit));
			Chest.Crystal->SetRelativeRotation(FRotator(0.0f, T * 40.0f, 0.0f));
		}
		if (Chest.Light)
		{
			// A slow breath of light; the epic chest's quicker and deeper.
			const float Depth = Chest.Tier >= 3 ? 0.35f : 0.15f;
			Chest.Light->SetIntensity(Chest.LightBase * (1.0f - Depth + Depth * (0.5f + 0.5f * FMath::Sin(T * (Chest.Tier >= 3 ? 3.2f : 1.8f)))));
		}
		for (int32 i = 0; i < Chest.Sparks.Num(); ++i)
		{
			if (UStaticMeshComponent* Spark = Chest.Sparks[i])
			{
				// Each rises from round the coffer over its own second and a half, then starts again.
				const float Life = FMath::Fmod(T * 0.65f + i / 12.0f, 1.0f);
				const float Angle = i * 2.4f + T * 0.5f;
				const float Out = (0.32f + (i % 4) * 0.06f) * Unit;
				Spark->SetRelativeLocation(FVector(FMath::Cos(Angle) * Out, FMath::Sin(Angle) * Out * 0.7f, (0.45f + Life * 0.75f) * Unit));
				const float Size = 2.6f * FMath::Sin(Life * PI);
				Spark->SetRelativeScale3D(FVector(Size, Size, Size) / 100.0f);
				Spark->SetRelativeRotation(FRotator(T * 90.0f, T * 70.0f, 0.0f));
			}
		}
	}
}
