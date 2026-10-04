// Cast Studio's published files, as the game reads them (Source/TMCast).
//
// What the class creator publishes for how abilities move
// (Content/Data/CastStudio/AbilityAnimation.json) and how they look
// (AbilityLooks.json) must read the same here as the creator meant it, and
// anything wrong in it must be set aside with a reason rather than played, or
// skipped in silence. A missing file means today's motion and today's look
// everywhere. The creator's own tests (tests/caststudio.test.mjs and castlooks.test.mjs) write
// the samples read below.
//
// The looks are checked for the plan's phase 2 (Docs/CastStudio-Plan.md):
// moments, the order events play in, anchors, strip, when, and fit to area
// within 5%; and today's look, written out as events (CastLegacy.h), must say
// what the director has always played.
//
//   CastTest [Content/Data/CastStudio folder]   with a folder, its files must read cleanly

#include "CastAnimation.h"
#include "CastLegacy.h"
#include "CastLooks.h"

#include "SimAbility.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace TMCast;

namespace
{
	int Failures = 0;

	void Fail(const std::string& What)
	{
		std::printf("FAIL: %s\n", What.c_str());
		++Failures;
	}

	void Check(bool bOk, const std::string& What)
	{
		if (!bOk)
		{
			Fail(What);
		}
	}

	bool Near(double A, double B)
	{
		return std::fabs(A - B) < 1e-9;
	}

	template <typename TFile>
	bool Mentions(const TFile& File, const std::string& Words)
	{
		for (const std::string& Problem : File.Problems)
		{
			if (Problem.find(Words) != std::string::npos)
			{
				return true;
			}
		}
		return false;
	}

	/** What the creator publishes for its sample ledger (tests/caststudio.test.mjs, "publishes the sample"). */
	const char* const Sample = R"({
  "format": "tactical-masters-ability-animation",
  "schemaVersion": 1,
  "abilities": {
    "frost_stalker_ice_dagger": {
      "aurora": {
        "release": {"clip": "/Game/ParagonAurora/Characters/Heroes/Aurora/Animations/Primary_Attack_C.Primary_Attack_C", "contact": 0.35},
        "recover": {"clip": "/Game/ParagonAurora/Characters/Heroes/Aurora/Animations/Primary_Attack_C_Recovery.Primary_Attack_C_Recovery"}
      }
    },
    "storm_bulwark_thunder_slam": {
      "steel": {
        "release": {"contact": 0.6}
      },
      "terra": {
        "windup": {"clip": "/Game/ParagonTerra/Characters/Heroes/Terra/Animations/Ability_R_Start.Ability_R_Start"},
        "loop": {"clip": "/Game/ParagonTerra/Characters/Heroes/Terra/Animations/Ability_R_Loop.Ability_R_Loop"}
      }
    }
  }
})";

	void ReadsTheSample()
	{
		FAnimationFile File;
		Check(ReadAnimationFile(Sample, File), "the sample reads");
		Check(File.Problems.empty(), "the sample has nothing set aside");
		const FAnimationPicks* Dagger = File.Find("frost_stalker_ice_dagger", "aurora");
		Check(Dagger != nullptr, "the dagger's picks on aurora are found");
		if (Dagger)
		{
			Check(Dagger->Release.Clip.find("Primary_Attack_C.Primary_Attack_C") != std::string::npos, "its release clip");
			Check(Near(Dagger->Release.Contact, 0.35), "its contact");
			Check(Dagger->Windup.IsEmpty() && Dagger->Loop.IsEmpty(), "what was not picked stays empty");
			Check(!Dagger->Recover.Clip.empty(), "its recover clip");
		}
		Check(File.Find("frost_stalker_ice_dagger", "steel") == nullptr, "another body has no picks");
		Check(File.Find("no_such_ability", "aurora") == nullptr, "another ability has no picks");
		const FAnimationPicks* Slam = File.Find("storm_bulwark_thunder_slam", "steel");
		Check(Slam && Slam->Release.Clip.empty() && Near(Slam->Release.Contact, 0.6), "a contact alone, on today's clip");
		Check(File.Clips().size() == 4, "four clips named, once each");
	}

	void SetsAsideWhatIsWrong()
	{
		FAnimationFile File;
		Check(ReadAnimationFile(R"({"format": "tactical-masters-ability-animation", "schemaVersion": 1, "abilities": {
			"a": {"aurora": {"release": {"clip": "not a path", "contact": 1.5}, "windup": {"clip": "/Game/X/Y.Y"}, "flourish": {}}},
			"b": {"steel": {"loop": {"clip": "/Game/Z/W.W", "rate": 2}}},
			"c": [1, 2],
			"d": {"grux": {"recover": "/Game/Q/R.R"}}
		}})", File), "a file with mistakes still reads");
		Check(Mentions(File, "a.aurora.release.clip"), "a clip that is no path is said");
		Check(Mentions(File, "a.aurora.release.contact"), "a contact past 1 is said");
		Check(Mentions(File, "a.aurora.flourish"), "a part this version does not know is said");
		Check(Mentions(File, "b.steel.loop.rate"), "a key this version does not know is said");
		Check(Mentions(File, "ability 'c'"), "an ability that is not an object is said");
		Check(Mentions(File, "d.grux.recover"), "a pick that is not an object is said");
		const FAnimationPicks* A = File.Find("a", "aurora");
		Check(A && A->Release.IsEmpty() && A->Windup.Clip == "/Game/X/Y.Y", "the good part of an entry is kept, the bad left out");
		const FAnimationPicks* B = File.Find("b", "steel");
		Check(B && B->Loop.Clip == "/Game/Z/W.W", "a pick with an unknown key keeps its clip");
		Check(File.Find("d", "grux") == nullptr, "an entry with nothing usable is left out");
	}

	void RefusesWhatIsNotItsOwn()
	{
		FAnimationFile File;
		Check(!ReadAnimationFile("{", File) && Mentions(File, "not JSON"), "broken JSON is refused");
		Check(!ReadAnimationFile(R"({"format": "tactical-masters-map", "schemaVersion": 1})", File) && Mentions(File, "format"),
			"another file's format is refused");
		Check(!ReadAnimationFile(R"({"format": "tactical-masters-ability-animation", "schemaVersion": 2, "abilities": {"a": {"s": {"loop": {"clip": "/Game/A.A"}}}}})", File)
			&& File.Abilities.empty() && Mentions(File, "newer"), "a newer schema plays nothing, and says so");
		Check(ReadAnimationFile(R"({"format": "tactical-masters-ability-animation", "schemaVersion": 1})", File)
			&& File.Abilities.empty() && File.Problems.empty(), "a file with nothing picked is fine");
	}

	void ContactMaths()
	{
		Check(Near(ContactSeconds(0.0, 1.2, 0.4), 0.48), "contact is a share of the release");
		Check(Near(ContactSeconds(0.5, 1.0, 0.25), 0.75), "a wind-up before it comes first");
		Check(Near(ContactSeconds(0.0, 1.0, 1.7), 1.0), "a share past the end is held to the end");
		Check(Near(ContactSeconds(0.0, 1.0, -1.0), 0.0), "a share below zero is held to the start");
		Check(Near(ContactSeconds(0.3, 0.0, 0.5), 0.3), "no release: the lead alone");
		Check(Near(ContactSeconds(-2.0, 1.0, 0.5), 0.5), "no negative lead");
	}

	// ------------------------------------------------------------- the looks

	/**
	 * What the creator publishes for its sample looks, byte for byte in its
	 * abilities and footprints (tests/castlooks.test.mjs, "publishes the sample
	 * looks"). The statuses and reactions tables are the game's to read before
	 * the creator writes them: added here by hand.
	 */
	const char* const SampleLooks = R"({
  "format": "tactical-masters-ability-looks",
  "schemaVersion": 1,
  "abilities": {
    "frost_stalker_ice_dagger": {
      "strip": "all",
      "events": [
        {"moment": "castStart", "effect": "/Game/FX/P_Frost_Hands.P_Frost_Hands", "anchor": "hand", "follow": true, "size": 120},
        {"moment": "projectile", "effect": "/Game/FX/P_Frost_Shard.P_Frost_Shard", "size": 90},
        {"moment": "impact", "effect": "/Game/FX/P_Frost_Hit.P_Frost_Hit", "size": 200, "when": "normal"},
        {"moment": "impact", "effect": "/Game/FX/P_Frost_Hit.P_Frost_Hit", "size": 280, "when": "critical"},
        {"moment": "impact", "effect": "light", "on": "any", "tint": [0.5, 0.8, 1], "brightness": 9000, "radius": 300},
        {"moment": "targetStatus", "effect": "/Game/FX/P_Frost_Crust.P_Frost_Crust", "anchor": "ground", "offset": [0, 0, 10], "size": 160},
        {"moment": "expire", "effect": "/Game/FX/P_Frost_Break.P_Frost_Break", "delay": 0.1}
      ]
    },
    "storm_bulwark_thunder_slam": {
      "strip": "legacy",
      "events": [
        {"moment": "area", "effect": "/Game/FX/P_Slam.P_Slam", "fit": "area"},
        {"moment": "area", "effect": "shake", "strength": 0.6, "harm": "add"}
      ]
    }
  },
  "statuses": {
    "burn": {"events": [{"moment": "statusActive", "effect": "/Game/FX/P_Embers.P_Embers", "follow": true, "size": 140}]}
  },
  "reactions": {
    "shock": {"events": [{"moment": "reaction", "effect": "/Game/FX/P_Zap.P_Zap", "size": 220}]}
  },
  "footprints": {"/Game/FX/P_Frost_Hit.P_Frost_Hit": 180, "/Game/FX/P_Slam.P_Slam": 310}
})";

	const char* const LooksHead = R"({"format": "tactical-masters-ability-looks", "schemaVersion": 1, )";

	void ReadsTheSampleLooks()
	{
		FLooksFile File;
		Check(ReadLooksFile(SampleLooks, File), "the sample looks read");
		for (const std::string& Problem : File.Problems)
		{
			Fail("the sample looks: " + Problem);
		}
		const FLook* Dagger = File.Ability("frost_stalker_ice_dagger");
		Check(Dagger && Dagger->Events.size() == 7, "the dagger's seven events");
		if (Dagger)
		{
			Check(Dagger->Strip == EStrip::All, "its strip");
			const FLookEvent& Hands = Dagger->Events[0];
			Check(Hands.Moment == EMoment::CastStart && Hands.Anchor == EAnchor::Hand && Hands.bFollow && Near(Hands.Size, 120.0),
				"the cast-start event, in the hand, following");
			Check(Dagger->Events[4].Kind == EEffectKind::Light && Near(Dagger->Events[4].Duration, 0.5), "a light lasts half a second unless told");
			Check(Dagger->Events[1].Moment == EMoment::Projectile && Dagger->HasProjectileEffect(), "a projectile effect");
			Check(Near(Dagger->Events[5].Offset.Z, 10.0) && Dagger->Events[5].Anchor == EAnchor::Ground, "an offset off the ground");
			Check(Near(Dagger->Events[6].Delay, 0.1), "a delay");
		}
		Check(File.StripOf("storm_bulwark_thunder_slam") == EStrip::Legacy, "strip legacy");
		Check(File.StripOf("no_such_ability") == EStrip::None, "an ability with no entry keeps today's look whole");
		const FLook* Slam = File.Ability("storm_bulwark_thunder_slam");
		Check(Slam && Slam->Events[0].Anchor == EAnchor::Center, "an area event sits on the area's centre unless told");
		Check(Slam && !Slam->HasProjectileEffect(), "no projectile, nothing hidden");
		Check(File.Status("burn") && File.Status("burn")->Events[0].Moment == EMoment::StatusActive, "a status's look");
		Check(File.Reaction("shock") && File.Reaction("shock")->Events.size() == 1, "a reaction's look");
		Check(Near(FootprintOf(File, "/Game/FX/P_Slam.P_Slam"), 310.0) && Near(FootprintOf(File, "/Game/FX/P_None.P_None", 77.0), 77.0),
			"footprints, and the fallback where none is given");
		Check(File.Effects().size() == 8, "eight effects named, once each");
	}

	void SetsAsideWhatIsWrongInLooks()
	{
		FLooksFile File;
		Check(ReadLooksFile(std::string(LooksHead) + R"("abilities": {
			"a": {"strip": "most", "events": [
				{"moment": "flourish", "effect": "/Game/X/Y.Y"},
				{"moment": "statusGain", "effect": "/Game/X/Y.Y"},
				{"moment": "impact", "effect": "fire"},
				{"moment": "impact"},
				{"moment": "impact", "effect": "/Game/X/Y.Y", "size": -4, "when": "sometimes", "sparkle": 2, "offset": [1, 2]},
				{"moment": "release", "effect": "light", "minSize": 300, "maxSize": 100, "repeat": 0.01}
			]},
			"b": [1]
		}, "statuses": {"burn": {"events": [{"moment": "impact", "effect": "/Game/X/Y.Y"}]}},
		"footprints": {"/Game/X/Y.Y": -3}, "colours": {}})", File), "a looks file with mistakes still reads");
		Check(Mentions(File, "abilities.a.strip"), "an unknown strip is said");
		Check(Mentions(File, "events[0]") && Mentions(File, "flourish"), "an unknown moment is said");
		Check(Mentions(File, "events[1]: statusGain is not a moment of this table"), "a status's moment on an ability is said");
		Check(Mentions(File, "events[2].effect"), "an effect that is no path, light or shake is said");
		Check(Mentions(File, "events[3]: no effect"), "an event with no effect is said");
		Check(Mentions(File, "events[4].size") && Mentions(File, "events[4].when") && Mentions(File, "events[4].sparkle")
			&& Mentions(File, "events[4].offset"), "each bad value of a good event is said");
		Check(Mentions(File, "minSize is above maxSize") && Mentions(File, "held to 0.1"), "limits that cross, and a repeat too quick, are said");
		Check(Mentions(File, "abilities.b"), "an entry that is not an object is said");
		Check(Mentions(File, "statuses.burn.events[0]: impact is not a moment of this table"), "an ability's moment on a status is said");
		Check(Mentions(File, "footprints./Game/X/Y.Y") && Mentions(File, "colours"), "a bad footprint and an unknown table are said");
		const FLook* A = File.Ability("a");
		Check(A && A->Events.size() == 2, "the two events that can play are kept");
		Check(A && A->Strip == EStrip::None, "a bad strip strips nothing");
		Check(A && A->Events[0].Size == 0.0 && A->Events[0].When == EWhen::Always, "a bad value keeps its default");
		Check(A && Near(A->Events[1].Repeat, 0.1), "a repeat held to a tenth of a second");
		Check(File.Status("burn") == nullptr, "a status with nothing usable is left out");
	}

	void RefusesLooksThatAreNotItsOwn()
	{
		FLooksFile File;
		Check(!ReadLooksFile("[", File) && Mentions(File, "not JSON"), "broken JSON is refused");
		Check(!ReadLooksFile(R"({"format": "tactical-masters-ability-animation", "schemaVersion": 1})", File) && Mentions(File, "format"),
			"the animation file is not a looks file");
		Check(!ReadLooksFile(R"({"format": "tactical-masters-ability-looks", "schemaVersion": 2, "abilities": {"a": {"events": [{"moment": "impact", "effect": "light"}]}}})", File)
			&& File.Abilities.empty() && Mentions(File, "newer"), "a newer schema plays nothing, and says so");
		Check(ReadLooksFile(R"({"format": "tactical-masters-ability-looks", "schemaVersion": 1})", File)
			&& File.Abilities.empty() && File.Problems.empty(), "a looks file with nothing in it is fine");
	}

	void MomentsAndOrder()
	{
		for (int i = 0; i < static_cast<int>(EMoment::Count); ++i)
		{
			EMoment Back = EMoment::Count;
			Check(MomentNamed(MomentName(static_cast<EMoment>(i)), Back) && Back == static_cast<EMoment>(i), std::string("moment ") + MomentName(static_cast<EMoment>(i)) + " by name");
		}
		Check(IsLasting(EMoment::Casting) && IsLasting(EMoment::Projectile) && IsLasting(EMoment::TargetStatus)
			&& IsLasting(EMoment::Summon) && IsLasting(EMoment::StatusActive), "the lasting moments");
		Check(!IsLasting(EMoment::Impact) && !IsLasting(EMoment::Release) && !IsLasting(EMoment::Expire) && !IsLasting(EMoment::Tick),
			"the one-shot moments");

		FLooksFile File;
		ReadLooksFile(std::string(LooksHead) + R"("abilities": {"a": {"events": [
			{"moment": "impact", "effect": "/Game/A/One.One"},
			{"moment": "release", "effect": "/Game/A/Two.Two"},
			{"moment": "impact", "effect": "/Game/A/Three.Three", "when": "critical"},
			{"moment": "impact", "effect": "/Game/A/Four.Four", "when": "normal", "delay": 0.2},
			{"moment": "impact", "effect": "/Game/A/Five.Five", "on": "evaded"},
			{"moment": "impact", "effect": "/Game/A/Six.Six", "side": "ally"},
			{"moment": "impact", "effect": "/Game/A/Seven.Seven", "on": "any", "side": "enemy"},
			{"moment": "area", "effect": "/Game/A/Eight.Eight", "when": "critical"}
		]}}})", File);
		const FLook& A = File.Abilities["a"];
		auto Names = [](const std::vector<const FLookEvent*>& Events)
		{
			std::string Out;
			for (const FLookEvent* Event : Events)
			{
				Out += (Out.empty() ? "" : " ") + Event->Effect.substr(Event->Effect.find_last_of('.') + 1);
			}
			return Out;
		};
		FMomentContext Plain;
		Check(Names(EventsFor(A, EMoment::Impact, Plain)) == "One Four Seven",
			"an enemy struck: the plain, the normal-only and the enemy-side ones, in the order written");
		FMomentContext Crit;
		Crit.bCritical = true;
		Check(Names(EventsFor(A, EMoment::Impact, Crit)) == "One Three Seven", "a critical one: critical-only in, normal-only out");
		FMomentContext Dodged;
		Dodged.Result = EOn::Evaded;
		Check(Names(EventsFor(A, EMoment::Impact, Dodged)) == "Five Seven", "a dodge: only what plays on a dodge, or on anything");
		FMomentContext Friend;
		Friend.bAlly = true;
		Check(Names(EventsFor(A, EMoment::Impact, Friend)) == "One Four Six", "an ally struck: the ally-side one in, the enemy-side one out");
		Check(Names(EventsFor(A, EMoment::Release, Plain)) == "Two", "another moment's events stay out");
		Check(EventsFor(A, EMoment::Area, Plain).empty() && EventsFor(A, EMoment::Area, Crit).size() == 1, "when applies to every moment");
	}

	void LooksRoundTrip()
	{
		FLooksFile File;
		ReadLooksFile(SampleLooks, File);
		for (const auto& Entry : File.Abilities)
		{
			const std::string Text = std::string(LooksHead) + "\"abilities\": {\"x\": " + LookJson(Entry.second) + "}}";
			FLooksFile Again;
			Check(ReadLooksFile(Text, Again) && Again.Problems.empty(), Entry.first + " written out reads back cleanly");
			Check(Again.Abilities.count("x") && LookJson(Again.Abilities["x"]) == LookJson(Entry.second), Entry.first + " reads back the same");
		}
	}

	void Anchors()
	{
		FAnchorInput On;
		On.Ground = { 100.0, 200.0, 20.0 };
		On.Yaw = 90.0;
		On.Centre = { 500.0, 0.0, 20.0 };
		On.Aim = { 600.0, 0.0, 20.0 };
		auto Is = [](const FCastVec& A, double X, double Y, double Z) { return Near(A.X, X) && Near(A.Y, Y) && Near(A.Z, Z); };
		Check(Is(AnchorPoint(EAnchor::Ground, On).Where, 100.0, 200.0, 20.0), "ground is where it stands");
		Check(Is(AnchorPoint(EAnchor::Body, On).Where, 100.0, 200.0, 115.0), "body is 95 cm up, where hits play today");
		const FAnchorPoint Hand = AnchorPoint(EAnchor::Hand, On);
		Check(Is(Hand.Where, 100.0, 245.0, 135.0) && std::string(Hand.Resolved) == "fallback",
			"no hand socket: today's hands, 115 cm up and 45 cm the way it faces, said to be a fallback");
		Check(std::string(AnchorPoint(EAnchor::Weapon, On).Resolved) == "fallback", "no weapon socket: the hand's fallback");
		Check(Is(AnchorPoint(EAnchor::Head, On).Where, 100.0, 200.0, 190.0), "no head socket: 170 cm up");
		On.bHasHand = true;
		On.Hand = { 1.0, 2.0, 3.0 };
		Check(Is(AnchorPoint(EAnchor::Hand, On).Where, 1.0, 2.0, 3.0) && std::string(AnchorPoint(EAnchor::Hand, On).Resolved) == "socket",
			"a hand socket is used when the body has one");
		Check(Is(AnchorPoint(EAnchor::Weapon, On).Where, 1.0, 2.0, 3.0), "no weapon socket but a hand: the hand");
		On.bHasWeapon = true;
		On.Weapon = { 7.0, 8.0, 9.0 };
		Check(Is(AnchorPoint(EAnchor::Weapon, On).Where, 7.0, 8.0, 9.0), "a weapon socket");
		Check(Is(AnchorPoint(EAnchor::Center, On).Where, 500.0, 0.0, 20.0) && Is(AnchorPoint(EAnchor::Aim, On).Where, 600.0, 0.0, 20.0),
			"the area's centre and the aim");
		Check(Is(OffsetBy({ 10.0, 5.0, 3.0 }, 0.0), 10.0, 5.0, 3.0), "an offset facing east is as written");
		Check(Is(OffsetBy({ 10.0, 5.0, 3.0 }, 90.0), -5.0, 10.0, 3.0), "an offset turns with the facing");
	}

	void FitsTheArea()
	{
		FLooksFile File;
		File.Footprints["/Game/FX/P_Slam.P_Slam"] = 310.0;
		FLookEvent Slam;
		Slam.Kind = EEffectKind::Particle;
		Slam.Effect = "/Game/FX/P_Slam.P_Slam";
		Slam.bFitArea = true;
		// A metre is 100 cm: a circle of radius 2.25 m is 450 cm across, and an
		// effect filmed 310 cm across plays at 1.45.
		const double Circle = AreaCentre("circle", 4.0, { 0, 0, 0 }, { 300, 0, 0 }).X;
		Check(Near(Circle, 300.0), "a circle with range sits on the aim");
		struct FCase { const char* Shape; double Aoe; double Range; double Across; };
		const FCase Cases[] = { { "circle", 2.25, 4.0, 450.0 }, { "self", 1.5, 0.0, 300.0 }, { "cone", 0.0, 5.0, 500.0 },
			{ "line", 0.75, 6.0, 150.0 }, { "vector", 1.0, 6.0, 200.0 } };
		for (const FCase& Case : Cases)
		{
			const double Across = AreaAcross(Case.Shape, Case.Aoe, Case.Range);
			Check(Near(Across, Case.Across), std::string(Case.Shape) + ": the area is as wide as the rules make it");
			const double Shown = ScaleFor(Slam, File, Across) * 310.0;
			Check(std::fabs(Shown - Case.Across) <= 0.05 * Case.Across, std::string(Case.Shape) + ": fitted within 5%");
		}
		Check(Near(AreaAcross("unit", 2.0, 5.0), 0.0), "a single target has no area");
		Check(Near(ScaleFor(Slam, File, 0.0), 1.0), "fitted with no area: as made");
		Slam.FitScale = 0.5;
		Check(Near(ScaleFor(Slam, File, 620.0), 1.0), "half the area's width");
		Slam.MinSize = 400.0;
		Check(Near(WantedSize(Slam, 200.0), 400.0), "held to its least size");
		Slam.MaxSize = 500.0;
		Check(Near(WantedSize(Slam, 3000.0), 500.0), "held to its most");
		Check(Near(EffectScale(100000.0, 10.0), 6.0) && Near(EffectScale(1.0, 1000.0), 0.08), "scales held to 0.08-6, as today");
		FLookEvent Sized;
		Sized.Effect = "/Game/FX/P_Unmeasured.P_Unmeasured";
		Sized.Size = 250.0;
		Check(Near(ScaleFor(Sized, File, 0.0), 2.5), "no footprint: a metre across assumed");
		FLookEvent Plain;
		Plain.Scale = 1.7;
		Check(Near(ScaleFor(Plain, File, 900.0), 1.7), "no size: its own scale");
		FCastVec Centre = AreaCentre("cone", 5.0, { 0, 0, 0 }, { 0, 1000, 0 });
		Check(Near(Centre.X, 0.0) && Near(Centre.Y, 275.0), "a cone's centre is 0.55 of its range along the aim");
		Centre = AreaCentre("line", 6.0, { 0, 0, 0 }, { 1000, 0, 0 });
		Check(Near(Centre.X, 600.0), "a line's centre is 0.6 of the way");
		Centre = AreaCentre("self", 0.0, { 5, 5, 0 }, { 1000, 0, 0 });
		Check(Near(Centre.X, 5.0) && Near(Centre.Z, 10.0), "a self area is round its user");
	}

	void Shakes()
	{
		FLookEvent Shake;
		Shake.Kind = EEffectKind::Shake;
		Shake.Strength = 0.6;
		Check(Near(ShakeStrength(Shake, 0.9), 0.6), "a plain shake ignores how hard it hurt");
		Shake.Harm = EHarm::Add;
		Shake.Strength = 0.55;
		Check(Near(ShakeStrength(Shake, 0.3), 0.85) && Near(ShakeStrength(Shake, 5.0), 1.5), "add: today's area jolt, held to 1.5");
		Shake.Harm = EHarm::Scale;
		Shake.Strength = 1.0;
		Check(Near(ShakeStrength(Shake, 0.2), 0.4) && Near(ShakeStrength(Shake, 0.01), 0.12) && Near(ShakeStrength(Shake, 0.8), 0.9),
			"scale: today's single blow, a scratch barely, a third of someone properly");
		Check(Near(ShakeStrength(Shake, 0.0), 0.0), "scale with nothing hurt: no shake");
	}

	void SoundsInTimeWithTheSwing()
	{
		FLooksFile File;
		Check(ReadLooksFile(std::string(LooksHead) + R"("abilities": {"a": {"events": [
			{"moment": "swing", "effect": "sound", "sound": "/Game/Audio/Whoosh.Whoosh", "part": "release", "share": 0.4, "volume": 0.8, "pitch": 1.1},
			{"moment": "impact", "effect": "sound", "sound": "/Game/Audio/Hit.Hit", "delay": 0.05},
			{"moment": "impact", "effect": "sound", "sound": "/Game/Audio/Hit.Hit", "part": "release", "share": 0.5},
			{"moment": "swing", "effect": "sound"},
			{"moment": "swing", "effect": "sound", "sound": "/Game/Audio/Grunt.Grunt", "part": "recover", "share": 2, "volume": 9}
		]}}})", File), "a looks file with sounds reads");
		const FLook* A = File.Ability("a");
		Check(A && A->Events.size() == 4 && A->HasSound(), "four sound events kept, one without a sound left out");
		Check(Mentions(File, "events[2].part: only a swing event") && Mentions(File, "events[3].sound")
			&& Mentions(File, "events[4].share") && Mentions(File, "events[4].volume"), "what is wrong in a sound is said");
		if (A && A->Events.size() == 4)
		{
			const FLookEvent& Whoosh = A->Events[0];
			Check(Whoosh.Kind == EEffectKind::Sound && Whoosh.Sound == "/Game/Audio/Whoosh.Whoosh" && Near(Whoosh.Volume, 0.8) && Near(Whoosh.Pitch, 1.1)
				&& Whoosh.Part == EPart::Release && Near(Whoosh.Share, 0.4), "a whoosh timed to 40% of the release");
			FSwingTimes Swing;
			Swing.Windup = 0.5;
			Swing.Release = 1.0;
			Swing.Recover = 0.6;
			Check(Near(SyncSeconds(Whoosh, Swing), 0.9), "it plays after the wind-up and 40% of the release");
			Swing.Windup = 0.0;
			Swing.Release = 2.0;
			Check(Near(SyncSeconds(Whoosh, Swing), 0.8), "a longer release on another body: still 40% of it");
			Check(Near(SyncSeconds(A->Events[1], Swing), 0.05), "no part: its delay alone");
			Check(A->Events[2].Part == EPart::None, "an impact is not timed to the swing");
			Check(Near(SyncSeconds(A->Events[3], Swing), 2.0) && Near(A->Events[3].Volume, 1.0), "a share past 1 is left out: the start of the recover, at its own volume");
			const std::string Text = std::string(LooksHead) + "\"abilities\": {\"x\": " + LookJson(*A) + "}}";
			FLooksFile Again;
			Check(ReadLooksFile(Text, Again) && Again.Problems.empty() && LookJson(Again.Abilities["x"]) == LookJson(*A), "sounds write out and read back the same");
		}
		Check(File.Sounds().size() == 3 && File.Effects().empty(), "the sounds named, once each; a sound is not an effect to load");
		FLooksFile Quiet;
		ReadLooksFile(SampleLooks, Quiet);
		Check(!Quiet.Ability("frost_stalker_ice_dagger")->HasSound(), "no sound of its own: today's sounds play");
	}

	// ------------------------------------------------------- today's look

	TMSim::FAbility MakeAbility(const char* Id, const char* Name, TMSim::EEffect Effect, const char* Shape, float Range, float Aoe)
	{
		TMSim::FAbility Ability;
		Ability.Id = Id;
		Ability.Name = Name;
		Ability.Effect = Effect;
		Ability.Shape = Shape;
		Ability.MaxRange = Range;
		Ability.Aoe = Aoe;
		Ability.Target = Effect == TMSim::EEffect::Damage ? TMSim::ETargetSide::Enemy : TMSim::ETargetSide::Ally;
		return Ability;
	}

	const FLookEvent* Find(const FLook& Look, EMoment Moment, EEffectKind Kind, EWhen When = EWhen::Always, int Skip = 0)
	{
		for (const FLookEvent& Event : Look.Events)
		{
			if (Event.Moment == Moment && Event.Kind == Kind && Event.When == When && Skip-- == 0)
			{
				return &Event;
			}
		}
		return nullptr;
	}

	void TodaysLook()
	{
		Check(LegacyFlavourCount() == 11 && std::string(LegacyFlavour(LegacyFlavourCount() - 1).Name) == "steel"
			&& std::string(LegacyFlavour(-5).Name) == "fire", "the eleven flavours, steel last, held to the table");

		// "Flame Lance": fire by its name, a bolt thrown from afar.
		TMSim::FAbility Lance = MakeAbility("pyro_flame_lance", "Flame Lance", TMSim::EEffect::Damage, "unit", 5.0f, 0.0f);
		Lance.Scale = TMSim::EScale::Mag;
		const FLegacyKind Kind = LegacyKindOf(Lance);
		Check(std::string(LegacyFlavour(Kind.Flavour).Name) == "fire" && Kind.bMagic && Kind.bRanged && !Kind.bArea, "Flame Lance reads as fire, magic, ranged");
		Check(LegacyShotOf(Lance, "bolt") == ELegacyShot::Orb && LegacyShotOf(Lance, "shoot") == ELegacyShot::Arrow, "a bolt throws an orb, a shot an arrow");
		const FLook Look = LegacyLook(Lance, 0);
		Check(Look.Strip == EStrip::All, "today's look comes stripped of itself, to stand in for it");
		const FLookEvent* Flare = Find(Look, EMoment::Swing, EEffectKind::Particle);
		Check(Flare && Flare->Effect == LegacyFlavour(Kind.Flavour).Cast.Path && Near(Flare->Size, 130.0)
			&& Flare->Anchor == EAnchor::Ground && Near(Flare->Offset.X, 45.0) && Near(Flare->Offset.Z, 115.0),
			"the flare in its hands as the swing begins: 130 cm, today's hands");
		const FLookEvent* Glow = Find(Look, EMoment::Swing, EEffectKind::Light);
		Check(Glow && Near(Glow->Brightness, 7000.0) && Near(Glow->Radius, 320.0) && Near(Glow->Duration, 0.5), "and its light");
		Check(Near(LegacyLook(Lance, 3).Events[0].Size, 200.0), "an ultimate's flare is bigger");
		const FLookEvent* Shot = Find(Look, EMoment::Projectile, EEffectKind::Particle);
		Check(Shot && Near(Shot->Size, 110.0) && Look.HasProjectileEffect(), "what flies: the fire shot, 110 cm");
		const FLookEvent* ShotGlow = Find(Look, EMoment::Projectile, EEffectKind::Light);
		Check(ShotGlow && Near(ShotGlow->Brightness, 8000.0) && Near(ShotGlow->Radius, 260.0) && Near(ShotGlow->Duration, 0.0),
			"the orb's own glow, for as long as it flies");
		const FLookEvent* Hit = Find(Look, EMoment::Impact, EEffectKind::Particle, EWhen::Normal);
		const FLookEvent* CritHit = Find(Look, EMoment::Impact, EEffectKind::Particle, EWhen::Critical);
		Check(Hit && CritHit && Near(Hit->Size, 230.0) && Near(CritHit->Size, 322.0) && Hit->On == EOn::Struck,
			"its hit: 230 cm, 1.4 times on a critical");
		const FLookEvent* Jolt = Find(Look, EMoment::Area, EEffectKind::Shake, EWhen::Normal);
		const FLookEvent* CritJolt = Find(Look, EMoment::Area, EEffectKind::Shake, EWhen::Critical);
		Check(Jolt && CritJolt && Jolt->Harm == EHarm::Scale && Near(ShakeStrength(*Jolt, 0.2), 0.4) && Near(ShakeStrength(*CritJolt, 0.2), 0.6),
			"a single blow jolts as hard as it hurt, half again on a critical");

		// A fire circle: the ground bursts, sized as today (at least 220 cm, at most 1400).
		const TMSim::FAbility Rain = MakeAbility("pyro_ember_rain", "Ember Rain", TMSim::EEffect::Damage, "circle", 5.0f, 0.5f);
		const FLook Burst = LegacyLook(Rain, 1);
		const FLookEvent* Ground = Find(Burst, EMoment::Area, EEffectKind::Particle);
		const FLookEvent* Ground2 = Find(Burst, EMoment::Area, EEffectKind::Particle, EWhen::Always, 1);
		Check(Ground && Ground->bFitArea && Near(WantedSize(*Ground, AreaAcross("circle", 0.5, 5.0)), 220.0)
			&& Near(WantedSize(*Ground, AreaAcross("circle", 3.0, 5.0)), 600.0) && Near(WantedSize(*Ground, AreaAcross("circle", 9.0, 5.0)), 1400.0),
			"the burst fits the circle, at least 220 cm and at most 1400");
		Check(Ground2 && Near(WantedSize(*Ground2, 600.0), 480.0) && Near(WantedSize(*Ground2, 3000.0), 1120.0), "the second layer is 0.8 of it");
		const FLookEvent* Flash = Find(Burst, EMoment::Area, EEffectKind::Light);
		Check(Flash && Near(LightRadius(*Flash, 600.0), 780.0) && Near(Flash->Brightness, 30000.0) && Near(Flash->Offset.Z, 150.0),
			"its flash: 1.3 times the area, 150 cm up");
		const FLookEvent* AreaJolt = Find(Burst, EMoment::Area, EEffectKind::Shake);
		Check(AreaJolt && AreaJolt->Harm == EHarm::Add && Near(ShakeStrength(*AreaJolt, 0.25), 0.8), "an area's jolt: 0.55 and how hard it hurt");
		const FLookEvent* AreaHit = Find(Burst, EMoment::Impact, EEffectKind::Particle, EWhen::Normal);
		Check(AreaHit && Near(AreaHit->Size, 230.0 * 0.75), "hits in an area are three quarters the size");

		// A heal on one ally; a buff on oneself; a plain sword.
		const TMSim::FAbility Mend = MakeAbility("cleric_mend", "Mend", TMSim::EEffect::Heal, "unit", 4.0f, 0.0f);
		const FLook Healing = LegacyLook(Mend, 0);
		const FLookEvent* HealHit = Find(Healing, EMoment::Impact, EEffectKind::Particle);
		Check(HealHit && HealHit->Effect == LegacyCommonFx().Heal.Path && Near(HealHit->Size, 190.0), "a heal plays the heal on whoever it reaches");
		Check(Find(Healing, EMoment::Impact, EEffectKind::Particle, EWhen::Critical) == nullptr, "a heal's look is the same critical or not");
		Check(Find(Healing, EMoment::Projectile, EEffectKind::Particle) != nullptr, "a holy heal from afar flies there as an orb, as today");
		const TMSim::FAbility Near1 = MakeAbility("cleric_touch", "Touch", TMSim::EEffect::Heal, "unit", 1.0f, 0.0f);
		Check(Find(LegacyLook(Near1, 0), EMoment::Projectile, EEffectKind::Light) == nullptr, "a heal at arm's length throws nothing");
		TMSim::FAbility Guard = MakeAbility("knight_guard", "Guard", TMSim::EEffect::Support, "self", 0.0f, 0.0f);
		const FLook Buffing = LegacyLook(Guard, 1);
		const FLookEvent* Aura = Find(Buffing, EMoment::Swing, EEffectKind::Particle);
		Check(Aura && Aura->Anchor == EAnchor::Ground && Near(Aura->Offset.Z, 60.0) && Near(Aura->Size, 200.0), "a buff on oneself: an aura round it as it begins");
		const TMSim::FAbility Cut = MakeAbility("squire_cut", "Cut", TMSim::EEffect::Damage, "unit", 1.0f, 0.0f);
		const FLook Steel = LegacyLook(Cut, 0);
		Check(Find(Steel, EMoment::Swing, EEffectKind::Particle) == nullptr, "a plain blade gathers nothing in its hands");
		const FLookEvent* Slash = Find(Steel, EMoment::Impact, EEffectKind::Particle, EWhen::Normal);
		Check(Slash && Slash->Effect == LegacyFlavour(LegacyFlavourCount() - 1).Hit.Path && Near(Slash->Size, 200.0), "it hits as steel");
		TMSim::FAbility Smash = MakeAbility("brute_smash", "Hammer Smash", TMSim::EEffect::Damage, "unit", 1.0f, 0.0f);
		const FLook Blunt = LegacyLook(Smash, 0);
		const FLookEvent* Low = Find(Blunt, EMoment::Impact, EEffectKind::Particle, EWhen::Normal, 1);
		Check(Low && Low->Effect == LegacyCommonFx().Blunt2.Path && Near(Low->Offset.Z, -60.0), "a hammer crushes, the second layer 60 cm lower");

		// A raise, and the class file's own effect.
		TMSim::FAbility Raise = MakeAbility("cleric_raise", "Raise", TMSim::EEffect::Revive, "unit", 3.0f, 0.0f);
		Raise.Target = TMSim::ETargetSide::KoAlly;
		Raise.VfxSystem = "/Game/FX/P_Own.P_Own";
		Raise.VfxAt = "targets";
		Raise.VfxScale = 1.5f;
		const FLook Raising = LegacyLook(Raise, 2);
		bool bRevived = false;
		bool bOwn = false;
		for (const FLookEvent& Event : Raising.Events)
		{
			bRevived = bRevived || (Event.On == EOn::Revived && Event.Effect == LegacyCommonFx().Revive.Path && Near(Event.Size, 260.0));
			bOwn = bOwn || (Event.Effect == "/Game/FX/P_Own.P_Own" && Event.Moment == EMoment::Impact && Event.On == EOn::Any
				&& Near(Event.Scale, 1.5) && Near(Event.Offset.Z, 90.0));
		}
		Check(bRevived, "the one raised gets today's revive");
		Check(bOwn, "the class file's effect on each target, at its own scale");

		// Every built-in ability's look writes out and reads back cleanly.
		int Checked = 0;
		for (const TMSim::FJobDef* Job : TMSim::AllJobs())
		{
			for (int Slot = 0; Slot < 4; ++Slot)
			{
				const TMSim::FAbility* Ability = TMSim::JobAbility(Job->Id, Slot);
				if (!Ability)
				{
					continue;
				}
				FLooksFile Again;
				const std::string Text = std::string(LooksHead) + "\"abilities\": {\"x\": " + LookJson(LegacyLook(*Ability, Slot))
					+ "}, \"footprints\": " + LegacyFootprintsJson() + "}";
				if (!ReadLooksFile(Text, Again) || !Again.Problems.empty())
				{
					Fail(Ability->Id + "'s look today does not read back: " + (Again.Problems.empty() ? std::string() : Again.Problems[0]));
				}
				++Checked;
			}
		}
		Check(Checked >= 24, "the built-in classes' abilities were all written out");
	}

	std::string ReadAll(const std::filesystem::path& Path)
	{
		std::ifstream In(Path, std::ios::binary);
		std::stringstream Out;
		Out << In.rdbuf();
		return Out.str();
	}
}

int main(int Argc, char** Argv)
{
	ReadsTheSample();
	SetsAsideWhatIsWrong();
	RefusesWhatIsNotItsOwn();
	ContactMaths();
	ReadsTheSampleLooks();
	SetsAsideWhatIsWrongInLooks();
	RefusesLooksThatAreNotItsOwn();
	MomentsAndOrder();
	LooksRoundTrip();
	Anchors();
	FitsTheArea();
	Shakes();
	SoundsInTimeWithTheSwing();
	TodaysLook();

	if (Argc > 1)
	{
		const std::filesystem::path File = std::filesystem::path(Argv[1]) / "AbilityAnimation.json";
		if (std::filesystem::exists(File))
		{
			FAnimationFile Published;
			Check(ReadAnimationFile(ReadAll(File), Published), "the published AbilityAnimation.json reads");
			for (const std::string& Problem : Published.Problems)
			{
				Fail("the published AbilityAnimation.json: " + Problem);
			}
			std::printf("published: %d abilities with picks\n", static_cast<int>(Published.Abilities.size()));
		}
		else
		{
			std::printf("nothing published yet: every ability moves as the character map says\n");
		}
		const std::filesystem::path LooksPath = std::filesystem::path(Argv[1]) / "AbilityLooks.json";
		if (std::filesystem::exists(LooksPath))
		{
			FLooksFile Looks;
			Check(ReadLooksFile(ReadAll(LooksPath), Looks), "the published AbilityLooks.json reads");
			for (const std::string& Problem : Looks.Problems)
			{
				Fail("the published AbilityLooks.json: " + Problem);
			}
			std::printf("published: %d abilities with looks\n", static_cast<int>(Looks.Abilities.size()));
		}
		else
		{
			std::printf("no looks published yet: every ability looks as it did\n");
		}
	}

	if (Failures > 0)
	{
		std::printf("%d FAILED\n", Failures);
		return 1;
	}
	std::printf("ALL TESTS PASSED\n");
	return 0;
}
