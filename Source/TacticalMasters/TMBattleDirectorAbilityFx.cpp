// What an ability looks like: what it is made of, and the effects that show it.
//
// A class file may name one particle effect for an ability ("vfx"), and many
// share the same few, so from the effects alone a fire bolt and a frost bolt
// looked alike. Here each ability is read for what it is made of -- the words
// of its name first ("Flame Lance", "Frost Dagger"), then the built-in effect
// it borrows, its element, the status it leaves, and its class's name -- and
// given a look: a flare in the user's hands as it is cast, what flies to the
// target, the hit on each unit it touches, the burst on the ground an area
// covers, a flash of its colour, and a jolt of the camera for a heavy blow. The
// effect the class file names still plays too.
//
// The effects are Fab's Paragon particles, chosen by watching the strips Unreal
// films of them (Tools\VfxCatalog.bat); each is scaled from how big it was
// filmed to the size wanted here. Nothing here is read by the rules.
//
// This is "today's look" in Cast Studio (Docs/CastStudio-Plan.md): an ability
// whose published look strips it (TMBattleDirectorCast.cpp) skips it here.

#include "TMBattleDirector.h"
#include "TMDrawable.h"

#include "CastLegacy.h"

#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"

#include "SimAbility.h"
#include "SimBattle.h"

#include <cctype>
#include <string>
#include <vector>

namespace
{
	// The table of effects, and the reading of an ability for its flavour, are
	// TMCast's now (Source/TMCast/Public/CastLegacy.h, 2026-10-02): the class lab
	// writes the same look out as Cast Studio events for "Import today's look",
	// so both must read an ability the same way.

	bool LookIsStruck(TMSim::EEventKind Kind)
	{
		return Kind == TMSim::EEventKind::Hit || Kind == TMSim::EEventKind::Absorbed
			|| Kind == TMSim::EEventKind::Revived || Kind == TMSim::EEventKind::StatusApplied;
	}

	FLinearColor LookColourOf(const TMCast::FCastVec& Colour)
	{
		return FLinearColor(static_cast<float>(Colour.X), static_cast<float>(Colour.Y), static_cast<float>(Colour.Z));
	}

	FLinearColor LookBrighter(const FLinearColor& Colour, float By)
	{
		return FLinearColor(Colour.R * By, Colour.G * By, Colour.B * By, 1.0f);
	}

	int32 SteelLook()
	{
		return TMCast::LegacyFlavourCount() - 1;
	}
}

void ATMBattleDirector::LookAssetPaths(TArray<FSoftObjectPath>& Paths) const
{
	// Read behind the loading bar with the heroes, not the first time each is
	// wanted mid-battle: that stalled the game a tenth of a second or more each
	// time (the lag hunt, 2026-09-30).
	for (int32 i = 0; i < TMCast::LegacyFlavourCount(); ++i)
	{
		const TMCast::FLegacyFlavour& Look = TMCast::LegacyFlavour(i);
		for (const TMCast::FLegacyFx* Fx : { &Look.Cast, &Look.Shot, &Look.Hit, &Look.Hit2, &Look.Burst, &Look.Burst2, &Look.Aura })
		{
			if (Fx->IsSet())
			{
				Paths.AddUnique(FSoftObjectPath(FString(UTF8_TO_TCHAR(Fx->Path))));
			}
		}
	}
	const TMCast::FLegacyCommon& Common = TMCast::LegacyCommonFx();
	for (const TMCast::FLegacyFx* Fx : { &Common.Blunt, &Common.Blunt2, &Common.Pierce, &Common.Heal, &Common.Heal2, &Common.Revive, &Common.Debuff })
	{
		Paths.AddUnique(FSoftObjectPath(FString(UTF8_TO_TCHAR(Fx->Path))));
	}
	// And every effect and sound Cast Studio's published looks name.
	for (const std::string& Effect : CastLooks.Effects())
	{
		Paths.AddUnique(FSoftObjectPath(FString(UTF8_TO_TCHAR(Effect.c_str()))));
	}
	for (const std::string& Sound : CastLooks.Sounds())
	{
		Paths.AddUnique(FSoftObjectPath(FString(UTF8_TO_TCHAR(Sound.c_str()))));
	}
	for (const TMSim::FUnit& Unit : Battle.Units)
	{
		for (int32 Slot = 0; Slot < TMSim::AbilitySlots; ++Slot)
		{
			const TMSim::FAbility* Ability = Unit.Ability(Slot);
			if (Ability && !Ability->VfxSystem.empty())
			{
				Paths.AddUnique(FSoftObjectPath(FString(UTF8_TO_TCHAR(Ability->VfxSystem.c_str()))));
			}
		}
	}
}

const TCHAR* ATMBattleDirector::LookName(const FTMLook& Look)
{
	// Made once, all of them, so each name stays where it is for the life of the program.
	static const TArray<FString> Names = []()
	{
		TArray<FString> All;
		for (int32 i = 0; i < TMCast::LegacyFlavourCount(); ++i)
		{
			All.Add(FString(UTF8_TO_TCHAR(TMCast::LegacyFlavour(i).Name)));
		}
		return All;
	}();
	return *Names[FMath::Clamp(Look.Flavour, 0, Names.Num() - 1)];
}

const ATMBattleDirector::FTMLook& ATMBattleDirector::LookOf(const TMSim::FAbility& Ability)
{
	const FString Key = UTF8_TO_TCHAR(Ability.Id.c_str());
	if (const FTMLook* Known = Looks.Find(Key))
	{
		return *Known;
	}
	// Read as the class lab reads it for Cast Studio (CastLegacy.h).
	const TMCast::FLegacyKind Kind = TMCast::LegacyKindOf(Ability);
	FTMLook Look;
	Look.Flavour = Kind.Flavour;
	Look.Weapon = static_cast<uint8>(Kind.Weapon);
	Look.bRanged = Kind.bRanged;
	Look.bArea = Kind.bArea;
	Look.bMagic = Kind.bMagic;
	return Looks.Add(Key, Look);
}

UObject* ATMBattleDirector::LoadLookFx(const FString& Key)
{
	if (Key.IsEmpty())
	{
		return nullptr;
	}
	TObjectPtr<UObject>* Known = LoadedVfx.Find(Key);
	if (!Known)
	{
		UObject* System = FSoftObjectPath(Key).TryLoad();
		if (!System || !(System->IsA<UNiagaraSystem>() || System->IsA<UParticleSystem>()))
		{
			UE_LOG(LogTemp, Warning, TEXT("An ability look names a particle effect that is not in the project: %s"), *Key);
			System = nullptr;
		}
		// One holding a material cooked without shaders is not played (TMDrawable.h).
		if (System && !TMDrawable::EffectUsable(Cast<UParticleSystem>(System), GetWorld()))
		{
			System = nullptr;
		}
		Known = &LoadedVfx.Add(Key, System);
	}
	return Known->Get();
}

UFXSystemComponent* ATMBattleDirector::PlayFx(const TMCast::FLegacyFx& Fx, const FVector& Local, float WantCm, USceneComponent* AttachTo)
{
	if (!Fx.IsSet())
	{
		return nullptr;
	}
	const FString Path = UTF8_TO_TCHAR(Fx.Path);
	return PlayFx(*Path, Local, WantCm, static_cast<float>(Fx.SizeCm), AttachTo);
}

UFXSystemComponent* ATMBattleDirector::PlayFx(const TCHAR* Path, const FVector& Local, float WantCm, float SizeCm,
	USceneComponent* AttachTo)
{
	if (!Path || !*Path)
	{
		return nullptr;
	}
	UObject* const System = LoadLookFx(FString(Path));
	if (!System)
	{
		return nullptr;
	}
	const float Size = FMath::Clamp(WantCm / FMath::Max(SizeCm, 10.0f), 0.08f, 6.0f);
	UFXSystemComponent* Playing = nullptr;
	if (AttachTo)
	{
		if (UNiagaraSystem* Niagara = Cast<UNiagaraSystem>(System))
		{
			Playing = UNiagaraFunctionLibrary::SpawnSystemAttached(Niagara, AttachTo, NAME_None, FVector::ZeroVector,
				FRotator::ZeroRotator, FVector(Size), EAttachLocation::KeepRelativeOffset, true, ENCPoolMethod::None);
		}
		else if (UParticleSystem* Cascade = Cast<UParticleSystem>(System))
		{
			Playing = UGameplayStatics::SpawnEmitterAttached(Cascade, AttachTo, NAME_None, FVector::ZeroVector,
				FRotator::ZeroRotator, FVector(Size), EAttachLocation::KeepRelativeOffset, true);
		}
		if (Playing)
		{
			// Its own size, not the size of what carries it.
			Playing->SetUsingAbsoluteScale(true);
			Playing->SetWorldScale3D(FVector(Size));
		}
		return Playing;
	}
	const FVector Where = GetActorTransform().TransformPosition(Local);
	if (UNiagaraSystem* Niagara = Cast<UNiagaraSystem>(System))
	{
		Playing = UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, Niagara, Where, FRotator::ZeroRotator, FVector(Size));
	}
	else if (UParticleSystem* Cascade = Cast<UParticleSystem>(System))
	{
		Playing = UGameplayStatics::SpawnEmitterAtLocation(this, Cascade, Where, FRotator::ZeroRotator, FVector(Size));
	}
	if (Playing)
	{
		// Switched off after a while, like a class's own effect (AdvanceVfx):
		// much of it loops.
		PlayingVfx.Add({ Playing, 0.0f });
		++EffectsPlayed;
	}
	return Playing;
}

void ATMBattleDirector::Pulse(const FVector& Local, const FLinearColor& Colour, float Brightness, float Radius, float Seconds)
{
	UPointLightComponent* Light = NewObject<UPointLightComponent>(this, NAME_None, RF_Transient);
	Light->SetupAttachment(RootComponent);
	Light->RegisterComponent();
	Light->SetRelativeLocation(Local);
	Light->SetLightColor(Colour);
	Light->SetIntensity(Brightness);
	Light->SetAttenuationRadius(Radius);
	Light->SetCastShadows(false);
	FTMPulse& Each = Pulses.AddDefaulted_GetRef();
	Each.Light = Light;
	Each.Life = Seconds;
	Each.Peak = Brightness;
}

void ATMBattleDirector::Jolt(float Strength)
{
	// The strongest of what lands together, not the sum: ten arrows are not
	// ten earthquakes.
	if (Strength > JoltStrength * (JoltLeft / 0.35f))
	{
		JoltStrength = FMath::Min(Strength, 1.5f);
		JoltLeft = 0.35f;
	}
}

void ATMBattleDirector::AdvanceLooks(float DeltaSeconds)
{
	for (int32 i = Pulses.Num() - 1; i >= 0; --i)
	{
		FTMPulse& Each = Pulses[i];
		Each.Age += DeltaSeconds;
		if (!Each.Light.IsValid() || Each.Age >= Each.Life)
		{
			if (Each.Light.IsValid())
			{
				Each.Light->DestroyComponent();
			}
			Pulses.RemoveAtSwap(i);
			continue;
		}
		const float Left = 1.0f - Each.Age / Each.Life;
		Each.Light->SetIntensity(Each.Peak * Left * Left);
	}

	if (LookCaptureIn >= 0.0f)
	{
		LookCaptureIn -= DeltaSeconds;
		if (LookCaptureIn < 0.0f)
		{
			bWorthSeeing = true;
		}
	}

	JoltClock += DeltaSeconds;
	if (JoltLeft > 0.0f)
	{
		JoltLeft = FMath::Max(0.0f, JoltLeft - DeltaSeconds);
		// A few centimetres for a sword, a hand's breadth for a meteor, fading
		// fast; shaken on three unrelated beats so it never settles into a sway.
		const float Amount = JoltStrength * (JoltLeft / 0.35f) * 9.0f * FMath::Max(1.0f, CamDistance / 1500.0f);
		JoltOffset = FVector(FMath::Sin(JoltClock * 71.0f), FMath::Sin(JoltClock * 53.0f + 1.3f),
			FMath::Sin(JoltClock * 61.0f + 2.1f) * 0.6f) * Amount;
	}
	else
	{
		JoltOffset = FVector::ZeroVector;
		JoltStrength = 0.0f;
	}
}

void ATMBattleDirector::LookCast(const FTMBlow& Blow)
{
	if (!Blow.Ability || Blow.Ability->Kind == "passive")
	{
		return;
	}
	const TMSim::FUnit* User = Battle.FindUnit(Blow.Caster);
	if (!User || !IsSeen(*User))
	{
		return;
	}
	const FTMLook& Look = LookOf(*Blow.Ability);
	const TMCast::FLegacyFlavour& Set = TMCast::LegacyFlavour(Look.Flavour);
	const FVector At = ShownAt(*User);
	const std::string Shape = TMSim::ShapeOf(*Blow.Ability);
	const bool bOnSelf = Shape == "self" && Blow.Ability->Aoe <= 0.0f;

	// Something it does to itself shows now, round it: a buff's aura, a heal.
	if (bOnSelf && Blow.Ability->Effect != TMSim::EEffect::Damage)
	{
		if (Blow.Ability->Effect == TMSim::EEffect::Heal)
		{
			PlayFx(TMCast::LegacyCommonFx().Heal, At + FVector(0.0f, 0.0f, 90.0f), 190.0f);
			Pulse(At + FVector(0.0f, 0.0f, 120.0f), FLinearColor(0.35f, 1.0f, 0.5f), 9000.0f, 380.0f, 0.6f);
		}
		else
		{
			PlayFx(Set.Aura, At + FVector(0.0f, 0.0f, 60.0f), 200.0f);
			Pulse(At + FVector(0.0f, 0.0f, 120.0f), LookColourOf(Set.Colour), 8000.0f, 380.0f, 0.6f);
		}
		return;
	}
	// Magic gathers in the hands before it goes.
	if (Look.bMagic || Blow.Ability->Effect != TMSim::EEffect::Damage)
	{
		const int32 Index = Battle.Units.empty() ? INDEX_NONE : static_cast<int32>(User - &Battle.Units[0]);
		const float Yaw = Motions.IsValidIndex(Index) ? Motions[Index].Yaw : 0.0f;
		const FVector Hands = At + FVector(0.0f, 0.0f, 115.0f) + FRotator(0.0f, Yaw, 0.0f).Vector() * 45.0f;
		PlayFx(Set.Cast, Hands, Blow.Slot == 3 ? 200.0f : 130.0f);
		Pulse(Hands, LookColourOf(Set.Colour), Blow.Slot == 3 ? 14000.0f : 7000.0f, 320.0f, 0.5f);
	}
}

void ATMBattleDirector::LookLand(const FTMBlow& Blow)
{
	if (!Blow.Ability)
	{
		return;
	}
	const TMSim::FAbility& Ability = *Blow.Ability;
	const FTMLook& Look = LookOf(Ability);
	const TMCast::FLegacyFlavour& Set = TMCast::LegacyFlavour(Look.Flavour);
	const TMSim::FUnit* User = Battle.FindUnit(Blow.Caster);

	if (CaptureEverySeconds > 0.0f && User && IsSeen(*User))
	{
		LookCaptureIn = 0.2f;
	}

	// Who it touched, and how hard.
	TArray<int32> Touched;
	TArray<int32> Critical;
	float Heaviest = 0.0f;
	for (const TMSim::FEvent& Event : Blow.Events)
	{
		if (Event.Kind == TMSim::EEventKind::Critical)
		{
			Critical.AddUnique(Event.Unit);
		}
		if (Event.Kind == TMSim::EEventKind::Hit && Harms(Event))
		{
			if (const TMSim::FUnit* Struck = Battle.FindUnit(Event.Unit))
			{
				Heaviest = FMath::Max(Heaviest, static_cast<float>(Event.Amount) / FMath::Max(1, Struck->MaxHp()));
			}
		}
	}

	// The ground an area covers bursts, sized to it.
	if (Look.bArea && User)
	{
		const std::string Shape = TMSim::ShapeOf(Ability);
		const FVector From = ShownAt(*User);
		const int Level = Battle.Map.NodeLevel(TMSim::FMap::NodeOf(Blow.Aim));
		const FVector Aimed = WorldFromMetres(Blow.Aim, Level) + FVector(0.0f, 0.0f, BoardHeight);
		FVector Centre = Aimed;
		float Across = FMath::Max(2.0f * Ability.Aoe * TileSize, 220.0f);
		if (Shape == "self" || (Shape == "circle" && Ability.MaxRange <= 0.0f))
		{
			Centre = From + FVector(0.0f, 0.0f, 10.0f);
		}
		else if (Shape == "cone")
		{
			const FVector Way = (Aimed - From).GetSafeNormal2D();
			Centre = From + Way * Ability.MaxRange * TileSize * 0.55f;
			Across = FMath::Max(Ability.MaxRange * TileSize, 250.0f);
		}
		else if (Shape == "line" || Shape == "vector")
		{
			Centre = FMath::Lerp(From, Aimed, 0.6f);
			Across = FMath::Max(Ability.Aoe * 2.0f * TileSize, 260.0f);
		}
		Across = FMath::Min(Across, 1400.0f);
		const bool bHarms = Ability.Effect == TMSim::EEffect::Damage
			|| (Ability.Effect == TMSim::EEffect::Support && Ability.Target == TMSim::ETargetSide::Enemy);
		bool bSeen = User && IsSeen(*User);
		for (const TMSim::FUnit& Unit : Battle.Units)
		{
			bSeen = bSeen || (Unit.IsAlive() && IsSeen(Unit) && FVector::Dist2D(ShownAt(Unit), Centre) < Across);
		}
		if (bSeen)
		{
			if (bHarms)
			{
				PlayFx(Set.Burst, Centre, Across);
				PlayFx(Set.Burst2, Centre, Across * 0.8f);
				Pulse(Centre + FVector(0.0f, 0.0f, 150.0f), LookBrighter(LookColourOf(Set.Colour), 1.0f), Blow.Slot == 3 ? 60000.0f : 30000.0f,
					Across * 1.3f, 0.55f);
				Jolt(Blow.Slot == 3 ? 1.2f : 0.55f + Heaviest);
			}
			else
			{
				// A blessing on everyone standing in it.
				const TMCast::FLegacyFx& Glow = Ability.Effect == TMSim::EEffect::Heal ? TMCast::LegacyCommonFx().Heal : Set.Aura;
				PlayFx(Glow, Centre, Across * 0.7f);
				Pulse(Centre + FVector(0.0f, 0.0f, 150.0f), Ability.Effect == TMSim::EEffect::Heal
					? FLinearColor(0.35f, 1.0f, 0.5f) : LookColourOf(Set.Colour), 16000.0f, Across * 1.2f, 0.7f);
			}
		}
	}
	else if (Heaviest > 0.0f)
	{
		// A single blow jolts as hard as it hurt: a scratch barely, a third of
		// someone's health properly.
		Jolt(FMath::Clamp(Heaviest * 2.0f, 0.12f, 0.9f) * (Critical.Num() > 0 ? 1.5f : 1.0f) * (Blow.Slot == 3 ? 1.5f : 1.0f));
	}

	for (const TMSim::FEvent& Event : Blow.Events)
	{
		if (LookIsStruck(Event.Kind) && !Touched.Contains(Event.Unit))
		{
			Touched.Add(Event.Unit);
			LookOn(Ability, Event, Critical.Contains(Event.Unit), Look.bArea);
		}
	}
}

void ATMBattleDirector::LookOn(const TMSim::FAbility& Ability, const TMSim::FEvent& Event, bool bCritical, bool bArea)
{
	const TMSim::FUnit* Unit = Battle.FindUnit(Event.Unit);
	if (!Unit || !IsSeen(*Unit))
	{
		return;
	}
	const FTMLook& Look = LookOf(Ability);
	const TMCast::FLegacyFlavour& Set = TMCast::LegacyFlavour(Look.Flavour);
	const FVector Body = ShownAt(*Unit) + FVector(0.0f, 0.0f, 95.0f);
	const float Big = (bCritical ? 1.4f : 1.0f) * (bArea ? 0.75f : 1.0f);

	if (Event.Kind == TMSim::EEventKind::Revived)
	{
		PlayFx(TMCast::LegacyCommonFx().Revive, Body, 260.0f);
		PlayFx(TMCast::LegacyCommonFx().Heal, Body, 200.0f);
		Pulse(Body + FVector(0.0f, 0.0f, 80.0f), FLinearColor(1.0f, 0.9f, 0.55f), 22000.0f, 450.0f, 0.9f);
		return;
	}
	if (Ability.Effect == TMSim::EEffect::Heal)
	{
		PlayFx(TMCast::LegacyCommonFx().Heal, Body, 190.0f);
		PlayFx(TMCast::LegacyCommonFx().Heal2, Body, 150.0f);
		Pulse(Body, FLinearColor(0.35f, 1.0f, 0.5f), 9000.0f, 350.0f, 0.6f);
		return;
	}
	if (Ability.Effect == TMSim::EEffect::Support && Ability.Target != TMSim::ETargetSide::Enemy)
	{
		PlayFx(Set.Aura, ShownAt(*Unit) + FVector(0.0f, 0.0f, 60.0f), 190.0f);
		Pulse(Body, LookColourOf(Set.Colour), 7000.0f, 320.0f, 0.6f);
		return;
	}

	// A blow, or a curse: what it is made of, on the one it struck.
	if (Look.bMagic)
	{
		// Sized for the game's camera, which frames the board from well back.
		PlayFx(Set.Hit, Body, 230.0f * Big);
		PlayFx(Set.Hit2, Body, 200.0f * Big);
	}
	else
	{
		switch (Look.Weapon)
		{
		case 1:
		case 3:
			PlayFx(TMCast::LegacyCommonFx().Blunt, Body, 170.0f * Big);
			PlayFx(TMCast::LegacyCommonFx().Blunt2, Body - FVector(0.0f, 0.0f, 60.0f), 150.0f * Big);
			break;
		case 2:
			PlayFx(TMCast::LegacyCommonFx().Pierce, Body, 150.0f * Big);
			break;
		default:
			PlayFx(Set.Hit, Body, 200.0f * Big);
			break;
		}
	}
	if (Ability.Effect == TMSim::EEffect::Support && (Look.Flavour == SteelLook() || Look.Flavour == TMCast::LegacyFlavourNamed("shadow")))
	{
		PlayFx(TMCast::LegacyCommonFx().Debuff, ShownAt(*Unit) + FVector(0.0f, 0.0f, 40.0f), 170.0f);
	}
	Pulse(Body, LookBrighter(LookColourOf(Set.Colour), bCritical ? 1.3f : 1.0f), (Look.bMagic ? 12000.0f : 6000.0f) * Big, 300.0f * Big, 0.35f);
}

UFXSystemComponent* ATMBattleDirector::LookShot(const FTMLook& Look, USceneComponent* Carrier)
{
	const TMCast::FLegacyFlavour& Set = TMCast::LegacyFlavour(Look.Flavour);
	return Carrier ? PlayFx(Set.Shot, FVector::ZeroVector, 110.0f, Carrier) : nullptr;
}

FLinearColor ATMBattleDirector::LookColour(const FTMLook& Look)
{
	return LookColourOf(TMCast::LegacyFlavour(Look.Flavour).Colour);
}
