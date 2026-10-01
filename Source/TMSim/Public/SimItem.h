// Items: what a unit carries into a battle, or picks up in one.
//
// Docs/design/feat-neutral-camps.md (and feat-items.md for the formulas). A unit
// has three open slots. An item adds to the class's stats, raises the damage or
// healing its abilities do (flat, then percent), makes its Turn Gauge fill
// faster, or lets it climb higher. Items come from files in Content/Data/Items,
// made like class files and read as strictly: an unknown key or a number out of
// range is refused with the reason.
//
// A unit carrying nothing is exactly the unit it was before items existed: every
// formula below reduces to the old one bit for bit when every sum is zero, which
// is what keeps the Godot parity tests passing.

#pragma once

#include "SimTypes.h"

#include <string>
#include <vector>

namespace TMSim
{
	struct FUnit;

	enum class EItemTier : uint8_t { Common, Uncommon, Rare, Epic, Count };

	/** Which of its user's abilities an item's power bonus reaches. */
	enum class EItemScale : uint8_t { Any, Att, Mag };

	struct FItemDef
	{
		std::string Id;
		std::string Name;
		/** What it does, in words, for tooltips and the guide. Never read by the rules. */
		std::string Desc;
		/** Its icon's name, for the view. Never read by the rules. */
		std::string Icon;
		EItemTier Tier = EItemTier::Common;
		/** Points it costs on the setup screen; 0 means it can't be bought, only found. */
		int Cost = 0;

		/** Added to the class's own stats. May be negative on a trade-off item. */
		int Stats[StatCount] = { 0 };
		/** Added to the power of the user's damage abilities, then that total raised by a percent. */
		int DamageFlat = 0;
		int DamagePercent = 0;
		/** Likewise for healing. */
		int HealFlat = 0;
		int HealPercent = 0;
		EItemScale Scale = EItemScale::Any;
		/** Percent faster the Turn Gauge fills. */
		int TgPercent = 0;
		/** Extra height levels it can climb in one step. */
		int Jump = 0;

		/** An ability of its own, used from an item slot (5 to 7), or empty. Registered with the item. */
		std::string AbilityId;
		/** Dropped only by this boss (its class id), never by another camp; empty for any. */
		std::string Boss;

		// Effects that only count sometimes, or happen on something
		// (Docs/design/feat-neutral-camps.md 14). All 0 or false on a plain item.

		/** Percent more damage to monsters. */
		int VsMonstersPercent = 0;
		/** Percent more ability damage while above its target. */
		int HighGroundPercent = 0;
		/** AttDef and MagDef while it didn't walk on its last turn. */
		int StillDefence = 0;
		/** Percent more damage, and Move, while no ally is within Items::AloneReach. */
		int AlonePercent = 0;
		int AloneMove = 0;
		/** Below Items::LowHealthShare of its health: percent more damage and this much A-Eva. */
		int LowHealthPercent = 0;
		int LowHealthEvasion = 0;
		/** A-Eva and M-Eva while unseen by the other side as its turn began. */
		int UnseenEvasion = 0;
		/** Percent of the damage its abilities deal that heals it. */
		int LifestealPercent = 0;
		/** Damage back to whoever hits it at arm's length. */
		int Thorns = 0;
		/** Percent of its gauge back for each unit or monster it knocks out. */
		int KillTgPercent = 0;
		/** Its first damaging hit of the battle is critical. */
		bool bFirstStrike = false;
		/** Once a battle, a blow that would knock it out leaves it at 1 health. */
		bool bPhoenix = false;
		/** Percent more damage it takes (a trade-off; may be only positive). */
		int DamageTakenPercent = 0;
		/** Percent change to healing its allies give it (negative on a trade-off). */
		int HealTakenPercent = 0;
		/** Docile and provoked monsters take no notice of it. */
		bool bCalmsMonsters = false;
		/** Can't be knocked down. */
		bool bSteady = false;
		/** Uses a shrine without its rest. */
		bool bShrineKey = false;

		/** True for one read from a file (every item is, today). */
		bool bFromFile = false;
	};

	namespace Items
	{
		/** Slots per unit. */
		inline constexpr int Slots = 3;
		/** Metres within which an ally stops a unit counting as alone. */
		inline constexpr float AloneReach = 4.0f;
		/** Share of its health below which a unit is "low". */
		inline constexpr double LowHealthShare = 0.3;
		/** The most the items (and anything else that adds to them) may give, all together. */
		inline constexpr int DamagePercentCap = 60;
		inline constexpr int HealPercentCap = 60;
		inline constexpr int TgPercentCap = 30;
		/** The most items may add to evasion and crit chance, each. */
		inline constexpr int ChanceCap = 20;
		/** The most items may add to Move, Speed and Jump, each. */
		inline constexpr int StepCap = 2;
	}

	/** The name a file uses for a tier: common, uncommon, rare, epic. */
	TMSIM_API const char* ItemTierName(EItemTier Tier);
	/** What an item of this tier costs on the setup screen when its file doesn't say. */
	TMSIM_API int ItemTierCost(EItemTier Tier);

	/** Null if nothing is registered under that id. */
	TMSIM_API const FItemDef* FindItem(const std::string& ItemId);
	/** Every item the game knows, by tier and then by id. */
	TMSIM_API const std::vector<const FItemDef*>& AllItems();
	/** Adds an item. "" when added, otherwise why not (an id already in use). */
	TMSIM_API std::string RegisterItem(const FItemDef& Item);
	/** Forgets every item. For tests: units still holding one must be gone first. */
	TMSIM_API void ForgetLoadedItems();

	inline constexpr const char* ItemFileFormat = "tactical-masters-item";
	inline constexpr int ItemFileVersion = 1;

	/** Reads one item file. "" on success; otherwise every reason it can't be used, one per line. */
	TMSIM_API std::string ReadItemFile(const std::string& Text, FItemDef& OutItem);
	/** Reads an item file and registers it. "" if the item is now in the game. */
	TMSIM_API std::string LoadItemFile(const std::string& Text);

	/** The sum of one number over a unit's items, for the effects above. */
	TMSIM_API int GearSum(const FUnit& Unit, int FItemDef::* Member);
	/** Whether any item it carries has that flag. */
	TMSIM_API bool GearHas(const FUnit& Unit, bool FItemDef::* Member);

	/** What a loadout costs in setup points. */
	TMSIM_API int LoadoutCost(const FUnit& Unit);

	/**
	 * Items the computer takes for a unit of this class with this many points,
	 * best first: a tank armours up, a damage class sharpens its hits, a support
	 * heals harder or acts sooner. Deterministic, so both machines of a match
	 * agree. Ids of items it may not take again (already chosen) are skipped.
	 */
	TMSIM_API std::vector<std::string> SuggestLoadout(const std::string& JobId, int Points,
		const std::vector<std::string>& Taken = {});
}
