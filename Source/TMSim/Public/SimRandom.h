// Godot's random number generator, reproduced exactly.
//
// The battle is a deterministic simulation: both sides of an online match step
// the same rules and compare checksums, and a replay re-runs a recorded battle.
// That only holds if every roll comes out the same, so this is not "a PCG32" --
// it is the one Godot 4 uses, down to the bit, so a battle can be replayed in
// either engine and land on the same result.
//
// The shape was recovered by dumping Godot's own output and searching the
// plausible variants; the combination below is the one that reproduces it:
//
//   output function   XSH-RR
//   increment         (1442695040888963407 << 1) | 1
//   seeding           the canonical pcg32_srandom_r
//
// Verified against Godot 4.7.2 for seeds 1, 42 and 12345:
//   seed 1     randi()            1811587497, 683407368, 2033395789, 2375931748
//   seed 42    randi()            492690617, 1919685028, 3561993920, 683038915
//   seed 12345 randi()            1321476956, 17539747, 3348728241, 2863338820
//   seed 1     randi_range(1,100) 98, 69, 90, 49, 90, 30, 26, 30
//   seed 42    randi_range(1,100) 18, 29, 21, 16, 33, 57, 99, 4
//   seed 12345 randi_range(1,100) 57, 48, 42, 21, 7, 70, 42, 89
//
// Randf is deliberately absent, and the rules never ask for one. The only float
// draw in the whole game is the tie-break by which an easy or medium computer
// player settles for a worse option, and a hard one never reaches it: GDScript
// short-circuits `mistakes > 0.0 and rng.randf() < mistakes`, so at hard the
// generator is untouched and the choice is wholly deterministic.
//
// What is known about it, measured against 4.7.2 rather than guessed, so nobody
// has to find it out twice:
//   - one randf() costs exactly two randi() draws (the state afterwards is the
//     state after two, and the next randi is the third of the plain sequence);
//   - it returns a value exactly representable as a 32-bit float;
//   - seed 1     -> 0.32955908775329590, 0.27659484744071960
//     seed 42    -> 0.11837019026279449, 0.65903240442276001
//     seed 12345 -> 0.25204190611839294, 0.66667300462722778
//     against the plain draws seed 1 -> 1811587497, 683407368, 2033395789, ...
// No simple combination of those two draws reproduces all six values, so the
// exponent trick needs reading out of the engine source rather than inferring.
// Until it is, easy and medium cannot be replayed bit-for-bit -- hard can, and
// that is what the parity tests use.

#pragma once

#include <cstdint>

namespace TMSim
{
	class FSimRandom
	{
	public:
		FSimRandom() { Seed(0); }
		explicit FSimRandom(uint64_t InSeed) { Seed(InSeed); }

		/** Restarts the sequence. The same seed always gives the same battle. */
		void Seed(uint64_t InSeed)
		{
			CurrentSeed = InSeed;
			State = 0;
			Next();
			State += InSeed;
			Next();
		}

		/** The raw 32-bit draw: Godot's randi(). */
		uint32_t Randi() { return Next(); }

		/**
		 * Godot's randi_range(): inclusive at both ends. Modulo, bias and all --
		 * matching Godot matters more here than an even spread, and the rolls it
		 * is used for are percentages out of 100.
		 */
		int64_t RandiRange(int64_t From, int64_t To)
		{
			if (To < From)
			{
				const int64_t Swap = From;
				From = To;
				To = Swap;
			}
			return static_cast<int64_t>(Next()) % (To - From + 1) + From;
		}

		/** Saving and restoring mid-battle, for snapshots the AI thinks on. */
		uint64_t GetState() const { return State; }
		void SetState(uint64_t InState) { State = InState; }
		uint64_t GetSeed() const { return CurrentSeed; }

	private:
		uint32_t Next()
		{
			const uint64_t Old = State;
			State = Old * Multiplier + Increment;
			const uint32_t Xorshifted = static_cast<uint32_t>(((Old >> 18u) ^ Old) >> 27u);
			const uint32_t Rot = static_cast<uint32_t>(Old >> 59u);
			return (Xorshifted >> Rot) | (Xorshifted << ((0u - Rot) & 31u));
		}

		static constexpr uint64_t Multiplier = 6364136223846793005ull;
		static constexpr uint64_t Increment = (1442695040888963407ull << 1) | 1ull;

		uint64_t State = 0;
		uint64_t CurrentSeed = 0;
	};
}
