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
//
// For Cast Studio (Docs/CastStudio-Plan.md, catalogue version 2), so the
// creator can compose an ability with Unreal closed:
// - one fixed camera for every body, written to the catalogue, so a point in
//   centimetres can be found on any strip and an effect drawn there at its size;
// - 32 frames a second, laid out 16 to a row so no picture is too wide;
// - where the hands, head, weapon, root and pelvis are on every frame
//   (<sheet>.tracks.json), and how far the root drifts;
// - every clip on each body's skeleton listed (its "library"), and the clips
//   Cast Studio's published picks name filmed too;
// - with -tmanimwanted=<file>, only the clips listed there, added beside the
//   catalogue (Tools\AnimClips.bat): the creator's "Film the picked clips".

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
	static constexpr int32 FramesPerSecond = 32;
	/** At most three seconds of a clip; most are shorter. */
	static constexpr int32 MaxFrames = 96;
	/** Frames to a row of a strip: 16 x 256 pixels across at most. */
	static constexpr int32 Columns = 16;
	/** The small copy keeps every other frame, in one row. */
	static constexpr int32 ThumbEvery = 2;
	/** The camera, the same for every body (written to the catalogue). */
	static constexpr float CameraFov = 40.0f;
	static constexpr float CameraPitch = -10.0f;
	static constexpr float CameraDistance = 460.0f;
	static constexpr float LookHeight = 95.0f;

private:
	struct FJob
	{
		FString Body;
		USkeletalMesh* Mesh = nullptr;
		float Yaw = 0.0f;
		UAnimSequence* Clip = nullptr;
		float Scale = 1.0f;
		/** Whether its cloth is simulated, as the game draws it (FTMAnimSet::bCloth). */
		bool bCloth = true;
	};

	struct FFilmed
	{
		FString Id;        // body/clip
		FString Body;
		FString Path;
		FString Name;
		FString Sheet;
		FString Thumb;
		FString Tracks;
		int32 Frames = 0;
		float Seconds = 0.0f;
		/** How far the root has moved over the ground by the last frame, and at most, in cm. */
		float Drift = 0.0f;
		float MostDrift = 0.0f;
	};

	/** The points followed on every frame, by name: hand_r, hand_l, head, weapon, root, pelvis. */
	static const TArray<FString>& TrackNames();
	/** The bone or socket each point is on this mesh, or none (WhereOf). */
	FName PointOn(const FString& Track) const;
	/** Where the points are on every frame so far, in cm from the feet, the tracks of a frame together. */
	TArray<FVector> Points;
	/** The bone or socket each track follows on the mesh being filmed (PointOn), or none. */
	TArray<FName> TrackOn;

	void Shoot(TArray<FColor>& Pixels);
	/** Where each track's point is now, relative to where the body stands. */
	void Measure();
	void SaveTracks();
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
	/**
	 * -tmanimonly=a,b,c: film only the bodies of these animation sets, into
	 * catalog-<first>.json beside the others, for Tools\AnimCatalog.bat to merge.
	 * Thirty heroes' clips at once are more than the editor build can hold.
	 */
	TSet<FString> Only;
	FString CatalogName = TEXT("catalog.json");
	/** -tmanimwanted: only the listed clips, and only a list of them is written. */
	bool bWanted = false;
	/** Each body's camera yaw (-tmanimyaw), written so the creator can place points. */
	float Around = 30.0f;
	bool bDone = true;
};
