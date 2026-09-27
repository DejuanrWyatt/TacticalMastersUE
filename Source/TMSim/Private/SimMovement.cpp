// Walking: where a unit can get to, how it gets there, and moving it.
//
// Ported from game_state.gd. The pathfinder is Dijkstra over the navigation
// grid, and it is written the same way as the original rather than the way a
// C++ programmer might reach for -- flat arrays, an explicit binary heap, the
// same order of comparisons. That is deliberate: two paths of equal length are
// equally correct, but only one of them matches Godot, and a replay that picks
// the other one is a replay that goes wrong.

#include "SimBattle.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace TMSim
{
	namespace
	{
		constexpr double Infinity = std::numeric_limits<double>::infinity();

		/** The eight ways out of a node, in the order the original tries them. */
		constexpr int DirX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
		constexpr int DirY[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
	}

	double FBattle::MoveOf(const FUnit& Unit, bool bSprint) const
	{
		const double Metres = Unit.Stat(EStat::Move) * Tuning.MoveMultiplier * Unit.MoveFactor();
		return bSprint ? Metres * Tuning.SprintMultiplier : Metres;
	}

	int FBattle::JumpOf(const FUnit& Unit) const
	{
		return Unit.Flies() ? Ground::FlyJump : Ground::Jump;
	}

	void FBattle::HeapSwap(int A, int B)
	{
		std::swap(HeapCost[A], HeapCost[B]);
		std::swap(HeapNode[A], HeapNode[B]);
	}

	void FBattle::HeapPush(double InCost, int Node)
	{
		HeapCost.push_back(InCost);
		HeapNode.push_back(Node);
		int i = static_cast<int>(HeapCost.size()) - 1;
		while (i > 0)
		{
			const int Up = (i - 1) >> 1;
			if (HeapCost[Up] <= HeapCost[i])
			{
				break;
			}
			HeapSwap(i, Up);
			i = Up;
		}
	}

	void FBattle::HeapPop()
	{
		const int Last = static_cast<int>(HeapCost.size()) - 1;
		HeapSwap(0, Last);
		HeapCost.resize(Last);
		HeapNode.resize(Last);
		int i = 0;
		while (true)
		{
			const int L = i * 2 + 1;
			const int R = L + 1;
			int Smallest = i;
			if (L < Last && HeapCost[L] < HeapCost[Smallest]) { Smallest = L; }
			if (R < Last && HeapCost[R] < HeapCost[Smallest]) { Smallest = R; }
			if (Smallest == i)
			{
				break;
			}
			HeapSwap(i, Smallest);
			i = Smallest;
		}
	}

	void FBattle::MarkBlocked(const FVec2& Point, double Radius, std::vector<uint8_t>& Flags) const
	{
		const FNode Centre = FMap::NodeOf(Point);
		const int R = static_cast<int>(std::ceil(Radius / Ground::NavStep));
		for (int dy = -R; dy <= R; ++dy)
		{
			for (int dx = -R; dx <= R; ++dx)
			{
				const FNode N{ Centre.X + dx, Centre.Y + dy };
				if (N.X < 0 || N.Y < 0 || N.X >= Map.NavX || N.Y >= Map.NavY)
				{
					continue;
				}
				if (FMap::NodePos(N).DistanceTo(Point) < Radius)
				{
					Flags[N.Y * Map.NavX + N.X] = 1;
				}
			}
		}
	}

	void FBattle::RunDijkstra(const std::vector<FNode>& Starts, double MaxCost, int Team, int Jump)
	{
		const int Count = Map.NavX * Map.NavY;

		std::vector<uint8_t> Blocked(Count, 0);
		// Nodes an enemy has engaged: stepping out of one costs extra movement.
		std::vector<uint8_t> Engaged(Count, 0);
		const double EngageRadius = Tuning.EngageRadius;
		const double EngageCost = Tuning.EngageCost;
		const bool bEngaging = Team >= 0 && EngageRadius > 0.0 && EngageCost > 0.0;
		if (Team >= 0)
		{
			for (const FUnit& Other : Units)
			{
				if (Other.IsAlive() && Other.Team != Team)
				{
					MarkBlocked(Other.Pos, Ground::UnitSpacing, Blocked);
					if (bEngaging)
					{
						MarkBlocked(Other.Pos, EngageRadius, Engaged);
					}
				}
			}
		}

		Cost.assign(Count, Infinity);
		Parent.assign(Count, -1);
		HeapCost.clear();
		HeapNode.clear();

		for (const FNode& S : Starts)
		{
			if (S.X >= 0 && S.Y >= 0 && S.X < Map.NavX && S.Y < Map.NavY)
			{
				const int Si = S.Y * Map.NavX + S.X;
				Cost[Si] = 0.0;
				HeapPush(0.0, Si);
			}
		}

		const double Diagonal = Ground::NavStep * Ground::NavDiagonal * 2.0;
		while (!HeapNode.empty())
		{
			const double C = HeapCost[0];
			const int i = HeapNode[0];
			HeapPop();
			if (C > Cost[i])
			{
				continue;
			}
			const int X = i % Map.NavX;
			const int Y = i / Map.NavX;
			const int Level = Map.NavLevels[i];

			for (int k = 0; k < 8; ++k)
			{
				const int Mx = X + DirX[k];
				const int My = Y + DirY[k];
				if (Mx < 0 || My < 0 || Mx >= Map.NavX || My >= Map.NavY)
				{
					continue;
				}
				const int M = My * Map.NavX + Mx;
				const int MLevel = Map.NavLevels[M];
				if (MLevel <= 0 || Blocked[M] == 1 || std::abs(MLevel - Level) > Jump)
				{
					continue;
				}

				double Step = Ground::NavStep;
				if (DirX[k] != 0 && DirY[k] != 0)
				{
					// No cutting corners past water, cliffs or enemies.
					const int A = Y * Map.NavX + Mx;
					const int B = My * Map.NavX + X;
					const int ALevel = Map.NavLevels[A];
					const int BLevel = Map.NavLevels[B];
					if (ALevel <= 0 || BLevel <= 0 || Blocked[A] == 1 || Blocked[B] == 1)
					{
						continue;
					}
					if (std::abs(ALevel - Level) > Jump || std::abs(BLevel - Level) > Jump)
					{
						continue;
					}
					Step = Diagonal;
				}

				double Next = C + Step;
				// Breaking away from an enemy costs; walking in doesn't.
				if (bEngaging && Engaged[i] == 1 && Engaged[M] == 0)
				{
					Next += EngageCost;
				}
				if (Next > MaxCost + 0.001 || Next >= Cost[M])
				{
					continue;
				}
				Cost[M] = Next;
				Parent[M] = i;
				HeapPush(Next, M);
			}
		}
	}

	std::vector<std::pair<FNode, double>> FBattle::ReachableNodes(const FUnit& Unit, bool bSprint)
	{
		RunDijkstra({ FMap::NodeOf(Unit.Pos) }, MoveOf(Unit, bSprint), Unit.Team, JumpOf(Unit));

		std::vector<std::pair<FNode, double>> Out;
		for (int i = 0; i < static_cast<int>(Cost.size()); ++i)
		{
			if (Cost[i] == Infinity)
			{
				continue;
			}
			const FNode N = Map.NodeAt(i);
			const FVec2 P = FMap::NodePos(N);

			// Walked through, but not stood on: two units cannot share a spot.
			bool bTaken = false;
			for (const FUnit& Other : Units)
			{
				if (&Other != &Unit && Other.IsAlive() && Other.Pos.DistanceTo(P) < Ground::UnitSpacing)
				{
					bTaken = true;
					break;
				}
			}
			if (!bTaken)
			{
				Out.push_back({ N, Cost[i] });
			}
		}
		return Out;
	}

	std::vector<FVec2> FBattle::PathTo(const FUnit& Unit, const FNode& To, bool bSprint)
	{
		RunDijkstra({ FMap::NodeOf(Unit.Pos) }, MoveOf(Unit, bSprint), Unit.Team, JumpOf(Unit));

		std::vector<FVec2> Path;
		const int Target = Map.NodeIndex(To);
		if (To.X < 0 || To.Y < 0 || To.X >= Map.NavX || To.Y >= Map.NavY || Cost[Target] == Infinity)
		{
			return Path;
		}
		int At = Target;
		while (true)
		{
			Path.insert(Path.begin(), FMap::NodePos(Map.NodeAt(At)));
			if (Parent[At] < 0)
			{
				break;
			}
			At = Parent[At];
		}
		return Path;
	}

	std::string FBattle::ValidateMove(int UnitId, const FVec2& To, bool bSprint)
	{
		FUnit* Unit = FindUnit(UnitId);
		if (!Unit || !Unit->IsAlive())
		{
			return "No such unit.";
		}
		if (!Unit->bReady)
		{
			return "It isn't ready.";
		}
		if (Unit->IsStunned())
		{
			return "It can't act.";
		}
		if (Unit->IsCasting())
		{
			// Committed. A cast is begun standing still and finished standing still,
			// so the choice is walk-then-cast or cast and stay where you are.
			return "Can't move while casting.";
		}
		if (Unit->IsRooted())
		{
			return "It can't walk.";
		}
		if (Unit->bMoved)
		{
			return "Already moved this turn.";
		}
		if (Unit->ActsOnce() && Unit->bActed)
		{
			return "Knocked down: it can walk or act this turn, not both.";
		}
		if (bSprint && Unit->bActed)
		{
			return "Can't sprint after using an ability.";
		}
		// It has to be a node centre, and one it can actually get to.
		if (FMap::Snap(To).DistanceTo(To) > 0.001f)
		{
			return "That isn't a spot on the board.";
		}
		const FNode Target = FMap::NodeOf(To);
		for (const auto& Pair : ReachableNodes(*Unit, bSprint))
		{
			if (Pair.first == Target)
			{
				return std::string();
			}
		}
		return "It can't reach there.";
	}

	bool FBattle::ApplyMove(int UnitId, const FVec2& To, bool bSprint, FTickReport& Report)
	{
		FUnit* Unit = FindUnit(UnitId);
		if (!Unit)
		{
			return false;
		}

		// It faces the way it last stepped, which is what decides whether the
		// next blow lands on its front, its side or its back.
		const std::vector<FVec2> Path = PathTo(*Unit, FMap::NodeOf(To), bSprint);
		FVec2 Step = To - Unit->Pos;
		if (Path.size() >= 2)
		{
			Step = Path[Path.size() - 1] - Path[Path.size() - 2];
		}
		if (Step.Length() > 0.001f)
		{
			Unit->Facing = Step.Normalized();
		}

		Unit->Pos = To;
		Unit->bMoved = true;
		if (bSprint)
		{
			Unit->bActed = true;  // a sprint is the unit's action as well
		}
		Report.Say(EEventKind::Moved, Unit->Id);
		return true;
	}
}
