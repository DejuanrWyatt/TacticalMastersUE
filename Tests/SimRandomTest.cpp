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

    printf("\n%s\n", Failures == 0 ? "C++ MATCHES GODOT BIT FOR BIT" : "PARITY BROKEN");
    return Failures;
}
