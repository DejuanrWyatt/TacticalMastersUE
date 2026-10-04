// Walking: where a unit can get to, how it gets there, and moving it.
//
// Ported from game_state.gd. The pathfinder is Dijkstra over the navigation
// grid, and it is written the same way as the original rather than the way a
// C++ programmer might reach for -- flat arrays, an explicit binary heap, the
// same order of comparisons. That is deliberate: two paths of equal length are
// equally correct, but only one of them matches Godot, and a replay that picks
// the other one is a replay that goes wrong.

#include "SimBattle.h"
#include "SimAbility.h"

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
		double Metres = Unit.Stat(EStat::Move) * Tuning.MoveMultiplier * Unit.MoveFactor();
		// Lone Wolf Pelt: further while no ally is near. Nothing without the item.
		if (Unit.HasItems())
		{
			const int Alone = GearSum(Unit, &FItemDef::AloneMove);
			if (Alone > 0 && IsAlone(Unit, Unit.Pos))
			{
				Metres += Alone * Tuning.MoveMultiplier * Unit.MoveFactor();
			}
		}
		return bSprint ? Metres * Tuning.SprintMultiplier : Metres;
	}

	int FBattle::JumpOf(const FUnit& Unit) const
	{
		// Items can add a level or two (Spring Greaves); flying needs none.
		return Unit.Flies() ? Ground::FlyJump : Ground::Jump + Unit.ItemJump();
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
		// Zone of control: within reach of an enemy tank a walk ends (FTuning::ZoneOfControl).
		std::vector<uint8_t> Zoned;
		const bool bZones = Team >= 0 && Tuning.ZoneOfControl >= 1.0 && EngageRadius > 0.0;
		if (bZones)
		{
			Zoned.assign(Count, 0);
		}
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
					if (bZones)
					{
						const FJobDef* Job = FindJob(Other.Job);
						// A tank whose reach the walk starts in does not hold it: a
						// unit already beside one can still walk away.
						bool bStartsInside = false;
						for (const FNode& S : Starts)
						{
							bStartsInside = bStartsInside
								|| static_cast<double>(FMap::NodePos(S).DistanceTo(Other.Pos)) < EngageRadius;
						}
						if (Job && !Job->Roles.empty() && Job->Roles.front() == "tank" && !bStartsInside)
						{
							MarkBlocked(Other.Pos, EngageRadius, Zoned);
						}
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
			// Stepped into a tank's zone: the walk can end here, and go no further.
			// (A tank the walk starts beside has no zone for it: see above.)
			if (bZones && Zoned[i] == 1 && C > 0.0)
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

	bool FBattle::WalkVia(const FUnit& Unit, const std::vector<FVec2>& Via, bool bSprint, double& OutLeft)
	{
		// Waypoints (2026-10-01): one walk in legs, each a shortest way from the
		// last spot to the next with what is left of the move. The legs add up to
		// exactly what the walk costs, engagement included, so a walk by
		// waypoints is never further than the unit could have gone.
		OutLeft = MoveOf(Unit, bSprint);
		FNode From = FMap::NodeOf(Unit.Pos);
		for (const FVec2& Point : Via)
		{
			const FNode Next = FMap::NodeOf(Point);
			if (Next.X < 0 || Next.Y < 0 || Next.X >= Map.NavX || Next.Y >= Map.NavY)
			{
				return false;
			}
			// A waypoint inside an enemy tank's zone would be a walk that goes on
			// past where it had to stop (FTuning::ZoneOfControl).
			if (Tuning.ZoneOfControl >= 1.0)
			{
				for (const FUnit& Other : Units)
				{
					const FJobDef* Job = Other.IsAlive() && Other.Team != Unit.Team ? FindJob(Other.Job) : nullptr;
					if (Job && !Job->Roles.empty() && Job->Roles.front() == "tank"
						&& static_cast<double>(FMap::NodePos(Next).DistanceTo(Other.Pos)) < Tuning.EngageRadius)
					{
						return false;
					}
				}
			}
			RunDijkstra({ From }, OutLeft, Unit.Team, JumpOf(Unit));
			const double Leg = Cost[Map.NodeIndex(Next)];
			if (Leg == Infinity)
			{
				return false;
			}
			OutLeft -= Leg;
			From = Next;
		}
		return true;
	}

	std::vector<std::pair<FNode, double>> FBattle::ReachableVia(const FUnit& Unit, const std::vector<FVec2>& Via, bool bSprint)
	{
		if (Via.empty())
		{
			return ReachableNodes(Unit, bSprint);
		}
		std::vector<std::pair<FNode, double>> Out;
		double Left = 0.0;
		if (!WalkVia(Unit, Via, bSprint, Left))
		{
			return Out;
		}
		const double Spent = MoveOf(Unit, bSprint) - Left;
		RunDijkstra({ FMap::NodeOf(Via.back()) }, Left, Unit.Team, JumpOf(Unit));
		for (int i = 0; i < static_cast<int>(Cost.size()); ++i)
		{
			if (Cost[i] == Infinity)
			{
				continue;
			}
			const FNode N = Map.NodeAt(i);
			const FVec2 P = FMap::NodePos(N);
			// As ReachableNodes: walked through, but not stood on.
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
				Out.push_back({ N, Spent + Cost[i] });
			}
		}
		return Out;
	}

	bool FBattle::HoldsTheLine(const FUnit& Unit)
	{
		const FJobDef* Job = FindJob(Unit.Job);
		return Job && !Job->Roles.empty() && Job->Roles.front() == "tank";
	}

	std::vector<FNode> FBattle::ZoneShadow(const FUnit& Unit, const std::vector<FVec2>& Via, bool bSprint)
	{
		std::vector<FNode> Out;
		if (Tuning.ZoneOfControl < 1.0)
		{
			return Out;
		}
		const std::vector<std::pair<FNode, double>> With = ReachableVia(Unit, Via, bSprint);
		const double Saved = Tuning.ZoneOfControl;
		Tuning.ZoneOfControl = 0.0;
		const std::vector<std::pair<FNode, double>> Without = ReachableVia(Unit, Via, bSprint);
		Tuning.ZoneOfControl = Saved;
		std::vector<uint8_t> Kept(static_cast<size_t>(Map.NavX * Map.NavY), 0);
		for (const std::pair<FNode, double>& Entry : With)
		{
			Kept[static_cast<size_t>(Map.NodeIndex(Entry.first))] = 1;
		}
		for (const std::pair<FNode, double>& Entry : Without)
		{
			if (Kept[static_cast<size_t>(Map.NodeIndex(Entry.first))] == 0)
			{
				Out.push_back(Entry.first);
			}
		}
		return Out;
	}

	std::vector<FVec2> FBattle::PathIgnoringZones(const FUnit& Unit, const std::vector<FVec2>& Via, const FNode& To, bool bSprint)
	{
		const double Saved = Tuning.ZoneOfControl;
		Tuning.ZoneOfControl = 0.0;
		std::vector<FVec2> Path = PathVia(Unit, Via, To, bSprint);
		Tuning.ZoneOfControl = Saved;
		return Path;
	}

	double FBattle::StrikeReach(const FUnit& Unit) const
	{
		// As ThreatOn counts them: ready by its next turn, or now on its turn.
		const int ReadyBy = Unit.bReady ? 0 : 1;
		double Best = 0.0;
		for (int Slot = 0; Slot < AbilitySlots; ++Slot)
		{
			const FAbility* Ability = Unit.Ability(Slot);
			if (!Ability || Ability->Effect != EEffect::Damage || Ability->Target != ETargetSide::Enemy
				|| Ability->Kind == "passive" || Ability->Kind == "aura" || Ability->Kind == "toggle"
				|| Unit.Cooldowns[Slot] > ReadyBy || (Slot == 3 && Unit.Ult < Pace::UltMax))
			{
				continue;
			}
			Best = std::max(Best, static_cast<double>(Ability->MaxRange) + Ability->Aoe + Ground::HitRadius);
		}
		return Best;
	}

	std::vector<FLane> FBattle::TankLanes(int TankId, const FVec2& At, const std::vector<int>& Enemies, const std::vector<int>& BackLine)
	{
		std::vector<FLane> Out;
		FUnit* Tank = FindUnit(TankId);
		if (!Tank)
		{
			return Out;
		}
		// Which of the back line the enemy can stand within striking reach of
		// this turn, and the nearest spot to each.
		auto Strikes = [&](const FUnit& Enemy, std::vector<int>& Reached, std::vector<int>& Nearest)
		{
			const double Reach = StrikeReach(Enemy);
			Reached.clear();
			Nearest.assign(BackLine.size(), -1);
			if (Reach <= 0.0)
			{
				return;
			}
			RunDijkstra({ FMap::NodeOf(Enemy.Pos) }, MoveOf(Enemy), Enemy.Team, JumpOf(Enemy));
			std::vector<double> Closest(BackLine.size(), Infinity);
			for (int i = 0; i < static_cast<int>(Cost.size()); ++i)
			{
				if (Cost[i] == Infinity)
				{
					continue;
				}
				const FVec2 P = FMap::NodePos(Map.NodeAt(i));
				for (size_t k = 0; k < BackLine.size(); ++k)
				{
					const FUnit* Target = FindUnit(BackLine[k]);
					if (!Target)
					{
						continue;
					}
					const double Apart = P.DistanceTo(Target->Pos);
					if (Apart < Closest[k])
					{
						Closest[k] = Apart;
						Nearest[k] = i;
					}
				}
			}
			for (size_t k = 0; k < BackLine.size(); ++k)
			{
				if (Closest[k] <= Reach)
				{
					Reached.push_back(BackLine[k]);
				}
			}
		};
		const FVec2 Was = Tank->Pos;
		for (const int Id : Enemies)
		{
			const FUnit* Enemy = FindUnit(Id);
			if (!Enemy || !Enemy->IsAlive() || Enemy->Team == Tank->Team)
			{
				continue;
			}
			FLane Lane;
			Lane.EnemyId = Id;
			std::vector<int> Nearest;
			Strikes(*Enemy, Lane.Before, Nearest);
			Tank->Pos = At;
			Strikes(*Enemy, Lane.After, Nearest);
			if (Lane.Before.empty() && Lane.After.empty())
			{
				Tank->Pos = Was;
				continue;
			}
			// Toward the first it loses, else the first it still reaches.
			for (const int Lost : Lane.Before)
			{
				if (Lane.TowardId < 0 && std::find(Lane.After.begin(), Lane.After.end(), Lost) == Lane.After.end())
				{
					Lane.TowardId = Lost;
				}
			}
			if (Lane.TowardId < 0)
			{
				Lane.TowardId = Lane.After.empty() ? Lane.Before.front() : Lane.After.front();
			}
			for (size_t k = 0; k < BackLine.size(); ++k)
			{
				if (BackLine[k] == Lane.TowardId && Nearest[k] >= 0)
				{
					Lane.Path = PathTo(*Enemy, Map.NodeAt(Nearest[k]));
				}
			}
			Tank->Pos = Was;
			Out.push_back(Lane);
		}
		Tank->Pos = Was;
		return Out;
	}

	std::vector<FVec2> FBattle::PathVia(const FUnit& Unit, const std::vector<FVec2>& Via, const FNode& To, bool bSprint)
	{
		if (Via.empty())
		{
			return PathTo(Unit, To, bSprint);
		}
		std::vector<FVec2> Path;
		double Left = MoveOf(Unit, bSprint);
		FNode From = FMap::NodeOf(Unit.Pos);
		std::vector<FNode> Stops;
		for (const FVec2& Point : Via)
		{
			Stops.push_back(FMap::NodeOf(Point));
		}
		Stops.push_back(To);
		for (const FNode& Next : Stops)
		{
			if (Next.X < 0 || Next.Y < 0 || Next.X >= Map.NavX || Next.Y >= Map.NavY)
			{
				return std::vector<FVec2>();
			}
			RunDijkstra({ From }, Left, Unit.Team, JumpOf(Unit));
			const int Target = Map.NodeIndex(Next);
			if (Cost[Target] == Infinity)
			{
				return std::vector<FVec2>();
			}
			Left -= Cost[Target];
			std::vector<FVec2> Leg;
			for (int At = Target; At >= 0; At = Parent[At])
			{
				Leg.insert(Leg.begin(), FMap::NodePos(Map.NodeAt(At)));
			}
			// Each leg starts where the last one ended: that spot once.
			const size_t Skip = Path.empty() ? 0 : 1;
			Path.insert(Path.end(), Leg.begin() + static_cast<std::ptrdiff_t>(std::min(Skip, Leg.size())), Leg.end());
			From = Next;
		}
		return Path;
	}

	std::vector<FVec2> FBattle::RouteTo(const FUnit& Unit, const std::vector<FVec2>& Via, const FNode& To, double* OutCost)
	{
		// As PathVia, in legs, with no budget: the move limit is what splits it
		// into turns, and that is the caller's to do.
		constexpr double NoLimit = 1.0e9;
		std::vector<FVec2> Route;
		double Spent = 0.0;
		FNode From = FMap::NodeOf(Unit.Pos);
		std::vector<FNode> Stops;
		for (const FVec2& Point : Via)
		{
			Stops.push_back(FMap::NodeOf(Point));
		}
		Stops.push_back(To);
		for (const FNode& Next : Stops)
		{
			if (Next.X < 0 || Next.Y < 0 || Next.X >= Map.NavX || Next.Y >= Map.NavY)
			{
				return std::vector<FVec2>();
			}
			RunDijkstra({ From }, NoLimit, Unit.Team, JumpOf(Unit));
			const int Target = Map.NodeIndex(Next);
			if (Cost[Target] == Infinity)
			{
				return std::vector<FVec2>();
			}
			Spent += Cost[Target];
			std::vector<FVec2> Leg;
			for (int At = Target; At >= 0; At = Parent[At])
			{
				Leg.insert(Leg.begin(), FMap::NodePos(Map.NodeAt(At)));
			}
			const size_t Skip = Route.empty() ? 0 : 1;
			Route.insert(Route.end(), Leg.begin() + static_cast<std::ptrdiff_t>(std::min(Skip, Leg.size())), Leg.end());
			From = Next;
		}
		if (OutCost)
		{
			*OutCost = Spent;
		}
		return Route;
	}

	std::string FBattle::ValidateMove(int UnitId, const FVec2& To, bool bSprint, const std::vector<FVec2>& Via)
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
		// Waypoints: a few, each a node centre, each a step on from the last.
		if (static_cast<int>(Via.size()) > MaxWaypoints)
		{
			return "Too many waypoints.";
		}
		FNode Previous = FMap::NodeOf(Unit->Pos);
		for (const FVec2& Point : Via)
		{
			if (FMap::Snap(Point).DistanceTo(Point) > 0.001f)
			{
				return "A waypoint isn't a spot on the board.";
			}
			if (FMap::NodeOf(Point) == Previous)
			{
				return "Two waypoints on one spot.";
			}
			Previous = FMap::NodeOf(Point);
		}
		if (!Via.empty())
		{
			double Left = 0.0;
			if (!WalkVia(*Unit, Via, bSprint, Left))
			{
				return "It can't reach that waypoint.";
			}
		}
		const FNode Target = FMap::NodeOf(To);
		for (const auto& Pair : ReachableVia(*Unit, Via, bSprint))
		{
			if (Pair.first == Target)
			{
				return std::string();
			}
		}
		return "It can't reach there.";
	}

	bool FBattle::ApplyMove(int UnitId, const FVec2& To, bool bSprint, FTickReport& Report, const std::vector<FVec2>& Via, int Face)
	{
		FUnit* Unit = FindUnit(UnitId);
		if (!Unit)
		{
			return false;
		}

		// It faces the way it last stepped, which is what decides whether the
		// next blow lands on its front, its side or its back.
		const std::vector<FVec2> Path = PathVia(*Unit, Via, FMap::NodeOf(To), bSprint);
		Unit->WalkFrom = Unit->Pos;
		Unit->WalkVia = Via;
		// Walking by a waypoint on a cache picks it up on the way, as stopping
		// there would (2026-10-01: "past an item to pick it up").
		for (const FVec2& Point : Via)
		{
			Unit->Pos = Point;
			PickUpAt(*Unit, Report);
		}
		FVec2 Step = To - Unit->Pos;
		if (Path.size() >= 2)
		{
			Step = Path[Path.size() - 1] - Path[Path.size() - 2];
		}
		// Off-Balance: it can't turn to face anything until it is hit. Told which
		// way to face on arrival (2026-10-03), it turns that way instead: a walk
		// can end with its back to a wall and its front to the enemy.
		if (!Unit->HasStatus("offbalance"))
		{
			if (Face >= 0)
			{
				Unit->Facing = FacingWay(Face);
			}
			else if (Step.Length() > 0.001f)
			{
				Unit->Facing = Step.Normalized();
			}
		}

		Unit->Pos = To;
		Unit->bMoved = true;
		if (bSprint)
		{
			Unit->bActed = true;  // a sprint is the unit's action as well
		}
		Report.Say(EEventKind::Moved, Unit->Id);
		// Walking onto items picks them up (feat-neutral-camps.md, "Picking up").
		PickUpAt(*Unit, Report);
		// Suppressed: walking out from under it draws a blow (feat-status-effects.md).
		if (!Unit->Statuses.empty() && Unit->HasStatus("suppressed"))
		{
			SuppressedMoved(*Unit, Report);
		}
		return true;
	}
}
