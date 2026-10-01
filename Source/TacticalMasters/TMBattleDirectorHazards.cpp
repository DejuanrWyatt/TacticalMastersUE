// The hazard grounds: burning ground hurts what ends its turn there, a healing
// spring mends it. Ten looks, one of each per theme, chosen by the human from
// the mockups (2026-09-30: "approve all 10"):
//
//                  burning ground          healing spring
//   meadow         smouldering grass       spring pool
//   ruined_keep    burning rubble          old fountain
//   volcanic       lava vent               hot spring
//   winter         fire pit in snow        steaming ice spring
//   moonlit_glade  witchfire ring          fairy pool
//
// Built from the basic shapes, lit or unlit (GlowShared), so no new material.
// What moves -- flames, embers, steam, motes -- is a sphere each, moved every
// frame (AdvanceHazards). The always-on cue that says "this tile does
// something" is a ring at the tile's edge painted into the indicator film
// (TMBattleDirectorIndicators.cpp): orange for burning, cyan for a spring.
// Presentation only: the rules know a hazard as a number on a tile.
//
// Distances are in metres, as the mockup's (a tile is two metres across).

#include "TMBattleDirector.h"

#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/Crc.h"

namespace TMHazardLook
{
	enum : uint8 { KFlame, KEmber, KSteam, KMote };

	FLinearColor Srgb(uint8 R, uint8 G, uint8 B)
	{
		return FLinearColor(FColor(R, G, B));
	}

	const FLinearColor Orange(1.0f, 0.45f, 0.12f);
	const FLinearColor HotWhite(1.0f, 0.85f, 0.45f);
	const FLinearColor Cyan(0.35f, 0.95f, 1.0f);
}

UMaterialInstanceDynamic* ATMBattleDirector::GlowShared(const FLinearColor& Colour, float Opacity)
{
	// GlowPaint makes a new one each call (a tower's is repainted); a hazard's
	// glows never change, so the same colour is shared.
	const uint32 Key = FCrc::MemCrc32(&Colour, sizeof(FLinearColor), FCrc::MemCrc32(&Opacity, sizeof(float)));
	if (TObjectPtr<UMaterialInstanceDynamic>* Known = GlowShades.Find(Key))
	{
		return *Known;
	}
	UMaterialInstanceDynamic* Mid = GlowPaint(Colour);
	if (Mid && Opacity < 1.0f)
	{
		Mid->SetVectorParameterValue(TEXT("TintColorAndOpacity"), FLinearColor(Colour.R, Colour.G, Colour.B, Opacity));
	}
	GlowShades.Add(Key, Mid);
	return Mid;
}

void ATMBattleDirector::BuildHazard(const FTMTheme& Theme, const FVector& Foot, int32 Hazard, int32 Seed)
{
	using namespace TMHazardLook;
	FRandomStream Roll(Seed);
	const float Cm = 100.0f;

	// A lit piece, and an unlit one that glows.
	auto Lit = [&](const TCHAR* Kind, const FVector& At, const FVector& Size, float Yaw, const FLinearColor& Colour)
	{
		return Shape(Kind, Foot + At * Cm, Size * Cm, FRotator(0, Yaw, 0), Colour);
	};
	auto Lamp = [&](const TCHAR* Kind, const FVector& At, const FVector& Size, const FLinearColor& Colour, float Opacity = 1.0f)
	{
		UStaticMeshComponent* Piece = Shape(Kind, Foot + At * Cm, Size * Cm, FRotator::ZeroRotator, Colour);
		if (Piece)
		{
			Piece->SetMaterial(0, GlowShared(Colour, Opacity));
			Piece->SetCastShadow(false);
		}
		return Piece;
	};
	auto Moving = [&](uint8 Kind, const FVector& At, float Size, const FLinearColor& Colour, float Opacity = 1.0f)
	{
		if (UStaticMeshComponent* Piece = Lamp(TEXT("Sphere"), At, FVector(Size), Colour, Opacity))
		{
			FTMHazardPart Part;
			Part.Piece = Piece;
			Part.Home = Foot + At * Cm;
			Part.Kind = Kind;
			Part.Phase = Roll.FRand();
			Part.Size = Size;
			HazardParts.Add(Part);
		}
	};

	// ---- the pieces the looks are made of
	auto Blot = [&](const FLinearColor& Colour, float Radius, float Z = 0.006f)
	{
		// A ragged patch on the ground: overlapping thin discs.
		for (int32 i = 0; i < 7; ++i)
		{
			const float A = Roll.FRand() * 6.28f;
			const float D = Roll.FRand() * Radius * 0.45f;
			const float R = Radius * (0.55f + Roll.FRand() * 0.35f);
			Lit(TEXT("Cylinder"), FVector(FMath::Cos(A) * D, FMath::Sin(A) * D, Z + i * 0.001f), FVector(R * 2.0f, R * 2.0f, 0.012f), 0.0f, Colour);
		}
	};
	auto Tufts = [&](const FLinearColor& Colour, int32 Count, float From, float To, float Height)
	{
		for (int32 i = 0; i < Count; ++i)
		{
			const float A = Roll.FRand() * 6.28f;
			const float D = Roll.FRandRange(From, To);
			for (int32 k = 0; k < 3; ++k)
			{
				const float H = Height * (0.6f + Roll.FRand() * 0.6f);
				Lit(TEXT("Cube"), FVector(FMath::Cos(A) * D + Roll.FRandRange(-0.05f, 0.05f), FMath::Sin(A) * D + Roll.FRandRange(-0.05f, 0.05f), H * 0.5f),
					FVector(0.025f, 0.025f, H), Roll.FRand() * 90.0f, Colour * (0.8f + Roll.FRand() * 0.4f));
			}
		}
	};
	auto Coals = [&](int32 Count, float Radius)
	{
		// Half dark, half glowing.
		for (int32 i = 0; i < Count; ++i)
		{
			const float A = Roll.FRand() * 6.28f;
			const float D = Roll.FRand() * Radius;
			const float S = 0.06f + Roll.FRand() * 0.1f;
			const FVector At(FMath::Cos(A) * D, FMath::Sin(A) * D, S * 0.35f);
			const FVector Size(S, S * 0.9f, S * 0.7f);
			if (i % 2 == 0)
			{
				Lamp(TEXT("Cube"), At, Size, Orange * (0.5f + Roll.FRand() * 1.2f));
			}
			else
			{
				Lit(TEXT("Cube"), At, Size, Roll.FRand() * 90.0f, Srgb(30, 18, 14));
			}
		}
	};
	auto Fire = [&](float X, float Y, float Size, const FLinearColor& Colour)
	{
		// Tongues that rise and fade, over a white-hot heart.
		for (int32 i = 0; i < 9; ++i)
		{
			const float A = i * 2.4f;
			const float Out = (0.05f + (i % 3) * 0.06f) * Size;
			Moving(KFlame, FVector(X + FMath::Cos(A) * Out, Y + FMath::Sin(A) * Out, 0.05f), Size, Colour * 2.2f);
		}
		Lamp(TEXT("Sphere"), FVector(X, Y, 0.1f * Size), FVector(0.35f * Size, 0.35f * Size, 0.25f * Size), HotWhite * 3.0f);
	};
	auto Embers = [&](int32 Count, float Spread)
	{
		for (int32 i = 0; i < Count; ++i)
		{
			Moving(KEmber, FVector(Roll.FRandRange(-0.8f, 0.8f) * Spread, Roll.FRandRange(-0.8f, 0.8f) * Spread, 0.15f), 0.05f + Roll.FRand() * 0.04f,
				HotWhite * 3.0f);
		}
	};
	auto Stones = [&](int32 Count, float Radius, const FLinearColor& Colour, float Size)
	{
		for (int32 i = 0; i < Count; ++i)
		{
			const float A = i / static_cast<float>(Count) * 6.28f + Roll.FRand() * 0.2f;
			const float S = Size * (0.7f + Roll.FRand() * 0.6f);
			Lit(TEXT("Cube"), FVector(FMath::Cos(A) * Radius, FMath::Sin(A) * Radius, S * 0.35f), FVector(S, S * 0.9f, S * 0.75f), A * 57.3f + Roll.FRand() * 20.0f,
				Colour * (0.85f + Roll.FRand() * 0.3f));
		}
	};
	auto Steam = [&](int32 Count, const FLinearColor& Colour, float Opacity)
	{
		for (int32 i = 0; i < Count; ++i)
		{
			Moving(KSteam, FVector(Roll.FRandRange(-0.5f, 0.5f), Roll.FRandRange(-0.5f, 0.5f), 0.2f), 0.45f + Roll.FRand() * 0.4f, Colour, Opacity);
		}
	};
	auto Motes = [&](int32 Count, const FLinearColor& Colour)
	{
		for (int32 i = 0; i < Count; ++i)
		{
			Moving(KMote, FVector(Roll.FRandRange(-0.9f, 0.9f), Roll.FRandRange(-0.9f, 0.9f), 0.25f + Roll.FRand() * 1.0f), 0.05f + Roll.FRand() * 0.04f,
				Colour * 2.5f);
		}
	};
	auto Pool = [&](float Radius, const FLinearColor& Colour, bool bRim, const FLinearColor& Rim)
	{
		// Water that shines: unlit, a little see-through.
		Lamp(TEXT("Cylinder"), FVector(0, 0, 0.012f), FVector(Radius * 2.0f, Radius * 2.0f, 0.02f), Colour * 0.8f, 0.92f);
		if (bRim)
		{
			Stones(14, Radius + 0.08f, Rim, 0.22f);
		}
	};

	const FLinearColor Grass = Theme.Tops.Num() > 0 ? Theme.Tops[0] : Srgb(86, 128, 52);
	const FLinearColor Scorch = Srgb(46, 34, 26);
	const FLinearColor Stone = Srgb(120, 116, 108);
	const FLinearColor DarkStone = Srgb(60, 58, 58);
	const FLinearColor Flag = Srgb(122, 116, 104);
	const FLinearColor Crust = Srgb(42, 36, 34);
	const FLinearColor Charred = Srgb(34, 26, 22);
	const FLinearColor IronGrey(0.05f, 0.05f, 0.055f);
	const FString& Id = Theme.Id;

	if (Hazard < 0)
	{
		if (Id == TEXT("ruined_keep"))
		{
			// Burning rubble: a toppled brazier, coals spilled over cracked flagstones.
			Lit(TEXT("Cube"), FVector(0, 0, 0.01f), FVector(1.9f, 1.9f, 0.02f), 0.0f, Flag);
			Blot(Scorch, 0.7f, 0.022f);
			if (UStaticMeshComponent* Bowl = Lit(TEXT("Cylinder"), FVector(0.35f, 0.15f, 0.2f), FVector(0.44f, 0.44f, 0.36f), 0.0f, IronGrey))
			{
				Bowl->SetRelativeRotation(FRotator(70.0f, 30.0f, 0.0f));
			}
			if (UStaticMeshComponent* Leg = Lit(TEXT("Cylinder"), FVector(0.72f, 0.35f, 0.08f), FVector(0.08f, 0.08f, 0.4f), 0.0f, IronGrey))
			{
				Leg->SetRelativeRotation(FRotator(80.0f, 10.0f, 0.0f));
			}
			Stones(6, 0.8f, Stone, 0.28f);
			Coals(16, 0.65f);
			Fire(-0.1f, 0.0f, 0.6f, Orange);
			Embers(10, 1.0f);
		}
		else if (Id == TEXT("volcanic"))
		{
			// Lava vent: crust plates over glowing cracks.
			Lamp(TEXT("Cube"), FVector(0, 0, 0.004f), FVector(1.9f, 1.9f, 0.008f), Orange * 1.4f);
			for (int32 i = -2; i <= 2; ++i)
			{
				for (int32 k = -2; k <= 2; ++k)
				{
					if (FMath::Abs(i) + FMath::Abs(k) > 3)
					{
						continue;
					}
					const float S = 0.33f + Roll.FRand() * 0.06f;
					Lit(TEXT("Cube"), FVector(i * 0.4f + Roll.FRandRange(-0.025f, 0.025f), k * 0.4f + Roll.FRandRange(-0.025f, 0.025f), 0.035f),
						FVector(S, S, 0.07f), Roll.FRandRange(-6.0f, 6.0f), Crust * (0.85f + Roll.FRand() * 0.3f));
				}
			}
			Fire(0.0f, 0.0f, 0.35f, Orange);
			Embers(14, 0.9f);
		}
		else if (Id == TEXT("winter"))
		{
			// Fire pit in snow: stones and crossed logs, the snow melted wet round it.
			Blot(Srgb(70, 62, 58) * 0.7f, 1.05f);
			Stones(10, 0.55f, DarkStone, 0.22f);
			Lit(TEXT("Cylinder"), FVector(0, 0, 0.012f), FVector(0.6f, 0.6f, 0.02f), 0.0f, Charred);
			for (const float Yaw : { 20.0f, 110.0f })
			{
				if (UStaticMeshComponent* Timber = Lit(TEXT("Cylinder"), FVector(0, 0, 0.09f), FVector(0.14f, 0.14f, 0.8f), 0.0f, Charred))
				{
					Timber->SetRelativeRotation(FRotator(90.0f, Yaw, 0.0f));
				}
			}
			Coals(8, 0.3f);
			Fire(0.0f, 0.0f, 0.65f, Orange);
			Embers(8, 1.0f);
		}
		else if (Id == TEXT("moonlit_glade"))
		{
			// Witchfire ring: mossy standing stones, fire under violet smoke.
			Blot(Scorch, 0.8f);
			Stones(7, 0.8f, DarkStone, 0.35f);
			Coals(10, 0.5f);
			Fire(0.0f, 0.0f, 0.55f, Orange);
			Steam(8, FLinearColor(0.6f, 0.3f, 1.0f), 0.22f);
			Embers(8, 1.0f);
		}
		else
		{
			// Smouldering grass (the meadow's, and any theme without its own).
			Blot(Scorch, 1.0f);
			Tufts(Srgb(30, 23, 15), 12, 0.2f, 0.9f, 0.15f);
			Coals(14, 0.75f);
			Fire(0.2f, -0.1f, 0.6f, Orange);
			Fire(-0.4f, 0.3f, 0.4f, Orange);
			Embers(10, 1.0f);
		}
		return;
	}

	if (Id == TEXT("ruined_keep"))
	{
		// Old fountain: a cracked stone basin brimming with glowing water.
		Lit(TEXT("Cube"), FVector(0, 0, 0.01f), FVector(1.9f, 1.9f, 0.02f), 0.0f, Flag);
		Lit(TEXT("Cylinder"), FVector(0, 0, 0.13f), FVector(1.7f, 1.7f, 0.26f), 0.0f, Stone);
		Lamp(TEXT("Cylinder"), FVector(0, 0, 0.265f), FVector(1.44f, 1.44f, 0.02f), FLinearColor(0.25f, 0.8f, 0.85f) * 0.8f, 0.92f);
		Lit(TEXT("Cylinder"), FVector(0, 0, 0.5f), FVector(0.2f, 0.2f, 0.5f), 0.0f, Stone);
		Lit(TEXT("Cylinder"), FVector(0, 0, 0.76f), FVector(0.56f, 0.56f, 0.08f), 0.0f, Stone);
		Lamp(TEXT("Sphere"), FVector(0, 0, 0.84f), FVector(0.3f, 0.3f, 0.14f), Cyan * 2.0f);
		Motes(8, FLinearColor(0.7f, 1.0f, 0.95f));
	}
	else if (Id == TEXT("volcanic"))
	{
		// Hot spring: a teal pool in black rock, steam rising.
		Pool(0.8f, FLinearColor(0.2f, 0.75f, 0.7f), true, Crust);
		Steam(14, FLinearColor(0.85f, 0.9f, 0.95f), 0.2f);
	}
	else if (Id == TEXT("winter"))
	{
		// Steaming ice spring: a pool cut into blocks of ice.
		Pool(0.78f, FLinearColor(0.25f, 0.8f, 0.8f), false, Stone);
		for (int32 i = 0; i < 12; ++i)
		{
			const float A = i / 12.0f * 6.28f;
			const float S = 0.2f + Roll.FRand() * 0.13f;
			Lit(TEXT("Cube"), FVector(FMath::Cos(A) * 0.9f, FMath::Sin(A) * 0.9f, S * 0.45f), FVector(S, S * 0.8f, S * 0.9f), A * 57.3f,
				FLinearColor(0.45f, 0.68f, 0.85f));
		}
		Steam(12, FLinearColor(0.85f, 0.9f, 0.95f), 0.2f);
	}
	else if (Id == TEXT("moonlit_glade"))
	{
		// Fairy pool: glowing water, luminous mushrooms, fireflies.
		Pool(0.8f, FLinearColor(0.3f, 0.75f, 1.0f), true, DarkStone);
		for (int32 i = 0; i < 7; ++i)
		{
			const float A = Roll.FRand() * 6.28f;
			const float D = 0.82f + Roll.FRand() * 0.14f;
			const float H = 0.1f + Roll.FRand() * 0.12f;
			const float X = FMath::Cos(A) * D;
			const float Y = FMath::Sin(A) * D;
			Lit(TEXT("Cylinder"), FVector(X, Y, H * 0.5f), FVector(0.05f, 0.05f, H), 0.0f, FLinearColor(0.7f, 0.7f, 0.65f));
			Lamp(TEXT("Sphere"), FVector(X, Y, H), FVector(0.16f, 0.16f, 0.07f), FLinearColor(0.3f, 0.7f, 1.0f) * 2.0f);
		}
		Motes(14, FLinearColor(0.8f, 1.0f, 0.5f));
	}
	else
	{
		// Spring pool (the meadow's, and any theme without its own): lily pads, reeds, motes.
		Pool(0.85f, FLinearColor(0.25f, 0.75f, 0.8f), true, Stone);
		for (const FVector2D Pad : { FVector2D(0.3f, 0.2f), FVector2D(-0.35f, -0.15f), FVector2D(0.05f, -0.45f) })
		{
			Lit(TEXT("Cylinder"), FVector(Pad.X, Pad.Y, 0.03f), FVector(0.26f, 0.26f, 0.01f), 0.0f, FLinearColor(0.06f, 0.25f, 0.04f));
		}
		Tufts(Grass * 1.2f, 8, 0.86f, 0.97f, 0.5f);
		Motes(10, FLinearColor(0.7f, 1.0f, 0.95f));
	}
}

void ATMBattleDirector::AdvanceHazards(float DeltaSeconds)
{
	using namespace TMHazardLook;
	HazardClock += DeltaSeconds;
	for (const FTMHazardPart& Part : HazardParts)
	{
		UStaticMeshComponent* Piece = Part.Piece.Get();
		// Hidden by the fog: nothing to move.
		if (!Piece || !Piece->IsVisible())
		{
			continue;
		}
		const float T = HazardClock + Part.Phase * 10.0f;
		const float S = Part.Size * 100.0f;
		FVector At = Part.Home;
		FVector Scale(Part.Size);
		if (Part.Kind == KFlame)
		{
			// From the coals up, thinning, gone at the top; then again.
			const float Life = FMath::Frac(HazardClock * 0.95f + Part.Phase);
			const float Fade = FMath::Sin(FMath::Min(1.0f, Life * 1.15f) * PI);
			At += FVector(FMath::Sin(T * 5.0f) * 0.05f * S, FMath::Cos(T * 4.3f) * 0.03f * S, Life * 1.3f * S);
			const float Thick = 0.32f * Part.Size * (1.0f - 0.55f * Life) * Fade;
			Scale = FVector(Thick * 2.0f, Thick * 2.0f, Thick * 4.8f);
		}
		else if (Part.Kind == KEmber)
		{
			// Drifting up and winking out.
			const float Life = FMath::Frac(HazardClock * 0.22f + Part.Phase);
			At += FVector(FMath::Sin(T * 1.7f) * 12.0f, FMath::Cos(T * 1.3f) * 12.0f, Life * 170.0f);
			Scale = FVector(Part.Size * (1.0f - Life) * FMath::Min(1.0f, Life * 6.0f));
		}
		else if (Part.Kind == KSteam)
		{
			// Rising, swelling, thinning away.
			const float Life = FMath::Frac(HazardClock * 0.12f + Part.Phase);
			At += FVector(FMath::Sin(T * 0.6f) * 15.0f, FMath::Cos(T * 0.45f) * 10.0f, Life * 160.0f);
			Scale = FVector(Part.Size * (0.5f + Life) * FMath::Sin(Life * PI));
		}
		else
		{
			// Motes and fireflies hang and wander.
			At += FVector(FMath::Sin(T * 0.7f) * 15.0f, FMath::Cos(T * 0.53f) * 15.0f, FMath::Sin(T * 1.3f) * 12.0f);
			Scale = FVector(Part.Size * (0.75f + 0.25f * FMath::Sin(T * 3.0f)));
		}
		Piece->SetRelativeLocationAndRotation(At, FRotator::ZeroRotator);
		Piece->SetRelativeScale3D(Scale.ComponentMax(FVector(0.001f)));
	}
}
