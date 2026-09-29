#include "TMAnimStudio.h"

#include "Animation/AnimSequence.h"
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
	Camera->FOVAngle = 40.0f;
	const FVector Look = Stage + FVector(0.0f, 0.0f, 95.0f);
	// -tmanimyaw=90 films from the body's right side instead, which is how to
	// tell a fall forwards from a fall backwards when choosing clips.
	float Around = 30.0f;
	FParse::Value(FCommandLine::Get(), TEXT("tmanimyaw="), Around);
	const FRotator Towards(-10.0f, 180.0f + Around, 0.0f);
	Camera->SetWorldLocationAndRotation(Look - Towards.Vector() * 460.0f, Towards);

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
	IFileManager::Get().DeleteDirectory(*OutDir, false, true);
	IFileManager::Get().MakeDirectory(*OutDir, true);

	// Every clip each body's set names, once per body, in the order bodies are
	// named: two bodies that share a set are filmed apart, since they look apart.
	TArray<FString> Names;
	Director->Bodies.GetKeys(Names);
	Names.Sort();
	for (const FString& Name : Names)
	{
		const ATMBattleDirector::FTMBody& Body = Director->Bodies[Name];
		const ATMBattleDirector::FTMAnimSet* Set = Body.Animations;
		// A hero's skins play the hero's own clips: filming the hero once is enough.
		// (Loading a mesh only fills the director's cache, hence the const_cast.)
		if (!Set || Name.StartsWith(Body.SetName + TEXT("_")) || !const_cast<ATMBattleDirector*>(&*Director)->MeshOf(Body))
		{
			continue;
		}
		TArray<UAnimSequence*> Clips;
		auto Add = [&Clips](UAnimSequence* Clip) { if (Clip) { Clips.AddUnique(Clip); } };
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
			Jobs.Add({ Name, Body.Mesh, Body.Yaw, Clip });
		}
	}
	bDone = false;
	UE_LOG(LogTemp, Log, TEXT("ANIM STUDIO: %d clips to film on %d bodies"), Jobs.Num(), Names.Num());
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
		const int32 Across = FrameSize * FrameCount;
		for (int32 Y = 0; Y < FrameSize && Pixels.Num() > 0; ++Y)
		{
			for (int32 X = 0; X < FrameSize; ++X)
			{
				FColor Pixel = Pixels[Y * FrameSize + X];
				Pixel.A = 255;
				Strip[Y * Across + Frame * FrameSize + X] = Pixel;
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
		const float Length = Job.Clip->GetPlayLength();
		FrameCount = FMath::Clamp(FMath::CeilToInt(Length * FramesPerSecond) + 1, 2, MaxFrames);
		Strip.SetNumZeroed(FrameSize * FrameSize * FrameCount);
		FFilmed& Entry = Filmed.AddDefaulted_GetRef();
		Entry.Id = IdOf(Job.Body, Job.Clip);
		Entry.Body = Job.Body;
		Entry.Path = FSoftObjectPath(Job.Clip).ToString();
		Entry.Name = Job.Clip->GetName();
		Entry.Sheet = Job.Body + TEXT("__") + Entry.Name + TEXT(".png");
		Entry.Frames = FrameCount;
		Entry.Seconds = Length;
	}
	++Frame;
	Actor->SetPosition(FMath::Min(Frame / static_cast<float>(FramesPerSecond), Jobs[Index].Clip->GetPlayLength()), false);
}

void ATMAnimStudio::SaveStrip()
{
	FFilmed& Entry = Filmed.Last();
	const FImageView Image(Strip.GetData(), FrameSize * FrameCount, FrameSize);
	if (!FImageUtils::SaveImageByExtension(*(OutDir / Entry.Sheet), Image))
	{
		UE_LOG(LogTemp, Warning, TEXT("ANIM STUDIO: could not save %s"), *Entry.Sheet);
		Filmed.Pop();
		return;
	}
	TArray<FColor> Small;
	Small.SetNumUninitialized(ThumbSize * FrameCount * ThumbSize);
	FImageUtils::ImageResize(FrameSize * FrameCount, FrameSize, TArrayView<const FColor>(Strip),
		ThumbSize * FrameCount, ThumbSize, TArrayView<FColor>(Small), false, true);
	Entry.Thumb = FPaths::GetBaseFilename(Entry.Sheet) + TEXT(".thumb.png");
	FImageUtils::SaveImageByExtension(*(OutDir / Entry.Thumb), FImageView(Small.GetData(), ThumbSize * FrameCount, ThumbSize));
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

	FString Json = FString::Printf(
		TEXT("{\n  \"format\": \"tactical-masters-anim-catalog\",\n  \"version\": 1,\n")
		TEXT("  \"made\": \"%s\",\n  \"frameSize\": %d,\n  \"thumbSize\": %d,\n  \"fps\": %d,\n  \"clips\": ["),
		*FDateTime::UtcNow().ToIso8601(), FrameSize, ThumbSize, FramesPerSecond);
	for (int32 i = 0; i < Filmed.Num(); ++i)
	{
		const FFilmed& Entry = Filmed[i];
		Json += FString::Printf(
			TEXT("%s\n    {\"id\": \"%s\", \"body\": \"%s\", \"path\": \"%s\", \"name\": \"%s\", \"sheet\": \"%s\", \"thumb\": \"%s\", \"frames\": %d, \"seconds\": %.3f}"),
			i ? TEXT(",") : TEXT(""), *Escaped(Entry.Id), *Escaped(Entry.Body), *Escaped(Entry.Path), *Escaped(Entry.Name),
			*Escaped(Entry.Sheet), *Escaped(Entry.Thumb), Entry.Frames, Entry.Seconds);
	}
	Json += TEXT("\n  ],\n  \"bodies\": [");

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
		Json += FString::Printf(TEXT("%s\n    {\"name\": \"%s\", \"set\": \"%s\", \"idle\": %s, \"walk\": %s, \"run\": %s, \"hit\": %s, \"death\": %s, \"rise\": %s,\n      \"motions\": {"),
			Written++ ? TEXT(",") : TEXT(""), *Escaped(Name), *Escaped(SetName), *One(Name, Set->Idle), *One(Name, Set->Walk),
			*One(Name, Set->Run), *Many(Name, Set->Hit), *Many(Name, Set->Death), *One(Name, Set->Rise));
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
			Json += FString::Printf(TEXT("%s\n        \"%s\": {\"from\": \"%s\", \"release\": %s, \"intro\": %s, \"windup\": %s, \"castRelease\": %s}"),
				MotionCount++ ? TEXT(",") : TEXT(""), *Escaped(Motion), *Escaped(From), *Many(Name, Release),
				*One(Name, Intro), *One(Name, Windup), *One(Name, CastRelease));
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
	FFileHelper::SaveStringToFile(Json, *(OutDir / TEXT("catalog.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
