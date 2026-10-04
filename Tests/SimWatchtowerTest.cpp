// Watchtowers: where they stand, what taking one costs, and what holding one shows.
//
// Not Godot's (Docs/design/feat-objectives.md), so there is no Godot dump to
// measure against. What this holds the rules to instead:
//
//   - a battle that asks for no towers has none, and its dice are untouched by
//     the placing either way, so every Godot parity test still means what it did;
//   - towers stand where the design says: in mirrored pairs (an odd one in the
//     middle), on ground both sides can walk to, away from where anybody starts
//     and from each other -- and the same seed always puts them in the same
//     places, which is what lets a match's two machines and a replay agree;
//   - taking one costs the turns the rule number says, each one a whole turn,
//     and is refused when it should be;
//   - a held tower sees for its side, and only for its side;
//   - the computer takes towers in a battle of its own, legally, and that battle
//     replays from its orders to the same checksum.
//
// Run with the maps folder: SimWatchtowerTest <Content/Data/Maps>.

#include "SimAI.h"
#include "SimBattle.h"
#include "SimOrderText.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace TMSim;

namespace
{
	int Failures = 0;

	void Fail(const std::string& What)
	{
		if (Failures < 20)
		{
			std::printf("  %s\n", What.c_str());
		}
		++Failures;
	}

	std::string ReadAll(const std::filesystem::path& Path)
	{
		std::ifstream In(Path, std::ios::binary);
		std::stringstream Buffer;
		Buffer << In.rdbuf();
		return Buffer.str();
	}

	/** Two sides of four on a map, as the director deals them. */
	void Deal(FBattle& Battle, const FMapDef& Map, uint64_t Seed)
	{
		const char* Roster[8] =
		{
			"knight", "archer", "black_mage", "white_mage",
			"knight", "archer", "black_mage", "white_mage"
		};
		Battle.Map.BuildMirrored(Map.Top);
		const FVec2 Size = Battle.Map.SizeMeters();
		Battle.SpawnPoints[0] = Map.Spawns[0];
		Battle.SpawnPoints[1] = FVec2(Size.X - Map.Spawns[0].X, Size.Y - Map.Spawns[0].Y);
		for (int Index = 0; Index < 8; ++Index)
		{
			FUnit Unit;
			Unit.Id = Index;
			Unit.Team = Index < 4 ? 0 : 1;
			Unit.Job = Roster[Index];
			const FVec2 Spot = Map.Spawns[static_cast<size_t>(Index % 4)];
			Unit.Pos = Index < 4 ? Spot : FVec2(Size.X - Spot.X, Size.Y - Spot.Y);
			Battle.Units.push_back(Unit);
		}
		Battle.Start(Seed);
	}

	const FUnit* WaitingOn(const FBattle& Battle)
	{
		for (const FUnit& Unit : Battle.Units)
		{
			if (Unit.IsAlive() && Unit.bReady)
			{
				return &Unit;
			}
		}
		return nullptr;
	}

	/** Runs the clock until a unit of this side is ready, and gives it back. */
	FUnit* NextReady(FBattle& Battle, int Team)
	{
		for (int Tick = 0; Tick < 5000; ++Tick)
		{
			for (FUnit& Unit : Battle.Units)
			{
				if (Unit.IsAlive() && Unit.bReady)
				{
					if (Unit.Team == Team)
					{
						return &Unit;
					}
					// Somebody else's turn: they give it up.
					FTickReport Report;
					Battle.Apply(FOrder::MakeEndTurn(Unit.Id, Unit.Serial), Report);
				}
			}
			FTickReport Report;
			Battle.Advance(1, Report);
		}
		return nullptr;
	}

	/** Puts a unit next to a tower, as if it had walked there: on the nearest free node within reach. */
	void StandAt(FBattle& Battle, FUnit& Unit, const FWatchtower& Tower)
	{
		for (int Ring = 0; Ring < 5; ++Ring)
		{
			for (int DY = -Ring; DY <= Ring; ++DY)
			{
				for (int DX = -Ring; DX <= Ring; ++DX)
				{
					const FVec2 Spot = FMap::Snap(FVec2(Tower.Pos.X + static_cast<float>(DX) * 0.5f, Tower.Pos.Y + static_cast<float>(DY) * 0.5f));
					if (Battle.Map.NodeWalkable(FMap::NodeOf(Spot)) && Spot.DistanceTo(Tower.Pos) <= Watchtower::Reach)
					{
						const FUnit* There = Battle.UnitNear(Spot, Ground::UnitSpacing);
						if (!There || There->Id == Unit.Id)
						{
							Unit.Pos = Spot;
							return;
						}
					}
				}
			}
		}
	}

	std::vector<FMapDef> LoadMaps(const char* Folder)
	{
		std::vector<FMapDef> Out;
		Out.push_back(FindMap("highlands"));
		if (!Folder || !std::filesystem::exists(Folder))
		{
			return Out;
		}
		std::vector<std::filesystem::path> Paths;
		for (const auto& Entry : std::filesystem::directory_iterator(Folder))
		{
			const std::string Name = Entry.path().filename().string();
			if (Name.size() > 11 && Name.substr(Name.size() - 11) == ".tmmap.json")
			{
				Paths.push_back(Entry.path());
			}
		}
		std::sort(Paths.begin(), Paths.end());
		for (const auto& Path : Paths)
		{
			FMapDef Def;
			if (ReadMapFile(ReadAll(Path), Def).empty())
			{
				Out.push_back(Def);
			}
		}
		return Out;
	}
}

int main(int ArgCount, char** Args)
{
	const std::vector<FMapDef> Maps = LoadMaps(ArgCount >= 2 ? Args[1] : nullptr);
	const FMapDef* Big = nullptr;
	for (const FMapDef& Map : Maps)
	{
		if (Map.Id == "riverwatch_fords")
		{
			Big = &Map;
		}
	}
	if (!Big)
	{
		Big = &Maps.back();
	}

	// No towers unless asked for, and the dice untouched by asking.
	{
		const int Before = Failures;
		FBattle Plain;
		Deal(Plain, *Big, 777);
		if (!Plain.Watchtowers.empty())
		{
			Fail("a battle that asks for no towers should have none");
		}
		FBattle Towered;
		Towered.Tuning.WatchtowerCount = 4.0;
		Deal(Towered, *Big, 777);
		if (Towered.Rng.GetState() != Plain.Rng.GetState())
		{
			Fail("placing towers should not touch the battle's dice");
		}
		if (Failures == Before)
		{
			std::printf("no towers unless asked for, and the battle's dice are the same either way\n");
		}
	}

	// Where they stand, on every map, for every count and many seeds.
	{
		const int Before = Failures;
		int Battles = 0;
		int Short = 0;
		for (const FMapDef& Map : Maps)
		{
			for (int Count = 0; Count <= 8; ++Count)
			{
				for (uint64_t Seed = 1; Seed <= 40; ++Seed)
				{
					FBattle Battle;
					Battle.Tuning.WatchtowerCount = Count;
					Deal(Battle, Map, Seed);
					++Battles;
					const std::vector<FWatchtower>& Towers = Battle.Watchtowers;
					if (static_cast<int>(Towers.size()) > Count)
					{
						Fail(Map.Id + ": more towers than asked for");
					}
					if (static_cast<int>(Towers.size()) < Count)
					{
						++Short;
					}
					const FVec2 Size = Battle.Map.SizeMeters();
					// The odd one in the very middle, the rest in twins.
					size_t First = 0;
					if (Count % 2 == 1 && !Towers.empty()
						&& Towers[0].Pos == FVec2(Size.X * 0.5f, Size.Y * 0.5f))
					{
						First = 1;
					}
					if ((Towers.size() - First) % 2 != 0)
					{
						Fail(Map.Id + ": a tower without its twin");
						continue;
					}
					for (size_t i = First; i + 1 < Towers.size(); i += 2)
					{
						const FVec2 Blue = Towers[i].Pos;
						const FVec2 Red = Towers[i + 1].Pos;
						if (Red != FVec2(Size.X - Blue.X, Size.Y - Blue.Y) || Blue.Y >= Size.Y * 0.5f)
						{
							Fail(Map.Id + ": a pair that is not blue's tower and its twin on red's half");
						}
					}
					const std::vector<double> Walk = Battle.DistanceFrom(Battle.SpawnPoints[0]);
					for (size_t i = 0; i < Towers.size(); ++i)
					{
						const FVec2 At = Towers[i].Pos;
						const FNode Node = FMap::NodeOf(At);
						if (!Battle.Map.NodeWalkable(Node) || Battle.HazardAt(At) != 0 || Battle.IsCover(At)
							|| !std::isfinite(Walk[static_cast<size_t>(Battle.Map.NodeIndex(Node))]))
						{
							Fail(Map.Id + ": a tower on ground nobody can walk to, or on a hazard");
						}
						if (std::min(std::min(At.X, Size.X - At.X), std::min(At.Y, Size.Y - At.Y)) < static_cast<float>(Watchtower::FromEdge))
						{
							Fail(Map.Id + ": a tower against the edge of the map");
						}
						for (const FUnit& Unit : Battle.Units)
						{
							if (At.DistanceTo(Unit.Pos) < Watchtower::AwayFromStart)
							{
								Fail(Map.Id + ": a tower where somebody starts");
							}
						}
						for (size_t j = i + 1; j < Towers.size(); ++j)
						{
							if (At.DistanceTo(Towers[j].Pos) < Watchtower::Apart)
							{
								Fail(Map.Id + ": two towers too close");
							}
						}
						if (Towers[i].Owner != -1 || Towers[i].Capturer != -1 || Towers[i].Progress != 0)
						{
							Fail(Map.Id + ": a tower that starts held");
						}
					}
					// The same seed, the same places.
					FBattle Again;
					Again.Tuning.WatchtowerCount = Count;
					Deal(Again, Map, Seed);
					if (Again.Checksum() != Battle.Checksum())
					{
						Fail(Map.Id + ": the same seed put the towers somewhere else");
					}
				}
			}
			// Different seeds, different places (it is meant to be random).
			FBattle One;
			One.Tuning.WatchtowerCount = 2;
			Deal(One, Map, 1);
			bool bMoved = false;
			for (uint64_t Seed = 2; Seed <= 10 && !One.Watchtowers.empty(); ++Seed)
			{
				FBattle Other;
				Other.Tuning.WatchtowerCount = 2;
				Deal(Other, Map, Seed);
				bMoved = bMoved || (!Other.Watchtowers.empty() && Other.Watchtowers[0].Pos != One.Watchtowers[0].Pos);
			}
			if (!One.Watchtowers.empty() && !bMoved)
			{
				Fail(Map.Id + ": the towers stand in the same place whatever the seed");
			}
		}
		if (Failures == Before)
		{
			std::printf("%d battles over %d maps: towers in mirrored pairs, on open ground both sides reach, away from the starts, the edges and each other, the same for the same seed (%d had room for fewer than asked)\n",
				Battles, static_cast<int>(Maps.size()), Short);
		}
	}

	// Taking one: what it costs, and when it is refused. One turn by default
	// (2026-10-02); two here, so a capture part-way through is tested too.
	{
		const int Before = Failures;
		if (FTuning().WatchtowerTurns != 1.0 || FTuning().WatchtowerSight != 28.0)
		{
			Fail("a tower should take one turn to capture and see 28 m by default");
		}
		FBattle Battle;
		Battle.Tuning.WatchtowerCount = 2;
		Battle.Tuning.WatchtowerTurns = 2;
		Deal(Battle, *Big, 99);
		if (Battle.Watchtowers.size() != 2)
		{
			Fail("the big map should have room for two towers");
		}
		else
		{
			FUnit* Blue = NextReady(Battle, 0);
			const FWatchtower Tower = Battle.Watchtowers[0];
			if (Battle.ValidateCapture(Blue->Id, 0) != "Too far from the watchtower: stand next to it.")
			{
				Fail("capturing from across the map should be refused as too far");
			}
			if (Battle.ValidateCapture(Blue->Id, 7) != "No such watchtower.")
			{
				Fail("capturing a tower that is not there should be refused");
			}
			StandAt(Battle, *Blue, Tower);
			const FOrder Stale = FOrder::MakeCapture(Blue->Id, Blue->Serial + 1, 0);
			if (Battle.Validate(Stale).empty())
			{
				Fail("a capture written for another turn should be refused");
			}
			// An enemy standing at it: contested.
			FUnit* Red = nullptr;
			for (FUnit& Unit : Battle.Units)
			{
				Red = (!Red && Unit.Team == 1) ? &Unit : Red;
			}
			const FVec2 RedWas = Red->Pos;
			Red->Pos = FMap::Snap(FVec2(Tower.Pos.X + 1.0f, Tower.Pos.Y + 1.0f));
			if (Battle.ValidateCapture(Blue->Id, 0) != "An enemy is standing at the watchtower.")
			{
				Fail("capturing with an enemy at the tower should be refused as contested");
			}
			Red->Pos = RedWas;

			const int Serial = Blue->Serial;
			const FOrder Capture = FOrder::MakeCapture(Blue->Id, Serial, 0);
			const std::string Refused = Battle.Validate(Capture);
			if (!Refused.empty())
			{
				Fail("a unit standing at a free tower should be able to capture it: " + Refused);
			}
			FTickReport Report;
			Battle.Apply(Capture, Report);
			const int Needed = Battle.CaptureTurnsNeeded();
			if (Needed != 2 || Battle.Watchtowers[0].Capturer != 0 || Battle.Watchtowers[0].Progress != 1
				|| Battle.Watchtowers[0].Owner != -1)
			{
				Fail("one turn of two should leave blue part-way to the tower");
			}
			if (Blue->bReady || Report.WhoWas(EEventKind::Capturing).size() != 1)
			{
				Fail("capturing should end the unit's turn and say so");
			}
			// Not seen from the tower yet.
			int SeenBefore = 0;
			for (float Y = 0.25f; Y < Battle.Map.SizeMeters().Y; Y += 1.0f)
			{
				for (float X = 0.25f; X < Battle.Map.SizeMeters().X; X += 1.0f)
				{
					SeenBefore += Battle.CanSee(0, FVec2(X, Y)) ? 1 : 0;
				}
			}

			// Its next turn, still there: the tower is blue's.
			Blue = nullptr;
			for (int Tries = 0; Tries < 20 && !Blue; ++Tries)
			{
				FUnit* Next = NextReady(Battle, 0);
				if (Next && Next->Id == Capture.UnitId)
				{
					Blue = Next;
				}
				else if (Next)
				{
					FTickReport Skip;
					Battle.Apply(FOrder::MakeEndTurn(Next->Id, Next->Serial), Skip);
				}
			}
			FTickReport Second;
			Battle.Apply(FOrder::MakeCapture(Blue->Id, Blue->Serial, 0), Second);
			if (Battle.Watchtowers[0].Owner != 0 || Battle.Watchtowers[0].Progress != 0
				|| Second.WhoWas(EEventKind::Captured).size() != 1)
			{
				Fail("the second turn should take the tower for blue");
			}
			// Seeing from it: more of the board for blue, the same for red, and
			// every newly seen spot one the tower sees.
			int SeenAfter = 0;
			int Wrong = 0;
			for (float Y = 0.25f; Y < Battle.Map.SizeMeters().Y; Y += 1.0f)
			{
				for (float X = 0.25f; X < Battle.Map.SizeMeters().X; X += 1.0f)
				{
					const FVec2 Spot(X, Y);
					const bool bSeen = Battle.CanSee(0, Spot);
					SeenAfter += bSeen ? 1 : 0;
					if (bSeen && !Battle.TowerSees(Battle.Watchtowers[0], Spot))
					{
						bool bByUnit = false;
						for (const FUnit& Unit : Battle.Units)
						{
							bByUnit = bByUnit || (Unit.Team == 0 && Unit.IsAlive()
								&& Unit.Pos.DistanceTo(Spot) <= Battle.SightOf(Unit) && Battle.HasLineOfSight(Unit.Pos, Spot));
						}
						Wrong += bByUnit ? 0 : 1;
					}
				}
			}
			if (Wrong > 0 || SeenAfter <= SeenBefore)
			{
				Fail("a held tower should let its side see more, and only what the tower sees ("
					+ std::to_string(SeenBefore) + " -> " + std::to_string(SeenAfter) + ")");
			}
			const FVec2 Far(Tower.Pos.X + static_cast<float>(Battle.Tuning.WatchtowerSight) + 2.0f, Tower.Pos.Y);
			if (Battle.TowerSees(Battle.Watchtowers[0], Far))
			{
				Fail("a tower should not see past its sight");
			}
			// Nothing on the ground blocks its view: every spot within its sight
			// is seen, even one a unit standing there could not see.
			int Blocked = 0;
			for (float Y = 0.25f; Y < Battle.Map.SizeMeters().Y; Y += 1.0f)
			{
				for (float X = 0.25f; X < Battle.Map.SizeMeters().X; X += 1.0f)
				{
					const FVec2 Spot(X, Y);
					if (static_cast<double>(Tower.Pos.DistanceTo(Spot)) <= Battle.Tuning.WatchtowerSight
						&& (!Battle.TowerSees(Battle.Watchtowers[0], Spot) || !Battle.CanSee(0, Spot)))
					{
						++Blocked;
					}
				}
			}
			if (Blocked > 0)
			{
				Fail("a held tower should see every spot within its sight, blocked or not (" + std::to_string(Blocked) + " missed)");
			}
			if (Battle.ValidateCapture(Blue->Id, 0).empty())
			{
				Fail("a side should not capture a tower it already holds");
			}

			// Red takes it back, and wipes nothing of blue's since blue holds it.
			FUnit* Taker = NextReady(Battle, 1);
			StandAt(Battle, *Taker, Tower);
			for (FUnit& Unit : Battle.Units)
			{
				// Blue off the tower, so it is not contested.
				if (Unit.Team == 0 && Unit.Pos.DistanceTo(Tower.Pos) <= Watchtower::Reach)
				{
					Unit.Pos = Battle.SpawnPoints[0];
				}
			}
			FTickReport Third;
			Battle.Apply(FOrder::MakeCapture(Taker->Id, Taker->Serial, 0), Third);
			if (Battle.Watchtowers[0].Owner != 0 || Battle.Watchtowers[0].Capturer != 1 || Battle.Watchtowers[0].Progress != 1)
			{
				Fail("red's first turn should put red part-way, with blue still holding it");
			}
		}

		// The number of turns is the rule number, changed by order like any other.
		FBattle Tuned;
		Tuned.Tuning.WatchtowerCount = 2;
		Deal(Tuned, *Big, 5);
		int TurnsKey = -1;
		for (size_t i = 0; i < TuningKeys().size(); ++i)
		{
			TurnsKey = std::string(TuningKeys()[i].Key) == "watchtower_turns" ? static_cast<int>(i) : TurnsKey;
		}
		FTickReport Report;
		Tuned.Apply(FOrder::MakeTune({ { TurnsKey, 3.0 } }), Report);
		if (Tuned.CaptureTurnsNeeded() != 3)
		{
			Fail("the capture turns rule number should set how many turns it takes");
		}
		Tuned.Apply(FOrder::MakeTune({ { TurnsKey, 40.0 } }), Report);
		if (Tuned.CaptureTurnsNeeded() != 6)
		{
			Fail("the capture turns rule number should be held to its range (at most 6)");
		}
		Tuned.Apply(FOrder::MakeTune({ { TurnsKey, 3.0 } }), Report);
		int Turns = 0;
		const int Unit = NextReady(Tuned, 0)->Id;
		while (Tuned.Watchtowers[0].Owner != 0 && Turns < 10)
		{
			FUnit* Next = NextReady(Tuned, 0);
			if (Next->Id != Unit)
			{
				FTickReport Skip;
				Tuned.Apply(FOrder::MakeEndTurn(Next->Id, Next->Serial), Skip);
				continue;
			}
			StandAt(Tuned, *Next, Tuned.Watchtowers[0]);
			const FOrder Capture = FOrder::MakeCapture(Next->Id, Next->Serial, 0);
			if (!Tuned.Validate(Capture).empty())
			{
				Fail("capture refused while counting turns: " + Tuned.Validate(Capture));
				break;
			}
			FTickReport Step;
			Tuned.Apply(Capture, Step);
			++Turns;
		}
		if (Turns != 3)
		{
			Fail("with the rule at 3 it should take three turns, not " + std::to_string(Turns));
		}

		// Having acted, there is nothing left to capture with.
		FBattle Acted;
		Acted.Tuning.WatchtowerCount = 2;
		Deal(Acted, *Big, 5);
		FUnit* Busy = NextReady(Acted, 0);
		StandAt(Acted, *Busy, Acted.Watchtowers[0]);
		Busy->bActed = true;
		if (Acted.ValidateCapture(Busy->Id, 0) != "Already used its action this turn.")
		{
			Fail("a unit that has acted should not capture");
		}

		if (Failures == Before)
		{
			std::printf("taking a tower costs the turns the rule number says (2, then 3), each a whole turn; refused when too far, contested, already held, already acted or for another turn\n");
			std::printf("a held tower shows its side more of the board, only what the tower itself sees\n");
		}
	}

	// Orders as text.
	{
		const int Before = Failures;
		const FOrder Capture = FOrder::MakeCapture(3, 17, 1);
		FOrder Back;
		const std::string Error = OrderFromText(OrderToText(Capture), Back);
		if (!Error.empty() || Back.Type != EOrderType::Capture || Back.UnitId != 3 || Back.Serial != 17 || Back.Tower != 1)
		{
			Fail("a capture order should survive being written as text: " + OrderToText(Capture));
		}
		for (const char* Bad : { "capture", "capture 3 17", "capture 3 17 -1", "capture 3 17 1 9", "capture x 17 1" })
		{
			FOrder Ignored;
			if (OrderFromText(Bad, Ignored).empty())
			{
				Fail(std::string("a malformed capture should be refused: ") + Bad);
			}
		}
		if (Failures == Before)
		{
			std::printf("a capture order survives the trip between machines; malformed ones are refused\n");
		}
	}

	// The computer against itself with towers, on every map big enough: every
	// order legal, towers taken, and the battle replays from its orders.
	{
		const int Before = Failures;
		int Taken = 0;
		int Played = 0;
		for (const FMapDef& Map : Maps)
		{
			for (uint64_t Seed = 1; Seed <= 3; ++Seed)
			{
				FBattle Battle;
				Battle.Tuning.WatchtowerCount = 4;
				Deal(Battle, Map, Seed);
				FAIPlayer Computers[2] = { FAIPlayer("hard"), FAIPlayer("hard") };
				Computers[0].Rng.Seed(Seed);
				Computers[1].Rng.Seed(Seed + 1);
				std::vector<std::pair<int, FOrder>> Orders;
				int Refusals = 0;
				int Captures = 0;
				while (Battle.TickCount < 4000 && Battle.Winner < 0)
				{
					const FUnit* Unit = WaitingOn(Battle);
					if (!Unit)
					{
						FTickReport Report;
						Battle.Advance(1, Report);
						continue;
					}
					FOrder Order = Computers[Unit->Team].NextCommand(Battle, *Unit);
					if (!Battle.Validate(Order).empty())
					{
						++Refusals;
						Order = FOrder::MakeEndTurn(Unit->Id, Unit->Serial);
					}
					Orders.emplace_back(Battle.TickCount, Order);
					FTickReport Report;
					Battle.Apply(Order, Report);
					Captures += static_cast<int>(Report.WhoWas(EEventKind::Captured).size());
				}
				++Played;
				Taken += Captures;
				if (Refusals > 0)
				{
					Fail(Map.Id + ": the computer gave " + std::to_string(Refusals) + " orders the rules refused");
				}
				// Replayed from the orders alone.
				FBattle Replay;
				Replay.Tuning.WatchtowerCount = 4;
				Deal(Replay, Map, Seed);
				size_t Next = 0;
				while (Next < Orders.size() || (Replay.TickCount < Battle.TickCount && Replay.Winner < 0))
				{
					if (Next < Orders.size() && Orders[Next].first == Replay.TickCount)
					{
						FTickReport Report;
						if (!Replay.Validate(Orders[Next].second).empty())
						{
							Fail(Map.Id + ": the replay refused an order the battle took");
							break;
						}
						Replay.Apply(Orders[Next].second, Report);
						++Next;
						continue;
					}
					if (Replay.Winner >= 0)
					{
						break;
					}
					FTickReport Report;
					Replay.Advance(1, Report);
				}
				if (Replay.Checksum() != Battle.Checksum())
				{
					Fail(Map.Id + ": a battle with towers did not replay to the same checksum");
				}
			}
		}
		if (Taken == 0)
		{
			Fail("the computer never took a tower");
		}
		if (Failures == Before)
		{
			std::printf("the computer played %d battles with towers: every order legal, %d towers taken, each battle replayed to the same checksum\n",
				Played, Taken);
		}
	}

	std::printf("\n%s\n", Failures == 0 ? "WATCHTOWERS STAND, FALL AND SEE AS THE RULES SAY" : "WATCHTOWERS ARE WRONG");
	return Failures == 0 ? 0 : 1;
}
