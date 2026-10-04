#include "TMSoundStudio.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Audio.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Sound/SoundCue.h"
#include "Sound/SoundNodeWavePlayer.h"
#include "Sound/SoundWave.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	FString SoundEscaped(const FString& Text)
	{
		return Text.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\""));
	}

	/** The package path made into a file name, so two packs' SW_Hit do not collide. */
	FString SoundFileOf(const FSoftObjectPath& Path)
	{
		return Path.GetLongPackageName().Mid(6).Replace(TEXT("/"), TEXT("__")) + TEXT(".wav");
	}
}

ATMSoundStudio::ATMSoundStudio()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ATMSoundStudio::BeginPlay()
{
	Super::BeginPlay();

	OutDir = FPaths::ProjectSavedDir() / TEXT("SoundCatalog");
	// A fresh catalogue each time: a sound removed from the project must not
	// linger in the creator as one it can still pick.
	IFileManager::Get().DeleteDirectory(*OutDir, false, true);
	IFileManager::Get().MakeDirectory(*OutDir, true);

	FindSounds();
	UE_LOG(LogTemp, Log, TEXT("SOUND STUDIO: %d sounds to copy"), ToCopy.Num());
#if !WITH_EDITOR
	UE_LOG(LogTemp, Warning, TEXT("SOUND STUDIO: a packaged game holds no editor audio; start it from UnrealEditor-Cmd (Tools\\SoundCatalog.bat)"));
#endif
}

void ATMSoundStudio::FindSounds()
{
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	// A game run started from the editor binary may still be scanning.
	Registry.SearchAllAssets(true);

	FARFilter Filter;
	Filter.PackagePaths.Add(FName(TEXT("/Game")));
	Filter.bRecursivePaths = true;
	Filter.ClassPaths.Add(USoundWave::StaticClass()->GetClassPathName());
	Filter.ClassPaths.Add(USoundCue::StaticClass()->GetClassPathName());
	TArray<FAssetData> Found;
	Registry.GetAssets(Filter, Found);
	for (const FAssetData& Asset : Found)
	{
		ToCopy.Add(Asset.GetSoftObjectPath());
	}
	// Alphabetical, so the catalogue reads the same from one run to the next.
	ToCopy.Sort([](const FSoftObjectPath& A, const FSoftObjectPath& B) { return A.ToString() < B.ToString(); });
}

void ATMSoundStudio::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bDone)
	{
		return;
	}
	for (int32 Step = 0; Step < PerTick && Index < ToCopy.Num(); ++Step, ++Index)
	{
		CopyOne(ToCopy[Index]);
	}
	UE_LOG(LogTemp, Log, TEXT("SOUND STUDIO: %d/%d "), Index, ToCopy.Num());
	// A few thousand sounds' audio held at once is gigabytes: let it go now and then.
	if (Index % 256 < PerTick)
	{
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	}
	if (Index >= ToCopy.Num())
	{
		WriteCatalogue();
		int32 Playable = 0;
		for (const FEntry& Entry : Copied)
		{
			Playable += Entry.File.IsEmpty() ? 0 : 1;
		}
		UE_LOG(LogTemp, Log, TEXT("SOUND STUDIO DONE: %d of %d sounds copied -> %s"), Playable, ToCopy.Num(), *OutDir);
		bDone = true;
		FPlatformMisc::RequestExit(false);
	}
}

void ATMSoundStudio::CopyOne(const FSoftObjectPath& Path)
{
	UObject* Loaded = Path.TryLoad();
	if (!Loaded)
	{
		UE_LOG(LogTemp, Warning, TEXT("SOUND STUDIO: could not load %s"), *Path.ToString());
		return;
	}
	FEntry Entry;
	Entry.Path = Path.ToString();
	Entry.Name = Path.GetAssetName();
	if (USoundWave* Wave = Cast<USoundWave>(Loaded))
	{
		Entry.Kind = TEXT("wave");
		Entry.Seconds = Wave->GetDuration();
		Entry.bLoops = Wave->bLooping;
		if (const FString* Already = Written.Find(Entry.Path))
		{
			Entry.File = *Already;
		}
		else if (WriteWave(Wave, SoundFileOf(Path)))
		{
			Entry.File = SoundFileOf(Path);
			Written.Add(Entry.Path, Entry.File);
		}
	}
	else if (USoundCue* Cue = Cast<USoundCue>(Loaded))
	{
		Entry.Kind = TEXT("cue");
		Entry.Seconds = Cue->GetDuration();
		// A cue picks among its waves, mixes or modulates them; the creator plays
		// the first one it holds, which is what the cue sounds like near enough.
		TArray<USoundNodeWavePlayer*> Players;
		Cue->RecursiveFindNode<USoundNodeWavePlayer>(Cue->FirstNode, Players);
		for (USoundNodeWavePlayer* Player : Players)
		{
			USoundWave* Played = Player ? Player->GetSoundWave() : nullptr;
			if (!Played)
			{
				continue;
			}
			const FSoftObjectPath WavePath(Played);
			if (const FString* Already = Written.Find(WavePath.ToString()))
			{
				Entry.File = *Already;
				break;
			}
			if (WriteWave(Played, SoundFileOf(WavePath)))
			{
				Entry.File = SoundFileOf(WavePath);
				Written.Add(WavePath.ToString(), Entry.File);
				break;
			}
		}
		// Many cues hold their waves only by name until they are played (a wave
		// player loads its wave when the cue starts, and a dialogue player goes
		// through a DialogueWave first), so the nodes above often have nothing
		// loaded. Then the wave is found the way the cue's package names it: the
		// first SoundWave among what it depends on, two steps out at most.
		if (Entry.File.IsEmpty())
		{
			Entry.File = WaveFileOfCue(Path);
		}
		// A cue that loops says it lasts for ever (10000 s, Unreal's word for
		// that): it is listed as looping, with no length.
		Entry.bLoops = Cue->IsLooping() || Entry.Seconds >= 10000.0f;
		if (Entry.Seconds >= 10000.0f)
		{
			Entry.Seconds = 0.0f;
		}
	}
	else
	{
		return;
	}
	Copied.Add(Entry);
}

FString ATMSoundStudio::WaveFileOfCue(const FSoftObjectPath& CuePath)
{
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	TArray<FName> Ring;
	Ring.Add(CuePath.GetLongPackageFName());
	TSet<FName> Seen;
	Seen.Add(Ring[0]);
	for (int32 Step = 0; Step < 2 && Ring.Num() > 0; ++Step)
	{
		TArray<FName> Next;
		for (const FName& Package : Ring)
		{
			TArray<FName> Depends;
			Registry.GetDependencies(Package, Depends, UE::AssetRegistry::EDependencyCategory::Package);
			// The same wave each run, whatever order the registry gives them in.
			Depends.Sort(FNameLexicalLess());
			for (const FName& Depend : Depends)
			{
				if (Seen.Contains(Depend) || !Depend.ToString().StartsWith(TEXT("/Game/")))
				{
					continue;
				}
				Seen.Add(Depend);
				Next.Add(Depend);
				TArray<FAssetData> Assets;
				Registry.GetAssetsByPackageName(Depend, Assets);
				for (const FAssetData& Asset : Assets)
				{
					if (Asset.AssetClassPath != USoundWave::StaticClass()->GetClassPathName())
					{
						continue;
					}
					const FSoftObjectPath WavePath = Asset.GetSoftObjectPath();
					if (const FString* Already = Written.Find(WavePath.ToString()))
					{
						return *Already;
					}
					USoundWave* Found = Cast<USoundWave>(WavePath.TryLoad());
					if (Found && WriteWave(Found, SoundFileOf(WavePath)))
					{
						Written.Add(WavePath.ToString(), SoundFileOf(WavePath));
						return SoundFileOf(WavePath);
					}
				}
			}
		}
		Ring = MoveTemp(Next);
	}
	return FString();
}

bool ATMSoundStudio::WriteWave(USoundWave* Wave, const FString& Name)
{
#if WITH_EDITOR
	TArray<uint8> Pcm;
	uint32 SampleRate = 0;
	uint16 Channels = 0;
	if (!Wave->GetImportedSoundWaveData(Pcm, SampleRate, Channels) || Pcm.Num() == 0 || SampleRate == 0 || Channels == 0)
	{
		return false;
	}
	// Sixteen-bit samples, every channel side by side; a long one is cut short
	// on a whole frame.
	const int32 FrameBytes = 2 * Channels;
	const int32 Longest = static_cast<int32>(LongestPreview * SampleRate) * FrameBytes;
	const int32 Bytes = FMath::Min(Pcm.Num() - Pcm.Num() % FrameBytes, Longest);
	TArray<uint8> File;
	SerializeWaveFile(File, Pcm.GetData(), Bytes, Channels, static_cast<int32>(SampleRate));
	return FFileHelper::SaveArrayToFile(File, *(OutDir / Name));
#else
	return false;
#endif
}

void ATMSoundStudio::WriteCatalogue() const
{
	// Read by the class creator (app/sound.mjs there). Hand-written rather than
	// through the Json module: it is one flat list.
	FString Json = FString::Printf(
		TEXT("{\n  \"format\": \"tactical-masters-sound-catalog\",\n  \"version\": 1,\n  \"made\": \"%s\",\n  \"sounds\": ["),
		*FDateTime::UtcNow().ToIso8601());
	for (int32 i = 0; i < Copied.Num(); ++i)
	{
		const FEntry& Entry = Copied[i];
		// The pack is the first folder under Content, which is where Fab puts one.
		FString Pack = Entry.Path.Mid(6);
		Pack.Split(TEXT("/"), &Pack, nullptr);
		Json += FString::Printf(
			TEXT("%s\n    {\"path\": \"%s\", \"name\": \"%s\", \"pack\": \"%s\", \"kind\": \"%s\", \"seconds\": %.3f, \"loops\": %s, \"file\": \"%s\"}"),
			i ? TEXT(",") : TEXT(""), *SoundEscaped(Entry.Path), *SoundEscaped(Entry.Name), *SoundEscaped(Pack),
			*Entry.Kind, Entry.Seconds, Entry.bLoops ? TEXT("true") : TEXT("false"), *SoundEscaped(Entry.File));
	}
	Json += TEXT("\n  ]\n}\n");
	FFileHelper::SaveStringToFile(Json, *(OutDir / TEXT("catalog.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
