// Copies every sound in the project out as a plain .wav, for the class creator.
//
// The creator runs in a browser, and a browser cannot open a SoundWave or a
// SoundCue: the audio inside is compressed for Unreal. So Unreal takes each one
// here, writes what it holds as an ordinary wave file, and lists them all. The
// creator plays the file when you audition a sound and when its sketch of an
// ability plays; the game plays the real asset.
//
// Asked for with -tmsoundcatalog (Tools\SoundCatalog.bat). It writes
// Saved/SoundCatalog/catalog.json and one .wav per sound, then exits. It needs
// the editor's copy of each sound, so it only works when started from the
// editor binary (UnrealEditor-Cmd), never from a packaged game.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "TMSoundStudio.generated.h"

class USoundWave;

UCLASS(Transient, NotPlaceable)
class TACTICALMASTERS_API ATMSoundStudio : public AActor
{
	GENERATED_BODY()

public:
	ATMSoundStudio();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Sounds copied a tick, so the log shows it moving. */
	static constexpr int32 PerTick = 8;
	/** The longest preview kept, in seconds: a music track is cut short, since
	 *  the creator only needs to hear how it starts. */
	static constexpr float LongestPreview = 12.0f;

private:
	struct FEntry
	{
		FString Path;      // the object path, /Game/Pack/SW_Thing.SW_Thing
		FString Name;      // SW_Thing
		FString Kind;      // wave or cue
		FString File;      // the .wav's file name ("" if it has no audio to copy)
		float Seconds = 0.0f;
		bool bLoops = false;
	};

	void FindSounds();
	void CopyOne(const FSoftObjectPath& Path);
	/** Writes Wave out as Name.wav; false if it holds nothing to write. */
	bool WriteWave(USoundWave* Wave, const FString& Name);
	/** A cue's wave found through what its package depends on, written out; its file name, or "". */
	FString WaveFileOfCue(const FSoftObjectPath& CuePath);
	void WriteCatalogue() const;

	TArray<FSoftObjectPath> ToCopy;
	TArray<FEntry> Copied;
	/** Waves already written, by object path to file name: a cue that plays a
	 *  wave shares its file. */
	TMap<FString, FString> Written;
	int32 Index = 0;
	bool bDone = false;
	FString OutDir;
};
