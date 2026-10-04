#include "CastLegacy.h"

#include "SimAbility.h"
#include "SimBattle.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace TMCast
{
	namespace
	{
		// The effects are Fab's Paragon particles, chosen by watching the strips
		// Unreal films of them (Tools\VfxCatalog.bat); each is scaled from how big
		// it was filmed to the size wanted. Moved here from the director's
		// TMBattleDirectorAbilityFx.cpp unchanged (2026-10-02, Cast Studio phase 2).
	const FLegacyFlavour GAbilityLooks[] =
	{
		{ "fire", FCastVec{1.0, 0.45, 0.1},
			{ "/Game/ParagonGRIMexe/FX/Particles/Abilities/BFG/FX/P_GRIM_BFG_MuzzleFlash.P_GRIM_BFG_MuzzleFlash", 81 },
			{ "/Game/ParagonGRIMexe/FX/Particles/Abilities/Ultimate/FX/P_GRIM_Ultimate_Projectile_New.P_GRIM_Ultimate_Projectile_New", 283 },
			{ "/Game/ParagonGRIMexe/FX/Particles/Abilities/Ultimate/FX/P_GRIM_Ultimate_HitCharacter.P_GRIM_Ultimate_HitCharacter", 357 },
			{ nullptr, 0 },
			{ "/Game/ParagonHowitzer/FX/Particles/Abilities/Ultimate/FX/P_HB_Ult_Explo_High.P_HB_Ult_Explo_High", 384 },
			{ "/Game/ParagonIggyScorch/FX/Particles/IggyScorch/Abilities/Primary/FX/P_IggyScorch_Molotov_HitPlayer.P_IggyScorch_Molotov_HitPlayer", 259 },
			{ "/Game/ParagonMinions/FX/Particles/Buffs/Buff_Red/FX/P_Buff_Red_SpawnFX.P_Buff_Red_SpawnFX", 510 } },
		{ "ice", FCastVec{0.55, 0.85, 1.0},
			{ "/Game/ParagonRevenant/FX/Particles/Revenant/Skins/FrostKing/Abilities/Primary/FX/P_Revenant_FrostKing_LastShot_MuzzleFlash.P_Revenant_FrostKing_LastShot_MuzzleFlash", 212 },
			{ "/Game/ParagonMorigesh/FX/Particles/Morigesh/Skins/NorthernMystic/P_Morigesh_NorthernMystic_SkillshotAOE_Projectile.P_Morigesh_NorthernMystic_SkillshotAOE_Projectile", 18 },
			{ "/Game/ParagonRampage/FX/Particles/Rampage_v001_IceBlue/FX/P_Rampage_Ice_Melee_Impact.P_Rampage_Ice_Melee_Impact", 199 },
			{ nullptr, 0 },
			{ "/Game/ParagonRevenant/FX/Particles/Revenant/Skins/FrostKing/Abilities/Primary/FX/P_Revenant_FrostKing_Primary_HitWorld.P_Revenant_FrostKing_Primary_HitWorld", 195 },
			{ "/Game/ParagonRampage/FX/Particles/Rampage_v001_IceBlue/FX/P_Rampage_Ice_Lunge_Impact.P_Rampage_Ice_Lunge_Impact", 1273 },
			{ "/Game/ParagonLtBelica/FX/Particles/Belica/Abilities/TeslaConduit/FX/P_Wing_Burst.P_Wing_Burst", 146 } },
		{ "lightning", FCastVec{0.55, 0.7, 1.0},
			{ "/Game/ParagonGadget/FX/Abilities/Primary/FX/P_PrimaryZap_Muzzle.P_PrimaryZap_Muzzle", 75 },
			{ "/Game/ParagonGadget/FX/Abilities/Primary/FX/P_PrimaryZap_Projectile_Trail.P_PrimaryZap_Projectile_Trail", 365 },
			{ "/Game/ParagonGadget/FX/Abilities/Primary/FX/P_PrimaryZap_Projectile_Explode_Player.P_PrimaryZap_Projectile_Explode_Player", 128 },
			{ nullptr, 0 },
			{ "/Game/ParagonKwang/FX/Particles/Abilities/LightStrike/FX/P_Kwang_LightStrike_Burst.P_Kwang_LightStrike_Burst", 1351 },
			{ "/Game/ParagonDekker/FX/Particles/Abilities/SlowBomb/FX/P_Dekker_SlowBomb_Explosion.P_Dekker_SlowBomb_Explosion", 833 },
			{ "/Game/ParagonKwang/FX/Particles/Abilities/LightStrike/FX/P_KwangBuff.P_KwangBuff", 56 } },
		{ "water", FCastVec{0.25, 0.75, 1.0},
			{ "/Game/ParagonGideon/FX/Particles/Gideon/Skins/Undertow/P_Meteor_Cast_Undertow.P_Meteor_Cast_Undertow", 14 },
			{ "/Game/ParagonZinx/FX/Particles/Zinx/Abilities/StunShot/FX/P_Zinx_StunShot_Projectile.P_Zinx_StunShot_Projectile", 93 },
			{ "/Game/ParagonTwinblast/FX/Particles/SummerTime/FX/P_TwinBlast_VortexGrenade_Explode_Summer.P_TwinBlast_VortexGrenade_Explode_Summer", 765 },
			{ nullptr, 0 },
			{ "/Game/ParagonTwinblast/FX/Particles/SummerTime/FX/P_TwinBlast_VortexGrenade_Explode_Summer.P_TwinBlast_VortexGrenade_Explode_Summer", 765 },
			{ "/Game/ParagonGideon/FX/Particles/Gideon/Skins/Undertow/P_Ult_Cast_Undertow.P_Ult_Cast_Undertow", 2148 },
			{ "/Game/ParagonMinions/FX/Particles/Minions/Prime_Helix/Abilities/SpecialAttack2/FX/P_PH_Bubble.P_PH_Bubble", 659 } },
		{ "wind", FCastVec{0.7, 1.0, 0.9},
			{ "/Game/PotaVFX_Smoke/VFX/System/SmokeBurst/NS_MagicSmokeBurstAir.NS_MagicSmokeBurstAir", 279 },
			{ "/Game/ParagonWraith/FX/Particles/Abilities/ScopedShot/FX/P_Wraith_Sniper_Projectile.P_Wraith_Sniper_Projectile", 81 },
			{ "/Game/ParagonKhaimera/FX/ParticleSystems/Abilities/Leap/FX/P_Khaimera_Leap_AOE_Burst_Air.P_Khaimera_Leap_AOE_Burst_Air", 662 },
			{ nullptr, 0 },
			{ "/Game/ParagonSerath/FX/Particles/Abilities/Ascend/FX/P_FallenAngel_Ascend_HitSmoke.P_FallenAngel_Ascend_HitSmoke", 418 },
			{ "/Game/ParagonKhaimera/FX/ParticleSystems/Abilities/Leap/FX/P_Khaimera_Leap_AOE_Burst_Air.P_Khaimera_Leap_AOE_Burst_Air", 662 },
			{ "/Game/PotaVFX_Smoke/VFX/System/SmokeBurst/NS_MagicSmokeBurstAir.NS_MagicSmokeBurstAir", 279 } },
		{ "earth", FCastVec{0.85, 0.65, 0.4},
			{ "/Game/ParagonGrux/FX/Particles/Abilities/Stampede/FX/P_Stampede_Cast.P_Stampede_Cast", 602 },
			{ nullptr, 0 },
			{ "/Game/ParagonRampage/FX/Particles/Abilities/RipNToss/FX/P_RipNToss_HandImpact.P_RipNToss_HandImpact", 177 },
			{ "/Game/ParagonGreystone/FX/Particles/Greystone/Abilities/ClearAPath/FX/P_Greystone_ClearAPath_Impacts.P_Greystone_ClearAPath_Impacts", 390 },
			{ "/Game/ParagonMinions/FX/Particles/Minions/Prime_Helix/Abilities/SpecialAttack3/FX/P_Prime_Ground_Box.P_Prime_Ground_Box", 283 },
			{ "/Game/ParagonDrongo/FX/Particles/Abilities/Ultimate/FX/P_Drongo_Ultimate_Explosion.P_Drongo_Ultimate_Explosion", 1343 },
			{ "/Game/ParagonSteel/FX/Particles/Steel/Abilities/ShieldBlock/FX/P_Steel_Shieldblock.P_Steel_Shieldblock", 143 } },
		{ "nature", FCastVec{0.45, 1.0, 0.3},
			{ "/Game/ParagonMinions/FX/Particles/Buffs/Buff_Green/Abilities/Spawn/FX/P_Buff_Green_SpawnFX.P_Buff_Green_SpawnFX", 327 },
			{ "/Game/ParagonDrongo/FX/Particles/Abilities/Shards/FX/P_Drongo_Shards_Projectile_Bullet.P_Drongo_Shards_Projectile_Bullet", 195 },
			{ "/Game/ParagonSevarog/FX/Particles/Abilities/Primary/FX/P_Sevarog_Melee_SucessfulImpact.P_Sevarog_Melee_SucessfulImpact", 179 },
			{ "/Game/ParagonMorigesh/FX/Particles/Morigesh/Abilities/SkillshotAOE/FX/P_Morigesh_SkillshotAOE_Explosion.P_Morigesh_SkillshotAOE_Explosion", 814 },
			{ "/Game/ParagonFey/FX/Particles/Fey/Abilities/Ultimate/FX/P_Ultimate_Root_Spikes.P_Ultimate_Root_Spikes", 305 },
			{ "/Game/ParagonSevarog/FX/Particles/Abilities/SoulSiphon/FX/P_SiphonCasting.P_SiphonCasting", 60 },
			{ "/Game/ParagonMinions/FX/Particles/Buffs/Buff_Green/Abilities/Spawn/FX/P_Buff_Green_SpawnFX.P_Buff_Green_SpawnFX", 327 } },
		{ "holy", FCastVec{1.0, 0.85, 0.45},
			{ "/Game/ParagonDekker/FX/Particles/Abilities/SlowField/FX/P_Dekker_SlowField_HitWorld.P_Dekker_SlowField_HitWorld", 176 },
			{ "/Game/ParagonMuriel/FX/Particles/Abilities/Primary/FX/P_Muriel_Primary_Projectile.P_Muriel_Primary_Projectile", 44 },
			{ "/Game/ParagonFey/FX/Particles/Fey/Abilities/Growth/FX/P_Growth_HitWorld.P_Growth_HitWorld", 120 },
			{ nullptr, 0 },
			{ "/Game/ParagonFey/FX/Particles/Fey/Abilities/Growth/FX/P_Growth_PoisonSpores_Boom.P_Growth_PoisonSpores_Boom", 100 },
			{ "/Game/ParagonSteel/FX/Particles/Steel/Skins/Doomsday/P_ShieldSolar_Doomsday.P_ShieldSolar_Doomsday", 176 },
			{ "/Game/ParagonSerath/FX/Particles/Abilities/Ultimate/FX/P_HolyStateActive.P_HolyStateActive", 104 } },
		{ "shadow", FCastVec{0.6, 0.2, 0.9},
			{ "/Game/ParagonSerath/FX/Particles/Abilities/Fury/FX/P_Fury_CastingHandEvil.P_Fury_CastingHandEvil", 31 },
			{ "/Game/ParagonGideon/FX/Particles/Gideon/Abilities/ProjectileMeteor/FX/P_Gideon_RMB_Proj.P_Gideon_RMB_Proj", 55 },
			{ "/Game/ParagonMinions/FX/Particles/Minions/Prime_Helix/Abilities/PrimaryAttack/FX/Helix_PrimaryImpact.Helix_PrimaryImpact", 286 },
			{ nullptr, 0 },
			{ "/Game/ParagonMinions/FX/Particles/Buffs/Buff_Black_V2/Abilities/Spawn/FX/P_Buff_Black_SpawnFX.P_Buff_Black_SpawnFX", 243 },
			{ "/Game/ParagonCountess/FX/Particles/Abilities/Ultimate/FX/p_CountessUltImpact.p_CountessUltImpact", 172 },
			{ "/Game/ParagonMinions/FX/Particles/Buffs/Buff_Black_V2/Abilities/Spawn/FX/P_Buff_Black_SpawnFX.P_Buff_Black_SpawnFX", 243 } },
		{ "arcane", FCastVec{0.75, 0.4, 1.0},
			{ "/Game/ParagonGideon/FX/Particles/Gideon/Abilities/Portal/FX/P_Portal_Cast.P_Portal_Cast", 15 },
			{ "/Game/ParagonGideon/FX/Particles/Gideon/Abilities/Meteor/FX/P_Gideon_Meteor_Trail.P_Gideon_Meteor_Trail", 74 },
			{ "/Game/ParagonMuriel/FX/Particles/Abilities/LifeLock/FX/P_LifeLock_HitCharacter.P_LifeLock_HitCharacter", 296 },
			{ nullptr, 0 },
			{ "/Game/ParagonMinions/FX/Particles/Minions/Prime_Helix/Abilities/SpecialAttack2/FX/P_PH_Shockwave_V2.P_PH_Shockwave_V2", 1191 },
			{ "/Game/ParagonGideon/FX/Particles/Gideon/Abilities/ProjectileMeteor/FX/P_Gideon_RMB_HitWorld.P_Gideon_RMB_HitWorld", 509 },
			{ "/Game/ParagonLtBelica/FX/Particles/Belica/Abilities/TeslaConduit/FX/P_Wing_Burst.P_Wing_Burst", 146 } },
		{ "steel", FCastVec{1.0, 0.9, 0.75},
			{ nullptr, 0 },
			{ nullptr, 0 },
			{ "/Game/ParagonGreystone/FX/Particles/Greystone/Abilities/ClearAPath/FX/P_Greystone_ClearAPath_Impacts.P_Greystone_ClearAPath_Impacts", 390 },
			{ nullptr, 0 },
			{ "/Game/ParagonDrongo/FX/Particles/Abilities/Ultimate/FX/P_Drongo_Ultimate_Explosion.P_Drongo_Ultimate_Explosion", 1343 },
			{ "/Game/ParagonGreystone/FX/Particles/Greystone/Abilities/ClearAPath/FX/P_Greystone_ClearAPath_Impacts.P_Greystone_ClearAPath_Impacts", 390 },
			{ "/Game/ParagonMinions/FX/Particles/Buffs/Buff_Red/FX/P_Buff_Red_SpawnFX.P_Buff_Red_SpawnFX", 510 } },
	};
	constexpr int NumAbilityLooks = static_cast<int>(sizeof(GAbilityLooks) / sizeof(GAbilityLooks[0]));
	constexpr int SteelLook = NumAbilityLooks - 1;

	const FLegacyFx GLookBlunt = { "/Game/ParagonRampage/FX/Particles/Abilities/Primary/FX/P_Rampage_Melee_Impact.P_Rampage_Melee_Impact", 68 };
	const FLegacyFx GLookBlunt2 = { "/Game/ParagonRampage/FX/Particles/Abilities/RipNToss/FX/P_RipNToss_HandImpact.P_RipNToss_HandImpact", 177 };
	const FLegacyFx GLookPierce = { "/Game/ParagonWraith/FX/Particles/Abilities/ScopedShot/FX/P_Wraith_Sniper_HitCharacter.P_Wraith_Sniper_HitCharacter", 185 };
	const FLegacyFx GLookHeal = { "/Game/ParagonGreystone/FX/Particles/Greystone/Skins/Novaborn/P_Greystone_Novaborn_HToKill_Resurrect.P_Greystone_Novaborn_HToKill_Resurrect", 170 };
	const FLegacyFx GLookHeal2 = { "/Game/ParagonKhaimera/FX/ParticleSystems/Abilities/WarriorSustain/FX/P_Passive_Activate.P_Passive_Activate", 33 };
	const FLegacyFx GLookRevive = { "/Game/ParagonFey/FX/Particles/Fey/Abilities/Growth/FX/P_Growth_PoisonSpores_Boom.P_Growth_PoisonSpores_Boom", 100 };
	const FLegacyFx GLookDebuff = { "/Game/ParagonMinions/FX/Particles/Buffs/Buff_Black_V2/Abilities/Spawn/FX/P_Buff_Black_SpawnFX.P_Buff_Black_SpawnFX", 243 };

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
		static_assert(sizeof(GLookWords) / sizeof(GLookWords[0]) == static_cast<size_t>(NumAbilityLooks - 1), "one word list per look but steel");

		const FLegacyCommon GCommon = { GLookBlunt, GLookBlunt2, GLookPierce, GLookHeal, GLookHeal2, GLookRevive, GLookDebuff };

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

		int LookFromWords(const std::vector<std::string>& Words)
		{
			for (const std::string& Word : Words)
			{
				for (int i = 0; i < NumAbilityLooks - 1; ++i)
				{
					if (LookListed(GLookWords[i], Word))
					{
						return i;
					}
				}
			}
			return -1;
		}

		/** The built-in effect it borrows, and the status it leaves, as a look. */
		const char* LookFromFx(const std::string& Fx)
		{
			if (Fx == "fire" || Fx == "meteor") { return "fire"; }
			if (Fx == "blizzard") { return "ice"; }
			if (Fx == "holy_blade" || Fx == "sanctuary" || Fx == "cure" || Fx == "raise") { return "holy"; }
			return nullptr;
		}

		const char* LookFromStatus(const std::string& Status)
		{
			if (Status == "burn" || Status == "oiled") { return "fire"; }
			if (Status == "freeze" || Status == "chilled" || Status == "slow") { return "ice"; }
			if (Status == "wet") { return "water"; }
			if (Status == "stun") { return "lightning"; }
			if (Status == "regen") { return "holy"; }
			if (Status == "doom" || Status == "silence" || Status == "terrified") { return "shadow"; }
			if (Status == "sleep" || Status == "charmed" || Status == "stop") { return "arcane"; }
			if (Status == "root" || Status == "decay") { return "nature"; }
			return nullptr;
		}

		/** The element the rules read (fire, ice, lightning, water) as a look. */
		const char* LookFromElement(const std::string& Element)
		{
			if (Element == "fire") { return "fire"; }
			if (Element == "ice") { return "ice"; }
			if (Element == "lightning") { return "lightning"; }
			if (Element == "water") { return "water"; }
			return nullptr;
		}

		/** What a plain weapon does: 0 cuts, 1 crushes, 2 pierces, 3 is a thrown stone. */
		int LookWeaponOf(const std::vector<std::string>& Words, const std::string& Fx)
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

		const FCastVec HealGreen = { 0.35, 1.0, 0.5 };

		// ---------------------------------------------------- building events

		FLookEvent Particle(EMoment Moment, const FLegacyFx& Fx, EAnchor Anchor, double Up, double Size)
		{
			FLookEvent Event;
			Event.Moment = Moment;
			Event.Kind = EEffectKind::Particle;
			Event.Effect = Fx.Path ? Fx.Path : "";
			Event.Anchor = Anchor;
			Event.Offset = { 0.0, 0.0, Up };
			Event.Size = Size;
			return Event;
		}

		FLookEvent Light(EMoment Moment, EAnchor Anchor, double Up, const FCastVec& Tint, double Brightness, double Radius, double Seconds)
		{
			FLookEvent Event;
			Event.Moment = Moment;
			Event.Kind = EEffectKind::Light;
			Event.Anchor = Anchor;
			Event.Offset = { 0.0, 0.0, Up };
			Event.Tint = Tint;
			Event.Brightness = Brightness;
			Event.Radius = Radius;
			Event.Duration = Seconds;
			return Event;
		}

		/** Today's hands: 115 cm up and 45 cm in front of where it stands (not a socket: today never used one). */
		void InHands(FLookEvent& Event)
		{
			Event.Anchor = EAnchor::Ground;
			Event.Offset = { HandReach, 0.0, HandHeight };
		}

		/** Fitted to the area as today: the area's width times Share, held between Least x Share and 1400 x Share. */
		void FitToArea(FLookEvent& Event, double Share, double Least)
		{
			Event.Size = 0.0;
			Event.bFitArea = true;
			Event.FitScale = Share;
			Event.MinSize = Least * Share;
			Event.MaxSize = 1400.0 * Share;
		}

		void Add(FLook& Look, const FLookEvent& Event, bool bNeedsEffect = true)
		{
			if (!bNeedsEffect || !Event.Effect.empty())
			{
				Look.Events.push_back(Event);
			}
		}

		/** An event twice: as it is, when the blow is not critical; scaled up, when it is. */
		void AddBothWays(FLook& Look, FLookEvent Event, double CritSize, double CritBrightness = 1.0, double CritTint = 1.0)
		{
			if (Event.Kind == EEffectKind::Particle && Event.Effect.empty())
			{
				return;
			}
			Event.When = EWhen::Normal;
			Look.Events.push_back(Event);
			Event.When = EWhen::Critical;
			Event.Size *= CritSize;
			Event.Radius *= CritSize;
			Event.Brightness *= CritBrightness;
			Event.Tint = Event.Tint * CritTint;
			Look.Events.push_back(Event);
		}
	}

	int LegacyFlavourCount() { return NumAbilityLooks; }

	const FLegacyFlavour& LegacyFlavour(int Index)
	{
		return GAbilityLooks[std::min(NumAbilityLooks - 1, std::max(0, Index))];
	}

	int LegacyFlavourNamed(const char* Name)
	{
		for (int i = 0; i < NumAbilityLooks; ++i)
		{
			if (std::string(GAbilityLooks[i].Name) == Name)
			{
				return i;
			}
		}
		return -1;
	}

	const FLegacyCommon& LegacyCommonFx() { return GCommon; }

	FLegacyKind LegacyKindOf(const TMSim::FAbility& Ability)
	{
		FLegacyKind Look;
		// Its own name says it best ("Flame Lance"); then what it borrows, what it
		// leaves, the element the rules give it; then its id, which starts with
		// its class ("frost_stalker_..."); and failing all that, plain steel.
		const std::vector<std::string> Named = LookWordsOf(Ability.Name);
		const std::vector<std::string> Id = LookWordsOf(Ability.Id);
		int Flavour = LookFromWords(Named);
		const char* Said = nullptr;
		if (Flavour < 0 && (Said = LookFromFx(Ability.Fx)) != nullptr)
		{
			Flavour = LegacyFlavourNamed(Said);
		}
		if (Flavour < 0 && Ability.HasStatus() && (Said = LookFromStatus(Ability.StatusId)) != nullptr)
		{
			Flavour = LegacyFlavourNamed(Said);
		}
		if (Flavour < 0 && (Said = LookFromElement(TMSim::ElementOf(Ability))) != nullptr)
		{
			Flavour = LegacyFlavourNamed(Said);
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
		return Look;
	}

	ELegacyShot LegacyShotOf(const TMSim::FAbility& Ability, const std::string& Motion)
	{
		if (Ability.Fx == "throw_stone")
		{
			return ELegacyShot::Stone;
		}
		if (Motion == "shoot")
		{
			return ELegacyShot::Arrow;
		}
		if (Motion == "bolt" && Ability.Effect == TMSim::EEffect::Damage)
		{
			return ELegacyShot::Orb;
		}
		// A spell thrown from afar flies there, whatever the motion that throws
		// it; earth throws a stone.
		const FLegacyKind Kind = LegacyKindOf(Ability);
		if (Kind.bMagic && Kind.bRanged && Ability.Effect != TMSim::EEffect::Revive)
		{
			return Kind.Flavour == LegacyFlavourNamed("earth") ? ELegacyShot::Stone : ELegacyShot::Orb;
		}
		return ELegacyShot::None;
	}

	FCastVec LegacyShotColour(const TMSim::FAbility& Ability, ELegacyShot Shot)
	{
		const std::string& Fx = Ability.Fx;
		if (Fx == "fire" || Fx == "meteor") { return { 1.0, 0.42, 0.08 }; }
		if (Fx == "blizzard") { return { 0.55, 0.85, 1.0 }; }
		if (Fx == "holy_blade" || Fx == "sanctuary" || Fx == "cure" || Fx == "raise") { return { 1.0, 0.88, 0.45 }; }
		if (Fx == "haste" || Fx == "focus" || Fx == "chakra") { return { 0.45, 1.0, 0.55 }; }
		if (Shot == ELegacyShot::Arrow) { return { 0.42, 0.28, 0.14 }; }
		if (Shot == ELegacyShot::Stone) { return { 0.35, 0.33, 0.3 }; }
		return { 0.7, 0.45, 1.0 };  // plain magic
	}

	FLook LegacyLook(const TMSim::FAbility& Ability, int Slot)
	{
		FLook Look;
		Look.Strip = EStrip::All;
		const FLegacyKind Kind = LegacyKindOf(Ability);
		const FLegacyFlavour& Set = LegacyFlavour(Kind.Flavour);
		const std::string Shape = TMSim::ShapeOf(Ability);
		const bool bUltimate = Slot == 3;
		const bool bHeal = Ability.Effect == TMSim::EEffect::Heal;
		const bool bSupport = Ability.Effect == TMSim::EEffect::Support;

		// The class file's own effect, where it names one (PlayVfx).
		if (!Ability.VfxSystem.empty())
		{
			FLookEvent Own;
			Own.Kind = EEffectKind::Particle;
			Own.Effect = Ability.VfxSystem;
			Own.Scale = std::max(0.01, std::min(20.0, static_cast<double>(Ability.VfxScale)));
			if (Ability.VfxAt == "user")
			{
				Own.Moment = EMoment::Swing;
				Own.Anchor = EAnchor::Ground;
				Own.Offset = { 0.0, 0.0, 90.0 };
				Add(Look, Own);
			}
			else if (Ability.VfxAt == "point")
			{
				Own.Moment = EMoment::Area;
				Own.Anchor = EAnchor::Aim;
				Add(Look, Own);
			}
			else if (Ability.VfxAt == "targets")
			{
				Own.Moment = EMoment::Impact;
				Own.On = EOn::Any;
				Own.Anchor = EAnchor::Ground;
				Own.Offset = { 0.0, 0.0, 90.0 };
				Add(Look, Own);
			}
		}

		// The swing begins (LookCast).
		if (Ability.Kind != "passive")
		{
			const bool bOnSelf = Shape == "self" && Ability.Aoe <= 0.0f;
			if (bOnSelf && Ability.Effect != TMSim::EEffect::Damage)
			{
				// Something it does to itself shows now, round it: a buff's aura, a heal.
				if (bHeal)
				{
					Add(Look, Particle(EMoment::Swing, GLookHeal, EAnchor::Ground, 90.0, 190.0));
					Add(Look, Light(EMoment::Swing, EAnchor::Ground, 120.0, HealGreen, 9000.0, 380.0, 0.6), false);
				}
				else
				{
					Add(Look, Particle(EMoment::Swing, Set.Aura, EAnchor::Ground, 60.0, 200.0));
					Add(Look, Light(EMoment::Swing, EAnchor::Ground, 120.0, Set.Colour, 8000.0, 380.0, 0.6), false);
				}
			}
			else if (Kind.bMagic || Ability.Effect != TMSim::EEffect::Damage)
			{
				// Magic gathers in the hands before it goes.
				FLookEvent Flare = Particle(EMoment::Swing, Set.Cast, EAnchor::Ground, 0.0, bUltimate ? 200.0 : 130.0);
				InHands(Flare);
				Add(Look, Flare);
				FLookEvent Glow = Light(EMoment::Swing, EAnchor::Ground, 0.0, Set.Colour, bUltimate ? 14000.0 : 7000.0, 320.0, 0.5);
				InHands(Glow);
				Add(Look, Glow, false);
			}
		}

		// What flies (LaunchShots, LookShot): the look's own shot, and the glow an orb gives off.
		const ELegacyShot Shot = LegacyShotOf(Ability, TMSim::MotionOf(Ability, Slot));
		if (Shot == ELegacyShot::Orb)
		{
			if (Kind.bMagic && Set.Shot.IsSet())
			{
				FLookEvent Flying = Particle(EMoment::Projectile, Set.Shot, EAnchor::Body, 0.0, 110.0);
				Flying.Offset = {};
				Add(Look, Flying);
			}
			FLookEvent Glow = Light(EMoment::Projectile, EAnchor::Body, 0.0,
				Kind.bMagic ? Set.Colour : LegacyShotColour(Ability, Shot), 8000.0, 260.0, 0.0);
			Glow.Offset = {};
			Add(Look, Glow, false);
		}

		// It lands (LookLand): the ground it covers bursts, sized to it; or a jolt as hard as it hurt.
		if (Kind.bArea)
		{
			const double Least = Shape == "cone" ? 250.0 : (Shape == "line" || Shape == "vector") ? 260.0 : 220.0;
			const bool bHarms = Ability.Effect == TMSim::EEffect::Damage || (bSupport && Ability.Target == TMSim::ETargetSide::Enemy);
			if (bHarms)
			{
				FLookEvent Burst = Particle(EMoment::Area, Set.Burst, EAnchor::Center, 0.0, 0.0);
				FitToArea(Burst, 1.0, Least);
				Add(Look, Burst);
				FLookEvent Burst2 = Particle(EMoment::Area, Set.Burst2, EAnchor::Center, 0.0, 0.0);
				FitToArea(Burst2, 0.8, Least);
				Add(Look, Burst2);
				FLookEvent Flash = Light(EMoment::Area, EAnchor::Center, 150.0, Set.Colour, bUltimate ? 60000.0 : 30000.0, 0.0, 0.55);
				FitToArea(Flash, 1.3, Least);
				Add(Look, Flash, false);
				FLookEvent Shake;
				Shake.Moment = EMoment::Area;
				Shake.Kind = EEffectKind::Shake;
				Shake.Anchor = EAnchor::Center;
				Shake.Strength = bUltimate ? 1.2 : 0.55;
				Shake.Harm = bUltimate ? EHarm::None : EHarm::Add;
				Add(Look, Shake, false);
			}
			else
			{
				// A blessing on everyone standing in it.
				FLookEvent Glow = Particle(EMoment::Area, bHeal ? GLookHeal : Set.Aura, EAnchor::Center, 0.0, 0.0);
				FitToArea(Glow, 0.7, Least);
				Add(Look, Glow);
				FLookEvent Flash = Light(EMoment::Area, EAnchor::Center, 150.0, bHeal ? HealGreen : Set.Colour, 16000.0, 0.0, 0.7);
				FitToArea(Flash, 1.2, Least);
				Add(Look, Flash, false);
			}
		}
		else
		{
			// A single blow jolts as hard as it hurt, harder for a critical one or an ultimate.
			FLookEvent Shake;
			Shake.Moment = EMoment::Area;
			Shake.Kind = EEffectKind::Shake;
			Shake.Anchor = EAnchor::Center;
			Shake.Harm = EHarm::Scale;
			Shake.Strength = bUltimate ? 1.5 : 1.0;
			Shake.When = EWhen::Normal;
			Look.Events.push_back(Shake);
			Shake.Strength *= 1.5;
			Shake.When = EWhen::Critical;
			Look.Events.push_back(Shake);
		}

		// On each unit it touched (LookOn).
		if (Ability.Effect == TMSim::EEffect::Revive)
		{
			FLookEvent Up = Particle(EMoment::Impact, GLookRevive, EAnchor::Body, 0.0, 260.0);
			Up.On = EOn::Revived;
			Add(Look, Up);
			FLookEvent Mend = Particle(EMoment::Impact, GLookHeal, EAnchor::Body, 0.0, 200.0);
			Mend.On = EOn::Revived;
			Add(Look, Mend);
			FLookEvent Gold = Light(EMoment::Impact, EAnchor::Body, 80.0, { 1.0, 0.9, 0.55 }, 22000.0, 450.0, 0.9);
			Gold.On = EOn::Revived;
			Add(Look, Gold, false);
		}
		if (bHeal)
		{
			Add(Look, Particle(EMoment::Impact, GLookHeal, EAnchor::Body, 0.0, 190.0));
			Add(Look, Particle(EMoment::Impact, GLookHeal2, EAnchor::Body, 0.0, 150.0));
			Add(Look, Light(EMoment::Impact, EAnchor::Body, 0.0, HealGreen, 9000.0, 350.0, 0.6), false);
		}
		else if (bSupport && Ability.Target != TMSim::ETargetSide::Enemy)
		{
			Add(Look, Particle(EMoment::Impact, Set.Aura, EAnchor::Ground, 60.0, 190.0));
			Add(Look, Light(EMoment::Impact, EAnchor::Body, 0.0, Set.Colour, 7000.0, 320.0, 0.6), false);
		}
		else
		{
			// A blow, or a curse: what it is made of, on the one it struck. A
			// critical one is 1.4 times the size; on an area, three quarters.
			const double Big = Kind.bArea ? 0.75 : 1.0;
			if (Kind.bMagic)
			{
				// Sized for the game's camera, which frames the board from well back.
				AddBothWays(Look, Particle(EMoment::Impact, Set.Hit, EAnchor::Body, 0.0, 230.0 * Big), 1.4);
				AddBothWays(Look, Particle(EMoment::Impact, Set.Hit2, EAnchor::Body, 0.0, 200.0 * Big), 1.4);
			}
			else if (Kind.Weapon == 1 || Kind.Weapon == 3)
			{
				AddBothWays(Look, Particle(EMoment::Impact, GLookBlunt, EAnchor::Body, 0.0, 170.0 * Big), 1.4);
				AddBothWays(Look, Particle(EMoment::Impact, GLookBlunt2, EAnchor::Body, -60.0, 150.0 * Big), 1.4);
			}
			else if (Kind.Weapon == 2)
			{
				AddBothWays(Look, Particle(EMoment::Impact, GLookPierce, EAnchor::Body, 0.0, 150.0 * Big), 1.4);
			}
			else
			{
				AddBothWays(Look, Particle(EMoment::Impact, Set.Hit, EAnchor::Body, 0.0, 200.0 * Big), 1.4);
			}
			if (bSupport && (Kind.Flavour == SteelLook || Kind.Flavour == LegacyFlavourNamed("shadow")))
			{
				Add(Look, Particle(EMoment::Impact, GLookDebuff, EAnchor::Ground, 40.0, 170.0));
			}
			AddBothWays(Look, Light(EMoment::Impact, EAnchor::Body, 0.0, Set.Colour, (Kind.bMagic ? 12000.0 : 6000.0) * Big, 300.0 * Big, 0.35),
				1.4, 1.4, 1.3);
		}
		return Look;
	}

	std::string LegacyFootprintsJson()
	{
		std::map<std::string, double> All;
		for (const FLegacyFlavour& Set : GAbilityLooks)
		{
			for (const FLegacyFx* Fx : { &Set.Cast, &Set.Shot, &Set.Hit, &Set.Hit2, &Set.Burst, &Set.Burst2, &Set.Aura })
			{
				if (Fx->IsSet())
				{
					All[Fx->Path] = Fx->SizeCm;
				}
			}
		}
		for (const FLegacyFx* Fx : { &GLookBlunt, &GLookBlunt2, &GLookPierce, &GLookHeal, &GLookHeal2, &GLookRevive, &GLookDebuff })
		{
			All[Fx->Path] = Fx->SizeCm;
		}
		std::string Out = "{";
		for (const auto& Each : All)
		{
			char Number[32];
			std::snprintf(Number, sizeof(Number), "%g", Each.second);
			Out += (Out.size() > 1 ? ", \"" : "\"") + Each.first + "\": " + Number;
		}
		return Out + "}";
	}
}
