// The computer player.
//
// It is a player, and that is the whole design. It looks at a battle it cannot
// change and returns an order, and that order is validated and applied exactly
// as one arriving from a person across the network would be. It has no way to
// reach into the rules, no privileged call, and nothing it can do that a player
// could not do by clicking.
//
// For a game played against other people that is the point: the computer is for
// practising against and for testing with, so it has to be held to the same
// rules as the people it stands in for. If the computer ever needed a special
// case inside the rules, the rules would be wrong.
//
// Ported from ai_player.gd. It thinks about where to stand; what to do with its
// turn arrives with abilities.

#pragma once

#include "SimOrder.h"
#include "SimRandom.h"
#include "SimTypes.h"

#include <string>

namespace TMSim
{
	class FBattle;
	struct FUnit;

	/**
	 * How well it plays. Easy takes a worse option now and then; hard never
	 * does, and is the one worth testing against.
	 */
	struct FSkill
	{
		/** Seconds from becoming ready to its first order. */
		double Think = 0.4;
		/** Seconds between orders. */
		double Step = 0.6;
		/** Chance of settling for a worse option than the best one. */
		double Mistakes = 0.0;
		/** How many of the best options it might settle for. */
		int Top = 1;
	};

	class TMSIM_API FAIPlayer
	{
	public:
		explicit FAIPlayer(const std::string& Difficulty = "hard");

		/** "easy", "medium" or "hard"; anything else is treated as hard. */
		void SetDifficulty(const std::string& Difficulty);
		const FSkill& Skill() const { return Level; }

		/**
		 * The next single order for this unit. The battle is passed as
		 * non-const only because asking where a unit can walk uses the
		 * pathfinder's scratch arrays; nothing about the battle is changed.
		 */
		FOrder NextCommand(FBattle& Battle, const FUnit& Unit);

		// Where it would rather stand. These are the whole of its movement and
		// are public so they can be measured against the Godot game directly,
		// which is worth more than hiding them: they ask questions about the
		// battle and change nothing, so a player could ask them too.

		/** Where it would rather stand, given what it can see. */
		FVec2 ApproachSpot(FBattle& Battle, const FUnit& Unit, bool bSprint);
		/** The reachable spot furthest from the enemies it can see. */
		FVec2 RetreatSpot(FBattle& Battle, const FUnit& Unit, bool bSprint);
		/** What standing on this ground is worth: embers hurt, springs help. */
		double GroundValue(const FBattle& Battle, const FUnit& Unit, const FVec2& Spot) const;

		/** Its own seeded generator, so a battle against it replays the same. */
		FSimRandom Rng;

	private:

		FSkill Level;
		/** Only the careful settings sprint or back off when badly hurt. */
		bool IsCareful() const { return Level.Mistakes < 0.4; }
	};
}
