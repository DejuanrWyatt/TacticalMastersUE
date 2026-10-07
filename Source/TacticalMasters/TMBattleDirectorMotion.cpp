// Bodies and animation: what each unit wears, and how it moves on screen.
//
// A unit walks along its path, swings when it uses an ability, flinches when
// it is hit, falls when it is knocked out and stands again when it is raised --
// all read from what the rules say happened, never feeding back into them. The
// clips are played straight on each unit's mesh, one at a time, so no Animation
// Blueprint is needed to see a battle move; one can take over later without
// anything here changing what the rules do (Docs/CharacterSetup.md).
//
// Which body each class wears, and which clip means what, is data:
// Content/Data/CharacterMap/characters.json. A clip must be authored for the
// skeleton of the body that plays it; a body whose set does not fit simply
// stands still, as every unit did before this.

#include "TMBattleDirector.h"

#include "Animation/AnimSequence.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Dom/JsonObject.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include "SimAbility.h"
#include "SimMap.h"

#include <type_traits>

namespace
{
	/** Degrees a unit turns a second, walking or turning to face a target. */
	constexpr float TurnRate = 540.0f;
	/** A walk longer than this, in metres, is run rather than walked. */
	constexpr float RunFrom = 3.5f;

	/**
	 * A walk from From along Path with its corners cut (Chaikin's corner
	 * cutting, twice), so a unit walks a curve instead of turning on each node.
	 * Only where both ends of a stretch are at one height: a step up or down a
	 * level is walked as it was, so no corner is cut across a cliff. The end
	 * stays exactly where the rules put the unit.
	 */
	TArray<FVector> SmoothedWalk(const FVector& From, const TArray<FVector>& Path)
	{
		if (Path.Num() < 2)
		{
			return Path;
		}
		TArray<FVector> Points;
		Points.Add(From);
		Points.Append(Path);
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			TArray<FVector> Cut;
			Cut.Add(Points[0]);
			const int32 Last = Points.Num() - 1;
			for (int32 k = 0; k < Last; ++k)
			{
				const FVector& A = Points[k];
				const FVector& B = Points[k + 1];
				if (FMath::Abs(A.Z - B.Z) > 1.0f)
				{
					Cut.Add(A);
					Cut.Add(B);
					continue;
				}
				if (k > 0)
				{
					Cut.Add(FMath::Lerp(A, B, 0.25f));
				}
				Cut.Add(k + 1 < Last ? FMath::Lerp(A, B, 0.75f) : B);
			}
			Points = MoveTemp(Cut);
		}
		Points.RemoveAt(0);
		return Points;
	}

	float YawOf(const TMSim::FVec2& Facing)
	{
		return FMath::RadiansToDegrees(FMath::Atan2(Facing.Y, Facing.X));
	}

	template <typename T>
	T* LoadNamed(const FString& Path, TArray<TObjectPtr<UObject>>& Keep)
	{
		if (Path.IsEmpty())
		{
			return nullptr;
		}
		T* Found = LoadObject<T>(nullptr, *Path);
		if (!Found)
		{
			UE_LOG(LogTemp, Warning, TEXT("character map: nothing at %s"), *Path);
			return nullptr;
		}
		// A clip made to be layered over another pose cannot play on its own.
		if constexpr (std::is_same_v<T, UAnimSequence>)
		{
			if (Found->IsValidAdditive())
			{
				UE_LOG(LogTemp, Warning, TEXT("character map: %s is additive, made to be layered, and cannot play alone; left out"), *Path);
				return nullptr;
			}
		}
		Keep.Add(Found);
		return Found;
	}
}

bool ATMBattleDirector::LoadCharacterMap()
{
	if (bCharacterMapRead)
	{
		return Bodies.Num() > 0;
	}
	bCharacterMapRead = true;
	const FString File = FPaths::ProjectContentDir() / TEXT("Data/CharacterMap/characters.json");
	FString Text;
	TSharedPtr<FJsonObject> Root;
	if (!FFileHelper::LoadFileToString(Text, *File)
		|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid()
		|| Root->GetStringField(TEXT("format")) != TEXT("tactical-masters-characters"))
	{
		UE_LOG(LogTemp, Warning, TEXT("no character map at %s: every unit wears the level's mesh, standing still"), *File);
		return false;
	}

	// The animation sets as written; each is loaded when first worn (SetOf).
	const TSharedPtr<FJsonObject>* Sets = nullptr;
	if (Root->TryGetObjectField(TEXT("animations"), Sets))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*Sets)->Values)
		{
			const TSharedPtr<FJsonObject> Set = Entry.Value->AsObject();
			if (Set.IsValid())
			{
				SetSources.Add(Entry.Key, Set);
			}
		}
	}
	AnimSets.Reserve(SetSources.Num());

	// "bodyScale": a size by body, or by animation set so a hero's skins share it.
	const TSharedPtr<FJsonObject>* Scales = nullptr;
	Root->TryGetObjectField(TEXT("bodyScale"), Scales);
	const TSharedPtr<FJsonObject>* BodyList = nullptr;
	if (Root->TryGetObjectField(TEXT("bodies"), BodyList))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*BodyList)->Values)
		{
			const TSharedPtr<FJsonObject> Body = Entry.Value->AsObject();
			if (!Body.IsValid())
			{
				continue;
			}
			FTMBody Out;
			Body->TryGetStringField(TEXT("mesh"), Out.MeshPath);
			double Yaw = 0.0;
			Body->TryGetNumberField(TEXT("yaw"), Yaw);
			Out.Yaw = static_cast<float>(Yaw);
			Body->TryGetStringField(TEXT("animations"), Out.SetName);
			// Its set comes with its mesh, when a unit first wears it (MeshOf).
			double Scale = 1.0;
			if (Scales && ((*Scales)->TryGetNumberField(Entry.Key, Scale) || (*Scales)->TryGetNumberField(Out.SetName, Scale)) && Scale > 0.0)
			{
				Out.Scale = static_cast<float>(Scale);
			}
			// Only a body whose mesh is in the project counts; it is loaded
			// when a unit first wears it.
			if (!Out.MeshPath.IsEmpty() && FPackageName::DoesPackageExist(FSoftObjectPath(Out.MeshPath).GetLongPackageName()))
			{
				Bodies.Add(Entry.Key, Out);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("character map: body %s has no mesh at %s"), *Entry.Key, *Out.MeshPath);
			}
		}
	}
	auto Names = [&Root](const TCHAR* Field, TMap<FString, FString>& Into)
	{
		const TSharedPtr<FJsonObject>* List = nullptr;
		if (Root->TryGetObjectField(Field, List))
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*List)->Values)
			{
				Into.Add(Entry.Key, Entry.Value->AsString());
			}
		}
	};
	Names(TEXT("looks"), LookBodies);
	Names(TEXT("classes"), ClassBodies);
	Root->TryGetStringField(TEXT("default"), DefaultBody);
	UE_LOG(LogTemp, Log, TEXT("character map: %d bodies, %d animation sets"), Bodies.Num(), SetSources.Num());
	return Bodies.Num() > 0;
}

void ATMBattleDirector::LoadCastAnimation()
{
	// Written by the class creator's Publish (Cast Studio). Read again only when
	// it changes, so a battle started after a Publish plays the new picks
	// without restarting the game.
	const FString File = FPaths::ProjectContentDir() / TEXT("Data/CastStudio/AbilityAnimation.json");
	const FDateTime Time = IFileManager::Get().GetTimeStamp(*File);
	if (Time == CastAnimationTime)
	{
		return;
	}
	CastAnimationTime = Time;
	CastAnimation = TMCast::FAnimationFile();
	CastWarned.Reset();
	FString Text;
	if (Time == FDateTime::MinValue() || !FFileHelper::LoadFileToString(Text, *File))
	{
		UE_LOG(LogTemp, Log, TEXT("Cast Studio: no animation picks published; every ability moves as the character map says"));
		return;
	}
	const bool bRead = TMCast::ReadAnimationFile(TCHAR_TO_UTF8(*Text), CastAnimation);
	for (const std::string& Problem : CastAnimation.Problems)
	{
		UE_LOG(LogTemp, Warning, TEXT("Cast Studio: %s: %hs"), *File, Problem.c_str());
	}
	UE_LOG(LogTemp, Log, TEXT("Cast Studio: %s animation picks for %d abilities"), bRead ? TEXT("read") : TEXT("could not read"),
		static_cast<int32>(CastAnimation.Abilities.size()));
}

ATMBattleDirector::FTMCastClips ATMBattleDirector::CastClipsFor(const FTMBody& Body, const TMSim::FAbility& Ability)
{
	FTMCastClips Out;
	const TMCast::FAnimationPicks* Picks = CastAnimation.Find(Ability.Id, TCHAR_TO_UTF8(*Body.SetName));
	if (!Picks)
	{
		return Out;
	}
	USkeletalMesh* Mesh = MeshOf(Body);
	const USkeleton* Skeleton = Mesh ? Mesh->GetSkeleton() : nullptr;
	// A clip is made for one skeleton. Played on another it bends the body into
	// nonsense, so it is left out -- and said, once, never quietly replaced.
	auto Load = [&](const TMCast::FClipPick& Pick, const TCHAR* Part) -> UAnimSequence*
	{
		if (Pick.Clip.empty())
		{
			return nullptr;
		}
		const FString Path = UTF8_TO_TCHAR(Pick.Clip.c_str());
		UAnimSequence* Clip = LoadNamed<UAnimSequence>(Path, CharacterAssets);
		FString Why;
		if (!Clip)
		{
			Why = TEXT("there is no playable clip at that path");
		}
		else if (Skeleton && Clip->GetSkeleton() != Skeleton)
		{
			Why = FString::Printf(TEXT("it is made for %s, and the body is %s"),
				Clip->GetSkeleton() ? *Clip->GetSkeleton()->GetName() : TEXT("no skeleton"), *Skeleton->GetName());
			Clip = nullptr;
		}
		const FString Key = FString::Printf(TEXT("%hs/%s/%s"), Ability.Id.c_str(), *Body.SetName, Part);
		if (!Why.IsEmpty() && !CastWarned.Contains(Key))
		{
			CastWarned.Add(Key);
			UE_LOG(LogTemp, Warning, TEXT("Cast Studio: %hs's %s on %s can't play: %s (%s). Today's clip plays instead."),
				Ability.Id.c_str(), Part, *Body.SetName, *Why, *Path);
		}
		return Clip;
	};
	Out.Windup = Load(Picks->Windup, TEXT("wind-up"));
	Out.Release = Load(Picks->Release, TEXT("release"));
	Out.Loop = Load(Picks->Loop, TEXT("loop"));
	Out.Recover = Load(Picks->Recover, TEXT("recover"));
	Out.Contact = static_cast<float>(Picks->Release.Contact);
	return Out;
}

USkeletalMesh* ATMBattleDirector::MeshOf(const FTMBody& Body)
{
	if (Body.Mesh && Body.Animations)
	{
		return Body.Mesh;
	}
	const double Began = FPlatformTime::Seconds();
	if (!Body.Mesh)
	{
		Body.Mesh = LoadNamed<USkeletalMesh>(Body.MeshPath, CharacterAssets);
	}
	const double MeshDone = FPlatformTime::Seconds();
	if (Body.Mesh && !Body.Animations)
	{
		Body.Animations = SetOf(Body.SetName);
	}
	UE_LOG(LogTemp, Log, TEXT("body %s: mesh %.0f ms, clips %.0f ms"), *Body.SetName,
		(MeshDone - Began) * 1000.0, (FPlatformTime::Seconds() - MeshDone) * 1000.0);
	return Body.Mesh;
}

const ATMBattleDirector::FTMAnimSet* ATMBattleDirector::SetOf(const FString& Name)
{
	if (const FTMAnimSet* Loaded = AnimSets.Find(Name))
	{
		return Loaded;
	}
	const TSharedPtr<FJsonObject>* Source = SetSources.Find(Name);
	if (!Source)
	{
		return nullptr;
	}
	FTMAnimSet& Out = AnimSets.Add(Name);
	BuildAnimSet(**Source, Out);
	return &Out;
}

void ATMBattleDirector::BuildAnimSet(const FJsonObject& Set, FTMAnimSet& Out)
{
	auto One = [&](const TCHAR* Key) { FString Path; Set.TryGetStringField(Key, Path); return LoadNamed<UAnimSequence>(Path, CharacterAssets); };
	auto Many = [&](const TCHAR* Key, TArray<UAnimSequence*>& Into)
	{
		const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
		if (Set.TryGetArrayField(Key, List))
		{
			for (const TSharedPtr<FJsonValue>& Item : *List)
			{
				if (UAnimSequence* Clip = LoadNamed<UAnimSequence>(Item->AsString(), CharacterAssets))
				{
					Into.Add(Clip);
				}
			}
		}
	};
	Out.Idle = One(TEXT("idle"));
	Out.Walk = One(TEXT("walk"));
	Out.Run = One(TEXT("run"));
	Out.Cast = One(TEXT("cast"));
	Out.Rise = One(TEXT("rise"));
	Set.TryGetBoolField(TEXT("cloth"), Out.bCloth);
	Many(TEXT("attack"), Out.Attack);
	const TSharedPtr<FJsonObject>* MotionList = nullptr;
	if (Set.TryGetObjectField(TEXT("motions"), MotionList))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Motion : (*MotionList)->Values)
		{
			const TSharedPtr<FJsonObject> Clips = Motion.Value->AsObject();
			if (!Clips.IsValid())
			{
				continue;
			}
			FTMMotionClips& Into = Out.Motions.Add(Motion.Key);
			auto Clip = [&](const TCHAR* Key) { FString Path; Clips->TryGetStringField(Key, Path); return LoadNamed<UAnimSequence>(Path, CharacterAssets); };
			Into.Intro = Clip(TEXT("intro"));
			Into.Windup = Clip(TEXT("windup"));
			Into.CastRelease = Clip(TEXT("castRelease"));
			double Impact = -1.0;
			if (Clips->TryGetNumberField(TEXT("impact"), Impact))
			{
				Into.Impact = FMath::Clamp(static_cast<float>(Impact), 0.0f, 1.0f);
			}
			const TArray<TSharedPtr<FJsonValue>>* Releases = nullptr;
			if (Clips->TryGetArrayField(TEXT("release"), Releases))
			{
				for (const TSharedPtr<FJsonValue>& Item : *Releases)
				{
					if (UAnimSequence* Loaded = LoadNamed<UAnimSequence>(Item->AsString(), CharacterAssets))
					{
						Into.Release.Add(Loaded);
					}
				}
			}
			else if (UAnimSequence* Single = Clip(TEXT("release")))
			{
				Into.Release.Add(Single);
			}
		}
	}
	Many(TEXT("hit"), Out.Hit);
	Many(TEXT("death"), Out.Death);
	// Reactions, statuses and moments: a name, or a list of names.
	for (const FString& Key : ExtraKeys())
	{
		TArray<UAnimSequence*> Clips;
		Many(*Key, Clips);
		if (Clips.Num() == 0)
		{
			if (UAnimSequence* Single = One(*Key))
			{
				Clips.Add(Single);
			}
		}
		if (Clips.Num() > 0)
		{
			Out.Extras.Add(Key, Clips);
		}
	}
	// A jump and a landing for leaps (2026-10-03): named in the set, or else the
	// clips Paragon heroes keep beside their idle under the usual names.
	if (Out.Idle)
	{
		const FString Folder = FPackageName::GetLongPackagePath(Out.Idle->GetOutermost()->GetName());
		auto Beside = [&](std::initializer_list<const TCHAR*> Names) -> UAnimSequence*
		{
			for (const TCHAR* Name : Names)
			{
				const FString Package = Folder / Name;
				if (FPackageName::DoesPackageExist(Package))
				{
					if (UAnimSequence* Clip = LoadNamed<UAnimSequence>(Package + TEXT(".") + Name, CharacterAssets))
					{
						return Clip;
					}
				}
			}
			return nullptr;
		};
		if (!Out.Extras.Contains(TEXT("jump")))
		{
			if (UAnimSequence* Jump = Beside({ TEXT("Jump_Start"), TEXT("JumpStart"), TEXT("Jump_Up"), TEXT("Jump_Loop"), TEXT("JumpApex"), TEXT("Jump_Apex") }))
			{
				Out.Extras.Add(TEXT("jump"), { Jump });
			}
		}
		if (!Out.Extras.Contains(TEXT("land")))
		{
			if (UAnimSequence* Land = Beside({ TEXT("Jump_Land"), TEXT("JumpLand"), TEXT("Land") }))
			{
				Out.Extras.Add(TEXT("land"), { Land });
			}
		}
	}
	const TSharedPtr<FJsonObject>* Idles = nullptr;
	if (Set.TryGetObjectField(TEXT("idles"), Idles))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Idle : (*Idles)->Values)
		{
			if (UAnimSequence* Loaded = LoadNamed<UAnimSequence>(Idle.Value->AsString(), CharacterAssets))
			{
				Out.Extras.Add(TEXT("idle:") + Idle.Key, { Loaded });
			}
		}
	}
	double Speed = 0.0;
	if (Set.TryGetNumberField(TEXT("walkSpeed"), Speed) && Speed > 0.0)
	{
		Out.WalkSpeed = static_cast<float>(Speed);
	}
	if (Set.TryGetNumberField(TEXT("runSpeed"), Speed) && Speed > 0.0)
	{
		Out.RunSpeed = static_cast<float>(Speed);
	}
}

const ATMBattleDirector::FTMBody* ATMBattleDirector::BodyFor(const TMSim::FUnit& Unit) const
{
	return BodyForJob(Unit.Job);
}

const ATMBattleDirector::FTMBody* ATMBattleDirector::BodyForJob(const std::string& JobId) const
{
	// The class's own, then its look's, then the default. A built-in class has
	// no look written down, but each built-in's id is itself a look.
	const FString Id = UTF8_TO_TCHAR(JobId.c_str());
	if (const FString* Named = ClassBodies.Find(Id))
	{
		if (const FTMBody* Body = Bodies.Find(*Named))
		{
			return Body;
		}
	}
	FString Look = Id;
	if (const TMSim::FJobDef* Job = TMSim::FindJob(JobId))
	{
		if (!Job->Look.empty())
		{
			Look = UTF8_TO_TCHAR(Job->Look.c_str());
		}
	}
	if (const FString* Named = LookBodies.Find(Look))
	{
		if (const FTMBody* Body = Bodies.Find(*Named))
		{
			return Body;
		}
	}
	return Bodies.Find(DefaultBody);
}

void ATMBattleDirector::ResetMotion()
{
	ClearBlows();
	ClearCast();
	bCelebrated = false;
	LoadCastAnimation();
	LoadCastLooks();
	Motions.Reset();
	Motions.SetNum(Battle.Units.size());
	for (int32 i = 0; i < Motions.Num(); ++i)
	{
		const TMSim::FUnit& Unit = Battle.Units[i];
		FTMMotion& Motion = Motions[i];
		Motion.Body = BodyFor(Unit);
		for (int32 Slot = 0; Slot < TMSim::AbilitySlots && Motion.Body; ++Slot)
		{
			if (const TMSim::FAbility* Ability = Unit.Ability(Slot))
			{
				Motion.Picked[Slot] = CastClipsFor(*Motion.Body, *Ability);
			}
		}
		Motion.Shown = WorldFor(Unit);
		Motion.Yaw = YawOf(Unit.Facing);
		Motion.SimPos = Unit.Pos;
		if (UnitVisuals.IsValidIndex(i) && UnitVisuals[i])
		{
			UnitVisuals[i]->SetRelativeLocation(Motion.Shown);
			UnitVisuals[i]->SetRelativeRotation(FRotator(0.0f, Motion.Yaw + (Motion.Body ? Motion.Body->Yaw : 180.0f), 0.0f));
		}
		if (Motion.Body && Motion.Body->Animations)
		{
			Animate(i, Motion.Body->Animations->Idle, true);
		}
	}
}

FVector ATMBattleDirector::ShownAt(const TMSim::FUnit& Unit) const
{
	for (int32 i = 0; i < Motions.Num() && i < static_cast<int32>(Battle.Units.size()); ++i)
	{
		if (Battle.Units[i].Id == Unit.Id)
		{
			return Motions[i].Shown;
		}
	}
	return WorldFor(Unit);
}

void ATMBattleDirector::Animate(int32 Index, UAnimSequence* Clip, bool bLoop, float MaxSeconds)
{
	if (!Clip || !UnitVisuals.IsValidIndex(Index) || !UnitVisuals[Index] || !Motions.IsValidIndex(Index))
	{
		return;
	}
	FTMMotion& Motion = Motions[Index];
	// The loop already playing is left to run, or it would restart every frame.
	if (bLoop && Motion.Playing == Clip && Motion.OneShotLeft <= 0.0f)
	{
		return;
	}
	UnitVisuals[Index]->PlayAnimation(Clip, bLoop);
	// Anything else played cuts a chain short (a flinch mid-swing); a chain
	// carries itself on by setting Then again after this (AdvanceMotion).
	Motion.Then.Reset();
	Motion.Playing = Clip;
	Motion.OneShotLeft = bLoop ? 0.0f : Clip->GetPlayLength();
	if (!bLoop && MaxSeconds > 0.0f)
	{
		Motion.OneShotLeft = FMath::Min(Motion.OneShotLeft, MaxSeconds);
	}
}

const ATMBattleDirector::FTMMotionClips* ATMBattleDirector::FindMotion(const FTMAnimSet& Set, const FString& Motion)
{
	// The nearest motion a set has, so a body with a few clips still moves
	// sensibly for all of them.
	static const TMap<FString, TArray<FString>> Nearest =
	{
		{ TEXT("heavy"), { TEXT("melee") } },
		{ TEXT("dash"), { TEXT("heavy"), TEXT("melee") } },
		{ TEXT("shoot"), { TEXT("bolt") } },
		{ TEXT("area"), { TEXT("bolt") } },
		{ TEXT("heal"), { TEXT("buff"), TEXT("bolt") } },
		{ TEXT("buff"), { TEXT("heal"), TEXT("bolt") } },
		{ TEXT("revive"), { TEXT("heal"), TEXT("buff"), TEXT("bolt") } },
		{ TEXT("channel"), { TEXT("area"), TEXT("bolt") } },
	};
	if (const FTMMotionClips* Found = Set.Motions.Find(Motion))
	{
		return Found;
	}
	if (const TArray<FString>* Next = Nearest.Find(Motion))
	{
		for (const FString& Other : *Next)
		{
			if (const FTMMotionClips* Found = Set.Motions.Find(Other))
			{
				return Found;
			}
		}
	}
	return nullptr;
}

UAnimSequence* ATMBattleDirector::StandingClip(int32 Index) const
{
	const FTMMotion& Motion = Motions[Index];
	const FTMAnimSet* Set = Motion.Body ? Motion.Body->Animations : nullptr;
	if (!Set)
	{
		return nullptr;
	}
	const TMSim::FUnit& Unit = Battle.Units[Index];
	// Stunned or asleep, if the set has a way to show it.
	if (Unit.HasStatus("stun"))
	{
		if (UAnimSequence* Dazed = Set->Extra(TEXT("stunned")))
		{
			return Dazed;
		}
	}
	if (Unit.HasStatus("sleep"))
	{
		if (UAnimSequence* Asleep = Set->Extra(TEXT("sleep")))
		{
			return Asleep;
		}
	}
	// Charging a cast, or between the turns of a channel: its loop, if it has one.
	int32 Slot = Unit.IsCasting() ? Unit.Casting.Slot : (Unit.IsChanneling() ? Unit.Channeling.Slot : -1);
	if (Slot >= 0)
	{
		if (Slot < TMSim::AbilitySlots && Motion.Picked[Slot].Loop)
		{
			return Motion.Picked[Slot].Loop;
		}
		if (const TMSim::FAbility* Ability = Unit.Ability(Slot))
		{
			const FTMMotionClips* Clips = FindMotion(*Set, UTF8_TO_TCHAR(TMSim::MotionOf(*Ability, Slot).c_str()));
			if (Clips && Clips->Windup)
			{
				return Clips->Windup;
			}
		}
	}
	// A stance that suits the class: its first ability's motion says whether it
	// is a fighter, an archer or a caster.
	if (const TMSim::FAbility* First = Unit.Ability(0))
	{
		if (UAnimSequence* Own = Set->Extra(*(TEXT("idle:") + FString(UTF8_TO_TCHAR(TMSim::MotionOf(*First, 0).c_str())))))
		{
			return Own;
		}
	}
	return Set->Idle;
}

void ATMBattleDirector::AnimateEvents(const TMSim::FTickReport& Report)
{
	if (Motions.Num() != static_cast<int32>(Battle.Units.size()))
	{
		return;
	}
	auto IndexOf = [this](int32 UnitId)
	{
		for (int32 i = 0; i < static_cast<int32>(Battle.Units.size()); ++i)
		{
			if (Battle.Units[i].Id == UnitId)
			{
				return i;
			}
		}
		return -1;
	};
	for (const TMSim::FEvent& Event : Report.Events)
	{
		const int32 i = IndexOf(Event.Unit);
		if (i < 0 || !Motions[i].Body || !Motions[i].Body->Animations)
		{
			continue;
		}
		FTMMotion& Motion = Motions[i];
		const FTMAnimSet& Set = *Motion.Body->Animations;
		const TMSim::FUnit& Unit = Battle.Units[i];
		if (Event.Kind == TMSim::EEventKind::CastStarted)
		{
			if (IsSeen(Unit))
			{
				const FVector Where = WorldFor(Unit) + FVector(0.0f, 0.0f, 100.0f);
				PlayEventSound(TEXT("cast"), &Where, 0.6f);
			}
			// Starting to charge: the motion's intro, then its wind-up loops for
			// as long as the cast lasts (StandingClip).
			const TMSim::FAbility* Ability = TMSim::FindAbility(Event.Id);
			const FTMMotionClips* Clips = Ability ? FindMotion(Set, UTF8_TO_TCHAR(TMSim::MotionOf(*Ability, Event.Slot).c_str())) : nullptr;
			Motion.bWasCasting = true;
			const TMSim::FVec2 Toward = Event.Where - Unit.Pos;
			if (Toward.Length() > 0.05f)
			{
				Motion.Yaw = YawOf(Toward);
			}
			// A picked wind-up starts the cast in place of the set's intro.
			UAnimSequence* Picked = Event.Slot >= 0 && Event.Slot < TMSim::AbilitySlots ? Motion.Picked[Event.Slot].Windup : nullptr;
			UAnimSequence* Intro = Picked ? Picked : (Clips ? Clips->Intro : nullptr);
			if (Intro && Motion.Path.Num() == 0)
			{
				Animate(i, Intro, false);
			}
			continue;
		}
		if (Event.Kind == TMSim::EEventKind::CastFizzled)
		{
			// Lost to a stun or a knock: whatever it was charging stops.
			Motion.bWasCasting = false;
			Motion.OneShotLeft = 0.0f;
			if (Set.Hit.Num() > 0 && Unit.IsAlive())
			{
				Animate(i, Set.Hit[0], false);
			}
			continue;
		}
		if (Event.Kind == TMSim::EEventKind::Resolved)
		{
			// Its motion (TMSim::MotionOf): the class file's, or one worked out
			// from the ability. A switch flipped, or an aura, is no motion.
			const TMSim::FAbility* Ability = TMSim::FindAbility(Event.Id);
			// A warned blow landing (2026-10-06): the swing was when it was drawn.
			if (Event.Amount == 1 && Ability && Ability->Special == "warned")
			{
				continue;
			}
			const FString Named = Ability ? FString(UTF8_TO_TCHAR(TMSim::MotionOf(*Ability, Event.Slot).c_str())) : FString(TEXT("none"));
			const bool bCharged = Motion.bWasCasting;
			Motion.bWasCasting = false;
			// What Cast Studio picked for it on this body (ResetMotion), if anything.
			const FTMCastClips NoPicks;
			const FTMCastClips& Picks = Event.Slot >= 0 && Event.Slot < TMSim::AbilitySlots ? Motion.Picked[Event.Slot] : NoPicks;
			Motion.LastLead = 0.0f;
			Motion.LastContact = Picks.Contact;
			if (Named == TEXT("none") && !Picks.Release)
			{
				continue;
			}
			UAnimSequence* Clip = Picks.Release;
			// An ultimate may have clips of its own: "<motion>_ult".
			const FTMMotionClips* Ultimate = Event.Slot == 3 ? Set.Motions.Find(Named + TEXT("_ult")) : nullptr;
			if (Clip)
			{
				// Picked: it plays whatever the set has.
			}
			else if (Ultimate && Ultimate->Release.Num() > 0)
			{
				Clip = Ultimate->Release[0];
			}
			else if (const FTMMotionClips* Clips = FindMotion(Set, Named))
			{
				if (bCharged && Clips->CastRelease)
				{
					Clip = Clips->CastRelease;
				}
				else if (Clips->Release.Num() > 0)
				{
					Clip = Clips->Release[FMath::Max(0, Event.Slot) % Clips->Release.Num()];
				}
			}
			if (!Clip)
			{
				// No clip for it or anything near it: a weapon swings, anything
				// else makes the set's cast motion.
				const bool bWeapon = Named == TEXT("melee") || Named == TEXT("heavy") || Named == TEXT("dash") || Named == TEXT("shoot");
				Clip = bWeapon && Set.Attack.Num() > 0 ? Set.Attack[FMath::Max(0, Event.Slot) % Set.Attack.Num()] : Set.Cast;
			}
			Motion.LastRelease = Clip;
			// The swing in order: a picked wind-up first (unless a cast already
			// wound up), the release, then a picked recover.
			TArray<UAnimSequence*> Swing;
			if (Picks.Windup && !bCharged && Clip)
			{
				Swing.Add(Picks.Windup);
				Motion.LastLead = Picks.Windup->GetPlayLength();
			}
			Swing.Add(Clip);
			if (Picks.Recover && Clip)
			{
				Swing.Add(Picks.Recover);
			}
			UAnimSequence* First = Swing[0];
			Swing.RemoveAt(0);
			const TMSim::FVec2 Toward = Event.Where - Unit.Pos;
			const float Yaw = Toward.Length() > 0.05f ? YawOf(Toward) : YawOf(Unit.Facing);
			// A movement skill (2026-10-03) moved it as it went off: through the
			// air (a leap), along the ground (a dash) or through smoke (a step
			// behind) -- not walked there by the way a walk would go.
			const bool bMovedBy = Ability && !(Unit.Pos == Motion.SimPos);
			uint8 Flight = 0;
			if (bMovedBy && (Ability->Special == "leap" || Ability->Special == "vault"))
			{
				Flight = 1;
			}
			else if (bMovedBy && (Ability->Special == "behind" || Ability->Special == "shadowhop" || Ability->Special == "recall"))
			{
				Flight = 3;
			}
			else if (bMovedBy && (Ability->Special == "dash" || Ability->Special == "charge" || Ability->Special == "disengage"
				|| Ability->Special == "riptide"))
			{
				// The new spells' moves (2026-10-05): a rush along the ground.
				Flight = 2;
			}
			else if (bMovedBy && TMSim::ShapeOf(*Ability) == "vector")
			{
				// The Dragoon's Jump goes up; a charge or a rush stays low.
				const FString Id = UTF8_TO_TCHAR(Ability->Id.c_str());
				Flight = Id.Contains(TEXT("jump")) || Id.Contains(TEXT("leap")) ? 1 : 2;
			}
			if (Flight != 0)
			{
				const FVector Landing = WorldFromMetres(Unit.Pos, Battle.Map.NodeLevel(TMSim::FMap::NodeOf(Unit.Pos)));
				const float Distance = FVector::Dist2D(Motion.Shown, Landing);
				Motion.Flight = Flight;
				Motion.FlightFrom = Motion.Shown;
				Motion.FlightAge = 0.0f;
				Motion.FlightTime = Flight == 1 ? FMath::Clamp(0.35f + Distance / 1200.0f, 0.45f, 0.9f)
					: (Flight == 2 ? FMath::Clamp(0.15f + Distance / 1800.0f, 0.2f, 0.55f) : 0.3f);
				Motion.FlightArc = Flight == 1 ? FMath::Clamp(Distance * 0.35f, 90.0f, 280.0f) : 0.0f;
				Motion.SimPos = Unit.Pos;
				Motion.Path.Reset();
				Motion.Path.Add(Landing);
				Motion.OneShotLeft = 0.0f;
				Motion.Yaw = YawOf(Toward.Length() > 0.05f ? Toward : Unit.Facing);
				if (Flight == 1)
				{
					// Up: the body's jump, if it has one; the blow comes on landing.
					if (UAnimSequence* Jump = Set.Extra(TEXT("jump")))
					{
						Animate(i, Jump, false);
					}
				}
				else if (Flight == 3 && IsSeen(Unit))
				{
					// Gone in a puff of smoke where it stood (its ability's own effect plays where it lands).
					PlayFx(TEXT("/Game/PotaVFX_Smoke/VFX/System/SmokeBurst/NS_MagicSmokeBurstAir.NS_MagicSmokeBurstAir"),
						Motion.Shown + FVector(0.0f, 0.0f, 60.0f), 180.0f, 100.0f);
				}
			}
			if (Motion.Path.Num() > 0 && Motion.Flight != 2)
			{
				// Still walking there: the swing comes on arrival.
				Motion.Queued = First;
				Motion.QueuedThen = Swing;
				Motion.QueuedYaw = Yaw;
			}
			else
			{
				Motion.Yaw = Yaw;
				Animate(i, First, false);
				Motion.Then = Swing;
			}
		}
		else if (Event.Kind == TMSim::EEventKind::BecameReady && Unit.IsAlive()
			&& Motion.Path.Num() == 0 && Motion.OneShotLeft <= 0.0f && !Unit.IsCasting())
		{
			// A chime when one of this side's units is ready (battle.gd:1052-1063).
			if (bPlayerInput && PlayerCanOrder(&Unit))
			{
				PlayEventSound(TEXT("ready"), nullptr, 0.6f);
			}
			// Its turn: a small gesture, if the set has one.
			if (UAnimSequence* Ready = Set.Extra(TEXT("ready"), Unit.Id))
			{
				Animate(i, Ready, false);
			}
		}
	}
}

void ATMBattleDirector::PredictWalk(const TMSim::FOrder& Order)
{
	int32 i = INDEX_NONE;
	for (int32 k = 0; k < static_cast<int32>(Battle.Units.size()); ++k)
	{
		if (Battle.Units[k].Id == Order.UnitId)
		{
			i = k;
		}
	}
	if (!Motions.IsValidIndex(i) || !UnitVisuals.IsValidIndex(i))
	{
		return;
	}
	const TMSim::FUnit& Unit = Battle.Units[i];
	FTMMotion& Motion = Motions[i];
	if (!Unit.IsAlive() || Motion.bDown)
	{
		return;
	}
	// The way it will walk, found as AdvanceMotion finds it once the rules have it there.
	TMSim::FBattle Copy = Battle;
	std::vector<TMSim::FVec2> Way;
	if (const TMSim::FUnit* Walker = Copy.FindUnit(Unit.Id))
	{
		Way = Order.Via.empty() ? Copy.PathTo(*Walker, TMSim::FMap::NodeOf(Order.To), true)
			: Copy.PathVia(*Walker, Order.Via, TMSim::FMap::NodeOf(Order.To), true);
	}
	if (Way.empty() || !(Way.back() == Order.To))
	{
		return;
	}
	TArray<FVector> Path;
	float Length = 0.0f;
	TMSim::FVec2 Last = Unit.Pos;
	for (const TMSim::FVec2& Point : Way)
	{
		Path.Add(WorldFromMetres(Point, Battle.Map.NodeLevel(TMSim::FMap::NodeOf(Point))));
		Length += Last.DistanceTo(Point);
		Last = Point;
	}
	Motion.Path = SmoothedWalk(Motion.Shown, Path);
	Motion.bRun = Length > RunFrom;
	Motion.Pace = 1.0f + FMath::Clamp((Length - 8.0f) / 16.0f, 0.0f, 1.0f) * 0.35f;
	Motion.WalkAge = 0.0f;
	Motion.OneShotLeft = 0.0f;
	FTMPredicted& Predicted = PredictedWalks.Add(Unit.Id);
	Predicted.To = Order.To;
	Predicted.Sent = FPlatformTime::Seconds();
}

void ATMBattleDirector::SettlePredictions()
{
	const double Now = FPlatformTime::Seconds();
	for (auto It = PredictedWalks.CreateIterator(); It; ++It)
	{
		const TMSim::FUnit* Unit = Battle.FindUnit(It.Key());
		if (Unit && Unit->Pos == It.Value().To)
		{
			continue;  // taken: AdvanceMotion carries on with it
		}
		// Still out with the host: wait for it, for a few seconds at most.
		if (Unit && Unit->IsAlive() && !bHostRefused && Now - It.Value().Sent < 3.0)
		{
			continue;
		}
		// Refused, or never answered: back to where the rules have it.
		for (int32 k = 0; Unit && k < static_cast<int32>(Battle.Units.size()) && k < Motions.Num(); ++k)
		{
			if (Battle.Units[k].Id == Unit->Id)
			{
				Motions[k].Path.Reset();
				Motions[k].Path.Add(WorldFor(*Unit));
				Motions[k].Pace = 1.0f;
				Motions[k].WalkAge = 0.0f;
			}
		}
		It.RemoveCurrent();
	}
}

void ATMBattleDirector::AdvanceMotion(float DeltaSeconds)
{
	if (Motions.Num() != static_cast<int32>(Battle.Units.size()) || Motions.Num() != UnitVisuals.Num())
	{
		return;
	}
	for (int32 i = 0; i < Motions.Num(); ++i)
	{
		const TMSim::FUnit& Unit = Battle.Units[i];
		FTMMotion& Motion = Motions[i];
		const FTMAnimSet* Set = Motion.Body ? Motion.Body->Animations : nullptr;

		// The rules moved it: walk there along the way it went. The walk is
		// found again here, on a copy with the unit back where it started, since
		// the rules keep only where it ended up.
		// A monster off the board (not yet woken, or gone) comes back where the
		// rules put it, and so does anyone who blinked: shown there, not walked.
		if (Unit.bMonster && !Unit.IsAlive() && !Unit.IsKo())
		{
			SnapUnits.Add(Unit.Id);
		}
		if (Unit.IsAlive() && SnapUnits.Remove(Unit.Id) > 0)
		{
			Motion.SimPos = Unit.Pos;
			Motion.Shown = WorldFromMetres(Unit.Pos, Battle.Map.NodeLevel(TMSim::FMap::NodeOf(Unit.Pos)));
			Motion.Path.Reset();
		}
		// A walk already shown setting off before the host answered (PredictWalk):
		// the rules now have it where it was going, so it simply walks on.
		if (const FTMPredicted* Predicted = PredictedWalks.Find(Unit.Id))
		{
			if (Predicted->To == Unit.Pos)
			{
				Motion.SimPos = Unit.Pos;
				PredictedWalks.Remove(Unit.Id);
			}
		}
		if (!(Unit.Pos == Motion.SimPos))
		{
			const TMSim::FVec2 From = Motion.SimPos;
			Motion.SimPos = Unit.Pos;
			TMSim::FBattle Copy = Battle;
			std::vector<TMSim::FVec2> Way;
			if (TMSim::FUnit* Walker = Copy.FindUnit(Unit.Id))
			{
				Walker->Pos = From;
				Walker->bMoved = false;
				// By its waypoints, when it was told to go by some (2026-10-01).
				Way = !Unit.WalkVia.empty() && Unit.WalkFrom == From
					? Copy.PathVia(*Walker, Unit.WalkVia, TMSim::FMap::NodeOf(Unit.Pos), true)
					: Copy.PathTo(*Walker, TMSim::FMap::NodeOf(Unit.Pos), true);
			}
			if (Way.empty() || !(Way.back() == Unit.Pos))
			{
				Way = { Unit.Pos };  // no path it could have walked: straight there
			}
			Motion.Path.Reset();
			float Length = 0.0f;
			TMSim::FVec2 Last = From;
			for (const TMSim::FVec2& Point : Way)
			{
				Motion.Path.Add(WorldFromMetres(Point, Battle.Map.NodeLevel(TMSim::FMap::NodeOf(Point))));
				Length += Last.DistanceTo(Point);
				Last = Point;
			}
			// The way is from node to node, all corners; it is walked with its
			// corners cut, where the ground is level (2026-10-03).
			Motion.Path = SmoothedWalk(Motion.Shown, Motion.Path);
			Motion.bRun = Length > RunFrom;
			// A long walk goes a little faster, up to a third, so the battle is not
			// spent watching it; its steps quicken to match (ShowStatuses).
			Motion.Pace = 1.0f + FMath::Clamp((Length - 8.0f) / 16.0f, 0.0f, 1.0f) * 0.35f;
			Motion.WalkAge = 0.0f;
			Motion.OneShotLeft = 0.0f;
		}

		// Knocked out: fall, once, and lie there until raised or gone. Not before
		// the blow that did it has landed, though, nor up again before the spell
		// that raised it has.
		if (Motion.HeldBlows > 0)
		{
		}
		else if (!Unit.IsAlive())
		{
			const bool bWasDown = Motion.bDown;
			ON_SCOPE_EXIT
			{
				// Falls: a thud and a cry (battle.gd:456), once, as it goes down.
				if (!bWasDown && Motion.bDown && IsSeen(Unit))
				{
					const FVector Where = WorldFor(Unit) + FVector(0.0f, 0.0f, 50.0f);
					PlayEventSound(TEXT("knockout"), &Where);
					PlayVoice(i, TEXT("death"), 1.0f);
					// It has weight (2026-10-03): the world slows as it goes down, the camera shakes.
					SlowWorld(0.45f, 0.5f);
					Jolt(0.7f);
				}
			};
			if (!Motion.bDown && Unit.IsKo() && Set && (Motion.DeathClip || Set->Death.Num() > 0))
			{
				Motion.bDown = true;
				Motion.Path.Reset();
				Animate(i, Motion.DeathClip ? Motion.DeathClip : Set->Death[Unit.Id % Set->Death.Num()], false);
			}
			Motion.bDown = Motion.bDown || Unit.IsKo();
		}
		else if (Motion.bDown)
		{
			Motion.bDown = false;
			Motion.DeathClip = nullptr;
			Animate(i, Set ? (Set->Rise ? Set->Rise : Set->Idle) : nullptr, Set && !Set->Rise);
		}

		if (Motion.Path.Num() > 0 && !Motion.bDown && Motion.Flight != 0)
		{
			// A movement skill under way (AnimateEvents): along a line to where the
			// rules put it, up in an arc for a leap, gone and back for a step.
			Motion.FlightAge += DeltaSeconds;
			const float T = FMath::Clamp(Motion.FlightAge / FMath::Max(0.01f, Motion.FlightTime), 0.0f, 1.0f);
			const FVector Landing = Motion.Path.Last();
			if (Motion.Flight == 3)
			{
				Motion.Shown = T < 0.5f ? Motion.FlightFrom : Landing;
			}
			else
			{
				const float Along = Motion.Flight == 2 ? FMath::InterpEaseOut(0.0f, 1.0f, T, 2.0f) : T;
				Motion.Shown = FMath::Lerp(Motion.FlightFrom, Landing, Along);
				Motion.Shown.Z += Motion.FlightArc * 4.0f * T * (1.0f - T);
			}
			if (T >= 1.0f)
			{
				Motion.Shown = Landing;
				Motion.Path.Reset();
				// Down: a leap lands with a thump and the camera feels it.
				if (Motion.Flight == 1 && IsSeen(Unit))
				{
					Jolt(0.35f);
				}
				Motion.Flight = 0;
			}
			if (Motion.Path.Num() == 0 && Motion.Queued)
			{
				// Arrived with something to do: the blow (AdvanceBlows times it from now).
				Motion.Yaw = Motion.QueuedYaw;
				Animate(i, Motion.Queued, false);
				Motion.Then = Motion.QueuedThen;
				Motion.QueuedThen.Reset();
				Motion.Queued = nullptr;
			}
		}
		else if (Motion.Path.Num() > 0 && !Motion.bDown)
		{
			SoundStep(i, DeltaSeconds);
			Motion.WalkAge += DeltaSeconds;
			// Eased: into its stride over a fifth of a second, and slowing over the last
			// half metre, rather than at full speed from the first frame to the last.
			const float Into = FMath::Clamp(0.4f + Motion.WalkAge / 0.2f * 0.6f, 0.4f, 1.0f);
			const float Left = FVector::Dist2D(Motion.Shown, Motion.Path.Last());
			const float Out = Motion.Queued ? 1.0f : FMath::Clamp(Left / 50.0f, 0.5f, 1.0f);
			const float Speed = (Set ? (Motion.bRun ? Set->RunSpeed : Set->WalkSpeed) : 400.0f) * Motion.Pace * Into * Out;
			float Step = Speed * DeltaSeconds;
			while (Step > 0.0f && Motion.Path.Num() > 0)
			{
				const FVector ToNext = Motion.Path[0] - Motion.Shown;
				const float Distance = ToNext.Size();
				if (FVector2D(ToNext.X, ToNext.Y).Size() > 1.0f)
				{
					const float Want = FMath::RadiansToDegrees(FMath::Atan2(ToNext.Y, ToNext.X));
					Motion.Yaw = FMath::FixedTurn(Motion.Yaw, Want, TurnRate * DeltaSeconds);
				}
				if (Distance <= Step)
				{
					Motion.Shown = Motion.Path[0];
					Motion.Path.RemoveAt(0);
					Step -= Distance;
				}
				else
				{
					Motion.Shown += ToNext / Distance * Step;
					Step = 0.0f;
				}
			}
			if (Set)
			{
				Animate(i, Motion.bRun && Set->Run ? Set->Run : Set->Walk, true);
			}
			if (Motion.Path.Num() == 0 && Motion.Queued)
			{
				// Arrived with something to do: turn to it and do it. Its blow
				// starts counting towards landing now (AdvanceBlows).
				Motion.Yaw = Motion.QueuedYaw;
				Animate(i, Motion.Queued, false);
				Motion.Then = Motion.QueuedThen;
				Motion.QueuedThen.Reset();
				Motion.Queued = nullptr;
			}
		}
		else if (!Motion.bDown)
		{
			// Arrived ahead of the host's answer: waits where it walked to.
			const FTMPredicted* Waiting = PredictedWalks.Find(Unit.Id);
			Motion.Shown = Waiting ? WorldFromMetres(Waiting->To, Battle.Map.NodeLevel(TMSim::FMap::NodeOf(Waiting->To))) : WorldFor(Unit);
			// Standing: turn to face the way the rules say it faces.
			if (Motion.OneShotLeft <= 0.0f)
			{
				Motion.Yaw = FMath::FixedTurn(Motion.Yaw, YawOf(Unit.Facing), TurnRate * DeltaSeconds);
			}
		}

		ShowStatuses(i, DeltaSeconds);
		if (Motion.OneShotLeft > 0.0f)
		{
			Motion.OneShotLeft -= DeltaSeconds * Motion.PlayRate;
			if (Motion.OneShotLeft <= 0.0f && !Motion.bDown && Set)
			{
				if (Motion.Then.Num() > 0 && Motion.Path.Num() == 0)
				{
					// The next part of a picked swing: release after wind-up, recover after release.
					TArray<UAnimSequence*> Rest = Motion.Then;
					UAnimSequence* Next = Rest[0];
					Rest.RemoveAt(0);
					Animate(i, Next, false);
					Motion.Then = Rest;
				}
				else
				{
					// Mid-leap or mid-dash, nothing takes over until it lands.
					if (Motion.Flight == 0)
					{
						Animate(i, Motion.Path.Num() > 0 ? (Motion.bRun && Set->Run ? Set->Run : Set->Walk) : StandingClip(i), true);
					}
				}
			}
		}
		else if (Motion.Path.Num() == 0 && !Motion.bDown && Set)
		{
			Animate(i, StandingClip(i), true);
		}

		// The body and everything that stands with it, nudged by a blow and
		// lifted if it flies.
		FVector Offset = FVector::ZeroVector;
		if (Motion.JoltAge >= 0.0f)
		{
			Motion.JoltAge += DeltaSeconds;
			// Out fast, back slowly.
			const float Out = 0.07f;
			const float Back = 0.35f;
			const float Part = Motion.JoltAge < Out ? Motion.JoltAge / Out : FMath::Max(0.0f, 1.0f - (Motion.JoltAge - Out) / Back);
			Offset += Motion.JoltDir * Motion.JoltSize * FMath::InterpEaseOut(0.0f, 1.0f, Part, 2.0f);
			if (Motion.JoltAge >= Out + Back)
			{
				Motion.JoltAge = -1.0f;
			}
		}
		// Down with no fall to play: tip over backwards and settle, and back up
		// as it is raised.
		const bool bNoFall = Motion.bDown && !Motion.DeathClip && !(Set && Set->Death.Num() > 0);
		Motion.Tipped = FMath::FInterpConstantTo(Motion.Tipped, bNoFall ? 1.0f : 0.0f, DeltaSeconds, 2.5f);
		const float Tip = FMath::InterpEaseIn(0.0f, 1.0f, Motion.Tipped, 2.0f);
		Offset.Z -= 20.0f * Tip;
		float Sway = 0.0f;
		if (Unit.IsAlive() && Unit.HasStatus("fly"))
		{
			Offset.Z += 60.0f + 8.0f * FMath::Sin(Motion.Clock * 2.2f);
		}
		if (Unit.IsAlive() && Unit.HasStatus("stun") && !(Set && Set->Extra(TEXT("stunned"))))
		{
			// Dazed, with no clip to show it: a slow reel on the spot.
			Sway = 7.0f * FMath::Sin(Motion.Clock * 3.0f);
		}
		if (UnitVisuals[i])
		{
			UnitVisuals[i]->SetRelativeLocation(Motion.Shown + Offset);
			const FQuat Standing = FRotator(0.0f, Motion.Yaw + Sway + (Motion.Body ? Motion.Body->Yaw : 180.0f), 0.0f).Quaternion();
			// The tip leans it away from the way it faces: over onto its back.
			const FVector Facing = FRotator(0.0f, Motion.Yaw, 0.0f).Vector();
			const FQuat Over(FVector::CrossProduct(FVector::UpVector, Facing).GetSafeNormal(), -FMath::DegreesToRadians(85.0f * Tip));
			UnitVisuals[i]->SetRelativeRotation(Over * Standing);
		}
		if (Plates.IsValidIndex(i) && Plates[i])
		{
			Plates[i]->SetRelativeLocation(Motion.Shown + Offset + FVector(0.0f, 0.0f, 150.0f * UnitSize));
		}
		if (ReadyLights.IsValidIndex(i) && ReadyLights[i])
		{
			ReadyLights[i]->SetRelativeLocation(Motion.Shown + FVector(0.0f, 0.0f, 55.0f));
		}
		if (StatusLights.IsValidIndex(i) && StatusLights[i])
		{
			StatusLights[i]->SetRelativeLocation(Motion.Shown + Offset + FVector(0.0f, 0.0f, 110.0f));
		}
	}
	AdvanceBlows(DeltaSeconds);
	if (Battle.Winner != -1 && Blows.Num() == 0 && !bCelebrated)
	{
		Celebrate();
	}
}
