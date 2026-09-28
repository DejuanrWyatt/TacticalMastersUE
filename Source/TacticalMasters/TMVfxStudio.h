// Films every particle effect in the project, for the class creator.
//
// The class creator runs in a browser, and a browser cannot play a Niagara or
// Cascade system: only Unreal can. So Unreal plays each one here, alone in
// front of a camera, and saves what it saw as a strip of frames. The creator
// shows the strip as a looping picture and lets an ability name the effect; the
// game then plays the real thing when that ability goes off.
//
// Asked for with -tmvfxcatalog (Tools\VfxCatalog.bat). It writes
// Saved/VfxCatalog/catalog.json and one PNG strip per effect, then exits.
// Nothing here touches a battle.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "TMVfxStudio.generated.h"

class UFXSystemComponent;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;

UCLASS(Transient, NotPlaceable)
class TACTICALMASTERS_API ATMVfxStudio : public AActor
{
	GENERATED_BODY()

public:
	ATMVfxStudio();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** The size of one frame, in pixels. Square. */
	static constexpr int32 FrameSize = 256;
	/** The size of a frame in the small copy the creator lists effects with. */
	static constexpr int32 ThumbSize = 96;
	/** Frames a second, and how many: two seconds of each effect. */
	static constexpr int32 FramesPerSecond = 16;
	static constexpr int32 FrameCount = 32;
	/** How long an effect plays before it is filmed, to find how big it gets. */
	static constexpr float MeasureSeconds = 2.0f;
	/** The camera's field of view, in degrees. */
	static constexpr float Fov = 50.0f;

private:
	struct FEntry
	{
		FString Path;      // the object path, /Game/Pack/NS_Thing.NS_Thing
		FString Name;      // NS_Thing
		FString Kind;      // niagara or cascade
		FString Sheet;     // the PNG strip's file name
		FString Thumb;     // and its small copy's
		float Radius = 0.0f;   // half the width of the picture, in centimetres
		bool bLoops = false;
		bool bVisible = false;
	};

	enum class EPhase : uint8 { Next, Measure, Film, Done };

	void FindEffects();
	UFXSystemComponent* Play(UObject* System);
	void StopPlaying();
	void AimAtReach();
	void Place();
	bool Shoot(TArray<FColor>& Pixels);
	void LookAtFrame();
	void Refine();
	void Record();
	void TakeFrame();
	void SaveStrip();
	void WriteCatalogue() const;

	UPROPERTY()
	TObjectPtr<USceneCaptureComponent2D> Camera = nullptr;

	UPROPERTY()
	TObjectPtr<UTextureRenderTarget2D> Film = nullptr;

	UPROPERTY()
	TObjectPtr<class UPointLightComponent> Lamp = nullptr;

	UPROPERTY()
	TObjectPtr<UFXSystemComponent> Playing = nullptr;

	UPROPERTY()
	TObjectPtr<UObject> Current = nullptr;

	/** Every effect found, by object path, and those filmed so far. */
	TArray<FSoftObjectPath> ToFilm;
	TArray<FEntry> Filmed;
	int32 Index = -1;

	EPhase Phase = EPhase::Next;
	float Elapsed = 0.0f;
	FBox Reach;
	/** Where the camera looks and how far back it stands. */
	FVector Middle = FVector::ZeroVector;
	float Distance = 0.0f;
	bool bAimed = false;
	/** The corners of everything the camera has seen of the effect, in pixels. */
	FIntPoint SeenLow;
	FIntPoint SeenHigh;
	int32 FramesTaken = 0;
	TArray<FColor> Strip;
	FString OutDir;
};
