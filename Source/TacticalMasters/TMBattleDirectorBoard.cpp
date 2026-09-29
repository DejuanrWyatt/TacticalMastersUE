// The board and its look: the ground the rules give, dressed in a theme.
//
// A theme (Content/Data/Themes/<id>.theme.json) says what colour the ground is
// at each height and down its sides, what the rock looks like (pillars of a
// ruin, boulders, crystals), the water, the embers and springs, what grows in
// the land around the board, and the sun, sky and fog. The same map can be a
// summer meadow or a winter pass; the rules see the same ground either way.
//
// Everything is built from the engine's basic shapes, painted through its basic
// shape material, and lit by the level's own sun and sky -- so a theme needs no
// asset made in the editor. Better meshes (a Fab pack, say) can replace the
// shapes later without changing what a theme says.

#include "TMBattleDirector.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Math/RandomStream.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	FLinearColor Hex(const TSharedPtr<FJsonObject>& Json, const TCHAR* Key, const FLinearColor& Otherwise)
	{
		FString Text;
		if (Json.IsValid() && Json->TryGetStringField(Key, Text) && Text.StartsWith(TEXT("#")) && Text.Len() == 7)
		{
			return FLinearColor(FColor::FromHex(Text));
		}
		return Otherwise;
	}

	float Number(const TSharedPtr<FJsonObject>& Json, const TCHAR* Key, float Otherwise)
	{
		double Value = Otherwise;
		return Json.IsValid() && Json->TryGetNumberField(Key, Value) ? static_cast<float>(Value) : Otherwise;
	}

	/** A colour moved a little, the same way every time for the same tile. */
	FLinearColor Wander(const FLinearColor& Colour, float Amount, int32 Seed)
	{
		const FRandomStream Dice(Seed);
		const float Shift = (Dice.FRand() * 2.0f - 1.0f) * Amount;
		return FLinearColor(FMath::Clamp(Colour.R + Shift, 0.0f, 1.0f), FMath::Clamp(Colour.G + Shift * 1.1f, 0.0f, 1.0f),
			FMath::Clamp(Colour.B + Shift * 0.8f, 0.0f, 1.0f), 1.0f);
	}

	/**
	 * Ground a unit can stand on answers the pointer: a click on the board is
	 * found by tracing into it (PickUnderCursor). Everything else -- rock, water,
	 * the land around, trees -- lets the trace through, as the old board did by
	 * having nothing there.
	 */
	void Solid(UStaticMeshComponent* Part)
	{
		if (Part)
		{
			Part->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Part->SetCollisionResponseToAllChannels(ECR_Block);
		}
	}

	/** The top of a raised tile is a slab of its own colour on a column of the side's. */
	constexpr float SlabMetres = 0.14f;
}

void ATMBattleDirector::LoadThemes()
{
	if (bThemesRead)
	{
		return;
	}
	bThemesRead = true;
	const FString Dir = FPaths::ProjectContentDir() / TEXT("Data/Themes");
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(Dir / TEXT("*.theme.json")), true, false);
	Files.Sort();
	for (const FString& File : Files)
	{
		FString Text;
		TSharedPtr<FJsonObject> Json;
		if (!FFileHelper::LoadFileToString(Text, *(Dir / File))
			|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid()
			|| Json->GetStringField(TEXT("format")) != TEXT("tactical-masters-theme"))
		{
			UE_LOG(LogTemp, Warning, TEXT("theme file %s left out: not a theme"), *File);
			continue;
		}
		FTMTheme Theme;
		Theme.Id = File.LeftChop(11);  // ".theme.json"
		Json->TryGetStringField(TEXT("name"), Theme.Name);
		auto Part = [&Json](const TCHAR* Key) { const TSharedPtr<FJsonObject>* Found = nullptr; return Json->TryGetObjectField(Key, Found) ? *Found : TSharedPtr<FJsonObject>(); };

		const TSharedPtr<FJsonObject> Ground = Part(TEXT("ground"));
		const TArray<TSharedPtr<FJsonValue>>* Tops = nullptr;
		if (Ground.IsValid() && Ground->TryGetArrayField(TEXT("tops"), Tops))
		{
			for (const TSharedPtr<FJsonValue>& Top : *Tops)
			{
				Theme.Tops.Add(FLinearColor(FColor::FromHex(Top->AsString())));
			}
		}
		if (Theme.Tops.Num() == 0)
		{
			Theme.Tops.Add(FLinearColor(0.35f, 0.5f, 0.25f));
		}
		Theme.Side = Hex(Ground, TEXT("side"), Theme.Side);
		Theme.Jitter = Number(Ground, TEXT("jitter"), Theme.Jitter);

		const TSharedPtr<FJsonObject> Rock = Part(TEXT("rock"));
		Theme.Rock = Hex(Rock, TEXT("colour"), Theme.Rock);
		if (Rock.IsValid())
		{
			Rock->TryGetStringField(TEXT("style"), Theme.RockStyle);
		}
		const TSharedPtr<FJsonObject> Water = Part(TEXT("water"));
		Theme.Water = Hex(Water, TEXT("colour"), Theme.Water);
		Theme.WaterOpacity = Number(Water, TEXT("opacity"), Theme.WaterOpacity);
		if (Water.IsValid())
		{
			Water->TryGetBoolField(TEXT("glows"), Theme.bWaterGlows);
		}
		const TSharedPtr<FJsonObject> Embers = Part(TEXT("embers"));
		Theme.Embers = Hex(Embers, TEXT("colour"), Theme.Embers);
		Theme.EmberGlow = Hex(Embers, TEXT("glow"), Theme.EmberGlow);
		const TSharedPtr<FJsonObject> Spring = Part(TEXT("spring"));
		Theme.Spring = Hex(Spring, TEXT("colour"), Theme.Spring);
		Theme.SpringGlow = Hex(Spring, TEXT("glow"), Theme.SpringGlow);

		const TSharedPtr<FJsonObject> Around = Part(TEXT("around"));
		Theme.Outside = Hex(Around, TEXT("ground"), Theme.Outside);
		if (Around.IsValid())
		{
			Around->TryGetStringField(TEXT("trees"), Theme.Trees);
		}
		Theme.TreeCount = static_cast<int32>(Number(Around, TEXT("treeCount"), static_cast<float>(Theme.TreeCount)));
		Theme.Leaves = Hex(Around, TEXT("leaves"), Theme.Leaves);
		Theme.Trunk = Hex(Around, TEXT("trunk"), Theme.Trunk);
		Theme.RockCount = static_cast<int32>(Number(Around, TEXT("rockCount"), static_cast<float>(Theme.RockCount)));

		const TSharedPtr<FJsonObject> Sun = Part(TEXT("sun"));
		Theme.SunPitch = Number(Sun, TEXT("pitch"), Theme.SunPitch);
		Theme.SunYaw = Number(Sun, TEXT("yaw"), Theme.SunYaw);
		Theme.SunIntensity = Number(Sun, TEXT("intensity"), Theme.SunIntensity);
		Theme.SunColour = Hex(Sun, TEXT("colour"), Theme.SunColour);
		Theme.SkyIntensity = Number(Part(TEXT("sky")), TEXT("intensity"), Theme.SkyIntensity);
		const TSharedPtr<FJsonObject> Fog = Part(TEXT("fog"));
		Theme.FogDensity = Number(Fog, TEXT("density"), Theme.FogDensity);
		Theme.FogColour = Hex(Fog, TEXT("colour"), Theme.FogColour);

		const TSharedPtr<FJsonObject> Kit = Part(TEXT("kit"));
		auto Paths = [&Kit](const TCHAR* Key, TArray<FString>& Into)
		{
			const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
			if (Kit.IsValid() && Kit->TryGetArrayField(Key, List))
			{
				for (const TSharedPtr<FJsonValue>& Item : *List)
				{
					Into.Add(Item->AsString());
				}
			}
		};
		Paths(TEXT("top"), Theme.KitTop);
		Paths(TEXT("rock"), Theme.KitRock);
		Paths(TEXT("tree"), Theme.KitTree);
		Paths(TEXT("boulder"), Theme.KitBoulder);
		Theme.KitRockFill = Number(Kit, TEXT("rockFill"), Theme.KitRockFill);
		Theme.KitTreeHeight = Number(Kit, TEXT("treeHeight"), Theme.KitTreeHeight);
		Theme.KitBoulderSize = Number(Kit, TEXT("boulderSize"), Theme.KitBoulderSize);

		ThemeIds.Add(Theme.Id);
		Themes.Add(Theme.Id, Theme);
	}
	UE_LOG(LogTemp, Log, TEXT("%d themes"), ThemeIds.Num());
}

const ATMBattleDirector::FTMTheme& ATMBattleDirector::ActiveTheme() const
{
	static const FTMTheme Plain;
	// The one chosen, else the map's own, else any, else a plain look.
	if (const FTMTheme* Chosen = Themes.Find(Setup.ThemeId))
	{
		return *Chosen;
	}
	if (const FTMTheme* Own = Themes.Find(UTF8_TO_TCHAR(TMSim::FindMap(Setup.MapId).Theme.c_str())))
	{
		return *Own;
	}
	return ThemeIds.Num() > 0 ? Themes[ThemeIds[0]] : Plain;
}

UMaterialInstanceDynamic* ATMBattleDirector::Paint(const FLinearColor& Colour)
{
	const uint32 Key = Colour.ToFColor(false).ToPackedARGB();
	if (TObjectPtr<UMaterialInstanceDynamic>* Known = Paints.Find(Key))
	{
		return *Known;
	}
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	UMaterialInstanceDynamic* Mid = Base ? UMaterialInstanceDynamic::Create(Base, this) : nullptr;
	if (Mid)
	{
		Mid->SetVectorParameterValue(TEXT("Color"), Colour);
	}
	Paints.Add(Key, Mid);
	return Mid;
}

UStaticMeshComponent* ATMBattleDirector::Shape(const TCHAR* Name, const FVector& Where, const FVector& Size, const FRotator& Turn, const FLinearColor& Colour)
{
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), Name, Name));
	if (!Mesh)
	{
		return nullptr;
	}
	// Movable and transient, like the units: built from the rules whenever a
	// battle starts, never saved into the level (see the tiles' note).
	UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
	Part->SetMobility(EComponentMobility::Movable);
	Part->SetupAttachment(RootComponent);
	Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Part->RegisterComponent();
	Part->SetStaticMesh(Mesh);
	// The basic shapes are a metre across and centred, but the cone and the
	// cylinder stand on their middles too; Where is always the centre.
	Part->SetRelativeLocation(Where);
	Part->SetRelativeRotation(Turn);
	Part->SetRelativeScale3D(Size / 100.0f);
	Part->SetMaterial(0, Paint(Colour));
	// Only the top of the ground takes the indicators (Solid turns it on).
	Part->SetReceivesDecals(false);
	BoardProps.Add(Part);
	return Part;
}

UStaticMeshComponent* ATMBattleDirector::KitPiece(const FString& Path, const FVector& Foot, float Footprint, float Height, float Yaw, bool bStretch, float MinHeight)
{
	TObjectPtr<UStaticMesh>* Known = KitMeshes.Find(Path);
	if (!Known)
	{
		UStaticMesh* Loaded = LoadObject<UStaticMesh>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!Loaded)
		{
			UE_LOG(LogTemp, Warning, TEXT("theme kit: no mesh at %s; the basic shape stands in"), *Path);
		}
		Known = &KitMeshes.Add(Path, Loaded);
	}
	UStaticMesh* Mesh = *Known;
	if (!Mesh)
	{
		return nullptr;
	}
	const FBox Bounds = Mesh->GetBoundingBox();
	const FVector Size = Bounds.GetSize().ComponentMax(FVector(1.0));
	FVector Scale;
	if (bStretch)
	{
		// Floor pieces: exactly a tile across, and as thick as asked.
		Scale = FVector(Footprint / Size.X, Footprint / Size.Y, Height > 0.0f ? Height / Size.Z : (Footprint / Size.X + Footprint / Size.Y) * 0.5);
	}
	else
	{
		double Uniform = Height > 0.0f ? Height / Size.Z : Footprint / FMath::Max(Size.X, Size.Y);
		// Cover has to look like cover: never shorter than asked.
		if (MinHeight > 0.0f && Size.Z * Uniform < MinHeight)
		{
			Uniform = MinHeight / Size.Z;
		}
		Scale = FVector(Uniform);
	}
	// Stood on its bottom middle, wherever the mesh's own pivot is.
	const FRotator Turn(0.0f, Yaw, 0.0f);
	const FVector Pivot(Bounds.GetCenter().X, Bounds.GetCenter().Y, Bounds.Min.Z);
	UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
	Part->SetMobility(EComponentMobility::Movable);
	Part->SetupAttachment(RootComponent);
	Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Part->RegisterComponent();
	Part->SetStaticMesh(Mesh);
	Part->SetReceivesDecals(false);
	Part->SetRelativeScale3D(Scale);
	Part->SetRelativeRotation(Turn);
	Part->SetRelativeLocation(Foot - Turn.RotateVector(Pivot * Scale));
	BoardProps.Add(Part);
	return Part;
}

void ATMBattleDirector::BuildBoard()
{
	LoadThemes();
	const FTMTheme& Theme = ActiveTheme();
	const TMSim::FMap& Map = Battle.Map;
	const float M = TileSize;  // centimetres to a metre
	const float Tile = TMSim::Ground::TileSize * M;
	const float Level = TMSim::Ground::LevelHeight * M;
	const FRandomStream Dice(GetTypeHash(FString(UTF8_TO_TCHAR(Setup.MapId.c_str()))) ^ GetTypeHash(Theme.Id));
	auto TopColour = [&Theme](int32 Height) { return Theme.Tops[FMath::Clamp(Height - 1, 0, Theme.Tops.Num() - 1)]; };

	for (int32 Y = 0; Y < Map.TilesY; ++Y)
	{
		for (int32 X = 0; X < Map.TilesX; ++X)
		{
			const int32 Index = Y * Map.TilesX + X;
			const int32 Height = Map.TileLevel(X, Y);
			const bool bRock = Map.Covers[Index] != 0;
			const int32 Hazard = Map.Hazards[Index];
			const FVector Centre((X + 0.5f) * Tile, (Y + 0.5f) * Tile, 0.0f);
			const int32 Seed = Index * 7919 + 17;
			// A hair under a tile leaves a seam, so the grid reads without lines drawn on it.
			const float Across = Tile * 0.985f;

			if (Height > 0)
			{
				// A column of the side's colour up to under the top, the top on it.
				const float Top = Height * Level;
				const float Column = FMath::Max(1.0f, Top - SlabMetres * M);
				Solid(Shape(TEXT("Cube"), Centre + FVector(0, 0, Column * 0.5f), FVector(Across, Across, Column), FRotator::ZeroRotator,
					Wander(Theme.Side, Theme.Jitter * 0.6f, Seed + 1)));
				FLinearColor Surface = Wander(TopColour(Height), Theme.Jitter, Seed);
				if (Hazard < 0)
				{
					Surface = Wander(Theme.Embers, Theme.Jitter * 0.5f, Seed);
				}
				else if (Hazard > 0)
				{
					Surface = Theme.Spring;
				}
				UStaticMeshComponent* Slab = Shape(TEXT("Cube"), Centre + FVector(0, 0, Top - SlabMetres * M * 0.5f), FVector(Across, Across, SlabMetres * M),
					FRotator::ZeroRotator, Surface);
				Solid(Slab);
				if (Slab)
				{
					Slab->SetReceivesDecals(true);
				}
				if (Hazard == 0 && Theme.KitTop.Num() > 0 && Slab
					&& KitPiece(Theme.KitTop[Dice.RandHelper(Theme.KitTop.Num())], Centre + FVector(0, 0, Top - SlabMetres * M), Across,
						SlabMetres * M, 90.0f * Dice.RandHelper(4), true))
				{
					// Hidden, not gone: it is still what a click on the board finds.
					Slab->SetHiddenInGame(true);
					Cast<UStaticMeshComponent>(BoardProps.Last())->SetReceivesDecals(true);
				}

				if (Hazard != 0)
				{
					// Embers glow and smoulder; a spring shines. A light each, and for
					// embers a scatter of hot coals on the ground.
					const FLinearColor Glow = Hazard < 0 ? Theme.EmberGlow : Theme.SpringGlow;
					if (Hazard < 0)
					{
						for (int32 k = 0; k < 7; ++k)
						{
							const FVector Coal(Dice.FRandRange(-0.4f, 0.4f) * Tile, Dice.FRandRange(-0.4f, 0.4f) * Tile, Top + 4.0f);
							Shape(TEXT("Cube"), Centre + Coal, FVector(Dice.FRandRange(10.0f, 22.0f), Dice.FRandRange(10.0f, 22.0f), 8.0f),
								FRotator(0, Dice.FRandRange(0.0f, 90.0f), 0), Glow);
						}
					}
					else
					{
						Shape(TEXT("Cylinder"), Centre + FVector(0, 0, Top + 2.0f), FVector(Tile * 0.7f, Tile * 0.7f, 4.0f), FRotator::ZeroRotator,
							Glow * 0.9f);
					}
					UPointLightComponent* Light = NewObject<UPointLightComponent>(this, NAME_None, RF_Transient);
					Light->SetupAttachment(RootComponent);
					Light->RegisterComponent();
					Light->SetRelativeLocation(Centre + FVector(0, 0, Top + 60.0f));
					Light->SetLightColor(Glow);
					Light->SetIntensity(Hazard < 0 ? 9000.0f : 6000.0f);
					Light->SetAttenuationRadius(320.0f);
					Light->SetCastShadows(false);
					BoardProps.Add(Light);
					BoardLights.Add(Light);
					BoardLightBase.Add(Light->Intensity);
				}
			}
			else if (bRock)
			{
				// Rock stands on the ground: a floor, and the rock itself, tall
				// enough that it plainly hides what is behind it.
				Shape(TEXT("Cube"), Centre + FVector(0, 0, Level * 0.5f), FVector(Across, Across, Level), FRotator::ZeroRotator,
					Wander(Theme.Side, Theme.Jitter * 0.6f, Seed + 1));
				const FLinearColor Stone = Wander(Theme.Rock, Theme.Jitter, Seed + 2);
				if (Theme.KitRock.Num() > 0
					&& KitPiece(Theme.KitRock[Dice.RandHelper(Theme.KitRock.Num())], Centre + FVector(0, 0, Level), Tile * Theme.KitRockFill,
						0.0f, Dice.FRandRange(0.0f, 360.0f), false, 1.8f * M))
				{
					// The kit's rock stands here; nothing else is needed.
				}
				else if (Theme.RockStyle == TEXT("pillars"))
				{
					// A ruin: a pillar, some broken off, with a fallen block beside.
					const float Tall = Dice.FRandRange(2.2f, 3.4f) * M * (Dice.FRand() < 0.3f ? 0.55f : 1.0f);
					Shape(TEXT("Cylinder"), Centre + FVector(0, 0, Level + Tall * 0.5f), FVector(Tile * 0.55f, Tile * 0.55f, Tall),
						FRotator::ZeroRotator, Stone);
					Shape(TEXT("Cube"), Centre + FVector(0, 0, Level + 12.0f), FVector(Tile * 0.8f, Tile * 0.8f, 24.0f), FRotator::ZeroRotator, Stone * 0.9f);
					Shape(TEXT("Cube"), Centre + FVector(Dice.FRandRange(-0.3f, 0.3f) * Tile, Dice.FRandRange(-0.3f, 0.3f) * Tile, Level + 25.0f),
						FVector(Tile * 0.5f, Tile * 0.3f, 50.0f), FRotator(0, Dice.FRandRange(0.0f, 180.0f), Dice.FRandRange(-15.0f, 15.0f)), Stone * 0.85f);
				}
				else if (Theme.RockStyle == TEXT("crystals"))
				{
					for (int32 k = 0; k < 4; ++k)
					{
						const float Tall = Dice.FRandRange(1.6f, 3.0f) * M;
						Shape(TEXT("Cone"), Centre + FVector(Dice.FRandRange(-0.25f, 0.25f) * Tile, Dice.FRandRange(-0.25f, 0.25f) * Tile, Level + Tall * 0.45f),
							FVector(Tile * 0.35f, Tile * 0.35f, Tall), FRotator(Dice.FRandRange(-14.0f, 14.0f), Dice.FRandRange(0.0f, 360.0f), Dice.FRandRange(-14.0f, 14.0f)),
							Wander(Theme.Rock, Theme.Jitter * 2.0f, Seed + k));
					}
				}
				else
				{
					// Boulders: a big one and a smaller one leaning on it.
					Shape(TEXT("Sphere"), Centre + FVector(0, 0, Level + 0.9f * M), FVector(Tile * 0.95f, Tile * 0.85f, 2.1f * M),
						FRotator(0, Dice.FRandRange(0.0f, 360.0f), 0), Stone);
					Shape(TEXT("Sphere"), Centre + FVector(Dice.FRandRange(-0.3f, 0.3f) * Tile, Dice.FRandRange(-0.3f, 0.3f) * Tile, Level + 0.5f * M),
						FVector(Tile * 0.55f, Tile * 0.5f, 1.1f * M), FRotator(0, Dice.FRandRange(0.0f, 360.0f), 0), Stone * 0.85f);
				}
			}
			else
			{
				// Water: a bed below and the surface over it, lower than the banks.
				Shape(TEXT("Cube"), Centre + FVector(0, 0, 10.0f), FVector(Tile, Tile, 20.0f), FRotator::ZeroRotator, Theme.Water * 0.35f);
				UStaticMeshComponent* Surface = Shape(TEXT("Plane"), Centre + FVector(0, 0, Level * 0.62f), FVector(Tile, Tile, 100.0f),
					FRotator::ZeroRotator, Theme.Water);
				UMaterialInterface* Clear = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/Widget3DPassThrough_Translucent.Widget3DPassThrough_Translucent"));
				if (Surface && Clear)
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
					Surface->SetMaterial(0, Mid);
				}
				if (Theme.bWaterGlows && ((X + Y) % 2 == 0))
				{
					UPointLightComponent* Light = NewObject<UPointLightComponent>(this, NAME_None, RF_Transient);
					Light->SetupAttachment(RootComponent);
					Light->RegisterComponent();
					Light->SetRelativeLocation(Centre + FVector(0, 0, Level + 40.0f));
					Light->SetLightColor(Theme.Water);
					Light->SetIntensity(7000.0f);
					Light->SetAttenuationRadius(420.0f);
					Light->SetCastShadows(false);
					BoardProps.Add(Light);
					BoardLights.Add(Light);
					BoardLightBase.Add(Light->Intensity);
				}
			}
		}
	}

	// The land around the board: flat ground far out, with trees and rocks on
	// it -- never close enough to hide the board from the camera.
	const FVector2D Board(Map.TilesX * Tile, Map.TilesY * Tile);
	const float Margin = 70.0f * M;
	Shape(TEXT("Cube"), FVector(Board.X * 0.5f, Board.Y * 0.5f, -25.0f), FVector(Board.X + 2.0f * Margin, Board.Y + 2.0f * Margin, 50.0f),
		FRotator::ZeroRotator, Theme.Outside);
	auto OutsideSpot = [&](float Near, float Far)
	{
		for (int32 Try = 0; Try < 30; ++Try)
		{
			const FVector2D Spot(Dice.FRandRange(-Far, Board.X + Far), Dice.FRandRange(-Far, Board.Y + Far));
			const double OffX = FMath::Max3(-Spot.X, Spot.X - Board.X, 0.0);
			const double OffY = FMath::Max3(-Spot.Y, Spot.Y - Board.Y, 0.0);
			// The camera starts at the low corner, looking across: on those two
			// sides things stand further back so they never stand in front of the board.
			const bool bCameraSide = Spot.X < 0.0 || Spot.Y < 0.0;
			if (FMath::Max(OffX, OffY) >= (bCameraSide ? Near * 3.0f : Near))
			{
				return Spot;
			}
		}
		return FVector2D(-Far, -Far);
	};
	if (Theme.Trees != TEXT("none"))
	{
		for (int32 i = 0; i < Theme.TreeCount; ++i)
		{
			const FVector2D Spot = OutsideSpot(4.0f * M, 40.0f * M);
			const float Scale = Dice.FRandRange(0.8f, 1.5f);
			const FVector Foot(Spot.X, Spot.Y, 0.0f);
			const FLinearColor Leaves = Wander(Theme.Leaves, 0.06f, i);
			if (Theme.KitTree.Num() > 0
				&& KitPiece(Theme.KitTree[Dice.RandHelper(Theme.KitTree.Num())], Foot, 0.0f, Theme.KitTreeHeight * M * Scale * 0.8f,
					Dice.FRandRange(0.0f, 360.0f), false))
			{
				continue;
			}
			Shape(TEXT("Cylinder"), Foot + FVector(0, 0, 70.0f * Scale), FVector(28.0f, 28.0f, 140.0f) * Scale, FRotator::ZeroRotator, Theme.Trunk);
			if (Theme.Trees == TEXT("pine"))
			{
				Shape(TEXT("Cone"), Foot + FVector(0, 0, 230.0f * Scale), FVector(200.0f, 200.0f, 260.0f) * Scale, FRotator::ZeroRotator, Leaves);
				Shape(TEXT("Cone"), Foot + FVector(0, 0, 350.0f * Scale), FVector(140.0f, 140.0f, 200.0f) * Scale, FRotator::ZeroRotator, Leaves * 1.08f);
			}
			else if (Theme.Trees == TEXT("round"))
			{
				Shape(TEXT("Sphere"), Foot + FVector(0, 0, 260.0f * Scale), FVector(260.0f, 260.0f, 230.0f) * Scale, FRotator::ZeroRotator, Leaves);
			}
			else
			{
				// Dead: a bare trunk with two broken limbs.
				Shape(TEXT("Cylinder"), Foot + FVector(0, 0, 230.0f * Scale), FVector(20.0f, 20.0f, 180.0f) * Scale, FRotator::ZeroRotator, Theme.Trunk);
				Shape(TEXT("Cylinder"), Foot + FVector(30, 0, 250.0f * Scale), FVector(12.0f, 12.0f, 120.0f) * Scale, FRotator(0, Dice.FRandRange(0.0f, 360.0f), 40.0f), Theme.Trunk);
				Shape(TEXT("Cylinder"), Foot + FVector(-25, 0, 200.0f * Scale), FVector(12.0f, 12.0f, 100.0f) * Scale, FRotator(0, Dice.FRandRange(0.0f, 360.0f), -35.0f), Theme.Trunk);
			}
		}
	}
	for (int32 i = 0; i < Theme.RockCount; ++i)
	{
		const FVector2D Spot = OutsideSpot(2.5f * M, 35.0f * M);
		const float Scale = Dice.FRandRange(0.6f, 2.2f);
		if (Theme.KitBoulder.Num() > 0
			&& KitPiece(Theme.KitBoulder[Dice.RandHelper(Theme.KitBoulder.Num())], FVector(Spot.X, Spot.Y, 0.0f),
				Theme.KitBoulderSize * M * Scale * 0.6f, 0.0f, Dice.FRandRange(0.0f, 360.0f), false))
		{
			continue;
		}
		Shape(TEXT("Sphere"), FVector(Spot.X, Spot.Y, 20.0f * Scale), FVector(160.0f, 130.0f, 110.0f) * Scale,
			FRotator(0, Dice.FRandRange(0.0f, 360.0f), 0), Wander(Theme.Rock, Theme.Jitter, 5000 + i));
	}

	ApplyThemeLighting();
	UE_LOG(LogTemp, Log, TEXT("Board built: %d by %d tiles, %d pieces, dressed as %s"), Map.TilesX, Map.TilesY, BoardProps.Num(), *Theme.Id);
}

void ATMBattleDirector::ApplyThemeLighting()
{
	// Only while playing: in the editor this would change the level's own
	// lights, and the level is the human's to set.
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return;
	}
	const FTMTheme& Theme = ActiveTheme();
	for (TActorIterator<ADirectionalLight> It(World); It; ++It)
	{
		if (UDirectionalLightComponent* Sun = Cast<UDirectionalLightComponent>(It->GetLightComponent()))
		{
			Sun->SetMobility(EComponentMobility::Movable);
			It->SetActorRotation(FRotator(Theme.SunPitch, Theme.SunYaw, 0.0f));
			Sun->SetIntensity(Theme.SunIntensity);
			Sun->SetLightColor(Theme.SunColour);
		}
	}
	for (TActorIterator<ASkyLight> It(World); It; ++It)
	{
		if (USkyLightComponent* Sky = It->GetLightComponent())
		{
			Sky->SetIntensity(Theme.SkyIntensity);
			Sky->RecaptureSky();
		}
	}
	for (TActorIterator<AExponentialHeightFog> It(World); It; ++It)
	{
		if (UExponentialHeightFogComponent* Fog = It->GetComponent())
		{
			Fog->SetFogDensity(Theme.FogDensity);
			Fog->SetFogInscatteringColor(Theme.FogColour);
		}
	}
}

void ATMBattleDirector::AdvanceBoard(float DeltaSeconds)
{
	// Embers flicker, springs and lava breathe; each light on its own beat.
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	for (int32 i = 0; i < BoardLights.Num(); ++i)
	{
		if (UPointLightComponent* Light = BoardLights[i])
		{
			const float Beat = FMath::Sin(Now * (3.0f + (i % 5)) + i * 1.7f) * FMath::Sin(Now * 7.3f + i);
			Light->SetIntensity(BoardLightBase[i] * (0.8f + 0.25f * Beat));
		}
	}
	(void)DeltaSeconds;
}
