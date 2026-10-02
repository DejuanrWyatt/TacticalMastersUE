// The Unit Guide's turntable: a class's hero standing on its own, idling and
// turning slowly, filmed for the guide's class page.
//
// The hero is a body like any unit's (the character map's, BodyForJob), worn by
// a component of its own far above the board. Only its own camera can see it
// (bVisibleInSceneCaptureOnly), so it never shows in the battle, and up there
// nothing casts a shadow on it. A hero not read yet is read in the background,
// as a battle's are, and the page says so meanwhile.
//
// Presentation only: nothing here is read by the rules.

#include "TMBattleDirector.h"

#include "Components/PointLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StreamableManager.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Misc/App.h"

namespace
{
	/** Where the hero stands, from the director: well above anything on the board. */
	const FVector StandAt(0.0f, 0.0f, 60000.0f);
	/** Degrees a second it turns on its own. */
	constexpr float SpinPerSecond = 22.0f;
	constexpr int32 FilmW = 560;
	constexpr int32 FilmH = 720;
	/** The camera's field of view, across. */
	constexpr float FieldOfView = 30.0f;
}

UTextureRenderTarget2D* ATMBattleDirector::GuideModel(const std::string& JobId, bool& bOutLoading)
{
	bOutLoading = false;
	UWorld* World = GetWorld();
	if (!World || !RootComponent || !LoadCharacterMap())
	{
		return nullptr;
	}
	const FTMBody* Body = BodyForJob(JobId);
	if (!Body)
	{
		return nullptr;
	}

	// Read it first, in the background where a person is watching.
	if (!IsBodyLoaded(*Body))
	{
		if (World->IsGameWorld() && !FApp::IsUnattended())
		{
			TArray<FSoftObjectPath> Paths;
			UnreadPaths(TArray<const FTMBody*>{ Body }, Paths);
			if (Paths.Num() > 0)
			{
				if (!ShowcaseLoad.IsValid() || ShowcaseLoadFor != Body->MeshPath)
				{
					if (ShowcaseLoad.IsValid())
					{
						ShowcaseLoad->CancelHandle();
					}
					ShowcaseLoadFor = Body->MeshPath;
					ShowcaseLoad = UAssetManager::GetStreamableManager().RequestAsyncLoad(Paths, FStreamableDelegate(),
						FStreamableManager::AsyncLoadHighPriority);
				}
				if (ShowcaseLoad.IsValid() && !ShowcaseLoad->HasLoadCompleted())
				{
					bOutLoading = true;
					return nullptr;
				}
			}
		}
		ShowcaseLoad.Reset();
		ShowcaseLoadFor.Reset();
		if (!MeshOf(*Body))
		{
			return nullptr;
		}
	}

	if (!ShowcaseBody)
	{
		ShowcaseBody = NewObject<USkeletalMeshComponent>(this,
			MakeUniqueObjectName(this, USkeletalMeshComponent::StaticClass(), TEXT("GuideHero")), RF_Transient);
		ShowcaseBody->SetupAttachment(RootComponent);
		ShowcaseBody->bDisableClothSimulation = true;
		ShowcaseBody->bDisableMorphTarget = true;
		ShowcaseBody->RegisterComponent();
		ShowcaseBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ShowcaseBody->SetCastShadow(false);
		ShowcaseBody->SetVisibleInSceneCaptureOnly(true);
		// Nobody sees it but its camera, so it must be told to keep moving anyway.
		ShowcaseBody->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

		ShowcaseLight = NewObject<UPointLightComponent>(this,
			MakeUniqueObjectName(this, UPointLightComponent::StaticClass(), TEXT("GuideLight")), RF_Transient);
		ShowcaseLight->SetupAttachment(RootComponent);
		ShowcaseLight->RegisterComponent();
		ShowcaseLight->SetCastShadows(false);
		ShowcaseLight->SetAttenuationRadius(1400.0f);
		ShowcaseLight->SetIntensity(ReadyLightBrightness);
		ShowcaseLight->SetLightColor(FLinearColor(1.0f, 0.95f, 0.88f));

		ShowcaseFilm = NewObject<UTextureRenderTarget2D>(this);
		ShowcaseFilm->RenderTargetFormat = RTF_RGBA8;
		ShowcaseFilm->ClearColor = FLinearColor(0.02f, 0.03f, 0.05f, 1.0f);
		ShowcaseFilm->InitAutoFormat(FilmW, FilmH);
		ShowcaseFilm->UpdateResourceImmediate(true);

		ShowcaseCamera = NewObject<USceneCaptureComponent2D>(this,
			MakeUniqueObjectName(this, USceneCaptureComponent2D::StaticClass(), TEXT("GuideCamera")), RF_Transient);
		ShowcaseCamera->SetupAttachment(RootComponent);
		ShowcaseCamera->RegisterComponent();
		ShowcaseCamera->TextureTarget = ShowcaseFilm;
		ShowcaseCamera->bCaptureEveryFrame = false;
		ShowcaseCamera->bCaptureOnMovement = false;
		ShowcaseCamera->bAlwaysPersistRenderingState = true;
		ShowcaseCamera->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
		ShowcaseCamera->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
		ShowcaseCamera->ShowFlags.SetFog(false);
		ShowcaseCamera->ShowFlags.SetAtmosphere(false);
		ShowcaseCamera->ShowFlags.SetMotionBlur(false);
		ShowcaseCamera->FOVAngle = FieldOfView;
		ShowcaseCamera->ShowOnlyComponents.Add(ShowcaseBody);
	}

	// Dressed as the class, idling, when the class changes.
	const FString Want = UTF8_TO_TCHAR(JobId.c_str());
	// A new body at most every quarter second: flipping through the Codex fast
	// swapped skeletal meshes faster than the renderer let go of the last one,
	// and crashed it (2026-10-01, SkeletalRenderGPUSkin.cpp "Index == 1").
	// The guide's hero has no cloth either: it only stands and turns.
	const double SwapNow = World->GetRealTimeSeconds();
	if ((ShowcaseJob != Want || !ShowcaseBody->IsVisible()) && (ShowcaseSwappedAt < 0.0 || SwapNow - ShowcaseSwappedAt >= 0.25))
	{
		ShowcaseSwappedAt = SwapNow;
		ShowcaseJob = Want;
		WearBody(ShowcaseBody, *Body, true);
		ShowcaseBody->SetVisibility(true);
		ShowcaseLight->SetVisibility(true);
		if (Body->Animations && Body->Animations->Idle)
		{
			ShowcaseBody->PlayAnimation(Body->Animations->Idle, true);
		}
		else
		{
			ShowcaseBody->Stop();
		}
		ShowcaseYaw = 0.0f;
		// How tall it stands, from its mesh: Paragon's stand on their origin.
		// Mesh bounds can be generous, so it is kept to a sensible range.
		float Top = 190.0f;
		if (Body->Mesh)
		{
			const FBoxSphereBounds Bounds = Body->Mesh->GetBounds();
			Top = static_cast<float>(Bounds.Origin.Z + Bounds.BoxExtent.Z);
		}
		ShowcaseHeight = FMath::Clamp(Top, 120.0f, 450.0f) * Body->Scale;
	}

	// Turning slowly, by the clock rather than the battle's time, so it turns
	// while the battle is paused under the guide.
	const double Now = World->GetRealTimeSeconds();
	if (ShowcaseLastTime >= 0.0)
	{
		ShowcaseYaw += SpinPerSecond * static_cast<float>(FMath::Clamp(Now - ShowcaseLastTime, 0.0, 0.1));
	}
	ShowcaseLastTime = Now;
	ShowcaseYaw = FMath::Fmod(ShowcaseYaw, 360.0f);

	// Stood up, turned, and framed head to foot with a little room.
	const FVector Feet = GetActorLocation() + StandAt;
	ShowcaseBody->SetWorldLocationAndRotation(Feet, FRotator(0.0f, ShowcaseYaw + Body->Yaw, 0.0f));
	const float Aspect = static_cast<float>(FilmW) / FilmH;
	const float HalfUp = FMath::Tan(FMath::DegreesToRadians(FieldOfView * 0.5f)) / Aspect;
	const FVector Middle = Feet + FVector(0.0f, 0.0f, ShowcaseHeight * 0.5f);
	const float Distance = ShowcaseHeight * 0.58f / FMath::Max(HalfUp, 0.05f);
	const FVector Eye = Middle + FVector(Distance, 0.0f, ShowcaseHeight * 0.1f);
	ShowcaseCamera->SetWorldLocationAndRotation(Eye, (Middle - Eye).Rotation());
	ShowcaseLight->SetWorldLocation(Feet + FVector(Distance * 0.5f, -Distance * 0.35f, ShowcaseHeight * 1.2f));
	// Deferred: a capture in the middle of a tick can trip the engine's
	// end-of-frame assertion (see PortraitOf, TMBattleDirectorLooks.cpp).
	ShowcaseCamera->CaptureSceneDeferred();
	return ShowcaseFilm;
}

void ATMBattleDirector::GuideModelTurn(float Degrees)
{
	ShowcaseYaw = FMath::Fmod(ShowcaseYaw + Degrees + 360.0f, 360.0f);
}

void ATMBattleDirector::HideGuideModel()
{
	if (ShowcaseLoad.IsValid())
	{
		ShowcaseLoad->CancelHandle();
		ShowcaseLoad.Reset();
	}
	ShowcaseLoadFor.Reset();
	if (ShowcaseBody)
	{
		ShowcaseBody->Stop();
		ShowcaseBody->SetVisibility(false);
	}
	if (ShowcaseLight)
	{
		ShowcaseLight->SetVisibility(false);
	}
	ShowcaseLastTime = -1.0;
}
