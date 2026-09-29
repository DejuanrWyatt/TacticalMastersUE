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
#include "Engine/SkeletalMesh.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
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

	// The animation sets first, since bodies name them.
	const TSharedPtr<FJsonObject>* Sets = nullptr;
	if (Root->TryGetObjectField(TEXT("animations"), Sets))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*Sets)->Values)
		{
			const TSharedPtr<FJsonObject> Set = Entry.Value->AsObject();
			if (!Set.IsValid())
			{
				continue;
			}
			FTMAnimSet& Out = AnimSets.Add(Entry.Key);
			auto One = [&](const TCHAR* Key) { FString Path; Set->TryGetStringField(Key, Path); return LoadNamed<UAnimSequence>(Path, CharacterAssets); };
			auto Many = [&](const TCHAR* Key, TArray<UAnimSequence*>& Into)
			{
				const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
				if (Set->TryGetArrayField(Key, List))
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
			Many(TEXT("attack"), Out.Attack);
			const TSharedPtr<FJsonObject>* MotionList = nullptr;
			if (Set->TryGetObjectField(TEXT("motions"), MotionList))
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
			const TSharedPtr<FJsonObject>* Idles = nullptr;
			if (Set->TryGetObjectField(TEXT("idles"), Idles))
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
			if (Set->TryGetNumberField(TEXT("walkSpeed"), Speed) && Speed > 0.0)
			{
				Out.WalkSpeed = static_cast<float>(Speed);
			}
			if (Set->TryGetNumberField(TEXT("runSpeed"), Speed) && Speed > 0.0)
			{
				Out.RunSpeed = static_cast<float>(Speed);
			}
		}
	}

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
			Out.Animations = AnimSets.Find(Out.SetName);
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
	UE_LOG(LogTemp, Log, TEXT("character map: %d bodies, %d animation sets"), Bodies.Num(), AnimSets.Num());
	return Bodies.Num() > 0;
}

USkeletalMesh* ATMBattleDirector::MeshOf(const FTMBody& Body)
{
	if (!Body.Mesh)
	{
		Body.Mesh = LoadNamed<USkeletalMesh>(Body.MeshPath, CharacterAssets);
	}
	return Body.Mesh;
}

const ATMBattleDirector::FTMBody* ATMBattleDirector::BodyFor(const TMSim::FUnit& Unit) const
{
	// The class's own, then its look's, then the default. A built-in class has
	// no look written down, but each built-in's id is itself a look.
	const FString Id = UTF8_TO_TCHAR(Unit.Job.c_str());
	if (const FString* Named = ClassBodies.Find(Id))
	{
		if (const FTMBody* Body = Bodies.Find(*Named))
		{
			return Body;
		}
	}
	FString Look = Id;
	if (const TMSim::FJobDef* Job = TMSim::FindJob(Unit.Job))
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
	bCelebrated = false;
	Motions.Reset();
	Motions.SetNum(Battle.Units.size());
	for (int32 i = 0; i < Motions.Num(); ++i)
	{
		const TMSim::FUnit& Unit = Battle.Units[i];
		FTMMotion& Motion = Motions[i];
		Motion.Body = BodyFor(Unit);
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

void ATMBattleDirector::Animate(int32 Index, UAnimSequence* Clip, bool bLoop)
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
	Motion.Playing = Clip;
	Motion.OneShotLeft = bLoop ? 0.0f : Clip->GetPlayLength();
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
		if (const TMSim::FAbility* Ability = TMSim::JobAbility(Unit.Job, Slot))
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
	if (const TMSim::FAbility* First = TMSim::JobAbility(Unit.Job, 0))
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
			if (Clips && Clips->Intro && Motion.Path.Num() == 0)
			{
				Animate(i, Clips->Intro, false);
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
			const FString Named = Ability ? FString(UTF8_TO_TCHAR(TMSim::MotionOf(*Ability, Event.Slot).c_str())) : FString(TEXT("none"));
			const bool bCharged = Motion.bWasCasting;
			Motion.bWasCasting = false;
			if (Named == TEXT("none"))
			{
				continue;
			}
			UAnimSequence* Clip = nullptr;
			// An ultimate may have clips of its own: "<motion>_ult".
			const FTMMotionClips* Ultimate = Event.Slot == 3 ? Set.Motions.Find(Named + TEXT("_ult")) : nullptr;
			if (Ultimate && Ultimate->Release.Num() > 0)
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
			const TMSim::FVec2 Toward = Event.Where - Unit.Pos;
			const float Yaw = Toward.Length() > 0.05f ? YawOf(Toward) : YawOf(Unit.Facing);
			if (Motion.Path.Num() > 0)
			{
				// Still walking there: the swing comes on arrival.
				Motion.Queued = Clip;
				Motion.QueuedYaw = Yaw;
			}
			else
			{
				Motion.Yaw = Yaw;
				Animate(i, Clip, false);
			}
		}
		else if (Event.Kind == TMSim::EEventKind::BecameReady && Unit.IsAlive()
			&& Motion.Path.Num() == 0 && Motion.OneShotLeft <= 0.0f && !Unit.IsCasting())
		{
			// Its turn: a small gesture, if the set has one.
			if (UAnimSequence* Ready = Set.Extra(TEXT("ready"), Unit.Id))
			{
				Animate(i, Ready, false);
			}
		}
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
				Way = Copy.PathTo(*Walker, TMSim::FMap::NodeOf(Unit.Pos), true);
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
			Motion.bRun = Length > RunFrom;
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

		if (Motion.Path.Num() > 0 && !Motion.bDown)
		{
			const float Speed = Set ? (Motion.bRun ? Set->RunSpeed : Set->WalkSpeed) : 400.0f;
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
				Motion.Queued = nullptr;
			}
		}
		else if (!Motion.bDown)
		{
			Motion.Shown = WorldFor(Unit);
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
				Animate(i, Motion.Path.Num() > 0 ? (Motion.bRun && Set->Run ? Set->Run : Set->Walk) : StandingClip(i), true);
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
			UnitVisuals[i]->SetRelativeRotation(FRotator(0.0f, Motion.Yaw + Sway + (Motion.Body ? Motion.Body->Yaw : 180.0f), 0.0f));
		}
		if (Plates.IsValidIndex(i) && Plates[i])
		{
			Plates[i]->SetRelativeLocation(Motion.Shown + Offset + FVector(0.0f, 0.0f, 150.0f));
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
