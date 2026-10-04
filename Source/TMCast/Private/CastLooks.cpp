#include "CastLooks.h"

#include "SimJson.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

namespace TMCast
{
	namespace
	{
		constexpr const char* GMomentNames[] =
		{
			"castStart", "casting", "swing", "release", "projectile", "impact", "area",
			"targetStatus", "allyStatus", "selfStatus", "tick", "expire", "summon",
			"statusGain", "statusActive", "statusTick", "statusEnd", "reaction",
		};
		static_assert(sizeof(GMomentNames) / sizeof(GMomentNames[0]) == static_cast<size_t>(EMoment::Count), "a name per moment");

		constexpr const char* GAnchorNames[] = { "ground", "body", "head", "hand", "weapon", "center", "aim" };
		constexpr const char* GStripNames[] = { "none", "legacy", "all" };
		constexpr const char* GWhenNames[] = { "always", "critical", "normal" };
		constexpr const char* GOnNames[] = { "struck", "evaded", "revived", "any" };
		constexpr const char* GSideNames[] = { "any", "enemy", "ally" };
		constexpr const char* GHarmNames[] = { "none", "scale", "add" };
		constexpr const char* GPartNames[] = { "none", "windup", "release", "recover" };

		template <typename TEnum, size_t N>
		bool Named(const char* const (&Names)[N], const std::string& Name, TEnum& Out)
		{
			for (size_t i = 0; i < N; ++i)
			{
				if (Name == Names[i])
				{
					Out = static_cast<TEnum>(i);
					return true;
				}
			}
			return false;
		}

		/** Which table a look is in decides which moments it may name. */
		enum class ETable : unsigned char { Ability, Status, Reaction };

		bool MomentFits(EMoment Moment, ETable Table)
		{
			const int M = static_cast<int>(Moment);
			switch (Table)
			{
			case ETable::Ability: return M <= static_cast<int>(EMoment::Summon);
			case ETable::Status: return M >= static_cast<int>(EMoment::StatusGain) && M <= static_cast<int>(EMoment::StatusEnd);
			case ETable::Reaction: return Moment == EMoment::Reaction;
			}
			return false;
		}

		/** An object path as Unreal writes one: "/Game/Pack/P_X.P_X". */
		bool IsEffectPath(const std::string& Path)
		{
			if (Path.size() < 4 || Path[0] != '/' || Path.find('.') == std::string::npos)
			{
				return false;
			}
			for (const char C : Path)
			{
				if (C == ' ' || C == '"' || C == '\\' || static_cast<unsigned char>(C) < 32)
				{
					return false;
				}
			}
			return true;
		}

		/** A number in [Low, High], or said and left as it was. */
		void ReadNumber(const TMSim::FJson& Json, const std::string& Where, double Low, double High, double& Out,
			std::vector<std::string>& Problems)
		{
			if (!Json.IsNumber() || !std::isfinite(Json.Number) || Json.Number < Low || Json.Number > High)
			{
				char Range[96];
				std::snprintf(Range, sizeof(Range), "%g to %g", Low, High);
				Problems.push_back(Where + ": should be a number, " + Range + "; left out");
				return;
			}
			Out = Json.Number;
		}

		bool ReadTriple(const TMSim::FJson& Json, const std::string& Where, double Low, double High, FCastVec& Out,
			std::vector<std::string>& Problems)
		{
			if (!Json.IsArray() || Json.Array.size() != 3)
			{
				Problems.push_back(Where + ": should be three numbers; left out");
				return false;
			}
			double Each[3];
			for (int i = 0; i < 3; ++i)
			{
				const TMSim::FJson& N = Json.Array[static_cast<size_t>(i)];
				if (!N.IsNumber() || !std::isfinite(N.Number) || N.Number < Low || N.Number > High)
				{
					char Range[96];
					std::snprintf(Range, sizeof(Range), "%g to %g", Low, High);
					Problems.push_back(Where + ": should be three numbers, each " + Range + "; left out");
					return false;
				}
				Each[i] = N.Number;
			}
			Out = { Each[0], Each[1], Each[2] };
			return true;
		}

		template <typename TEnum, size_t N>
		void ReadWord(const TMSim::FJson& Json, const std::string& Where, const char* const (&Names)[N], TEnum& Out,
			std::vector<std::string>& Problems)
		{
			TEnum Value = Out;
			if (!Json.IsString() || !Named(Names, Json.String, Value))
			{
				std::string Words;
				for (size_t i = 0; i < N; ++i)
				{
					Words += (i ? ", " : "") + std::string(Names[i]);
				}
				Problems.push_back(Where + ": should be one of " + Words + "; left out");
				return;
			}
			Out = Value;
		}

		/** One event, or false (and said why) when it cannot play at all. */
		bool ReadEvent(const TMSim::FJson& Json, const std::string& Where, ETable Table, FLookEvent& Out,
			std::vector<std::string>& Problems)
		{
			if (!Json.IsObject())
			{
				Problems.push_back(Where + ": an event should be an object; left out");
				return false;
			}
			const TMSim::FJson* Moment = Json.Find("moment");
			if (!Moment || !Moment->IsString() || !MomentNamed(Moment->String, Out.Moment))
			{
				Problems.push_back(Where + ": no moment this version knows" + (Moment && Moment->IsString() ? " ('" + Moment->String + "')" : std::string()) + "; left out");
				return false;
			}
			if (!MomentFits(Out.Moment, Table))
			{
				Problems.push_back(Where + ": " + Moment->String + " is not a moment of this table; left out");
				return false;
			}
			const TMSim::FJson* Effect = Json.Find("effect");
			if (!Effect || !Effect->IsString())
			{
				Problems.push_back(Where + ": no effect; left out");
				return false;
			}
			if (Effect->String == "light")
			{
				Out.Kind = EEffectKind::Light;
			}
			else if (Effect->String == "shake")
			{
				Out.Kind = EEffectKind::Shake;
			}
			else if (Effect->String == "sound")
			{
				Out.Kind = EEffectKind::Sound;
				const TMSim::FJson* Sound = Json.Find("sound");
				if (!Sound || !Sound->IsString() || !IsEffectPath(Sound->String))
				{
					Problems.push_back(Where + ".sound: not a sound's object path; left out");
					return false;
				}
				Out.Sound = Sound->String;
			}
			else if (IsEffectPath(Effect->String))
			{
				Out.Kind = EEffectKind::Particle;
				Out.Effect = Effect->String;
			}
			else
			{
				Problems.push_back(Where + ".effect: not an effect's object path, \"light\", \"shake\" or \"sound\"; left out");
				return false;
			}
			Out.Anchor = Out.Moment == EMoment::Area ? EAnchor::Center : EAnchor::Body;
			Out.Duration = Out.Kind == EEffectKind::Light ? 0.5 : 0.0;
			for (const std::pair<std::string, TMSim::FJson>& Member : Json.Object)
			{
				const std::string& Key = Member.first;
				const TMSim::FJson& Value = Member.second;
				const std::string At = Where + "." + Key;
				if (Key == "moment" || Key == "effect") { continue; }
				if (Key == "anchor") { ReadWord(Value, At, GAnchorNames, Out.Anchor, Problems); }
				else if (Key == "follow")
				{
					if (Value.Type == TMSim::FJson::EType::Bool) { Out.bFollow = Value.Bool; }
					else { Problems.push_back(At + ": should be true or false; left out"); }
				}
				else if (Key == "offset") { ReadTriple(Value, At, -5000.0, 5000.0, Out.Offset, Problems); }
				else if (Key == "rotation") { ReadTriple(Value, At, -360.0, 360.0, Out.Rotation, Problems); }
				else if (Key == "size") { ReadNumber(Value, At, 0.0, 10000.0, Out.Size, Problems); }
				else if (Key == "fit")
				{
					if (Value.IsString() && Value.String == "area") { Out.bFitArea = true; }
					else if (Value.IsString() && Value.String == "none") { Out.bFitArea = false; }
					else { Problems.push_back(At + ": should be \"area\" or \"none\"; left out"); }
				}
				else if (Key == "fitScale") { ReadNumber(Value, At, 0.01, 10.0, Out.FitScale, Problems); }
				else if (Key == "minSize") { ReadNumber(Value, At, 0.0, 10000.0, Out.MinSize, Problems); }
				else if (Key == "maxSize") { ReadNumber(Value, At, 0.0, 10000.0, Out.MaxSize, Problems); }
				else if (Key == "scale") { ReadNumber(Value, At, 0.01, 20.0, Out.Scale, Problems); }
				else if (Key == "delay") { ReadNumber(Value, At, 0.0, 10.0, Out.Delay, Problems); }
				else if (Key == "duration") { ReadNumber(Value, At, 0.0, 30.0, Out.Duration, Problems); }
				else if (Key == "repeat")
				{
					ReadNumber(Value, At, 0.0, 30.0, Out.Repeat, Problems);
					if (Out.Repeat > 0.0 && Out.Repeat < 0.1)
					{
						Problems.push_back(At + ": less than a tenth of a second; held to 0.1");
						Out.Repeat = 0.1;
					}
				}
				else if (Key == "when") { ReadWord(Value, At, GWhenNames, Out.When, Problems); }
				else if (Key == "on") { ReadWord(Value, At, GOnNames, Out.On, Problems); }
				else if (Key == "side") { ReadWord(Value, At, GSideNames, Out.Side, Problems); }
				else if (Key == "tint") { ReadTriple(Value, At, 0.0, 4.0, Out.Tint, Problems); }
				else if (Key == "brightness") { ReadNumber(Value, At, 0.0, 200000.0, Out.Brightness, Problems); }
				else if (Key == "radius") { ReadNumber(Value, At, 1.0, 5000.0, Out.Radius, Problems); }
				else if (Key == "strength") { ReadNumber(Value, At, 0.0, 3.0, Out.Strength, Problems); }
				else if (Key == "harm") { ReadWord(Value, At, GHarmNames, Out.Harm, Problems); }
				else if (Key == "sound") { continue; }  // read with the effect
				else if (Key == "volume") { ReadNumber(Value, At, 0.0, 4.0, Out.Volume, Problems); }
				else if (Key == "pitch") { ReadNumber(Value, At, 0.25, 4.0, Out.Pitch, Problems); }
				else if (Key == "part") { ReadWord(Value, At, GPartNames, Out.Part, Problems); }
				else if (Key == "share") { ReadNumber(Value, At, 0.0, 1.0, Out.Share, Problems); }
				else if (Key == "note") { continue; }  // the designer's own words, for the creator
				else
				{
					// A newer creator may write more than this game reads: said, not refused.
					Problems.push_back(At + ": not read by this version");
				}
			}
			if (Out.Part != EPart::None && Out.Moment != EMoment::Swing)
			{
				Problems.push_back(Where + ".part: only a swing event is timed to a part of the swing; left out");
				Out.Part = EPart::None;
				Out.Share = 0.0;
			}
			if (Out.MaxSize > 0.0 && Out.MinSize > Out.MaxSize)
			{
				Problems.push_back(Where + ": minSize is above maxSize; both left out");
				Out.MinSize = Out.MaxSize = 0.0;
			}
			return true;
		}

		void ReadTable(const TMSim::FJson* Table, const char* Name, ETable Kind, std::map<std::string, FLook>& Out,
			std::vector<std::string>& Problems)
		{
			if (!Table)
			{
				return;
			}
			if (!Table->IsObject())
			{
				Problems.push_back(std::string(Name) + " should be an object; left out");
				return;
			}
			for (const std::pair<std::string, TMSim::FJson>& Entry : Table->Object)
			{
				const std::string Where = std::string(Name) + "." + Entry.first;
				if (Entry.first.empty() || !Entry.second.IsObject())
				{
					Problems.push_back(Where + ": should be an object with events; left out");
					continue;
				}
				FLook Look;
				bool bAny = false;
				for (const std::pair<std::string, TMSim::FJson>& Member : Entry.second.Object)
				{
					if (Member.first == "strip" && Kind == ETable::Ability)
					{
						ReadWord(Member.second, Where + ".strip", GStripNames, Look.Strip, Problems);
						bAny = true;
					}
					else if (Member.first == "events")
					{
						if (!Member.second.IsArray())
						{
							Problems.push_back(Where + ".events: should be a list; left out");
							continue;
						}
						for (size_t i = 0; i < Member.second.Array.size(); ++i)
						{
							FLookEvent Event;
							if (ReadEvent(Member.second.Array[i], Where + ".events[" + std::to_string(i) + "]", Kind, Event, Problems))
							{
								Look.Events.push_back(Event);
								bAny = true;
							}
						}
					}
					else if (Member.first == "note")
					{
						continue;
					}
					else
					{
						Problems.push_back(Where + "." + Member.first + ": not read by this version");
					}
				}
				if (bAny && (Look.Strip != EStrip::None || !Look.Events.empty()))
				{
					Out[Entry.first] = Look;
				}
			}
		}

		std::string Number(double Value)
		{
			char Buffer[64];
			std::snprintf(Buffer, sizeof(Buffer), "%.6g", Value);
			std::string Out = Buffer;
			// Whatever the machine's locale, a point.
			std::replace(Out.begin(), Out.end(), ',', '.');
			return Out;
		}

		std::string Quote(const std::string& Text)
		{
			std::string Out = "\"";
			for (const char C : Text)
			{
				if (C == '"' || C == '\\') { Out += '\\'; Out += C; }
				else if (static_cast<unsigned char>(C) < 32) { char U[8]; std::snprintf(U, sizeof(U), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(C))); Out += U; }
				else { Out += C; }
			}
			return Out + "\"";
		}

		std::string Triple(const FCastVec& V)
		{
			return "[" + Number(V.X) + ", " + Number(V.Y) + ", " + Number(V.Z) + "]";
		}

		double Clamp(double Value, double Low, double High)
		{
			return std::min(High, std::max(Low, Value));
		}
	}

	const char* MomentName(EMoment Moment)
	{
		const size_t Index = static_cast<size_t>(Moment);
		return Index < static_cast<size_t>(EMoment::Count) ? GMomentNames[Index] : "";
	}

	bool MomentNamed(const std::string& Name, EMoment& Out)
	{
		return Named(GMomentNames, Name, Out);
	}

	bool IsLasting(EMoment Moment)
	{
		switch (Moment)
		{
		case EMoment::Casting:
		case EMoment::Projectile:
		case EMoment::TargetStatus:
		case EMoment::AllyStatus:
		case EMoment::SelfStatus:
		case EMoment::Summon:
		case EMoment::StatusActive:
			return true;
		default:
			return false;
		}
	}

	const char* AnchorName(EAnchor Anchor) { return GAnchorNames[static_cast<size_t>(Anchor)]; }
	const char* StripName(EStrip Strip) { return GStripNames[static_cast<size_t>(Strip)]; }

	bool FLook::Has(EMoment Moment) const
	{
		for (const FLookEvent& Event : Events)
		{
			if (Event.Moment == Moment)
			{
				return true;
			}
		}
		return false;
	}

	bool FLook::HasSound() const
	{
		for (const FLookEvent& Event : Events)
		{
			if (Event.Kind == EEffectKind::Sound)
			{
				return true;
			}
		}
		return false;
	}

	std::vector<std::string> FLooksFile::Sounds() const
	{
		std::set<std::string> All;
		for (const std::map<std::string, FLook>* Table : { &Abilities, &Statuses, &Reactions })
		{
			for (const auto& Entry : *Table)
			{
				for (const FLookEvent& Event : Entry.second.Events)
				{
					if (Event.Kind == EEffectKind::Sound && !Event.Sound.empty())
					{
						All.insert(Event.Sound);
					}
				}
			}
		}
		return std::vector<std::string>(All.begin(), All.end());
	}

	double SyncSeconds(const FLookEvent& Event, const FSwingTimes& Swing)
	{
		const double Share = std::min(1.0, std::max(0.0, Event.Share));
		double At = 0.0;
		switch (Event.Part)
		{
		case EPart::Windup: At = Share * std::max(0.0, Swing.Windup); break;
		case EPart::Release: At = std::max(0.0, Swing.Windup) + Share * std::max(0.0, Swing.Release); break;
		case EPart::Recover: At = std::max(0.0, Swing.Windup) + std::max(0.0, Swing.Release) + Share * std::max(0.0, Swing.Recover); break;
		case EPart::None: break;
		}
		return At + std::max(0.0, Event.Delay);
	}

	bool FLook::HasProjectileEffect() const
	{
		for (const FLookEvent& Event : Events)
		{
			if (Event.Moment == EMoment::Projectile && Event.Kind == EEffectKind::Particle)
			{
				return true;
			}
		}
		return false;
	}

	const FLook* FLooksFile::Ability(const std::string& Id) const
	{
		const auto Found = Abilities.find(Id);
		return Found == Abilities.end() ? nullptr : &Found->second;
	}

	const FLook* FLooksFile::Status(const std::string& Id) const
	{
		const auto Found = Statuses.find(Id);
		return Found == Statuses.end() ? nullptr : &Found->second;
	}

	const FLook* FLooksFile::Reaction(const std::string& Id) const
	{
		const auto Found = Reactions.find(Id);
		return Found == Reactions.end() ? nullptr : &Found->second;
	}

	EStrip FLooksFile::StripOf(const std::string& AbilityId) const
	{
		const FLook* Look = Ability(AbilityId);
		return Look ? Look->Strip : EStrip::None;
	}

	std::vector<std::string> FLooksFile::Effects() const
	{
		std::set<std::string> All;
		for (const std::map<std::string, FLook>* Table : { &Abilities, &Statuses, &Reactions })
		{
			for (const auto& Entry : *Table)
			{
				for (const FLookEvent& Event : Entry.second.Events)
				{
					if (Event.Kind == EEffectKind::Particle && !Event.Effect.empty())
					{
						All.insert(Event.Effect);
					}
				}
			}
		}
		return std::vector<std::string>(All.begin(), All.end());
	}

	bool ReadLooksFile(const std::string& Text, FLooksFile& Out)
	{
		Out = FLooksFile();
		TMSim::FJson Root;
		const std::string Error = TMSim::ParseJson(Text, Root);
		if (!Error.empty())
		{
			Out.Problems.push_back("not JSON: " + Error);
			return false;
		}
		const TMSim::FJson* Format = Root.IsObject() ? Root.Find("format") : nullptr;
		if (!Format || !Format->IsString() || Format->String != LooksFormat())
		{
			Out.Problems.push_back(std::string("not an ability looks file (format should be \"") + LooksFormat() + "\")");
			return false;
		}
		const TMSim::FJson* Schema = Root.Find("schemaVersion");
		if (!Schema || !Schema->IsNumber() || Schema->Number < 1.0)
		{
			Out.Problems.push_back("no schemaVersion");
			return false;
		}
		if (Schema->Number > LooksSchema)
		{
			// Written by a newer creator: guessing could play the wrong look, so
			// none is played and every ability looks as it did before.
			Out.Problems.push_back("schemaVersion " + std::to_string(static_cast<int>(Schema->Number))
				+ " is newer than this game reads (" + std::to_string(LooksSchema) + ")");
			return false;
		}
		for (const std::pair<std::string, TMSim::FJson>& Member : Root.Object)
		{
			const std::string& Key = Member.first;
			if (Key == "format" || Key == "schemaVersion") { continue; }
			if (Key == "abilities") { ReadTable(&Member.second, "abilities", ETable::Ability, Out.Abilities, Out.Problems); }
			else if (Key == "statuses") { ReadTable(&Member.second, "statuses", ETable::Status, Out.Statuses, Out.Problems); }
			else if (Key == "reactions") { ReadTable(&Member.second, "reactions", ETable::Reaction, Out.Reactions, Out.Problems); }
			else if (Key == "footprints")
			{
				if (!Member.second.IsObject())
				{
					Out.Problems.push_back("footprints should be an object; left out");
					continue;
				}
				for (const std::pair<std::string, TMSim::FJson>& Print : Member.second.Object)
				{
					double Cm = 0.0;
					const size_t Before = Out.Problems.size();
					ReadNumber(Print.second, "footprints." + Print.first, 1.0, 20000.0, Cm, Out.Problems);
					if (Out.Problems.size() == Before)
					{
						Out.Footprints[Print.first] = Cm;
					}
				}
			}
			else
			{
				Out.Problems.push_back(Key + ": not read by this version");
			}
		}
		return true;
	}

	std::string EventJson(const FLookEvent& Event)
	{
		const FLookEvent Usual;
		std::string Out = "{\"moment\": " + Quote(MomentName(Event.Moment)) + ", \"effect\": "
			+ Quote(Event.Kind == EEffectKind::Light ? "light" : Event.Kind == EEffectKind::Shake ? "shake"
				: Event.Kind == EEffectKind::Sound ? "sound" : Event.Effect);
		if (Event.Kind == EEffectKind::Sound) { Out += ", \"sound\": " + Quote(Event.Sound); }
		const EAnchor UsualAnchor = Event.Moment == EMoment::Area ? EAnchor::Center : EAnchor::Body;
		if (Event.Anchor != UsualAnchor) { Out += ", \"anchor\": " + Quote(AnchorName(Event.Anchor)); }
		if (Event.bFollow) { Out += ", \"follow\": true"; }
		if (Event.Offset.X != 0.0 || Event.Offset.Y != 0.0 || Event.Offset.Z != 0.0) { Out += ", \"offset\": " + Triple(Event.Offset); }
		if (Event.Rotation.X != 0.0 || Event.Rotation.Y != 0.0 || Event.Rotation.Z != 0.0) { Out += ", \"rotation\": " + Triple(Event.Rotation); }
		if (Event.Size > 0.0) { Out += ", \"size\": " + Number(Event.Size); }
		if (Event.bFitArea) { Out += ", \"fit\": \"area\""; }
		if (Event.FitScale != Usual.FitScale) { Out += ", \"fitScale\": " + Number(Event.FitScale); }
		if (Event.MinSize > 0.0) { Out += ", \"minSize\": " + Number(Event.MinSize); }
		if (Event.MaxSize > 0.0) { Out += ", \"maxSize\": " + Number(Event.MaxSize); }
		if (Event.Scale != Usual.Scale) { Out += ", \"scale\": " + Number(Event.Scale); }
		if (Event.Delay > 0.0) { Out += ", \"delay\": " + Number(Event.Delay); }
		const double UsualDuration = Event.Kind == EEffectKind::Light ? 0.5 : 0.0;
		if (Event.Duration != UsualDuration) { Out += ", \"duration\": " + Number(Event.Duration); }
		if (Event.Repeat > 0.0) { Out += ", \"repeat\": " + Number(Event.Repeat); }
		if (Event.When != EWhen::Always) { Out += ", \"when\": " + Quote(GWhenNames[static_cast<size_t>(Event.When)]); }
		if (Event.On != EOn::Struck) { Out += ", \"on\": " + Quote(GOnNames[static_cast<size_t>(Event.On)]); }
		if (Event.Side != ESide::Any) { Out += ", \"side\": " + Quote(GSideNames[static_cast<size_t>(Event.Side)]); }
		if (Event.Kind == EEffectKind::Light)
		{
			Out += ", \"tint\": " + Triple(Event.Tint) + ", \"brightness\": " + Number(Event.Brightness);
			if (!Event.bFitArea) { Out += ", \"radius\": " + Number(Event.Radius); }
		}
		if (Event.Kind == EEffectKind::Sound)
		{
			if (Event.Volume != Usual.Volume) { Out += ", \"volume\": " + Number(Event.Volume); }
			if (Event.Pitch != Usual.Pitch) { Out += ", \"pitch\": " + Number(Event.Pitch); }
		}
		if (Event.Part != EPart::None)
		{
			Out += ", \"part\": " + Quote(GPartNames[static_cast<size_t>(Event.Part)]) + ", \"share\": " + Number(Event.Share);
		}
		if (Event.Kind == EEffectKind::Shake)
		{
			Out += ", \"strength\": " + Number(Event.Strength);
			if (Event.Harm != EHarm::None) { Out += ", \"harm\": " + Quote(GHarmNames[static_cast<size_t>(Event.Harm)]); }
		}
		return Out + "}";
	}

	std::string LookJson(const FLook& Look)
	{
		std::string Out = "{\"strip\": " + Quote(StripName(Look.Strip)) + ", \"events\": [";
		for (size_t i = 0; i < Look.Events.size(); ++i)
		{
			Out += (i ? ", " : "") + EventJson(Look.Events[i]);
		}
		return Out + "]}";
	}

	bool Plays(const FLookEvent& Event, const FMomentContext& Context)
	{
		if ((Event.When == EWhen::Critical && !Context.bCritical) || (Event.When == EWhen::Normal && Context.bCritical))
		{
			return false;
		}
		if (Event.Moment == EMoment::Impact)
		{
			if (Event.On != EOn::Any && Event.On != Context.Result)
			{
				return false;
			}
			if ((Event.Side == ESide::Enemy && Context.bAlly) || (Event.Side == ESide::Ally && !Context.bAlly))
			{
				return false;
			}
		}
		return true;
	}

	std::vector<const FLookEvent*> EventsFor(const FLook& Look, EMoment Moment, const FMomentContext& Context)
	{
		std::vector<const FLookEvent*> Out;
		for (const FLookEvent& Event : Look.Events)
		{
			if (Event.Moment == Moment && Plays(Event, Context))
			{
				Out.push_back(&Event);
			}
		}
		return Out;
	}

	double AreaAcross(const std::string& Shape, double Aoe, double MaxRange)
	{
		if (Shape == "cone")
		{
			return std::max(0.0, MaxRange) * 100.0;
		}
		if (Shape == "circle" || Shape == "self" || Shape == "point" || Shape == "line" || Shape == "vector")
		{
			return 2.0 * std::max(0.0, Aoe) * 100.0;
		}
		return 0.0;
	}

	FCastVec AreaCentre(const std::string& Shape, double MaxRange, const FCastVec& From, const FCastVec& Aim)
	{
		if (Shape == "self" || (Shape == "circle" && MaxRange <= 0.0))
		{
			return From + FCastVec{ 0.0, 0.0, 10.0 };  // just off the ground round its user, as today
		}
		if (Shape == "cone")
		{
			FCastVec Way = { Aim.X - From.X, Aim.Y - From.Y, 0.0 };
			const double Length = std::sqrt(Way.X * Way.X + Way.Y * Way.Y);
			Way = Length > 1e-6 ? Way * (1.0 / Length) : FCastVec{ 1.0, 0.0, 0.0 };
			return From + Way * (MaxRange * 100.0 * 0.55);
		}
		if (Shape == "line" || Shape == "vector")
		{
			return From + (Aim - From) * 0.6;
		}
		return Aim;
	}

	double WantedSize(const FLookEvent& Event, double AreaCm)
	{
		double Want = 0.0;
		if (Event.bFitArea && AreaCm > 0.0)
		{
			Want = AreaCm * Event.FitScale;
		}
		else if (Event.Size > 0.0)
		{
			Want = Event.Size;
		}
		else if (Event.bFitArea)
		{
			// A single target has no area: the smallest size allowed, else as made.
			Want = Event.MinSize;
		}
		if (Want <= 0.0)
		{
			return 0.0;
		}
		if (Event.MinSize > 0.0)
		{
			Want = std::max(Want, Event.MinSize);
		}
		if (Event.MaxSize > 0.0)
		{
			Want = std::min(Want, Event.MaxSize);
		}
		return Want;
	}

	double EffectScale(double WantCm, double FilmedCm)
	{
		return Clamp(WantCm / std::max(FilmedCm, 10.0), 0.08, 6.0);
	}

	double FootprintOf(const FLooksFile& File, const std::string& Effect, double Fallback)
	{
		const auto Found = File.Footprints.find(Effect);
		return Found == File.Footprints.end() ? Fallback : Found->second;
	}

	double ScaleFor(const FLookEvent& Event, const FLooksFile& File, double AreaCm)
	{
		const double Want = WantedSize(Event, AreaCm);
		return Want > 0.0 ? EffectScale(Want, FootprintOf(File, Event.Effect)) : Event.Scale;
	}

	double LightRadius(const FLookEvent& Event, double AreaCm)
	{
		const double Want = Event.bFitArea ? WantedSize(Event, AreaCm) : 0.0;
		return Want > 0.0 ? Want : Event.Radius;
	}

	double ShakeStrength(const FLookEvent& Event, double Harm)
	{
		const double H = std::max(0.0, Harm);
		double Strength = Event.Strength;
		if (Event.Harm == EHarm::Scale)
		{
			if (H <= 0.0)
			{
				return 0.0;  // nothing hurt: nothing to feel
			}
			// As hard as it hurt: a scratch barely, a third of someone's health
			// properly (today's single blow).
			Strength = Clamp(H * 2.0, 0.12, 0.9) * Event.Strength;
		}
		else if (Event.Harm == EHarm::Add)
		{
			Strength = Event.Strength + H;
		}
		return Clamp(Strength, 0.0, 1.5);
	}

	FCastVec OffsetBy(const FCastVec& Offset, double Yaw)
	{
		const double R = Yaw * 3.14159265358979323846 / 180.0;
		const double C = std::cos(R);
		const double S = std::sin(R);
		// Forward is the facing, right a quarter turn clockwise from it (Unreal's Y).
		return { Offset.X * C - Offset.Y * S, Offset.X * S + Offset.Y * C, Offset.Z };
	}

	FAnchorPoint AnchorPoint(EAnchor Anchor, const FAnchorInput& Input)
	{
		FAnchorPoint Out;
		const FCastVec Up = { 0.0, 0.0, 1.0 };
		switch (Anchor)
		{
		case EAnchor::Ground:
			Out.Where = Input.Ground;
			break;
		case EAnchor::Body:
			Out.Where = Input.Ground + Up * BodyHeight;
			break;
		case EAnchor::Head:
			if (Input.bHasHead)
			{
				Out.Where = Input.Head;
				Out.Resolved = "socket";
			}
			else
			{
				Out.Where = Input.Ground + Up * HeadHeight;
				Out.Resolved = "fallback";
			}
			break;
		case EAnchor::Weapon:
			if (Input.bHasWeapon)
			{
				Out.Where = Input.Weapon;
				Out.Resolved = "socket";
				break;
			}
			[[fallthrough]];
		case EAnchor::Hand:
			if (Input.bHasHand)
			{
				Out.Where = Input.Hand;
				Out.Resolved = "socket";
			}
			else
			{
				Out.Where = Input.Ground + Up * HandHeight + OffsetBy({ HandReach, 0.0, 0.0 }, Input.Yaw);
				Out.Resolved = "fallback";
			}
			break;
		case EAnchor::Center:
			Out.Where = Input.Centre;
			break;
		case EAnchor::Aim:
			Out.Where = Input.Aim;
			break;
		}
		return Out;
	}
}
