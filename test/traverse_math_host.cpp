// Host test for src/traverse_math.h (not built by PlatformIO):
//   c++ -std=c++17 -O2 -Isrc test/traverse_math_host.cpp -o /tmp/tm && /tmp/tm
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "traverse_math.h"

using namespace traverse_math;

namespace
{
    constexpr uint32_t STEPS_PER_MM   = 3200;
    constexpr uint32_t STEPS_PER_TURN = 3200;
    constexpr uint32_t MAX_LAYERS     = 4096;

    int failures = 0;

    void check(bool ok, const char* what, long a = 0, long b = 0)
    {
        if (ok) return;
        if (++failures <= 20) printf("  FAIL: %s (%ld, %ld)\n", what, a, b);
    }

    int32_t steps(uint32_t um)
    {
        return (int32_t)(((uint64_t)um * STEPS_PER_MM + 500) / 1000);
    }

    // The even-layer formula as brought up on hardware (before scatter)
    int32_t plainReference(const Pattern& p, uint32_t s)
    {
        uint32_t stepsPerLayer = p.turnsPerLayer * p.spindleStepsPerTurn;
        uint32_t layer         = s / stepsPerLayer;
        uint32_t u             = s % stepsPerLayer;
        uint64_t offQ          = (uint64_t)u * p.pitchQ / p.spindleStepsPerTurn;
        uint64_t spanQ         = p.turnsPerLayer * p.pitchQ;
        bool away              = ((layer & 1) == 0) == p.firstAway;
        return p.startSteps + (int32_t)((away ? offQ : spanQ - offQ) >> Q);
    }

    void testPlain(uint32_t wireUm, uint32_t widthUm, uint32_t offsetUm, bool away, uint32_t turns)
    {
        Pattern p;
        check(makePattern(&p, wireUm, widthUm, offsetUm, away, STEPS_PER_MM, STEPS_PER_TURN), "plain pattern");
        check(p.table == nullptr, "plain pattern has no table");
        for (uint32_t s = 0; s <= turns * STEPS_PER_TURN; s++)
            if (position(p, s) != plainReference(p, s))
            {
                check(false, "plain position differs from the reference", s, position(p, s));
                break;
            }
        printf("plain  %3u um, %5u um wide: %u turns/layer, %u layers\n", wireUm, widthUm, p.turnsPerLayer,
               layerCount(p, turns * STEPS_PER_TURN));
    }

    void testScatter(const Scatter& s)
    {
        std::vector<Layer> table(MAX_LAYERS + 1);
        Pattern p;
        ScatterStats preview, st;
        check(makeScatter(nullptr, nullptr, MAX_LAYERS, &preview, s, STEPS_PER_MM, STEPS_PER_TURN), "preview");
        check(makeScatter(&p, table.data(), MAX_LAYERS, &st, s, STEPS_PER_MM, STEPS_PER_TURN), "table");
        check(!memcmp(&preview, &st, sizeof(st)), "preview stats equal the table's");
        check(p.table == table.data() && p.tableLayers == st.layers, "pattern points at the table");

        int32_t nearS = steps(s.nearUm), farS = steps(s.farUm), endS = steps(s.endUm);
        uint64_t wireQ = (((uint64_t)s.wireUm * STEPS_PER_MM << Q) + 500) / 1000;

        // Layers: turnarounds within the scatter bands, pitch never below the wire
        check(table[st.layers - 1].startTurn < s.turns && table[st.layers].startTurn >= s.turns, "layers hold the turns",
              table[st.layers].startTurn, s.turns);
        for (uint32_t L = 0; L <= st.layers; L++)
        {
            int32_t x  = table[L].fromSteps;
            bool nearSide = ((L & 1) == 0) == s.firstAway;
            if (nearSide) check(x >= nearS && x <= nearS + endS, "near turnaround in its band", L, x);
            else check(x <= farS && x >= farS - endS, "far turnaround in its band", L, x);
            if (L == st.layers) break;
            check(table[L].toSteps == table[L + 1].fromSteps, "layer ends where the next starts", L);
            uint32_t n     = table[L + 1].startTurn - table[L].startTurn;
            uint64_t spanQ = (uint64_t)abs(table[L].toSteps - table[L].fromSteps) << Q;
            check(n >= 1, "at least one turn per layer", L);
            check(spanQ / n >= wireQ || n == 1, "pitch at least the wire", L, n);
            check(spanQ / n * 1000 <= (uint64_t)st.maxPitchUm * STEPS_PER_MM << Q, "pitch within the stats' max", L);
        }

        // Every spindle step: at most one guide step, inside the area,
        // exactly on the turnarounds at layer boundaries
        uint32_t total = s.turns * STEPS_PER_TURN;
        int32_t prev   = position(p, 0);
        check(prev == table[0].fromSteps, "starts at the first turnaround");
        uint32_t layer = 0;
        for (uint32_t S = 1; S <= total; S++)
        {
            int32_t x = position(p, S);
            if (abs(x - prev) > 1) check(false, "more than one guide step per spindle step", S, x - prev);
            if (x < nearS || x > farS) check(false, "inside the winding area", S, x);
            prev = x;
            uint32_t l = layerOf(p, S);
            if (l != layer && l != layer + 1) check(false, "layers advance one at a time", S, l);
            layer = l;
            if (S % STEPS_PER_TURN == 0 && table[l].startTurn * STEPS_PER_TURN == S && x != table[l].fromSteps)
                check(false, "on the turnaround at a layer boundary", S, x);
        }
        check(layerCount(p, total) == st.layers, "layer count");

        // The same seed gives the same table, another seed another one
        std::vector<Layer> again(MAX_LAYERS + 1);
        makeScatter(nullptr, again.data(), MAX_LAYERS, nullptr, s, STEPS_PER_MM, STEPS_PER_TURN);
        check(!memcmp(table.data(), again.data(), (st.layers + 1) * sizeof(Layer)), "same seed, same table");
        Scatter other = s;
        other.seed++;
        makeScatter(nullptr, again.data(), MAX_LAYERS, nullptr, other, STEPS_PER_MM, STEPS_PER_TURN);
        if (s.endUm || s.pitchPct)
            check(memcmp(table.data(), again.data(), (st.layers + 1) * sizeof(Layer)) != 0, "other seed, other table");

        printf("scatter %3u um, %5u..%5u um, ends %4u um, pitch +%3u%%, seed %5u: %u layers, %u..%u turns/layer, "
               "max pitch %u um\n",
               s.wireUm, s.nearUm, s.farUm, s.endUm, s.pitchPct, s.seed, st.layers, st.minTurns, st.maxTurns,
               st.maxPitchUm);
    }
}

int main()
{
    testPlain(71, 10000, 6000, true, 2000);
    testPlain(56, 46000, 1000, false, 3000);
    testPlain(500, 1000, 0, true, 50);

    //           wire  near   far    ends  pitch% seed   away   turns
    testScatter({71, 8500, 17500, 500, 30, 1, true, 11000});
    testScatter({71, 8500, 17500, 500, 30, 4711, false, 11000});
    testScatter({56, 2000, 46000, 3000, 100, 99999, true, 11000});
    testScatter({76, 6000, 7000, 300, 0, 7, true, 3000});   // narrow: few turns per layer
    testScatter({71, 8500, 17500, 0, 50, 3, true, 8000});   // pitch scatter only
    testScatter({71, 8500, 17500, 1000, 0, 3, false, 8000}); // turnaround scatter only
    testScatter({71, 8500, 17500, 0, 0, 3, true, 2000});    // neither: even layers over the full area

    // Refusals: ends too large for the area, more layers than the table holds
    Scatter tooWide{71, 8500, 9500, 500, 30, 1, true, 1000};
    check(!makeScatter(nullptr, nullptr, MAX_LAYERS, nullptr, tooWide, STEPS_PER_MM, STEPS_PER_TURN), "end scatter refused");
    Scatter tooMany{76, 6000, 6300, 0, 100, 1, true, 11000};
    check(!makeScatter(nullptr, nullptr, MAX_LAYERS, nullptr, tooMany, STEPS_PER_MM, STEPS_PER_TURN), "too many layers refused");

    printf(failures ? "%d FAILURE(S)\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
