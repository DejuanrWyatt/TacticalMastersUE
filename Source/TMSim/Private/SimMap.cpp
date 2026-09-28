#include "SimMap.h"

#include "SimJson.h"

#include <cstdlib>

#include <algorithm>
#include <cmath>

namespace TMSim
{
	int FMap::CharLevel(char Ch)
	{
		switch (Ch)
		{
		case '~': return 0;  // water, too deep to wade through
		case '#': return 0;  // rock, blocks sight as well as movement
		case 'x': return 1;  // embers
		case '+': return 1;  // spring
		default:
			// Anything else is a digit saying how high that ground is.
			return (Ch >= '0' && Ch <= '9') ? (Ch - '0') : 0;
		}
	}

	int FMap::CharHazard(char Ch)
	{
		if (Ch == 'x') return -1;
		if (Ch == '+') return 1;
		return 0;
	}

	bool FMap::CharIsCover(char Ch)
	{
		return Ch == '#';
	}

	void FMap::BuildMirrored(const std::vector<std::string>& TopRows)
	{
		// The far half is the near half turned around, so neither side is given
		// the better ground.
		std::vector<std::string> All = TopRows;
		for (auto It = TopRows.rbegin(); It != TopRows.rend(); ++It)
		{
			std::string Reversed = *It;
			std::reverse(Reversed.begin(), Reversed.end());
			All.push_back(Reversed);
		}
		Build(All);
	}

	void FMap::Build(const std::vector<std::string>& Rows)
	{
		TilesY = static_cast<int>(Rows.size());
		TilesX = TilesY > 0 ? static_cast<int>(Rows[0].size()) : 0;
		NavX = TilesX * Ground::NodesPerTile;
		NavY = TilesY * Ground::NodesPerTile;

		Heights.clear();
		Hazards.clear();
		Covers.clear();
		Heights.reserve(TilesX * TilesY);
		for (const std::string& Row : Rows)
		{
			for (char Ch : Row)
			{
				Heights.push_back(CharLevel(Ch));
				Hazards.push_back(CharHazard(Ch));
				Covers.push_back(CharIsCover(Ch) ? 1 : 0);
			}
		}

		// Every node's height, worked out once: the pathfinder asks for this
		// constantly and the ground never changes.
		NavLevels.assign(NavX * NavY, 0);
		for (int Y = 0; Y < NavY; ++Y)
		{
			for (int X = 0; X < NavX; ++X)
			{
				NavLevels[Y * NavX + X] = NodeLevel(FNode{ X, Y });
			}
		}
	}

	int FMap::TileLevel(int TileX, int TileY) const
	{
		if (TileX < 0 || TileY < 0 || TileX >= TilesX || TileY >= TilesY)
		{
			return 0;
		}
		return Heights[TileY * TilesX + TileX];
	}

	int FMap::NodeLevel(const FNode& Node) const
	{
		// Floor division, and it has to stay floor for negatives: a node just
		// off the edge belongs to the tile off the edge, not to tile zero.
		const int TileX = static_cast<int>(std::floor(static_cast<double>(Node.X) / Ground::NodesPerTile));
		const int TileY = static_cast<int>(std::floor(static_cast<double>(Node.Y) / Ground::NodesPerTile));
		return TileLevel(TileX, TileY);
	}

	bool FMap::NodeWalkable(const FNode& Node) const
	{
		return Node.X >= 0 && Node.Y >= 0 && Node.X < NavX && Node.Y < NavY && NodeLevel(Node) > 0;
	}

	FNode FMap::NodeOf(const FVec2& Point)
	{
		return FNode{
			static_cast<int>(std::floor(Point.X / Ground::NavStep)),
			static_cast<int>(std::floor(Point.Y / Ground::NavStep))
		};
	}

	FVec2 FMap::NodePos(const FNode& Node)
	{
		return FVec2((Node.X + 0.5f) * Ground::NavStep, (Node.Y + 0.5f) * Ground::NavStep);
	}

	namespace
	{
		std::vector<FMapDef>& Maps()
		{
			static std::vector<FMapDef> List;
			if (List.empty())
			{
				// Highlands, as map_data.gd has it, with WEST_SPAWNS.
				FMapDef Highlands;
				Highlands.Id = "highlands";
				Highlands.Name = "Highlands";
				Highlands.Desc = "Rolling hills with a high ridge on each flank. Take the high ground for bonus damage.";
				Highlands.Theme = "meadow";
				Highlands.Top = HighlandsRows();
				Highlands.Spawns = { FVec2(2.75f, 4.75f), FVec2(0.75f, 8.75f), FVec2(4.75f, 6.75f), FVec2(2.75f, 10.75f) };
				List.push_back(Highlands);
			}
			return List;
		}

		bool GoodId(const std::string& Id)
		{
			if (Id.empty() || Id.size() > 40)
			{
				return false;
			}
			for (char Ch : Id)
			{
				if (!((Ch >= 'a' && Ch <= 'z') || (Ch >= '0' && Ch <= '9') || Ch == '_'))
				{
					return false;
				}
			}
			return true;
		}

		/** The tile a point in metres is on, as an index into a map's tiles. */
		int TileOf(const FMap& Map, const FVec2& Point)
		{
			const FNode Node = FMap::NodeOf(Point);
			return (Node.Y / Ground::NodesPerTile) * Map.TilesX + Node.X / Ground::NodesPerTile;
		}
	}

	std::string CheckMap(const FMapDef& Def)
	{
		std::string Problems;
		auto Say = [&Problems](const std::string& What) { Problems += What + "\n"; };
		if (!GoodId(Def.Id))
		{
			Say("id: lowercase letters, digits and _ only, up to 40");
		}
		if (Def.Name.empty())
		{
			Say("name: missing");
		}
		// Big enough to manoeuvre on, small enough for the pathfinder to stay quick.
		if (Def.Top.size() < 3 || Def.Top.size() > 20)
		{
			Say("top: 3 to 20 rows (the map is twice that tall)");
			return Problems;
		}
		const size_t Width = Def.Top[0].size();
		if (Width < 8 || Width > 40)
		{
			Say("top: rows 8 to 40 tiles wide");
			return Problems;
		}
		for (size_t Row = 0; Row < Def.Top.size(); ++Row)
		{
			if (Def.Top[Row].size() != Width)
			{
				Say("top: row " + std::to_string(Row + 1) + " is not as wide as the first");
				return Problems;
			}
			for (char Ch : Def.Top[Row])
			{
				if (!((Ch >= '1' && Ch <= '9') || Ch == '~' || Ch == '#' || Ch == 'x' || Ch == '+'))
				{
					Say(std::string("top: '") + Ch + "' in row " + std::to_string(Row + 1)
						+ " is not ground (1-9 a height, ~ water, # rock, x embers, + spring)");
					return Problems;
				}
			}
		}

		FMap Map;
		Map.BuildMirrored(Def.Top);
		const FVec2 Size = Map.SizeMeters();
		if (Def.Spawns.size() != 4)
		{
			Say("spawns: four, one for each unit a side fields");
			return Problems;
		}
		for (size_t i = 0; i < Def.Spawns.size(); ++i)
		{
			const FVec2 Spot = Def.Spawns[i];
			if (Spot.X < 0.0f || Spot.Y < 0.0f || Spot.X >= Size.X || Spot.Y >= Size.Y
				|| !Map.NodeWalkable(FMap::NodeOf(Spot)))
			{
				Say("spawns: " + std::to_string(i + 1) + " is not on ground a unit can stand on");
				continue;
			}
			for (size_t j = 0; j < Def.Spawns.size(); ++j)
			{
				// Red stands at blue's spots turned about.
				const FVec2 Theirs(Size.X - Def.Spawns[j].X, Size.Y - Def.Spawns[j].Y);
				if ((j > i && Spot.DistanceTo(Def.Spawns[j]) < Ground::UnitSpacing) || Spot.DistanceTo(Theirs) < 4.0f)
				{
					Say("spawns: " + std::to_string(i + 1) + " is too close to another unit's");
				}
			}
		}
		if (!Problems.empty())
		{
			return Problems;
		}

		// Every blue spawn must be able to walk to every red one, a tile at a
		// time and never climbing more than a unit can (Ground::Jump).
		for (size_t i = 0; i < Def.Spawns.size(); ++i)
		{
			const int From = TileOf(Map, Def.Spawns[i]);
			std::vector<char> Seen(static_cast<size_t>(Map.TilesX) * Map.TilesY, 0);
			std::vector<int> Open = { From };
			Seen[From] = 1;
			for (size_t k = 0; k < Open.size(); ++k)
			{
				const int X = Open[k] % Map.TilesX;
				const int Y = Open[k] / Map.TilesX;
				const int Steps[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
				for (const auto& Step : Steps)
				{
					const int NX = X + Step[0];
					const int NY = Y + Step[1];
					if (NX < 0 || NY < 0 || NX >= Map.TilesX || NY >= Map.TilesY)
					{
						continue;
					}
					const int Next = NY * Map.TilesX + NX;
					const int There = Map.TileLevel(NX, NY);
					if (!Seen[Next] && There > 0 && std::abs(There - Map.TileLevel(X, Y)) <= Ground::Jump)
					{
						Seen[Next] = 1;
						Open.push_back(Next);
					}
				}
			}
			for (size_t j = 0; j < Def.Spawns.size(); ++j)
			{
				if (!Seen[TileOf(Map, FVec2(Size.X - Def.Spawns[j].X, Size.Y - Def.Spawns[j].Y))])
				{
					Say("spawns: blue " + std::to_string(i + 1) + " cannot walk to red " + std::to_string(j + 1)
						+ ", so a battle might never be decided");
					return Problems;
				}
			}
		}
		return Problems;
	}

	std::string ReadMapFile(const std::string& Text, FMapDef& Out)
	{
		FJson Json;
		const std::string Bad = ParseJson(Text, Json);
		if (!Bad.empty())
		{
			return "not JSON: " + Bad;
		}
		if (!Json.IsObject())
		{
			return "a map file is one object";
		}
		const FJson* Format = Json.Find("format");
		if (!Format || !Format->IsString() || Format->String != "tactical-masters-map")
		{
			return "format: must be \"tactical-masters-map\"";
		}
		const FJson* Version = Json.Find("version");
		if (!Version || !Version->IsNumber() || Version->Number != 1.0)
		{
			return "version: must be 1";
		}
		static const char* const Keys[] = { "format", "version", "id", "name", "desc", "theme", "top", "spawns" };
		std::string Problems;
		for (const auto& Member : Json.Object)
		{
			bool bKnown = false;
			for (const char* Key : Keys)
			{
				bKnown = bKnown || Member.first == Key;
			}
			if (!bKnown)
			{
				Problems += "unknown key \"" + Member.first + "\"\n";
			}
		}
		auto TextOf = [&Json](const char* Key)
		{
			const FJson* Found = Json.Find(Key);
			return Found && Found->IsString() ? Found->String : std::string();
		};
		Out = FMapDef();
		Out.Id = TextOf("id");
		Out.Name = TextOf("name");
		Out.Desc = TextOf("desc");
		Out.Theme = TextOf("theme");
		if (const FJson* Top = Json.Find("top"); Top && Top->IsArray())
		{
			for (const FJson& Row : Top->Array)
			{
				Out.Top.push_back(Row.IsString() ? Row.String : std::string());
			}
		}
		if (const FJson* Spawns = Json.Find("spawns"); Spawns && Spawns->IsArray())
		{
			for (const FJson& Spot : Spawns->Array)
			{
				if (Spot.IsArray() && Spot.Array.size() == 2 && Spot.Array[0].IsNumber() && Spot.Array[1].IsNumber())
				{
					// Where a unit can actually stand: the middle of a navigation node.
					Out.Spawns.push_back(FMap::Snap(FVec2(static_cast<float>(Spot.Array[0].Number), static_cast<float>(Spot.Array[1].Number))));
				}
				else
				{
					Problems += "spawns: each is [x, y] in metres\n";
				}
			}
		}
		return Problems + CheckMap(Out);
	}

	std::string RegisterMap(const FMapDef& Def)
	{
		// Highlands is Godot's, and the parity tests stand on it.
		if (Def.Id == "highlands")
		{
			return "id: highlands is built in";
		}
		const std::string Problems = CheckMap(Def);
		if (!Problems.empty())
		{
			return Problems;
		}
		std::vector<FMapDef>& List = Maps();
		for (FMapDef& Existing : List)
		{
			if (Existing.Id == Def.Id)
			{
				Existing = Def;
				return std::string();
			}
		}
		List.push_back(Def);
		return std::string();
	}

	const FMapDef& FindMap(const std::string& Id)
	{
		for (const FMapDef& Def : Maps())
		{
			if (Def.Id == Id)
			{
				return Def;
			}
		}
		return Maps().front();
	}

	bool HasMap(const std::string& Id)
	{
		for (const FMapDef& Def : Maps())
		{
			if (Def.Id == Id)
			{
				return true;
			}
		}
		return false;
	}

	const std::vector<FMapDef>& AllMaps()
	{
		return Maps();
	}

	const std::vector<std::string>& HighlandsRows()
	{
		static const std::vector<std::string> Rows =
		{
			"112233211111",
			"112233211111",
			"111222111111",
			"111111111111",
			"122111121111",
			"123111121111",
		};
		return Rows;
	}
}
