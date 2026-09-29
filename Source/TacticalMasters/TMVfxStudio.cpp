#include "TMVfxStudio.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "TextureResource.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#endif

namespace
{
	/**
	 * Where the effects are played: far above the board, so nothing an effect
	 * throws about can land on it. Only the effect is drawn (see BeginPlay), so the
	 * board would not show even if it were in view.
	 */
	const FVector Stage(0.0, 0.0, 50000.0);

	FString Escaped(const FString& Text)
	{
		return Text.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\""));
	}
}

ATMVfxStudio::ATMVfxStudio()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ATMVfxStudio::BeginPlay()
{
	Super::BeginPlay();

	// Every frame of every strip is the same slice of time, however fast this
	// machine draws: the engine is told each frame lasts exactly one frame of
	// film, and a frame is taken every tick.
	FApp::SetUseFixedTimeStep(true);
	FApp::SetFixedDeltaTime(1.0 / FramesPerSecond);

	Film = NewObject<UTextureRenderTarget2D>(this);
	Film->RenderTargetFormat = RTF_RGBA8;
	Film->ClearColor = FLinearColor::Black;
	Film->InitAutoFormat(FrameSize, FrameSize);
	Film->UpdateResourceImmediate(true);

	Camera = NewObject<USceneCaptureComponent2D>(this);
	Camera->SetupAttachment(RootComponent);
	Camera->RegisterComponent();
	Camera->TextureTarget = Film;
	Camera->bCaptureEveryFrame = false;
	Camera->bCaptureOnMovement = false;
	Camera->bAlwaysPersistRenderingState = true;
	Camera->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	// Only the effect, on black: the level, its sky and its fog would make every
	// picture about the level instead.
	Camera->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	Camera->ShowFlags.SetAtmosphere(false);
	Camera->ShowFlags.SetFog(false);
	Camera->ShowFlags.SetCloud(false);
	Camera->ShowFlags.SetMotionBlur(false);
	// A fixed exposure. Left to adapt, a bright effect would darken itself over
	// the two seconds and the strip would show the camera adjusting.
	Camera->PostProcessSettings.bOverride_AutoExposureMethod = true;
	Camera->PostProcessSettings.AutoExposureMethod = AEM_Manual;
	Camera->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	Camera->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = false;
	Camera->PostProcessSettings.bOverride_AutoExposureBias = true;
	Camera->PostProcessSettings.AutoExposureBias = 0.0f;
	Camera->PostProcessBlendWeight = 1.0f;

	// Most effects glow by themselves, but some particles are lit -- leaves,
	// feathers, smoke -- and far above the level nothing lights them.
	Lamp = NewObject<UPointLightComponent>(this);
	Lamp->SetupAttachment(RootComponent);
	Lamp->RegisterComponent();
	Lamp->SetMobility(EComponentMobility::Movable);
	Lamp->bUseInverseSquaredFalloff = false;
	Lamp->SetLightFalloffExponent(1.0f);
	Lamp->SetIntensity(6.0f);
	Lamp->SetCastShadows(false);

	// A filtered run (-tmvfxonly) is a check, filmed apart, so the creator's
	// full catalogue is left as it was.
	OutDir = FPaths::ProjectSavedDir() / (FParse::Param(FCommandLine::Get(), TEXT("tmvfxonly")) || FCString::Strifind(FCommandLine::Get(), TEXT("tmvfxonly=")) ? TEXT("VfxCheck") : TEXT("VfxCatalog"));
	// A fresh catalogue each time: an effect removed from the project must not
	// linger in the creator as a strip it can still pick.
	IFileManager::Get().DeleteDirectory(*OutDir, false, true);
	IFileManager::Get().MakeDirectory(*OutDir, true);

	FindEffects();
	UE_LOG(LogTemp, Log, TEXT("VFX STUDIO: %d effects to film"), ToFilm.Num());
}

void ATMVfxStudio::FindEffects()
{
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	// A game run started from the editor binary may still be scanning.
	Registry.SearchAllAssets(true);

	FARFilter Filter;
	Filter.PackagePaths.Add(FName(TEXT("/Game")));
	Filter.bRecursivePaths = true;
	Filter.ClassPaths.Add(UNiagaraSystem::StaticClass()->GetClassPathName());
	Filter.ClassPaths.Add(UParticleSystem::StaticClass()->GetClassPathName());
	TArray<FAssetData> Found;
	Registry.GetAssets(Filter, Found);
	// -tmvfxonly=<file>: only the effects listed in it, one object path a line --
	// the ones Tools/assign_vfx.py chose, say, rather than every one of a
	// thousand and more.
	TSet<FString> Only;
	FString OnlyFile;
	if (FParse::Value(FCommandLine::Get(), TEXT("tmvfxonly="), OnlyFile))
	{
		TArray<FString> Lines;
		FFileHelper::LoadFileToStringArray(Lines, *OnlyFile);
		for (const FString& Line : Lines)
		{
			if (!Line.TrimStartAndEnd().IsEmpty())
			{
				Only.Add(Line.TrimStartAndEnd());
			}
		}
		UE_LOG(LogTemp, Log, TEXT("VFX STUDIO: only the %d effects listed in %s"), Only.Num(), *OnlyFile);
	}
	for (const FAssetData& Asset : Found)
	{
		if (Only.Num() == 0 || Only.Contains(Asset.GetSoftObjectPath().ToString()))
		{
			ToFilm.Add(Asset.GetSoftObjectPath());
		}
	}
	// Alphabetical, so the catalogue reads the same from one run to the next.
	ToFilm.Sort([](const FSoftObjectPath& A, const FSoftObjectPath& B) { return A.ToString() < B.ToString(); });
}

UFXSystemComponent* ATMVfxStudio::Play(UObject* System)
{
	UFXSystemComponent* Component = nullptr;
	if (UNiagaraSystem* Niagara = Cast<UNiagaraSystem>(System))
	{
		Component = UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, Niagara, Stage,
			FRotator::ZeroRotator, FVector(1.0), false, true, ENCPoolMethod::None, false);
	}
	else if (UParticleSystem* Cascade = Cast<UParticleSystem>(System))
	{
		Component = UGameplayStatics::SpawnEmitterAtLocation(this, Cascade, Stage,
			FRotator::ZeroRotator, FVector(1.0), false);
	}
	if (Component)
	{
		Camera->ClearShowOnlyComponents();
		Camera->ShowOnlyComponents.Add(Component);
	}
	return Component;
}

void ATMVfxStudio::StopPlaying()
{
	if (Playing)
	{
		Playing->DestroyComponent();
		Playing = nullptr;
	}
}

void ATMVfxStudio::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	switch (Phase)
	{
	case EPhase::Next:
	{
		StopPlaying();
		++Index;
		if (Index >= ToFilm.Num())
		{
			WriteCatalogue();
			UE_LOG(LogTemp, Log, TEXT("VFX STUDIO DONE: %d of %d effects filmed -> %s"),
				Filmed.Num(), ToFilm.Num(), *OutDir);
			Phase = EPhase::Done;
			FPlatformMisc::RequestExit(false);
			return;
		}
		Current = ToFilm[Index].TryLoad();
#if WITH_EDITOR
		// Started from the editor, an effect is compiled when it is first loaded,
		// and until then it draws nothing, or draws its materials as the grey
		// checkerboard Unreal shows while their shaders are being built. Filmed
		// then, a strip shows the compiler rather than the effect.
		if (UNiagaraSystem* Niagara = Cast<UNiagaraSystem>(Current))
		{
			Niagara->WaitForCompilationComplete(true, false);
		}
		FAssetCompilingManager::Get().FinishAllCompilation();
		if (GShaderCompilingManager)
		{
			GShaderCompilingManager->FinishAllCompilation();
		}
#endif
		Playing = Current ? Play(Current) : nullptr;
		if (!Playing)
		{
			UE_LOG(LogTemp, Warning, TEXT("VFX STUDIO: could not play %s"), *ToFilm[Index].ToString());
			return;
		}
		Phase = EPhase::Measure;
		Elapsed = 0.0f;
		Reach = FBox(ForceInit);
		bAimed = false;
		SeenLow = FIntPoint(FrameSize, FrameSize);
		SeenHigh = FIntPoint(-1, -1);
		break;
	}
	case EPhase::Measure:
		// Played once unfilmed, to learn where it goes and how far it spreads, so
		// the camera can stand back far enough for the whole of it and no further.
		Elapsed += DeltaSeconds;
		if (!bAimed)
		{
			// First, what the effect says about its own size. Often that is a box
			// nobody sized -- a Niagara system's is a metre whatever it holds --
			// so it only points the camera the right way to start with.
			if (Playing->Bounds.SphereRadius > 0.0f)
			{
				Reach += Playing->Bounds.GetBox();
			}
			if (Elapsed >= 0.4f)
			{
				AimAtReach();
			}
		}
		else
		{
			// Then what the camera actually sees of it.
			LookAtFrame();
		}
		if (Elapsed >= MeasureSeconds)
		{
			Refine();
			// Then from the start again, which is where the strip must begin. Any
			// shaders the first play asked for are finished first.
#if WITH_EDITOR
			if (GShaderCompilingManager)
			{
				GShaderCompilingManager->FinishAllCompilation();
			}
#endif
			StopPlaying();
			Playing = Play(Current);
			Phase = EPhase::Film;
			FramesTaken = 0;
			Strip.SetNumZeroed(FrameSize * FrameSize * FrameCount);
			Record();
		}
		break;
	case EPhase::Film:
		TakeFrame();
		if (FramesTaken >= FrameCount)
		{
			SaveStrip();
			Phase = EPhase::Next;
		}
		break;
	case EPhase::Done:
		break;
	}
}

void ATMVfxStudio::AimAtReach()
{
	bAimed = true;
	// Held to a sensible range, in centimetres: an effect that had not started
	// claims nothing, and one with a box nobody sized can claim kilometres.
	Middle = Stage;
	float Radius = 150.0f;
	if (Reach.IsValid)
	{
		Middle = Reach.GetCenter();
		Radius = FMath::Clamp(static_cast<float>(Reach.GetExtent().Size()), 30.0f, 1500.0f);
		// An effect that drifts far from where it began is framed on where it began.
		if (FVector::Dist(Middle, Stage) > Radius)
		{
			Middle = Stage;
		}
	}
	Distance = Radius / FMath::Tan(FMath::DegreesToRadians(Fov * 0.5f)) * 1.15f;
	Place();
}

void ATMVfxStudio::Place()
{
	// From the front and a little above, as the game's camera sees the board,
	// with a light beside the camera for effects whose particles are lit.
	const FRotator Looking(-25.0f, 0.0f, 0.0f);
	Camera->FOVAngle = Fov;
	Camera->SetWorldLocationAndRotation(Middle - Looking.Vector() * Distance, Looking);
	Lamp->SetWorldLocation(Camera->GetComponentLocation() + FVector(0.0, Distance * 0.5, Distance * 0.5));
	Lamp->SetAttenuationRadius(Distance * 4.0f);
}

bool ATMVfxStudio::Shoot(TArray<FColor>& Pixels)
{
	Camera->CaptureScene();
	FTextureRenderTargetResource* Resource = Film->GameThread_GetRenderTargetResource();
	return Resource && Resource->ReadPixels(Pixels) && Pixels.Num() == FrameSize * FrameSize;
}

void ATMVfxStudio::LookAtFrame()
{
	TArray<FColor> Pixels;
	if (!Shoot(Pixels))
	{
		return;
	}
	for (int32 Y = 0; Y < FrameSize; ++Y)
	{
		for (int32 X = 0; X < FrameSize; ++X)
		{
			const FColor Pixel = Pixels[Y * FrameSize + X];
			// Faint haze is left out: it would make every effect look as big as
			// the glow around it.
			if (Pixel.R + Pixel.G + Pixel.B > 60)
			{
				SeenLow = FIntPoint(FMath::Min(SeenLow.X, X), FMath::Min(SeenLow.Y, Y));
				SeenHigh = FIntPoint(FMath::Max(SeenHigh.X, X), FMath::Max(SeenHigh.Y, Y));
			}
		}
	}
}

void ATMVfxStudio::Refine()
{
	if (SeenHigh.X < 0)
	{
		// Nothing seen at all: filmed as it was aimed, and the strip will say so.
		return;
	}
	const float Half = FrameSize * 0.5f;
	const bool bClipped = SeenLow.X <= 0 || SeenLow.Y <= 0 || SeenHigh.X >= FrameSize - 1 || SeenHigh.Y >= FrameSize - 1;
	if (bClipped)
	{
		// Spilling out of the picture: stand back and keep the aim.
		Distance *= 1.8f;
		Place();
		return;
	}
	// Move the aim onto the middle of what was seen, then come in until it fills
	// most of the picture.
	const FVector2D Centre((SeenLow.X + SeenHigh.X) * 0.5f - Half, (SeenLow.Y + SeenHigh.Y) * 0.5f - Half);
	const float Span = FMath::Max(SeenHigh.X - SeenLow.X, SeenHigh.Y - SeenLow.Y) * 0.5f / Half;
	const float Across = Distance * FMath::Tan(FMath::DegreesToRadians(Fov * 0.5f));
	Middle += Camera->GetRightVector() * (Centre.X / Half * Across)
		- Camera->GetUpVector() * (Centre.Y / Half * Across);
	Distance *= FMath::Clamp(Span / 0.8f, 0.1f, 1.0f);
	Place();
}

void ATMVfxStudio::Record()
{
	const float Radius = Distance * FMath::Tan(FMath::DegreesToRadians(Fov * 0.5f));
	FEntry& Entry = Filmed.AddDefaulted_GetRef();
	Entry.Path = ToFilm[Index].ToString();
	Entry.Name = ToFilm[Index].GetAssetName();
	Entry.Kind = Current->IsA<UNiagaraSystem>() ? TEXT("niagara") : TEXT("cascade");
	// The package path made into a file name, so two packs' NS_Fire do not collide.
	Entry.Sheet = ToFilm[Index].GetLongPackageName().Mid(6).Replace(TEXT("/"), TEXT("__")) + TEXT(".png");
	Entry.Radius = Radius;
}

void ATMVfxStudio::TakeFrame()
{
	TArray<FColor> Pixels;
	if (!Shoot(Pixels))
	{
		++FramesTaken;
		return;
	}
	// Laid side by side, left to right. The capture leaves alpha meaning
	// something else entirely, so every pixel is made opaque: black is the
	// background the creator draws the strip on.
	const int32 Across = FrameSize * FrameCount;
	for (int32 Y = 0; Y < FrameSize; ++Y)
	{
		for (int32 X = 0; X < FrameSize; ++X)
		{
			FColor Pixel = Pixels[Y * FrameSize + X];
			Pixel.A = 255;
			Strip[Y * Across + FramesTaken * FrameSize + X] = Pixel;
		}
	}
	++FramesTaken;
}

void ATMVfxStudio::SaveStrip()
{
	FEntry& Entry = Filmed.Last();
	// Still going at the end of the strip: it loops, or at least lasts, and the
	// creator plays it round. One that has finished plays once and pauses.
	Entry.bLoops = Playing && Playing->IsActive();
	// Some effects show nothing played on their own: arrows posed for a poster,
	// effects drawn on the screen, effects that follow a character's bones. They
	// are kept in the catalogue, marked, so the creator can say why the strip is
	// black instead of looking broken.
	Entry.bVisible = false;
	for (const FColor& Pixel : Strip)
	{
		if (Pixel.R + Pixel.G + Pixel.B > 60)
		{
			Entry.bVisible = true;
			break;
		}
	}
	const FImageView Image(Strip.GetData(), FrameSize * FrameCount, FrameSize);
	if (!FImageUtils::SaveImageByExtension(*(OutDir / Entry.Sheet), Image))
	{
		UE_LOG(LogTemp, Warning, TEXT("VFX STUDIO: could not save %s"), *Entry.Sheet);
		Filmed.Pop();
		return;
	}
	// And a small copy for the creator's list of effects, which shows every one
	// at once: seventy full strips would be over half a gigabyte to a browser.
	// The frames scale evenly because the strip's width and height shrink alike.
	TArray<FColor> Small;
	Small.SetNumUninitialized(ThumbSize * FrameCount * ThumbSize);
	FImageUtils::ImageResize(FrameSize * FrameCount, FrameSize, TArrayView<const FColor>(Strip),
		ThumbSize * FrameCount, ThumbSize, TArrayView<FColor>(Small), false, true);
	Entry.Thumb = FPaths::GetBaseFilename(Entry.Sheet) + TEXT(".thumb.png");
	FImageUtils::SaveImageByExtension(*(OutDir / Entry.Thumb), FImageView(Small.GetData(), ThumbSize * FrameCount, ThumbSize));

	UE_LOG(LogTemp, Log, TEXT("VFX STUDIO: %d/%d %s (%s, %.0f cm%s%s)"), Index + 1, ToFilm.Num(),
		*Entry.Name, *Entry.Kind, Entry.Radius, Entry.bLoops ? TEXT(", loops") : TEXT(""),
		Entry.bVisible ? TEXT("") : TEXT(", shows nothing alone"));
}

void ATMVfxStudio::WriteCatalogue() const
{
	// Read by the class creator (app/vfx.mjs there). Hand-written rather than
	// through the Json module: it is one flat list.
	FString Json = FString::Printf(
		TEXT("{\n  \"format\": \"tactical-masters-vfx-catalog\",\n  \"version\": 1,\n")
		TEXT("  \"made\": \"%s\",\n  \"frameSize\": %d,\n  \"thumbSize\": %d,\n  \"frames\": %d,\n  \"fps\": %d,\n  \"effects\": ["),
		*FDateTime::UtcNow().ToIso8601(), FrameSize, ThumbSize, FrameCount, FramesPerSecond);
	for (int32 i = 0; i < Filmed.Num(); ++i)
	{
		const FEntry& Entry = Filmed[i];
		// The pack is the first folder under Content, which is where Fab puts one.
		FString Pack = Entry.Path.Mid(6);
		Pack.Split(TEXT("/"), &Pack, nullptr);
		Json += FString::Printf(
			TEXT("%s\n    {\"path\": \"%s\", \"name\": \"%s\", \"pack\": \"%s\", \"kind\": \"%s\", \"sheet\": \"%s\", \"thumb\": \"%s\", \"radius\": %.0f, \"loops\": %s, \"visible\": %s}"),
			i ? TEXT(",") : TEXT(""), *Escaped(Entry.Path), *Escaped(Entry.Name), *Escaped(Pack),
			*Entry.Kind, *Escaped(Entry.Sheet), *Escaped(Entry.Thumb), Entry.Radius, Entry.bLoops ? TEXT("true") : TEXT("false"),
			Entry.bVisible ? TEXT("true") : TEXT("false"));
	}
	Json += TEXT("\n  ]\n}\n");
	FFileHelper::SaveStringToFile(Json, *(OutDir / TEXT("catalog.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
