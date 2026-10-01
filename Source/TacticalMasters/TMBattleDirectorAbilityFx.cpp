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

#include "TMBattleDirector.h"

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
	/** An effect, and how wide it was filmed (centimetres of what shows). */
	struct FLookFx
	{
		const TCHAR* Path;
		float SizeCm;
	};

	struct FFlavourLook
	{
		const TCHAR* Name;
		FLinearColor Colour;
		/** In the user's hands as it is cast. */
		FLookFx Cast;
		/** What flies to the target, for a spell thrown from afar. */
		FLookFx Shot;
		/** On each unit it strikes, and a second layer under it. */
		FLookFx Hit;
		FLookFx Hit2;
		/** On the ground an area covers, and a second layer. */
		FLookFx Burst;
		FLookFx Burst2;
		/** On an ally it strengthens. */
		FLookFx Aura;
	};

	const FFlavourLook GAbilityLooks[] =
	{
		{ TEXT("fire"), FLinearColor(1.0f, 0.45f, 0.1f),
			{ TEXT("/Game/ParagonGRIMexe/FX/Particles/Abilities/BFG/FX/P_GRIM_BFG_MuzzleFlash.P_GRIM_BFG_MuzzleFlash"), 81 },
			{ TEXT("/Game/ParagonGRIMexe/FX/Particles/Abilities/Ultimate/FX/P_GRIM_Ultimate_Projectile_New.P_GRIM_Ultimate_Projectile_New"), 283 },
			{ TEXT("/Game/ParagonGRIMexe/FX/Particles/Abilities/Ultimate/FX/P_GRIM_Ultimate_HitCharacter.P_GRIM_Ultimate_HitCharacter"), 357 },
			{ nullptr, 0 },
			{ TEXT("/Game/ParagonHowitzer/FX/Particles/Abilities/Ultimate/FX/P_HB_Ult_Explo_High.P_HB_Ult_Explo_High"), 384 },
			{ TEXT("/Game/ParagonIggyScorch/FX/Particles/IggyScorch/Abilities/Primary/FX/P_IggyScorch_Molotov_HitPlayer.P_IggyScorch_Molotov_HitPlayer"), 259 },
			{ TEXT("/Game/ParagonMinions/FX/Particles/Buffs/Buff_Red/FX/P_Buff_Red_SpawnFX.P_Buff_Red_SpawnFX"), 510 } },
		{ TEXT("ice"), FLinearColor(0.55f, 0.85f, 1.0f),
			{ TEXT("/Game/ParagonRevenant/FX/Particles/Revenant/Skins/FrostKing/Abilities/Primary/FX/P_Revenant_FrostKing_LastShot_MuzzleFlash.P_Revenant_FrostKing_LastShot_MuzzleFlash"), 212 },
			{ TEXT("/Game/ParagonMorigesh/FX/Particles/Morigesh/Skins/NorthernMystic/P_Morigesh_NorthernMystic_SkillshotAOE_Projectile.P_Morigesh_NorthernMystic_SkillshotAOE_Projectile"), 18 },
			{ TEXT("/Game/ParagonRampage/FX/Particles/Rampage_v001_IceBlue/FX/P_Rampage_Ice_Melee_Impact.P_Rampage_Ice_Melee_Impact"), 199 },
			{ nullptr, 0 },
			{ TEXT("/Game/ParagonRevenant/FX/Particles/Revenant/Skins/FrostKing/Abilities/Primary/FX/P_Revenant_FrostKing_Primary_HitWorld.P_Revenant_FrostKing_Primary_HitWorld"), 195 },
			{ TEXT("/Game/ParagonRampage/FX/Particles/Rampage_v001_IceBlue/FX/P_Rampage_Ice_Lunge_Impact.P_Rampage_Ice_Lunge_Impact"), 1273 },
			{ TEXT("/Game/ParagonLtBelica/FX/Particles/Belica/Abilities/TeslaConduit/FX/P_Wing_Burst.P_Wing_Burst"), 146 } },
		{ TEXT("lightning"), FLinearColor(0.55f, 0.7f, 1.0f),
			{ TEXT("/Game/ParagonGadget/FX/Abilities/Primary/FX/P_PrimaryZap_Muzzle.P_PrimaryZap_Muzzle"), 75 },
			{ TEXT("/Game/ParagonGadget/FX/Abilities/Primary/FX/P_PrimaryZap_Projectile_Trail.P_PrimaryZap_Projectile_Trail"), 365 },
			{ TEXT("/Game/ParagonGadget/FX/Abilities/Primary/FX/P_PrimaryZap_Projectile_Explode_Player.P_PrimaryZap_Projectile_Explode_Player"), 128 },
			{ nullptr, 0 },
			{ TEXT("/Game/ParagonKwang/FX/Particles/Abilities/LightStrike/FX/P_Kwang_LightStrike_Burst.P_Kwang_LightStrike_Burst"), 1351 },
			{ TEXT("/Game/ParagonDekker/FX/Particles/Abilities/SlowBomb/FX/P_Dekker_SlowBomb_Explosion.P_Dekker_SlowBomb_Explosion"), 833 },
			{ TEXT("/Game/ParagonKwang/FX/Particles/Abilities/LightStrike/FX/P_KwangBuff.P_KwangBuff"), 56 } },
		{ TEXT("water"), FLinearColor(0.25f, 0.75f, 1.0f),
			{ TEXT("/Game/ParagonGideon/FX/Particles/Gideon/Skins/Undertow/P_Meteor_Cast_Undertow.P_Meteor_Cast_Undertow"), 14 },
			{ TEXT("/Game/ParagonZinx/FX/Particles/Zinx/Abilities/StunShot/FX/P_Zinx_StunShot_Projectile.P_Zinx_StunShot_Projectile"), 93 },
			{ TEXT("/Game/ParagonTwinblast/FX/Particles/SummerTime/FX/P_TwinBlast_VortexGrenade_Explode_Summer.P_TwinBlast_VortexGrenade_Explode_Summer"), 765 },
			{ nullptr, 0 },
			{ TEXT("/Game/ParagonTwinblast/FX/Particles/SummerTime/FX/P_TwinBlast_VortexGrenade_Explode_Summer.P_TwinBlast_VortexGrenade_Explode_Summer"), 765 },
			{ TEXT("/Game/ParagonGideon/FX/Particles/Gideon/Skins/Undertow/P_Ult_Cast_Undertow.P_Ult_Cast_Undertow"), 2148 },
			{ TEXT("/Game/ParagonMinions/FX/Particles/Minions/Prime_Helix/Abilities/SpecialAttack2/FX/P_PH_Bubble.P_PH_Bubble"), 659 } },
		{ TEXT("wind"), FLinearColor(0.7f, 1.0f, 0.9f),
			{ TEXT("/Game/PotaVFX_Smoke/VFX/System/SmokeBurst/NS_MagicSmokeBurstAir.NS_MagicSmokeBurstAir"), 279 },
			{ TEXT("/Game/ParagonWraith/FX/Particles/Abilities/ScopedShot/FX/P_Wraith_Sniper_Projectile.P_Wraith_Sniper_Projectile"), 81 },
			{ TEXT("/Game/ParagonKhaimera/FX/ParticleSystems/Abilities/Leap/FX/P_Khaimera_Leap_AOE_Burst_Air.P_Khaimera_Leap_AOE_Burst_Air"), 662 },
			{ nullptr, 0 },
			{ TEXT("/Game/ParagonSerath/FX/Particles/Abilities/Ascend/FX/P_FallenAngel_Ascend_HitSmoke.P_FallenAngel_Ascend_HitSmoke"), 418 },
			{ TEXT("/Game/ParagonKhaimera/FX/ParticleSystems/Abilities/Leap/FX/P_Khaimera_Leap_AOE_Burst_Air.P_Khaimera_Leap_AOE_Burst_Air"), 662 },
			{ TEXT("/Game/PotaVFX_Smoke/VFX/System/SmokeBurst/NS_MagicSmokeBurstAir.NS_MagicSmokeBurstAir"), 279 } },
		{ TEXT("earth"), FLinearColor(0.85f, 0.65f, 0.4f),
			{ TEXT("/Game/ParagonGrux/FX/Particles/Abilities/Stampede/FX/P_Stampede_Cast.P_Stampede_Cast"), 602 },
			{ nullptr, 0 },
			{ TEXT("/Game/ParagonRampage/FX/Particles/Abilities/RipNToss/FX/P_RipNToss_HandImpact.P_RipNToss_HandImpact"), 177 },
			{ TEXT("/Game/ParagonGreystone/FX/Particles/Greystone/Abilities/ClearAPath/FX/P_Greystone_ClearAPath_Impacts.P_Greystone_ClearAPath_Impacts"), 390 },
			{ TEXT("/Game/ParagonMinions/FX/Particles/Minions/Prime_Helix/Abilities/SpecialAttack3/FX/P_Prime_Ground_Box.P_Prime_Ground_Box"), 283 },
			{ TEXT("/Game/ParagonDrongo/FX/Particles/Abilities/Ultimate/FX/P_Drongo_Ultimate_Explosion.P_Drongo_Ultimate_Explosion"), 1343 },
			{ TEXT("/Game/ParagonSteel/FX/Particles/Steel/Abilities/ShieldBlock/FX/P_Steel_Shieldblock.P_Steel_Shieldblock"), 143 } },
		{ TEXT("nature"), FLinearColor(0.45f, 1.0f, 0.3f),
			{ TEXT("/Game/ParagonMinions/FX/Particles/Buffs/Buff_Green/Abilities/Spawn/FX/P_Buff_Green_SpawnFX.P_Buff_Green_SpawnFX"), 327 },
			{ TEXT("/Game/ParagonDrongo/FX/Particles/Abilities/Shards/FX/P_Drongo_Shards_Projectile_Bullet.P_Drongo_Shards_Projectile_Bullet"), 195 },
			{ TEXT("/Game/ParagonSevarog/FX/Particles/Abilities/Primary/FX/P_Sevarog_Melee_SucessfulImpact.P_Sevarog_Melee_SucessfulImpact"), 179 },
			{ TEXT("/Game/ParagonMorigesh/FX/Particles/Morigesh/Abilities/SkillshotAOE/FX/P_Morigesh_SkillshotAOE_Explosion.P_Morigesh_SkillshotAOE_Explosion"), 814 },
			{ TEXT("/Game/ParagonFey/FX/Particles/Fey/Abilities/Ultimate/FX/P_Ultimate_Root_Spikes.P_Ultimate_Root_Spikes"), 305 },
			{ TEXT("/Game/ParagonSevarog/FX/Particles/Abilities/SoulSiphon/FX/P_SiphonCasting.P_SiphonCasting"), 60 },
			{ TEXT("/Game/ParagonMinions/FX/Particles/Buffs/Buff_Green/Abilities/Spawn/FX/P_Buff_Green_SpawnFX.P_Buff_Green_SpawnFX"), 327 } },
		{ TEXT("holy"), FLinearColor(1.0f, 0.85f, 0.45f),
			{ TEXT("/Game/ParagonDekker/FX/Particles/Abilities/SlowField/FX/P_Dekker_SlowField_HitWorld.P_Dekker_SlowField_HitWorld"), 176 },
			{ TEXT("/Game/ParagonMuriel/FX/Particles/Abilities/Primary/FX/P_Muriel_Primary_Projectile.P_Muriel_Primary_Projectile"), 44 },
			{ TEXT("/Game/ParagonFey/FX/Particles/Fey/Abilities/Growth/FX/P_Growth_HitWorld.P_Growth_HitWorld"), 120 },
			{ nullptr, 0 },
			{ TEXT("/Game/ParagonFey/FX/Particles/Fey/Abilities/Growth/FX/P_Growth_PoisonSpores_Boom.P_Growth_PoisonSpores_Boom"), 100 },
			{ TEXT("/Game/ParagonSteel/FX/Particles/Steel/Skins/Doomsday/P_ShieldSolar_Doomsday.P_ShieldSolar_Doomsday"), 176 },
			{ TEXT("/Game/ParagonSerath/FX/Particles/Abilities/Ultimate/FX/P_HolyStateActive.P_HolyStateActive"), 104 } },
		{ TEXT("shadow"), FLinearColor(0.6f, 0.2f, 0.9f),
			{ TEXT("/Game/ParagonSerath/FX/Particles/Abilities/Fury/FX/P_Fury_CastingHandEvil.P_Fury_CastingHandEvil"), 31 },
			{ TEXT("/Game/ParagonGideon/FX/Particles/Gideon/Abilities/ProjectileMeteor/FX/P_Gideon_RMB_Proj.P_Gideon_RMB_Proj"), 55 },
			{ TEXT("/Game/ParagonMinions/FX/Particles/Minions/Prime_Helix/Abilities/PrimaryAttack/FX/Helix_PrimaryImpact.Helix_PrimaryImpact"), 286 },
			{ nullptr, 0 },
			{ TEXT("/Game/ParagonMinions/FX/Particles/Buffs/Buff_Black_V2/Abilities/Spawn/FX/P_Buff_Black_SpawnFX.P_Buff_Black_SpawnFX"), 243 },
			{ TEXT("/Game/ParagonCountess/FX/Particles/Abilities/Ultimate/FX/p_CountessUltImpact.p_CountessUltImpact"), 172 },
			{ TEXT("/Game/ParagonMinions/FX/Particles/Buffs/Buff_Black_V2/Abilities/Spawn/FX/P_Buff_Black_SpawnFX.P_Buff_Black_SpawnFX"), 243 } },
		{ TEXT("arcane"), FLinearColor(0.75f, 0.4f, 1.0f),
			{ TEXT("/Game/ParagonGideon/FX/Particles/Gideon/Abilities/Portal/FX/P_Portal_Cast.P_Portal_Cast"), 15 },
			{ TEXT("/Game/ParagonGideon/FX/Particles/Gideon/Abilities/Meteor/FX/P_Gideon_Meteor_Trail.P_Gideon_Meteor_Trail"), 74 },
			{ TEXT("/Game/ParagonMuriel/FX/Particles/Abilities/LifeLock/FX/P_LifeLock_HitCharacter.P_LifeLock_HitCharacter"), 296 },
			{ nullptr, 0 },
			{ TEXT("/Game/ParagonMinions/FX/Particles/Minions/Prime_Helix/Abilities/SpecialAttack2/FX/P_PH_Shockwave_V2.P_PH_Shockwave_V2"), 1191 },
			{ TEXT("/Game/ParagonGideon/FX/Particles/Gideon/Abilities/ProjectileMeteor/FX/P_Gideon_RMB_HitWorld.P_Gideon_RMB_HitWorld"), 509 },
			{ TEXT("/Game/ParagonLtBelica/FX/Particles/Belica/Abilities/TeslaConduit/FX/P_Wing_Burst.P_Wing_Burst"), 146 } },
		{ TEXT("steel"), FLinearColor(1.0f, 0.9f, 0.75f),
			{ nullptr, 0 },
			{ nullptr, 0 },
			{ TEXT("/Game/ParagonGreystone/FX/Particles/Greystone/Abilities/ClearAPath/FX/P_Greystone_ClearAPath_Impacts.P_Greystone_ClearAPath_Impacts"), 390 },
			{ nullptr, 0 },
			{ TEXT("/Game/ParagonDrongo/FX/Particles/Abilities/Ultimate/FX/P_Drongo_Ultimate_Explosion.P_Drongo_Ultimate_Explosion"), 1343 },
			{ TEXT("/Game/ParagonGreystone/FX/Particles/Greystone/Abilities/ClearAPath/FX/P_Greystone_ClearAPath_Impacts.P_Greystone_ClearAPath_Impacts"), 390 },
			{ TEXT("/Game/ParagonMinions/FX/Particles/Buffs/Buff_Red/FX/P_Buff_Red_SpawnFX.P_Buff_Red_SpawnFX"), 510 } },
	};
	constexpr int32 NumAbilityLooks = UE_ARRAY_COUNT(GAbilityLooks);
	constexpr int32 SteelLook = NumAbilityLooks - 1;

	const FLookFx GLookBlunt = { TEXT("/Game/ParagonRampage/FX/Particles/Abilities/Primary/FX/P_Rampage_Melee_Impact.P_Rampage_Melee_Impact"), 68 };
	const FLookFx GLookBlunt2 = { TEXT("/Game/ParagonRampage/FX/Particles/Abilities/RipNToss/FX/P_RipNToss_HandImpact.P_RipNToss_HandImpact"), 177 };
	const FLookFx GLookPierce = { TEXT("/Game/ParagonWraith/FX/Particles/Abilities/ScopedShot/FX/P_Wraith_Sniper_HitCharacter.P_Wraith_Sniper_HitCharacter"), 185 };
	const FLookFx GLookHeal = { TEXT("/Game/ParagonGreystone/FX/Particles/Greystone/Skins/Novaborn/P_Greystone_Novaborn_HToKill_Resurrect.P_Greystone_Novaborn_HToKill_Resurrect"), 170 };
	const FLookFx GLookHeal2 = { TEXT("/Game/ParagonKhaimera/FX/ParticleSystems/Abilities/WarriorSustain/FX/P_Passive_Activate.P_Passive_Activate"), 33 };
	const FLookFx GLookRevive = { TEXT("/Game/ParagonFey/FX/Particles/Fey/Abilities/Growth/FX/P_Growth_PoisonSpores_Boom.P_Growth_PoisonSpores_Boom"), 100 };
	const FLookFx GLookDebuff = { TEXT("/Game/ParagonMinions/FX/Particles/Buffs/Buff_Black_V2/Abilities/Spawn/FX/P_Buff_Black_SpawnFX.P_Buff_Black_SpawnFX"), 243 };

	/** The words that say what an ability is made of, one list per look above (not steel). */
	const char* const GLookWords[] =
	{
		"flame fire molten salamander ember meteor burn blaze inferno magma scorch pyre ash cinder mortar bomb",
		"frost ice hail hailstorm blizzard yeti freeze frozen chill glacier rime snow zero absolute",
		"thunder thunderbird lightning spark shock storm volt static",
		"tide wave leviathan water maelstrom torrent flood surf rain",
		"gale cyclone roc wind air gust tempest zephyr sirocco",
		"stone quake golem rock earth boulder granite crag tremor",
		"thorn wild bramble treant barb herd venom poison toxic spore vine rot decay tar",
		"holy seraph sanctuary radiance radiant light divine aegis sacred bless cure raise rebirth mend dawn prism refract flare",
		"shadow umbral hex curse doom lich death dark void night soul dread pact grave",
		"time quicken chord ballad anthem finale arcane mana star astral song hymn veil tick clockwork dissonance carbuncle",
	};
	static_assert(UE_ARRAY_COUNT(GLookWords) == NumAbilityLooks - 1, "one word list per look but steel");

	std::vector<std::string> LookWordsOf(const std::string& Text)
	{
		std::vector<std::string> Out;
		std::string Word;
		for (const char C : Text)
		{
			if (std::isalpha(static_cast<unsigned char>(C)))
			{
				Word += static_cast<char>(std::tolower(static_cast<unsigned char>(C)));
			}
			else if (!Word.empty())
			{
				Out.push_back(Word);
				Word.clear();
			}
		}
		if (!Word.empty())
		{
			Out.push_back(Word);
		}
		return Out;
	}

	bool LookListed(const char* List, const std::string& Word)
	{
		for (const std::string& Each : LookWordsOf(List))
		{
			if (Each == Word)
			{
				return true;
			}
		}
		return false;
	}

	int32 LookFromWords(const std::vector<std::string>& Words)
	{
		for (const std::string& Word : Words)
		{
			for (int32 i = 0; i < NumAbilityLooks - 1; ++i)
			{
				if (LookListed(GLookWords[i], Word))
				{
					return i;
				}
			}
		}
		return -1;
	}

	int32 LookNamedIndex(const TCHAR* Name)
	{
		for (int32 i = 0; i < NumAbilityLooks; ++i)
		{
			if (FCString::Strcmp(GAbilityLooks[i].Name, Name) == 0)
			{
				return i;
			}
		}
		return -1;
	}

	/** The built-in effect it borrows, and the status it leaves, as a look. */
	const TCHAR* LookFromFx(const std::string& Fx)
	{
		if (Fx == "fire" || Fx == "meteor") { return TEXT("fire"); }
		if (Fx == "blizzard") { return TEXT("ice"); }
		if (Fx == "holy_blade" || Fx == "sanctuary" || Fx == "cure" || Fx == "raise") { return TEXT("holy"); }
		return nullptr;
	}

	const TCHAR* LookFromStatus(const std::string& Status)
	{
		if (Status == "burn" || Status == "oiled") { return TEXT("fire"); }
		if (Status == "freeze" || Status == "chilled" || Status == "slow") { return TEXT("ice"); }
		if (Status == "wet") { return TEXT("water"); }
		if (Status == "stun") { return TEXT("lightning"); }
		if (Status == "regen") { return TEXT("holy"); }
		if (Status == "doom" || Status == "silence" || Status == "terrified") { return TEXT("shadow"); }
		if (Status == "sleep" || Status == "charmed" || Status == "stop") { return TEXT("arcane"); }
		if (Status == "root" || Status == "decay") { return TEXT("nature"); }
		return nullptr;
	}

	/** The element the rules read (fire, ice, lightning, water) as a look. */
	const TCHAR* LookFromElement(const std::string& Element)
	{
		if (Element == "fire") { return TEXT("fire"); }
		if (Element == "ice") { return TEXT("ice"); }
		if (Element == "lightning") { return TEXT("lightning"); }
		if (Element == "water") { return TEXT("water"); }
		return nullptr;
	}

	/** What a plain weapon does: 0 cuts, 1 crushes, 2 pierces, 3 is a thrown stone. */
	uint8 LookWeaponOf(const std::vector<std::string>& Words, const std::string& Fx)
	{
		const char* Pierce = "arrow snipe volley kunai lance jab spear pin shot bolt barb needle dart quill thrust shuriken";
		const char* Blunt = "mace bash smash slam uppercut punch rod staff fist hammer club stomp headbutt shove palm maul";
		if (Fx == "throw_stone")
		{
			return 3;
		}
		if (Fx == "pin_shot" || Fx == "bow_shot" || Fx == "aimed_shot" || Fx == "arrow_rain")
		{
			return 2;
		}
		if (Fx == "shield_bash" || Fx == "punch" || Fx == "wave_fist")
		{
			return 1;
		}
		for (const std::string& Word : Words)
		{
			if (LookListed(Pierce, Word)) { return 2; }
			if (LookListed(Blunt, Word)) { return 1; }
		}
		return 0;
	}

	bool LookIsStruck(TMSim::EEventKind Kind)
	{
		return Kind == TMSim::EEventKind::Hit || Kind == TMSim::EEventKind::Absorbed
			|| Kind == TMSim::EEventKind::Revived || Kind == TMSim::EEventKind::StatusApplied;
	}

	FLinearColor LookBrighter(const FLinearColor& Colour, float By)
	{
		return FLinearColor(Colour.R * By, Colour.G * By, Colour.B * By, 1.0f);
	}
}

void ATMBattleDirector::LookAssetPaths(TArray<FSoftObjectPath>& Paths) const
{
	// Read behind the loading bar with the heroes, not the first time each is
	// wanted mid-battle: that stalled the game a tenth of a second or more each
	// time (the lag hunt, 2026-09-30).
	for (const FFlavourLook& Look : GAbilityLooks)
	{
		for (const FLookFx* Fx : { &Look.Cast, &Look.Shot, &Look.Hit, &Look.Hit2, &Look.Burst, &Look.Burst2, &Look.Aura })
		{
			if (Fx->Path && *Fx->Path)
			{
				Paths.AddUnique(FSoftObjectPath(FString(Fx->Path)));
			}
		}
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
	return GAbilityLooks[FMath::Clamp(Look.Flavour, 0, NumAbilityLooks - 1)].Name;
}

const ATMBattleDirector::FTMLook& ATMBattleDirector::LookOf(const TMSim::FAbility& Ability)
{
	const FString Key = UTF8_TO_TCHAR(Ability.Id.c_str());
	if (const FTMLook* Known = Looks.Find(Key))
	{
		return *Known;
	}
	FTMLook Look;
	// Its own name says it best ("Flame Lance"); then what it borrows, what it
	// leaves, the element the rules give it; then its id, which starts with
	// its class ("frost_stalker_..."); and failing all that, plain steel.
	const std::vector<std::string> Named = LookWordsOf(Ability.Name);
	const std::vector<std::string> Id = LookWordsOf(Ability.Id);
	int32 Flavour = LookFromWords(Named);
	const TCHAR* Said = nullptr;
	if (Flavour < 0 && (Said = LookFromFx(Ability.Fx)) != nullptr)
	{
		Flavour = LookNamedIndex(Said);
	}
	if (Flavour < 0 && Ability.HasStatus() && (Said = LookFromStatus(Ability.StatusId)) != nullptr)
	{
		Flavour = LookNamedIndex(Said);
	}
	if (Flavour < 0 && (Said = LookFromElement(TMSim::ElementOf(Ability))) != nullptr)
	{
		Flavour = LookNamedIndex(Said);
	}
	if (Flavour < 0)
	{
		Flavour = LookFromWords(Id);
	}
	Look.Flavour = Flavour < 0 ? SteelLook : Flavour;
	std::vector<std::string> All = Named;
	All.insert(All.end(), Id.begin(), Id.end());
	Look.Weapon = LookWeaponOf(All, Ability.Fx);
	Look.bMagic = Look.Flavour != SteelLook;

	const std::string Shape = TMSim::ShapeOf(Ability);
	Look.bArea = (Shape == "circle" && Ability.Aoe > 0.0f) || Shape == "cone" || Shape == "line" || Shape == "vector"
		|| (Shape == "self" && Ability.Aoe > 0.0f);
	Look.bRanged = (Shape == "unit" && Ability.MaxRange > 2.2f) || Shape == "line";
	return Looks.Add(Key, Look);
}

UFXSystemComponent* ATMBattleDirector::PlayFx(const TCHAR* Path, const FVector& Local, float WantCm, float SizeCm,
	USceneComponent* AttachTo)
{
	if (!Path || !*Path)
	{
		return nullptr;
	}
	const FString Key(Path);
	TObjectPtr<UObject>* Known = LoadedVfx.Find(Key);
	if (!Known)
	{
		UObject* System = FSoftObjectPath(Key).TryLoad();
		if (!System || !(System->IsA<UNiagaraSystem>() || System->IsA<UParticleSystem>()))
		{
			UE_LOG(LogTemp, Warning, TEXT("An ability look names a particle effect that is not in the project: %s"), *Key);
			System = nullptr;
		}
		Known = &LoadedVfx.Add(Key, System);
	}
	if (!*Known)
	{
		return nullptr;
	}
	const float Size = FMath::Clamp(WantCm / FMath::Max(SizeCm, 10.0f), 0.08f, 6.0f);
	UFXSystemComponent* Playing = nullptr;
	if (AttachTo)
	{
		if (UNiagaraSystem* Niagara = Cast<UNiagaraSystem>(*Known))
		{
			Playing = UNiagaraFunctionLibrary::SpawnSystemAttached(Niagara, AttachTo, NAME_None, FVector::ZeroVector,
				FRotator::ZeroRotator, FVector(Size), EAttachLocation::KeepRelativeOffset, true, ENCPoolMethod::None);
		}
		else if (UParticleSystem* Cascade = Cast<UParticleSystem>(*Known))
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
	if (UNiagaraSystem* Niagara = Cast<UNiagaraSystem>(*Known))
	{
		Playing = UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, Niagara, Where, FRotator::ZeroRotator, FVector(Size));
	}
	else if (UParticleSystem* Cascade = Cast<UParticleSystem>(*Known))
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
	const FFlavourLook& Set = GAbilityLooks[Look.Flavour];
	const FVector At = ShownAt(*User);
	const std::string Shape = TMSim::ShapeOf(*Blow.Ability);
	const bool bOnSelf = Shape == "self" && Blow.Ability->Aoe <= 0.0f;

	// Something it does to itself shows now, round it: a buff's aura, a heal.
	if (bOnSelf && Blow.Ability->Effect != TMSim::EEffect::Damage)
	{
		if (Blow.Ability->Effect == TMSim::EEffect::Heal)
		{
			PlayFx(GLookHeal.Path, At + FVector(0.0f, 0.0f, 90.0f), 190.0f, GLookHeal.SizeCm);
			Pulse(At + FVector(0.0f, 0.0f, 120.0f), FLinearColor(0.35f, 1.0f, 0.5f), 9000.0f, 380.0f, 0.6f);
		}
		else
		{
			PlayFx(Set.Aura.Path, At + FVector(0.0f, 0.0f, 60.0f), 200.0f, Set.Aura.SizeCm);
			Pulse(At + FVector(0.0f, 0.0f, 120.0f), Set.Colour, 8000.0f, 380.0f, 0.6f);
		}
		return;
	}
	// Magic gathers in the hands before it goes.
	if (Look.bMagic || Blow.Ability->Effect != TMSim::EEffect::Damage)
	{
		const int32 Index = Battle.Units.empty() ? INDEX_NONE : static_cast<int32>(User - &Battle.Units[0]);
		const float Yaw = Motions.IsValidIndex(Index) ? Motions[Index].Yaw : 0.0f;
		const FVector Hands = At + FVector(0.0f, 0.0f, 115.0f) + FRotator(0.0f, Yaw, 0.0f).Vector() * 45.0f;
		PlayFx(Set.Cast.Path, Hands, Blow.Slot == 3 ? 200.0f : 130.0f, Set.Cast.SizeCm);
		Pulse(Hands, Set.Colour, Blow.Slot == 3 ? 14000.0f : 7000.0f, 320.0f, 0.5f);
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
	const FFlavourLook& Set = GAbilityLooks[Look.Flavour];
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
				PlayFx(Set.Burst.Path, Centre, Across, Set.Burst.SizeCm);
				PlayFx(Set.Burst2.Path, Centre, Across * 0.8f, Set.Burst2.SizeCm);
				Pulse(Centre + FVector(0.0f, 0.0f, 150.0f), LookBrighter(Set.Colour, 1.0f), Blow.Slot == 3 ? 60000.0f : 30000.0f,
					Across * 1.3f, 0.55f);
				Jolt(Blow.Slot == 3 ? 1.2f : 0.55f + Heaviest);
			}
			else
			{
				// A blessing on everyone standing in it.
				const FLookFx& Glow = Ability.Effect == TMSim::EEffect::Heal ? GLookHeal : Set.Aura;
				PlayFx(Glow.Path, Centre, Across * 0.7f, Glow.SizeCm);
				Pulse(Centre + FVector(0.0f, 0.0f, 150.0f), Ability.Effect == TMSim::EEffect::Heal
					? FLinearColor(0.35f, 1.0f, 0.5f) : Set.Colour, 16000.0f, Across * 1.2f, 0.7f);
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
	const FFlavourLook& Set = GAbilityLooks[Look.Flavour];
	const FVector Body = ShownAt(*Unit) + FVector(0.0f, 0.0f, 95.0f);
	const float Big = (bCritical ? 1.4f : 1.0f) * (bArea ? 0.75f : 1.0f);

	if (Event.Kind == TMSim::EEventKind::Revived)
	{
		PlayFx(GLookRevive.Path, Body, 260.0f, GLookRevive.SizeCm);
		PlayFx(GLookHeal.Path, Body, 200.0f, GLookHeal.SizeCm);
		Pulse(Body + FVector(0.0f, 0.0f, 80.0f), FLinearColor(1.0f, 0.9f, 0.55f), 22000.0f, 450.0f, 0.9f);
		return;
	}
	if (Ability.Effect == TMSim::EEffect::Heal)
	{
		PlayFx(GLookHeal.Path, Body, 190.0f, GLookHeal.SizeCm);
		PlayFx(GLookHeal2.Path, Body, 150.0f, GLookHeal2.SizeCm);
		Pulse(Body, FLinearColor(0.35f, 1.0f, 0.5f), 9000.0f, 350.0f, 0.6f);
		return;
	}
	if (Ability.Effect == TMSim::EEffect::Support && Ability.Target != TMSim::ETargetSide::Enemy)
	{
		PlayFx(Set.Aura.Path, ShownAt(*Unit) + FVector(0.0f, 0.0f, 60.0f), 190.0f, Set.Aura.SizeCm);
		Pulse(Body, Set.Colour, 7000.0f, 320.0f, 0.6f);
		return;
	}

	// A blow, or a curse: what it is made of, on the one it struck.
	if (Look.bMagic)
	{
		// Sized for the game's camera, which frames the board from well back.
		PlayFx(Set.Hit.Path, Body, 230.0f * Big, Set.Hit.SizeCm);
		PlayFx(Set.Hit2.Path, Body, 200.0f * Big, Set.Hit2.SizeCm);
	}
	else
	{
		switch (Look.Weapon)
		{
		case 1:
		case 3:
			PlayFx(GLookBlunt.Path, Body, 170.0f * Big, GLookBlunt.SizeCm);
			PlayFx(GLookBlunt2.Path, Body - FVector(0.0f, 0.0f, 60.0f), 150.0f * Big, GLookBlunt2.SizeCm);
			break;
		case 2:
			PlayFx(GLookPierce.Path, Body, 150.0f * Big, GLookPierce.SizeCm);
			break;
		default:
			PlayFx(Set.Hit.Path, Body, 200.0f * Big, Set.Hit.SizeCm);
			break;
		}
	}
	if (Ability.Effect == TMSim::EEffect::Support && (Look.Flavour == SteelLook || Look.Flavour == LookNamedIndex(TEXT("shadow"))))
	{
		PlayFx(GLookDebuff.Path, ShownAt(*Unit) + FVector(0.0f, 0.0f, 40.0f), 170.0f, GLookDebuff.SizeCm);
	}
	Pulse(Body, LookBrighter(Set.Colour, bCritical ? 1.3f : 1.0f), (Look.bMagic ? 12000.0f : 6000.0f) * Big, 300.0f * Big, 0.35f);
}

UFXSystemComponent* ATMBattleDirector::LookShot(const FTMLook& Look, USceneComponent* Carrier)
{
	const FFlavourLook& Set = GAbilityLooks[FMath::Clamp(Look.Flavour, 0, NumAbilityLooks - 1)];
	return Carrier ? PlayFx(Set.Shot.Path, FVector::ZeroVector, 110.0f, Set.Shot.SizeCm, Carrier) : nullptr;
}

FLinearColor ATMBattleDirector::LookColour(const FTMLook& Look)
{
	return GAbilityLooks[FMath::Clamp(Look.Flavour, 0, NumAbilityLooks - 1)].Colour;
}
