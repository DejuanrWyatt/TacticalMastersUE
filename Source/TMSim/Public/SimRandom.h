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
// Randf is Godot's too, used only by an easy or medium computer player deciding
// whether to settle for a worse option (ai_player.gd:154). Hard never draws it:
// GDScript short-circuits `mistakes > 0.0 and rng.randf() < mistakes`.
// RandomPCG::randf (core/math/random_pcg.h) takes one draw for an exponent --
// how many leading zeros it has -- and a second for the digits, with the top
// and bottom bits forced on, and scales the second by the first. Two draws per
// call, a result exactly representable as a 32-bit float, and these values,
// measured against Godot 4.7.2 and matched exactly:
//   seed 1     -> 0.32955908775329590, 0.27659484744071960
//   seed 42    -> 0.11837019026279449, 0.65903240442276001
//   seed 12345 -> 0.25204190611839294, 0.66667300462722778

#pragma once

#include <cmath>

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
		 * Godot's randi_range(): inclusive at both ends, either way round
		 * (RandomPCG::random, core/math/random_pcg.cpp). Two things about it
		 * matter, both measured against 4.7.2:
		 *   - equal ends return at once and draw nothing: randi_range(3, 3) on
		 *     seed 1 leaves the next randi() at 1811587497, the first draw;
		 *   - the draw is PCG's bounded one (pcg32_boundedrand_r), which throws
		 *     away draws below 2^32 mod the range, so the result is unbiased. For
		 *     a range of 100 only draws under 96 are thrown away, which is why
		 *     a plain modulo agreed on every percentage roll ever measured. On
		 *     0..1610612735 it shows: seed 0 -> 1327520283 (its first draw,
		 *     881477183, thrown away), seed 1 -> 200974761, seed 3 -> 1282583244.
		 */
		int64_t RandiRange(int64_t From, int64_t To)
		{
			if (From == To)
			{
				return From;
			}
			const int64_t Low = From < To ? From : To;
			const uint32_t Bound = static_cast<uint32_t>((From < To ? To - From : From - To) + 1);
			const uint32_t Threshold = (0u - Bound) % Bound;
			while (true)
			{
				const uint32_t Draw = Next();
				if (Draw >= Threshold)
				{
					return Low + static_cast<int64_t>(Draw % Bound);
				}
			}
		}

		/** Godot's randf(): a float in [0, 1], two draws (see the note at the top). */
		float Randf()
		{
			const uint32_t Exponent = Next();
			if (Exponent == 0)
			{
				return 0.0f;
			}
			int Zeros = 0;
			for (uint32_t Bit = 0x80000000u; Bit != 0 && (Exponent & Bit) == 0; Bit >>= 1)
			{
				++Zeros;
			}
			return std::ldexp(static_cast<float>(Next() | 0x80000001u), -32 - Zeros);
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
