// What a designer picked, in the class creator's Cast Studio, for how each
// ability moves each body: Content/Data/CastStudio/AbilityAnimation.json.
//
// Cast Studio (Docs/CastStudio-Plan.md) authors how abilities look and move.
// The creator publishes what was picked; the director plays it. This reads the
// published file and nothing else: no Unreal types, so the plain-C++ tests
// check it in a second, as they check the rules.
//
// It is presentation only. The rules never include this module, so nothing
// here can change a battle, a replay or an online match by a hair. An empty
// or missing file means every ability moves exactly as it did before Cast
// Studio existed: the character map's clips, and the motion's usual contact.
//
// The file, by ability id and then by animation set (clips are made for one
// skeleton, so a pick belongs to the set whose skeleton it was filmed on):
//
//   { "format": "tactical-masters-ability-animation", "schemaVersion": 1,
//     "abilities": { "<ability id>": { "<animation set>": {
//         "windup":  { "clip": "/Game/..." },              before the release; at the start of a cast
//         "release": { "clip": "/Game/...", "contact": 0.4 },   the blow; contact is when it connects,
//                                                          as a share of the clip (alone: today's clip)
//         "loop":    { "clip": "/Game/..." },              while it charges or channels
//         "recover": { "clip": "/Game/..." } } } } }       after the release, before standing again

#pragma once

#include <map>
#include <string>
#include <vector>

// UBT defines TMCAST_API when it builds this module; the fallback is for the
// standalone tests, as TMSIM_API's is.
#ifndef TMCAST_API
#define TMCAST_API
#endif

namespace TMCast
{
	/** One clip picked for one part of an ability's motion. */
	struct FClipPick
	{
		/** The clip's object path, or empty: keep what the character map plays. */
		std::string Clip;
		/** Release only: when it connects, 0 to 1 of the clip; below zero, the motion's usual. */
		double Contact = -1.0;

		bool IsEmpty() const { return Clip.empty() && Contact < 0.0; }
	};

	/** Everything picked for one ability on one animation set. */
	struct FAnimationPicks
	{
		FClipPick Windup;
		FClipPick Release;
		FClipPick Loop;
		FClipPick Recover;

		bool IsEmpty() const { return Windup.IsEmpty() && Release.IsEmpty() && Loop.IsEmpty() && Recover.IsEmpty(); }
	};

	struct FAnimationFile
	{
		/** By ability id, then animation set. Only entries with something picked. */
		std::map<std::string, std::map<std::string, FAnimationPicks>> Abilities;
		/**
		 * What was set aside, and why, one line each: a pick that names no clip,
		 * a contact outside 0 to 1, a key this version does not know. The rest of
		 * the file still counts. Said once when the file is read, never silent.
		 */
		std::vector<std::string> Problems;

		/** What was picked for an ability on a set, or null for nothing. */
		TMCAST_API const FAnimationPicks* Find(const std::string& Ability, const std::string& Set) const;
		/** Every clip the file names, once each, sorted: loaded behind the loading bar. */
		TMCAST_API std::vector<std::string> Clips() const;
	};

	inline const char* AnimationFormat() { return "tactical-masters-ability-animation"; }
	constexpr int AnimationSchema = 1;

	/**
	 * Reads the file's text. False when it cannot be read at all -- not JSON, not
	 * this format, a newer schema -- and then Out is empty and Problems says why.
	 * Anything smaller is set aside into Problems and the rest is kept.
	 */
	TMCAST_API bool ReadAnimationFile(const std::string& Text, FAnimationFile& Out);

	/**
	 * When a blow connects, in seconds after its swing begins: whatever plays
	 * before the release (a picked wind-up), then the contact share of the
	 * release. A share outside 0 to 1 is held to it; no release, the lead alone.
	 */
	TMCAST_API double ContactSeconds(double LeadSeconds, double ReleaseSeconds, double Share);
}
