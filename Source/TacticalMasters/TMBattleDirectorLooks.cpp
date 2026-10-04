// How units look to the player: whose side they are on, when they act next, and
// a face for the HUD's unit panels.
//
// A ring lies under every unit and fills as its Turn Gauge does, blue for an
// ally and red for an enemy, so who acts next can be read off the board at a
// glance. Each ring is a flat square showing a picture drawn here, through the
// engine's own widget material, so it needs no asset made in the editor.
//
// The outline around each body is marked here (custom depth stencil 1 for an
// ally, 2 for an enemy) and drawn by a post-process material, which is made in
// the editor (Docs/TeamOutline.md). Without it the units simply have no outline.
//
// Nothing here is read by the rules.

#include "TMBattleDirector.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Canvas.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

#include "TMSettings.h"

namespace
{
	constexpr int32 RingPixels = 128;
	/** Across, in centimetres: about a body's width, so neighbours' rings do not merge. */
	constexpr float RingSize = 110.0f;
	const TCHAR* const OutlinePath = TEXT("/Game/UI/M_TeamOutline.M_TeamOutline");

	FLinearColor OutlineSideColour(bool bFriend)
	{
		if (bFriend)
		{
			return FLinearColor(0.2f, 0.55f, 1.0f);
		}
		return FTMSettings::Get().bColorblind ? FLinearColor(1.0f, 0.6f, 0.1f) : FLinearColor(1.0f, 0.18f, 0.15f);
	}
}

int32 ATMBattleDirector::FriendTeam() const
{
	const int32 Viewer = ViewerTeam();
	if (Viewer >= 0)
	{
		return Viewer;
	}
	// Two people taking turns: whoever is ordering now sees their own units
	// blue, as each would on their own screen.
	for (const TMSim::FUnit& Unit : Battle.Units)
	{
		if (Unit.Id == SelectedId)
		{
			return Unit.Team;
		}
	}
	return 0;
}

void ATMBattleDirector::BuildTurnRings()
{
	UMaterialInterface* Paint = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/Widget3DPassThrough_Translucent.Widget3DPassThrough_Translucent"));
	UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (!Paint || !Plane || !GetWorld() || !GetWorld()->IsGameWorld())
	{
		return;
	}
	for (size_t i = 0; i < Battle.Units.size(); ++i)
	{
		UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(this);
		Target->RenderTargetFormat = RTF_RGBA8;
		Target->ClearColor = FLinearColor::Transparent;
		Target->InitAutoFormat(RingPixels, RingPixels);
		Target->UpdateResourceImmediate(true);

		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Paint, this);
		Mid->SetTextureParameterValue(TEXT("SlateUI"), Target);

		UStaticMeshComponent* Ring = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
		Ring->SetStaticMesh(Plane);
		Ring->SetMobility(EComponentMobility::Movable);
		Ring->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Ring->SetCastShadow(false);
		Ring->SetupAttachment(RootComponent);
		Ring->RegisterComponent();
		Ring->SetMaterial(0, Mid);
		Ring->SetRelativeScale3D(FVector(RingSize / 100.0f));

		TurnRings.Add(Ring);
		TurnRingTargets.Add(Target);
		TurnRingPaint.Add(Mid);
		TurnRingDrawn.Add(-1.0f);
	}
}

void ATMBattleDirector::AdvanceTurnRings()
{
	const double Now = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0;
	for (int32 i = 0; i < TurnRings.Num() && i < static_cast<int32>(Battle.Units.size()) && i < Motions.Num(); ++i)
	{
		const TMSim::FUnit& Unit = Battle.Units[i];
		UStaticMeshComponent* Ring = TurnRings[i];
		if (!Ring)
		{
			continue;
		}
		const bool bShow = Unit.IsAlive() && IsSeen(Unit) && Screen == EScreen::Battle;
		Ring->SetVisibility(bShow);
		if (!bShow)
		{
			continue;
		}
		// On the ground where the body stands, just above the tiles.
		Ring->SetRelativeLocation(Motions[i].Shown + FVector(0.0f, 0.0f, 3.0f));

		// The ring is the unit's health now, not only its turn (2026-10-01, the
		// human's pick "C" of the health bar mockups): the outer band its health in
		// its side's colour (amber for a monster), a shield's worth after it in
		// white; a thin inner band its Turn Gauge, gold while it may act, with a gold
		// edge round the whole. The bar over its head is shown only while the
		// pointer is on it (TMBattleHudPanels.cpp DrawOverheads).
		const float Full = Unit.bReady ? 1.0f : FMath::Clamp(static_cast<float>(Unit.Tg) / TMSim::Pace::TgMax, 0.0f, 1.0f);
		const float MaxHp = static_cast<float>(FMath::Max(1, Unit.MaxHp()));
		int32 Soak = 0;
		for (const TMSim::FStatus& Status : Unit.Statuses)
		{
			const TMSim::FStatusDef* Def = TMSim::FindStatus(Status.Id);
			Soak += Def && Def->bAbsorbs ? Status.Amount : 0;
		}
		const float Health = FMath::Clamp(Unit.Hp / MaxHp, 0.0f, 1.0f);
		const float Shield = FMath::Clamp(Soak / MaxHp, 0.0f, 1.0f - Health);
		// A unit whose turn it is breathes, so it is found at once.
		const float Breath = Unit.bReady ? 0.8f + 0.2f * FMath::Sin(static_cast<float>(Now) * 5.0f) : 1.0f;
		TurnRingPaint[i]->SetVectorParameterValue(TEXT("TintColorAndOpacity"), FLinearColor(1.0f, 1.0f, 1.0f, Breath));
		// Painted again only when what it shows changes (a whole number, exact in a float).
		const int32 Key = ((FMath::RoundToInt(Health * 400.0f) * 101 + FMath::RoundToInt(Shield * 100.0f)) * 101 + FMath::RoundToInt(Full * 100.0f)) * 2
			+ (Unit.bReady ? 1 : 0);
		if (static_cast<int32>(TurnRingDrawn[i]) == Key)
		{
			continue;
		}
		TurnRingDrawn[i] = static_cast<float>(Key);
		const FLinearColor Side = Unit.bMonster ? FLinearColor(0.95f, 0.66f, 0.2f) : OutlineSideColour(IsFriend(Unit));
		const FLinearColor RingGold(1.0f, 0.82f, 0.35f);

		// Each band from twelve o'clock clockwise, in its own colour.
		UKismetRenderingLibrary::ClearRenderTarget2D(this, TurnRingTargets[i], FLinearColor::Transparent);
		UCanvas* Canvas = nullptr;
		FVector2D Size;
		FDrawToRenderTargetContext Context;
		UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, TurnRingTargets[i], Canvas, Size, Context);
		if (Canvas)
		{
			const FVector2D Middle = Size * 0.5f;
			const float Outer = Size.X * 0.47f;
			const float Inner = Size.X * 0.405f;
			const int32 Steps = 72;
			TArray<FCanvasUVTri> Triangles;
			auto Band = [&](float From, float To, const FLinearColor& C, float Out, float In)
			{
				const int32 Count = FMath::Max(1, FMath::CeilToInt(Steps * (To - From)));
				for (int32 k = 0; k < Count; ++k)
				{
					const float A0 = UE_TWO_PI * FMath::Lerp(From, To, static_cast<float>(k) / Count) - UE_HALF_PI;
					const float A1 = UE_TWO_PI * FMath::Lerp(From, To, static_cast<float>(k + 1) / Count) - UE_HALF_PI;
					const FVector2D D0(FMath::Cos(A0), FMath::Sin(A0));
					const FVector2D D1(FMath::Cos(A1), FMath::Sin(A1));
					FCanvasUVTri T1;
					T1.V0_Pos = Middle + D0 * Out; T1.V1_Pos = Middle + D1 * Out; T1.V2_Pos = Middle + D0 * In;
					T1.V0_Color = T1.V1_Color = T1.V2_Color = C;
					FCanvasUVTri T2;
					T2.V0_Pos = Middle + D1 * Out; T2.V1_Pos = Middle + D1 * In; T2.V2_Pos = Middle + D0 * In;
					T2.V0_Color = T2.V1_Color = T2.V2_Color = C;
					Triangles.Add(T1);
					Triangles.Add(T2);
				}
			};
			// Health: a dark track, the health over it, then any shield.
			Band(0.0f, 1.0f, FLinearColor(0.02f, 0.02f, 0.04f, 0.55f), Outer, Inner);
			if (Health > 0.0f)
			{
				Band(0.0f, Health, FLinearColor(Side.R, Side.G, Side.B, 1.0f), Outer, Inner);
			}
			if (Shield > 0.0f)
			{
				Band(Health, Health + Shield, FLinearColor(1.0f, 1.0f, 1.0f, 0.85f), Outer, Inner);
			}
			// The Turn Gauge, thin, inside it.
			const float GaugeOut = Inner - Size.X * 0.035f;
			const float GaugeIn = GaugeOut - Size.X * 0.025f;
			Band(0.0f, 1.0f, FLinearColor(0.02f, 0.02f, 0.04f, 0.35f), GaugeOut, GaugeIn);
			if (Full > 0.0f)
			{
				Band(0.0f, Full, Unit.bReady ? RingGold : FLinearColor(0.55f, 0.75f, 0.95f, 0.95f), GaugeOut, GaugeIn);
			}
			if (Unit.bReady)
			{
				// Its turn: a thin gold edge round the whole.
				Band(0.0f, 1.0f, RingGold, Size.X * 0.5f, Outer + Size.X * 0.008f);
			}
			Canvas->K2_DrawTriangle(nullptr, Triangles);
		}
		UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Context);
	}
}

void ATMBattleDirector::ApplyOutlines()
{
	for (int32 i = 0; i < UnitVisuals.Num() && i < static_cast<int32>(Battle.Units.size()); ++i)
	{
		USkeletalMeshComponent* Body = UnitVisuals[i];
		if (!Body)
		{
			continue;
		}
		// 3 is a unit under the pointer or in the aim: the outline material draws
		// it thick, glowing red (Tools/make_outline_material.py).
		const int32 Stencil = MarkedUnits.Contains(Battle.Units[i].Id) ? 3 : IsFriend(Battle.Units[i]) ? 1 : 2;
		if (!Body->bRenderCustomDepth || Body->CustomDepthStencilValue != Stencil)
		{
			Body->SetRenderCustomDepth(true);
			Body->SetCustomDepthStencilValue(Stencil);
		}
	}
}

void ATMBattleDirector::UpdateMarks()
{
	// Who is marked: the unit under the pointer, and, while an ability is being
	// aimed, every unit it would touch from here -- whatever its shape: one
	// target, a circle, a cone, a line.
	TSet<int32> Now;
	if (Screen == EScreen::Battle && Battle.Winner == -1)
	{
		if (const TMSim::FUnit* Hovered = Battle.FindUnit(HoverUnitId))
		{
			if (IsSeen(*Hovered))
			{
				Now.Add(Hovered->Id);
			}
		}
		// Pointed at on the turn order (2026-10-02): the unit on the board lights up too.
		if (const TMSim::FUnit* OnBar = Battle.FindUnit(HudHoverUnitId))
		{
			if (IsSeen(*OnBar))
			{
				Now.Add(OnBar->Id);
			}
		}
		const TMSim::FUnit* Unit = SelectedUnit();
		if (AimMode == EAimMode::Ability && PlayerCanOrder(Unit) && !Battle.IsPlanning())
		{
			const FAim Where = Aim();
			if (Where.bHave)
			{
				for (const TMSim::FHit& Hit : Battle.Preview(*Unit, AimSlot, Unit->Pos, Where.Point))
				{
					const TMSim::FUnit* Touched = Battle.FindUnit(Hit.UnitId);
					if (Touched && IsSeen(*Touched))
					{
						Now.Add(Hit.UnitId);
					}
				}
			}
		}
	}
	bool bSame = Now.Num() == MarkedUnits.Num();
	for (const int32 Id : Now)
	{
		bSame = bSame && MarkedUnits.Contains(Id);
	}
	if (!bSame)
	{
		MarkedUnits = MoveTemp(Now);
		// Red over an enemy, blue over a friend (the human's ask, 2026-09-30): an
		// aim that touches any enemy marks all it touches red.
		bool bEnemy = false;
		for (const int32 Id : MarkedUnits)
		{
			const TMSim::FUnit* Marked = Battle.FindUnit(Id);
			bEnemy = bEnemy || (Marked && !IsFriend(*Marked));
		}
		if (OutlineMid)
		{
			OutlineMid->SetVectorParameterValue(TEXT("HoverColour"),
				bEnemy ? FLinearColor(1.0f, 0.08f, 0.05f, 1.0f) : FLinearColor(0.15f, 0.55f, 1.0f, 1.0f));
		}
		ApplyOutlines();
	}
}

void ATMBattleDirector::AddOutlineToCamera()
{
	if (bOutlineAdded || !Watcher)
	{
		return;
	}
	bOutlineAdded = true;
	UMaterialInterface* Outline = LoadObject<UMaterialInterface>(nullptr, OutlinePath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Outline)
	{
		UE_LOG(LogTemp, Log, TEXT("no team outline material at %s: units have no outline (Docs/TeamOutline.md)"), OutlinePath);
		return;
	}
	UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Outline, this);
	Mid->SetVectorParameterValue(TEXT("AllyColour"), OutlineSideColour(true));
	Mid->SetVectorParameterValue(TEXT("EnemyColour"), OutlineSideColour(false));
	// What is under the pointer or in the aim: red, as League of Legends marks it.
	Mid->SetVectorParameterValue(TEXT("HoverColour"), FLinearColor(1.0f, 0.08f, 0.05f, 1.0f));
	OutlineMid = Mid;
	if (UCameraComponent* Lens = Watcher->GetCameraComponent())
	{
		Lens->PostProcessSettings.AddBlendable(Mid, 1.0f);
	}
}

UTextureRenderTarget2D* ATMBattleDirector::PortraitOf(int32 Side, const TMSim::FUnit& Unit)
{
	TObjectPtr<USceneCaptureComponent2D>& Camera = Side == 0 ? PortraitCamera0 : PortraitCamera1;
	TObjectPtr<UTextureRenderTarget2D>& Film = Side == 0 ? PortraitFilm0 : PortraitFilm1;
	int32 Index = INDEX_NONE;
	for (int32 i = 0; i < static_cast<int32>(Battle.Units.size()); ++i)
	{
		if (Battle.Units[i].Id == Unit.Id)
		{
			Index = i;
		}
	}
	if (!UnitVisuals.IsValidIndex(Index) || !UnitVisuals[Index] || !Motions.IsValidIndex(Index))
	{
		return nullptr;
	}
	if (!Camera)
	{
		Film = NewObject<UTextureRenderTarget2D>(this);
		Film->RenderTargetFormat = RTF_RGBA8;
		Film->ClearColor = FLinearColor(0.02f, 0.03f, 0.05f, 1.0f);
		Film->InitAutoFormat(256, 256);
		Film->UpdateResourceImmediate(true);
		Camera = NewObject<USceneCaptureComponent2D>(this);
		Camera->SetupAttachment(RootComponent);
		Camera->RegisterComponent();
		Camera->TextureTarget = Film;
		Camera->bCaptureEveryFrame = false;
		Camera->bCaptureOnMovement = false;
		Camera->bAlwaysPersistRenderingState = true;
		Camera->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
		// Only the unit, on the panel's own dark ground.
		Camera->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
		Camera->ShowFlags.SetFog(false);
		Camera->ShowFlags.SetAtmosphere(false);
		Camera->ShowFlags.SetMotionBlur(false);
		Camera->FOVAngle = 28.0f;
	}
	FramePortrait(Camera, Index);
	// Deferred (2026-10-01): a capture made now, in the middle of a tick, flushes
	// every component's end-of-frame update first, and a burst effect that
	// finishes then destroys itself mid-flush -- the engine's assertion
	// "ComponentsThatNeedEndOfFrameUpdate_OnGameThread.IsValidIndex" (the
	// v13 play-test crash). Deferred, it is drawn with the frame instead.
	Camera->CaptureSceneDeferred();
	return Film;
}

void ATMBattleDirector::FramePortrait(USceneCaptureComponent2D* Camera, int32 Index)
{
	Camera->ShowOnlyComponents.Reset();
	Camera->ShowOnlyComponents.Add(UnitVisuals[Index]);
	// Face to face with it, a little above: head and shoulders.
	const FTMMotion& Motion = Motions[Index];
	const FVector Facing = FRotator(0.0f, Motion.Yaw, 0.0f).Vector();
	const FVector Head = Motion.Shown + FVector(0.0f, 0.0f, 150.0f * UnitSize);
	const FVector Eye = Head + Facing * 175.0f * UnitSize + FVector(0.0f, 0.0f, 20.0f * UnitSize);
	Camera->SetRelativeLocationAndRotation(Eye, (Head - Eye).Rotation());
}

UTextureRenderTarget2D* ATMBattleDirector::CardPortrait(const TMSim::FUnit& Unit) const
{
	for (int32 i = 0; i < static_cast<int32>(Battle.Units.size()) && i < CardFilms.Num(); ++i)
	{
		if (Battle.Units[i].Id == Unit.Id)
		{
			return CardFilms[i];
		}
	}
	return nullptr;
}

void ATMBattleDirector::AdvanceCardPortraits()
{
	// Every unit's face for the turn cards along the top, two a frame in turn:
	// a live picture of each, without eight cameras filming every frame.
	const int32 Count = FMath::Min(static_cast<int32>(Battle.Units.size()), FMath::Min(UnitVisuals.Num(), Motions.Num()));
	if (Count == 0 || Screen != EScreen::Battle)
	{
		return;
	}
	if (!CardCamera)
	{
		CardCamera = NewObject<USceneCaptureComponent2D>(this);
		CardCamera->SetupAttachment(RootComponent);
		CardCamera->RegisterComponent();
		CardCamera->bCaptureEveryFrame = false;
		CardCamera->bCaptureOnMovement = false;
		CardCamera->bAlwaysPersistRenderingState = true;
		CardCamera->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
		CardCamera->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
		CardCamera->ShowFlags.SetFog(false);
		CardCamera->ShowFlags.SetAtmosphere(false);
		CardCamera->ShowFlags.SetMotionBlur(false);
		CardCamera->FOVAngle = 28.0f;
	}
	while (CardFilms.Num() < Count)
	{
		UTextureRenderTarget2D* Film = NewObject<UTextureRenderTarget2D>(this);
		Film->RenderTargetFormat = RTF_RGBA8;
		Film->ClearColor = FLinearColor(0.02f, 0.03f, 0.05f, 1.0f);
		Film->InitAutoFormat(128, 128);
		Film->UpdateResourceImmediate(true);
		CardFilms.Add(Film);
	}
	// One card a frame: a deferred capture draws the camera as it is at the end
	// of the frame, so a second card set up now would take the first's place.
	{
		const int32 Index = CardNext++ % Count;
		if (UnitVisuals[Index] && CardFilms[Index])
		{
			CardCamera->TextureTarget = CardFilms[Index];
			FramePortrait(CardCamera, Index);
			// Deferred (2026-10-01): a capture made now, in the middle of a tick, flushes
			// every component's end-of-frame update first, and a burst effect that
			// finishes then destroys itself mid-flush -- the engine's assertion
			// "ComponentsThatNeedEndOfFrameUpdate_OnGameThread.IsValidIndex" (the
			// v13 play-test crash). Deferred, it is drawn with the frame instead.
			CardCamera->CaptureSceneDeferred();
		}
	}
}

void ATMBattleDirector::ClearLooks()
{
	for (TObjectPtr<UStaticMeshComponent>& Ring : TurnRings)
	{
		if (Ring)
		{
			Ring->DestroyComponent();
		}
	}
	TurnRings.Reset();
	TurnRingTargets.Reset();
	TurnRingPaint.Reset();
	TurnRingDrawn.Reset();
	CardFilms.Reset();
	CardNext = 0;
}
