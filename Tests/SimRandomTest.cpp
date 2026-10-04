#include "SimRandom.h"
#include <cstdio>
using namespace TMSim;

static int Failures = 0;

static void CheckRandi(uint64_t Seed, const uint32_t* Expected, int Count)
{
    FSimRandom R(Seed);
    printf("seed %-6llu randi   ", (unsigned long long)Seed);
    bool Ok = true;
    for (int i = 0; i < Count; ++i)
    {
        uint32_t V = R.Randi();
        if (V != Expected[i]) Ok = false;
        printf("%u ", V);
    }
    printf(" %s\n", Ok ? "OK" : "MISMATCH");
    if (!Ok) ++Failures;
}

static void CheckRange(uint64_t Seed, const int* Expected, int Count)
{
    FSimRandom R(Seed);
    printf("seed %-6llu range   ", (unsigned long long)Seed);
    bool Ok = true;
    for (int i = 0; i < Count; ++i)
    {
        int V = (int)R.RandiRange(1, 100);
        if (V != Expected[i]) Ok = false;
        printf("%d ", V);
    }
    printf(" %s\n", Ok ? "OK" : "MISMATCH");
    if (!Ok) ++Failures;
}

int main()
{
    const uint32_t R1[]  = {1811587497u, 683407368u, 2033395789u, 2375931748u};
    const uint32_t R42[] = {492690617u, 1919685028u, 3561993920u, 683038915u};
    const uint32_t R12[] = {1321476956u, 17539747u, 3348728241u, 2863338820u};
    CheckRandi(1, R1, 4); CheckRandi(42, R42, 4); CheckRandi(12345, R12, 4);

    const int G1[]  = {98, 69, 90, 49, 90, 30, 26, 30};
    const int G42[] = {18, 29, 21, 16, 33, 57, 99, 4};
    const int G12[] = {57, 48, 42, 21, 7, 70, 42, 89};
    CheckRange(1, G1, 8); CheckRange(42, G42, 8); CheckRange(12345, G12, 8);

    // Measured in Godot 4.7.2, not derived: randf() two at a time.
    {
        const struct { uint64_t Seed; double A; double B; } Floats[] = {
            {1, 0.32955908775329590, 0.27659484744071960},
            {42, 0.11837019026279449, 0.65903240442276001},
            {12345, 0.25204190611839294, 0.66667300462722778},
        };
        for (const auto& F : Floats)
        {
            FSimRandom R(F.Seed);
            const double A = R.Randf();
            const double B = R.Randf();
            const bool Ok = A == F.A && B == F.B;
            printf("seed %-6llu randf   %.17f %.17f  %s\n", (unsigned long long)F.Seed, A, B, Ok ? "OK" : "MISMATCH");
            if (!Ok) ++Failures;
        }
    }

    // randi_range with equal ends draws nothing, and a wide range throws away
    // the draws its bounded draw rejects (seed 0's first) -- both measured.
    {
        FSimRandom Equal(1);
        const bool EqualOk = Equal.RandiRange(3, 3) == 3 && Equal.Randi() == 1811587497u;
        printf("seed 1      equal ends draw nothing  %s\n", EqualOk ? "OK" : "MISMATCH");
        if (!EqualOk) ++Failures;
        const struct { uint64_t Seed; int64_t Value; uint32_t Next; } Wide[] = {
            {0, 1327520283, 692503688u}, {1, 200974761, 683407368u}, {3, 1282583244, 4290596118u},
        };
        for (const auto& W : Wide)
        {
            FSimRandom R(W.Seed);
            const int64_t V = R.RandiRange(0, 1610612735);
            const uint32_t Next = R.Randi();
            const bool Ok = V == W.Value && Next == W.Next;
            printf("seed %-6llu wide range %lld then %u  %s\n", (unsigned long long)W.Seed, (long long)V, Next, Ok ? "OK" : "MISMATCH");
            if (!Ok) ++Failures;
        }
    }

    printf("\n%s\n", Failures == 0 ? "THE DICE ROLL AS RECORDED, BIT FOR BIT" : "THE DICE CHANGED");
    return Failures;
}
