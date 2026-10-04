// Plays a whole battle through the order path, then replays it.
//
// The other tests each check one piece against the Godot game. This one checks
// the thing the pieces are for: that a battle is exactly the list of orders
// that were applied to it. It is the claim the whole design rests on, and it is
// the one that cannot be checked by reading the code.
//
// Ultimately this is a game of people against other people. Both machines step
// the same rules over the same orders and compare notes, and a replay re-runs a
// recorded match. So "the same orders and the same seed give the same battle"
// is not a nicety -- it is the difference between a match and two machines
// quietly playing different games. This test plays one out with the computer on
// both sides, writes down every order and the tick it landed on, replays the
// list into a fresh battle, and compares the two after every single step.
//
// It also watches for the two ways the order path could be wrong. An order the
// rules accept and then do not carry out means the checking half and the doing
// half disagree. And a unit that is ready, is refused every order, and so stays
// ready forever would hang a real match on the spot.
//
// The orders all come from the computer player, which chooses abilities as well
// as where to stand, so this is the real thing playing itself rather than a
// stand-in policy written for the test.
//
// The units fight now, which is what makes the replay check mean anything:
// seeding the replay differently on purpose fails, because rolling to evade and
// to crit is the only thing in a battle that touches the dice. While nothing
// could deal damage that probe passed, and the check could not tell a right seed
// from a wrong one. If it ever starts passing again, something has stopped using
// the seeded generator and the replay guarantee is hollow.

#include "SimAI.h"
#include "SimBattle.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace TMSim;

namespace
{
	int Failures = 0;

	void Fail(const std::string& What)
	{
		if (Failures < 10)
		{
			std::printf("  %s\n", What.c_str());
		}
		++Failures;
	}

	/** Two sides of four, as the director builds them. */
	void Deal(FBattle& Battle)
	{
		const char* Roster[8] =
		{
			"knight", "archer", "black_mage", "white_mage",
			"knight", "archer", "black_mage", "white_mage"
		};
		const FVec2 Blue[4] =
		{
			FVec2(2.75f, 4.75f), FVec2(0.75f, 8.75f),
			FVec2(4.75f, 6.75f), FVec2(2.75f, 10.75f)
		};
		Battle.Map.BuildMirrored(HighlandsRows());
		const FVec2 Size = Battle.Map.SizeMeters();
		for (int Index = 0; Index < 8; ++Index)
		{
			FUnit Unit;
			Unit.Id = Index;
			Unit.Team = Index < 4 ? 0 : 1;
			Unit.Job = Roster[Index];
			const FVec2 Spot = Blue[Index % 4];
			Unit.Pos = Index < 4 ? Spot : FVec2(Size.X - Spot.X, Size.Y - Spot.Y);
			Battle.Units.push_back(Unit);
		}
		Battle.Start(12345);
	}

	/** The unit the battle is waiting on, as the director asks it. */
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

	/** One order that got through, and the tick it got through on. */
	struct FRecorded
	{
		int Tick = 0;
		FOrder Order;
	};

}

int main()
{
	const int TickLimit = 3000;  // five minutes of battle, which is plenty

	FBattle Battle;
	Deal(Battle);
	FAIPlayer Computer("hard");
	Computer.Rng.Seed(12345);

	std::vector<FRecorded> Recorded;
	/** The checksum after each tick, so a divergence is caught where it happens. */
	struct FMark { int Tick; uint64_t Sum; };
	std::vector<FMark> Marks;
	int Refusals = 0;
	int Stuck = 0;

	while (Battle.TickCount < TickLimit && Battle.Winner < 0)
	{
		const FUnit* Unit = WaitingOn(Battle);
		if (!Unit)
		{
			FTickReport Report;
			Battle.Advance(1, Report);
			Marks.push_back({ Battle.TickCount, Battle.Checksum() });
			continue;
		}

		const int UnitId = Unit->Id;
		const int Serial = Unit->Serial;
		FOrder Order = Computer.NextCommand(Battle, *Unit);

		// Checked, then applied, and never the other way round.
		const std::string Refused = Battle.Validate(Order);
		if (!Refused.empty())
		{
			++Refusals;
			// It cannot have this turn, so it gives the turn up. A unit that
			// could neither act nor stop being ready would hang the match.
			Order = FOrder::MakeEndTurn(UnitId, Serial);
			if (!Battle.Validate(Order).empty())
			{
				++Stuck;
				Fail("unit " + std::to_string(UnitId)
					+ " is ready and cannot even end its turn, so the battle is stuck");
				break;
			}
		}

		FTickReport Report;
		if (!Battle.Apply(Order, Report))
		{
			Fail("an order for unit " + std::to_string(UnitId)
				+ " passed the rules and then did nothing");
			break;
		}
		Recorded.push_back({ Battle.TickCount, Order });

		// Still ready and still not moved means nothing happened, and asking
		// again would ask forever.
		if (Order.Type == EOrderType::EndTurn)
		{
			const FUnit* After = Battle.FindUnit(UnitId);
			if (After && After->bReady)
			{
				Fail("unit " + std::to_string(UnitId) + " ended its turn and is still ready");
				break;
			}
		}
	}

	std::printf("%d orders over %d ticks\n", static_cast<int>(Recorded.size()), Battle.TickCount);
	std::printf("%d refused, %d stuck\n", Refusals, Stuck);

	// What the fight came to. Orders alone would not say whether any of them did
	// anything: a battle where every blow missed would look identical from here.
	int Standing[2] = { 0, 0 };
	int HealthLost = 0;
	for (const FUnit& Unit : Battle.Units)
	{
		if (Unit.IsAlive())
		{
			++Standing[Unit.Team];
		}
		HealthLost += Unit.MaxHp() - Unit.Hp;
	}
	std::printf("%d left standing to %d, %d health between them, winner %d\n",
		Standing[0], Standing[1], HealthLost, Battle.Winner);

	// Now the replay: the same seed, the same orders, nothing else. Nobody
	// decides anything this time -- the list is read off and applied.
	FBattle Replay;
	Deal(Replay);
	size_t Next = 0;
	size_t Marked = 0;
	int Mismatches = 0;
	while (Replay.TickCount < Battle.TickCount)
	{
		while (Next < Recorded.size() && Recorded[Next].Tick == Replay.TickCount)
		{
			const FOrder& Order = Recorded[Next].Order;
			const std::string Refused = Replay.Validate(Order);
			if (!Refused.empty())
			{
				Fail("the replay refused a recorded order for unit "
					+ std::to_string(Order.UnitId) + ": " + Refused);
				++Mismatches;
			}
			else
			{
				FTickReport Report;
				Replay.Apply(Order, Report);
			}
			++Next;
		}
		FTickReport Report;
		Replay.Advance(1, Report);

		// Compared here rather than only at the end. Comparing once tells you the
		// replay went wrong; comparing every tick tells you which tick, and that
		// is the difference between a bug you can find and one you cannot. It is
		// also what two machines in a match do, so this is the same check.
		if (Marked < Marks.size() && Marks[Marked].Tick == Replay.TickCount)
		{
			if (Marks[Marked].Sum != Replay.Checksum() && Mismatches == 0)
			{
				Fail("the replay parts company with the battle at tick "
					+ std::to_string(Replay.TickCount));
				++Mismatches;
			}
			++Marked;
		}
	}
	// Anything the first battle did on its very last tick.
	while (Next < Recorded.size() && Recorded[Next].Tick == Replay.TickCount)
	{
		FTickReport Report;
		const FOrder& Order = Recorded[Next].Order;
		if (Replay.Validate(Order).empty())
		{
			Replay.Apply(Order, Report);
		}
		++Next;
	}

	std::printf("replayed %d of %d orders, checked at %d of %d ticks\n",
		static_cast<int>(Next), static_cast<int>(Recorded.size()),
		static_cast<int>(Marked), static_cast<int>(Marks.size()));

	// Every tick has to have been compared. If the two ran out of step the marks
	// stop lining up and the comparison quietly stops happening, which would leave
	// the rest of the replay unchecked while still reporting success.
	if (Marked != Marks.size())
	{
		Fail("the replay only stayed in step for " + std::to_string(Marked) + " of "
			+ std::to_string(Marks.size()) + " ticks");
	}
	if (Battle.Checksum() != Replay.Checksum())
	{
		Fail("the replay ended in a different state from the battle it replayed");
		for (size_t i = 0; i < Battle.Units.size() && i < Replay.Units.size(); ++i)
		{
			const FUnit& A = Battle.Units[i];
			const FUnit& B = Replay.Units[i];
			if (A.Pos != B.Pos || A.Hp != B.Hp || A.Tg != B.Tg || A.Serial != B.Serial)
			{
				char Line[220];
				std::snprintf(Line, sizeof(Line),
					"    unit %d: %.2f,%.2f hp %d tg %d turn %d -- replay has %.2f,%.2f hp %d tg %d turn %d",
					A.Id, A.Pos.X, A.Pos.Y, A.Hp, A.Tg, A.Serial,
					B.Pos.X, B.Pos.Y, B.Hp, B.Tg, B.Serial);
				std::printf("%s\n", Line);
			}
		}
	}

	// And the checksum itself, field by field. A checksum that quietly ignores
	// something is worse than none: two machines would drift apart in that field
	// and keep playing, each sure the other agreed. So change one thing at a
	// time and insist the number moves. This is checked here rather than on a
	// fresh battle because a played-out one has statuses, cooldowns and spent
	// gauges on it, which an untouched one does not.
	{
		int Blind = 0;
		// Setup, when given, runs before the checksum is taken, so the change can
		// be to something that has to exist first.
		auto Notices = [&Blind, &Battle](const char* Field, void (*Change)(FBattle&), void (*Setup)(FBattle&) = nullptr)
		{
			FBattle Copy = Battle;
			if (Setup)
			{
				Setup(Copy);
			}
			const uint64_t Before = Copy.Checksum();
			Change(Copy);
			if (Copy.Checksum() == Before)
			{
				Fail(std::string("the checksum does not notice ") + Field);
				++Blind;
			}
		};

		Notices("the clock", [](FBattle& B) { B.TickCount += 1; });
		Notices("who won", [](FBattle& B) { B.Winner = 1 - B.Winner; });
		Notices("the planning time", [](FBattle& B) { B.PlanningTicks += 1; });
		Notices("where a unit stands, across", [](FBattle& B) { B.Units[3].Pos.X += 0.5f; });
		Notices("where a unit stands, along", [](FBattle& B) { B.Units[3].Pos.Y += 0.5f; });
		Notices("which way a unit faces", [](FBattle& B) { B.Units[2].Facing.X += 1.0f; });
		Notices("a unit's health", [](FBattle& B) { B.Units[5].Hp -= 1; });
		Notices("a unit's gauge", [](FBattle& B) { B.Units[1].Tg += 1; });
		Notices("which turn a unit is on", [](FBattle& B) { B.Units[0].Serial += 1; });
		Notices("a unit's ultimate meter", [](FBattle& B) { B.Units[4].Ult += 1; });
		Notices("whether a unit is ready", [](FBattle& B) { B.Units[6].bReady = !B.Units[6].bReady; });
		Notices("whether a unit has walked", [](FBattle& B) { B.Units[6].bMoved = !B.Units[6].bMoved; });
		Notices("whether a unit has acted", [](FBattle& B) { B.Units[6].bActed = !B.Units[6].bActed; });
		Notices("a unit's turn countdown", [](FBattle& B) { B.Units[7].Clock += 1; });
		Notices("a recharging ability", [](FBattle& B) { B.Units[2].Cooldowns[1] += 1; });
		Notices("a switched-on toggle", [](FBattle& B) { B.Units[3].Toggled[2] = !B.Units[3].Toggled[2]; });
		Notices("a toggle already flipped this turn", [](FBattle& B)
			{ B.Units[3].ToggledTurn[2] = !B.Units[3].ToggledTurn[2]; });
		Notices("a spell part-way out", [](FBattle& B) { B.Units[2].Casting.Slot = 1; });
		Notices("how much longer a cast has", [](FBattle& B)
			{
				B.Units[2].Casting.Slot = 1;
				B.Units[2].Casting.Ticks = 7;
			});
		Notices("where a cast is aimed", [](FBattle& B)
			{
				B.Units[2].Casting.Slot = 1;
				B.Units[2].Casting.Target.X += 1.0f;
			});
		Notices("who a cast is following", [](FBattle& B)
			{
				B.Units[2].Casting.Slot = 1;
				B.Units[2].Casting.FollowId = 5;
			});
		Notices("a channelled ability", [](FBattle& B) { B.Units[6].Channeling.Slot = 3; });
		Notices("where the dice have got to", [](FBattle& B) { B.Rng.Randi(); });
		Notices("how long a fallen unit can still be raised", [](FBattle& B) { B.Units[4].KoTicks += 1; });
		Notices("a gauge owed for holding back", [](FBattle& B) { B.Units[1].bHustling = !B.Units[1].bHustling; });
		Notices("how long a unit has gone unhurt", [](FBattle& B) { B.Units[3].UnharmedTurns += 1; });
		Notices("how many turns a channel has left", [](FBattle& B)
			{
				B.Units[6].Channeling.Slot = 3;
				B.Units[6].Channeling.Turns = 2;
			});
		Notices("a status on a unit", [](FBattle& B)
			{
				FStatus Status;
				Status.Id = "poison";
				Status.Turns = 2;
				B.Units[0].Statuses.push_back(Status);
			});
		Notices("how long a status has left", [](FBattle& B)
			{
				FStatus Status;
				Status.Id = "poison";
				Status.Turns = 2;
				B.Units[0].Statuses.push_back(Status);
				B.Units[0].Statuses.back().Turns = 3;
			});
		Notices("a buff on a unit", [](FBattle& B)
			{
				FBuff Buff;
				Buff.Stat = EStat::AttDef;
				Buff.Amount = 3;
				Buff.Turns = 2;
				B.Units[1].Buffs.push_back(Buff);
			});
		Notices("which aura keeps a buff topped up",
			[](FBattle& B) { B.Units[1].Buffs.back().Aura = "Warcry"; },
			[](FBattle& B)
			{
				FBuff Buff;
				Buff.Stat = EStat::AttDef;
				Buff.Amount = 3;
				Buff.Turns = 2;
				B.Units[1].Buffs.push_back(Buff);
			});
		Notices("how long blue has held the middle", [](FBattle& B) { B.CaptureTicks[0] += 1; });
		Notices("how long red has held the middle", [](FBattle& B) { B.CaptureTicks[1] += 1; });
		Notices("whether blue is done placing", [](FBattle& B) { B.PlanningDone[0] = !B.PlanningDone[0]; });
		Notices("whether red is done placing", [](FBattle& B) { B.PlanningDone[1] = !B.PlanningDone[1]; });
		Notices("a rule number", [](FBattle& B) { B.Tuning.SpeedMultiplier += 0.05; });
		// The watchtowers (not Godot's): that there is one, where, who holds it,
		// and who is how far into taking it.
		auto OneTower = [](FBattle& B) { B.Watchtowers.push_back(FWatchtower()); };
		Notices("a watchtower", OneTower);
		Notices("where a watchtower stands", [](FBattle& B) { B.Watchtowers[0].Pos.X += 2.0f; }, OneTower);
		Notices("who holds a watchtower", [](FBattle& B) { B.Watchtowers[0].Owner = 1; }, OneTower);
		Notices("who is taking a watchtower", [](FBattle& B) { B.Watchtowers[0].Capturer = 0; }, OneTower);
		Notices("how far into taking it", [](FBattle& B) { B.Watchtowers[0].Progress = 1; }, OneTower);
		std::printf("the checksum notices %d kinds of change\n", 42 - Blind);
	}

	// How a battle is won besides by wiping out the other side: the time limit
	// and holding the middle. The same five checks as the Godot project's own
	// smoke_test.gd (_test_victory_conditions and the capture checks), on the
	// same board; GodotBattleTrace.txt then measures them in whole battles.
	{
		const int Before = Failures;
		auto Run = [](FBattle& B, int Ticks)
		{
			for (int i = 0; i < Ticks && B.Winner == -1; ++i)
			{
				FTickReport Report;
				B.Apply(FOrder::MakeAdvance(1), Report);
			}
		};

		FBattle Timed;
		Timed.Tuning.BattleSeconds = 5.0;
		Deal(Timed);
		Timed.Units[4].Hp = 10;  // red is hurt, so blue should win on health
		Run(Timed, 200);
		if (Timed.Winner != 0 || Timed.TickCount != 50)
		{
			Fail("the time limit should end the battle at tick 50 for the healthier side (winner "
				+ std::to_string(Timed.Winner) + " at " + std::to_string(Timed.TickCount) + ")");
		}

		FBattle Level;
		Level.Tuning.BattleSeconds = 3.0;
		Deal(Level);
		Run(Level, 200);
		if (Level.Winner != FBattle::Draw)
		{
			Fail("an even battle at the time limit should be a draw");
		}

		FBattle Endless;
		Deal(Endless);
		Run(Endless, 50);
		if (Endless.Winner != -1)
		{
			Fail("without a limit a battle should keep going");
		}

		FBattle Hold;
		Hold.Tuning.CaptureSeconds = 3.0;
		Deal(Hold);
		for (FUnit& Unit : Hold.Units)
		{
			Unit.Pos = Unit.Team == 0 ? Hold.CapturePoint() : Hold.CapturePoint() + FVec2(30.0f, 30.0f);
		}
		Run(Hold, 40);
		if (Hold.Winner != 0)
		{
			Fail("holding the middle alone should win the battle");
		}

		FBattle Contested;
		Contested.Tuning.CaptureSeconds = 3.0;
		Deal(Contested);
		for (FUnit& Unit : Contested.Units)
		{
			Unit.Pos = Contested.CapturePoint() + FVec2(0.5f * Unit.Team, 0.0f);
		}
		Run(Contested, 40);
		if (Contested.CaptureTicks[0] != 0 || Contested.CaptureTicks[1] != 0)
		{
			Fail("a contested middle should count for neither side");
		}
		if (Failures == Before)
		{
			std::printf("a battle is called on time for the healthier side, and won by holding the middle\n");
		}
	}

	// Developer Tools change the rule numbers by order: known keys only, and a
	// value out of range is held to the range rather than refused, as Godot's
	// clean_tuning does (game_state.gd:255-260).
	{
		const int Before = Failures;
		FBattle Tuned;
		Deal(Tuned);
		int Speed = -1;
		int KoSeconds = -1;
		for (size_t i = 0; i < TuningKeys().size(); ++i)
		{
			if (std::string(TuningKeys()[i].Key) == "speed_multiplier") { Speed = static_cast<int>(i); }
			if (std::string(TuningKeys()[i].Key) == "ko_seconds") { KoSeconds = static_cast<int>(i); }
		}
		// Godot's 28, in Godot's order, then the watchtowers' three, the item
		// budget, the camps' two, element reactions and friendly fire after them
		// (not Godot's), so every older rule keeps its index; the boss's hunt and
		// claim last (2026-10-02).
		if (TuningKeys().size() != 44 || Speed < 0 || KoSeconds < 0
			|| std::string(TuningKeys()[27].Key) != "battle_seconds"
			|| std::string(TuningKeys()[28].Key) != "watchtower_count"
			|| std::string(TuningKeys()[32].Key) != "camps"
			|| std::string(TuningKeys()[35].Key) != "friendly_fire"
			|| std::string(TuningKeys()[36].Key) != "camp_respawn"
			|| std::string(TuningKeys()[37].Key) != "defense_model"
			|| std::string(TuningKeys()[39].Key) != "zone_of_control"
			|| std::string(TuningKeys()[38].Key) != "defense_scale"
			|| std::string(TuningKeys()[40].Key) != "boss_hunt"
			|| std::string(TuningKeys()[41].Key) != "boss_claim"
			|| std::string(TuningKeys()[42].Key) != "spring_percent"
			|| std::string(TuningKeys()[43].Key) != "spring_rest_turns")
		{
			Fail("the tuning table should have Godot's 28 rule numbers, then the watchtowers' three, the item budget, the camps' two, elements and friendly fire");
		}
		const FOrder Tune = FOrder::MakeTune({ { Speed, 1.5 }, { KoSeconds, 999.0 } });
		if (!Tuned.Validate(Tune).empty())
		{
			Fail("a tuning order with known keys should be accepted");
		}
		FTickReport Report;
		Tuned.Apply(Tune, Report);
		if (Tuned.Tuning.SpeedMultiplier != 1.5 || Tuned.Tuning.KoSeconds != 60.0)
		{
			Fail("tuning should set the number, held to its range (ko_seconds tops out at 60)");
		}
		if (Tuned.Validate(FOrder::MakeTune({ { 99, 1.0 } })).empty())
		{
			Fail("a tuning order with an unknown key should be refused");
		}
		if (Failures == Before)
		{
			std::printf("rule numbers change by order, held to their range, and unknown ones are refused\n");
		}
	}

	// Friendly fire (2026-10-01): off, an area blow passes over its own side;
	// on, it catches them too -- never the caster, never a single-target blow.
	{
		const int Before = Failures;
		FBattle Fire;
		Deal(Fire);
		FUnit* Caster = nullptr;
		int AreaSlot = -1;
		int SingleSlot = -1;
		for (FUnit& Unit : Fire.Units)
		{
			for (int Slot = 0; Slot < 4 && Unit.Team == 0; ++Slot)
			{
				const FAbility* Ability = Unit.Ability(Slot);
				if (!Ability || Ability->Effect != EEffect::Damage || Ability->Target != ETargetSide::Enemy || Ability->MaxRange < 3.0f)
				{
					continue;
				}
				if (Ability->Aoe >= 1.0f && ShapeOf(*Ability) == "circle" && AreaSlot < 0)
				{
					Caster = &Unit;
					AreaSlot = Slot;
				}
			}
		}
		if (!Caster)
		{
			Fail("friendly fire: no blue unit has a circle blow to test with");
		}
		else
		{
			for (int Slot = 0; Slot < 4; ++Slot)
			{
				const FAbility* Ability = Caster->Ability(Slot);
				if (Ability && Ability->Effect == EEffect::Damage && Ability->Aoe == 0.0f && ShapeOf(*Ability) == "unit" && SingleSlot < 0)
				{
					SingleSlot = Slot;
				}
			}
			FUnit* Ally = nullptr;
			FUnit* Enemy = nullptr;
			for (FUnit& Unit : Fire.Units)
			{
				Ally = !Ally && Unit.Team == 0 && &Unit != Caster ? &Unit : Ally;
				Enemy = !Enemy && Unit.Team == 1 ? &Unit : Enemy;
			}
			const FVec2 Aim(Caster->Pos.X + 2.5f, Caster->Pos.Y);
			Ally->Pos = FVec2(Aim.X + 0.3f, Aim.Y);
			Enemy->Pos = FVec2(Aim.X - 0.3f, Aim.Y);
			auto Hits = [&](int Slot, const FUnit& Who)
			{
				for (const FHit& Hit : Fire.Preview(*Caster, Slot, Caster->Pos, Aim))
				{
					if (Hit.UnitId == Who.Id)
					{
						return true;
					}
				}
				return false;
			};
			if (!Hits(AreaSlot, *Enemy) || Hits(AreaSlot, *Ally))
			{
				Fail("friendly fire off: an area blow should catch the enemy and not the ally");
			}
			Fire.Tuning.FriendlyFire = 1.0;
			if (!Hits(AreaSlot, *Enemy) || !Hits(AreaSlot, *Ally) || Hits(AreaSlot, *Caster))
			{
				Fail("friendly fire on: an area blow should catch the enemy and the ally, never the caster");
			}
			if (SingleSlot >= 0 && Hits(SingleSlot, *Ally))
			{
				Fail("friendly fire on: a single-target blow should still never hit an ally");
			}
		}
		if (Failures == Before)
		{
			std::printf("friendly fire: off, area blows pass over their own side; on, they catch it too, never the caster or a single-target blow\n");
		}
	}

	// Defense model 1 (2026-10-01, Docs/design/feat-defense.md): Armor and
	// Resist take a share off a hit, there is one Evasion, and an evaded hit
	// is dodged one time in ten and grazed for half the other nine.
	{
		const int Before = Failures;
		FBattle Old;
		Deal(Old);
		FBattle New;
		New.Tuning.DefenseModel = 1.0;
		Deal(New);
		int Sums = 0;
		for (size_t u = 0; u < New.Units.size(); ++u)
		{
			const FUnit& User = New.Units[u];
			for (int Slot = 0; Slot < 4 && User.Team == 0; ++Slot)
			{
				const FAbility* Ability = User.Ability(Slot);
				if (!Ability || Ability->Effect != EEffect::Damage)
				{
					continue;
				}
				for (size_t t = 0; t < New.Units.size(); ++t)
				{
					const FUnit& Target = New.Units[t];
					if (Target.Team != 1)
					{
						continue;
					}
					const int Raw = RoundToInt(Ability->Power * New.FlankBonus(Target, Target.Pos, User.Pos));
					const int Def = Target.Stat(Ability->Scale == EScale::Att ? EStat::AttDef : EStat::MagDef);
					const int WantNew = std::max(1, RoundToInt(Raw * 0.5 * 30.0 / (30.0 + Def)));
					const int WantOld = std::max(1, RoundToInt((Raw - Def) * 0.5));
					const int GotNew = New.CalcAmount(User, *Ability, User.Pos, Target, Target.Pos, 0, 0);
					const int GotOld = Old.CalcAmount(Old.Units[u], *Ability, User.Pos, Old.Units[t], Target.Pos, 0, 0);
					++Sums;
					if (GotNew != WantNew || GotOld != WantOld)
					{
						Fail("defense: " + User.Job + "'s " + Ability->Name + " on " + Target.Job + " does " + std::to_string(GotNew)
							+ " (want " + std::to_string(WantNew) + ") and classic " + std::to_string(GotOld) + " (want " + std::to_string(WantOld) + ")");
					}
				}
			}
		}
		if (Sums == 0)
		{
			Fail("defense: no damaging ability to check");
		}
		// No pile of defense makes a unit immune; under the classic rules it does.
		{
			FBattle Walled = New;
			FBattle WalledOld = Old;
			FUnit& Wall = Walled.Units[4];
			FUnit& WallOld = WalledOld.Units[4];
			Wall.Buffs.push_back({ EStat::AttDef, 60, 3 });
			WallOld.Buffs.push_back({ EStat::AttDef, 60, 3 });
			const FUnit& User = Walled.Units[0];
			for (int Slot = 0; Slot < 4; ++Slot)
			{
				const FAbility* Ability = User.Ability(Slot);
				if (Ability && Ability->Effect == EEffect::Damage && Ability->Scale == EScale::Att && Ability->Power >= 20.0f)
				{
					const int Now = Walled.CalcAmount(User, *Ability, User.Pos, Wall, Wall.Pos, 0, 0);
					const int Then = WalledOld.CalcAmount(WalledOld.Units[0], *Ability, User.Pos, WallOld, Wall.Pos, 0, 0);
					if (Now <= 2 || Then != 1)
					{
						Fail("defense: AttDef +60 should leave a real hit (" + std::to_string(Now) + ") where the classic rules leave 1 (" + std::to_string(Then) + ")");
					}
					break;
				}
			}
		}
		// One Evasion: the higher of the two, plus anything added to either.
		{
			FBattle Quick = New;
			FUnit& Archer = Quick.Units[1];
			const int AEva = Archer.Stats ? Archer.Stats->Get(EStat::AEva) : 0;
			const int MEva = Archer.Stats ? Archer.Stats->Get(EStat::MEva) : 0;
			const int Plain = Quick.EvasionOf(Archer);
			Archer.Buffs.push_back({ EStat::MEva, 5, 2 });
			const int WithM = Quick.EvasionOf(Archer);
			Archer.Buffs.push_back({ EStat::AEva, 3, 2 });
			const int WithBoth = Quick.EvasionOf(Archer);
			if (Plain != std::max(AEva, MEva) || WithM != Plain + 5 || WithBoth != Plain + 8)
			{
				Fail("defense: Evasion should be the higher of A-Eva and M-Eva plus what is added to either");
			}
			const FUnit& Foe = Quick.Units[4];
			FAbility Physical;
			Physical.Effect = EEffect::Damage;
			Physical.Scale = EScale::Att;
			FAbility Magic = Physical;
			Magic.Scale = EScale::Mag;
			if (Quick.EvadeChance(Archer, Physical, &Foe) != Quick.EvadeChance(Archer, Magic, &Foe)
				|| Quick.EvadeChance(Archer, Physical, &Foe) != WithBoth)
			{
				Fail("defense: one Evasion should stand against physical and magic blows alike");
			}
		}
		// The dice, over whole battles: dodges a tenth of the evasions, grazes the
		// rest; each graze lands as a hit; the same orders give the same battle.
		int Dodged = 0;
		int Grazed = 0;
		int GrazesLanding = 0;
		for (int Seed = 1; Seed <= 3; ++Seed)
		{
			FBattle Fight;
			Fight.Tuning.DefenseModel = 1.0;
			Deal(Fight);
			FAIPlayer Hard("hard");
			Hard.Rng.Seed(static_cast<uint64_t>(Seed));
			std::vector<FOrder> Given;
			while (Fight.TickCount < TickLimit && Fight.Winner < 0)
			{
				const FUnit* Waiting = WaitingOn(Fight);
				FOrder Order = Waiting ? Hard.NextCommand(Fight, *Waiting) : FOrder::MakeAdvance(1);
				if (!Fight.Validate(Order).empty())
				{
					Fail("defense: the computer gave an order the rules refused");
					Order = FOrder::MakeEndTurn(Waiting->Id, Waiting->Serial);
				}
				FTickReport Report;
				Fight.Apply(Order, Report);
				Given.push_back(Order);
				for (size_t e = 0; e < Report.Events.size(); ++e)
				{
					const FEvent& Event = Report.Events[e];
					Dodged += Event.Kind == EEventKind::Evaded ? 1 : 0;
					if (Event.Kind == EEventKind::Grazed)
					{
						++Grazed;
						for (size_t n = e + 1; n < Report.Events.size(); ++n)
						{
							if (Report.Events[n].Kind == EEventKind::Hit && Report.Events[n].Unit == Event.Unit)
							{
								++GrazesLanding;
								break;
							}
						}
					}
				}
			}
			FBattle Again;
			Again.Tuning.DefenseModel = 1.0;
			Deal(Again);
			for (const FOrder& Order : Given)
			{
				FTickReport Report;
				Again.Apply(Order, Report);
			}
			if (Again.Checksum() != Fight.Checksum())
			{
				Fail("defense: a battle under the new defense does not replay");
			}
		}
		if (Grazed == 0 || Dodged * 3 > Grazed || GrazesLanding != Grazed)
		{
			Fail("defense: evasions should be grazes about nine times in ten, each landing (" + std::to_string(Dodged) + " dodged, "
				+ std::to_string(Grazed) + " grazed, " + std::to_string(GrazesLanding) + " landed)");
		}
		if (Failures == Before)
		{
			std::printf("defense rules 1: Armor and Resist take a share (%d hits checked), nothing stacks to immune, one Evasion for both kinds; "
				"%d evasions dodged and %d grazed over three battles, each replaying the same\n", Sums, Dodged, Grazed);
		}
	}

	// Zone of control (2026-10-01): an enemy walking into a tank's reach stops
	// there; off, as in Godot, nothing changes.
	{
		const int Before = Failures;
		auto Check = [](bool bOk, const char* What) { if (!bOk) { Fail(std::string("zone of control: ") + What); } };
		int Walks = 0;
		int Stopped = 0;
		for (int Pass = 0; Pass < 2; ++Pass)
		{
			FBattle Zone;
			Zone.Tuning.ZoneOfControl = Pass;
			Deal(Zone);
			// Blue's archer, red's knight (a tank first), in the open middle of the board.
			FUnit& Walker = Zone.Units[1];
			FUnit& Tank = Zone.Units[4];
			const FVec2 Size = Zone.Map.SizeMeters();
			Tank.Pos = FMap::Snap(FVec2(Size.X * 0.5f, Size.Y * 0.5f));
			Walker.Pos = FMap::Snap(Tank.Pos + FVec2(-4.0f, 0.0f));
			const double Reach = Zone.Tuning.EngageRadius;
			for (const auto& Entry : Zone.ReachableNodes(Walker))
			{
				const std::vector<FVec2> Way = Zone.PathTo(Walker, Entry.first);
				if (Way.size() < 2)
				{
					continue;
				}
				++Walks;
				// Every step but the last must stay out of the tank's reach.
				bool bThrough = false;
				for (size_t i = 1; i + 1 < Way.size(); ++i)
				{
					bThrough = bThrough || static_cast<double>(Way[i].DistanceTo(Tank.Pos)) < Reach;
				}
				if (Pass == 1)
				{
					Check(!bThrough, "no walk passes through a tank's reach");
				}
				else if (bThrough)
				{
					++Stopped;
				}
			}
			if (Pass == 1)
			{
				// A waypoint inside the reach is refused: the walk would go on past its stop.
				std::vector<FVec2> Via = { FMap::Snap(Tank.Pos + FVec2(-1.2f, 0.0f)) };
				double Left = 0.0;
				Check(!Zone.WalkVia(Walker, Via, false, Left), "a waypoint in a tank's reach is refused");
				// A unit that starts beside a tank can still walk away.
				Walker.Pos = FMap::Snap(Tank.Pos + FVec2(-1.0f, 0.0f));
				Check(Zone.ReachableNodes(Walker).size() > 20, "a unit already beside it can walk away");
			}
		}
		Check(Stopped > 0, "off, some walks do pass the tank (or the test proves nothing)");
		if (Failures == Before)
		{
			std::printf("zone of control: %d walks checked; off, %d passed through a tank's reach; on, none did\n", Walks, Stopped);
		}
	}

	// The planning stage: the same checks as Godot's smoke_test.gd
	// (_test_planning_stage), on the same board.
	{
		const int Before = Failures;
		auto Check = [](bool bOk, const char* What) { if (!bOk) { Fail(std::string("planning: ") + What); } };
		auto Step = [](FBattle& B, const FOrder& Order) { FTickReport Report; B.Apply(Order, Report); };

		FBattle Plan;
		Plan.Tuning.PlanningSeconds = 10.0;
		Deal(Plan);
		Check(Plan.IsPlanning(), "a battle with planning time starts in the planning stage");
		FUnit& Placed = Plan.Units[0];
		const int GaugeBefore = Placed.Tg;
		Step(Plan, FOrder::MakeAdvance(20));
		Check(Plan.TickCount == 0 && Placed.Tg == GaugeBefore, "no gauge fills and no time passes while planning");

		const FVec2 Spot = FMap::Snap(Plan.SpawnPoints[0] + FVec2(1.0f, 1.0f));
		Check(Plan.Validate(FOrder::MakePlace(Placed.Id, Placed.Serial, Spot)).empty(), "a unit can be placed in its own spawn area");
		Step(Plan, FOrder::MakePlace(Placed.Id, Placed.Serial, Spot));
		Check(Placed.Pos == Spot, "placing moves it there");
		const FVec2 Far = FMap::Snap(Plan.SpawnPoints[1]);
		Check(!Plan.Validate(FOrder::MakePlace(Placed.Id, Placed.Serial, Far)).empty(), "it can't be placed in the other side's area");
		const FUnit& Enemy = Plan.Units[4];
		Check(!Plan.Validate(FOrder::MakePlace(Enemy.Id, Enemy.Serial, Spot)).empty(), "and not on top of somebody else");
		Check(!Plan.PlaceableNodes(Placed).empty(), "there are spots to place it on");
		Check(!Plan.Validate(FOrder::MakeMove(Placed.Id, Placed.Serial, Spot)).empty(), "nobody walks while planning");

		Step(Plan, FOrder::MakeReady(0));
		Check(Plan.IsPlanning(), "one side being ready isn't enough");
		Step(Plan, FOrder::MakeReady(1));
		Check(!Plan.IsPlanning(), "both sides ready starts the battle");
		Step(Plan, FOrder::MakeAdvance(5));
		Check(Plan.TickCount == 5, "time runs once the planning is over");
		Check(!Plan.Validate(FOrder::MakePlace(Placed.Id, Placed.Serial, Spot)).empty(), "and units can't be placed any more");

		FBattle Waited;
		Waited.Tuning.PlanningSeconds = 1.0;
		Deal(Waited);
		Step(Waited, FOrder::MakeAdvance(10));
		Check(!Waited.IsPlanning(), "the planning ends when its time runs out");
		if (Failures == Before)
		{
			std::printf("units are placed in their own spawn area before the fighting, which starts when both sides are ready\n");
		}
	}

	// A battle where nothing ever happened would pass everything above, and so
	// would one where every blow missed. It has to have been a fight: somebody
	// lost health, and it either reached a decision or ran the clock out trying.
	if (Recorded.size() < 50)
	{
		std::printf("THE BATTLE BARELY HAPPENED -- only %d orders\n", static_cast<int>(Recorded.size()));
		return 1;
	}
	if (HealthLost < 100)
	{
		std::printf("NOBODY REALLY FOUGHT -- %d health lost between eight units\n", HealthLost);
		return 1;
	}
	if (Battle.Winner < 0 && Battle.TickCount < TickLimit)
	{
		std::printf("THE BATTLE STOPPED EARLY WITH NOBODY WINNING -- %d ticks\n", Battle.TickCount);
		return 1;
	}
	// And one where the rules refused most of what was asked for.
	if (Refusals * 2 > static_cast<int>(Recorded.size()))
	{
		std::printf("THE RULES REFUSED MOST OF WHAT WAS ASKED FOR (%d of %d)\n",
			Refusals, static_cast<int>(Recorded.size()));
		return 1;
	}
	// The game's balance changes to the built-ins (ApplyGameBalance), last, since
	// they change the built-in classes for the rest of the run.
	{
		const int Before = Failures;
		auto Check = [](bool bOk, const char* What) { if (!bOk) { Fail(std::string("game balance: ") + What); } };
		const FJobDef* Knight = FindJob("knight");
		const FJobDef* Archer = FindJob("archer");
		Check(Knight && Knight->Stats.Get(EStat::Speed) == 6 && Archer && Archer->Stats.Get(EStat::Speed) == 12,
			"until it is applied, the built-ins are Godot's");
		ApplyGameBalance();
		ApplyGameBalance();  // twice is once
		Check(Knight->Stats.Get(EStat::Speed) == 8 && Knight->Stats.Get(EStat::Hp) == 115 && Knight->Stats.Get(EStat::MagDef) == 9,
			"the Knight: Speed 8, HP 115, MagDef 9");
		Check(Archer->Stats.Get(EStat::Speed) == 10 && Archer->Stats.Get(EStat::Sight) == 11 && Archer->Stats.Get(EStat::Crit) == 10,
			"the Archer: Speed 10, Sight 11, Crit 10");
		const FAbility* Bash = FindAbility("shield_bash");
		const FAbility* Guard = FindAbility("guard");
		Check(Bash && Bash->StatusId == "taunt" && Bash->StatusTurns == 2, "Shield Bash taunts for 2 turns");
		Check(Guard && Guard->StatusId == "guarded" && Guard->MaxRange == 4.0f && Guard->Buffs.empty(), "Guard guards an ally up to 4 m away");
		Check(FindAbility("bow_shot")->MaxRange == 8.0f && FindAbility("aimed_shot")->Cooldown == 3, "Bow Shot reaches 8 m, Aimed Shot every 3 turns");

		// A Guard cast in battle: the ally carries Guarded, by the Knight.
		FBattle Guarding;
		Guarding.Tuning = GameTuning();
		Deal(Guarding);
		FUnit& Guardian = Guarding.Units[0];
		FUnit& Ward = Guarding.Units[1];
		Ward.Pos = FMap::Snap(Guardian.Pos + FVec2(2.0f, 0.0f));
		Guardian.bReady = true;
		Guardian.Clock = 100;
		const FOrder Cast = FOrder::MakeUseAbility(Guardian.Id, Guardian.Serial, 2, Ward.Pos, Ward.Id);
		const std::string Why = Guarding.Validate(Cast);
		Check(Why.empty(), "a Knight can Guard an ally");
		FTickReport Report;
		Guarding.Apply(Cast, Report);
		bool bGuarded = false;
		for (const FStatus& Status : Ward.Statuses)
		{
			bGuarded = bGuarded || (Status.Id == "guarded" && Status.By == Guardian.Id);
		}
		Check(bGuarded, "the ally is Guarded by the Knight");
		if (Failures == Before)
		{
			std::printf("game balance: the Knight and the Archer change only when the game asks; a Knight's Guard lands on an ally\n");
		}
	}

	if (Failures > 0)
	{
		std::printf("THE BATTLE DOES NOT REPLAY (%d)\n", Failures);
		return 1;
	}
	std::printf("THE SAME ORDERS GIVE THE SAME BATTLE\n");
	return 0;
}
