#include "SimMap.h"

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
