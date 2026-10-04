// Neutral camps: monsters that wake around the map, fight whoever comes near
// in their own way, and leave items on the ground when beaten.
//
// Docs/design/feat-neutral-camps.md, and the bestiary it points to. Not
// Godot's: a battle whose rules ask for no camps has none, makes no monsters,
// and draws nothing from the two generators here, so it is exactly the battle
// it was before any of this.
//
// Everything a monster decides that is a rule lives here -- when it is set off,
// when it gives up, when it runs, when it goes home -- so it is the same on
// every machine and in a replay. What it does with a turn once it is fighting
// is an order like anybody's, chosen by FNeutralPlayer (SimNeutral.cpp).

#include "SimAbility.h"
#include "SimBattle.h"
#include "SimItem.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace TMSim
{
	namespace
	{
		constexpr int TicksPerSecond = Pace::TicksPerSecond;

		/** A camp's roster, in the bestiary's words: every kind but the bosses, which come from the map. */
		std::vector<FCampKind> BuildKinds()
		{
			std::vector<FCampKind> Out;
			auto Add = [&Out](const char* Id, const char* Name, int Tier, std::vector<std::string> Members, int LootTier, int LootCount = 1)
			{
				FCampKind Kind;
				Kind.Id = Id;
				Kind.Name = Name;
				Kind.Tier = Tier;
				Kind.Members = std::move(Members);
				Kind.LootTier = LootTier;
				Kind.LootCount = LootCount;
				Out.push_back(Kind);
				return &Out.back();
			};
			// Easy: one unit clears it in a couple of turns.
			Add("grazer_herd", "Grazer Herd", 0, { "mossback_grazer", "mossback_grazer" }, 0, 2);
			Add("scrapper_gang", "Scrapper Gang", 0, { "camp_scrapper", "camp_scrapper", "camp_scrapper" }, 0);
			Add("skink_den", "Skink Den", 0, { "tar_skink" }, 0);
			Add("lookout_post", "Lookout Post", 0, { "dawnling_lookout" }, 0);
			// Medium: two units.
			Add("deserter_patrol", "Deserter Patrol", 1, { "dusk_deserter", "dusk_deserter", "dusk_archer" }, 1);
			Add("skyray_roost", "Skyray Roost", 1, { "skyray" }, 1);
			Add("stalker_pair", "Stalker Pair", 1, { "jungle_stalker", "jungle_stalker" }, 1);
			Add("siege_nest", "Siege Nest", 1, { "siege_crawler" }, 1);
			// Hard: three units, and some thought.
			Add("runner_warden", "Runner and Warden", 2, { "treasure_runner", "dusk_warden" }, 1)->CarriedTier = 2;
			Add("brute_shrine", "Shrine of the Brute", 2, { "crag_brute" }, 2)->bShrine = true;
			Add("beetle_burrow", "Beetle Burrow", 2, { "molten_beetle" }, 2);
			Add("qilin_herd", "Qilin's Herd", 2, { "qilin", "mossback_grazer", "mossback_grazer" }, 2);
			Add("pelt_hunter", "Pelt Hunter", 2, { "pelt_hunter" }, 2);
			return Out;
		}

		/** The adds a boss holds in reserve, by its class. */
		std::vector<std::string> BossReserves(const std::string& Boss)
		{
			if (Boss == "helix_prime")
			{
				return { "camp_scrapper", "camp_scrapper", "camp_scrapper" };
			}
			return {};
		}

		/** Camps per tier for each setting, in pairs (epic: one). */
		const int Layout[4][4] =
		{
			{ 0, 0, 0, 0 },
			{ 1, 1, 0, 0 },
			{ 2, 1, 1, 1 },
			{ 3, 2, 1, 1 },
		};
		/** How far along the way from blue's start to red's each tier stands. */
		const double BandLow[3] = { 0.20, 0.30, 0.40 };
		const double BandHigh[3] = { 0.40, 0.45, 0.48 };

		bool KindReady(const FCampKind& Kind)
		{
			for (const std::string& Job : Kind.Members)
			{
				if (!FindJob(Job))
				{
					return false;
				}
			}
			for (const std::string& Job : Kind.Reserves)
			{
				if (!FindJob(Job))
				{
					return false;
				}
			}
			return true;
		}
	}

	const std::vector<FCampKind>& CampKinds()
	{
		static const std::vector<FCampKind> Kinds = BuildKinds();
		return Kinds;
	}

	// ------------------------------------------------------------ placing

	FVec2 FBattle::CampSpot(int Tier, int SkipCamp)
	{
		// Copied, because asking for another distance field may clear the cache
		// the first lives in.
		const std::vector<double> Blue = DistanceFrom(SpawnPoints[0]);
		const std::vector<double> Red = DistanceFrom(SpawnPoints[1]);
		auto Walkable = [&](const FVec2& Point)
		{
			const FNode Node = FMap::NodeOf(Point);
			return InBounds(Point) && Map.NodeWalkable(Node) && HazardAt(Point) == 0 && !IsCover(Point);
		};
		auto Fits = [&](const FVec2& Point)
		{
			const int Index = Map.NodeIndex(FMap::NodeOf(Point));
			if (!Walkable(Point) || Index < 0 || Index >= static_cast<int>(Blue.size())
				|| !std::isfinite(Blue[static_cast<size_t>(Index)]) || !std::isfinite(Red[static_cast<size_t>(Index)]))
			{
				return false;
			}
			const double Share = Blue[static_cast<size_t>(Index)] / (Blue[static_cast<size_t>(Index)] + Red[static_cast<size_t>(Index)]);
			if (Tier < 3 && (Share < BandLow[Tier] || Share > BandHigh[Tier]))
			{
				return false;
			}
			// Room to fight: its own tile and the eight round it are ground.
			for (int Dy = -1; Dy <= 1; ++Dy)
			{
				for (int Dx = -1; Dx <= 1; ++Dx)
				{
					if (!Walkable(FVec2(Point.X + Dx * Ground::TileSize, Point.Y + Dy * Ground::TileSize)))
					{
						return false;
					}
				}
			}
			const FVec2 Size = Map.SizeMeters();
			const FVec2 Twin(Size.X - Point.X, Size.Y - Point.Y);
			// Clear of its twin, and of the middle where the boss stands.
			if (static_cast<double>(Point.DistanceTo(Twin)) < Camp::Apart * 1.5
				|| static_cast<double>(Point.DistanceTo(FVec2(Size.X * 0.5f, Size.Y * 0.5f))) < Camp::Apart + 1.0)
			{
				return false;
			}
			for (const FVec2& Near : { Point, Twin })
			{
				for (int Team = 0; Team < 2; ++Team)
				{
					if (static_cast<double>(Near.DistanceTo(SpawnPoints[Team])) < Camp::Apart * 1.5)
					{
						return false;
					}
				}
				for (const FWatchtower& Tower : Watchtowers)
				{
					if (static_cast<double>(Near.DistanceTo(Tower.Pos)) < Camp::Apart)
					{
						return false;
					}
				}
				for (int i = 0; i < static_cast<int>(Camps.size()); ++i)
				{
					if (i != SkipCamp && static_cast<double>(Near.DistanceTo(Camps[static_cast<size_t>(i)].Spot)) < Camp::Apart)
					{
						return false;
					}
				}
				for (const FCache& Cache : Caches)
				{
					if (!Cache.Items.empty() && static_cast<double>(Near.DistanceTo(Cache.Pos)) < Camp::Apart * 0.5)
					{
						return false;
					}
				}
			}
			return true;
		};

		// Every tile of blue's half, shuffled, and the first that fits. In row
		// order before the shuffle, so the same seed always picks the same.
		std::vector<FVec2> Tiles;
		for (int Y = 0; Y < Map.TilesY / 2; ++Y)
		{
			for (int X = 0; X < Map.TilesX; ++X)
			{
				Tiles.push_back(FMap::Snap(FVec2((static_cast<float>(X) + 0.5f) * Ground::TileSize,
					(static_cast<float>(Y) + 0.5f) * Ground::TileSize)));
			}
		}
		for (int i = static_cast<int>(Tiles.size()) - 1; i > 0; --i)
		{
			const int j = static_cast<int>(CampRng.RandiRange(0, i));
			std::swap(Tiles[static_cast<size_t>(i)], Tiles[static_cast<size_t>(j)]);
		}
		for (const FVec2& Point : Tiles)
		{
			if (Fits(Point))
			{
				return Point;
			}
		}
		return FVec2(-1.0f, -1.0f);
	}

	std::vector<FVec2> FBattle::PatrolRoute(const FVec2& Spot, bool bRed)
	{
		// Out and round: four points 6 m off, turned about for red so the two
		// sides' patrols walk mirrored routes. Only points a monster could walk
		// to from the camp.
		std::vector<FVec2> Route{ Spot };
		const std::vector<double> Walk = DistanceFrom(Spot);
		const float Offsets[4][2] = { { 6.0f, 0.0f }, { 0.0f, 6.0f }, { -6.0f, 0.0f }, { 0.0f, -6.0f } };
		for (const auto& Offset : Offsets)
		{
			const float Sign = bRed ? -1.0f : 1.0f;
			const FVec2 Point = FMap::Snap(FVec2(Spot.X + Offset[0] * Sign, Spot.Y + Offset[1] * Sign));
			const int Index = Map.NodeIndex(FMap::NodeOf(Point));
			if (InBounds(Point) && Map.NodeWalkable(FMap::NodeOf(Point)) && HazardAt(Point) == 0 && Index >= 0
				&& Index < static_cast<int>(Walk.size()) && std::isfinite(Walk[static_cast<size_t>(Index)])
				&& Walk[static_cast<size_t>(Index)] < 12.0)
			{
				Route.push_back(Point);
			}
		}
		return Route;
	}

	void FBattle::PlaceCamps(uint64_t InSeed)
	{
		Camps.clear();
		Caches.clear();
		Stash[0].clear();
		Stash[1].clear();
		CampRng.Seed(InSeed ^ Camp::Salt);
		LootRng.Seed(InSeed ^ Camp::LootSalt);
		const int Level = std::max(0, std::min(3, RoundToInt(Tuning.CampLevel)));
		if (Level == 0 || Map.TilesX == 0)
		{
			return;
		}
		int NextId = 0;
		for (const FUnit& Unit : Units)
		{
			NextId = std::max(NextId, Unit.Id + 1);
		}
		const FVec2 Size = Map.SizeMeters();
		auto MakeMonster = [&](const std::string& Job, int CampIndex, const FVec2& Where)
		{
			FUnit Monster;
			Monster.Id = NextId++;
			Monster.Team = 2;
			Monster.Job = Job;
			if (const FJobDef* Def = FindJob(Job))
			{
				Monster.Stats = &Def->Stats;
			}
			Monster.bMonster = true;
			Monster.bOffBoard = true;
			Monster.Camp = CampIndex;
			Monster.Pos = Where;
			Monster.Home = Where;
			Monster.Hp = 0;
			Monster.KoTicks = 0;
			Units.push_back(Monster);
			return Monster.Id;
		};
		auto AddCamp = [&](int KindIndex, int Tier, const FVec2& Spot, bool bRed, const std::vector<std::string>& Members,
			const std::vector<std::string>& Reserves)
		{
			FCamp Made;
			Made.Kind = KindIndex;
			Made.Tier = Tier;
			Made.Spot = Spot;
			Made.bRed = bRed;
			Made.State = ECampState::Waiting;
			Made.Timer = Camp::FirstWake[Tier] * TicksPerSecond;
			Made.bShrine = KindIndex >= 0 && CampKinds()[static_cast<size_t>(KindIndex)].bShrine;
			const int Index = static_cast<int>(Camps.size());
			for (const std::string& Job : Members)
			{
				Made.Members.push_back(MakeMonster(Job, Index, Spot));
			}
			for (const std::string& Job : Reserves)
			{
				Made.Reserves.push_back(MakeMonster(Job, Index, Spot));
			}
			Camps.push_back(Made);
		};

		// The pairs, easy first: a kind drawn for the pair, a spot on blue's half
		// in its band, and the twin turned about the middle as the ground is.
		for (int Tier = 0; Tier < 3; ++Tier)
		{
			std::vector<int> Kinds;
			for (int k = 0; k < static_cast<int>(CampKinds().size()); ++k)
			{
				if (CampKinds()[static_cast<size_t>(k)].Tier == Tier && KindReady(CampKinds()[static_cast<size_t>(k)]))
				{
					Kinds.push_back(k);
				}
			}
			for (int Pair = 0; Pair < Layout[Level][Tier] && !Kinds.empty(); ++Pair)
			{
				const int KindIndex = Kinds[static_cast<size_t>(CampRng.RandiRange(0, static_cast<int64_t>(Kinds.size()) - 1))];
				const FVec2 Spot = CampSpot(Tier, -1);
				if (Spot.X < 0.0f)
				{
					break;  // no room on this map for another of this tier
				}
				const FCampKind& Kind = CampKinds()[static_cast<size_t>(KindIndex)];
				AddCamp(KindIndex, Tier, Spot, false, Kind.Members, Kind.Reserves);
				AddCamp(KindIndex, Tier, FVec2(Size.X - Spot.X, Size.Y - Spot.Y), true, Kind.Members, Kind.Reserves);
			}
		}

		// The boss: the map's own, or one drawn at random, at the walkable node
		// nearest the middle.
		if (Layout[Level][3] > 0)
		{
			std::vector<std::string> Bosses;
			for (const FJobDef* Def : AllMonsters())
			{
				if (Def->Monster.bMonster && Def->Monster.Tier == 3)
				{
					Bosses.push_back(Def->Id);
				}
			}
			std::sort(Bosses.begin(), Bosses.end());
			std::string Boss = BossJob;
			if (!Bosses.empty() && (Tuning.RandomBoss >= 0.5 || !FindJob(Boss)))
			{
				Boss = Bosses[static_cast<size_t>(CampRng.RandiRange(0, static_cast<int64_t>(Bosses.size()) - 1))];
			}
			const FJobDef* BossDef = FindJob(Boss);
			bool bReserves = true;
			for (const std::string& Job : BossReserves(Boss))
			{
				bReserves = bReserves && FindJob(Job);
			}
			if (BossDef && BossDef->Monster.bMonster && bReserves)
			{
				const FVec2 Middle(Size.X * 0.5f, Size.Y * 0.5f);
				FVec2 Best(-1.0f, -1.0f);
				float BestDistance = std::numeric_limits<float>::max();
				for (int i = 0; i < Map.NavX * Map.NavY; ++i)
				{
					const FNode Node = Map.NodeAt(i);
					const FVec2 Point = FMap::NodePos(Node);
					const float Distance = Point.DistanceTo(Middle);
					if (Map.NodeWalkable(Node) && HazardAt(Point) == 0 && !IsCover(Point) && Distance < BestDistance)
					{
						BestDistance = Distance;
						Best = Point;
					}
				}
				if (Best.X >= 0.0f)
				{
					AddCamp(-1, 3, Best, false, { Boss }, BossReserves(Boss));
				}
			}
		}
	}

	// ------------------------------------------------------------ waking

	void FBattle::WakeCamp(int Index, FTickReport& Report)
	{
		FCamp& Held = Camps[static_cast<size_t>(Index)];
		Held.State = ECampState::Awake;
		++Held.Wakes;
		const FCampKind* Kind = Held.Kind >= 0 ? &CampKinds()[static_cast<size_t>(Held.Kind)] : nullptr;
		if (Held.bShrine)
		{
			Held.ShrineRest = 0;
		}

		// A patrol's route, if any of it patrols.
		Held.Route.clear();
		for (const int Id : Held.Members)
		{
			const FUnit* Monster = FindUnit(Id);
			const FMonsterInfo* Info = Monster ? Monster->MonsterInfo() : nullptr;
			if (Info && Info->Temperament == ETemperament::Patrol && Held.Route.empty())
			{
				Held.Route = PatrolRoute(Held.Spot, Held.bRed);
			}
		}

		// Each on the nearest free spot to the camp, on the one-metre lattice as
		// units are placed, in the kind's order.
		const FNode Middle = FMap::NodeOf(Held.Spot);
		for (size_t m = 0; m < Held.Members.size(); ++m)
		{
			FUnit* Monster = FindUnit(Held.Members[m]);
			if (!Monster)
			{
				continue;
			}
			FVec2 Where = Held.Spot;
			float BestDistance = std::numeric_limits<float>::max();
			for (int Dy = -12; Dy <= 12; ++Dy)
			{
				for (int Dx = -12; Dx <= 12; ++Dx)
				{
					FNode Node;
					Node.X = Middle.X + Dx;
					Node.Y = Middle.Y + Dy;
					if ((Dx % 2 != 0) || (Dy % 2 != 0) || Node.X < 0 || Node.Y < 0 || Node.X >= Map.NavX || Node.Y >= Map.NavY
						|| !Map.NodeWalkable(Node))
					{
						continue;
					}
					const FVec2 Point = FMap::NodePos(Node);
					const float Distance = Point.DistanceTo(Held.Spot);
					if (Distance >= BestDistance || HazardAt(Point) != 0)
					{
						continue;
					}
					bool bFree = true;
					for (const FUnit& Other : Units)
					{
						bFree = bFree && !(Other.IsAlive() && Other.Pos.DistanceTo(Point) < Ground::UnitSpacing * 1.2f);
					}
					if (bFree)
					{
						BestDistance = Distance;
						Where = Point;
					}
				}
			}
			FUnit& M = *Monster;
			M.bOffBoard = false;
			M.Pos = Where;
			M.Home = Where;
			M.Facing = (FVec2(Map.SizeMeters().X * 0.5f, Map.SizeMeters().Y * 0.5f) - Where).Normalized();
			M.Phase = 0;
			M.Hp = M.MaxHp();
			M.KoTicks = 0;
			// Half the head start a side's unit gets: it wakes, looks round, then moves.
			M.Tg = std::min(Pace::TgMax - 1, M.Stat(EStat::Speed) * Pace::StartTgPerSpeed / 2);
			M.bReady = false;
			M.Clock = 0;
			M.bMoved = false;
			M.bActed = false;
			M.bHustling = false;
			M.Ult = 0;
			M.UnharmedTurns = 0;
			M.Statuses.clear();
			M.Buffs.clear();
			M.Casting = FCast();
			M.Channeling = FChannel();
			for (int Slot = 0; Slot < AbilitySlots; ++Slot)
			{
				M.Cooldowns[Slot] = 0;
				M.Toggled[Slot] = false;
				M.ToggledTurn[Slot] = false;
			}
			M.Mind = EMind::Resting;
			M.Grudge = -1;
			M.GrudgeTurns = 0;
			M.FleeTurns = 0;
			M.RouteStep = 0;
			M.Stagger = 0;
			M.TamedTurns = 0;
			M.Wrath.clear();
			M.HuntTarget = -1;
			M.HuntLost = 0;
			M.Claim[0] = 0;
			M.Claim[1] = 0;
			M.Team = 2;
			for (const FItemDef*& Item : M.Gear)
			{
				Item = nullptr;
			}
			// The Treasure Runner carries its prize.
			if (m == 0 && Kind && Kind->CarriedTier >= 0)
			{
				std::vector<const FItemDef*> Prize;
				RollLoot(Kind->CarriedTier, 1, Prize);
				if (!Prize.empty())
				{
					M.Gear[0] = Prize[0];
				}
			}
			M.Hp = M.MaxHp();
			Report.Say(EEventKind::Moved, M.Id);
		}
		// Woken by noise (2026-10-02, A): it goes for the side that made the most
		// (on a tie, the side that made the last of it), at whoever of that side was
		// loudest last. Set off, it fights from its next turn: no free move.
		if (Held.bNoiseWake)
		{
			const int Side = Held.NoiseBy[1] > Held.NoiseBy[0] ? 1 : Held.NoiseBy[0] > Held.NoiseBy[1] ? 0 : Held.LastLoudSide;
			const FUnit* Loud = FindUnit(Held.LoudUnit[Side]);
			FUnit* First = Held.Members.empty() ? nullptr : FindUnit(Held.Members[0]);
			if (Loud && IsQuarry(*Loud) && First)
			{
				AlertMonster(*First, Loud->Id, Report);
			}
		}
		Held.Noise = 0;
		Held.NoiseQuiet = 0;
		Held.NoiseBy[0] = 0;
		Held.NoiseBy[1] = 0;
		Held.LoudUnit[0] = -1;
		Held.LoudUnit[1] = -1;
		Held.bNoiseWake = false;
		FEvent Event;
		Event.Kind = EEventKind::CampAwake;
		Event.Slot = Index;
		Event.Where = Held.Spot;
		Report.Events.push_back(Event);
	}

	void FBattle::TickCamps(FTickReport& Report)
	{
		for (int i = 0; i < static_cast<int>(Camps.size()); ++i)
		{
			FCamp& Held = Camps[static_cast<size_t>(i)];
			if (Held.bShrine && Held.ShrineRest > 0)
			{
				--Held.ShrineRest;
			}
			if (Held.State == ECampState::Waiting)
			{
				// Noise fades: one step for each stretch of quiet (A).
				if (Held.Noise > 0 && !Held.bNoiseWake && ++Held.NoiseQuiet >= Camp::NoiseQuietSeconds * TicksPerSecond)
				{
					--Held.Noise;
					Held.NoiseQuiet = 0;
				}
				--Held.Timer;
				if (Held.Timer == Camp::Warning[Held.Tier] * TicksPerSecond)
				{
					FEvent Event;
					Event.Kind = EEventKind::CampWarning;
					Event.Slot = i;
					Event.Amount = Camp::Warning[Held.Tier];
					Event.Where = Held.Spot;
					Report.Events.push_back(Event);
				}
				if (Held.Timer <= 0)
				{
					WakeCamp(i, Report);
				}
				continue;
			}

			// Awake: cleared once none of it is left on the board (fallen, or run off).
			bool bAnyLeft = false;
			for (const int Id : Held.Members)
			{
				const FUnit* Monster = FindUnit(Id);
				bAnyLeft = bAnyLeft || (Monster && Monster->IsAlive() && Monster->bMonster && !Monster->bOffBoard);
			}
			for (const int Id : Held.Reserves)
			{
				const FUnit* Monster = FindUnit(Id);
				bAnyLeft = bAnyLeft || (Monster && Monster->IsAlive() && !Monster->bOffBoard);
			}
			if (bAnyLeft)
			{
				continue;
			}
			const FCampKind* Kind = Held.Kind >= 0 ? &CampKinds()[static_cast<size_t>(Held.Kind)] : nullptr;
			if (Held.Tier == 3)
			{
				// The boss is gone, and with it whatever it hunted (C).
				for (FUnit& Each : Units)
				{
					RemoveStatus(Each, "hunted");
				}
			}
			std::vector<const FItemDef*> Loot;
			if (Held.Tier == 3)
			{
				// An epic, a rare, and the boss's own prize if it has one.
				RollLoot(3, 1, Loot);
				RollLoot(2, 1, Loot);
				if (!Held.Members.empty())
				{
					if (const FUnit* Boss = FindUnit(Held.Members[0]))
					{
						RollLoot(3, 1, Loot, Boss->Job);
					}
				}
			}
			else if (Kind && Kind->LootTier >= 0)
			{
				RollLoot(Kind->LootTier, Kind->LootCount, Loot);
			}
			const int Cache = Loot.empty() ? -1 : DropItems(Held.Spot, Loot, std::vector<int>(Loot.size(), 0), Report);
			FEvent Event;
			Event.Kind = EEventKind::CampCleared;
			Event.Slot = i;
			Event.Amount = Cache;
			Event.Where = Held.Spot;
			Report.Events.push_back(Event);

			// Back later, somewhere new on its own side's half (the boss where it was);
			// or, with respawns off (the default since 2026-10-01), never.
			Held.State = ECampState::Waiting;
			Held.Timer = Tuning.CampRespawn >= 0.5 ? Camp::Respawn[Held.Tier] * TicksPerSecond : std::numeric_limits<int>::max();
			// A reserve spent is spent until the camp wakes again.
			for (const int Id : Held.Reserves)
			{
				if (FUnit* Monster = FindUnit(Id))
				{
					Monster->bOffBoard = true;
					Monster->Hp = 0;
					Monster->KoTicks = 0;
				}
			}
			if (Held.Tier < 3)
			{
				const FVec2 Spot = CampSpot(Held.Tier, i);
				if (Spot.X >= 0.0f)
				{
					const FVec2 Size = Map.SizeMeters();
					Held.Spot = Held.bRed ? FVec2(Size.X - Spot.X, Size.Y - Spot.Y) : Spot;
				}
			}
		}
	}

	// ------------------------------------------------------------- loot

	void FBattle::RollLoot(int Tier, int Count, std::vector<const FItemDef*>& Into, const std::string& Boss)
	{
		// Uniform over the tier's items that nobody has: not carried, not in a
		// cache, not already drawn. Every item exists at most once in a battle.
		auto OnBoard = [&](const FItemDef* Item)
		{
			for (const FUnit& Unit : Units)
			{
				for (const FItemDef* Carried : Unit.Gear)
				{
					if (Carried == Item)
					{
						return true;
					}
				}
			}
			for (const FCache& Cache : Caches)
			{
				for (const FItemDef* Lying : Cache.Items)
				{
					if (Lying == Item)
					{
						return true;
					}
				}
			}
			return std::find(Into.begin(), Into.end(), Item) != Into.end();
		};
		for (int n = 0; n < Count; ++n)
		{
			std::vector<const FItemDef*> Pool;
			for (const FItemDef* Item : AllItems())
			{
				if (static_cast<int>(Item->Tier) == Tier && !OnBoard(Item) && Item->Boss == Boss)
				{
					Pool.push_back(Item);
				}
			}
			if (Pool.empty())
			{
				return;
			}
			Into.push_back(Pool[static_cast<size_t>(LootRng.RandiRange(0, static_cast<int64_t>(Pool.size()) - 1))]);
		}
	}

	int FBattle::DropItems(const FVec2& Where, const std::vector<const FItemDef*>& Items, const std::vector<int>& Cooldowns,
		FTickReport& Report)
	{
		// Into a cache already lying right here, or a new one.
		int Index = -1;
		for (int i = 0; i < static_cast<int>(Caches.size()); ++i)
		{
			if (Caches[static_cast<size_t>(i)].Pos.DistanceTo(Where) < 0.3f)
			{
				Index = i;
			}
		}
		if (Index < 0)
		{
			FCache Made;
			Made.Pos = FMap::Snap(Where);
			Caches.push_back(Made);
			Index = static_cast<int>(Caches.size()) - 1;
		}
		FCache& Cache = Caches[static_cast<size_t>(Index)];
		for (size_t i = 0; i < Items.size(); ++i)
		{
			if (Items[i])
			{
				Cache.Items.push_back(Items[i]);
				Cache.Cooldowns.push_back(i < Cooldowns.size() ? Cooldowns[i] : 0);
			}
		}
		FEvent Event;
		Event.Kind = EEventKind::CacheAppeared;
		Event.Slot = Index;
		Event.Where = Cache.Pos;
		Report.Events.push_back(Event);
		return Index;
	}

	void FBattle::OnGone(FUnit& Unit, FTickReport& Report)
	{
		// Finished off: what it carried falls where it lay (3.4.4). A monster
		// leaves the board until its camp wakes again. A side's own unit's
		// items go back to its side's stash instead, for another to wear
		// (2026-10-01).
		std::vector<const FItemDef*> Dropped;
		std::vector<int> Cooldowns;
		for (int i = 0; i < Items::Slots; ++i)
		{
			if (Unit.Gear[i] && UsesStash(Unit))
			{
				ToStash(Unit.HomeTeam(), Unit.Gear[i], Unit.Cooldowns[ClassSlots + i]);
				FEvent Event;
				Event.Kind = EEventKind::ItemDropped;
				Event.Unit = Unit.Id;
				Event.Slot = -1;
				Event.Where = Unit.Pos;
				Event.Id = Unit.Gear[i]->Id;
				Report.Events.push_back(Event);
				Unit.Gear[i] = nullptr;
				Unit.Cooldowns[ClassSlots + i] = 0;
			}
			else if (Unit.Gear[i])
			{
				Dropped.push_back(Unit.Gear[i]);
				Cooldowns.push_back(Unit.Cooldowns[ClassSlots + i]);
				Unit.Gear[i] = nullptr;
				Unit.Cooldowns[ClassSlots + i] = 0;
			}
		}
		if (!Dropped.empty())
		{
			DropItems(Unit.Pos, Dropped, Cooldowns, Report);
		}
		if (Unit.bMonster)
		{
			Unit.bOffBoard = true;
			Unit.TamedTurns = 0;
			Unit.Team = 2;
		}
		if (Unit.PetOf >= 0)
		{
			Unit.bOffBoard = true;
			Unit.PetTurns = 0;
		}
	}

	void FBattle::PlacePets()
	{
		// One pet waiting off the board for each unit whose class can call one,
		// so every id is fixed before anybody moves. Its class is a monster file
		// (Content/Data/Monsters); one that is not loaded makes no pet.
		int NextId = 0;
		for (const FUnit& Unit : Units)
		{
			NextId = std::max(NextId, Unit.Id + 1);
		}
		const size_t Count = Units.size();
		for (size_t i = 0; i < Count; ++i)
		{
			const FUnit Owner = Units[i];
			if (Owner.bMonster || Owner.PetOf >= 0)
			{
				continue;
			}
			std::vector<std::string> Jobs;
			for (int Slot = 0; Slot < AbilitySlots; ++Slot)
			{
				const FAbility* Ability = Owner.Ability(Slot);
				if (Ability && Ability->Special == "pet" && FindJob(Ability->PetJob)
					&& std::find(Jobs.begin(), Jobs.end(), Ability->PetJob) == Jobs.end())
				{
					Jobs.push_back(Ability->PetJob);
				}
			}
			for (const std::string& Job : Jobs)
			{
				FUnit Pet;
				Pet.Id = NextId++;
				Pet.Team = Owner.Team;
				Pet.Job = Job;
				Pet.Stats = &FindJob(Job)->Stats;
				Pet.PetOf = Owner.Id;
				Pet.bOffBoard = true;
				Pet.Pos = Owner.Pos;
				Pet.Facing = Owner.Facing;
				Pet.Hp = 0;
				Pet.KoTicks = 0;
				Units.push_back(Pet);
			}
		}
	}

	void FBattle::CallPet(FUnit& Owner, const FAbility& Ability, const FVec2& Where, FTickReport& Report)
	{
		FUnit* Pet = nullptr;
		for (FUnit& Each : Units)
		{
			if (Each.PetOf == Owner.Id && Each.Job == Ability.PetJob)
			{
				Pet = &Each;
			}
		}
		if (!Pet)
		{
			return;
		}
		// Called again while it is out: it comes to the new spot, whole, its time
		// started afresh. Taken off first, so it is not in its own way.
		Pet->Hp = 0;
		auto Open = [this](const FVec2& Spot)
		{
			return InBounds(Spot) && Map.NodeWalkable(FMap::NodeOf(Spot)) && !UnitNear(Spot, Ground::UnitSpacing);
		};
		// Where it was aimed, else round there, else round its caller.
		FVec2 Spot = FMap::Snap(Where);
		bool bFound = Open(Spot);
		for (const FVec2& Middle : { Where, Owner.Pos })
		{
			for (int k = 0; k < 8 && !bFound; ++k)
			{
				const float Angle = 0.7853982f * static_cast<float>(k);
				const FVec2 Near = FMap::Snap(FVec2(Middle.X + 1.5f * std::cos(Angle), Middle.Y + 1.5f * std::sin(Angle)));
				if (Open(Near))
				{
					Spot = Near;
					bFound = true;
				}
			}
		}
		if (!bFound)
		{
			Pet->bOffBoard = true;
			return;
		}
		Pet->bOffBoard = false;
		Pet->Team = Owner.HomeTeam();
		Pet->Pos = Spot;
		Pet->Facing = Owner.Facing;
		Pet->Hp = Pet->MaxHp();
		Pet->KoTicks = 0;
		Pet->Tg = 0;
		Pet->bReady = false;
		Pet->Clock = 0;
		Pet->bMoved = false;
		Pet->bActed = false;
		Pet->Ult = 0;
		Pet->Statuses.clear();
		Pet->Buffs.clear();
		Pet->Casting = FCast();
		Pet->Channeling = FChannel();
		for (int Slot = 0; Slot < AbilitySlots; ++Slot)
		{
			Pet->Cooldowns[Slot] = 0;
		}
		// Its turns, counted as each begins (BecomeReady).
		Pet->PetTurns = Ability.PetTurns + 1;
		FEvent Event;
		Event.Kind = EEventKind::Teleported;
		Event.Unit = Pet->Id;
		Event.By = Owner.Id;
		Event.Where = Pet->Pos;
		Event.Id = "pet";
		Report.Events.push_back(Event);
	}

	void FBattle::SendPetAway(FUnit& Pet, FTickReport& Report)
	{
		Pet.Hp = 0;
		Pet.KoTicks = 0;
		Pet.Tg = 0;
		Pet.bReady = false;
		Pet.Clock = 0;
		Pet.bMoved = false;
		Pet.bActed = false;
		Pet.Statuses.clear();
		Pet.Buffs.clear();
		Pet.Casting = FCast();
		Pet.Channeling = FChannel();
		Pet.bOffBoard = true;
		Pet.PetTurns = 0;
		Report.Say(EEventKind::Gone, Pet.Id);
	}

	int FBattle::CacheNear(const FVec2& Point) const
	{
		int Best = -1;
		for (int i = 0; i < static_cast<int>(Caches.size()); ++i)
		{
			const FCache& Cache = Caches[static_cast<size_t>(i)];
			if (!Cache.Items.empty() && static_cast<double>(Cache.Pos.DistanceTo(Point)) <= Camp::TakeReach
				&& (Best < 0 || Cache.Pos.DistanceTo(Point) < Caches[static_cast<size_t>(Best)].Pos.DistanceTo(Point)))
			{
				Best = i;
			}
		}
		return Best;
	}

	void FBattle::PutInSlot(FUnit& Unit, int Slot, const FItemDef* Item, int Cooldown)
	{
		// Health follows the item: taking one heals by what it adds, leaving one
		// never leaves more health than the new most (the spec's 7, last rows).
		const int Before = Unit.MaxHp();
		Unit.Gear[Slot] = Item;
		Unit.Cooldowns[ClassSlots + Slot] = Cooldown;
		Unit.Toggled[ClassSlots + Slot] = false;
		const int After = Unit.MaxHp();
		if (After > Before && Unit.IsAlive())
		{
			Unit.Hp += After - Before;
		}
		Unit.Hp = std::min(Unit.Hp, After);
	}

	std::string FBattle::ValidateTake(int UnitId, int CacheIndex, const std::string& ItemId, int GearSlot) const
	{
		const FUnit* Unit = nullptr;
		for (const FUnit& Each : Units)
		{
			Unit = Each.Id == UnitId ? &Each : Unit;
		}
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
		// A side's unit takes into its side's stash, free; a monster into its own slots, with its action.
		const bool bToStash = UsesStash(*Unit);
		if (!bToStash && (Unit->bActed || Unit->IsCasting()))
		{
			return "Already used its action this turn.";
		}
		if (CacheIndex < 0 || CacheIndex >= static_cast<int>(Caches.size()))
		{
			return "Nothing lies there.";
		}
		const FCache& Cache = Caches[static_cast<size_t>(CacheIndex)];
		if (std::none_of(Cache.Items.begin(), Cache.Items.end(), [&](const FItemDef* Item) { return Item->Id == ItemId; }))
		{
			return "That item isn't there.";
		}
		if (static_cast<double>(Unit->Pos.DistanceTo(Cache.Pos)) > Camp::TakeReach)
		{
			return "Too far: stand next to the items.";
		}
		if (bToStash)
		{
			return std::string();
		}
		if (Unit->Carries(ItemId))
		{
			return "It already carries one.";
		}
		if (GearSlot < -1 || GearSlot >= Items::Slots)
		{
			return "No such slot.";
		}
		if (GearSlot == -1 && Unit->Gear[0] && Unit->Gear[1] && Unit->Gear[2])
		{
			return "Its slots are full: say which item to leave.";
		}
		return std::string();
	}

	std::string FBattle::ValidateDrop(int UnitId, int GearSlot) const
	{
		const FUnit* Unit = nullptr;
		for (const FUnit& Each : Units)
		{
			Unit = Each.Id == UnitId ? &Each : Unit;
		}
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
		if (GearSlot < 0 || GearSlot >= Items::Slots || !Unit->Gear[GearSlot])
		{
			return "Nothing in that slot.";
		}
		// Taking an item off is the turn: nothing else may have been done in it.
		if (UsesStash(*Unit) && (Unit->bActed || Unit->bMoved || Unit->IsCasting() || Unit->IsChanneling()))
		{
			return "Unequipping takes the whole turn: it has already moved or acted.";
		}
		return std::string();
	}

	std::string FBattle::ValidateEquip(int UnitId, const std::string& ItemId, int GearSlot) const
	{
		const FUnit* Unit = nullptr;
		for (const FUnit& Each : Units)
		{
			Unit = Each.Id == UnitId ? &Each : Unit;
		}
		if (!Unit || !Unit->IsAlive())
		{
			return "No such unit.";
		}
		if (!UsesStash(*Unit) || Unit->Team != Unit->HomeTeam())
		{
			return "Only a side's own units wear its items.";
		}
		const std::vector<FStashed>& Held = Stash[Unit->Team];
		if (std::none_of(Held.begin(), Held.end(), [&](const FStashed& Each) { return Each.Item && Each.Item->Id == ItemId; }))
		{
			return "That item isn't in the stash.";
		}
		if (Unit->Carries(ItemId))
		{
			return "It already wears one.";
		}
		if (GearSlot < -1 || GearSlot >= Items::Slots)
		{
			return "No such slot.";
		}
		if (GearSlot >= 0 && Unit->Gear[GearSlot])
		{
			return "That slot is taken: an item comes off only by spending a turn.";
		}
		if (GearSlot == -1 && Unit->Gear[0] && Unit->Gear[1] && Unit->Gear[2])
		{
			return "Its slots are full.";
		}
		return std::string();
	}

	void FBattle::ToStash(int Team, const FItemDef* Item, int Cooldown)
	{
		if (Item && (Team == 0 || Team == 1))
		{
			FStashed Put;
			Put.Item = Item;
			Put.Cooldown = Cooldown;
			Stash[Team].push_back(Put);
		}
	}

	void FBattle::ApplyEquip(FUnit& Unit, const std::string& ItemId, int GearSlot, FTickReport& Report)
	{
		std::vector<FStashed>& Held = Stash[Unit.Team];
		size_t At = 0;
		while (At < Held.size() && (!Held[At].Item || Held[At].Item->Id != ItemId))
		{
			++At;
		}
		if (At >= Held.size())
		{
			return;
		}
		int Slot = GearSlot;
		if (Slot < 0)
		{
			for (int i = Items::Slots - 1; i >= 0; --i)
			{
				Slot = Unit.Gear[i] ? Slot : i;
			}
		}
		if (Slot < 0 || Unit.Gear[Slot])
		{
			return;
		}
		const FStashed Taken = Held[At];
		Held.erase(Held.begin() + static_cast<std::ptrdiff_t>(At));
		PutInSlot(Unit, Slot, Taken.Item, Taken.Cooldown);
		FEvent Event;
		Event.Kind = EEventKind::ItemTaken;
		Event.Unit = Unit.Id;
		Event.Slot = -1;
		Event.Where = Unit.Pos;
		Event.Id = Taken.Item->Id;
		Report.Events.push_back(Event);
	}

	void FBattle::ApplyTake(FUnit& Unit, int CacheIndex, const std::string& ItemId, int GearSlot, FTickReport& Report)
	{
		FCache& Cache = Caches[static_cast<size_t>(CacheIndex)];
		size_t At = 0;
		while (At < Cache.Items.size() && Cache.Items[At]->Id != ItemId)
		{
			++At;
		}
		if (At >= Cache.Items.size())
		{
			return;
		}
		const FItemDef* Item = Cache.Items[At];
		const int Cooldown = At < Cache.Cooldowns.size() ? Cache.Cooldowns[At] : 0;
		Cache.Items.erase(Cache.Items.begin() + static_cast<std::ptrdiff_t>(At));
		if (At < Cache.Cooldowns.size())
		{
			Cache.Cooldowns.erase(Cache.Cooldowns.begin() + static_cast<std::ptrdiff_t>(At));
		}
		if (UsesStash(Unit))
		{
			// Into the side's stash, free: who wears it is chosen with Equip.
			ToStash(Unit.HomeTeam(), Item, Cooldown);
			FEvent Stashed;
			Stashed.Kind = EEventKind::ItemTaken;
			Stashed.Unit = Unit.Id;
			Stashed.Slot = CacheIndex;
			Stashed.Where = Cache.Pos;
			Stashed.Id = Item->Id;
			Report.Events.push_back(Stashed);
			return;
		}
		int Slot = GearSlot;
		if (Slot < 0)
		{
			for (int i = Items::Slots - 1; i >= 0; --i)
			{
				Slot = Unit.Gear[i] ? Slot : i;
			}
		}
		// Whatever was in the slot is left in the cache in its place.
		if (const FItemDef* Old = Unit.Gear[Slot])
		{
			Cache.Items.push_back(Old);
			Cache.Cooldowns.push_back(Unit.Cooldowns[ClassSlots + Slot]);
			FEvent Left;
			Left.Kind = EEventKind::ItemDropped;
			Left.Unit = Unit.Id;
			Left.Slot = CacheIndex;
			Left.Where = Cache.Pos;
			Left.Id = Old->Id;
			Report.Events.push_back(Left);
		}
		PutInSlot(Unit, Slot, Item, Cooldown);
		Unit.bActed = true;
		FEvent Event;
		Event.Kind = EEventKind::ItemTaken;
		Event.Unit = Unit.Id;
		Event.Slot = CacheIndex;
		Event.Where = Cache.Pos;
		Event.Id = Item->Id;
		Report.Events.push_back(Event);
	}

	void FBattle::PickUpAt(FUnit& Unit, FTickReport& Report)
	{
		// Only the two sides' own units; monsters and scavengers still take by order.
		if (!Unit.IsAlive() || Unit.bMonster || (Unit.Team != 0 && Unit.Team != 1))
		{
			return;
		}
		const int CacheIndex = CacheNear(Unit.Pos);
		if (CacheIndex < 0)
		{
			return;
		}
		// Everything lying there goes into the side's stash (2026-10-01): which
		// unit wears what is chosen afterwards, on the team's items screen.
		{
			FCache& Cache = Caches[static_cast<size_t>(CacheIndex)];
			for (size_t i = 0; i < Cache.Items.size(); ++i)
			{
				ToStash(Unit.HomeTeam(), Cache.Items[i], i < Cache.Cooldowns.size() ? Cache.Cooldowns[i] : 0);
				FEvent Event;
				Event.Kind = EEventKind::ItemTaken;
				Event.Unit = Unit.Id;
				Event.Slot = CacheIndex;
				Event.Where = Cache.Pos;
				Event.Id = Cache.Items[i]->Id;
				Report.Events.push_back(Event);
			}
			Cache.Items.clear();
			Cache.Cooldowns.clear();
		}
	}

	void FBattle::ApplyDrop(FUnit& Unit, int GearSlot, FTickReport& Report)
	{
		const FItemDef* Item = Unit.Gear[GearSlot];
		const int Cooldown = Unit.Cooldowns[ClassSlots + GearSlot];
		PutInSlot(Unit, GearSlot, nullptr, 0);
		if (UsesStash(Unit))
		{
			// Off, into the side's stash, and that is the turn.
			ToStash(Unit.HomeTeam(), Item, Cooldown);
			FEvent Off;
			Off.Kind = EEventKind::ItemDropped;
			Off.Unit = Unit.Id;
			Off.Slot = -1;
			Off.Where = Unit.Pos;
			Off.Id = Item->Id;
			Report.Events.push_back(Off);
			Unit.bMoved = true;
			Unit.bActed = true;
			EndTurn(Unit, false, Report);
			return;
		}
		const int Cache = DropItems(Unit.Pos, { Item }, { Cooldown }, Report);
		FEvent Event;
		Event.Kind = EEventKind::ItemDropped;
		Event.Unit = Unit.Id;
		Event.Slot = Cache;
		Event.Where = Unit.Pos;
		Event.Id = Item->Id;
		Report.Events.push_back(Event);
	}

	// ------------------------------------------------------------ sight

	bool FBattle::CanSeeUnit(int Team, const FUnit& Unit) const
	{
		if (Unit.Team == Team)
		{
			return true;
		}
		return CanSee(Team, Unit.Pos) && !Hidden(Team, Unit);
	}

	bool FBattle::Hidden(int Team, const FUnit& Unit) const
	{
		if (Unit.Team == Team)
		{
			return false;
		}
		// Vanished, or an ambusher still lying in wait: only someone right on top of it notices.
		bool bHidden = false;
		for (const FStatus& Status : Unit.Statuses)
		{
			const FStatusDef* Def = FindStatus(Status.Id);
			bHidden = bHidden || (Def && Def->bHidden);
		}
		if (!bHidden && Unit.bMonster && Unit.Mind == EMind::Resting)
		{
			const FMonsterInfo* Info = Unit.MonsterInfo();
			bHidden = Info && Info->Has(MonsterTrait::Ambush);
		}
		// In tall grass and not given away since its turn began (2026-10-04).
		if (!bHidden && !Unit.bSpotted && !Map.Grass.empty() && Map.InGrass(Unit.Pos))
		{
			bHidden = true;
		}
		if (!bHidden)
		{
			return false;
		}
		for (const FUnit& Other : Units)
		{
			if (Other.IsAlive() && Other.Team == Team && static_cast<double>(Other.Pos.DistanceTo(Unit.Pos)) <= Camp::AmbushReach)
			{
				return false;
			}
		}
		return true;
	}

	// -------------------------------------------------------- monster minds

	FVec2 FBattle::LeashAnchor(const FUnit& Monster) const
	{
		const FMonsterInfo* Info = Monster.MonsterInfo();
		if (!Info || Monster.Camp < 0 || Monster.Camp >= static_cast<int>(Camps.size()))
		{
			return Monster.Home;
		}
		const FCamp& Held = Camps[static_cast<size_t>(Monster.Camp)];
		if (Info->Temperament == ETemperament::GuardUnit)
		{
			// The nearest of its wards still standing.
			const FUnit* Nearest = nullptr;
			for (const int Id : Held.Members)
			{
				const FUnit* Ward = nullptr;
				for (const FUnit& Each : Units)
				{
					Ward = Each.Id == Id ? &Each : Ward;
				}
				if (Ward && Ward->Id != Monster.Id && Ward->IsAlive() && !Ward->bOffBoard
					&& (!Nearest || Ward->Pos.DistanceTo(Monster.Pos) < Nearest->Pos.DistanceTo(Monster.Pos)))
				{
					Nearest = Ward;
				}
			}
			return Nearest ? Nearest->Pos : Monster.Home;
		}
		if (Info->Temperament == ETemperament::Patrol && !Held.Route.empty())
		{
			FVec2 Best = Held.Route[0];
			for (const FVec2& Point : Held.Route)
			{
				Best = Point.DistanceTo(Monster.Pos) < Best.DistanceTo(Monster.Pos) ? Point : Best;
			}
			return Best;
		}
		return Monster.Home;
	}

	bool FBattle::HasQuarry(const FUnit& Monster) const
	{
		const FMonsterInfo* Info = Monster.MonsterInfo();
		if (!Info)
		{
			return false;
		}
		const FVec2 Anchor = LeashAnchor(Monster);
		const double Leash = Info->Temperament == ETemperament::GuardUnit ? Camp::WardLeash + Info->Leash
			: Info->Temperament == ETemperament::GuardPlace ? static_cast<double>(Info->Ring) + 2.0 : Info->Leash;
		for (const FUnit& Other : Units)
		{
			if (IsQuarry(Other) && static_cast<double>(Other.Pos.DistanceTo(Anchor)) <= Leash + 2.0
				&& (CanSeeUnit(Monster.Team, Other) || Other.Id == Monster.Grudge))
			{
				return true;
			}
		}
		return false;
	}

	bool FBattle::MonsterTriggered(const FUnit& Monster, int& OutBy) const
	{
		OutBy = -1;
		const FMonsterInfo* Info = Monster.MonsterInfo();
		if (!Info)
		{
			return false;
		}
		const bool bCalmable = Info->Temperament == ETemperament::Docile || Info->Temperament == ETemperament::Provoked;
		for (const FUnit& Other : Units)
		{
			if (!IsQuarry(Other) || (bCalmable && GearHas(Other, &FItemDef::bCalmsMonsters)))
			{
				continue;
			}
			const double FromHome = Other.Pos.DistanceTo(Monster.Home);
			const double FromMe = Other.Pos.DistanceTo(Monster.Pos);
			bool bSets = false;
			switch (Info->Temperament)
			{
			case ETemperament::Territorial:
			case ETemperament::GuardPlace:
				bSets = FromHome <= Info->Ring;
				break;
			case ETemperament::Aggressive:
				bSets = FromMe <= Info->Ring && HasLineOfSight(Monster.Pos, Other.Pos);
				break;
			case ETemperament::Patrol:
				bSets = FromMe <= SightOf(Monster) && HasLineOfSight(Monster.Pos, Other.Pos);
				break;
			case ETemperament::GuardUnit:
				if (Monster.Camp >= 0 && Monster.Camp < static_cast<int>(Camps.size()))
				{
					for (const int Id : Camps[static_cast<size_t>(Monster.Camp)].Members)
					{
						for (const FUnit& Ward : Units)
						{
							if (Ward.Id == Id && Ward.Id != Monster.Id && Ward.IsAlive() && !Ward.bOffBoard
								&& static_cast<double>(Ward.Pos.DistanceTo(Other.Pos)) <= Camp::WardReach)
							{
								bSets = true;
							}
						}
					}
				}
				break;
			default:
				break;
			}
			// An ambusher waits until someone is right on it.
			if (bSets && Info->Has(MonsterTrait::Ambush) && FromMe > Camp::AmbushReach + 1.0)
			{
				bSets = false;
			}
			if (bSets)
			{
				OutBy = Other.Id;
				return true;
			}
		}
		return false;
	}

	void FBattle::AlertMonster(FUnit& Monster, int By, FTickReport& Report)
	{
		// Its bonded campmates with it: a provoked pack comes as one, a guardian
		// comes for whoever troubles its ward. The docile never come.
		std::vector<FUnit*> Setting{ &Monster };
		if (Monster.Camp >= 0 && Monster.Camp < static_cast<int>(Camps.size()))
		{
			for (const int Id : Camps[static_cast<size_t>(Monster.Camp)].Members)
			{
				FUnit* Mate = FindUnit(Id);
				if (Mate && Mate != &Monster && Mate->IsAlive() && !Mate->bOffBoard && Mate->Team == 2)
				{
					const FMonsterInfo* Info = Mate->MonsterInfo();
					if (Info && Info->Temperament != ETemperament::Docile && Info->Temperament != ETemperament::Skittish)
					{
						Setting.push_back(Mate);
					}
				}
			}
		}
		for (FUnit* Each : Setting)
		{
			const FMonsterInfo* Info = Each->MonsterInfo();
			if (!Info || Info->Temperament == ETemperament::Docile || Info->Temperament == ETemperament::Skittish)
			{
				continue;
			}
			if (Each->Mind == EMind::Resting || Each->Mind == EMind::Returning)
			{
				Each->Mind = EMind::Alert;
				if (By >= 0 && Each->Grudge < 0)
				{
					Each->Grudge = By;
					Each->GrudgeTurns = Camp::GrudgeTurns;
				}
				FEvent Event;
				Event.Kind = EEventKind::MonsterAlert;
				Event.Unit = Each->Id;
				Event.By = By;
				Event.Where = Each->Pos;
				Report.Events.push_back(Event);

				// A lookout's cry wakes the nearest waiting camp early.
				if (Info->Has(MonsterTrait::Lookout))
				{
					int Nearest = -1;
					for (int i = 0; i < static_cast<int>(Camps.size()); ++i)
					{
						const FCamp& Other = Camps[static_cast<size_t>(i)];
						if (Other.State == ECampState::Waiting && i != Each->Camp && Other.Tier < 3
							&& static_cast<double>(Other.Spot.DistanceTo(Each->Pos)) <= Camp::LookoutReach
							&& (Nearest < 0 || Other.Spot.DistanceTo(Each->Pos) < Camps[static_cast<size_t>(Nearest)].Spot.DistanceTo(Each->Pos)))
						{
							Nearest = i;
						}
					}
					if (Nearest >= 0)
					{
						FCamp& Woken = Camps[static_cast<size_t>(Nearest)];
						Woken.Timer = std::min(Woken.Timer, Camp::Warning[Woken.Tier] * TicksPerSecond + 1);
					}
				}
			}
		}
	}

	void FBattle::MonsterHurt(FUnit& Monster, const FUnit& Attacker, double Flank, FTickReport& Report)
	{
		const FMonsterInfo* Info = Monster.MonsterInfo();
		if (!Info)
		{
			return;
		}
		// A boss's phases follow its health down, and never back up.
		while (Monster.Phase < static_cast<int>(Info->Phases.size()) && Monster.MaxHp() > 0
			&& Monster.Hp * 100 < Info->Phases[static_cast<size_t>(Monster.Phase)].BelowPercent * Monster.MaxHp())
		{
			++Monster.Phase;
			FEvent Event;
			Event.Kind = EEventKind::PhaseChanged;
			Event.Unit = Monster.Id;
			Event.Amount = Monster.Phase;
			Event.Where = Monster.Pos;
			Report.Events.push_back(Event);
		}
		// Hits from behind, three of them, stagger it. A boss winding up an attack
		// can be staggered too, and a stagger breaks the wind-up (2026-10-02, B).
		const bool bWindUp = Info->Tier >= 3 && Monster.IsCasting();
		if ((Info->Has(MonsterTrait::Stagger) || bWindUp) && Flank >= Tuning.BackBonus && Tuning.BackBonus > Tuning.SideBonus)
		{
			if (++Monster.Stagger >= Camp::StaggerHits)
			{
				Monster.Stagger = 0;
				if (Monster.IsCasting())
				{
					FEvent Broken;
					Broken.Kind = EEventKind::CastFizzled;
					Broken.Unit = Monster.Id;
					Broken.Slot = Monster.Casting.Slot;
					Broken.Where = Monster.Pos;
					Report.Events.push_back(Broken);
					Monster.Casting = FCast();
				}
				AddStatus(Monster, "staggered", 1);
				FEvent Event;
				Event.Kind = EEventKind::Staggered;
				Event.Unit = Monster.Id;
				Event.Where = Monster.Pos;
				Report.Events.push_back(Event);
			}
		}
		if (Monster.Team != 2 || Attacker.Team == Monster.Team)
		{
			return;  // tamed, or a friendly hit
		}
		const bool bCalm = GearHas(Attacker, &FItemDef::bCalmsMonsters)
			&& (Info->Temperament == ETemperament::Docile || Info->Temperament == ETemperament::Provoked);
		if (bCalm)
		{
			return;
		}
		Monster.Grudge = Attacker.Id;
		Monster.GrudgeTurns = Camp::GrudgeTurns;
		if (Info->Temperament == ETemperament::Docile)
		{
			Monster.Mind = EMind::Fleeing;
			Monster.FleeTurns = Camp::DocileFleeTurns;
		}
		else if (Info->Temperament == ETemperament::Skittish)
		{
			Monster.Mind = EMind::Fleeing;
		}
		else
		{
			AlertMonster(Monster, Attacker.Id, Report);
		}
		// Whoever guards it comes too.
		if (Monster.Camp >= 0 && Monster.Camp < static_cast<int>(Camps.size()))
		{
			for (const int Id : Camps[static_cast<size_t>(Monster.Camp)].Members)
			{
				FUnit* Guard = FindUnit(Id);
				const FMonsterInfo* GuardInfo = Guard ? Guard->MonsterInfo() : nullptr;
				if (Guard && Guard != &Monster && Guard->IsAlive() && !Guard->bOffBoard && Guard->Team == 2
					&& GuardInfo && GuardInfo->Temperament == ETemperament::GuardUnit)
				{
					AlertMonster(*Guard, Attacker.Id, Report);
				}
			}
		}
	}

	bool FBattle::MonsterTurnStarts(FUnit& Unit, FTickReport& Report)
	{
		// Tamed: its own turns count down on the side that tamed it, then it
		// goes back to being wild, walking home.
		if (Unit.TamedTurns > 0)
		{
			if (--Unit.TamedTurns > 0)
			{
				return false;
			}
			Unit.Team = 2;
			Unit.Mind = EMind::Returning;
			Unit.Grudge = -1;
			Unit.GrudgeTurns = 0;
			FEvent Event;
			Event.Kind = EEventKind::Tamed;
			Event.Unit = Unit.Id;
			Event.Amount = 0;
			Event.Where = Unit.Pos;
			Report.Events.push_back(Event);
		}
		const FMonsterInfo* Info = Unit.MonsterInfo();
		if (!Info || Unit.Team != 2)
		{
			return false;
		}
		if (Unit.GrudgeTurns > 0 && --Unit.GrudgeTurns == 0)
		{
			Unit.Grudge = -1;
		}
		if (Info->Tier >= 3 && Tuning.BossHunt >= 0.5)
		{
			UpdateHunt(Unit, Report);
		}
		FCamp* Held = Unit.Camp >= 0 && Unit.Camp < static_cast<int>(Camps.size()) ? &Camps[static_cast<size_t>(Unit.Camp)] : nullptr;
		int By = -1;
		switch (Unit.Mind)
		{
		case EMind::Resting:
			if (Info->Temperament == ETemperament::Skittish)
			{
				for (const FUnit& Other : Units)
				{
					if (IsQuarry(Other) && static_cast<double>(Other.Pos.DistanceTo(Unit.Pos)) <= SightOf(Unit)
						&& HasLineOfSight(Unit.Pos, Other.Pos))
					{
						Unit.Mind = EMind::Fleeing;
					}
				}
				return false;
			}
			if (MonsterTriggered(Unit, By))
			{
				// The fair warning: it shows it has seen them, and gives up this
				// turn to do so. It fights from its next.
				AlertMonster(Unit, By, Report);
				EndTurn(Unit, false, Report);
				return true;
			}
			if (Held && !Held->Route.empty()
				&& static_cast<double>(Unit.Pos.DistanceTo(Held->Route[static_cast<size_t>(Unit.RouteStep) % Held->Route.size()])) <= 1.0)
			{
				Unit.RouteStep = (Unit.RouteStep + 1) % static_cast<int>(Held->Route.size());
			}
			return false;

		case EMind::Alert:
			Unit.Mind = EMind::Fighting;
			return false;

		case EMind::Fighting:
			if (!HasQuarry(Unit) && Unit.GrudgeTurns <= 0)
			{
				Unit.Mind = EMind::Returning;
			}
			return false;

		case EMind::Returning:
			if (static_cast<double>(Unit.Pos.DistanceTo(LeashAnchor(Unit))) <= 1.0
				|| (Info->Temperament == ETemperament::GuardUnit && static_cast<double>(Unit.Pos.DistanceTo(LeashAnchor(Unit))) <= Camp::WardLeash))
			{
				// Home: it mends, and is at rest again.
				Unit.Mind = EMind::Resting;
				Unit.Hp = Unit.MaxHp();
				Unit.Statuses.clear();
				Unit.Buffs.clear();
				Unit.Stagger = 0;
				return false;
			}
			if (MonsterTriggered(Unit, By))
			{
				AlertMonster(Unit, By, Report);
				Unit.Mind = EMind::Fighting;
			}
			return false;

		case EMind::Fleeing:
			if (Info->Temperament == ETemperament::Skittish)
			{
				// Off the board at the edge, with whatever it carries.
				const FVec2 Size = Map.SizeMeters();
				const float Edge = std::min(std::min(Unit.Pos.X, Unit.Pos.Y), std::min(Size.X - Unit.Pos.X, Size.Y - Unit.Pos.Y));
				if (Edge <= 1.5f)
				{
					FEvent Event;
					Event.Kind = EEventKind::Escaped;
					Event.Unit = Unit.Id;
					Event.Where = Unit.Pos;
					Report.Events.push_back(Event);
					for (const FItemDef*& Item : Unit.Gear)
					{
						Item = nullptr;
					}
					Unit.Hp = 0;
					Unit.KoTicks = 0;
					Unit.bReady = false;
					Unit.bOffBoard = true;
					return true;
				}
				return false;
			}
			if (Unit.FleeTurns > 0)
			{
				--Unit.FleeTurns;
				return false;
			}
			Unit.Mind = EMind::Returning;
			return false;
		}
		return false;
	}

	void FBattle::UseShrine(FUnit& Unit, FTickReport& Report)
	{
		if (Unit.bMonster && Unit.Team == 2)
		{
			return;
		}
		for (int i = 0; i < static_cast<int>(Camps.size()); ++i)
		{
			FCamp& Held = Camps[static_cast<size_t>(i)];
			if (Held.bShrine && Held.ShrineRest <= 0 && static_cast<double>(Unit.Pos.DistanceTo(Held.Spot)) <= Camp::ShrineReach)
			{
				AddStatus(Unit, "surge", 2);
				if (!GearHas(Unit, &FItemDef::bShrineKey))
				{
					Held.ShrineRest = Camp::ShrineRestSeconds * TicksPerSecond;
				}
				FEvent Event;
				Event.Kind = EEventKind::ShrineUsed;
				Event.Unit = Unit.Id;
				Event.Slot = i;
				Event.Where = Held.Spot;
				Report.Events.push_back(Event);
				return;
			}
		}
	}

	// ----------------------------------------------------------- specials

	void FBattle::ApplySpecial(FUnit& User, const FAbility& Ability, const FVec2& Target, FUnit* Struck, FTickReport& Report)
	{
		if (Ability.Special == "blink")
		{
			// To the aim point, if a unit may stand there. Once, not per target.
			const FVec2 Spot = FMap::Snap(Target);
			const FUnit* Blocking = UnitNear(Spot, Ground::UnitSpacing);
			if (Struck == nullptr && InBounds(Spot) && Map.NodeWalkable(FMap::NodeOf(Spot))
				&& (Blocking == nullptr || Blocking->Id == User.Id))
			{
				User.Pos = Spot;
				FEvent Event;
				Event.Kind = EEventKind::Teleported;
				Event.Unit = User.Id;
				Event.Where = Spot;
				Report.Events.push_back(Event);
			}
			return;
		}
		if (!Struck)
		{
			return;
		}
		if (Ability.Special == "swap" && Struck->Id != User.Id)
		{
			std::swap(User.Pos, Struck->Pos);
			for (FUnit* Moved : { &User, Struck })
			{
				FEvent Event;
				Event.Kind = EEventKind::Teleported;
				Event.Unit = Moved->Id;
				Event.Where = Moved->Pos;
				Report.Events.push_back(Event);
			}
		}
		else if (Ability.Special == "tame")
		{
			const FMonsterInfo* Info = Struck->MonsterInfo();
			if (Info && Struck->Team == 2 && Info->Tier <= 1 && Struck->Hp * 2 < Struck->MaxHp() && (User.Team == 0 || User.Team == 1))
			{
				Struck->Team = User.Team;
				// Its next three turns, counted as each begins (MonsterTurnStarts).
				Struck->TamedTurns = Camp::TameTurns + 1;
				Struck->Mind = EMind::Fighting;
				Struck->Grudge = -1;
				Struck->GrudgeTurns = 0;
				FEvent Event;
				Event.Kind = EEventKind::Tamed;
				Event.Unit = Struck->Id;
				Event.By = User.Id;
				Event.Amount = Camp::TameTurns;
				Event.Where = Struck->Pos;
				Report.Events.push_back(Event);
			}
		}
		else if (Ability.Special == "summon" && Struck->Id == User.Id)
		{
			// Wakes the camp's reserves round the summoner.
			if (User.Camp >= 0 && User.Camp < static_cast<int>(Camps.size()))
			{
				FCamp& Held = Camps[static_cast<size_t>(User.Camp)];
				const float Angle = 6.2831853f / std::max<size_t>(1, Held.Reserves.size());
				for (size_t r = 0; r < Held.Reserves.size(); ++r)
				{
					FUnit* Add = FindUnit(Held.Reserves[r]);
					if (!Add || !Add->bOffBoard)
					{
						continue;
					}
					FVec2 Spot = FMap::Snap(FVec2(User.Pos.X + 2.5f * std::cos(Angle * static_cast<float>(r)), User.Pos.Y + 2.5f * std::sin(Angle * static_cast<float>(r))));
					if (!InBounds(Spot) || !Map.NodeWalkable(FMap::NodeOf(Spot)) || UnitNear(Spot, Ground::UnitSpacing))
					{
						Spot = User.Pos;
					}
					Add->bOffBoard = false;
					Add->Pos = Spot;
					Add->Home = User.Home;
					Add->Hp = Add->MaxHp();
					Add->Tg = 0;
					Add->bReady = false;
					Add->Statuses.clear();
					Add->Buffs.clear();
					Add->Mind = EMind::Fighting;
					Add->Grudge = User.Grudge;
					Add->GrudgeTurns = Camp::GrudgeTurns;
					Add->Team = 2;
					Report.Say(EEventKind::Moved, Add->Id);
				}
			}
		}
		else if (Ability.Special == "rewind" && Struck->Id == User.Id && !User.bRewindUsed && User.LastHurt > 0)
		{
			User.bRewindUsed = true;
			User.Hp = std::min(User.MaxHp(), User.Hp + User.LastHurt);
			FEvent Event;
			Event.Kind = EEventKind::Saved;
			Event.Unit = User.Id;
			Event.Amount = User.LastHurt;
			Event.Where = User.Pos;
			Event.Id = "rewind";
			Report.Events.push_back(Event);
		}
	}

	// ------------------------------------------- noise, the hunt and the claim
	// 2026-10-02, "Camps and Bosses Mockups" A, C and D.

	void FBattle::MakeNoise(const FVec2& Where, const FUnit& By, int Amount, FTickReport& Report)
	{
		if (Camps.empty() || Amount <= 0 || By.bMonster || (By.Team != 0 && By.Team != 1))
		{
			return;
		}
		for (int i = 0; i < static_cast<int>(Camps.size()); ++i)
		{
			FCamp& Held = Camps[static_cast<size_t>(i)];
			// Only a camp still to wake: not the boss's, not one cleared for good,
			// not one already giving its warning.
			if (Held.State != ECampState::Waiting || Held.Kind < 0 || (Held.Wakes > 0 && Tuning.CampRespawn < 0.5)
				|| Held.Timer <= Camp::Warning[Held.Tier] * TicksPerSecond + 1
				|| static_cast<double>(Where.DistanceTo(Held.Spot)) > Camp::NoiseReach)
			{
				continue;
			}
			Held.Noise = std::min(Camp::NoiseFull, Held.Noise + Amount);
			Held.NoiseQuiet = 0;
			Held.NoiseBy[By.Team] += Amount;
			Held.LoudUnit[By.Team] = By.Id;
			Held.LastLoudSide = By.Team;
			FEvent Event;
			Event.Kind = EEventKind::CampNoise;
			Event.Slot = i;
			Event.Unit = By.Id;
			Event.Amount = Held.Noise;
			Event.Where = Held.Spot;
			Report.Events.push_back(Event);
			if (Held.Noise >= Camp::NoiseFull)
			{
				// Full: it gives its warning now, and wakes when that runs out.
				Held.Timer = Camp::Warning[Held.Tier] * TicksPerSecond + 1;
				Held.bNoiseWake = true;
			}
		}
	}

	void FBattle::BossHurtBy(FUnit& Boss, const FUnit& Attacker, int Amount)
	{
		const FMonsterInfo* Info = Boss.MonsterInfo();
		if (!Info || Info->Tier < 3 || Amount <= 0 || Attacker.bMonster)
		{
			return;
		}
		auto Found = std::find_if(Boss.Wrath.begin(), Boss.Wrath.end(),
			[&Attacker](const std::pair<int, int>& Entry) { return Entry.first == Attacker.Id; });
		if (Found != Boss.Wrath.end())
		{
			Found->second += Amount;
		}
		else
		{
			Boss.Wrath.emplace_back(Attacker.Id, Amount);
		}
		const int Side = Attacker.HomeTeam();
		if (Side == 0 || Side == 1)
		{
			Boss.Claim[Side] += Amount;
		}
	}

	void FBattle::UpdateHunt(FUnit& Boss, FTickReport& Report)
	{
		auto Prey = [this](int Id) -> FUnit*
		{
			FUnit* Each = FindUnit(Id);
			return Each && !Each->bOffBoard && IsQuarry(*Each) ? Each : nullptr;
		};
		// The one it hunts: gone, or out of its sight for too long, and it lets go.
		if (Boss.HuntTarget >= 0)
		{
			FUnit* Held = Prey(Boss.HuntTarget);
			bool bLetGo = Held == nullptr;
			if (Held && !CanSeeUnit(Boss.Team, *Held))
			{
				if (++Boss.HuntLost >= Camp::ScentTurns)
				{
					// The scent is lost: it forgets that one, and turns to the next.
					bLetGo = true;
					const int Lost = Boss.HuntTarget;
					Boss.Wrath.erase(std::remove_if(Boss.Wrath.begin(), Boss.Wrath.end(),
						[Lost](const std::pair<int, int>& Entry) { return Entry.first == Lost; }), Boss.Wrath.end());
				}
			}
			else if (Held)
			{
				Boss.HuntLost = 0;
			}
			if (bLetGo)
			{
				if (FUnit* Was = FindUnit(Boss.HuntTarget))
				{
					RemoveStatus(*Was, "hunted");
				}
				Boss.HuntTarget = -1;
				Boss.HuntLost = 0;
			}
		}
		// Whoever has hurt it most, of those it could go for; the first to hurt it on a tie.
		int Best = -1;
		int Most = 0;
		for (const std::pair<int, int>& Entry : Boss.Wrath)
		{
			if (Entry.second > Most && Prey(Entry.first))
			{
				Best = Entry.first;
				Most = Entry.second;
			}
		}
		if (Best >= 0 && Best != Boss.HuntTarget)
		{
			if (FUnit* Was = FindUnit(Boss.HuntTarget))
			{
				RemoveStatus(*Was, "hunted");
			}
			Boss.HuntTarget = Best;
			Boss.HuntLost = 0;
			FUnit& Hunted = *FindUnit(Best);
			AddStatus(Hunted, "hunted", 99, 0, Boss.Id);
			FEvent Event;
			Event.Kind = EEventKind::Hunting;
			Event.Unit = Boss.Id;
			Event.By = Best;
			Event.Where = Hunted.Pos;
			Report.Events.push_back(Event);
		}
		if (Boss.HuntTarget >= 0)
		{
			// Its grudge is its prey, so it walks after it as it walks after a grudge.
			Boss.Grudge = Boss.HuntTarget;
			Boss.GrudgeTurns = std::max(Boss.GrudgeTurns, 1);
		}
	}

	void FBattle::ClaimBoss(FUnit& Boss, const FUnit& Killer, FTickReport& Report)
	{
		const FMonsterInfo* Info = Boss.MonsterInfo();
		if (!Info || Info->Tier < 3)
		{
			return;
		}
		if (FUnit* Prey = FindUnit(Boss.HuntTarget))
		{
			RemoveStatus(*Prey, "hunted");
		}
		Boss.HuntTarget = -1;
		Boss.HuntLost = 0;
		const int Side = Killer.HomeTeam();
		if (Tuning.BossClaim >= 0.5 && (Side == 0 || Side == 1))
		{
			// The last blow takes the boon, for every one of its side still standing.
			for (FUnit& Each : Units)
			{
				if (Each.IsAlive() && !Each.bMonster && Each.Team == Side)
				{
					AddStatus(Each, "boon", Camp::BoonTurns);
				}
			}
			FEvent Claimed;
			Claimed.Kind = EEventKind::BossClaimed;
			Claimed.Unit = Boss.Id;
			Claimed.By = Killer.Id;
			Claimed.Amount = Side;
			Claimed.Where = Boss.Pos;
			Report.Events.push_back(Claimed);
			// And the other side, for a big enough share of the work, a rare item.
			const int Other = 1 - Side;
			if (Boss.MaxHp() > 0 && Boss.Claim[Other] * 100 >= Camp::ClaimSharePercent * Boss.MaxHp())
			{
				std::vector<const FItemDef*> Prize;
				RollLoot(2, 1, Prize);
				if (!Prize.empty())
				{
					ToStash(Other, Prize[0], 0);
					FEvent Share;
					Share.Kind = EEventKind::ClaimShare;
					Share.Unit = Boss.Id;
					Share.Amount = Other;
					Share.Id = Prize[0]->Id;
					Share.Where = Boss.Pos;
					Report.Events.push_back(Share);
				}
			}
		}
		Boss.Wrath.clear();
		Boss.Claim[0] = 0;
		Boss.Claim[1] = 0;
	}
}
