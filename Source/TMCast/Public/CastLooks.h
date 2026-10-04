// How each ability looks, as a designer authored it in the class creator's Cast
// Studio: Content/Data/CastStudio/AbilityLooks.json.
//
// An ability's look is a list of events. Each says which moment of the ability
// it belongs to (the cast beginning, the release, the shot in flight, the hit
// on each target, the burst on the ground, a status it leaves, ...), what plays
// there (a particle effect from the project, a flash of light or a jolt of the
// camera), where (an anchor on a body or the ground, an offset, a size), when
// (a delay, how long) and in what case (always, or only on a critical hit).
// The director plays them (TMBattleDirectorCast.cpp); this only reads the file
// and answers questions about it, so the plain-C++ tests check every rule here
// in a second. Docs/CastStudio-Plan.md, sections 3.1, 3.2, 3.4 and 3.5.
//
// Presentation only. The rules never include this module, so a look can never
// change a battle, a replay or an online match. With no file -- or nothing in
// it for an ability -- the ability looks exactly as it did before Cast Studio.
//
//   { "format": "tactical-masters-ability-looks", "schemaVersion": 1,
//     "abilities": { "<ability id>": { "strip": "none|legacy|all", "events": [ {event}, ... ] } },
//     "statuses":  { "<status id>":  { "events": [ ... ] } },     statusGain/statusActive/statusTick/statusEnd
//     "reactions": { "<shock|freeze|ignite|douse|thaw>": { "events": [ ... ] } },   reaction
//     "footprints": { "<effect path>": <centimetres across, as filmed> } }
//
// An event:
//   "moment":   castStart casting swing release projectile impact area targetStatus allyStatus selfStatus
//               tick expire summon  (an ability's); statusGain statusActive statusTick statusEnd (a status's);
//               reaction (a reaction's)
//   "effect":   an effect's object path ("/Game/.../P_X.P_X"), or "light", or "shake", or "sound"
//   "anchor":   ground body head hand weapon center aim   (default body; center for area)
//   "follow":   true to move with what it is on (lasting moments always do)
//   "offset":   [forward, right, up] centimetres, turned with the unit's facing
//   "rotation": [pitch, yaw, roll] degrees, from the unit's facing
//   "size":     centimetres across wanted; or "fit": "area" with "fitScale" (1 = the area's own size),
//               held between "minSize" and "maxSize" when given; or neither, and "scale" (1 = as made)
//   "delay":    seconds after the moment     "duration": seconds (0: the moment's own length)
//   "repeat":   seconds between plays while a lasting moment lasts (0: once)
//   "when":     always critical normal       "on" (impact): struck evaded revived any
//   "side" (impact): any enemy ally
//   light:  "tint": [r, g, b], "brightness", "radius" (cm; fit sizes it instead)
//   shake:  "strength" (1 is a heavy blow; 0 to 3, what is felt held to 1.5), "harm": none scale add (by how hard the blow hurt)
//   sound:  "sound" (a SoundWave or SoundCue's object path), "volume" (1 as made), "pitch" (1 as made)
//   "part", "share" (swing only): played at a share of a part of the swing as the
//               body plays it -- "windup", "release" or "recover", 0 to 1 -- so a
//               whoosh lands on the frame the blade moves, whatever the clip's length.
//               The delay is added after it.
//
// An ability with a sound event of its own plays its sounds instead of today's
// swing and landing sounds (TMBattleDirectorSound.cpp); its hero's voice stays.

#pragma once

#include <map>
#include <string>
#include <vector>

#ifndef TMCAST_API
#define TMCAST_API
#endif

namespace TMCast
{
	/** A point or a direction in centimetres: X forward (east), Y right, Z up, as Unreal measures. */
	struct FCastVec
	{
		double X = 0.0;
		double Y = 0.0;
		double Z = 0.0;
	};
	inline FCastVec operator+(const FCastVec& A, const FCastVec& B) { return { A.X + B.X, A.Y + B.Y, A.Z + B.Z }; }
	inline FCastVec operator-(const FCastVec& A, const FCastVec& B) { return { A.X - B.X, A.Y - B.Y, A.Z - B.Z }; }
	inline FCastVec operator*(const FCastVec& A, double S) { return { A.X * S, A.Y * S, A.Z * S }; }

	enum class EMoment : unsigned char
	{
		// An ability's.
		CastStart,     // a cast begins (CastStarted); an instant ability's swing beginning
		Casting,       // while it charges or channels                          (lasting)
		Swing,         // the swing begins, after any walk there, cast or not
		Release,       // the blow connects: when the shot leaves, or the sword lands
		Projectile,    // on each shot in flight                                (lasting)
		Impact,        // on each unit it touched, as it lands
		Area,          // once as it lands: on the ground it covers, or the spot aimed at
		TargetStatus,  // on an enemy wearing a status it gave                  (lasting)
		AllyStatus,    // on an ally wearing a status it gave                   (lasting)
		SelfStatus,    // on its user wearing a status it gave                  (lasting)
		Tick,          // a status it gave hurting or healing a turn
		Expire,        // a status it gave ending, a pet it called leaving, a channel's end
		Summon,        // on a pet it called, while the pet stays               (lasting)
		// A status's, whoever gave it.
		StatusGain,
		StatusActive,  //                                                       (lasting)
		StatusTick,
		StatusEnd,
		// A reaction's (shock, freeze, ignite, douse, thaw).
		Reaction,
		Count
	};

	enum class EEffectKind : unsigned char { Particle, Light, Shake, Sound };
	/** A part of the swing a swing event can be timed to (Part, Share). */
	enum class EPart : unsigned char { None, Windup, Release, Recover };
	enum class EAnchor : unsigned char { Ground, Body, Head, Hand, Weapon, Center, Aim };
	enum class EWhen : unsigned char { Always, Critical, Normal };
	enum class EOn : unsigned char { Struck, Evaded, Revived, Any };
	enum class ESide : unsigned char { Any, Enemy, Ally };
	enum class EStrip : unsigned char { None, Legacy, All };
	enum class EHarm : unsigned char { None, Scale, Add };

	TMCAST_API const char* MomentName(EMoment Moment);
	TMCAST_API bool MomentNamed(const std::string& Name, EMoment& Out);
	/** Lives as long as what it is on: a cast, a shot, a status, a pet. */
	TMCAST_API bool IsLasting(EMoment Moment);
	TMCAST_API const char* AnchorName(EAnchor Anchor);
	TMCAST_API const char* StripName(EStrip Strip);

	struct FLookEvent
	{
		EMoment Moment = EMoment::Impact;
		EEffectKind Kind = EEffectKind::Particle;
		/** The effect's object path; empty for a light or a shake. */
		std::string Effect;
		EAnchor Anchor = EAnchor::Body;
		bool bFollow = false;
		/** Forward, right, up, in centimetres, turned with the unit's facing. */
		FCastVec Offset;
		/** Pitch, yaw, roll in degrees, from the unit's facing. */
		FCastVec Rotation;
		/** Centimetres across wanted; 0 for none (then Scale). */
		double Size = 0.0;
		bool bFitArea = false;
		double FitScale = 1.0;
		/** Limits on the size wanted, in centimetres; 0 for none. */
		double MinSize = 0.0;
		double MaxSize = 0.0;
		/** With no size: how big, 1 being as it was made. */
		double Scale = 1.0;
		double Delay = 0.0;
		double Duration = 0.0;
		double Repeat = 0.0;
		EWhen When = EWhen::Always;
		EOn On = EOn::Struck;
		ESide Side = ESide::Any;
		// A light.
		FCastVec Tint = { 1.0, 1.0, 1.0 };
		double Brightness = 8000.0;
		double Radius = 300.0;
		// A shake.
		double Strength = 0.5;
		EHarm Harm = EHarm::None;
		// A sound.
		std::string Sound;
		double Volume = 1.0;
		double Pitch = 1.0;
		/** Swing events: timed to a share of a part of the swing (SyncSeconds). */
		EPart Part = EPart::None;
		double Share = 0.0;
	};

	/** How long each part of a swing plays, in seconds, as the body plays it. */
	struct FSwingTimes
	{
		/** What plays before the release in the swing (a picked wind-up); 0 when a cast played it. */
		double Windup = 0.0;
		double Release = 0.0;
		double Recover = 0.0;
	};

	/**
	 * Seconds after the swing begins that an event timed to the swing plays: the
	 * start of its part plus its share of the part, then its delay. Without a
	 * part, its delay alone.
	 */
	TMCAST_API double SyncSeconds(const FLookEvent& Event, const FSwingTimes& Swing);

	struct FLook
	{
		EStrip Strip = EStrip::None;
		std::vector<FLookEvent> Events;

		TMCAST_API bool Has(EMoment Moment) const;
		/** A projectile event that plays an effect, not only a light: the plain shot is hidden for it (strip all). */
		TMCAST_API bool HasProjectileEffect() const;
		/** A sound of its own: today's swing and landing sounds give way to it. */
		TMCAST_API bool HasSound() const;
	};

	struct FLooksFile
	{
		std::map<std::string, FLook> Abilities;
		std::map<std::string, FLook> Statuses;
		std::map<std::string, FLook> Reactions;
		/** Centimetres across each effect was filmed at, by object path. */
		std::map<std::string, double> Footprints;
		/** What was set aside, and why, one line each. The rest of the file still counts. */
		std::vector<std::string> Problems;

		TMCAST_API const FLook* Ability(const std::string& Id) const;
		TMCAST_API const FLook* Status(const std::string& Id) const;
		TMCAST_API const FLook* Reaction(const std::string& Id) const;
		/** How much of today's look an ability keeps: none stripped when it has no entry. */
		TMCAST_API EStrip StripOf(const std::string& AbilityId) const;
		/** Every effect the file names, once each, sorted: loaded behind the loading bar. */
		TMCAST_API std::vector<std::string> Effects() const;
		/** Every sound the file names, once each, sorted. */
		TMCAST_API std::vector<std::string> Sounds() const;
	};

	inline const char* LooksFormat() { return "tactical-masters-ability-looks"; }
	constexpr int LooksSchema = 1;

	/**
	 * Reads the file's text. False when it cannot be read at all -- not JSON, not
	 * this format, a newer schema -- and then Out is empty and Problems says why.
	 * A smaller fault (an unknown moment, a size below zero) sets that event or
	 * value aside into Problems, and the rest is kept.
	 */
	TMCAST_API bool ReadLooksFile(const std::string& Text, FLooksFile& Out);

	/** One look as the file writes it: {"strip": ..., "events": [...]}. Read back by ReadLooksFile, unchanged. */
	TMCAST_API std::string LookJson(const FLook& Look);
	/** One event, as the file writes it, with only what differs from the defaults. */
	TMCAST_API std::string EventJson(const FLookEvent& Event);

	// ------------------------------------------------------------ which events play

	/** What happened where a moment is raised, for the events' filters. */
	struct FMomentContext
	{
		/** A critical hit: on this unit for an impact, anywhere in the blow otherwise. */
		bool bCritical = false;
		/** Impact only: what the blow did to the unit. */
		EOn Result = EOn::Struck;
		/** Impact only: the unit is on its user's side. */
		bool bAlly = false;
	};

	/** Whether an event plays in this context. */
	TMCAST_API bool Plays(const FLookEvent& Event, const FMomentContext& Context);
	/** The events of a moment that play in a context, in the order the file writes them. */
	TMCAST_API std::vector<const FLookEvent*> EventsFor(const FLook& Look, EMoment Moment, const FMomentContext& Context);

	// ------------------------------------------------------------ where and how big

	/**
	 * How wide an ability's area is, in centimetres, as the rules have it
	 * (TMSim::ShapeOf, MaxRange, Aoe; a metre is 100 cm): a circle's diameter, a
	 * cone's length, a line's width. 0 for a single target.
	 */
	TMCAST_API double AreaAcross(const std::string& Shape, double Aoe, double MaxRange);
	/**
	 * Where an ability's area is centred: round its user (10 cm up) for a self or
	 * a circle with no range, part way along a cone (0.55 of its range) or a line (0.6 of
	 * the way), otherwise the spot aimed at. From and Aim on the ground.
	 */
	TMCAST_API FCastVec AreaCentre(const std::string& Shape, double MaxRange, const FCastVec& From, const FCastVec& Aim);

	/** The size an event wants, in centimetres, for an area that wide; 0 for none (then its Scale). */
	TMCAST_API double WantedSize(const FLookEvent& Event, double AreaCm);
	/** How much to scale an effect filmed FilmedCm across so it shows WantCm across. Held to 0.08-6, as today. */
	TMCAST_API double EffectScale(double WantCm, double FilmedCm);
	/** The scale an event plays its effect at: its wanted size over the effect's footprint, or its Scale. */
	TMCAST_API double ScaleFor(const FLookEvent& Event, const FLooksFile& File, double AreaCm);
	/** How wide an effect was filmed: the file's footprint, or Fallback (cm) when it names none. */
	TMCAST_API double FootprintOf(const FLooksFile& File, const std::string& Effect, double Fallback = 100.0);
	/** A light's radius: its own, or fitted to the area. */
	TMCAST_API double LightRadius(const FLookEvent& Event, double AreaCm);
	/**
	 * A shake's strength for a blow whose heaviest hit took Harm (0-1) of
	 * someone's health. Held to 1.5. "scale" with nothing hurt is no shake.
	 */
	TMCAST_API double ShakeStrength(const FLookEvent& Event, double Harm);

	/** What the director knows of a unit when it places an event on it. */
	struct FAnchorInput
	{
		/** Where it stands on the ground, and which way it faces (degrees). */
		FCastVec Ground;
		double Yaw = 0.0;
		/** Its body's sockets, where it has them (hand_r, weapon_r or weapon, head). */
		bool bHasHand = false;
		FCastVec Hand;
		bool bHasWeapon = false;
		FCastVec Weapon;
		bool bHasHead = false;
		FCastVec Head;
		/** The ability's area centre and aim, on the ground (Area and its anchors). */
		FCastVec Centre;
		FCastVec Aim;
	};

	struct FAnchorPoint
	{
		FCastVec Where;
		/** "socket", or "fallback" when the body has no such socket and a fixed point stands in. */
		const char* Resolved = "fixed";
	};

	/** The point to centre heights on: 95 cm up for a body, as hits play today. */
	constexpr double BodyHeight = 95.0;
	constexpr double HeadHeight = 170.0;
	/** Today's hands, for a body without a hand socket: 115 cm up, 45 cm in front. */
	constexpr double HandHeight = 115.0;
	constexpr double HandReach = 45.0;

	/** An anchor, placed; the offset is not added (OffsetBy). */
	TMCAST_API FAnchorPoint AnchorPoint(EAnchor Anchor, const FAnchorInput& Input);
	/** An offset (forward, right, up) turned to a facing. */
	TMCAST_API FCastVec OffsetBy(const FCastVec& Offset, double Yaw);
}
