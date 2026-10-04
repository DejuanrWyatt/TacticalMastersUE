// How a blow lands: the moment it connects, what flies there, how the struck
// react, what their statuses look like, and the big moments of a battle.
//
// The rules settle an ability in an instant -- the swing, the dice, the damage,
// the fall -- and report it all at once. Shown that way, the number rises and
// the target falls before the sword has left its sheath. So the view holds on
// to what an ability did (a "blow") and shows it when the swing connects: part
// way into the release clip, or when the arrow, bolt or stone arrives.
//
// None of this is read by the rules. Holding a blow back changes when it is
// seen, never what happened: the health bars and the log already have it.

#include "TMBattleDirector.h"
#include "TMSettings.h"

#include "Animation/AnimSequence.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "Particles/ParticleSystemComponent.h"

#include "SimAbility.h"
#include "SimTypes.h"

namespace
{
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

	/** Everything an ability does to its targets, which waits for it to land. */
	bool IsConsequence(TMSim::EEventKind Kind)
	{
		switch (Kind)
		{
		case TMSim::EEventKind::Hit:
		case TMSim::EEventKind::Evaded:
		case TMSim::EEventKind::Grazed:
		case TMSim::EEventKind::Critical:
		case TMSim::EEventKind::Absorbed:
		case TMSim::EEventKind::StatusApplied:
		case TMSim::EEventKind::GaugeChanged:
		case TMSim::EEventKind::Knocked:
		case TMSim::EEventKind::Revived:
		case TMSim::EEventKind::Gone:
		// A cast lost to the killing blow: the loss is shown at once by the
		// body (AnimateEvents), and kept here only so the fall that follows it
		// stays with its blow.
		case TMSim::EEventKind::CastFizzled:
			return true;
		default:
			return false;
		}
	}

	/**
	 * How far into its release clip each motion connects, as a share of the
	 * clip. A set can say otherwise per motion ("impact"), once someone has
	 * watched its clips.
	 */
	float UsualImpact(const FString& Motion)
	{
		if (Motion == TEXT("melee")) { return 0.40f; }
		if (Motion == TEXT("heavy")) { return 0.50f; }
		if (Motion == TEXT("dash")) { return 0.45f; }
		if (Motion == TEXT("shoot")) { return 0.30f; }
		if (Motion == TEXT("bolt")) { return 0.40f; }
		return 0.50f;
	}

	/** In the same order as TMCast::ELegacyShot, which says what flies. */
	enum class EShot : uint8 { None, Arrow, Orb, Stone };

	/**
	 * What an ability throws today, read as Cast Studio reads it
	 * (TMCast::LegacyShotOf: a stone for throw_stone, an arrow for a shot, an
	 * orb for a damaging bolt or a spell thrown from afar).
	 */
	EShot ShotOf(const TMSim::FAbility& Ability, const FString& Motion)
	{
		return static_cast<EShot>(static_cast<uint8>(TMCast::LegacyShotOf(Ability, TCHAR_TO_UTF8(*Motion))));
	}

	/** A colour for what it is made of, read from the built-in effect it borrows (its "fx"). */
	FLinearColor ShotColour(const TMSim::FAbility& Ability, EShot Shot)
	{
		const TMCast::FCastVec Colour = TMCast::LegacyShotColour(Ability, static_cast<TMCast::ELegacyShot>(static_cast<uint8>(Shot)));
		return FLinearColor(static_cast<float>(Colour.X), static_cast<float>(Colour.Y), static_cast<float>(Colour.Z));
	}

	/** How a status shows on its bearer: a glow, and how its clips play. */
	struct FStatusLook
	{
		const char* Id;
		FLinearColor Colour;
		/** Pulses a second; zero for a steady glow. */
		float Pulse;
		bool bFlicker;
		/** How fast its clips play; 0 holds it still. */
		float Rate;
	};

	/** In order of which shows when a unit carries several: the most telling first. */
	const FStatusLook GLooks[] =
	{
		{ "freeze",    FLinearColor(0.55f, 0.85f, 1.0f), 0.0f, false, 0.0f },
		{ "stop",      FLinearColor(0.7f, 0.65f, 1.0f),  0.0f, false, 0.0f },
		{ "charmed",   FLinearColor(1.0f, 0.5f, 0.76f),  1.5f, false, 1.0f },
		{ "terrified", FLinearColor(0.65f, 0.5f, 0.85f), 3.0f, false, 1.3f },
		{ "stun",      FLinearColor(1.0f, 0.95f, 0.4f),  2.5f, false, 0.6f },
		{ "sleep",     FLinearColor(0.5f, 0.5f, 1.0f),   0.4f, false, 0.3f },
		{ "doom",      FLinearColor(0.6f, 0.1f, 0.8f),   1.2f, false, 1.0f },
		{ "burn",      FLinearColor(1.0f, 0.45f, 0.1f),  0.0f, true,  1.0f },
		{ "bleed",     FLinearColor(0.8f, 0.05f, 0.05f), 1.0f, false, 1.0f },
		{ "marked",    FLinearColor(1.0f, 0.3f, 0.3f),   2.0f, false, 1.0f },
		{ "decay",     FLinearColor(0.56f, 0.68f, 0.28f), 0.8f, false, 1.0f },
		{ "wounded",   FLinearColor(0.75f, 0.32f, 0.36f), 0.8f, false, 1.0f },
		{ "reflect",   FLinearColor(0.85f, 0.94f, 1.0f), 0.0f, false, 1.0f },
		{ "invuln",    FLinearColor(1.0f, 0.85f, 0.3f),  0.0f, false, 1.0f },
		{ "shield",    FLinearColor(0.5f, 0.75f, 1.0f),  0.0f, false, 1.0f },
		{ "barrier",   FLinearColor(0.5f, 0.75f, 1.0f),  0.0f, false, 1.0f },
		{ "slow",      FLinearColor(0.4f, 0.5f, 1.0f),   0.5f, false, 0.6f },
		{ "root",      FLinearColor(0.45f, 0.35f, 0.15f), 0.0f, false, 1.0f },
		{ "silence",   FLinearColor(0.7f, 0.4f, 1.0f),   0.8f, false, 1.0f },
		{ "taunt",     FLinearColor(1.0f, 0.2f, 0.2f),   1.5f, false, 1.0f },
		{ "regen",     FLinearColor(0.3f, 1.0f, 0.4f),   0.6f, false, 1.0f },
		{ "immunity",  FLinearColor(1.0f, 1.0f, 1.0f),   0.0f, false, 1.0f },
		{ "relentless", FLinearColor(1.0f, 0.5f, 0.2f),  1.0f, false, 1.0f },
		{ "stride",    FLinearColor(0.45f, 1.0f, 0.55f), 0.0f, false, 1.2f },
		{ "haste",     FLinearColor(1.0f, 0.87f, 0.35f), 0.0f, false, 1.4f },
		{ "chilled",   FLinearColor(0.65f, 0.88f, 1.0f), 0.0f, false, 0.8f },
		{ "protect",   FLinearColor(0.9f, 0.76f, 0.45f), 0.0f, false, 1.0f },
		{ "shell",     FLinearColor(0.7f, 0.55f, 1.0f),  0.0f, false, 1.0f },
		// 2026-10-03: statuses classes now give. Vanished is seen only by its own side: a dim, smoky breath.
		{ "veil",      FLinearColor(0.32f, 0.3f, 0.45f), 0.6f, false, 1.0f },
		{ "reraise",   FLinearColor(1.0f, 0.9f, 0.6f),   0.5f, false, 1.0f },
		{ "wet",       FLinearColor(0.3f, 0.55f, 1.0f),  0.0f, false, 1.0f },
		{ "oiled",     FLinearColor(0.5f, 0.42f, 0.2f),  0.0f, false, 1.0f },
	};

	/** The brightness of a status glow, well under the ready light's. */
	constexpr float GlowBrightness = 4000.0f;

	/**
	 * The colour a status's name rises in when it lands: its glow's, lightened to
	 * read as words, with a few set by hand where the glow is too dark or too like
	 * another ("Combat Text Mockups" A).
	 */
	FColor StatusWordTint(const std::string& Id)
	{
		static const TMap<FString, FColor> ByHand =
		{
			{ TEXT("burn"), FColor(255, 138, 42) },
			{ TEXT("bleed"), FColor(214, 51, 108) },
			{ TEXT("chilled"), FColor(159, 216, 255) },
			{ TEXT("freeze"), FColor(221, 243, 255) },
			{ TEXT("stun"), FColor(255, 216, 77) },
			{ TEXT("root"), FColor(168, 198, 108) },
			{ TEXT("charmed"), FColor(255, 143, 208) },
			{ TEXT("silence"), FColor(185, 140, 255) },
			{ TEXT("haste"), FColor(95, 224, 208) },
			{ TEXT("regen"), FColor(155, 232, 168) },
			{ TEXT("slow"), FColor(160, 168, 192) },
			{ TEXT("blind"), FColor(140, 147, 166) },
			{ TEXT("taunt"), FColor(255, 112, 64) },
			{ TEXT("shield"), FColor(140, 200, 255) },
			{ TEXT("barrier"), FColor(140, 200, 255) },
		};
		if (const FColor* Set = ByHand.Find(FString(UTF8_TO_TCHAR(Id.c_str()))))
		{
			return *Set;
		}
		for (const FStatusLook& Look : GLooks)
		{
			if (Id == Look.Id)
			{
				return FMath::Lerp(Look.Colour, FLinearColor::White, 0.3f).ToFColor(false);
			}
		}
		return FColor(224, 153, 255);
	}
}

const TArray<FString>& ATMBattleDirector::ExtraKeys()
{
	static const TArray<FString> Keys =
	{
		TEXT("hitFront"), TEXT("hitBack"), TEXT("hitLeft"), TEXT("hitRight"), TEXT("hitHeavy"),
		TEXT("evade"), TEXT("evadeLeft"), TEXT("evadeRight"), TEXT("block"),
		TEXT("deathFront"), TEXT("deathBack"), TEXT("deathLeft"), TEXT("deathRight"),
		TEXT("stunned"), TEXT("sleep"),
		TEXT("ready"), TEXT("victory"), TEXT("defeat"),
		// Leaps (2026-10-03): up, and down again. Found beside the idle by name when a set doesn't say.
		TEXT("jump"), TEXT("land"),
	};
	return Keys;
}

bool ATMBattleDirector::Harms(const TMSim::FEvent& Event)
{
	if (Event.Kind != TMSim::EEventKind::Hit)
	{
		return false;
	}
	// From an ability, it harms if the ability does damage: a heal carries its
	// healer too, so who is behind it says nothing. From nobody, it is a status
	// ticking -- a burn harms, regen heals -- or the ground.
	// Decay rotting a heal, and a Suppressed unit's free blow, always hurt.
	if (Event.Id == "decay" || Event.Id == "suppressed")
	{
		return true;
	}
	if (Event.By >= 0)
	{
		const TMSim::FAbility* Ability = TMSim::FindAbility(Event.Id);
		return !Ability || Ability->Effect == TMSim::EEffect::Damage;
	}
	const TMSim::FStatusDef* Status = TMSim::FindStatus(Event.Id);
	return !Status || Status->PerTurn < 0.0f;
}

void ATMBattleDirector::GatherBlows(const TMSim::FTickReport& Report)
{
	int32 Open = INDEX_NONE;
	const int32 FirstNew = Blows.Num();
	for (const TMSim::FEvent& Event : Report.Events)
	{
		if (Event.Kind == TMSim::EEventKind::Resolved)
		{
			FTMBlow& Blow = Blows.AddDefaulted_GetRef();
			Blow.Caster = Event.Unit;
			Blow.Ability = TMSim::FindAbility(Event.Id);
			Blow.Aim = Event.Where;
			Blow.Motion = Blow.Ability ? FString(UTF8_TO_TCHAR(TMSim::MotionOf(*Blow.Ability, Event.Slot).c_str())) : FString(TEXT("none"));
			Blow.Slot = Event.Slot;
			Open = Blows.Num() - 1;
			// An effect on the user plays as it goes off; the rest wait to land.
			const TMSim::FUnit* User = Battle.FindUnit(Event.Unit);
			if (Blow.Ability && !Blow.Ability->VfxSystem.empty() && Blow.Ability->VfxAt == "user" && User && IsSeen(*User)
				&& CastStrip(Blow.Ability) == TMCast::EStrip::None)
			{
				PlayVfx(*Blow.Ability, WorldFor(*User) + FVector(0.0f, 0.0f, VfxHeight));
			}
			if (Event.Slot == 3 && Blow.Ability && Blow.Motion != TEXT("none"))
			{
				UltimateBeat(Event);
			}
		}
		else if (IsConsequence(Event.Kind) && Open != INDEX_NONE)
		{
			Blows[Open].Events.Add(Event);
		}
		else
		{
			Open = INDEX_NONE;
			ShowOne(Event, nullptr, -1, nullptr);
		}
	}

	for (int32 b = FirstNew; b < Blows.Num(); ++b)
	{
		FTMBlow& Blow = Blows[b];
		const int32 Caster = IndexOfUnit(Battle, Blow.Caster);
		float Share = UsualImpact(Blow.Motion);
		float Lead = 0.0f;
		if (Motions.IsValidIndex(Caster))
		{
			const FTMMotion& Motion = Motions[Caster];
			Blow.Release = Motion.LastRelease;
			// Walking there first: it starts when the walk ends and the swing
			// begins (the queued clip, AdvanceMotion).
			Blow.bStarted = Motion.Queued == nullptr;
			if (Motion.Body && Motion.Body->Animations)
			{
				const FTMMotionClips* Clips = FindMotion(*Motion.Body->Animations, Blow.Motion);
				if (Clips && Clips->Impact >= 0.0f)
				{
					Share = Clips->Impact;
				}
			}
			// Cast Studio's contact for it, and a picked wind-up played before the
			// release (AnimateEvents): the blow connects when the designer said.
			if (Motion.LastContact >= 0.0f)
			{
				Share = Motion.LastContact;
			}
			Lead = Motion.LastLead;
		}
		else
		{
			Blow.bStarted = true;
		}
		// No motion, no swing to wait for -- unless Cast Studio gave it a release.
		const bool bPickedSwing = Motions.IsValidIndex(Caster) && Blow.Slot >= 0 && Blow.Slot < TMSim::AbilitySlots
			&& Motions[Caster].Picked[Blow.Slot].Release != nullptr;
		Blow.ImpactAt = Blow.Release && (Blow.Motion != TEXT("none") || bPickedSwing)
			? static_cast<float>(TMCast::ContactSeconds(Lead, Blow.Release->GetPlayLength(), Share)) : 0.0f;

		// Whoever this fells or raises stays as they are until it lands.
		for (const TMSim::FEvent& Event : Blow.Events)
		{
			if (Event.Kind == TMSim::EEventKind::Knocked || Event.Kind == TMSim::EEventKind::Revived
				|| Event.Kind == TMSim::EEventKind::Gone)
			{
				const int32 Struck = IndexOfUnit(Battle, Event.Unit);
				if (Motions.IsValidIndex(Struck))
				{
					++Motions[Struck].HeldBlows;
					Blow.Held.Add(Struck);
				}
			}
		}
	}
}

void ATMBattleDirector::AdvanceBlows(float DeltaSeconds)
{
	for (int32 b = 0; b < Blows.Num();)
	{
		FTMBlow& Blow = Blows[b];
		Blow.Age += DeltaSeconds;
		if (!Blow.bStarted)
		{
			const int32 Caster = IndexOfUnit(Battle, Blow.Caster);
			// A walk can be cut short, and a caster can fall on the way; a blow
			// is never left waiting for ever.
			if (!Motions.IsValidIndex(Caster) || Motions[Caster].Queued == nullptr || Blow.Age > 6.0f)
			{
				Blow.bStarted = true;
			}
		}
		bool bLanded = Blow.Age > 10.0f;
		if (Blow.bStarted && !Blow.bSounded)
		{
			// The swing begins (after any walk there): heard now, landing later.
			Blow.bSounded = true;
			SoundBlowStarts(Blow, Blow.Slot);
			if (CastStrip(Blow.Ability) != TMCast::EStrip::All)
			{
				LookCast(Blow);
			}
			CastSwing(Blow);
		}
		if (Blow.bStarted && !bLanded)
		{
			Blow.Since += DeltaSeconds;
			if (!Blow.bLaunched && Blow.Since >= Blow.ImpactAt)
			{
				Blow.bLaunched = true;
				CastRelease(Blow);
				LaunchShots(Blow);
			}
			if (Blow.bLaunched)
			{
				bool bFlying = false;
				for (FTMShot& Shot : Blow.Shots)
				{
					Shot.Age += DeltaSeconds;
					const float Part = Shot.Age / Shot.Flight;
					if (Part >= 1.0f || !Shot.Mesh.IsValid())
					{
						CastEnd(Shot.CastKey);
						Shot.CastKey = 0;
						if (Shot.Trail.IsValid())
						{
							Shot.Trail->DestroyComponent();
						}
						if (Shot.Mesh.IsValid())
						{
							Shot.Mesh->DestroyComponent();
						}
						continue;
					}
					bFlying = true;
					// Along a line, lifted into an arc, pointing the way it flies.
					const FVector Where = FMath::Lerp(Shot.From, Shot.To, Part) + FVector(0.0f, 0.0f, Shot.Arc * 4.0f * Part * (1.0f - Part));
					const FVector Heading = (Shot.To - Shot.From) + FVector(0.0f, 0.0f, Shot.Arc * 4.0f * (1.0f - 2.0f * Part));
					Shot.Mesh->SetRelativeLocation(Where);
					if (!Heading.IsNearlyZero())
					{
						Shot.Mesh->SetRelativeRotation(FRotationMatrix::MakeFromZ(Heading.GetSafeNormal()).Rotator());
					}
				}
				bLanded = !bFlying;
			}
		}
		if (bLanded)
		{
			FTMBlow Landing = MoveTemp(Blow);
			Blows.RemoveAt(b);
			LandBlow(Landing);
			continue;
		}
		++b;
	}
}

void ATMBattleDirector::LaunchShots(FTMBlow& Blow)
{
	if (!Blow.Ability)
	{
		return;
	}
	const FTMLook& Look = LookOf(*Blow.Ability);
	// A spell thrown from afar flies there, whatever the motion that throws it;
	// earth throws a stone.
	EShot Shot = ShotOf(*Blow.Ability, Blow.Motion);
	// Cast Studio (TMBattleDirectorCast.cpp): its projectile events ride each
	// shot. Stripped of today's look, the shot keeps no trail or glow of its
	// own, and hides its plain ball, rod or stone under an authored effect.
	// Nothing flies today but something was authored to: an unseen ball carries it.
	const TMCast::FLook* Authored = CastLookOf(Blow.Ability);
	const bool bStripped = CastStrip(Blow.Ability) == TMCast::EStrip::All;
	const bool bCarrierOnly = Shot == EShot::None && Authored && Authored->Has(TMCast::EMoment::Projectile);
	if (bCarrierOnly)
	{
		Shot = EShot::Orb;
	}
	const int32 Caster = IndexOfUnit(Battle, Blow.Caster);
	if (Shot == EShot::None || !Motions.IsValidIndex(Caster))
	{
		return;
	}
	if (!ShotSphere)
	{
		ShotSphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		ShotRod = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
		ShotMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	}
	UStaticMesh* Mesh = Shot == EShot::Arrow ? ShotRod : ShotSphere;
	if (!Mesh)
	{
		return;
	}

	// Everything it touched, once each; nothing touched, the spot aimed at.
	TArray<int32> Targets;
	for (const TMSim::FEvent& Event : Blow.Events)
	{
		if ((Event.Kind == TMSim::EEventKind::Hit || Event.Kind == TMSim::EEventKind::Evaded
			|| Event.Kind == TMSim::EEventKind::Absorbed) && !Targets.Contains(Event.Unit))
		{
			Targets.Add(Event.Unit);
		}
	}
	const FTMMotion& From = Motions[Caster];
	const FVector Forward = FRotator(0.0f, From.Yaw, 0.0f).Vector();
	const FVector Start = From.Shown + FVector(0.0f, 0.0f, 130.0f) + Forward * 40.0f;
	TArray<FVector> Ends;
	TArray<bool> Seen;
	const TMSim::FUnit& User = Battle.Units[Caster];
	for (int32 Target : Targets)
	{
		const TMSim::FUnit* Struck = Battle.FindUnit(Target);
		if (Struck)
		{
			Ends.Add(ShownAt(*Struck) + FVector(0.0f, 0.0f, 100.0f));
			Seen.Add(IsSeen(User) || IsSeen(*Struck));
		}
	}
	if (Ends.Num() == 0)
	{
		const int Level = Battle.Map.NodeLevel(TMSim::FMap::NodeOf(Blow.Aim));
		Ends.Add(WorldFromMetres(Blow.Aim, Level) + FVector(0.0f, 0.0f, BoardHeight + 20.0f));
		Seen.Add(IsSeen(User));
	}

	const FLinearColor Colour = Look.bMagic && Shot == EShot::Orb ? LookColour(Look) : ShotColour(*Blow.Ability, Shot);
	const float Speed = Shot == EShot::Arrow ? 2600.0f : (Shot == EShot::Orb ? 1500.0f : 1100.0f);
	for (int32 k = 0; k < Ends.Num(); ++k)
	{
		FTMShot& Flying = Blow.Shots.AddDefaulted_GetRef();
		Flying.From = Start;
		Flying.To = Ends[k];
		const float Distance = FVector::Dist(Start, Ends[k]);
		Flying.Flight = FMath::Clamp(Distance / Speed, 0.12f, 1.4f);
		Flying.Arc = Shot == EShot::Stone ? Distance * 0.3f : (Shot == EShot::Arrow ? Distance * 0.06f : 0.0f);
		if (!Seen[k])
		{
			continue;  // flies unseen: it still takes its time, so the blow lands when it would
		}
		UStaticMeshComponent* Body = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
		Body->SetStaticMesh(Mesh);
		Body->SetMobility(EComponentMobility::Movable);
		Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Body->SetCastShadow(false);
		Body->SetupAttachment(RootComponent);
		Body->RegisterComponent();
		// The basic shapes are a metre across: an arrow is a long thin rod.
		Body->SetRelativeScale3D(Shot == EShot::Arrow ? FVector(0.025f, 0.025f, 0.6f)
			: FVector(Shot == EShot::Orb ? 0.2f : 0.16f));
		Body->SetRelativeLocation(Start);
		if (ShotMaterial)
		{
			UMaterialInstanceDynamic* Paint = UMaterialInstanceDynamic::Create(ShotMaterial, Body);
			Paint->SetVectorParameterValue(TEXT("Color"), Colour);
			Body->SetMaterial(0, Paint);
		}
		if (Shot == EShot::Orb && Look.bMagic && !bStripped && !bCarrierOnly)
		{
			// What it is made of, flying: a fireball, a spark, a shard of ice.
			// The plain ball stays only when the look has nothing to fly as.
			Flying.Trail = LookShot(Look, Body);
			if (Flying.Trail.IsValid())
			{
				Body->SetVisibility(false);
			}
		}
		if (Shot == EShot::Orb && !bStripped && !bCarrierOnly)
		{
			// Magic gives off its own light, which is most of what makes it read.
			UPointLightComponent* Glow = NewObject<UPointLightComponent>(this, NAME_None, RF_Transient);
			Glow->SetupAttachment(Body);
			Glow->RegisterComponent();
			Glow->SetLightColor(Colour);
			Glow->SetIntensity(GlowBrightness * 2.0f);
			Glow->SetAttenuationRadius(260.0f);
			Glow->SetCastShadows(false);
			Flying.Glow = Glow;
		}
		Flying.Mesh = Body;
		Flying.CastKey = CastShot(Blow, Body);
		if (bCarrierOnly || (bStripped && Authored && Authored->HasProjectileEffect()))
		{
			Body->SetVisibility(false);
		}
	}
}

void ATMBattleDirector::LandBlow(FTMBlow& Blow)
{
	SoundBlowLands(Blow);
	if (CastStrip(Blow.Ability) != TMCast::EStrip::All)
	{
		LookLand(Blow);
	}
	CastLand(Blow);
	for (FTMShot& Shot : Blow.Shots)
	{
		CastEnd(Shot.CastKey);
		Shot.CastKey = 0;
		if (Shot.Trail.IsValid())
		{
			Shot.Trail->DestroyComponent();
		}
		if (Shot.Mesh.IsValid())
		{
			Shot.Mesh->DestroyComponent();
		}
		if (Shot.Glow.IsValid())
		{
			Shot.Glow->DestroyComponent();
		}
	}
	if (Blow.Ability && !Blow.Ability->VfxSystem.empty() && Blow.Ability->VfxAt == "point"
		&& CastStrip(Blow.Ability) == TMCast::EStrip::None)
	{
		const int Level = Battle.Map.NodeLevel(TMSim::FMap::NodeOf(Blow.Aim));
		PlayVfx(*Blow.Ability, WorldFromMetres(Blow.Aim, Level) + FVector(0.0f, 0.0f, BoardHeight));
	}
	TArray<int32> ShownOn;
	for (const TMSim::FEvent& Event : Blow.Events)
	{
		ShowOne(Event, Blow.Ability, Blow.Caster, &ShownOn);
	}
	for (int32 Index : Blow.Held)
	{
		if (Motions.IsValidIndex(Index))
		{
			Motions[Index].HeldBlows = FMath::Max(0, Motions[Index].HeldBlows - 1);
		}
	}
}

void ATMBattleDirector::ClearBlows()
{
	for (FTMBlow& Blow : Blows)
	{
		for (FTMShot& Shot : Blow.Shots)
		{
			CastEnd(Shot.CastKey);
			Shot.CastKey = 0;
			if (Shot.Trail.IsValid())
			{
				Shot.Trail->DestroyComponent();
			}
			if (Shot.Mesh.IsValid())
			{
				Shot.Mesh->DestroyComponent();
			}
			if (Shot.Glow.IsValid())
			{
				Shot.Glow->DestroyComponent();
			}
		}
	}
	Blows.Reset();
	for (FTMMotion& Motion : Motions)
	{
		Motion.HeldBlows = 0;
	}
	if (SlowUntil > 0.0 || HitStopUntil > 0.0 || AppliedDilation != 1.0f)
	{
		SlowUntil = 0.0;
		SlowFactor = 1.0f;
		HitStopUntil = 0.0;
		AppliedDilation = 1.0f;
		UGameplayStatics::SetGlobalTimeDilation(this, 1.0f);
	}
}

void ATMBattleDirector::ShowOne(const TMSim::FEvent& Event, const TMSim::FAbility* Ability, int32 CasterId, TArray<int32>* ShownOn)
{
	// Cast Studio's moments in what is shown: a cast beginning, a status given
	// or ticking or a reaction, a pet come or gone (TMBattleDirectorCast.cpp).
	CastShown(Event, Ability, CasterId);

	// The effect an ability names, once on each unit it touched.
	if (Ability && ShownOn && Ability->VfxAt == "targets" && !Ability->VfxSystem.empty()
		&& CastStrip(Ability) == TMCast::EStrip::None
		&& (Event.Kind == TMSim::EEventKind::Hit || Event.Kind == TMSim::EEventKind::Evaded
			|| Event.Kind == TMSim::EEventKind::Absorbed || Event.Kind == TMSim::EEventKind::Revived
			|| Event.Kind == TMSim::EEventKind::StatusApplied)
		&& !ShownOn->Contains(Event.Unit))
	{
		ShownOn->Add(Event.Unit);
		const TMSim::FUnit* Struck = Battle.FindUnit(Event.Unit);
		if (Struck && IsSeen(*Struck))
		{
			PlayVfx(*Ability, WorldFor(*Struck) + FVector(0.0f, 0.0f, VfxHeight));
		}
	}

	React(Event, CasterId);

	// Each kind its own look (2026-10-01, "Combat Text Mockups" A): red damage,
	// a bold bigger crit, a dark red burn, green healing, a status in its colour.
	FString What;
	FColor Tint = FColor::White;
	FTMFloatLook Look;
	bool bFlash = false;
	FLinearColor FlashColour(1.0f, 0.25f, 0.2f);
	switch (Event.Kind)
	{
	case TMSim::EEventKind::Hit:
	{
		// A Critical or a Grazed came just before this, for this unit.
		uint8 Mark = 0;
		BlowMarks.RemoveAndCopyValue(Event.Unit, Mark);
		const bool bCrit = (Mark & 1) != 0;
		const bool bGraze = (Mark & 2) != 0;
		// Down for damage and a burn, up for healing and regen.
		if (Harms(Event))
		{
			What = FString::Printf(TEXT("-%d%s"), Event.Amount, bCrit ? TEXT("!") : TEXT(""));
			Tint = FColor(255, 74, 61);
			Look.bBold = true;
			if (Event.Id == "burn")
			{
				Tint = FColor(163, 21, 15);
				Look.bEmber = true;
				Look.Scale = 0.85f;
			}
			else if (Event.Id == "bleed")
			{
				Tint = FColor(214, 51, 108);
				Look.Scale = 0.85f;
			}
			else if (Event.By < 0)
			{
				// The ground, decay: a tick, a size smaller.
				Look.Scale = 0.85f;
			}
			else if (bCrit)
			{
				Tint = FColor(255, 59, 46);
				Look.Scale = 1.45f;
				Look.bPop = true;
			}
			else if (bGraze)
			{
				Look.Scale = 0.85f;
				Look.bBold = false;
				Look.Tag = TEXT("graze");
				Look.TagTint = FColor(169, 200, 255);
			}
			bFlash = Event.By >= 0;
			// Worth a picture: a timed capture almost never lands on the second
			// and a half a number is up for, so the interesting frames were all
			// of eight people standing about.
			bWorthSeeing = bWorthSeeing || bFlash;
			PopHealth(Event.Unit, -Event.Amount);
			if (Event.By >= 0 && Event.By != Event.Unit)
			{
				PopHealth(Event.By, 0);
			}
		}
		else if (Event.Amount > 0)
		{
			What = FString::Printf(TEXT("+%d%s"), Event.Amount, bCrit ? TEXT("!") : TEXT(""));
			if (Event.By < 0)
			{
				// Regen, mending, healing ground: it adds up, it does not shout.
				Tint = FColor(155, 232, 168);
				Look.Scale = 0.8f;
			}
			else
			{
				Tint = bCrit ? FColor(47, 203, 92) : FColor(63, 212, 106);
				Look.bBold = true;
				Look.Scale = bCrit ? 1.4f : 1.0f;
				Look.bPop = bCrit;
			}
			bFlash = Event.By >= 0;
			FlashColour = FLinearColor(0.3f, 1.0f, 0.45f);
			PopHealth(Event.Unit, Event.Amount);
		}
		break;
	}
	case TMSim::EEventKind::Evaded:
		What = TEXT("dodge");
		Tint = FColor(215, 224, 255);
		break;
	case TMSim::EEventKind::Grazed:
		// Said on the halved hit that follows, as "-21 graze".
		BlowMarks.FindOrAdd(Event.Unit) |= 2;
		break;
	case TMSim::EEventKind::Critical:
		// The hit that follows is drawn big and bold, with a "!".
		BlowMarks.FindOrAdd(Event.Unit) |= 1;
		break;
	case TMSim::EEventKind::Absorbed:
		What = FString::Printf(TEXT("soaked %d"), Event.Amount);
		Tint = FColor(140, 200, 255);
		Look.Scale = 0.8f;
		bFlash = true;
		FlashColour = FLinearColor(0.45f, 0.7f, 1.0f);
		break;
	case TMSim::EEventKind::StatusApplied:
	{
		const TMSim::FStatusDef* Def = TMSim::FindStatus(Event.Id);
		What = (Def ? FString(UTF8_TO_TCHAR(Def->Name)) : FString(UTF8_TO_TCHAR(Event.Id.c_str()))).ToUpper();
		Tint = StatusWordTint(Event.Id);
		Look.Scale = 0.7f;
		Look.bBold = true;
		break;
	}
	case TMSim::EEventKind::Knocked:
		What = TEXT("DOWN");
		Tint = FColor(255, 90, 77);
		Look.Scale = 1.1f;
		Look.bBold = true;
		break;
	case TMSim::EEventKind::Revived:
		What = TEXT("UP AGAIN");
		Tint = FColor(255, 226, 122);
		Look.Scale = 0.85f;
		Look.bBold = true;
		bFlash = true;
		FlashColour = FLinearColor(1.0f, 0.85f, 0.4f);
		break;
	default:
		break;
	}
	const TMSim::FUnit* Unit = Battle.FindUnit(Event.Unit);
	// Nothing rises off a unit this side cannot see (battle.gd:450-452).
	if (What.IsEmpty() || !Unit || !IsSeen(*Unit))
	{
		return;
	}
	AddFloater(Event.Unit, What, Tint, true, &Look);
	if (bFlash)
	{
		FFlash& Flash = Flashes.AddDefaulted_GetRef();
		Flash.UnitId = Event.Unit;
		Flash.Colour = FlashColour;
		// A wound flares by how much it took (2026-10-03): a scratch as before, a big blow twice as bright.
		if (Event.Kind == TMSim::EEventKind::Hit && Harms(Event))
		{
			Flash.Strength = 1.0f + FMath::Min(1.5f, 4.0f * Event.Amount / static_cast<float>(FMath::Max(1, Unit->MaxHp())));
		}
	}
}

void ATMBattleDirector::AddFloater(int32 UnitId, const FString& What, const FColor& Tint, bool bCount, const FTMFloatLook* Look)
{
	const TMSim::FUnit* Unit = Battle.FindUnit(UnitId);
	if (!Unit)
	{
		return;
	}
	// Above the head, and nudged along by however many are already in flight
	// for this unit, so two numbers in the same instant do not sit on top of
	// one another.
	int32 Stacked = 0;
	for (const FTMFloater& Other : Floaters)
	{
		if (Other.UnitId == UnitId)
		{
			++Stacked;
		}
	}
	// As big as the player set it in Options, and stacked that far apart.
	const float Size = FloaterSize * FTMSettings::Get().DamageTextScale;
	const FVector Where = ShownAt(*Unit) + FVector(0.0f, 0.0f, 190.0f + Stacked * Size);
	const float Scale = Look ? Look->Scale : 1.0f;

	UTextRenderComponent* Text = NewObject<UTextRenderComponent>(this, NAME_None, RF_Transient);
	Text->SetMobility(EComponentMobility::Movable);
	Text->SetupAttachment(RootComponent);
	Text->RegisterComponent();
	Text->SetText(FText::FromString(What));
	Text->SetTextRenderColor(Tint);
	Text->SetWorldSize(Size * Scale);
	Text->SetHorizontalAlignment(EHTA_Center);
	Text->SetRelativeLocation(Where);
	// In a game the HUD draws it, outlined (ATMBattleHud::DrawWorldWords); this says where and what.
	Text->SetHiddenInGame(GetWorld() && GetWorld()->IsGameWorld());
	// Facing the camera is a per-frame job; billboarded in AdvanceFloaters.
	FTMFloater& Floater = Floaters.AddDefaulted_GetRef();
	Floater.Text = Text;
	Floater.UnitId = UnitId;
	if (Look)
	{
		Floater.Scale = Look->Scale;
		Floater.bBold = Look->bBold;
		Floater.bPop = Look->bPop;
		Floater.bEmber = Look->bEmber;
		Floater.bExact = true;
		Floater.Tag = Look->Tag;
		Floater.TagTint = Look->TagTint;
	}
	if (bCount)
	{
		++NumbersShown;
	}
}

double ATMBattleDirector::PopClock() const
{
	return FPlatformTime::Seconds();
}

float ATMBattleDirector::PopTrail(const FTMHpPop& Pop, double Now) const
{
	if (Pop.TrailAt < 0.0)
	{
		return static_cast<float>(Pop.Hp);
	}
	// Eased, so it slides off rather than ticking down.
	const float P = FMath::Clamp(static_cast<float>((Now - Pop.TrailAt) / HpDrainSeconds), 0.0f, 1.0f);
	const float Eased = P * P * (3.0f - 2.0f * P);
	return FMath::Max(static_cast<float>(Pop.Hp), Pop.TrailFrom + (Pop.Hp - Pop.TrailFrom) * Eased);
}

void ATMBattleDirector::PopHealth(int32 UnitId, int32 Change)
{
	// Only this side's own units: an enemy keeps its ring, and the full read on pointing.
	const TMSim::FUnit* Unit = Battle.FindUnit(UnitId);
	if (!Unit || !IsFriend(*Unit) || !IsSeen(*Unit))
	{
		return;
	}
	const double Now = PopClock();
	const int32 MaxHp = FMath::Max(1, Unit->MaxHp());
	// The bar ends where the rules are, so it is never wrong for long; a blow
	// still in flight may already be in that number.
	const int32 Hp = FMath::Clamp(Unit->Hp, 0, MaxHp);
	FTMHpPop* Pop = HpPops.Find(UnitId);
	const bool bLive = Pop && Now - Pop->ShownAt < HpPopSeconds;
	if (!bLive)
	{
		Pop = &HpPops.Add(UnitId);
		*Pop = FTMHpPop();
		Pop->Hp = FMath::Clamp(Hp - Change, 0, MaxHp);
	}
	const int32 Before = Pop->Hp;
	const float Trail = PopTrail(*Pop, Now);
	Pop->ShownAt = Now;
	Pop->Hp = Hp;
	if (Change < 0)
	{
		// The lost part stays, from wherever an earlier drain had got to.
		Pop->TrailFrom = FMath::Max(FMath::RoundToInt(Trail), Before);
		Pop->TrailAt = Now;
	}
	else if (Change > 0)
	{
		Pop->HealFrom = FMath::Min(Before, Hp);
		Pop->HealAt = Now;
	}
}

FString ATMBattleDirector::SideOf(int32 Index, const FVector& From) const
{
	const FVector To = From - Motions[Index].Shown;
	if (FVector2D(To.X, To.Y).IsNearlyZero())
	{
		return TEXT("Front");
	}
	const float Toward = FMath::RadiansToDegrees(FMath::Atan2(To.Y, To.X));
	const float Relative = FRotator::NormalizeAxis(Toward - Motions[Index].Yaw);
	if (FMath::Abs(Relative) <= 45.0f)
	{
		return TEXT("Front");
	}
	if (FMath::Abs(Relative) >= 135.0f)
	{
		return TEXT("Back");
	}
	// Unreal's yaw turns from +X towards +Y, which is a unit's right.
	return Relative > 0.0f ? TEXT("Right") : TEXT("Left");
}

void ATMBattleDirector::Jolt(int32 Index, const FVector& Dir, float Size)
{
	FTMMotion& Motion = Motions[Index];
	Motion.JoltDir = FVector(Dir.X, Dir.Y, 0.0f).GetSafeNormal();
	Motion.JoltSize = Size;
	Motion.JoltAge = 0.0f;
}

void ATMBattleDirector::React(const TMSim::FEvent& Event, int32 CasterId)
{
	const int32 i = IndexOfUnit(Battle, Event.Unit);
	if (!Motions.IsValidIndex(i))
	{
		return;
	}
	FTMMotion& Motion = Motions[i];
	const TMSim::FUnit& Unit = Battle.Units[i];
	const FTMAnimSet* Set = Motion.Body ? Motion.Body->Animations : nullptr;

	// Where the blow came from: the one who struck it, if anyone did.
	const int32 FromId = CasterId >= 0 ? CasterId : Event.By;
	const int32 From = FromId >= 0 ? IndexOfUnit(Battle, FromId) : INDEX_NONE;
	const bool bFromSomeone = Motions.IsValidIndex(From) && From != i;
	const FVector Away = bFromSomeone ? (Motion.Shown - Motions[From].Shown) : -FRotator(0.0f, Motion.Yaw, 0.0f).Vector();
	const FString Side = bFromSomeone ? SideOf(i, Motions[From].Shown) : FString(TEXT("Front"));
	// Reacting with a clip only when standing about: not mid-walk, not
	// mid-swing, not lying down.
	const bool bFree = Set && Unit.IsAlive() && Motion.Path.Num() == 0 && Motion.OneShotLeft <= 0.0f && !Motion.bDown;

	switch (Event.Kind)
	{
	case TMSim::EEventKind::Critical:
		Motion.bCritPending = true;
		break;
	case TMSim::EEventKind::Hit:
		if (Harms(Event) && Event.Amount > 0)
		{
			const bool bHeavy = Motion.bCritPending;
			Motion.bCritPending = false;
			// How much of it this took: knocked back by that, and the blow felt by
			// that (2026-10-03, TMBattleDirectorFeel.cpp) -- an instant's stillness as
			// a big one lands, a slowed beat for a critical, the camera kicked.
			const float Share = FMath::Clamp(Event.Amount / static_cast<float>(FMath::Max(1, Unit.MaxHp())), 0.0f, 1.0f);
			Jolt(i, Away, (bHeavy ? 30.0f : 12.0f) + 40.0f * FMath::Min(Share, 0.5f));
			if (Event.By >= 0 && IsSeen(Unit))
			{
				if (bHeavy || Share >= 0.15f)
				{
					HitStop(bHeavy ? 0.09f : 0.05f);
				}
				if (bHeavy)
				{
					SlowWorld(0.4f, 0.3f);
				}
				if (bHeavy || Share >= 0.06f)
				{
					Jolt(FMath::Clamp(Share * 3.0f, 0.15f, 1.0f) * (bHeavy ? 1.4f : 1.0f));
				}
			}
			// A grunt, not every time: a critical nearly always, a scratch now and then.
			if (Unit.IsAlive())
			{
				PlayVoice(i, bHeavy ? TEXT("painHeavy") : TEXT("pain"), bHeavy ? 0.9f : 0.3f);
			}
			if (bFree || (bHeavy && Set && Unit.IsAlive() && Motion.Path.Num() == 0 && !Motion.bDown))
			{
				UAnimSequence* Clip = bHeavy ? Set->Extra(TEXT("hitHeavy"), Event.Amount) : nullptr;
				if (!Clip)
				{
					Clip = Set->Extra(*(TEXT("hit") + Side), Event.Amount);
				}
				if (!Clip && Set->Hit.Num() > 0)
				{
					Clip = Set->Hit[Event.Amount % Set->Hit.Num()];
				}
				if (Clip)
				{
					Animate(i, Clip, false);
				}
			}
		}
		break;
	case TMSim::EEventKind::Evaded:
	{
		// Out of the way: a step to the side, whichever side it is, with the
		// hero's own dodge to that side -- its evade or dive, or the push-off of
		// a sideways jog, cut before it settles into running.
		const FVector Across = FVector(-Away.Y, Away.X, 0.0f) * ((Unit.Id % 2) ? 1.0f : -1.0f);
		Jolt(i, Across, 45.0f);
		if (bFree && Set)
		{
			const float Yaw = FMath::DegreesToRadians(Motion.Yaw);
			const FVector Right(-FMath::Sin(Yaw), FMath::Cos(Yaw), 0.0f);
			UAnimSequence* Dodge = Set->Extra(FVector::DotProduct(Across, Right) > 0.0f ? TEXT("evadeRight") : TEXT("evadeLeft"), Unit.Id);
			if (!Dodge)
			{
				Dodge = Set->Extra(TEXT("evade"), Unit.Id);
			}
			Animate(i, Dodge, false, 0.6f);
		}
		break;
	}
	case TMSim::EEventKind::Absorbed:
		Jolt(i, Away, 6.0f);
		if (bFree)
		{
			if (UAnimSequence* Guard = Set->Extra(TEXT("block"), Unit.Id))
			{
				Animate(i, Guard, false);
			}
		}
		break;
	case TMSim::EEventKind::Knocked:
		// Falls away from the blow: struck from the front, it goes over backwards.
		Motion.DeathClip = Set ? Set->Extra(*(TEXT("death") + Side), Unit.Id) : nullptr;
		break;
	default:
		break;
	}
}

void ATMBattleDirector::ShowStatuses(int32 Index, float DeltaSeconds)
{
	FTMMotion& Motion = Motions[Index];
	const TMSim::FUnit& Unit = Battle.Units[Index];
	Motion.Clock += DeltaSeconds;

	const FStatusLook* Look = nullptr;
	if (Unit.IsAlive())
	{
		for (const FStatusLook& Each : GLooks)
		{
			if (Unit.HasStatus(Each.Id))
			{
				Look = &Each;
				break;
			}
		}
	}

	// How fast its clips play. Several slowing things take the slowest.
	float Rate = 1.0f;
	if (Unit.IsAlive())
	{
		for (const FStatusLook& Each : GLooks)
		{
			if (Each.Rate != 1.0f && Unit.HasStatus(Each.Id))
			{
				Rate = Each.Rate > 1.0f ? FMath::Max(Rate, Each.Rate) : FMath::Min(Rate, Each.Rate);
			}
		}
	}
	// Asleep or dazed with a clip of its own to show it, that clip plays at its own pace.
	const FTMAnimSet* Set = Motion.Body ? Motion.Body->Animations : nullptr;
	if (Set && ((Unit.HasStatus("stun") && Set->Extra(TEXT("stunned"))) || (Unit.HasStatus("sleep") && Set->Extra(TEXT("sleep")))))
	{
		Rate = Unit.HasStatus("freeze") ? 0.0f : 1.0f;
	}
	if (Motion.bDown)
	{
		Rate = 1.0f;
	}
	// A long walk's quicker pace (AdvanceMotion), in its steps too.
	if (Motion.Path.Num() > 0 && !Motion.bDown && Motion.OneShotLeft <= 0.0f)
	{
		Rate *= Motion.Pace;
	}
	if (Rate != Motion.PlayRate && UnitVisuals.IsValidIndex(Index) && UnitVisuals[Index])
	{
		UnitVisuals[Index]->SetPlayRate(Rate);
	}
	Motion.PlayRate = Rate;

	// A burn stings now and then.
	if (Unit.IsAlive() && (Unit.HasStatus("burn") || Unit.HasStatus("bleed")) && Motion.Path.Num() == 0)
	{
		Motion.NextWince -= DeltaSeconds;
		if (Motion.NextWince <= 0.0f)
		{
			Motion.NextWince = 1.6f + 0.4f * (Unit.Id % 3);
			Jolt(Index, FVector(FMath::Cos(Motion.Clock * 7.0f), FMath::Sin(Motion.Clock * 7.0f), 0.0f), 6.0f);
		}
	}
	// And sleep shows its Zs.
	if (Unit.IsAlive() && Unit.HasStatus("sleep") && IsSeen(Unit))
	{
		Motion.NextWince -= DeltaSeconds;
		if (Motion.NextWince <= 0.0f)
		{
			Motion.NextWince = 2.5f;
			AddFloater(Unit.Id, TEXT("z z"), FColor(170, 170, 255), false);
		}
	}

	if (!StatusLights.IsValidIndex(Index) || !StatusLights[Index])
	{
		return;
	}
	UPointLightComponent* Glow = StatusLights[Index];
	if (!Look || !IsSeen(Unit))
	{
		Glow->SetVisibility(false);
		return;
	}
	float Strength = 1.0f;
	if (Look->bFlicker)
	{
		Strength = 0.7f + 0.3f * FMath::Sin(Motion.Clock * 17.0f) * FMath::Sin(Motion.Clock * 5.3f);
	}
	else if (Look->Pulse > 0.0f)
	{
		Strength = 0.55f + 0.45f * FMath::Sin(Motion.Clock * Look->Pulse * UE_TWO_PI);
	}
	Glow->SetVisibility(true);
	Glow->SetLightColor(Look->Colour);
	Glow->SetIntensity(GlowBrightness * Strength);
}

void ATMBattleDirector::UltimateBeat(const TMSim::FEvent& Event)
{
	const TMSim::FUnit* User = Battle.FindUnit(Event.Unit);
	const TMSim::FAbility* Ability = TMSim::FindAbility(Event.Id);
	if (!User || !Ability || !IsSeen(*User))
	{
		return;
	}
	// Its name in gold over the caster, and the world slowed for a moment
	// around it -- only the look of the world: the clock keeps real time (Tick).
	AddFloater(User->Id, FString(UTF8_TO_TCHAR(Ability->Name.c_str())).ToUpper(), FColor(255, 200, 60), false);
	FFlash& Flash = Flashes.AddDefaulted_GetRef();
	Flash.UnitId = User->Id;
	Flash.Colour = FLinearColor(1.0f, 0.8f, 0.3f);
	SlowWorld(0.35f, 0.9f);
	// And the camera closes in on it and what it is aimed at, for a moment
	// (Options: close-ups; any key or click skips it).
	const FTransform& Board = GetActorTransform();
	const FVector From = Board.TransformPosition(WorldFor(*User));
	const FVector To = Board.TransformPosition(WorldFromMetres(Event.Where, Battle.Map.NodeLevel(TMSim::FMap::NodeOf(Event.Where))));
	CloseUp(FVector((From.X + To.X) * 0.5, (From.Y + To.Y) * 0.5, From.Z), User->Id);
}

void ATMBattleDirector::Celebrate()
{
	bCelebrated = true;
	// Won or lost, from this side's seat (hud.gd's jingles); two people at one
	// screen, or nobody playing, hear the win.
	const int32 Mine = ViewerTeam();
	PlayEventSound(Mine >= 0 && Battle.Winner != Mine ? TEXT("defeat") : TEXT("victory"), nullptr, 0.8f);
	for (int32 i = 0; i < Motions.Num() && i < static_cast<int32>(Battle.Units.size()); ++i)
	{
		if (Battle.Units[i].IsAlive() && Battle.Units[i].Team == Battle.Winner)
		{
			PlayVoice(i, TEXT("cheer"), 1.0f);
			break;
		}
	}
	for (int32 i = 0; i < Motions.Num() && i < static_cast<int32>(Battle.Units.size()); ++i)
	{
		const TMSim::FUnit& Unit = Battle.Units[i];
		const FTMAnimSet* Set = Motions[i].Body ? Motions[i].Body->Animations : nullptr;
		if (!Set || !Unit.IsAlive() || Motions[i].Path.Num() > 0)
		{
			continue;
		}
		// The winners cheer; whoever is left standing on the other side hangs their head.
		if (UAnimSequence* Clip = Set->Extra(Unit.Team == Battle.Winner ? TEXT("victory") : TEXT("defeat"), Unit.Id))
		{
			Animate(i, Clip, false);
		}
	}
}
