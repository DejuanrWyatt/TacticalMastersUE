#include "SimItem.h"

#include "SimAbility.h"
#include "SimClassFile.h"
#include "SimJson.h"
#include "SimUnit.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace TMSim
{
	namespace
	{
		std::map<std::string, FItemDef>& Registry()
		{
			// A std::map so an item's address never moves once it is in: units
			// point at the items they carry.
			static std::map<std::string, FItemDef> Table;
			return Table;
		}

		std::vector<const FItemDef*>& Listing()
		{
			static std::vector<const FItemDef*> List;
			return List;
		}

		void Relist()
		{
			std::vector<const FItemDef*>& List = Listing();
			List.clear();
			for (const auto& Entry : Registry())
			{
				List.push_back(&Entry.second);
			}
			std::stable_sort(List.begin(), List.end(), [](const FItemDef* A, const FItemDef* B)
			{
				return A->Tier != B->Tier ? A->Tier < B->Tier : A->Id < B->Id;
			});
		}

		const char* const TierNames[] = { "common", "uncommon", "rare", "epic" };
		const char* const ItemKeys[] = { "format", "version", "id", "name", "desc", "icon", "tier", "cost", "stats", "bonus", "creator", "ability", "effects", "boss" };
		const char* const EffectKeys[] = { "vs_monsters_percent", "high_ground_percent", "still_defence", "alone_percent", "alone_move",
			"low_health_percent", "low_health_evasion", "unseen_evasion", "lifesteal_percent", "thorns", "kill_tg_percent", "first_strike",
			"phoenix", "damage_taken_percent", "heal_taken_percent", "calms_monsters", "steady", "shrine_key" };

		/** The ability the last item file read carried, until LoadItemFile registers it. */
		FAbility& PendingAbility()
		{
			static FAbility Held;
			return Held;
		}

		/** Abilities that came with items, to take back with them. */
		std::vector<std::string>& ItemAbilities()
		{
			static std::vector<std::string> List;
			return List;
		}
		const char* const BonusKeys[] = { "damage_flat", "damage_percent", "heal_flat", "heal_percent", "scale", "tg_percent" };

		/** How far each stat an item gives may go, either way. */
		int StatLimit(EStat Stat)
		{
			switch (Stat)
			{
			case EStat::Hp: return 100;
			case EStat::Speed: case EStat::Move: return 2;
			default: return 15;
			}
		}

		bool ValidItemId(const std::string& Id)
		{
			if (Id.empty() || Id.size() > 31 || Id[0] < 'a' || Id[0] > 'z')
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

		template <size_t N>
		bool KnownKey(const std::string& Key, const char* const (&List)[N])
		{
			for (const char* Each : List)
			{
				if (Key == Each)
				{
					return true;
				}
			}
			return false;
		}

		/** A whole number within a range, or a complaint. */
		bool WholeIn(const FJson& Value, double Low, double High, const std::string& Where, std::string& Problems, int& Out)
		{
			if (!Value.IsNumber() || std::floor(Value.Number) != Value.Number || Value.Number < Low || Value.Number > High)
			{
				Problems += Where + ": should be a whole number from " + std::to_string(static_cast<int>(Low))
					+ " to " + std::to_string(static_cast<int>(High)) + "\n";
				return false;
			}
			Out = static_cast<int>(Value.Number);
			return true;
		}

		/** Sums one item number over what a unit carries. */
		template <typename F>
		int SumGear(const FUnit& Unit, F Of)
		{
			int Total = 0;
			for (const FItemDef* Item : Unit.Gear)
			{
				if (Item)
				{
					Total += Of(*Item);
				}
			}
			return Total;
		}

		bool Reaches(const FItemDef& Item, bool bAttScale)
		{
			return Item.Scale == EItemScale::Any || (Item.Scale == EItemScale::Att) == bAttScale;
		}
	}

	const char* ItemTierName(EItemTier Tier)
	{
		const int Index = static_cast<int>(Tier);
		return Index >= 0 && Index < 4 ? TierNames[Index] : "common";
	}

	int ItemTierCost(EItemTier Tier)
	{
		switch (Tier)
		{
		case EItemTier::Common: return 1;
		case EItemTier::Uncommon: return 2;
		case EItemTier::Rare: return 3;
		default: return 0;  // epic: won, never bought
		}
	}

	const FItemDef* FindItem(const std::string& ItemId)
	{
		const auto Found = Registry().find(ItemId);
		return Found == Registry().end() ? nullptr : &Found->second;
	}

	const std::vector<const FItemDef*>& AllItems()
	{
		return Listing();
	}

	int GearSum(const FUnit& Unit, int FItemDef::* Member)
	{
		int Total = 0;
		for (const FItemDef* Item : Unit.Gear)
		{
			if (Item)
			{
				Total += Item->*Member;
			}
		}
		return Total;
	}

	bool GearHas(const FUnit& Unit, bool FItemDef::* Member)
	{
		for (const FItemDef* Item : Unit.Gear)
		{
			if (Item && Item->*Member)
			{
				return true;
			}
		}
		return false;
	}

	std::string RegisterItem(const FItemDef& Item)
	{
		if (!ValidItemId(Item.Id))
		{
			return "\"" + Item.Id + "\" is not an item id: lowercase letters, digits and _, starting with a letter.";
		}
		if (Registry().count(Item.Id))
		{
			return "There is already an item called \"" + Item.Id + "\".";
		}
		Registry().emplace(Item.Id, Item);
		Relist();
		return std::string();
	}

	void ForgetLoadedItems()
	{
		for (const std::string& AbilityId : ItemAbilities())
		{
			ForgetAbility(AbilityId);
		}
		ItemAbilities().clear();
		Registry().clear();
		Relist();
	}

	std::string ReadItemFile(const std::string& Text, FItemDef& Out)
	{
		Out = FItemDef();
		FJson Root;
		const std::string Parse = ParseJson(Text, Root);
		if (!Parse.empty())
		{
			return "not JSON: " + Parse + "\n";
		}
		if (!Root.IsObject())
		{
			return "an item file is one object\n";
		}
		std::string Problems;
		for (const auto& Member : Root.Object)
		{
			if (!KnownKey(Member.first, ItemKeys))
			{
				Problems += "item: an item file has no \"" + Member.first + "\"\n";
			}
		}
		const FJson* Format = Root.Find("format");
		if (!Format || !Format->IsString() || Format->String != ItemFileFormat)
		{
			Problems += std::string("item: \"format\" should be \"") + ItemFileFormat + "\"\n";
		}
		const FJson* Version = Root.Find("version");
		if (!Version || !Version->IsNumber() || Version->Number != ItemFileVersion)
		{
			Problems += "item: \"version\" should be " + std::to_string(ItemFileVersion) + "\n";
		}
		auto Text_ = [&](const char* Key, bool bRequired) -> std::string
		{
			const FJson* Value = Root.Find(Key);
			if (!Value)
			{
				if (bRequired)
				{
					Problems += std::string("item: missing \"") + Key + "\"\n";
				}
				return std::string();
			}
			if (!Value->IsString())
			{
				Problems += std::string("item: \"") + Key + "\" should be text\n";
				return std::string();
			}
			return Value->String;
		};
		Out.Id = Text_("id", true);
		Out.Name = Text_("name", true);
		Out.Desc = Text_("desc", false);
		Out.Icon = Text_("icon", false);
		Out.Boss = Text_("boss", false);
		if (!Out.Id.empty() && !ValidItemId(Out.Id))
		{
			Problems += "item: \"id\" should be lowercase letters, digits and _, starting with a letter, at most 31\n";
		}
		if (Out.Name.empty() || Out.Name.size() > 40)
		{
			Problems += "item: \"name\" should be 1 to 40 characters\n";
		}

		const std::string Tier = Text_("tier", true);
		bool bTier = false;
		for (int t = 0; t < 4; ++t)
		{
			if (Tier == TierNames[t])
			{
				Out.Tier = static_cast<EItemTier>(t);
				bTier = true;
			}
		}
		if (!bTier && !Tier.empty())
		{
			Problems += "item: \"tier\" should be common, uncommon, rare or epic\n";
		}
		Out.Cost = ItemTierCost(Out.Tier);
		if (const FJson* Cost = Root.Find("cost"))
		{
			WholeIn(*Cost, 0, 6, "item: \"cost\"", Problems, Out.Cost);
		}

		if (const FJson* Stats = Root.Find("stats"))
		{
			if (!Stats->IsObject())
			{
				Problems += "item: \"stats\" should be an object\n";
			}
			else
			{
				for (const auto& Member : Stats->Object)
				{
					if (Member.first == "jump")
					{
						WholeIn(Member.second, 0, Items::StepCap, "stats: \"jump\"", Problems, Out.Jump);
						continue;
					}
					const EStat Stat = StatFromName(Member.first);
					if (Stat == EStat::Count)
					{
						Problems += "stats: no stat called \"" + Member.first + "\"\n";
						continue;
					}
					const int Limit = StatLimit(Stat);
					WholeIn(Member.second, Stat == EStat::Hp ? -50 : -Limit, Limit, "stats: \"" + Member.first + "\"",
						Problems, Out.Stats[static_cast<int>(Stat)]);
				}
			}
		}
		if (const FJson* Bonus = Root.Find("bonus"))
		{
			if (!Bonus->IsObject())
			{
				Problems += "item: \"bonus\" should be an object\n";
			}
			else
			{
				for (const auto& Member : Bonus->Object)
				{
					if (!KnownKey(Member.first, BonusKeys))
					{
						Problems += "bonus: no bonus called \"" + Member.first + "\"\n";
					}
				}
				auto Number = [&](const char* Key, int High, int& Into)
				{
					if (const FJson* Value = Bonus->Find(Key))
					{
						WholeIn(*Value, 0, High, std::string("bonus: \"") + Key + "\"", Problems, Into);
					}
				};
				Number("damage_flat", 30, Out.DamageFlat);
				Number("damage_percent", 50, Out.DamagePercent);
				Number("heal_flat", 30, Out.HealFlat);
				Number("heal_percent", 50, Out.HealPercent);
				Number("tg_percent", Items::TgPercentCap, Out.TgPercent);
				if (const FJson* Scale = Bonus->Find("scale"))
				{
					if (Scale->IsString() && Scale->String == "any")
					{
						Out.Scale = EItemScale::Any;
					}
					else if (Scale->IsString() && Scale->String == "att")
					{
						Out.Scale = EItemScale::Att;
					}
					else if (Scale->IsString() && Scale->String == "mag")
					{
						Out.Scale = EItemScale::Mag;
					}
					else
					{
						Problems += "bonus: \"scale\" should be any, att or mag\n";
					}
				}
			}
		}
		if (const FJson* Effects = Root.Find("effects"))
		{
			if (!Effects->IsObject())
			{
				Problems += "item: \"effects\" should be an object\n";
			}
			else
			{
				for (const auto& Member : Effects->Object)
				{
					if (!KnownKey(Member.first, EffectKeys))
					{
						Problems += "effects: no effect called \"" + Member.first + "\"\n";
					}
				}
				auto Number = [&](const char* Key, int Low, int High, int& Into)
				{
					if (const FJson* Value = Effects->Find(Key))
					{
						WholeIn(*Value, Low, High, std::string("effects: \"") + Key + "\"", Problems, Into);
					}
				};
				auto Flag = [&](const char* Key, bool& Into)
				{
					if (const FJson* Value = Effects->Find(Key))
					{
						if (Value->Type != FJson::EType::Bool)
						{
							Problems += std::string("effects: \"") + Key + "\" should be true or false\n";
						}
						else
						{
							Into = Value->Bool;
						}
					}
				};
				Number("vs_monsters_percent", 0, 50, Out.VsMonstersPercent);
				Number("high_ground_percent", 0, 30, Out.HighGroundPercent);
				Number("still_defence", 0, 10, Out.StillDefence);
				Number("alone_percent", 0, 30, Out.AlonePercent);
				Number("alone_move", 0, 2, Out.AloneMove);
				Number("low_health_percent", 0, 40, Out.LowHealthPercent);
				Number("low_health_evasion", 0, 15, Out.LowHealthEvasion);
				Number("unseen_evasion", 0, 15, Out.UnseenEvasion);
				Number("lifesteal_percent", 0, 40, Out.LifestealPercent);
				Number("thorns", 0, 20, Out.Thorns);
				Number("kill_tg_percent", 0, 50, Out.KillTgPercent);
				Number("damage_taken_percent", 0, 30, Out.DamageTakenPercent);
				Number("heal_taken_percent", -75, 50, Out.HealTakenPercent);
				Flag("first_strike", Out.bFirstStrike);
				Flag("phoenix", Out.bPhoenix);
				Flag("calms_monsters", Out.bCalmsMonsters);
				Flag("steady", Out.bSteady);
				Flag("shrine_key", Out.bShrineKey);
			}
		}
		if (const FJson* Ability = Root.Find("ability"))
		{
			FAbility Read;
			const std::string Bad = ReadAbilityObject(*Ability, "ability", Read);
			if (!Bad.empty())
			{
				Problems += Bad + "\n";
			}
			else if (Read.Kind != "active")
			{
				Problems += "ability: an item's ability is an active one\n";
			}
			else
			{
				Out.AbilityId = Read.Id;
				PendingAbility() = Read;
			}
		}
		Out.bFromFile = true;
		return Problems;
	}

	std::string LoadItemFile(const std::string& Text)
	{
		FItemDef Item;
		const std::string Problems = ReadItemFile(Text, Item);
		if (!Problems.empty())
		{
			return Problems;
		}
		if (!Item.AbilityId.empty())
		{
			if (FindItem(Item.Id))
			{
				return "There is already an item called \"" + Item.Id + "\".";
			}
			const std::string Refused = RegisterAbility(PendingAbility());
			if (!Refused.empty())
			{
				return Item.Id + ": " + Refused;
			}
			ItemAbilities().push_back(Item.AbilityId);
		}
		return RegisterItem(Item);
	}

	int LoadoutCost(const FUnit& Unit)
	{
		return SumGear(Unit, [](const FItemDef& Item) { return Item.Cost; });
	}

	// ------------------------------------------------------------------ units

	int FUnit::ItemStat(EStat Which) const
	{
		const int Index = static_cast<int>(Which);
		const int Total = SumGear(*this, [Index](const FItemDef& Item) { return Item.Stats[Index]; });
		switch (Which)
		{
		case EStat::AEva: case EStat::MEva: case EStat::Crit:
			return std::min(Items::ChanceCap, Total);
		case EStat::Move: case EStat::Speed:
			return std::min(Items::StepCap, Total);
		default:
			return Total;
		}
	}

	int FUnit::ItemDamageFlat(bool bAttScale) const
	{
		return SumGear(*this, [bAttScale](const FItemDef& Item) { return Reaches(Item, bAttScale) ? Item.DamageFlat : 0; });
	}

	int FUnit::ItemDamagePercent(bool bAttScale) const
	{
		return std::min(Items::DamagePercentCap,
			SumGear(*this, [bAttScale](const FItemDef& Item) { return Reaches(Item, bAttScale) ? Item.DamagePercent : 0; }));
	}

	int FUnit::ItemHealFlat(bool bAttScale) const
	{
		return SumGear(*this, [bAttScale](const FItemDef& Item) { return Reaches(Item, bAttScale) ? Item.HealFlat : 0; });
	}

	int FUnit::ItemHealPercent(bool bAttScale) const
	{
		return std::min(Items::HealPercentCap,
			SumGear(*this, [bAttScale](const FItemDef& Item) { return Reaches(Item, bAttScale) ? Item.HealPercent : 0; }));
	}

	int FUnit::ItemTgPercent() const
	{
		return std::min(Items::TgPercentCap, SumGear(*this, [](const FItemDef& Item) { return Item.TgPercent; }));
	}

	int FUnit::ItemJump() const
	{
		return std::min(Items::StepCap, SumGear(*this, [](const FItemDef& Item) { return Item.Jump; }));
	}

	bool FUnit::Carries(const std::string& ItemId) const
	{
		for (const FItemDef* Item : Gear)
		{
			if (Item && Item->Id == ItemId)
			{
				return true;
			}
		}
		return false;
	}

	// --------------------------------------------------------- the computer

	std::vector<std::string> SuggestLoadout(const std::string& JobId, int Points, const std::vector<std::string>& Taken)
	{
		// What each kind of class values in an item, as a score per point of
		// cost. Simple on purpose: it has to be the same on every machine, and
		// a person can always choose better by hand.
		const bool bTank = JobHasRole(JobId, "tank");
		const bool bSupport = JobHasRole(JobId, "support");
		const bool bDamage = JobHasRole(JobId, "damage") || (!bTank && !bSupport);
		auto Worth = [&](const FItemDef& Item)
		{
			double Score = 0.0;
			Score += Item.Stats[static_cast<int>(EStat::Hp)] * (bTank ? 0.35 : 0.15);
			Score += (Item.Stats[static_cast<int>(EStat::AttDef)] + Item.Stats[static_cast<int>(EStat::MagDef)]) * (bTank ? 2.0 : 0.8);
			Score += (Item.Stats[static_cast<int>(EStat::AEva)] + Item.Stats[static_cast<int>(EStat::MEva)]) * 0.6;
			Score += Item.Stats[static_cast<int>(EStat::Crit)] * (bDamage ? 0.9 : 0.2);
			Score += Item.Stats[static_cast<int>(EStat::Speed)] * 9.0;
			Score += Item.Stats[static_cast<int>(EStat::Move)] * 4.0;
			Score += Item.Stats[static_cast<int>(EStat::Sight)] * 0.5;
			Score += Item.Stats[static_cast<int>(EStat::Patience)] * 0.3;
			Score += (Item.DamageFlat * 1.0 + Item.DamagePercent * 0.5) * (bDamage ? 1.0 : 0.3);
			Score += (Item.HealFlat * 0.8 + Item.HealPercent * 0.4) * (bSupport ? 1.0 : 0.0);
			Score += Item.TgPercent * 0.6;
			Score += Item.Jump * 2.0;
			return Score;
		};
		std::vector<std::string> Out;
		std::vector<std::string> Held = Taken;
		int Left = Points;
		while (static_cast<int>(Out.size()) < Items::Slots)
		{
			const FItemDef* Best = nullptr;
			double BestScore = 0.0;
			for (const FItemDef* Item : AllItems())
			{
				if (Item->Cost <= 0 || Item->Cost > Left || std::find(Held.begin(), Held.end(), Item->Id) != Held.end())
				{
					continue;
				}
				// Score per point, then the dearer one, then the id: never a tie.
				const double Score = Worth(*Item) / Item->Cost;
				if (!Best || Score > BestScore + 1e-9
					|| (std::fabs(Score - BestScore) <= 1e-9 && (Item->Cost > Best->Cost || (Item->Cost == Best->Cost && Item->Id < Best->Id))))
				{
					Best = Item;
					BestScore = Score;
				}
			}
			if (!Best || BestScore <= 0.0)
			{
				break;
			}
			Out.push_back(Best->Id);
			Held.push_back(Best->Id);
			Left -= Best->Cost;
		}
		return Out;
	}
}
