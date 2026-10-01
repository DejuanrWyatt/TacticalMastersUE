// Props drawn into vertex colours: stone in blocks and mortar, hammered metal,
// worn edges and shade near the ground, shown with M_GroundVertex (the ground's
// material), so a prop looks cut and built without a material of its own.
// Used by the treasure chest (TMBattleDirectorChest.cpp) and the watchtower
// (TMBattleDirectorTower.cpp).

#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"

namespace TMProp
{
	/** How finely the colours are drawn: one vertex every this many centimetres. */
	inline constexpr float Grain = 1.6f;

	inline uint32 Mix(int32 X, int32 Y, int32 Seed)
	{
		uint32 H = static_cast<uint32>(X) * 374761393u + static_cast<uint32>(Y) * 668265263u + static_cast<uint32>(Seed) * 2246822519u;
		H = (H ^ (H >> 13)) * 1274126177u;
		return H ^ (H >> 16);
	}
	inline float Hash(int32 X, int32 Y, int32 Seed)
	{
		return (Mix(X, Y, Seed) & 0xffffff) / 16777216.0f;
	}
	/** Smooth value noise, 0 to 1. */
	inline float Noise(float X, float Y, int32 Seed)
	{
		const int32 X0 = FMath::FloorToInt(X);
		const int32 Y0 = FMath::FloorToInt(Y);
		float FX = X - X0;
		float FY = Y - Y0;
		FX = FX * FX * (3.0f - 2.0f * FX);
		FY = FY * FY * (3.0f - 2.0f * FY);
		const float A = FMath::Lerp(Hash(X0, Y0, Seed), Hash(X0 + 1, Y0, Seed), FX);
		const float B = FMath::Lerp(Hash(X0, Y0 + 1, Seed), Hash(X0 + 1, Y0 + 1, Seed), FX);
		return FMath::Lerp(A, B, FY);
	}

	enum class ESurface : uint8 { Stone, DarkStone, Metal };

	/** The colour of a surface at a point on a face (U, V in centimetres along it), before shading. */
	inline FColor SurfaceAt(ESurface Kind, float U, float V, const FColor& Trim, int32 Seed)
	{
		float K = 1.0f;
		FColor Base = Trim;
		if (Kind == ESurface::Metal)
		{
			// Hammered and lightly brushed along its length.
			K = 0.86f + Noise(U * 0.35f, V * 0.35f, Seed) * 0.2f + Noise(U * 2.5f, V * 0.12f, Seed + 7) * 0.1f;
		}
		else
		{
			Base = Kind == ESurface::Stone ? FColor(126, 122, 116) : FColor(86, 84, 82);
			// Cut blocks in courses, each course shifted, mortar between them.
			const float Course = 9.0f;
			const float Length = 19.0f;
			const int32 Row = FMath::FloorToInt(V / Course);
			const float Shift = Hash(Row, 0, Seed) * Length;
			const float Along = U + Shift;
			const int32 Block = FMath::FloorToInt(Along / Length);
			const float InRow = V / Course - Row;
			const float InBlock = Along / Length - Block;
			const float Edge = FMath::Min(FMath::Min(InRow, 1.0f - InRow) * Course, FMath::Min(InBlock, 1.0f - InBlock) * Length);
			K = 0.8f + Noise(U * 0.18f, V * 0.18f, Seed) * 0.26f + Noise(U * 0.9f, V * 0.9f, Seed + 3) * 0.12f
				+ (Hash(Block, Row, Seed + 11) - 0.5f) * 0.14f;
			// Pits and chips.
			if (Noise(U * 1.4f, V * 1.4f, Seed + 5) > 0.82f)
			{
				K *= 0.78f;
			}
			if (Edge < 0.7f)
			{
				K *= 0.42f + 0.5f * Edge / 0.7f;
			}
		}
		return FColor(
			static_cast<uint8>(FMath::Clamp(Base.R * K, 0.0f, 255.0f)),
			static_cast<uint8>(FMath::Clamp(Base.G * K, 0.0f, 255.0f)),
			static_cast<uint8>(FMath::Clamp(Base.B * K, 0.0f, 255.0f)));
	}

	/** A mesh being built: one section, vertex-coloured. */
	struct FPropMesh
	{
		TArray<FVector> Points;
		TArray<int32> Triangles;
		TArray<FVector> Normals;
		TArray<FVector2D> Uvs;
		TArray<FLinearColor> Tints;
		TArray<FProcMeshTangent> Tangents;
		/** How big the stone's blocks are, against a chest's (1): a tower's are larger. */
		float Blocks = 1.0f;
		/** How finely this mesh is drawn, in centimetres a vertex. */
		float Step = Grain;

		/**
		 * A box, Centre and Size in centimetres, each face a grid fine enough to
		 * carry the stone's blocks. Worn edges catch the light, the foot is in shade.
		 */
		void Box(const FVector& Centre, const FVector& Size, ESurface Kind, const FColor& Trim, int32 Seed, float Ground)
		{
			const FVector Half = Size * 0.5f;
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				for (int32 Sign = -1; Sign <= 1; Sign += 2)
				{
					FVector Normal = FVector::ZeroVector;
					Normal[Axis] = static_cast<float>(Sign);
					const int32 A = (Axis + 1) % 3;
					const int32 B = (Axis + 2) % 3;
					const int32 NA = FMath::Clamp(FMath::CeilToInt(Size[A] / Step), 1, 64);
					const int32 NB = FMath::Clamp(FMath::CeilToInt(Size[B] / Step), 1, 64);
					const int32 First = Points.Num();
					for (int32 j = 0; j <= NB; ++j)
					{
						for (int32 i = 0; i <= NA; ++i)
						{
							FVector P = Centre;
							P[Axis] += Sign * Half[Axis];
							P[A] += -Half[A] + Size[A] * i / NA;
							P[B] += -Half[B] + Size[B] * j / NB;
							// Along the face: horizontal across, up the side (or across the top).
							const float U = (Axis == 2 ? P.X : (Axis == 0 ? P.Y : P.X)) + Seed * 13.0f;
							const float V = Axis == 2 ? P.Y : P.Z;
							FColor C = SurfaceAt(Kind, U / Blocks, (V + Sign * 31.0f) / Blocks, Trim, Seed + Axis * 2 + (Sign > 0 ? 1 : 0));
							// Worn edges: lighter where the face meets another.
							const float ToA = FMath::Min(i, NA - i) * Size[A] / NA;
							const float ToB = FMath::Min(j, NB - j) * Size[B] / NB;
							const float Wear = FMath::Clamp(1.0f - FMath::Min(ToA, ToB) / 1.6f, 0.0f, 1.0f);
							float K = 1.0f + Wear * (Kind == ESurface::Metal ? 0.35f : 0.22f);
							// Shade near the ground, and a little on the faces that look down.
							K *= FMath::Lerp(0.55f, 1.0f, FMath::Clamp((P.Z - Ground) / 18.0f, 0.0f, 1.0f));
							K *= Normal.Z < 0.0f ? 0.6f : 1.0f;
							C = FColor(static_cast<uint8>(FMath::Min(255.0f, C.R * K)), static_cast<uint8>(FMath::Min(255.0f, C.G * K)),
								static_cast<uint8>(FMath::Min(255.0f, C.B * K)));
							Points.Add(P);
							Normals.Add(Normal);
							Uvs.Add(FVector2D(static_cast<float>(i) / NA, static_cast<float>(j) / NB));
							Tints.Add(FLinearColor(C));
							FVector Along = FVector::ZeroVector;
							Along[A] = 1.0f;
							Tangents.Add(FProcMeshTangent(Along, false));
						}
					}
					for (int32 j = 0; j < NB; ++j)
					{
						for (int32 i = 0; i < NA; ++i)
						{
							const int32 P0 = First + j * (NA + 1) + i;
							const int32 P1 = P0 + 1;
							const int32 P2 = P0 + NA + 1;
							const int32 P3 = P2 + 1;
							// Wound so the face looks along its normal (A x B is the axis).
							if (Sign > 0)
							{
								Triangles.Append({ P0, P2, P1, P1, P2, P3 });
							}
							else
							{
								Triangles.Append({ P0, P1, P2, P1, P3, P2 });
							}
						}
					}
				}
			}
		}

		/** An upright post of eight sides, with a band at its foot and its top. */
		void Post(const FVector& Foot, float Radius, float Height, const FColor& Trim, int32 Seed, float Ground)
		{
			constexpr int32 Sides = 8;
			const int32 Rows = FMath::Max(2, FMath::CeilToInt(Height / Step));
			const int32 First = Points.Num();
			for (int32 r = 0; r <= Rows; ++r)
			{
				const float Z = Height * r / Rows;
				// The bands: a little wider, at each end.
				const bool bBand = Z < Height * 0.1f || Z > Height * 0.9f;
				const float R = Radius * (bBand ? 1.18f : 1.0f);
				for (int32 s = 0; s <= Sides; ++s)
				{
					const float Angle = 2.0f * PI * s / Sides;
					const FVector Out(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f);
					const FVector P = Foot + Out * R + FVector(0.0f, 0.0f, Z);
					FColor C = SurfaceAt(ESurface::Metal, s * 4.0f, Z, Trim, Seed);
					// A highlight down the side towards the light, shade at the foot.
					float K = 0.85f + 0.3f * FMath::Max(0.0f, FVector::DotProduct(Out, FVector(-0.5f, -0.6f, 0.0f).GetSafeNormal()));
					K *= bBand ? 1.12f : 1.0f;
					K *= FMath::Lerp(0.55f, 1.0f, FMath::Clamp((P.Z - Ground) / 18.0f, 0.0f, 1.0f));
					C = FColor(static_cast<uint8>(FMath::Min(255.0f, C.R * K)), static_cast<uint8>(FMath::Min(255.0f, C.G * K)),
						static_cast<uint8>(FMath::Min(255.0f, C.B * K)));
					Points.Add(P);
					Normals.Add(Out);
					Uvs.Add(FVector2D(static_cast<float>(s) / Sides, Z / Height));
					Tints.Add(FLinearColor(C));
					Tangents.Add(FProcMeshTangent(FVector(-Out.Y, Out.X, 0.0f), false));
				}
			}
			for (int32 r = 0; r < Rows; ++r)
			{
				for (int32 s = 0; s < Sides; ++s)
				{
					const int32 P0 = First + r * (Sides + 1) + s;
					const int32 P1 = P0 + 1;
					const int32 P2 = P0 + Sides + 1;
					const int32 P3 = P2 + 1;
					Triangles.Append({ P0, P1, P2, P1, P3, P2 });
				}
			}
			// Its cap.
			const int32 Middle = Points.Num();
			const FVector Top = Foot + FVector(0.0f, 0.0f, Height);
			Points.Add(Top);
			Normals.Add(FVector::UpVector);
			Uvs.Add(FVector2D(0.5f, 0.5f));
			Tints.Add(FLinearColor(Trim));
			Tangents.Add(FProcMeshTangent(FVector::ForwardVector, false));
			for (int32 s = 0; s <= Sides; ++s)
			{
				const float Angle = 2.0f * PI * s / Sides;
				Points.Add(Top + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f) * Radius * 1.18f);
				Normals.Add(FVector::UpVector);
				Uvs.Add(FVector2D(0.5f, 0.5f));
				Tints.Add(FLinearColor(Trim));
				Tangents.Add(FProcMeshTangent(FVector::ForwardVector, false));
			}
			for (int32 s = 0; s < Sides; ++s)
			{
				Triangles.Append({ Middle, Middle + 2 + s, Middle + 1 + s });
			}
		}

		/** A crystal: an eight-sided double point, flat-shaded. */
		void Crystal(const FVector& Centre, float Radius, float Height)
		{
			constexpr int32 Sides = 6;
			const FVector Tip = Centre + FVector(0.0f, 0.0f, Height);
			const FVector Foot = Centre - FVector(0.0f, 0.0f, Height * 0.55f);
			for (int32 s = 0; s < Sides; ++s)
			{
				const float A0 = 2.0f * PI * s / Sides;
				const float A1 = 2.0f * PI * (s + 1) / Sides;
				const FVector E0 = Centre + FVector(FMath::Cos(A0), FMath::Sin(A0), 0.0f) * Radius;
				const FVector E1 = Centre + FVector(FMath::Cos(A1), FMath::Sin(A1), 0.0f) * Radius;
				for (const FVector& Point : { Tip, Foot })
				{
					const bool bUp = Point == Tip;
					const FVector Normal = FVector::CrossProduct(E1 - E0, Point - E0).GetSafeNormal() * (bUp ? -1.0f : 1.0f);
					const int32 First = Points.Num();
					for (const FVector& P : { E0, E1, Point })
					{
						Points.Add(P);
						Normals.Add(Normal);
						Uvs.Add(FVector2D::ZeroVector);
						Tints.Add(FLinearColor::White);
						Tangents.Add(FProcMeshTangent(FVector::ForwardVector, false));
					}
					if (bUp)
					{
						Triangles.Append({ First, First + 2, First + 1 });
					}
					else
					{
						Triangles.Append({ First, First + 1, First + 2 });
					}
				}
			}
		}

		/**
		 * A tapered round (or many-sided) tower part: foot centre Foot, radius R0
		 * at the foot and R1 at the top, Height tall, with a top. Stone in courses
		 * round it, or metal; shade near the ground.
		 */
		void Frustum(const FVector& Foot, float R0, float R1, float Height, int32 Sides, ESurface Kind, const FColor& Trim, int32 Seed, float Ground)
		{
			const int32 Rows = FMath::Clamp(FMath::CeilToInt(Height / Step), 1, 160);
			const int32 Cols = FMath::Clamp(FMath::CeilToInt(2.0f * PI * FMath::Max(R0, R1) / Step), Sides, 240);
			const float Slope = (R0 - R1) / FMath::Max(1.0f, Height);
			const int32 First = Points.Num();
			for (int32 r = 0; r <= Rows; ++r)
			{
				const float Z = Height * r / Rows;
				const float R = FMath::Lerp(R0, R1, Z / Height);
				for (int32 c = 0; c <= Cols; ++c)
				{
					// Round, or flat-sided: the point on the polygon of Sides.
					const float Angle = 2.0f * PI * c / Cols;
					const float Sector = 2.0f * PI / Sides;
					const float Within = FMath::Fmod(Angle, Sector) - Sector * 0.5f;
					const float Out = Sides >= 16 ? 1.0f : FMath::Cos(Sector * 0.5f) / FMath::Cos(Within);
					const FVector Dir(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f);
					const float Mid = Angle - Within;
					const FVector Face = Sides >= 16 ? Dir : FVector(FMath::Cos(Mid), FMath::Sin(Mid), 0.0f);
					const FVector P = Foot + Dir * R * Out + FVector(0.0f, 0.0f, Z);
					const float U = Angle * FMath::Max(R0, R1) / Blocks + Seed * 13.0f;
					FColor C = SurfaceAt(Kind, U, Z / Blocks, Trim, Seed);
					float K = FMath::Lerp(0.55f, 1.0f, FMath::Clamp((P.Z - Ground) / 30.0f, 0.0f, 1.0f));
					// Lighter at its top and foot edges, where it is worn.
					K *= 1.0f + 0.18f * FMath::Clamp(1.0f - FMath::Min(Z, Height - Z) / 2.0f, 0.0f, 1.0f);
					C = FColor(static_cast<uint8>(FMath::Min(255.0f, C.R * K)), static_cast<uint8>(FMath::Min(255.0f, C.G * K)),
						static_cast<uint8>(FMath::Min(255.0f, C.B * K)));
					Points.Add(P);
					Normals.Add((Face + FVector(0.0f, 0.0f, Slope)).GetSafeNormal());
					Uvs.Add(FVector2D(static_cast<float>(c) / Cols, Z / FMath::Max(1.0f, Height)));
					Tints.Add(FLinearColor(C));
					Tangents.Add(FProcMeshTangent(FVector(-Face.Y, Face.X, 0.0f), false));
				}
			}
			for (int32 r = 0; r < Rows; ++r)
			{
				for (int32 c = 0; c < Cols; ++c)
				{
					const int32 P0 = First + r * (Cols + 1) + c;
					const int32 P1 = P0 + 1;
					const int32 P2 = P0 + Cols + 1;
					const int32 P3 = P2 + 1;
					Triangles.Append({ P0, P1, P2, P1, P3, P2 });
				}
			}
			// Its top: a disc in the colour of its rim.
			const int32 Middle = Points.Num();
			const FVector Top = Foot + FVector(0.0f, 0.0f, Height);
			const FColor Rim = SurfaceAt(Kind, 0.0f, Height / Blocks, Trim, Seed);
			Points.Add(Top);
			Normals.Add(FVector::UpVector);
			Uvs.Add(FVector2D(0.5f, 0.5f));
			Tints.Add(FLinearColor(Rim));
			Tangents.Add(FProcMeshTangent(FVector::ForwardVector, false));
			for (int32 c = 0; c <= Cols; ++c)
			{
				const int32 Index = First + Rows * (Cols + 1) + c;
				// Copied out first: adding an element of the array to itself breaks if it grows.
				const FVector Edge = Points[Index];
				const FLinearColor EdgeTint = Tints[Index];
				Points.Add(Edge);
				Normals.Add(FVector::UpVector);
				Uvs.Add(FVector2D(0.5f, 0.5f));
				Tints.Add(EdgeTint);
				Tangents.Add(FProcMeshTangent(FVector::ForwardVector, false));
			}
			for (int32 c = 0; c < Cols; ++c)
			{
				Triangles.Append({ Middle, Middle + 2 + c, Middle + 1 + c });
			}
		}

		UProceduralMeshComponent* Make(UObject* Owner, USceneComponent* Parent, UMaterialInterface* Material)
		{
			UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(Owner, NAME_None, RF_Transient);
			Mesh->SetMobility(EComponentMobility::Movable);
			Mesh->SetupAttachment(Parent);
			Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Mesh->RegisterComponent();
			// Both faces of every triangle, so no part shows through from any angle.
			const int32 OneSide = Triangles.Num();
			for (int32 k = 0; k < OneSide; k += 3)
			{
				Triangles.Append({ Triangles[k], Triangles[k + 2], Triangles[k + 1] });
			}
			Mesh->CreateMeshSection_LinearColor(0, Points, Triangles, Normals, Uvs, Tints, Tangents, false);
			Mesh->SetReceivesDecals(false);
			if (Material)
			{
				Mesh->SetMaterial(0, Material);
			}
			return Mesh;
		}
	};
}
