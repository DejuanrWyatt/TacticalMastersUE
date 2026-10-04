// Today's look: what an ability looks like with nothing authored for it.
//
// Before Cast Studio, every ability's look was worked out from what it is made
// of (TMBattleDirectorAbilityFx.cpp): its name ("Flame Lance"), the built-in
// effect it borrows, the status it leaves, its element, its id, and failing all
// that plain steel. That reading, and the table of effects it chooses from, live
// here now, so the director plays today's look from them as before, and the
// class lab can write the same look out as Cast Studio events (LegacyLook) for
// the creator's "Import today's look": strip all, plus the imported events,
// looks as it did. Docs/CastStudio-Plan.md, section 3.5.
//
// Engine-free like the rest of TMCast; the director turns the paths into
// effects and the colours into lights.

#pragma once

#include "CastLooks.h"

#include <string>

#ifndef TMCAST_API
#define TMCAST_API
#endif

namespace TMSim
{
	struct FAbility;
}

namespace TMCast
{
	/** An effect, and how wide it was filmed (centimetres of what shows). */
	struct FLegacyFx
	{
		const char* Path;
		double SizeCm;

		bool IsSet() const { return Path && *Path; }
	};

	/** One flavour of look: fire, ice, lightning, ... steel. */
	struct FLegacyFlavour
	{
		const char* Name;
		FCastVec Colour;
		/** In the user's hands as it is cast. */
		FLegacyFx Cast;
		/** What flies to the target, for a spell thrown from afar. */
		FLegacyFx Shot;
		/** On each unit it strikes, and a second layer under it. */
		FLegacyFx Hit;
		FLegacyFx Hit2;
		/** On the ground an area covers, and a second layer. */
		FLegacyFx Burst;
		FLegacyFx Burst2;
		/** On an ally it strengthens. */
		FLegacyFx Aura;
	};

	/** The plain weapon's hits, the heals, the revive and the curse, whatever the flavour. */
	struct FLegacyCommon
	{
		FLegacyFx Blunt;
		FLegacyFx Blunt2;
		FLegacyFx Pierce;
		FLegacyFx Heal;
		FLegacyFx Heal2;
		FLegacyFx Revive;
		FLegacyFx Debuff;
	};

	TMCAST_API int LegacyFlavourCount();
	/** A flavour by index, held to the table; the last is plain steel. */
	TMCAST_API const FLegacyFlavour& LegacyFlavour(int Index);
	TMCAST_API int LegacyFlavourNamed(const char* Name);
	TMCAST_API const FLegacyCommon& LegacyCommonFx();

	/** How an ability is read for its look. */
	struct FLegacyKind
	{
		/** Index into the flavours; the last is plain steel. */
		int Flavour = 0;
		/** For plain steel: 0 blade, 1 blunt, 2 point, 3 thrown stone. */
		int Weapon = 0;
		/** Thrown at its target from afar (a single target or a line). */
		bool bRanged = false;
		/** Lands on an area: a circle, a cone, a line, a burst round the user. */
		bool bArea = false;
		bool bMagic = false;
	};

	/** Reads an ability for its look, as the director always has. */
	TMCAST_API FLegacyKind LegacyKindOf(const TMSim::FAbility& Ability);

	/** What an ability throws, today: nothing, an arrow (a rod), an orb, a stone. */
	enum class ELegacyShot : unsigned char { None, Arrow, Orb, Stone };
	TMCAST_API ELegacyShot LegacyShotOf(const TMSim::FAbility& Ability, const std::string& Motion);
	/** The plain shot's colour, from the built-in effect it borrows. */
	TMCAST_API FCastVec LegacyShotColour(const TMSim::FAbility& Ability, ELegacyShot Shot);

	/**
	 * Today's look for an ability in a slot, as Cast Studio events, with strip
	 * all: played through the runtime player it shows what the procedural look
	 * and the class file's own effect ("vfx") show today. Slot matters: an
	 * ultimate flares brighter, and the motion (and so what flies) depends on it.
	 */
	TMCAST_API FLook LegacyLook(const TMSim::FAbility& Ability, int Slot);
	/** The footprints LegacyLook's effects were measured at, as the file writes them ({"path": cm, ...}). */
	TMCAST_API std::string LegacyFootprintsJson();
}
