// The ground a battle is fought on.
//
// A map is written out as rows of characters, one per tile: a digit is that
// height, and a few characters mean particular ground. A map is mirrored to
// make it, so neither side gets the better half of it.
//
// Units do not walk on tiles. Each tile is divided into a grid of navigation
// nodes four to a side, and a unit stands on a node -- so movement is smooth
// across the board while height, cover and hazards stay per tile.

#pragma once

#include "SimTypes.h"

#include <string>
#include <vector>

namespace TMSim
{
	namespace Ground
	{
		/** Metres to a tile. */
		inline constexpr float TileSize = 2.0f;
		/** Metres to a navigation node. */
		inline constexpr float NavStep = 0.5f;
		inline constexpr int NodesPerTile = 4;
		/** Cost of a diagonal step, as Godot writes it. */
		inline constexpr double NavDiagonal = 0.7071067811865476;
		/** Metres of height to a level. */
		inline constexpr float LevelHeight = 0.7f;
		/** Closest two units may stand; enemies block paths this close to them. */
		inline constexpr float UnitSpacing = 0.9f;
		/** Height levels a unit may cross in one step. */
		inline constexpr int Jump = 2;
		/** What a flier may cross instead: any of them. */
		inline constexpr int FlyJump = 999;
		/**
		 * How near a spot a unit has to be to be caught by something aimed at
		 * it. A single-target ability sweeps up everyone inside this and then
		 * keeps only the closest, which is what lets a click land on a unit
		 * without being exactly on its feet.
		 */
		inline constexpr float HitRadius = 0.6f;
		/** Past this, an ability is not swinging at arm's length any more. */
		inline constexpr float MeleeRange = 1.8f;
	}

	/** A spot on the navigation grid. */
	struct FNode
	{
		int X = 0;
		int Y = 0;

		bool operator==(const FNode& Other) const { return X == Other.X && Y == Other.Y; }
		bool operator!=(const FNode& Other) const { return !(*this == Other); }
	};

	class TMSIM_API FMap
	{
	public:
		/**
		 * Builds a map from the top half of it, mirrored. This is how the maps
		 * are written: only half is given, so the two sides are the same ground.
		 */
		void BuildMirrored(const std::vector<std::string>& TopRows);

		/** Builds from rows that are already the whole map. */
		void Build(const std::vector<std::string>& Rows);

		int TilesX = 0;
		int TilesY = 0;
		int NavX = 0;
		int NavY = 0;

		/** Height level of each tile. Level 0 cannot be walked on at all. */
		std::vector<int> Heights;
		/** -1 burns a unit that starts its turn on it, +1 heals. */
		std::vector<int> Hazards;
		/** 1 where the ground blocks sight as well as movement. */
		std::vector<int> Covers;
		/** The height of every navigation node, worked out once. */
		std::vector<int> NavLevels;

		int TileLevel(int TileX, int TileY) const;
		int NodeLevel(const FNode& Node) const;
		bool NodeWalkable(const FNode& Node) const;

		/** The node a point in metres falls on. */
		static FNode NodeOf(const FVec2& Point);
		/** The centre of a node, in metres. */
		static FVec2 NodePos(const FNode& Node);
		/** The nearest node centre to a point: where a unit actually stands. */
		static FVec2 Snap(const FVec2& Point) { return NodePos(NodeOf(Point)); }

		int NodeIndex(const FNode& Node) const { return Node.Y * NavX + Node.X; }
		FNode NodeAt(int Index) const { return FNode{ Index % NavX, Index / NavX }; }

		FVec2 SizeMeters() const { return FVec2(TilesX * Ground::TileSize, TilesY * Ground::TileSize); }

		/** The height level a character in a map row means. */
		static int CharLevel(char Ch);
		/** -1 burns, +1 heals, 0 for ordinary ground. */
		static int CharHazard(char Ch);
		static bool CharIsCover(char Ch);
	};

	/** Highlands: rolling hills with a high ridge on each flank. */
	TMSIM_API const std::vector<std::string>& HighlandsRows();

	/**
	 * A map as the game offers it: the top half of its ground (the rest is that
	 * turned about, as BuildMirrored does), where blue's four stand (red stands
	 * at the same spots turned about), and the look it is shown with.
	 *
	 * The five Godot maps are written in map_data.gd; Highlands is built in here
	 * exactly as Godot has it, and further maps are files
	 * (Content/Data/Maps/<id>.tmmap.json) read by ReadMapFile.
	 */
	struct FMapDef
	{
		std::string Id;
		std::string Name;
		std::string Desc;
		/** The look the view dresses it in, unless the player picks another. Never read by the rules. */
		std::string Theme;
		std::vector<std::string> Top;
		/** Blue's starting spots in metres, on navigation node centres. */
		std::vector<FVec2> Spawns;
	};

	/**
	 * Reads a map file. "" if it is a good map, otherwise every problem found,
	 * a line each. A map is refused if any blue spawn cannot walk to every red
	 * one: a battle on it could never be decided.
	 */
	TMSIM_API std::string ReadMapFile(const std::string& Text, FMapDef& Out);
	/** Checks a map however it was made. "" if it is good. */
	TMSIM_API std::string CheckMap(const FMapDef& Map);
	/** Adds a map, or replaces one of the same id. "" if it was taken. */
	TMSIM_API std::string RegisterMap(const FMapDef& Map);
	/** A map by id, Highlands for one not known. */
	TMSIM_API const FMapDef& FindMap(const std::string& Id);
	TMSIM_API bool HasMap(const std::string& Id);
	/** Every map, Highlands first, then in the order they were added. */
	TMSIM_API const std::vector<FMapDef>& AllMaps();
}
