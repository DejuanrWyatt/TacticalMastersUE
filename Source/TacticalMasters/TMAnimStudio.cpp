#include "TMAnimStudio.h"

#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "TextureResource.h"
#include "TMBattleDirector.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#endif

#include "SimAbility.h"

namespace
{
	/** Far above the board, as the effects studio does, with only the body drawn. */
	const FVector Stage(0.0, 20000.0, 50000.0);

	FString Escaped(const FString& Text)
	{
		return Text.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\""));
	}

	FString Quoted(const FString& Text)
	{
		return Text.IsEmpty() ? FString(TEXT("null")) : TEXT("\"") + Escaped(Text) + TEXT("\"");
	}

	/** "/Script/Engine.Skeleton'/Game/A/B.B'" or "/Game/A/B.B", as "/Game/A/B.B". */
	FString BarePath(const FString& Tagged)
	{
		int32 First = INDEX_NONE;
		int32 Last = INDEX_NONE;
		if (Tagged.FindChar(TEXT('\''), First) && Tagged.FindLastChar(TEXT('\''), Last) && Last > First)
		{
			return Tagged.Mid(First + 1, Last - First - 1);
		}
		return Tagged;
	}
}

const TArray<FString>& ATMAnimStudio::TrackNames()
{
	static const TArray<FString> Names = { TEXT("hand_r"), TEXT("hand_l"), TEXT("head"), TEXT("weapon"), TEXT("root"), TEXT("pelvis") };
	return Names;
}

FName ATMAnimStudio::PointOn(const FString& Track) const
{
	// The names Paragon heroes and the mannequins use; a weapon is whichever
	// weapon bone the skeleton has. A point a body lacks is written as null on
	// every frame, so the creator says so rather than guessing a place.
	TArray<FString> Candidates = { Track };
	if (Track == TEXT("weapon"))
	{
		Candidates = { TEXT("weapon_r"), TEXT("Weapon_R"), TEXT("weapon"), TEXT("weapon_l"), TEXT("Muzzle_01") };
	}
	else if (Track == TEXT("hand_r"))
	{
		Candidates.Add(TEXT("Hand_R"));
	}
	else if (Track == TEXT("hand_l"))
	{
		Candidates.Add(TEXT("Hand_L"));
	}
	else if (Track == TEXT("head"))
	{
		Candidates.Add(TEXT("Head"));
	}
	else if (Track == TEXT("pelvis"))
	{
		Candidates.Add(TEXT("Pelvis"));
	}
	for (const FString& Name : Candidates)
	{
		if (Actor && Actor->DoesSocketExist(FName(*Name)))
		{
			return FName(*Name);
		}
	}
	return NAME_None;
}

ATMAnimStudio::ATMAnimStudio()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

FString ATMAnimStudio::IdOf(const FString& Body, const UAnimSequence* Clip)
{
	return Clip ? Body + TEXT("/") + Clip->GetName() : FString();
}

void ATMAnimStudio::Begin(const ATMBattleDirector* InDirector)
{
	Director = InDirector;

	// Every frame is the same slice of the clip whatever this machine does: the
	// pose is set by hand to each sixteenth of a second, not left to play.
	FApp::SetUseFixedTimeStep(true);
	FApp::SetFixedDeltaTime(1.0 / FramesPerSecond);

	Film = NewObject<UTextureRenderTarget2D>(this);
	Film->RenderTargetFormat = RTF_RGBA8;
	Film->ClearColor = FLinearColor::Black;
	Film->InitAutoFormat(FrameSize, FrameSize);
	Film->UpdateResourceImmediate(true);

	Actor = NewObject<USkeletalMeshComponent>(this);
	Actor->SetupAttachment(RootComponent);
	Actor->RegisterComponent();
	Actor->SetWorldLocation(Stage);
	Actor->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	// Posed even though no camera the engine knows of is looking at it.
	Actor->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

	Camera = NewObject<USceneCaptureComponent2D>(this);
	Camera->SetupAttachment(RootComponent);
	Camera->RegisterComponent();
	Camera->TextureTarget = Film;
	Camera->bCaptureEveryFrame = false;
	Camera->bCaptureOnMovement = false;
	Camera->bAlwaysPersistRenderingState = true;
	Camera->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	Camera->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	Camera->ShowOnlyComponents.Add(Actor);
	Camera->ShowFlags.SetAtmosphere(false);
	Camera->ShowFlags.SetFog(false);
	Camera->ShowFlags.SetCloud(false);
	Camera->ShowFlags.SetMotionBlur(false);
	Camera->PostProcessSettings.bOverride_AutoExposureMethod = true;
	Camera->PostProcessSettings.AutoExposureMethod = AEM_Manual;
	Camera->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	Camera->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = false;
	Camera->PostProcessSettings.bOverride_AutoExposureBias = true;
	Camera->PostProcessSettings.AutoExposureBias = 0.0f;
	Camera->PostProcessBlendWeight = 1.0f;
	// From the front and a little to one side, a little above: how the game's
	// camera tends to see a unit, and enough of the side to read a swing.
	// The same for every body and clip, and written to the catalogue: the
	// creator places an effect on a strip from where this camera stands.
	Camera->FOVAngle = CameraFov;
	const FVector Look = Stage + FVector(0.0f, 0.0f, LookHeight);
	// -tmanimyaw=90 films from the body's right side instead, which is how to
	// tell a fall forwards from a fall backwards when choosing clips.
	FParse::Value(FCommandLine::Get(), TEXT("tmanimyaw="), Around);
	const FRotator Towards(CameraPitch, 180.0f + Around, 0.0f);
	Camera->SetWorldLocationAndRotation(Look - Towards.Vector() * CameraDistance, Towards);

	// A key light by the camera and a rim light behind, since far above the
	// level nothing else lights the body.
	const FVector Places[2] = { Camera->GetComponentLocation() + FVector(0.0f, 150.0f, 200.0f), Stage + FVector(-250.0f, -150.0f, 260.0f) };
	const float Strength[3] = { 16.0f, 9.0f, 6.0f };
	const FVector Fill = Camera->GetComponentLocation() + FVector(0.0f, -200.0f, -60.0f);
	for (int32 i = 0; i < 3; ++i)
	{
		UPointLightComponent* Lamp = NewObject<UPointLightComponent>(this);
		Lamp->SetupAttachment(RootComponent);
		Lamp->RegisterComponent();
		Lamp->SetMobility(EComponentMobility::Movable);
		Lamp->bUseInverseSquaredFalloff = false;
		Lamp->SetLightFalloffExponent(1.0f);
		Lamp->SetIntensity(Strength[i]);
		Lamp->SetAttenuationRadius(2000.0f);
		Lamp->SetCastShadows(false);
		Lamp->SetWorldLocation(i < 2 ? Places[i] : Fill);
		Lamps.Add(Lamp);
	}

	OutDir = FPaths::ProjectSavedDir() / TEXT("AnimCatalog");
	// -tmanimwanted=<file>: "<set> <clip path>" a line, the clips Cast Studio's
	// designer picked and has not seen filmed. Filmed into a list of their own
	// beside the catalogue, which stays as it is.
	TMap<FString, TArray<FString>> Wanted;
	FString WantedFile;
	if (FParse::Value(FCommandLine::Get(), TEXT("tmanimwanted="), WantedFile))
	{
		bWanted = true;
		TArray<FString> Lines;
		FFileHelper::LoadFileToStringArray(Lines, *WantedFile);
		for (const FString& Line : Lines)
		{
			FString SetName;
			FString Path;
			if (Line.TrimStartAndEnd().Split(TEXT(" "), &SetName, &Path) && !Path.IsEmpty())
			{
				Wanted.FindOrAdd(SetName).AddUnique(Path.TrimStartAndEnd());
				Only.Add(SetName);
			}
		}
		CatalogName = FString::Printf(TEXT("catalog-wanted-%lld.json"), FDateTime::UtcNow().GetTicks());
	}
	FString OnlyList;
	if (bWanted)
	{
		// Only the wanted clips' bodies, and nothing already filmed is touched.
	}
	else if (FParse::Value(FCommandLine::Get(), TEXT("tmanimonly="), OnlyList, false))
	{
		TArray<FString> Names;
		OnlyList.ParseIntoArray(Names, TEXT(","));
		for (const FString& Name : Names)
		{
			Only.Add(Name.TrimStartAndEnd());
		}
		// One batch of several: the others' films stay, and this one's list is its own.
		CatalogName = FString::Printf(TEXT("catalog-%s.json"), Names.Num() > 0 ? *Names[0] : TEXT("part"));
	}
	else
	{
		IFileManager::Get().DeleteDirectory(*OutDir, false, true);
	}
	IFileManager::Get().MakeDirectory(*OutDir, true);

	// Every clip each body's set names, once per body, in the order bodies are
	// named: two bodies that share a set are filmed apart, since they look apart.
	TArray<FString> Names;
	Director->Bodies.GetKeys(Names);
	Names.Sort();
	for (const FString& Name : Names)
	{
		const ATMBattleDirector::FTMBody& Body = Director->Bodies[Name];
		// A hero's skins play the hero's own clips: filming the hero once is enough.
		// (Loading a mesh and its clips only fills the director's cache, hence the const_cast.)
		if (Name.StartsWith(Body.SetName + TEXT("_")) || (Only.Num() > 0 && !Only.Contains(Body.SetName))
			|| !const_cast<ATMBattleDirector*>(&*Director)->MeshOf(Body))
		{
			continue;
		}
		const ATMBattleDirector::FTMAnimSet* Set = Body.Animations;
		if (!Set)
		{
			continue;
		}
		TArray<UAnimSequence*> Clips;
		auto Add = [&Clips](UAnimSequence* Clip) { if (Clip) { Clips.AddUnique(Clip); } };
		// A picked clip is filmed only on the skeleton it was made for.
		const USkeleton* Skeleton = Body.Mesh ? Body.Mesh->GetSkeleton() : nullptr;
		auto AddPath = [&Add, Skeleton](const FString& Path)
		{
			UAnimSequence* Clip = LoadObject<UAnimSequence>(nullptr, *Path);
			if (!Clip || Clip->IsValidAdditive() || (Skeleton && Clip->GetSkeleton() != Skeleton))
			{
				UE_LOG(LogTemp, Warning, TEXT("ANIM STUDIO: %s is not a clip this body can play; not filmed"), *Path);
				return;
			}
			Add(Clip);
		};
		if (bWanted)
		{
			for (const FString& Path : Wanted.FindRef(Body.SetName))
			{
				AddPath(Path);
			}
			for (UAnimSequence* Clip : Clips)
			{
				Jobs.Add({ Name, Body.Mesh, Body.Yaw, Clip, Body.Scale, Set->bCloth });
			}
			continue;
		}
		// What Cast Studio's published picks name for this set (AbilityAnimation.json).
		for (const auto& Ability : Director->CastAnimation.Abilities)
		{
			const auto Picks = Ability.second.find(TCHAR_TO_UTF8(*Body.SetName));
			if (Picks != Ability.second.end())
			{
				for (const TMCast::FClipPick* Pick : { &Picks->second.Windup, &Picks->second.Release, &Picks->second.Loop, &Picks->second.Recover })
				{
					if (!Pick->Clip.empty())
					{
						AddPath(UTF8_TO_TCHAR(Pick->Clip.c_str()));
					}
				}
			}
		}
		Add(Set->Idle);
		Add(Set->Walk);
		Add(Set->Run);
		Add(Set->Cast);
		Add(Set->Rise);
		for (UAnimSequence* Clip : Set->Attack) { Add(Clip); }
		for (UAnimSequence* Clip : Set->Hit) { Add(Clip); }
		for (UAnimSequence* Clip : Set->Death) { Add(Clip); }
		for (const TPair<FString, ATMBattleDirector::FTMMotionClips>& Motion : Set->Motions)
		{
			for (UAnimSequence* Clip : Motion.Value.Release) { Add(Clip); }
			Add(Motion.Value.Intro);
			Add(Motion.Value.Windup);
			Add(Motion.Value.CastRelease);
		}
		for (const TPair<FString, TArray<UAnimSequence*>>& Extra : Set->Extras)
		{
			for (UAnimSequence* Clip : Extra.Value) { Add(Clip); }
		}
		for (UAnimSequence* Clip : Clips)
		{
			Jobs.Add({ Name, Body.Mesh, Body.Yaw, Clip, Body.Scale, Set->bCloth });
		}
	}
	bDone = false;
	UE_LOG(LogTemp, Log, TEXT("ANIM STUDIO: %d clips to film on %d bodies"), Jobs.Num(), Names.Num());
}

void ATMAnimStudio::Measure()
{
	for (const FName& On : TrackOn)
	{
		Points.Add(On.IsNone() ? FVector::ZeroVector : Actor->GetSocketLocation(On) - Stage);
	}
}

void ATMAnimStudio::SaveTracks()
{
	// <sheet>.tracks.json, read by the creator when it shows the clip: where
	// each point is on each frame, in centimetres from where the body stands
	// (x forward, the way it faces; z up), seen by the catalogue's camera.
	FFilmed& Entry = Filmed.Last();
	const TArray<FString>& Names = TrackNames();
	const int32 Count = Names.Num();
	const int32 Frames = Count > 0 ? Points.Num() / Count : 0;
	FString Json = FString::Printf(TEXT("{\"fps\": %d, \"frames\": %d, \"tracks\": {"), FramesPerSecond, Frames);
	for (int32 k = 0; k < Count; ++k)
	{
		const bool bHas = TrackOn.IsValidIndex(k) && !TrackOn[k].IsNone();
		Json += FString::Printf(TEXT("%s\n  \"%s\": {\"on\": %s, \"points\": "), k ? TEXT(",") : TEXT(""), *Names[k],
			*Quoted(bHas ? TrackOn[k].ToString() : FString()));
		if (!bHas)
		{
			Json += TEXT("null}");
			continue;
		}
		Json += TEXT("[");
		for (int32 f = 0; f < Frames; ++f)
		{
			const FVector& P = Points[f * Count + k];
			Json += FString::Printf(TEXT("%s[%.1f,%.1f,%.1f]"), f ? TEXT(",") : TEXT(""), P.X, P.Y, P.Z);
		}
		Json += TEXT("]}");
	}
	Json += TEXT("\n}}\n");
	FFileHelper::SaveStringToFile(Json, *(OutDir / Entry.Tracks), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

	// How far the root wanders over the ground: a clip that walks off its spot
	// slides in a battle, where units are placed by the rules, not the clip.
	const int32 Root = Names.IndexOfByKey(TEXT("root"));
	if (Root != INDEX_NONE && TrackOn.IsValidIndex(Root) && !TrackOn[Root].IsNone() && Frames > 0)
	{
		const FVector Start = Points[Root];
		for (int32 f = 0; f < Frames; ++f)
		{
			const float Away = static_cast<float>(FVector::Dist2D(Points[f * Count + Root], Start));
			Entry.MostDrift = FMath::Max(Entry.MostDrift, Away);
			Entry.Drift = Away;
		}
	}
}

void ATMAnimStudio::Shoot(TArray<FColor>& Pixels)
{
	Camera->CaptureScene();
	FTextureRenderTargetResource* Resource = Film->GameThread_GetRenderTargetResource();
	if (!Resource || !Resource->ReadPixels(Pixels) || Pixels.Num() != FrameSize * FrameSize)
	{
		Pixels.Reset();
	}
}

void ATMAnimStudio::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bDone)
	{
		return;
	}

	// A pose set now is evaluated, and its bones sent to be drawn, over the
	// next frames rather than at once: filmed straight away, every picture
	// showed the pose before it. So each pose is given a few frames to settle.
	if (Settle > 0)
	{
		--Settle;
		return;
	}
	if (Index >= 0 && Frame >= 0)
	{
		// The pose set a few ticks ago has been drawn since: take its picture.
		TArray<FColor> Pixels;
		Shoot(Pixels);
		Measure();
		// Sixteen frames to a row, so a three-second clip is a picture a
		// browser can hold, not one 24,000 pixels wide.
		const int32 Across = FrameSize * FMath::Min(FrameCount, Columns);
		const int32 Left = (Frame % Columns) * FrameSize;
		const int32 Top = (Frame / Columns) * FrameSize;
		for (int32 Y = 0; Y < FrameSize && Pixels.Num() > 0; ++Y)
		{
			for (int32 X = 0; X < FrameSize; ++X)
			{
				FColor Pixel = Pixels[Y * FrameSize + X];
				Pixel.A = 255;
				Strip[(Top + Y) * Across + Left + X] = Pixel;
			}
		}
		if (Frame + 1 >= FrameCount)
		{
			SaveStrip();
			Frame = -1;
		}
	}
	Settle = SettleTicks;

	if (Frame < 0)
	{
		++Index;
		if (Index >= Jobs.Num())
		{
			WriteCatalogue();
			UE_LOG(LogTemp, Log, TEXT("ANIM STUDIO DONE: %d of %d clips filmed -> %s"), Filmed.Num(), Jobs.Num(), *OutDir);
			bDone = true;
			FPlatformMisc::RequestExit(false);
			return;
		}
		const FJob& Job = Jobs[Index];
		if (Actor->GetSkeletalMeshAsset() != Job.Mesh)
		{
			Actor->SetSkeletalMeshAsset(Job.Mesh);
			Actor->SetWorldRotation(FRotator(0.0f, Job.Yaw, 0.0f));
			Actor->SetWorldScale3D(FVector(Job.Scale));
#if WITH_EDITOR
			// A mesh's materials may still be compiling; filmed now, it would be
			// the grey checkerboard.
			FAssetCompilingManager::Get().FinishAllCompilation();
			if (GShaderCompilingManager)
			{
				GShaderCompilingManager->FinishAllCompilation();
			}
#endif
		}
		Actor->PlayAnimation(Job.Clip, false);
		Actor->SetPlayRate(0.0f);
		TrackOn.Reset();
		for (const FString& Track : TrackNames())
		{
			TrackOn.Add(PointOn(Track));
		}
		// Cloth as the game draws it; -tmnocloth films every body with none, to
		// see which heroes' cloth flies apart (Terra's did: "cloth": false).
		Actor->bDisableClothSimulation = !Job.bCloth || FParse::Param(FCommandLine::Get(), TEXT("tmnocloth"));
		const float Length = Job.Clip->GetPlayLength();
		FrameCount = FMath::Clamp(FMath::CeilToInt(Length * FramesPerSecond) + 1, 2, MaxFrames);
		const int32 Rows = (FrameCount + Columns - 1) / Columns;
		Strip.SetNumZeroed(FrameSize * FMath::Min(FrameCount, Columns) * FrameSize * Rows);
		Points.Reset();
		FFilmed& Entry = Filmed.AddDefaulted_GetRef();
		Entry.Id = IdOf(Job.Body, Job.Clip);
		Entry.Body = Job.Body;
		Entry.Path = FSoftObjectPath(Job.Clip).ToString();
		Entry.Name = Job.Clip->GetName();
		Entry.Sheet = Job.Body + TEXT("__") + Entry.Name + TEXT(".png");
		Entry.Tracks = Job.Body + TEXT("__") + Entry.Name + TEXT(".tracks.json");
		Entry.Frames = FrameCount;
		Entry.Seconds = Length;
	}
	++Frame;
	Actor->SetPosition(FMath::Min(Frame / static_cast<float>(FramesPerSecond), Jobs[Index].Clip->GetPlayLength()), false);
}

void ATMAnimStudio::SaveStrip()
{
	FFilmed& Entry = Filmed.Last();
	const int32 Across = FMath::Min(FrameCount, Columns);
	const int32 Rows = (FrameCount + Columns - 1) / Columns;
	const FImageView Image(Strip.GetData(), FrameSize * Across, FrameSize * Rows);
	if (!FImageUtils::SaveImageByExtension(*(OutDir / Entry.Sheet), Image))
	{
		UE_LOG(LogTemp, Warning, TEXT("ANIM STUDIO: could not save %s"), *Entry.Sheet);
		Filmed.Pop();
		return;
	}
	// The small copy: every other frame, in one row, for the creator's lists.
	const int32 ThumbFrames = (FrameCount + ThumbEvery - 1) / ThumbEvery;
	TArray<FColor> Small;
	Small.SetNumZeroed(ThumbSize * ThumbFrames * ThumbSize);
	TArray<FColor> One;
	One.SetNumUninitialized(FrameSize * FrameSize);
	TArray<FColor> Shrunk;
	Shrunk.SetNumUninitialized(ThumbSize * ThumbSize);
	for (int32 t = 0; t < ThumbFrames; ++t)
	{
		const int32 Source = t * ThumbEvery;
		const int32 Left = (Source % Columns) * FrameSize;
		const int32 Top = (Source / Columns) * FrameSize;
		for (int32 Y = 0; Y < FrameSize; ++Y)
		{
			FMemory::Memcpy(&One[Y * FrameSize], &Strip[(Top + Y) * FrameSize * Across + Left], FrameSize * sizeof(FColor));
		}
		FImageUtils::ImageResize(FrameSize, FrameSize, TArrayView<const FColor>(One), ThumbSize, ThumbSize, TArrayView<FColor>(Shrunk), false, true);
		for (int32 Y = 0; Y < ThumbSize; ++Y)
		{
			FMemory::Memcpy(&Small[Y * ThumbSize * ThumbFrames + t * ThumbSize], &Shrunk[Y * ThumbSize], ThumbSize * sizeof(FColor));
		}
	}
	Entry.Thumb = FPaths::GetBaseFilename(Entry.Sheet) + TEXT(".thumb.png");
	FImageUtils::SaveImageByExtension(*(OutDir / Entry.Thumb), FImageView(Small.GetData(), ThumbSize * ThumbFrames, ThumbSize));
	SaveTracks();
	UE_LOG(LogTemp, Log, TEXT("ANIM STUDIO: %d/%d %s (%d frames)"), Index + 1, Jobs.Num(), *Entry.Id, Entry.Frames);
}

void ATMAnimStudio::WriteCatalogue() const
{
	// Read by the class creator (app/anim.mjs there). For each body, what each
	// motion plays -- after the stand-ins a battle uses where its set has
	// nothing for that motion -- so the creator shows exactly what the game will.
	TSet<FString> Have;
	for (const FFilmed& Entry : Filmed)
	{
		Have.Add(Entry.Id);
	}
	auto One = [&Have](const FString& Body, const UAnimSequence* Clip)
	{
		const FString Id = IdOf(Body, Clip);
		return Quoted(Have.Contains(Id) ? Id : FString());
	};
	auto Many = [&Have](const FString& Body, const TArray<UAnimSequence*>& Clips)
	{
		FString Out = TEXT("[");
		int32 Count = 0;
		for (const UAnimSequence* Clip : Clips)
		{
			const FString Id = IdOf(Body, Clip);
			if (Have.Contains(Id))
			{
				Out += (Count++ ? TEXT(", ") : TEXT("")) + Quoted(Id);
			}
		}
		return Out + TEXT("]");
	};

	// Version 2 (Cast Studio): 32 frames a second, 16 to a row; the camera,
	// fixed for every body, so a point in centimetres can be found on a strip;
	// each clip's tracks file and root drift; each body's skeleton and library.
	FString Json = FString::Printf(
		TEXT("{\n  \"format\": \"tactical-masters-anim-catalog\",\n  \"version\": 2,\n")
		TEXT("  \"made\": \"%s\",\n  \"frameSize\": %d,\n  \"thumbSize\": %d,\n  \"fps\": %d,\n  \"columns\": %d,\n  \"thumbEvery\": %d,\n")
		TEXT("  \"camera\": {\"fov\": %.1f, \"pitch\": %.1f, \"yaw\": %.1f, \"distance\": %.1f, \"look\": [0, 0, %.1f], \"forward\": [1, 0, 0]},\n  \"clips\": ["),
		*FDateTime::UtcNow().ToIso8601(), FrameSize, ThumbSize, FramesPerSecond, Columns, ThumbEvery,
		CameraFov, CameraPitch, 180.0f + Around, CameraDistance, LookHeight);
	for (int32 i = 0; i < Filmed.Num(); ++i)
	{
		const FFilmed& Entry = Filmed[i];
		Json += FString::Printf(
			TEXT("%s\n    {\"id\": \"%s\", \"body\": \"%s\", \"path\": \"%s\", \"name\": \"%s\", \"sheet\": \"%s\", \"thumb\": \"%s\", \"tracks\": \"%s\", \"frames\": %d, \"seconds\": %.3f, \"drift\": %.1f, \"mostDrift\": %.1f}"),
			i ? TEXT(",") : TEXT(""), *Escaped(Entry.Id), *Escaped(Entry.Body), *Escaped(Entry.Path), *Escaped(Entry.Name),
			*Escaped(Entry.Sheet), *Escaped(Entry.Thumb), *Escaped(Entry.Tracks), Entry.Frames, Entry.Seconds, Entry.Drift, Entry.MostDrift);
	}
	if (bWanted)
	{
		// Only what was asked for; the class creator adds these to the catalogue.
		Json += TEXT("\n  ]\n}\n");
		FFileHelper::SaveStringToFile(Json, *(OutDir / CatalogName), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		return;
	}
	Json += TEXT("\n  ],\n  \"bodies\": [");

	// Every clip in the project, by the skeleton it was made for: a body's
	// library is what Cast Studio can offer it (a clip on another skeleton
	// would bend it into nonsense). Additive clips, made to be layered, are left out.
	TMap<FString, TArray<FAssetData>> BySkeleton;
	{
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		Registry.SearchAllAssets(true);
		FARFilter Filter;
		Filter.PackagePaths.Add(FName(TEXT("/Game")));
		Filter.bRecursivePaths = true;
		Filter.ClassPaths.Add(UAnimSequence::StaticClass()->GetClassPathName());
		TArray<FAssetData> Found;
		Registry.GetAssets(Filter, Found);
		for (const FAssetData& Asset : Found)
		{
			FString SkeletonTag;
			FString Additive;
			if (!Asset.GetTagValue(FName(TEXT("Skeleton")), SkeletonTag))
			{
				continue;
			}
			if (Asset.GetTagValue(FName(TEXT("AdditiveAnimType")), Additive) && !Additive.IsEmpty() && Additive != TEXT("AAT_None"))
			{
				continue;
			}
			BySkeleton.FindOrAdd(BarePath(SkeletonTag)).Add(Asset);
		}
	}

	TArray<FString> Names;
	Director->Bodies.GetKeys(Names);
	Names.Sort();
	int32 Written = 0;
	for (const FString& Name : Names)
	{
		const ATMBattleDirector::FTMBody& Body = Director->Bodies[Name];
		const ATMBattleDirector::FTMAnimSet* Set = Body.Animations;
		if (!Set)
		{
			continue;
		}
		FString SetName;
		for (const TPair<FString, ATMBattleDirector::FTMAnimSet>& Each : Director->AnimSets)
		{
			if (&Each.Value == Set)
			{
				SetName = Each.Key;
			}
		}
		const FString SkeletonPath = Body.Mesh && Body.Mesh->GetSkeleton() ? FSoftObjectPath(Body.Mesh->GetSkeleton()).ToString() : FString();
		FString Library = TEXT("[");
		if (const TArray<FAssetData>* Own = BySkeleton.Find(SkeletonPath))
		{
			TArray<FAssetData> Sorted = *Own;
			Sorted.Sort([](const FAssetData& A, const FAssetData& B) { return A.AssetName.LexicalLess(B.AssetName); });
			for (int32 k = 0; k < Sorted.Num(); ++k)
			{
				Library += FString::Printf(TEXT("%s\n        {\"path\": \"%s\", \"name\": \"%s\"}"), k ? TEXT(",") : TEXT(""),
					*Escaped(Sorted[k].GetObjectPathString()), *Escaped(Sorted[k].AssetName.ToString()));
			}
		}
		Library += TEXT("]");
		Json += FString::Printf(TEXT("%s\n    {\"name\": \"%s\", \"set\": \"%s\", \"skeleton\": %s, \"idle\": %s, \"walk\": %s, \"run\": %s, \"hit\": %s, \"death\": %s, \"rise\": %s,\n      \"library\": %s,\n      \"motions\": {"),
			Written++ ? TEXT(",") : TEXT(""), *Escaped(Name), *Escaped(SetName), *Quoted(SkeletonPath), *One(Name, Set->Idle), *One(Name, Set->Walk),
			*One(Name, Set->Run), *Many(Name, Set->Hit), *Many(Name, Set->Death), *One(Name, Set->Rise), *Library);
		// Every motion, as a battle would play it (AnimateEvents): its own clips,
		// a near motion's, or at last the set's attack or cast.
		int32 MotionCount = 0;
		for (const std::string& Each : TMSim::AnimMotions())
		{
			const FString Motion = UTF8_TO_TCHAR(Each.c_str());
			if (Motion == TEXT("none"))
			{
				continue;
			}
			FString From = Motion;
			TArray<UAnimSequence*> Release;
			UAnimSequence* Intro = nullptr;
			UAnimSequence* Windup = nullptr;
			UAnimSequence* CastRelease = nullptr;
			float Impact = -1.0f;
			if (const ATMBattleDirector::FTMMotionClips* Clips = ATMBattleDirector::FindMotion(*Set, Motion))
			{
				for (const TPair<FString, ATMBattleDirector::FTMMotionClips>& Named : Set->Motions)
				{
					if (&Named.Value == Clips)
					{
						From = Named.Key;
					}
				}
				Release = Clips->Release;
				Intro = Clips->Intro;
				Windup = Clips->Windup;
				CastRelease = Clips->CastRelease;
				Impact = Clips->Impact;
			}
			if (Release.Num() == 0)
			{
				const bool bWeapon = Motion == TEXT("melee") || Motion == TEXT("heavy") || Motion == TEXT("dash") || Motion == TEXT("shoot");
				if (bWeapon && Set->Attack.Num() > 0)
				{
					Release = Set->Attack;
					From = TEXT("attack");
				}
				else if (Set->Cast)
				{
					Release = { Set->Cast };
					From = TEXT("cast");
				}
			}
			// "impact": the set's contact for the motion, or null for the motion's usual (TMBattleDirectorBlows.cpp).
			Json += FString::Printf(TEXT("%s\n        \"%s\": {\"from\": \"%s\", \"release\": %s, \"intro\": %s, \"windup\": %s, \"castRelease\": %s, \"impact\": %s}"),
				MotionCount++ ? TEXT(",") : TEXT(""), *Escaped(Motion), *Escaped(From), *Many(Name, Release),
				*One(Name, Intro), *One(Name, Windup), *One(Name, CastRelease),
				Impact >= 0.0f ? *FString::Printf(TEXT("%.3f"), Impact) : TEXT("null"));
		}
		Json += TEXT("\n      },\n      \"extras\": {");
		TArray<FString> Keys;
		Set->Extras.GetKeys(Keys);
		Keys.Sort();
		for (int32 k = 0; k < Keys.Num(); ++k)
		{
			Json += FString::Printf(TEXT("%s\"%s\": %s"), k ? TEXT(", ") : TEXT(""), *Escaped(Keys[k]), *Many(Name, Set->Extras[Keys[k]]));
		}
		Json += TEXT("}}");
	}
	Json += TEXT("\n  ],\n");

	// Which body each class wears, as BodyFor reads it: its own, its look's, the default.
	auto Names2 = [](const TMap<FString, FString>& Map)
	{
		TArray<FString> Keys;
		Map.GetKeys(Keys);
		Keys.Sort();
		FString Out = TEXT("{");
		for (int32 k = 0; k < Keys.Num(); ++k)
		{
			Out += FString::Printf(TEXT("%s\"%s\": \"%s\""), k ? TEXT(", ") : TEXT(""), *Escaped(Keys[k]), *Escaped(Map[Keys[k]]));
		}
		return Out + TEXT("}");
	};
	Json += FString::Printf(TEXT("  \"looks\": %s,\n  \"classes\": %s,\n  \"default\": \"%s\"\n}\n"),
		*Names2(Director->LookBodies), *Names2(Director->ClassBodies), *Escaped(Director->DefaultBody));
	FFileHelper::SaveStringToFile(Json, *(OutDir / CatalogName), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
