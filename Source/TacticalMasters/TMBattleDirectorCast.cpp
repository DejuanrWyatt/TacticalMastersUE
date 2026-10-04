// Cast Studio's looks, played: what a designer authored in the class creator
// for how each ability looks (Content/Data/CastStudio/AbilityLooks.json).
//
// TMCast reads the file and answers the questions about it -- which events
// play at a moment, how big, where (Source/TMCast/Public/CastLooks.h). This
// raises the moments from the director's own hooks and turns the answers into
// particle effects, lights and jolts of the camera:
//
//   castStart   a cast beginning (CastStarted); an instant ability's swing beginning
//   casting     while its user charges or channels it
//   swing       the swing beginning, after any walk there (where today's flare plays)
//   release     the blow connecting: the shot leaving, the sword landing
//   projectile  riding each shot until it lands
//   impact      on each unit it touched, as it lands; area once, on the ground it covers
//   target/ally/selfStatus   on whoever wears a status it gave, until it ends
//   tick        that status hurting or healing a turn     expire   that status ending,
//               the pet it called leaving, its channel ending     summon   on its pet
//
// Sounds are events too: played where their anchor is, and a swing's sound can
// be timed to a share of a part of the swing as the body plays it (wind-up,
// release, recover), so it stays on the frame whatever the clip's length.
//
// and the statuses table (any status, whoever gave it) and the reactions table.
// The live-tactics moments are phase 7 (Docs/CastStudio-Plan.md).
//
// Only the look. The rules never read any of it, so a battle, a replay and an
// online match come out the same with or without it; each machine plays its own
// looks from its own events. With no file, nothing here plays and today's look
// (TMBattleDirectorAbilityFx.cpp) is untouched; an ability with strip "legacy"
// or "all" gives up its class file's effect, or all of today's look.

#include "TMBattleDirector.h"

#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"

#include "Sound/SoundBase.h"

#include "SimAbility.h"
#include "SimBattle.h"
#include "TMSettings.h"

namespace
{
	FVector ToVector(const TMCast::FCastVec& V)
	{
		return FVector(static_cast<float>(V.X), static_cast<float>(V.Y), static_cast<float>(V.Z));
	}

	TMCast::FCastVec ToCast(const FVector& V)
	{
		return { V.X, V.Y, V.Z };
	}

	FLinearColor TintOf(const TMCast::FCastVec& V)
	{
		return FLinearColor(static_cast<float>(V.X), static_cast<float>(V.Y), static_cast<float>(V.Z));
	}

	/** At most this many lines kept in the spawn log. */
	constexpr int32 SpawnLogLines = 400;
}

void ATMBattleDirector::LoadCastLooks()
{
	// Written by the class creator's Publish, beside the animation picks. Read
	// again only when it changes, so a battle started after a Publish shows it.
	const FString File = FPaths::ProjectContentDir() / TEXT("Data/CastStudio/AbilityLooks.json");
	const FDateTime Time = IFileManager::Get().GetTimeStamp(*File);
	if (Time == CastLooksTime)
	{
		return;
	}
	CastLooksTime = Time;
	CastLooks = TMCast::FLooksFile();
	FString Text;
	if (Time == FDateTime::MinValue() || !FFileHelper::LoadFileToString(Text, *File))
	{
		UE_LOG(LogTemp, Log, TEXT("Cast Studio: no looks published; every ability looks as it always has"));
		return;
	}
	const bool bRead = TMCast::ReadLooksFile(TCHAR_TO_UTF8(*Text), CastLooks);
	for (const std::string& Problem : CastLooks.Problems)
	{
		UE_LOG(LogTemp, Warning, TEXT("Cast Studio: %s: %hs"), *File, Problem.c_str());
	}
	UE_LOG(LogTemp, Log, TEXT("Cast Studio: %s looks for %d abilities, %d statuses, %d reactions"),
		bRead ? TEXT("read") : TEXT("could not read"), static_cast<int32>(CastLooks.Abilities.size()),
		static_cast<int32>(CastLooks.Statuses.size()), static_cast<int32>(CastLooks.Reactions.size()));
}

TMCast::EStrip ATMBattleDirector::CastStrip(const TMSim::FAbility* Ability) const
{
	return Ability ? CastLooks.StripOf(Ability->Id) : TMCast::EStrip::None;
}

const TMCast::FLook* ATMBattleDirector::CastLookOf(const TMSim::FAbility* Ability) const
{
	return Ability ? CastLooks.Ability(Ability->Id) : nullptr;
}

ATMBattleDirector::FTMCastAt ATMBattleDirector::CastAtUnit(int32 UnitId)
{
	FTMCastAt At;
	At.UnitId = UnitId;
	if (const TMSim::FUnit* Unit = Battle.FindUnit(UnitId))
	{
		At.Centre = ShownAt(*Unit);
		At.Aim = At.Centre;
		At.bSeen = IsSeen(*Unit);
	}
	else
	{
		At.bSeen = false;
	}
	return At;
}

bool ATMBattleDirector::CastPlace(const TMCast::FLookEvent& Event, const FTMCastAt& At, FVector& Local, float& Yaw, const char*& Resolved)
{
	TMCast::FAnchorInput Input;
	Input.Centre = ToCast(At.Centre);
	Input.Aim = ToCast(At.Aim);
	Input.Ground = Input.Centre;
	Yaw = 0.0f;
	const bool bOnUnit = Event.Anchor != TMCast::EAnchor::Center && Event.Anchor != TMCast::EAnchor::Aim;
	if (bOnUnit && At.UnitId >= 0)
	{
		int32 Index = INDEX_NONE;
		for (int32 i = 0; i < static_cast<int32>(Battle.Units.size()); ++i)
		{
			if (Battle.Units[i].Id == At.UnitId)
			{
				Index = i;
				break;
			}
		}
		if (Index == INDEX_NONE)
		{
			return false;
		}
		Input.Ground = ToCast(ShownAt(Battle.Units[Index]));
		Yaw = Motions.IsValidIndex(Index) ? Motions[Index].Yaw : 0.0f;
		Input.Yaw = Yaw;
		// The body's own sockets, where it has them: a hand follows the swing.
		if (UnitVisuals.IsValidIndex(Index) && UnitVisuals[Index])
		{
			USkeletalMeshComponent* Visual = UnitVisuals[Index];
			const FTransform Here = GetActorTransform();
			auto Socket = [&](const TCHAR* Name, bool& bHas, TMCast::FCastVec& Out)
			{
				if (!bHas && Visual->DoesSocketExist(FName(Name)))
				{
					bHas = true;
					Out = ToCast(Here.InverseTransformPosition(Visual->GetSocketLocation(FName(Name))));
				}
			};
			Socket(TEXT("hand_r"), Input.bHasHand, Input.Hand);
			Socket(TEXT("weapon_r"), Input.bHasWeapon, Input.Weapon);
			Socket(TEXT("weapon"), Input.bHasWeapon, Input.Weapon);
			Socket(TEXT("head"), Input.bHasHead, Input.Head);
		}
	}
	const TMCast::FAnchorPoint Point = TMCast::AnchorPoint(Event.Anchor, Input);
	Resolved = Point.Resolved;
	Local = ToVector(Point.Where + TMCast::OffsetBy(Event.Offset, Yaw));
	return true;
}

void ATMBattleDirector::CastRaise(const TMCast::FLook* Look, TMCast::EMoment Moment, const TMCast::FMomentContext& Context,
	const FTMCastAt& At, uint64 Key, const TMCast::FSwingTimes* Swing)
{
	if (!Look || !At.bSeen || !GetWorld() || !GetWorld()->IsGameWorld())
	{
		return;
	}
	for (const TMCast::FLookEvent* Event : TMCast::EventsFor(*Look, Moment, Context))
	{
		// Its delay, and on a swing the part of the swing it is timed to.
		const double Wait = Swing ? TMCast::SyncSeconds(*Event, *Swing) : Event->Delay;
		if (Wait > 0.0)
		{
			FTMCastPending& Waiting = CastPending.AddDefaulted_GetRef();
			Waiting.Event = *Event;
			Waiting.At = At;
			Waiting.Key = Key;
			Waiting.In = static_cast<float>(Wait);
			continue;
		}
		CastSpawn(*Event, At, Key);
	}
}

void ATMBattleDirector::CastSpawn(const TMCast::FLookEvent& Event, const FTMCastAt& At, uint64 Key)
{
	if (CastSpawnLog.Num() >= SpawnLogLines)
	{
		CastSpawnLog.RemoveAt(0, CastSpawnLog.Num() - SpawnLogLines + 1);
	}
	const bool bLasting = TMCast::IsLasting(Event.Moment) && Key != 0;
	// A repeating event on something lasting: a repeater that plays it again and again until the key ends.
	if (bLasting && Event.Repeat > 0.0)
	{
		FTMCastLive& Repeater = CastLive.AddDefaulted_GetRef();
		Repeater.Key = Key;
		Repeater.Event = Event;
		Repeater.Event.Repeat = 0.0;
		Repeater.Event.Moment = TMCast::EMoment::Impact;  // each play is a one-shot
		Repeater.At = At;
		Repeater.bRepeater = true;
		Repeater.NextRepeat = 0.0f;
		// For a repeater, Life is how often it plays.
		Repeater.Life = static_cast<float>(Event.Repeat);
		return;
	}

	if (Event.Kind == TMCast::EEffectKind::Sound)
	{
		FVector Local = FVector::ZeroVector;
		float Yaw = 0.0f;
		const char* Resolved = "";
		USoundBase* Sound = CanSound() ? SoundAt(UTF8_TO_TCHAR(Event.Sound.c_str())) : nullptr;
		if (Sound && (Event.Moment == TMCast::EMoment::Projectile && At.Carrier.IsValid()
			? (Local = GetActorTransform().InverseTransformPosition(At.Carrier->GetComponentLocation()), true)
			: CastPlace(Event, At, Local, Yaw, Resolved)))
		{
			UGameplayStatics::PlaySoundAtLocation(this, Sound, GetActorTransform().TransformPosition(Local),
				static_cast<float>(Event.Volume) * FTMSettings::Get().SfxVolume, static_cast<float>(Event.Pitch));
		}
		CastSpawnLog.Add(FString::Printf(TEXT("%hs sound %hs%s"), TMCast::MomentName(Event.Moment), Event.Sound.c_str(),
			Sound ? TEXT("") : TEXT(" (not played)")));
		return;
	}

	if (Event.Kind == TMCast::EEffectKind::Shake)
	{
		const double Strength = TMCast::ShakeStrength(Event, At.Harm);
		if (Strength > 0.0)
		{
			Jolt(static_cast<float>(Strength));
		}
		CastSpawnLog.Add(FString::Printf(TEXT("%hs shake %.2f"), TMCast::MomentName(Event.Moment), Strength));
		return;
	}

	USceneComponent* Carrier = Event.Moment == TMCast::EMoment::Projectile ? At.Carrier.Get() : nullptr;
	if (Event.Moment == TMCast::EMoment::Projectile && !Carrier)
	{
		return;  // the shot is gone already
	}
	FVector Local = FVector::ZeroVector;
	float Yaw = 0.0f;
	const char* Resolved = "attached";
	if (!Carrier && !CastPlace(Event, At, Local, Yaw, Resolved))
	{
		return;
	}
	const FRotator Turn(static_cast<float>(Event.Rotation.X), Yaw + static_cast<float>(Event.Rotation.Y), static_cast<float>(Event.Rotation.Z));

	if (Event.Kind == TMCast::EEffectKind::Light)
	{
		const float Radius = static_cast<float>(TMCast::LightRadius(Event, At.AreaCm));
		const FLinearColor Tint = TintOf(Event.Tint);
		const float Brightness = static_cast<float>(Event.Brightness);
		if (!bLasting || Event.Duration > 0.0)
		{
			if (Carrier)
			{
				// A flash on a shot flies with it.
				UPointLightComponent* Light = NewObject<UPointLightComponent>(this, NAME_None, RF_Transient);
				Light->SetupAttachment(Carrier);
				Light->RegisterComponent();
				Light->SetRelativeLocation(ToVector(Event.Offset));
				Light->SetLightColor(Tint);
				Light->SetIntensity(Brightness);
				Light->SetAttenuationRadius(Radius);
				Light->SetCastShadows(false);
				FTMCastLive& Live = CastLive.AddDefaulted_GetRef();
				Live.Component = Light;
				Live.Key = Key;
				Live.Event = Event;
				Live.Life = static_cast<float>(Event.Duration > 0.0 ? Event.Duration : 0.5);
			}
			else
			{
				Pulse(Local, Tint, Brightness, Radius, static_cast<float>(Event.Duration > 0.0 ? Event.Duration : 0.5));
			}
		}
		else
		{
			// A steady light for as long as what it is on lasts.
			UPointLightComponent* Light = NewObject<UPointLightComponent>(this, NAME_None, RF_Transient);
			Light->SetupAttachment(Carrier ? Carrier : RootComponent.Get());
			Light->RegisterComponent();
			Light->SetRelativeLocation(Carrier ? ToVector(Event.Offset) : Local);
			Light->SetLightColor(Tint);
			Light->SetIntensity(Brightness);
			Light->SetAttenuationRadius(Radius);
			Light->SetCastShadows(false);
			FTMCastLive& Live = CastLive.AddDefaulted_GetRef();
			Live.Component = Light;
			Live.Key = Key;
			Live.Event = Event;
			Live.At = At;
			Live.bFollow = !Carrier;
		}
		CastSpawnLog.Add(FString::Printf(TEXT("%hs light %.0f radius %.0f"), TMCast::MomentName(Event.Moment), Brightness, Radius));
		return;
	}

	// A particle effect.
	const FString Path = UTF8_TO_TCHAR(Event.Effect.c_str());
	UObject* System = LoadLookFx(Path);
	if (!System)
	{
		CastSpawnLog.Add(FString::Printf(TEXT("%hs %s MISSING"), TMCast::MomentName(Event.Moment), *Path));
		return;
	}
	const float Scale = static_cast<float>(TMCast::ScaleFor(Event, CastLooks, At.AreaCm));
	UFXSystemComponent* Playing = nullptr;
	if (Carrier)
	{
		if (UNiagaraSystem* Niagara = Cast<UNiagaraSystem>(System))
		{
			Playing = UNiagaraFunctionLibrary::SpawnSystemAttached(Niagara, Carrier, NAME_None, ToVector(Event.Offset),
				FRotator(static_cast<float>(Event.Rotation.X), static_cast<float>(Event.Rotation.Y), static_cast<float>(Event.Rotation.Z)),
				FVector(Scale), EAttachLocation::KeepRelativeOffset, true, ENCPoolMethod::None);
		}
		else if (UParticleSystem* Cascade = Cast<UParticleSystem>(System))
		{
			Playing = UGameplayStatics::SpawnEmitterAttached(Cascade, Carrier, NAME_None, ToVector(Event.Offset),
				FRotator(static_cast<float>(Event.Rotation.X), static_cast<float>(Event.Rotation.Y), static_cast<float>(Event.Rotation.Z)),
				FVector(Scale), EAttachLocation::KeepRelativeOffset, true);
		}
		if (Playing)
		{
			// Its own size, not the size of the ball that carries it.
			Playing->SetUsingAbsoluteScale(true);
			Playing->SetWorldScale3D(FVector(Scale));
		}
	}
	else
	{
		const FTransform Here = GetActorTransform();
		const FVector Where = Here.TransformPosition(Local);
		const FRotator Facing = Here.TransformRotation(Turn.Quaternion()).Rotator();
		if (UNiagaraSystem* Niagara = Cast<UNiagaraSystem>(System))
		{
			Playing = UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, Niagara, Where, Facing, FVector(Scale));
		}
		else if (UParticleSystem* Cascade = Cast<UParticleSystem>(System))
		{
			Playing = UGameplayStatics::SpawnEmitterAtLocation(this, Cascade, Where, Facing, FVector(Scale));
		}
	}
	if (!Playing)
	{
		return;
	}
	++EffectsPlayed;
	CastSpawnLog.Add(FString::Printf(TEXT("%hs %s scale %.2f at %.0f %.0f %.0f (%hs)"), TMCast::MomentName(Event.Moment), *Path, Scale,
		Local.X, Local.Y, Local.Z, Resolved));
	const bool bFollow = !Carrier && (Event.bFollow || bLasting);
	if (!bLasting && Event.Duration <= 0.0 && !bFollow)
	{
		// As today's: switched off after a while (AdvanceVfx), since much of it loops.
		PlayingVfx.Add({ Playing, 0.0f });
		return;
	}
	FTMCastLive& Live = CastLive.AddDefaulted_GetRef();
	Live.Component = Playing;
	Live.Key = bLasting ? Key : 0;
	Live.Event = Event;
	Live.At = At;
	Live.bFollow = bFollow;
	Live.Life = Event.Duration > 0.0 ? static_cast<float>(Event.Duration) : (bLasting ? 0.0f : VfxSeconds);
}

void ATMBattleDirector::CastEnd(uint64 Key)
{
	if (Key == 0)
	{
		return;
	}
	CastPending.RemoveAll([Key](const FTMCastPending& Waiting) { return Waiting.Key == Key; });
	for (int32 i = CastLive.Num() - 1; i >= 0; --i)
	{
		FTMCastLive& Live = CastLive[i];
		if (Live.Key != Key)
		{
			continue;
		}
		if (USceneComponent* Component = Live.Component.Get())
		{
			if (UFXSystemComponent* Fx = Cast<UFXSystemComponent>(Component))
			{
				// Let go of what it rode, so what it has thrown out fades where it is.
				Fx->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
				Fx->Deactivate();
			}
			else
			{
				Component->DestroyComponent();
			}
		}
		CastLive.RemoveAtSwap(i);
	}
}

void ATMBattleDirector::ClearCast()
{
	for (FTMCastLive& Live : CastLive)
	{
		if (USceneComponent* Component = Live.Component.Get())
		{
			Component->DestroyComponent();
		}
	}
	CastLive.Reset();
	CastPending.Reset();
	CastStatuses.Reset();
	CastCasting.Reset();
	CastPets.Reset();
	CastSpawnLog.Reset();
}

void ATMBattleDirector::AdvanceCast(float DeltaSeconds)
{
	if (!GetWorld() || !GetWorld()->IsGameWorld())
	{
		return;
	}

	// Casts and channels beginning and ending: their casting events, and a channel's expire.
	for (const TMSim::FUnit& Unit : Battle.Units)
	{
		const int32 Slot = Unit.IsCasting() ? Unit.Casting.Slot : (Unit.IsChanneling() ? Unit.Channeling.Slot : -1);
		FTMCastCasting* Was = CastCasting.Find(Unit.Id);
		if (Was && Was->Slot != Slot)
		{
			CastEnd(Was->Key);
			const TMSim::FAbility* Ability = Unit.Ability(Was->Slot);
			if (Was->bChannel && !Unit.IsChanneling())
			{
				CastRaise(CastLookOf(Ability), TMCast::EMoment::Expire, TMCast::FMomentContext(), CastAtUnit(Unit.Id));
			}
			CastCasting.Remove(Unit.Id);
			Was = nullptr;
		}
		if (!Was && Slot >= 0 && Unit.IsAlive())
		{
			FTMCastCasting Now;
			Now.Slot = Slot;
			Now.bChannel = !Unit.IsCasting();
			Now.Key = CastNewKey();
			CastCasting.Add(Unit.Id, Now);
			CastRaise(CastLookOf(Unit.Ability(Slot)), TMCast::EMoment::Casting, TMCast::FMomentContext(), CastAtUnit(Unit.Id), Now.Key);
		}
	}
	CastNoticeEnds();

	// Delays run out.
	for (int32 i = 0; i < CastPending.Num();)
	{
		CastPending[i].In -= DeltaSeconds;
		if (CastPending[i].In > 0.0f)
		{
			++i;
			continue;
		}
		const FTMCastPending Due = CastPending[i];
		CastPending.RemoveAt(i);
		FTMCastAt At = Due.At;
		if (At.UnitId >= 0)
		{
			// Where it is now, not where it was when the moment came.
			const FTMCastAt Now = CastAtUnit(At.UnitId);
			At.bSeen = Now.bSeen;
		}
		if (At.bSeen)
		{
			CastSpawn(Due.Event, At, Due.Key);
		}
	}

	// What plays: followers kept on what they sit on, lives run out, repeaters repeat.
	for (int32 i = CastLive.Num() - 1; i >= 0; --i)
	{
		FTMCastLive& Live = CastLive[i];
		Live.Age += DeltaSeconds;
		if (Live.bRepeater)
		{
			Live.NextRepeat -= DeltaSeconds;
			if (Live.NextRepeat <= 0.0f)
			{
				Live.NextRepeat += FMath::Max(0.1f, Live.Life);
				const TMCast::FLookEvent Once = Live.Event;
				const FTMCastAt At = Live.At;
				// CastSpawn adds to CastLive, so Live is not touched after it.
				if (At.UnitId < 0 || CastAtUnit(At.UnitId).bSeen)
				{
					CastSpawn(Once, At, 0);
				}
			}
			continue;
		}
		USceneComponent* Component = Live.Component.Get();
		if (!Component)
		{
			CastLive.RemoveAtSwap(i);
			continue;
		}
		if (Live.bFollow)
		{
			FVector Local;
			float Yaw = 0.0f;
			const char* Resolved = nullptr;
			if (CastPlace(Live.Event, Live.At, Local, Yaw, Resolved))
			{
				const FTransform Here = GetActorTransform();
				Component->SetWorldLocation(Here.TransformPosition(Local));
				const FRotator Turn(static_cast<float>(Live.Event.Rotation.X), Yaw + static_cast<float>(Live.Event.Rotation.Y),
					static_cast<float>(Live.Event.Rotation.Z));
				Component->SetWorldRotation(Here.TransformRotation(Turn.Quaternion()));
			}
		}
		if (Live.Life > 0.0f && Live.Age >= Live.Life)
		{
			if (UFXSystemComponent* Fx = Cast<UFXSystemComponent>(Component))
			{
				Fx->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
				Fx->Deactivate();
			}
			else
			{
				Component->DestroyComponent();
			}
			CastLive.RemoveAtSwap(i);
		}
	}
}

void ATMBattleDirector::CastNoticeEnds()
{
	TArray<FString> Ended;
	for (const TPair<FString, FTMCastStatus>& Each : CastStatuses)
	{
		const TMSim::FUnit* Unit = Battle.FindUnit(Each.Value.UnitId);
		if (Unit)
		{
			// A blow on its way that fells it or raises it: the body stands as it
			// was until it lands (HeldBlows), and so do the statuses it wears.
			const int32 Index = static_cast<int32>(Unit - &Battle.Units[0]);
			if (Motions.IsValidIndex(Index) && Motions[Index].HeldBlows > 0)
			{
				continue;
			}
		}
		if (!Unit || Unit->bOffBoard || !Unit->IsAlive() || !Unit->HasStatus(TCHAR_TO_UTF8(*Each.Value.Status)))
		{
			Ended.Add(Each.Key);
		}
	}
	for (const FString& Key : Ended)
	{
		const FTMCastStatus Gone = CastStatuses.FindAndRemoveChecked(Key);
		CastEnd(Gone.Key);
		const FTMCastAt At = CastAtUnit(Gone.UnitId);
		const TMSim::FAbility* Ability = Gone.Ability.IsEmpty() ? nullptr : TMSim::FindAbility(TCHAR_TO_UTF8(*Gone.Ability));
		CastRaise(CastLookOf(Ability), TMCast::EMoment::Expire, TMCast::FMomentContext(), At);
		CastRaise(CastLooks.Status(TCHAR_TO_UTF8(*Gone.Status)), TMCast::EMoment::StatusEnd, TMCast::FMomentContext(), At);
	}
}

void ATMBattleDirector::CastSwing(const FTMBlow& Blow)
{
	const TMCast::FLook* Look = CastLookOf(Blow.Ability);
	if (!Look)
	{
		return;
	}
	const FTMCastAt At = CastAtUnit(Blow.Caster);
	// How long each part of the swing plays on this body, for what is timed to it:
	// what went before the release (a picked wind-up), the release, the recover.
	TMCast::FSwingTimes Swing;
	for (int32 i = 0; i < static_cast<int32>(Battle.Units.size()) && i < Motions.Num(); ++i)
	{
		if (Battle.Units[i].Id == Blow.Caster)
		{
			const FTMMotion& Motion = Motions[i];
			Swing.Windup = Motion.LastLead;
			if (Blow.Slot >= 0 && Blow.Slot < TMSim::AbilitySlots && Motion.Picked[Blow.Slot].Recover)
			{
				Swing.Recover = Motion.Picked[Blow.Slot].Recover->GetPlayLength();
			}
		}
	}
	Swing.Release = Blow.Release ? Blow.Release->GetPlayLength() : 0.0;
	CastRaise(Look, TMCast::EMoment::Swing, TMCast::FMomentContext(), At, 0, &Swing);
	// An instant ability's cast begins with its swing; one with a cast time began at CastStarted.
	if (Blow.Ability->Cast <= 0.0f)
	{
		CastRaise(Look, TMCast::EMoment::CastStart, TMCast::FMomentContext(), At);
	}
}

void ATMBattleDirector::CastRelease(const FTMBlow& Blow)
{
	const TMCast::FLook* Look = CastLookOf(Blow.Ability);
	if (!Look)
	{
		return;
	}
	TMCast::FMomentContext Context;
	for (const TMSim::FEvent& Event : Blow.Events)
	{
		Context.bCritical = Context.bCritical || Event.Kind == TMSim::EEventKind::Critical;
	}
	CastRaise(Look, TMCast::EMoment::Release, Context, CastAtUnit(Blow.Caster));
}

uint64 ATMBattleDirector::CastShot(const FTMBlow& Blow, USceneComponent* Carrier)
{
	const TMCast::FLook* Look = CastLookOf(Blow.Ability);
	if (!Look || !Carrier || !Look->Has(TMCast::EMoment::Projectile))
	{
		return 0;
	}
	FTMCastAt At = CastAtUnit(Blow.Caster);
	At.Carrier = Carrier;
	At.bSeen = true;  // only shots someone sees get a body to ride (LaunchShots)
	const uint64 Key = CastNewKey();
	TMCast::FMomentContext Context;
	for (const TMSim::FEvent& Event : Blow.Events)
	{
		Context.bCritical = Context.bCritical || Event.Kind == TMSim::EEventKind::Critical;
	}
	CastRaise(Look, TMCast::EMoment::Projectile, Context, At, Key);
	return Key;
}

void ATMBattleDirector::CastLand(const FTMBlow& Blow)
{
	const TMCast::FLook* Look = CastLookOf(Blow.Ability);
	if (!Look || !Blow.Ability)
	{
		return;
	}
	const TMSim::FAbility& Ability = *Blow.Ability;
	const TMSim::FUnit* User = Battle.FindUnit(Blow.Caster);
	const int32 UserTeam = User ? User->HomeTeam() : -1;

	// Who it touched and how: the first of what it did to each, as today's look reads it.
	TArray<int32> Touched;
	TArray<TMCast::EOn> Results;
	TSet<int32> Critical;
	float Heaviest = 0.0f;
	for (const TMSim::FEvent& Event : Blow.Events)
	{
		if (Event.Kind == TMSim::EEventKind::Critical)
		{
			Critical.Add(Event.Unit);
		}
		if (Event.Kind == TMSim::EEventKind::Hit && Harms(Event))
		{
			if (const TMSim::FUnit* Struck = Battle.FindUnit(Event.Unit))
			{
				Heaviest = FMath::Max(Heaviest, static_cast<float>(Event.Amount) / FMath::Max(1, Struck->MaxHp()));
			}
		}
		const bool bTouch = Event.Kind == TMSim::EEventKind::Hit || Event.Kind == TMSim::EEventKind::Evaded
			|| Event.Kind == TMSim::EEventKind::Absorbed || Event.Kind == TMSim::EEventKind::Revived
			|| Event.Kind == TMSim::EEventKind::StatusApplied;
		if (bTouch && !Touched.Contains(Event.Unit))
		{
			Touched.Add(Event.Unit);
			Results.Add(Event.Kind == TMSim::EEventKind::Revived ? TMCast::EOn::Revived
				: Event.Kind == TMSim::EEventKind::Evaded ? TMCast::EOn::Evaded : TMCast::EOn::Struck);
		}
	}

	// The ground it covers, once.
	FTMCastAt Ground = CastAtUnit(Blow.Caster);
	const std::string Shape = TMSim::ShapeOf(Ability);
	const int Level = Battle.Map.NodeLevel(TMSim::FMap::NodeOf(Blow.Aim));
	const FVector Aimed = WorldFromMetres(Blow.Aim, Level) + FVector(0.0f, 0.0f, BoardHeight);
	const FVector From = User ? ShownAt(*User) : Aimed;
	Ground.Aim = Aimed;
	Ground.Centre = ToVector(TMCast::AreaCentre(Shape, Ability.MaxRange, ToCast(From), ToCast(Aimed)));
	Ground.AreaCm = static_cast<float>(TMCast::AreaAcross(Shape, Ability.Aoe, Ability.MaxRange));
	Ground.Harm = Heaviest;
	bool bSeen = User && IsSeen(*User);
	for (int32 Id : Touched)
	{
		const TMSim::FUnit* Unit = Battle.FindUnit(Id);
		bSeen = bSeen || (Unit && IsSeen(*Unit));
	}
	Ground.bSeen = bSeen;
	TMCast::FMomentContext Whole;
	Whole.bCritical = Critical.Num() > 0;
	CastRaise(Look, TMCast::EMoment::Area, Whole, Ground);

	// Each unit it touched.
	for (int32 i = 0; i < Touched.Num(); ++i)
	{
		const TMSim::FUnit* Unit = Battle.FindUnit(Touched[i]);
		if (!Unit)
		{
			continue;
		}
		FTMCastAt On = CastAtUnit(Touched[i]);
		On.AreaCm = Ground.AreaCm;
		On.Centre = Ground.Centre;
		On.Aim = Ground.Aim;
		On.Harm = Heaviest;
		TMCast::FMomentContext Context;
		Context.bCritical = Critical.Contains(Touched[i]);
		Context.Result = Results[i];
		Context.bAlly = Unit->HomeTeam() == UserTeam;
		CastRaise(Look, TMCast::EMoment::Impact, Context, On);
	}
}

void ATMBattleDirector::CastShown(const TMSim::FEvent& Event, const TMSim::FAbility* Ability, int32 CasterId)
{
	if (!GetWorld() || !GetWorld()->IsGameWorld())
	{
		return;
	}
	switch (Event.Kind)
	{
	case TMSim::EEventKind::CastStarted:
	{
		const TMSim::FUnit* User = Battle.FindUnit(Event.Unit);
		const TMSim::FAbility* Casting = User ? User->Ability(Event.Slot) : nullptr;
		CastRaise(CastLookOf(Casting), TMCast::EMoment::CastStart, TMCast::FMomentContext(), CastAtUnit(Event.Unit));
		break;
	}
	case TMSim::EEventKind::StatusApplied:
	{
		const FString Status = UTF8_TO_TCHAR(Event.Id.c_str());
		const FString Key = FString::Printf(TEXT("%d:%s"), Event.Unit, *Status);
		const FTMCastAt At = CastAtUnit(Event.Unit);
		const TMCast::FLook* StatusLook = CastLooks.Status(Event.Id);
		CastRaise(StatusLook, TMCast::EMoment::StatusGain, TMCast::FMomentContext(), At);
		if (CastStatuses.Contains(Key))
		{
			break;  // given again while it still lasts: what shows it already plays
		}
		FTMCastStatus& Given = CastStatuses.Add(Key);
		Given.UnitId = Event.Unit;
		Given.Status = Status;
		Given.Ability = Ability ? FString(UTF8_TO_TCHAR(Ability->Id.c_str())) : FString();
		Given.Key = CastNewKey();
		const TMSim::FUnit* Bearer = Battle.FindUnit(Event.Unit);
		const TMSim::FUnit* Giver = Battle.FindUnit(CasterId >= 0 ? CasterId : Event.By);
		TMCast::EMoment Moment = TMCast::EMoment::TargetStatus;
		if (Bearer && Giver && Bearer->Id == Giver->Id)
		{
			Moment = TMCast::EMoment::SelfStatus;
		}
		else if (Bearer && Giver && Bearer->HomeTeam() == Giver->HomeTeam())
		{
			Moment = TMCast::EMoment::AllyStatus;
		}
		CastRaise(CastLookOf(Ability), Moment, TMCast::FMomentContext(), At, Given.Key);
		CastRaise(StatusLook, TMCast::EMoment::StatusActive, TMCast::FMomentContext(), At, Given.Key);
		break;
	}
	case TMSim::EEventKind::Hit:
	{
		// A status's turn: from nobody, carrying the status's id.
		if (Event.By >= 0 || !TMSim::FindStatus(Event.Id))
		{
			break;
		}
		const FTMCastAt At = CastAtUnit(Event.Unit);
		if (const FTMCastStatus* Given = CastStatuses.Find(FString::Printf(TEXT("%d:%hs"), Event.Unit, Event.Id.c_str())))
		{
			const TMSim::FAbility* Gave = Given->Ability.IsEmpty() ? nullptr : TMSim::FindAbility(TCHAR_TO_UTF8(*Given->Ability));
			CastRaise(CastLookOf(Gave), TMCast::EMoment::Tick, TMCast::FMomentContext(), At);
		}
		CastRaise(CastLooks.Status(Event.Id), TMCast::EMoment::StatusTick, TMCast::FMomentContext(), At);
		break;
	}
	case TMSim::EEventKind::Reaction:
		CastRaise(CastLooks.Reaction(Event.Id), TMCast::EMoment::Reaction, TMCast::FMomentContext(), CastAtUnit(Event.Unit));
		break;
	case TMSim::EEventKind::Teleported:
	{
		if (Event.Id != "pet")
		{
			break;
		}
		// A pet called up: the ability of its owner's that calls it.
		const TMSim::FUnit* Caller = Battle.FindUnit(Event.By);
		const TMSim::FAbility* Calls = nullptr;
		for (int32 Slot = 0; Caller && Slot < TMSim::AbilitySlots && !Calls; ++Slot)
		{
			const TMSim::FAbility* Each = Caller->Ability(Slot);
			Calls = Each && Each->Special == "pet" ? Each : nullptr;
		}
		if (!Calls)
		{
			break;
		}
		if (const FTMCastPet* Was = CastPets.Find(Event.Unit))
		{
			CastEnd(Was->Key);
		}
		FTMCastPet& Pet = CastPets.Add(Event.Unit);
		Pet.Ability = UTF8_TO_TCHAR(Calls->Id.c_str());
		Pet.Key = CastNewKey();
		CastRaise(CastLookOf(Calls), TMCast::EMoment::Summon, TMCast::FMomentContext(), CastAtUnit(Event.Unit), Pet.Key);
		break;
	}
	case TMSim::EEventKind::Gone:
	{
		FTMCastPet Pet;
		if (CastPets.RemoveAndCopyValue(Event.Unit, Pet))
		{
			CastEnd(Pet.Key);
			FTMCastAt At = CastAtUnit(Event.Unit);
			At.bSeen = true;  // gone from the board, but seen going
			if (const TMSim::FUnit* Unit = Battle.FindUnit(Event.Unit))
			{
				At.bSeen = IsSeen(*Unit) || Unit->bOffBoard;
			}
			const TMSim::FAbility* Called = TMSim::FindAbility(TCHAR_TO_UTF8(*Pet.Ability));
			CastRaise(CastLookOf(Called), TMCast::EMoment::Expire, TMCast::FMomentContext(), At);
		}
		break;
	}
	default:
		break;
	}
}
