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

	enum class EShot : uint8 { None, Arrow, Orb, Stone };

	/** What an ability throws, if anything. Anything cast wide simply lands. */
	EShot ShotOf(const TMSim::FAbility& Ability, const FString& Motion)
	{
		if (Ability.Fx == "throw_stone")
		{
			return EShot::Stone;
		}
		if (Motion == TEXT("shoot"))
		{
			return EShot::Arrow;
		}
		if (Motion == TEXT("bolt") && Ability.Effect == TMSim::EEffect::Damage)
		{
			return EShot::Orb;
		}
		return EShot::None;
	}

	/**
	 * A colour for what it is made of, read from the built-in effect it
	 * borrows (its "fx"): fire burns orange, ice is pale blue, holy light gold.
	 */
	FLinearColor ShotColour(const TMSim::FAbility& Ability, EShot Shot)
	{
		const std::string& Fx = Ability.Fx;
		if (Fx == "fire" || Fx == "meteor") { return FLinearColor(1.0f, 0.42f, 0.08f); }
		if (Fx == "blizzard") { return FLinearColor(0.55f, 0.85f, 1.0f); }
		if (Fx == "holy_blade" || Fx == "sanctuary" || Fx == "cure" || Fx == "raise") { return FLinearColor(1.0f, 0.88f, 0.45f); }
		if (Fx == "haste" || Fx == "focus" || Fx == "chakra") { return FLinearColor(0.45f, 1.0f, 0.55f); }
		if (Shot == EShot::Arrow) { return FLinearColor(0.42f, 0.28f, 0.14f); }
		if (Shot == EShot::Stone) { return FLinearColor(0.35f, 0.33f, 0.3f); }
		return FLinearColor(0.7f, 0.45f, 1.0f);  // plain magic
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
		{ "stun",      FLinearColor(1.0f, 0.95f, 0.4f),  2.5f, false, 0.6f },
		{ "sleep",     FLinearColor(0.5f, 0.5f, 1.0f),   0.4f, false, 0.3f },
		{ "doom",      FLinearColor(0.6f, 0.1f, 0.8f),   1.2f, false, 1.0f },
		{ "burn",      FLinearColor(1.0f, 0.45f, 0.1f),  0.0f, true,  1.0f },
		{ "bleed",     FLinearColor(0.8f, 0.05f, 0.05f), 1.0f, false, 1.0f },
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
	};

	/** The brightness of a status glow, well under the ready light's. */
	constexpr float GlowBrightness = 4000.0f;
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
			if (Blow.Ability && !Blow.Ability->VfxSystem.empty() && Blow.Ability->VfxAt == "user" && User && IsSeen(*User))
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
		}
		else
		{
			Blow.bStarted = true;
		}
		Blow.ImpactAt = Blow.Release && Blow.Motion != TEXT("none") ? Share * Blow.Release->GetPlayLength() : 0.0f;

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
		}
		if (Blow.bStarted && !bLanded)
		{
			Blow.Since += DeltaSeconds;
			if (!Blow.bLaunched && Blow.Since >= Blow.ImpactAt)
			{
				Blow.bLaunched = true;
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
	const EShot Shot = ShotOf(*Blow.Ability, Blow.Motion);
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

	const FLinearColor Colour = ShotColour(*Blow.Ability, Shot);
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
		if (Shot == EShot::Orb)
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
	}
}

void ATMBattleDirector::LandBlow(FTMBlow& Blow)
{
	SoundBlowLands(Blow);
	for (FTMShot& Shot : Blow.Shots)
	{
		if (Shot.Mesh.IsValid())
		{
			Shot.Mesh->DestroyComponent();
		}
		if (Shot.Glow.IsValid())
		{
			Shot.Glow->DestroyComponent();
		}
	}
	if (Blow.Ability && !Blow.Ability->VfxSystem.empty() && Blow.Ability->VfxAt == "point")
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
	if (SlowUntil > 0.0)
	{
		SlowUntil = 0.0;
		UGameplayStatics::SetGlobalTimeDilation(this, 1.0f);
	}
}

void ATMBattleDirector::ShowOne(const TMSim::FEvent& Event, const TMSim::FAbility* Ability, int32 CasterId, TArray<int32>* ShownOn)
{
	// The effect an ability names, once on each unit it touched.
	if (Ability && ShownOn && Ability->VfxAt == "targets" && !Ability->VfxSystem.empty()
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

	FString What;
	FColor Tint = FColor::White;
	bool bFlash = false;
	FLinearColor FlashColour(1.0f, 0.25f, 0.2f);
	switch (Event.Kind)
	{
	case TMSim::EEventKind::Hit:
		// Down for damage and a burn, up for healing and regen.
		if (Harms(Event))
		{
			What = FString::Printf(TEXT("-%d"), Event.Amount);
			Tint = FColor(255, 115, 90);
			bFlash = Event.By >= 0;
			// Worth a picture: a timed capture almost never lands on the second
			// and a half a number is up for, so the interesting frames were all
			// of eight people standing about.
			bWorthSeeing = bWorthSeeing || bFlash;
		}
		else if (Event.Amount > 0)
		{
			What = FString::Printf(TEXT("+%d"), Event.Amount);
			Tint = FColor(115, 255, 128);
			bFlash = Event.By >= 0;
			FlashColour = FLinearColor(0.3f, 1.0f, 0.45f);
		}
		break;
	case TMSim::EEventKind::Evaded:
		What = TEXT("miss");
		Tint = FColor(215, 224, 255);
		break;
	case TMSim::EEventKind::Critical:
		What = TEXT("critical!");
		Tint = FColor(255, 217, 77);
		break;
	case TMSim::EEventKind::Absorbed:
		What = FString::Printf(TEXT("soaked %d"), Event.Amount);
		Tint = FColor(153, 217, 255);
		bFlash = true;
		FlashColour = FLinearColor(0.45f, 0.7f, 1.0f);
		break;
	case TMSim::EEventKind::StatusApplied:
		What = UTF8_TO_TCHAR(Event.Id.c_str());
		Tint = FColor(224, 153, 255);
		break;
	case TMSim::EEventKind::Knocked:
		What = TEXT("down");
		Tint = FColor(255, 77, 77);
		break;
	case TMSim::EEventKind::Revived:
		What = TEXT("up again");
		Tint = FColor(255, 242, 153);
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
	AddFloater(Event.Unit, What, Tint);
	if (bFlash)
	{
		FFlash& Flash = Flashes.AddDefaulted_GetRef();
		Flash.UnitId = Event.Unit;
		Flash.Colour = FlashColour;
	}
}

void ATMBattleDirector::AddFloater(int32 UnitId, const FString& What, const FColor& Tint, bool bCount)
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

	UTextRenderComponent* Text = NewObject<UTextRenderComponent>(this, NAME_None, RF_Transient);
	Text->SetMobility(EComponentMobility::Movable);
	Text->SetupAttachment(RootComponent);
	Text->RegisterComponent();
	Text->SetText(FText::FromString(What));
	Text->SetTextRenderColor(Tint);
	Text->SetWorldSize(Size);
	Text->SetHorizontalAlignment(EHTA_Center);
	Text->SetRelativeLocation(Where);
	// Facing the camera is a per-frame job; billboarded in AdvanceFloaters.
	Floaters.Add({ Text, UnitId, 0.0f });
	if (bCount)
	{
		++NumbersShown;
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
			Jolt(i, Away, bHeavy ? 32.0f : 14.0f);
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
				Rate = FCStringAnsi::Strcmp(Each.Id, "stride") == 0 ? FMath::Max(Rate, Each.Rate) : FMath::Min(Rate, Each.Rate);
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
	if (!FApp::IsUnattended())
	{
		UGameplayStatics::SetGlobalTimeDilation(this, 0.35f);
		SlowUntil = FPlatformTime::Seconds() + 0.9;
	}
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
