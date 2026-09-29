// Films every animation clip the character map uses, on the body that plays it,
// for the class creator.
//
// A browser cannot play an Unreal animation on an Unreal mesh, so, as with the
// particle effects (TMVfxStudio.h), Unreal plays each clip here in front of a
// camera and saves what it saw as a strip of frames. The creator plays the
// strips to show how each ability moves a class's body, and lets an ability
// choose its motion.
//
// It films through the director's own reading of the character map, so what
// the creator shows is what a battle plays: the same bodies, the same clips,
// and the same stand-ins where a set lacks a motion.
//
// Asked for with -tmanimcatalog (Tools\AnimCatalog.bat). It writes
// Saved/AnimCatalog/catalog.json and one PNG strip per clip and body, then
// exits. Nothing here touches a battle.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "TMAnimStudio.generated.h"

class ATMBattleDirector;
class UAnimSequence;
class USceneCaptureComponent2D;
class USkeletalMesh;
class USkeletalMeshComponent;
class UTextureRenderTarget2D;

UCLASS(Transient, NotPlaceable)
class TACTICALMASTERS_API ATMAnimStudio : public AActor
{
	GENERATED_BODY()

public:
	ATMAnimStudio();

	/** Films the bodies and clips the director read from the character map. */
	void Begin(const ATMBattleDirector* Director);
	virtual void Tick(float DeltaSeconds) override;

	/** The size of one frame, in pixels. Square. */
	static constexpr int32 FrameSize = 256;
	/** The size of a frame in the small copy the creator lists clips with. */
	static constexpr int32 ThumbSize = 96;
	static constexpr int32 FramesPerSecond = 16;
	/** At most three seconds of a clip; most are shorter. */
	static constexpr int32 MaxFrames = 48;

private:
	struct FJob
	{
		FString Body;
		USkeletalMesh* Mesh = nullptr;
		float Yaw = 0.0f;
		UAnimSequence* Clip = nullptr;
		float Scale = 1.0f;
	};

	struct FFilmed
	{
		FString Id;        // body/clip
		FString Body;
		FString Path;
		FString Name;
		FString Sheet;
		FString Thumb;
		int32 Frames = 0;
		float Seconds = 0.0f;
	};

	void Shoot(TArray<FColor>& Pixels);
	void SaveStrip();
	void WriteCatalogue() const;
	/** The id a clip is filmed under for a body, or empty for none. */
	static FString IdOf(const FString& Body, const UAnimSequence* Clip);

	const ATMBattleDirector* Director = nullptr;

	UPROPERTY()
	TObjectPtr<USceneCaptureComponent2D> Camera = nullptr;
	UPROPERTY()
	TObjectPtr<UTextureRenderTarget2D> Film = nullptr;
	UPROPERTY()
	TObjectPtr<USkeletalMeshComponent> Actor = nullptr;
	UPROPERTY()
	TArray<TObjectPtr<class UPointLightComponent>> Lamps;

	TArray<FJob> Jobs;
	TArray<FFilmed> Filmed;
	int32 Index = -1;
	/** The frame the pose was set to last tick, which this tick's picture shows. */
	int32 Frame = -1;
	int32 FrameCount = 0;
	/** Ticks left before the pose is drawn and can be filmed. */
	int32 Settle = 0;
	static constexpr int32 SettleTicks = 2;
	TArray<FColor> Strip;
	FString OutDir;
	bool bDone = true;
};
