// What a battle sounds like (Content/Data/Sounds/sounds.json, written by
// Tools/assign_sounds.py): a sound as an ability goes off and one where it
// lands, as the Godot game plays them (scripts/battle/fx.gd:23-33); the
// hero's own voice as it strikes, is hurt, falls and wins; footsteps, a chime
// when one of this side's units is ready, a buzz when a turn is lost, and the
// clicks of the menus (audio.gd:126-128).
//
// Presentation only, like the damage numbers: nothing here reads back into
// the rules, and a battle is the same heard or silent. The dice here -- which
// take of a sound, a little pitch, whether a hit draws a grunt -- are the
// view's own (FMath), never the battle's.

#include "TMBattleDirector.h"

#include "Dom/JsonObject.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Sound/SoundBase.h"

#include "SimAbility.h"
#include "TMSettings.h"

namespace
{
	/** Seconds between footsteps at a walk (the Godot step tween's pace). */
	constexpr float StepSeconds = 0.36f;

	/** Where a unit is in the battle's list, which is also its body's place in Motions. */
	int32 IndexOfUnit(const TMSim::FBattle& Battle, int32 UnitId)
	{
		for (int32 i = 0; i < static_cast<int32>(Battle.Units.size()); ++i)
		{
			if (Battle.Units[i].Id == UnitId)
			{
				return i;
			}
		}
		return INDEX_NONE;
	}
}

void ATMBattleDirector::LoadSounds()
{
	if (bSoundsRead)
	{
		return;
	}
	bSoundsRead = true;
	const FString File = FPaths::ProjectContentDir() / TEXT("Data/Sounds/sounds.json");
	FString Text;
	TSharedPtr<FJsonObject> Root;
	if (!FFileHelper::LoadFileToString(Text, *File) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("no sound map at %s: the battle is silent"), *File);
		return;
	}
	auto Pairs = [](const TSharedPtr<FJsonObject>& From, TMap<FString, TPair<FString, FString>>& Into)
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : From->Values)
		{
			const TArray<TSharedPtr<FJsonValue>>& Two = Entry.Value->AsArray();
			if (Two.Num() == 2)
			{
				Into.Add(Entry.Key, TPair<FString, FString>(Two[0]->AsString(), Two[1]->AsString()));
			}
		}
	};
	const TSharedPtr<FJsonObject>* Section = nullptr;
	if (Root->TryGetObjectField(TEXT("sfx"), Section))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*Section)->Values)
		{
			TArray<FString>& Takes = SoundTakes.Add(Entry.Key);
			for (const TSharedPtr<FJsonValue>& Path : Entry.Value->AsArray())
			{
				Takes.Add(Path->AsString());
			}
		}
	}
	if (Root->TryGetObjectField(TEXT("motions"), Section))
	{
		Pairs(*Section, MotionSounds);
	}
	if (Root->TryGetObjectField(TEXT("abilities"), Section))
	{
		Pairs(*Section, AbilitySounds);
	}
	if (Root->TryGetObjectField(TEXT("events"), Section))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*Section)->Values)
		{
			EventSounds.Add(Entry.Key, Entry.Value->AsString());
		}
	}
	if (Root->TryGetObjectField(TEXT("voices"), Section))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Hero : (*Section)->Values)
		{
			TMap<FString, FString>& Roles = Voices.Add(Hero.Key);
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Line : Hero.Value->AsObject()->Values)
			{
				Roles.Add(Line.Key, Line.Value->AsString());
			}
		}
	}
	UE_LOG(LogTemp, Log, TEXT("sound map: %d sounds, %d heroes' voices"), SoundTakes.Num(), Voices.Num());
}

USoundBase* ATMBattleDirector::SoundAt(const FString& Path)
{
	if (Path.IsEmpty())
	{
		return nullptr;
	}
	if (TObjectPtr<USoundBase>* Loaded = LoadedSounds.Find(Path))
	{
		return *Loaded;
	}
	USoundBase* Sound = LoadObject<USoundBase>(nullptr, *Path);
	if (!Sound)
	{
		UE_LOG(LogTemp, Warning, TEXT("sound map: nothing at %s"), *Path);
	}
	LoadedSounds.Add(Path, Sound);
	return Sound;
}

bool ATMBattleDirector::CanSound() const
{
	// Only in a game world with someone to hear it: the editor steps battles by
	// hand, and an unattended run (a test) has nobody listening.
	return GetWorld() && GetWorld()->IsGameWorld() && !FApp::IsUnattended() && FApp::CanEverRenderAudio();
}

void ATMBattleDirector::PlaySound(const FString& Name, const FVector* At, float Volume)
{
	// At is a place on the board (as WorldFor gives it), or null for everywhere at once.
	if (Name.IsEmpty() || !CanSound())
	{
		return;
	}
	LoadSounds();
	const TArray<FString>* Takes = SoundTakes.Find(Name);
	if (!Takes || Takes->Num() == 0)
	{
		return;
	}
	USoundBase* Sound = SoundAt((*Takes)[FMath::RandRange(0, Takes->Num() - 1)]);
	if (!Sound)
	{
		return;
	}
	const float Loud = Volume * FTMSettings::Get().SfxVolume;
	// A little pitch either way, so a sound heard twice isn't the same twice (audio.gd:83).
	const float Pitch = FMath::FRandRange(0.94f, 1.06f);
	if (At)
	{
		// Places here are on the board; the world may have moved the board.
		UGameplayStatics::PlaySoundAtLocation(this, Sound, GetActorTransform().TransformPosition(*At), Loud, Pitch);
	}
	else
	{
		UGameplayStatics::PlaySound2D(this, Sound, Loud, Pitch);
	}
}

void ATMBattleDirector::PlayEventSound(const TCHAR* Event, const FVector* At, float Volume)
{
	LoadSounds();
	if (const FString* Name = EventSounds.Find(Event))
	{
		PlaySound(*Name, At, Volume);
	}
}

void ATMBattleDirector::PlayVoice(int32 Index, const TCHAR* Line, float Chance)
{
	if (!CanSound() || !Motions.IsValidIndex(Index) || !Motions[Index].Body || FMath::FRand() > Chance)
	{
		return;
	}
	LoadSounds();
	const TMap<FString, FString>* Roles = Voices.Find(Motions[Index].Body->SetName);
	const FString* Path = Roles ? Roles->Find(Line) : nullptr;
	USoundBase* Sound = Path ? SoundAt(*Path) : nullptr;
	if (Sound && IsSeen(Battle.Units[Index]))
	{
		UGameplayStatics::PlaySoundAtLocation(this, Sound,
			GetActorTransform().TransformPosition(WorldFor(Battle.Units[Index]) + FVector(0.0f, 0.0f, 150.0f)), FTMSettings::Get().VoiceVolume);
	}
}

TPair<FString, FString> ATMBattleDirector::SoundsOf(const TMSim::FAbility* Ability, const FString& Motion)
{
	LoadSounds();
	if (Ability)
	{
		if (const TPair<FString, FString>* Own = AbilitySounds.Find(UTF8_TO_TCHAR(Ability->Id.c_str())))
		{
			return *Own;
		}
	}
	if (const TPair<FString, FString>* ByMotion = MotionSounds.Find(Motion))
	{
		return *ByMotion;
	}
	return TPair<FString, FString>();
}

void ATMBattleDirector::SoundBlowStarts(const FTMBlow& Blow, int32 Slot)
{
	const int32 Caster = IndexOfUnit(Battle, Blow.Caster);
	if (!Motions.IsValidIndex(Caster) || !IsSeen(Battle.Units[Caster]) || Blow.Motion == TEXT("none"))
	{
		return;
	}
	const FVector Where = WorldFor(Battle.Units[Caster]) + FVector(0.0f, 0.0f, 100.0f);
	PlaySound(SoundsOf(Blow.Ability, Blow.Motion).Key, &Where);
	// The hero's own effort with it: its primary, Q, E or ultimate by slot.
	static const TCHAR* BySlot[4] = { TEXT("primary"), TEXT("q"), TEXT("e"), TEXT("ultimate") };
	PlayVoice(Caster, BySlot[FMath::Clamp(Slot, 0, 3)], Slot == 3 ? 1.0f : 0.6f);
}

void ATMBattleDirector::SoundBlowLands(const FTMBlow& Blow)
{
	if (Blow.Motion == TEXT("none"))
	{
		return;
	}
	// Where it lands: on the first one it touched, or the spot aimed at.
	FVector Where = WorldFromMetres(Blow.Aim, Battle.Map.NodeLevel(TMSim::FMap::NodeOf(Blow.Aim))) + FVector(0.0f, 0.0f, 80.0f);
	for (const TMSim::FEvent& Event : Blow.Events)
	{
		if (Event.Kind == TMSim::EEventKind::Hit || Event.Kind == TMSim::EEventKind::Evaded || Event.Kind == TMSim::EEventKind::Absorbed)
		{
			const int32 Struck = IndexOfUnit(Battle, Event.Unit);
			if (Motions.IsValidIndex(Struck))
			{
				Where = WorldFor(Battle.Units[Struck]) + FVector(0.0f, 0.0f, 100.0f);
			}
			break;
		}
	}
	if (IsPointSeen(Blow.Aim))
	{
		PlaySound(SoundsOf(Blow.Ability, Blow.Motion).Value, &Where);
	}
}

void ATMBattleDirector::SoundStep(int32 Index, float DeltaSeconds)
{
	FTMMotion& Motion = Motions[Index];
	Motion.StepClock += DeltaSeconds * (Motion.bRun ? 1.4f : 1.0f);
	if (Motion.StepClock >= StepSeconds)
	{
		Motion.StepClock = 0.0f;
		if (IsSeen(Battle.Units[Index]))
		{
			PlayEventSound(TEXT("step"), &Motion.Shown, 0.35f);
		}
	}
}
