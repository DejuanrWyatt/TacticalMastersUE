#include "SimClassFile.h"

#include "SimJson.h"
#include "SimAbility.h"

#include <algorithm>
#include <cmath>

namespace TMSim
{
	namespace
	{
		// The format's limits: the clamps astra_import.gd put on each number,
		// which the class creator checks against too (app/tmclass.mjs).
		struct FLimit
		{
			const char* Key;
			double Low;
			double High;
		};
		const FLimit AbilityLimits[] =
		{
			{ "power", 0.0, 400.0 }, { "min_range", 0.0, 20.0 }, { "max_range", 0.0, 20.0 }, { "aoe", 0.0, 8.0 },
			{ "cooldown", 0.0, 10.0 }, { "cast", 0.0, 10.0 }, { "angle", 10.0, 180.0 }, { "channel", 1.0, 6.0 },
			{ "tg", -100.0, 100.0 },
		};
		// jobs.gd:281-282.
		const int StatLow[StatCount] = { 10, 0, 0, 0, 0, 0, 1, 1, 0, 3 };
		const int StatHigh[StatCount] = { 300, 30, 30, 60, 60, 60, 20, 15, 15, 25 };

		const char* const ClassKeys[] = { "format", "version", "id", "name", "color", "look", "icon", "roles", "stats", "abilities", "creator", "monster" };
		const char* const AbilityKeys[] = { "id", "name", "desc", "kind", "effect", "scale", "target", "shape", "power", "min_range",
			"max_range", "aoe", "angle", "channel", "cooldown", "cast", "tg", "status", "self_status", "buffs", "fx", "vfx", "anim", "special",
			"element", "pet", "zone" };
		const char* const Specials[] = { "blink", "swap", "tame", "summon", "rewind", "pet", "leap", "behind", "zone",
			// 2026-10-05, the unique and mobility spells (SimSpells.cpp).
			"link", "gravity", "transfer", "pact", "chain", "spiritswap", "reckoning", "dash", "disengage", "vault", "charge",
			"shadowhop", "recall", "rally", "hook", "shove", "riptide",
			// 2026-10-06, picks from Cire's Spell Codex (Docs/design/feat-codex-picks.md).
			"warned", "ricochet", "execute", "crowd" };
		const char* const ZoneKeys[] = { "turns", "percent", "sight", "also", "once", "reveal", "hide", "flammable", "portal" };
		const char* const Elements[] = { "fire", "ice", "lightning", "water", "none" };
		const char* const MonsterKeys[] = { "tier", "temperament", "traits", "ring", "leash", "phases" };
		const char* const Tiers[] = { "easy", "medium", "hard", "epic" };
		const char* const Temperaments[] = { "docile", "skittish", "provoked", "territorial", "aggressive", "guard_place", "guard_unit", "patrol" };
		const char* const TraitNames[] = { "ambush", "pack_hunter", "scavenger", "lookout", "unstoppable", "stagger", "enrage" };
		const char* const VfxKeys[] = { "system", "at", "scale" };
		const char* const VfxPlaces[] = { "user", "point", "targets" };
		const char* const Looks[] = { "squire", "knight", "archer", "monk", "black_mage", "white_mage" };
		const char* const Roles[] = { "tank", "damage", "support", "special" };
		const char* const Kinds[] = { "active", "passive", "toggle", "channeled", "active_passive", "aura" };
		const char* const Shapes[] = { "unit", "point", "circle", "self", "line", "cone", "global", "vector" };

		template <size_t N>
		bool OneOf(const std::string& Value, const char* const (&List)[N])
		{
			for (const char* Each : List)
			{
				if (Value == Each)
				{
					return true;
				}
			}
			return false;
		}

		bool IsWhole(double Value)
		{
			return std::floor(Value) == Value;
		}

		/** Lowercase letters, digits and _, starting with a letter. A class id is at most 31 long (astra_import.gd:146); an ability's is the class id and its name, so it may run longer. */
		bool ValidId(const std::string& Id, size_t Longest = 31)
		{
			if (Id.empty() || Id.size() > Longest || Id[0] < 'a' || Id[0] > 'z')
			{
				return false;
			}
			for (char C : Id)
			{
				if (!((C >= 'a' && C <= 'z') || (C >= '0' && C <= '9') || C == '_'))
				{
					return false;
				}
			}
			return true;
		}

		bool ValidColour(const std::string& Colour)
		{
			if (Colour.size() != 7 || Colour[0] != '#')
			{
				return false;
			}
			for (size_t i = 1; i < Colour.size(); ++i)
			{
				const char C = Colour[i];
				if (!((C >= '0' && C <= '9') || (C >= 'a' && C <= 'f')))
				{
					return false;
				}
			}
			return true;
		}

		/**
		 * An object path in the project's Content folder, as Unreal writes one:
		 * /Game/Pack/NS_Thing.NS_Thing. Only characters a package path may hold,
		 * and no way out of /Game, because the game hands it to the asset loader.
		 */
		bool ValidAssetPath(const std::string& Path)
		{
			if (Path.size() < 7 || Path.size() > 256 || Path.compare(0, 6, "/Game/") != 0
				|| Path.find("..") != std::string::npos || Path.find("//") != std::string::npos)
			{
				return false;
			}
			for (char C : Path)
			{
				if (!((C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9')
					|| C == '_' || C == '/' || C == '.' || C == '-'))
				{
					return false;
				}
			}
			return true;
		}

		struct FProblems
		{
			std::string Text;
			void Say(const std::string& Where, const std::string& What)
			{
				Text += Where + ": " + What + "\n";
			}
		};

		/** A string member, or empty with a complaint if it is there and not a string. */
		std::string StringOf(const FJson& Object, const char* Key, const std::string& Where, FProblems& Problems, bool bRequired)
		{
			const FJson* Value = Object.Find(Key);
			if (!Value)
			{
				if (bRequired)
				{
					Problems.Say(Where, std::string("missing \"") + Key + "\"");
				}
				return std::string();
			}
			if (!Value->IsString())
			{
				Problems.Say(Where, std::string("\"") + Key + "\" should be text");
				return std::string();
			}
			return Value->String;
		}

		void ReadAbility(const FJson& Json, int Slot, FAbility& Out, FProblems& Problems)
		{
			const std::string Where = "abilities[" + std::to_string(Slot) + "]";
			if (!Json.IsObject())
			{
				Problems.Say(Where, "should be an object");
				return;
			}
			for (const std::pair<std::string, FJson>& Member : Json.Object)
			{
				if (!OneOf(Member.first, AbilityKeys))
				{
					Problems.Say(Where, "unknown key \"" + Member.first + "\"");
				}
			}
			Out.Id = StringOf(Json, "id", Where, Problems, true);
			Out.Name = StringOf(Json, "name", Where, Problems, true);
			Out.Desc = StringOf(Json, "desc", Where, Problems, false);
			Out.Fx = StringOf(Json, "fx", Where, Problems, false);
			Out.Special = StringOf(Json, "special", Where, Problems, false);
			if (!Out.Special.empty() && !OneOf(Out.Special, Specials))
			{
				Problems.Say(Where, "unknown special \"" + Out.Special + "\"");
			}
			// A pet (2026-10-02): what it calls up and for how many of its turns.
			// The class need not be loaded yet: monsters are read after classes.
			if (const FJson* Pet = Json.Find("pet"))
			{
				const FJson* PetJob = Pet->IsObject() ? Pet->Find("job") : nullptr;
				const FJson* PetTurns = Pet->IsObject() ? Pet->Find("turns") : nullptr;
				if (!PetJob || !PetJob->IsString() || !ValidId(PetJob->String, 96) || !PetTurns || !PetTurns->IsNumber()
					|| !IsWhole(PetTurns->Number) || PetTurns->Number < 1.0 || PetTurns->Number > 10.0)
				{
					Problems.Say(Where, "\"pet\" should be a class id (\"job\") and 1 to 10 turns (\"turns\")");
				}
				else
				{
					Out.PetJob = PetJob->String;
					Out.PetTurns = static_cast<int>(PetTurns->Number);
				}
				if (Out.Special != "pet")
				{
					Problems.Say(Where, "\"pet\" goes with \"special\": \"pet\"");
				}
			}
			else if (Out.Special == "pet")
			{
				Problems.Say(Where, "\"special\": \"pet\" needs a \"pet\"");
			}
			// Ground that lasts (2026-10-04, Docs/design/feat-ground-zones.md).
			if (const FJson* Zone = Json.Find("zone"))
			{
				if (!Zone->IsObject())
				{
					Problems.Say(Where, "\"zone\" should be an object");
				}
				else
				{
					for (const std::pair<std::string, FJson>& Member : Zone->Object)
					{
						if (!OneOf(Member.first, ZoneKeys))
						{
							Problems.Say(Where, "unknown key \"" + Member.first + "\" in \"zone\"");
						}
					}
					const FJson* Turns = Zone->Find("turns");
					if (!Turns || !Turns->IsNumber() || !IsWhole(Turns->Number) || Turns->Number < 1.0 || Turns->Number > 6.0)
					{
						Problems.Say(Where, "\"zone\" needs \"turns\", 1 to 6");
					}
					else
					{
						Out.ZoneTurns = static_cast<int>(Turns->Number);
					}
					auto ZoneNumber = [&](const char* Key, double High, float& Into)
					{
						if (const FJson* Value = Zone->Find(Key))
						{
							if (!Value->IsNumber() || Value->Number < 0.0 || Value->Number > High)
							{
								Problems.Say(Where, std::string("\"zone\" ") + Key + " should be a number from 0 to "
									+ std::to_string(static_cast<int>(High)));
							}
							else
							{
								Into = static_cast<float>(Value->Number);
							}
						}
					};
					ZoneNumber("percent", 25.0, Out.ZonePercent);
					ZoneNumber("sight", 12.0, Out.ZoneSight);
					auto ZoneFlag = [&](const char* Key, bool& Into)
					{
						if (const FJson* Value = Zone->Find(Key))
						{
							if (Value->Type != FJson::EType::Bool)
							{
								Problems.Say(Where, std::string("\"zone\" ") + Key + " should be true or false");
							}
							else
							{
								Into = Value->Bool;
							}
						}
					};
					ZoneFlag("once", Out.bZoneOnce);
					ZoneFlag("reveal", Out.bZoneReveal);
					ZoneFlag("hide", Out.bZoneHide);
					ZoneFlag("flammable", Out.bZoneFlammable);
					ZoneFlag("portal", Out.bZonePortal);
					if (const FJson* Also = Zone->Find("also"))
					{
						const FJson* Id = Also->IsObject() ? Also->Find("id") : nullptr;
						const FJson* AlsoTurns = Also->IsObject() ? Also->Find("turns") : nullptr;
						if (!Id || !Id->IsString() || !FindStatus(Id->String) || !AlsoTurns || !AlsoTurns->IsNumber()
							|| !IsWhole(AlsoTurns->Number) || AlsoTurns->Number < 1.0 || AlsoTurns->Number > 10.0)
						{
							Problems.Say(Where, "\"zone\" also should be a known status id and 1 to 10 turns");
						}
						else
						{
							Out.ZoneStatus2 = Id->String;
							Out.ZoneStatus2Turns = static_cast<int>(AlsoTurns->Number);
						}
					}
				}
				if (Out.Special != "zone" && Out.Special != "recall")
				{
					Problems.Say(Where, "\"zone\" goes with \"special\": \"zone\" or \"recall\"");
				}
			}
			else if (Out.Special == "zone" || Out.Special == "recall")
			{
				Problems.Say(Where, "\"special\": \"" + Out.Special + "\" needs a \"zone\"");
			}
			Out.Element = StringOf(Json, "element", Where, Problems, false);
			if (!Out.Element.empty() && !OneOf(Out.Element, Elements))
			{
				Problems.Say(Where, "unknown element \"" + Out.Element + "\"");
			}
			Out.Anim = StringOf(Json, "anim", Where, Problems, false);
			if (!Out.Anim.empty() && std::find(AnimMotions().begin(), AnimMotions().end(), Out.Anim) == AnimMotions().end())
			{
				Problems.Say(Where, "unknown anim \"" + Out.Anim + "\"");
			}
			if (!ValidId(Out.Id, 96) && !Out.Id.empty())
			{
				Problems.Say(Where, "the id should be lowercase letters, digits and _");
			}

			Out.Kind = StringOf(Json, "kind", Where, Problems, true);
			if (!Out.Kind.empty() && !OneOf(Out.Kind, Kinds))
			{
				Problems.Say(Where, "unknown kind \"" + Out.Kind + "\"");
			}
			const std::string Effect = StringOf(Json, "effect", Where, Problems, true);
			if (Effect == "damage") { Out.Effect = EEffect::Damage; }
			else if (Effect == "heal") { Out.Effect = EEffect::Heal; }
			else if (Effect == "revive") { Out.Effect = EEffect::Revive; }
			else if (Effect == "support") { Out.Effect = EEffect::Support; }
			else if (!Effect.empty()) { Problems.Say(Where, "unknown effect \"" + Effect + "\""); }
			const std::string Scale = StringOf(Json, "scale", Where, Problems, true);
			if (Scale == "att") { Out.Scale = EScale::Att; }
			else if (Scale == "mag") { Out.Scale = EScale::Mag; }
			else if (!Scale.empty()) { Problems.Say(Where, "scale is att or mag"); }
			const std::string Target = StringOf(Json, "target", Where, Problems, true);
			if (Target == "enemy") { Out.Target = ETargetSide::Enemy; }
			else if (Target == "ally") { Out.Target = ETargetSide::Ally; }
			else if (Target == "ko_ally") { Out.Target = ETargetSide::KoAlly; }
			else if (!Target.empty()) { Problems.Say(Where, "unknown target \"" + Target + "\""); }
			Out.Shape = StringOf(Json, "shape", Where, Problems, false);
			if (!Out.Shape.empty() && !OneOf(Out.Shape, Shapes))
			{
				Problems.Say(Where, "unknown shape \"" + Out.Shape + "\"");
			}

			// The numbers. Each is checked against its limit rather than clamped:
			// a number out of range is a fault in whatever wrote the file.
			for (const FLimit& Limit : AbilityLimits)
			{
				const FJson* Value = Json.Find(Limit.Key);
				if (!Value)
				{
					continue;
				}
				if (!Value->IsNumber() || Value->Number < Limit.Low || Value->Number > Limit.High)
				{
					Problems.Say(Where, std::string("\"") + Limit.Key + "\" should be a number from "
						+ std::to_string(static_cast<int>(Limit.Low)) + " to " + std::to_string(static_cast<int>(Limit.High)));
				}
			}
			auto Number = [&Json](const char* Key, double Default)
			{
				const FJson* Value = Json.Find(Key);
				return Value && Value->IsNumber() ? Value->Number : Default;
			};
			for (const char* Whole : { "cooldown", "channel", "tg" })
			{
				if (!IsWhole(Number(Whole, 0.0)))
				{
					Problems.Say(Where, std::string("\"") + Whole + "\" should be a whole number");
				}
			}
			// Floats in the rules, as the built-in abilities are: the same narrowing
			// a float literal in SimAbility.cpp gets.
			Out.Power = static_cast<float>(Number("power", 0.0));
			Out.MinRange = static_cast<float>(Number("min_range", 0.0));
			Out.MaxRange = static_cast<float>(Number("max_range", 0.0));
			Out.Aoe = static_cast<float>(Number("aoe", 0.0));
			Out.Angle = static_cast<float>(Number("angle", 60.0));
			Out.Channel = static_cast<int>(Number("channel", 2.0));
			Out.Cooldown = static_cast<int>(Number("cooldown", 0.0));
			Out.Cast = static_cast<float>(Number("cast", 0.0));
			Out.TgChange = static_cast<int>(Number("tg", 0.0));

			if (const FJson* Status = Json.Find("status"))
			{
				const FJson* Id = Status->IsObject() ? Status->Find("id") : nullptr;
				const FJson* Turns = Status->IsObject() ? Status->Find("turns") : nullptr;
				if (!Id || !Id->IsString() || !FindStatus(Id->String) || !Turns || !Turns->IsNumber()
					|| !IsWhole(Turns->Number) || Turns->Number < 1.0 || Turns->Number > 10.0)
				{
					Problems.Say(Where, "\"status\" should be a known status id and 1 to 10 turns");
				}
				else
				{
					Out.StatusId = Id->String;
					Out.StatusTurns = static_cast<int>(Turns->Number);
				}
			}
			// What the user gives itself (2026-10-03), read as "status" is.
			if (const FJson* Self = Json.Find("self_status"))
			{
				const FJson* Id = Self->IsObject() ? Self->Find("id") : nullptr;
				const FJson* Turns = Self->IsObject() ? Self->Find("turns") : nullptr;
				if (!Id || !Id->IsString() || !FindStatus(Id->String) || !Turns || !Turns->IsNumber()
					|| !IsWhole(Turns->Number) || Turns->Number < 1.0 || Turns->Number > 10.0)
				{
					Problems.Say(Where, "\"self_status\" should be a known status id and 1 to 10 turns");
				}
				else
				{
					Out.SelfStatusId = Id->String;
					Out.SelfStatusTurns = static_cast<int>(Turns->Number);
				}
			}
			if (const FJson* Buffs = Json.Find("buffs"))
			{
				if (!Buffs->IsArray())
				{
					Problems.Say(Where, "\"buffs\" should be a list");
				}
				for (const FJson& Buff : Buffs->Array)
				{
					const FJson* Stat = Buff.IsObject() ? Buff.Find("stat") : nullptr;
					const FJson* Amount = Buff.IsObject() ? Buff.Find("amount") : nullptr;
					const FJson* Turns = Buff.IsObject() ? Buff.Find("turns") : nullptr;
					const EStat Which = Stat && Stat->IsString() ? StatFromName(Stat->String) : EStat::Count;
					if (Which == EStat::Count || !Amount || !Amount->IsNumber() || !IsWhole(Amount->Number)
						|| !Turns || !Turns->IsNumber() || !IsWhole(Turns->Number) || Turns->Number < 1.0 || Turns->Number > 5.0)
					{
						Problems.Say(Where, "each buff should be a stat, a whole amount, and 1 to 5 turns");
						continue;
					}
					Out.Buffs.push_back({ Which, static_cast<int>(Amount->Number), static_cast<int>(Turns->Number) });
				}
			}
			if (const FJson* Vfx = Json.Find("vfx"))
			{
				if (!Vfx->IsObject())
				{
					Problems.Say(Where, "\"vfx\" should be an object");
					return;
				}
				for (const std::pair<std::string, FJson>& Member : Vfx->Object)
				{
					if (!OneOf(Member.first, VfxKeys))
					{
						Problems.Say(Where, "unknown key \"" + Member.first + "\" in \"vfx\"");
					}
				}
				const std::string System = StringOf(*Vfx, "system", Where + ".vfx", Problems, true);
				if (!System.empty() && !ValidAssetPath(System))
				{
					Problems.Say(Where, "\"vfx\" system should be an asset path under /Game/");
				}
				std::string At = StringOf(*Vfx, "at", Where + ".vfx", Problems, false);
				if (At.empty())
				{
					At = "targets";
				}
				else if (!OneOf(At, VfxPlaces))
				{
					Problems.Say(Where, "\"vfx\" at should be user, point or targets");
				}
				double Size = 1.0;
				if (const FJson* Given = Vfx->Find("scale"))
				{
					if (!Given->IsNumber() || Given->Number < 0.1 || Given->Number > 5.0)
					{
						Problems.Say(Where, "\"vfx\" scale should be a number from 0.1 to 5");
					}
					else
					{
						Size = Given->Number;
					}
				}
				Out.VfxSystem = System;
				Out.VfxAt = At;
				Out.VfxScale = static_cast<float>(Size);
			}
		}
	}

	std::string ReadAbilityObject(const FJson& Json, const std::string& Where, FAbility& Out)
	{
		FProblems Problems;
		ReadAbility(Json, 0, Out, Problems);
		std::string Text = Problems.Text;
		// Said as "abilities[0]" by the reader; the caller names where it really is.
		for (size_t At = Text.find("abilities[0]"); At != std::string::npos; At = Text.find("abilities[0]", At + Where.size()))
		{
			Text.replace(At, 12, Where);
		}
		if (!Text.empty())
		{
			Text.pop_back();
		}
		return Text;
	}

	namespace
	{
		/** A class file's "monster": what makes it a neutral monster, and a boss's later phases. */
		void ReadMonster(const FJson& Json, FJobDef& OutJob, std::vector<FAbility>& OutAbilities, FProblems& Problems)
		{
			FMonsterInfo& Info = OutJob.Monster;
			Info.bMonster = true;
			if (!Json.IsObject())
			{
				Problems.Say("monster", "should be an object");
				return;
			}
			for (const std::pair<std::string, FJson>& Member : Json.Object)
			{
				if (!OneOf(Member.first, MonsterKeys))
				{
					Problems.Say("monster", "unknown key \"" + Member.first + "\"");
				}
			}
			const std::string Tier = StringOf(Json, "tier", "monster", Problems, true);
			Info.Tier = -1;
			for (int i = 0; i < 4; ++i)
			{
				Info.Tier = Tier == Tiers[i] ? i : Info.Tier;
			}
			if (Info.Tier < 0)
			{
				Problems.Say("monster", "\"tier\" should be easy, medium, hard or epic");
				Info.Tier = 0;
			}
			const std::string Temperament = StringOf(Json, "temperament", "monster", Problems, true);
			bool bKnown = false;
			for (int i = 0; i < 8; ++i)
			{
				if (Temperament == Temperaments[i])
				{
					Info.Temperament = static_cast<ETemperament>(i);
					bKnown = true;
				}
			}
			if (!bKnown)
			{
				Problems.Say("monster", "\"temperament\" should be one of docile, skittish, provoked, territorial, aggressive, guard_place, guard_unit, patrol");
			}
			if (const FJson* Traits = Json.Find("traits"))
			{
				if (!Traits->IsArray())
				{
					Problems.Say("monster", "\"traits\" should be a list");
				}
				for (const FJson& Trait : Traits->Array)
				{
					bool bFound = false;
					for (int i = 0; i < 7; ++i)
					{
						if (Trait.IsString() && Trait.String == TraitNames[i])
						{
							Info.Traits |= 1u << i;
							bFound = true;
						}
					}
					if (!bFound)
					{
						Problems.Say("monster", "unknown trait (ambush, pack_hunter, scavenger, lookout, unstoppable, stagger, enrage)");
					}
				}
			}
			auto Metres = [&](const char* Key, float& Into)
			{
				if (const FJson* Value = Json.Find(Key))
				{
					if (!Value->IsNumber() || Value->Number < 1.0 || Value->Number > 20.0)
					{
						Problems.Say("monster", std::string("\"") + Key + "\" should be 1 to 20 metres");
						return;
					}
					Into = static_cast<float>(Value->Number);
				}
			};
			Metres("ring", Info.Ring);
			Metres("leash", Info.Leash);
			if (const FJson* Phases = Json.Find("phases"))
			{
				if (!Phases->IsArray() || Phases->Array.size() > 3)
				{
					Problems.Say("monster", "\"phases\" should be a list of at most 3");
					return;
				}
				int Last = 100;
				for (size_t p = 0; p < Phases->Array.size(); ++p)
				{
					const FJson& Phase = Phases->Array[p];
					const std::string Where = "monster.phases[" + std::to_string(p) + "]";
					const FJson* Below = Phase.IsObject() ? Phase.Find("below") : nullptr;
					const FJson* List = Phase.IsObject() ? Phase.Find("abilities") : nullptr;
					if (!Below || !Below->IsNumber() || !IsWhole(Below->Number) || Below->Number < 1 || Below->Number >= Last)
					{
						Problems.Say(Where, "\"below\" should be a whole percent under the last phase's");
						continue;
					}
					if (!List || !List->IsArray() || List->Array.size() != 4)
					{
						Problems.Say(Where, "\"abilities\": exactly four");
						continue;
					}
					FMonsterPhase Next;
					Next.BelowPercent = static_cast<int>(Below->Number);
					Last = Next.BelowPercent;
					for (int Slot = 0; Slot < 4; ++Slot)
					{
						FAbility Ability;
						FProblems Inner;
						ReadAbility(List->Array[Slot], Slot, Ability, Inner);
						if (!Inner.Text.empty())
						{
							Problems.Text += Where + "." + Inner.Text;
						}
						Next.AbilityIds[Slot] = Ability.Id;
						OutAbilities.push_back(Ability);
					}
					Info.Phases.push_back(Next);
				}
			}
		}
	}

	std::string ReadClassFile(const std::string& Text, FJobDef& OutJob, std::vector<FAbility>& OutAbilities)
	{
		FJson Json;
		const std::string Bad = ParseJson(Text, Json);
		if (!Bad.empty())
		{
			return "not JSON: " + Bad;
		}
		if (!Json.IsObject())
		{
			return "not a class file";
		}
		const FJson* Format = Json.Find("format");
		if (!Format || !Format->IsString() || Format->String != ClassFileFormat)
		{
			return std::string("not a class file (format should be \"") + ClassFileFormat + "\")";
		}
		const FJson* Version = Json.Find("version");
		if (!Version || !Version->IsNumber() || Version->Number != ClassFileVersion)
		{
			return "a class file of a version this does not read";
		}

		FProblems Problems;
		for (const std::pair<std::string, FJson>& Member : Json.Object)
		{
			if (!OneOf(Member.first, ClassKeys))
			{
				Problems.Say("class", "unknown key \"" + Member.first + "\"");
			}
		}
		OutJob = FJobDef();
		OutAbilities.clear();
		OutJob.Id = StringOf(Json, "id", "class", Problems, true);
		if (!OutJob.Id.empty() && !ValidId(OutJob.Id))
		{
			Problems.Say("id", "lowercase letters, digits and _, starting with a letter");
		}
		OutJob.Name = StringOf(Json, "name", "class", Problems, true);
		OutJob.Color = StringOf(Json, "color", "class", Problems, true);
		if (!OutJob.Color.empty() && !ValidColour(OutJob.Color))
		{
			Problems.Say("color", "a colour like #9b83ff");
		}
		OutJob.Look = StringOf(Json, "look", "class", Problems, true);
		if (!OutJob.Look.empty() && !OneOf(OutJob.Look, Looks))
		{
			Problems.Say("look", "one of the six built-in bodies");
		}
		OutJob.Icon = StringOf(Json, "icon", "class", Problems, false);

		const FJson* RoleList = Json.Find("roles");
		if (!RoleList || !RoleList->IsArray() || RoleList->Array.empty() || RoleList->Array.size() > 2)
		{
			Problems.Say("roles", "one or two of tank, damage, support, special");
		}
		else
		{
			for (const FJson& Role : RoleList->Array)
			{
				if (!Role.IsString() || !OneOf(Role.String, Roles))
				{
					Problems.Say("roles", "one or two of tank, damage, support, special");
					break;
				}
				OutJob.Roles.push_back(Role.String);
			}
		}

		const FJson* Stats = Json.Find("stats");
		if (!Stats || !Stats->IsObject())
		{
			Problems.Say("stats", "missing");
		}
		else
		{
			for (const std::pair<std::string, FJson>& Member : Stats->Object)
			{
				if (StatFromName(Member.first) == EStat::Count)
				{
					Problems.Say("stats", "unknown stat \"" + Member.first + "\"");
				}
			}
			for (int i = 0; i < StatCount; ++i)
			{
				const EStat Which = static_cast<EStat>(i);
				const FJson* Value = Stats->Find(StatName(Which));
				if (!Value || !Value->IsNumber() || !IsWhole(Value->Number) || Value->Number < StatLow[i] || Value->Number > StatHigh[i])
				{
					Problems.Say(std::string("stats.") + StatName(Which), "a whole number from "
						+ std::to_string(StatLow[i]) + " to " + std::to_string(StatHigh[i]));
					continue;
				}
				OutJob.Stats.Set(Which, static_cast<int>(Value->Number));
			}
		}

		const FJson* List = Json.Find("abilities");
		if (!List || !List->IsArray() || List->Array.size() != 4)
		{
			Problems.Say("abilities", "exactly four");
		}
		else
		{
			for (int Slot = 0; Slot < 4; ++Slot)
			{
				FAbility Ability;
				ReadAbility(List->Array[Slot], Slot, Ability, Problems);
				OutJob.AbilityIds[Slot] = Ability.Id;
				OutAbilities.push_back(Ability);
			}
			for (int A = 0; A < 4; ++A)
			{
				for (int B = A + 1; B < 4; ++B)
				{
					if (!OutAbilities[A].Id.empty() && OutAbilities[A].Id == OutAbilities[B].Id)
					{
						Problems.Say("abilities", "two share the id \"" + OutAbilities[A].Id + "\"");
					}
				}
			}
		}
		if (const FJson* Monster = Json.Find("monster"))
		{
			ReadMonster(*Monster, OutJob, OutAbilities, Problems);
		}
		for (size_t A = 0; A < OutAbilities.size(); ++A)
		{
			for (size_t B = A + 1; B < OutAbilities.size(); ++B)
			{
				if (A >= 4 && !OutAbilities[A].Id.empty() && OutAbilities[A].Id == OutAbilities[B].Id)
				{
					Problems.Say("monster", "two phase abilities share the id \"" + OutAbilities[A].Id + "\"");
				}
			}
		}
		if (!Problems.Text.empty())
		{
			Problems.Text.pop_back();  // the last newline
		}
		return Problems.Text;
	}

	std::string LoadClassFile(const std::string& Text)
	{
		FJobDef Job;
		std::vector<FAbility> Abilities;
		const std::string Problems = ReadClassFile(Text, Job, Abilities);
		if (!Problems.empty())
		{
			return Problems;
		}
		const std::string Refused = RegisterJob(Job, Abilities);
		return Refused.empty() ? std::string() : Job.Id + ": " + Refused;
	}
}
