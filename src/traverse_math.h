#pragma once

#include <stdint.h>

// Where the wire guide belongs for a given spindle step count. Pure integer
// maths, no Arduino dependencies, so the host simulation can compile it too.
//
// The guide position is a fixed function of the spindle's step count, so it
// follows the spindle's ramps, pauses and stops by construction:
//
//   layer = S / (N * stepsPerTurn)        N = turns per layer
//   u     = S % (N * stepsPerTurn)        spindle steps into this layer
//   off   = u * pitch / stepsPerTurn      guide travel into this layer
//   x     = start + off                   (layers moving away from home)
//   x     = start + N * pitch - off       (layers moving toward home)
//
// Scatter winding has no closed form: each layer has its own pitch and
// turnaround points. makeScatter() builds a table of the layers from a seed
// before the run, and the position is looked up in it, so it is still a
// fixed function of the spindle step count:
//
//   layer L holds the turns from[L].startTurn up to [L + 1].startTurn (n)
//   u     = S - startTurn * stepsPerTurn
//   x     = from + u * (to - from) / (n * stepsPerTurn)
namespace traverse_math
{
    constexpr uint32_t Q = 16; // fixed point fraction bits for the pitch

    // One layer of a scatter winding. Its end is the next layer's start, so
    // a table of L layers has L + 1 entries (the last only ends the last layer).
    struct Layer
    {
        uint32_t startTurn; // turns wound before this layer
        int32_t fromSteps;  // guide position at the layer's start, steps from home
        int32_t toSteps;    // ... and at its end (the next layer's fromSteps)
    };

    struct Pattern
    {
        int32_t startSteps;           // the near edge of the winding, steps from home
        uint64_t pitchQ;              // guide travel per spindle turn, steps << Q
        uint32_t turnsPerLayer;       // N
        uint32_t spindleStepsPerTurn;
        bool firstAway;               // the first layer moves away from home
        const Layer* table;           // scatter: the layers (tableLayers + 1 entries), else nullptr
        uint32_t tableLayers;
    };

    // Builds a pattern from um values. Returns false if not even one turn
    // fits in the width.
    inline bool makePattern(Pattern* p, uint32_t wireUm, uint32_t widthUm, uint32_t offsetUm, bool firstAway,
                            uint32_t guideStepsPerMm, uint32_t spindleStepsPerTurn)
    {
        if (wireUm == 0 || widthUm < wireUm) return false;
        p->pitchQ              = (((uint64_t)wireUm * guideStepsPerMm << Q) + 500) / 1000;
        p->turnsPerLayer       = widthUm / wireUm;
        p->spindleStepsPerTurn = spindleStepsPerTurn;
        p->startSteps          = (int32_t)(((uint64_t)offsetUm * guideStepsPerMm + 500) / 1000);
        p->firstAway           = firstAway;
        p->table               = nullptr;
        p->tableLayers         = 0;
        return true;
    }

    // What a scatter winding is made from
    struct Scatter
    {
        uint32_t wireUm;
        uint32_t nearUm, farUm; // the winding area: flange 1 + margin .. flange 2 - margin
        uint32_t endUm;         // turnarounds land up to this far inside the area's edges
        uint32_t pitchPct;      // each layer's pitch: wire x 1 .. 1 + pitchPct / 100
        uint32_t seed;
        bool firstAway;
        uint32_t turns;         // layers are made until they hold this many turns
    };

    struct ScatterStats
    {
        uint32_t layers;
        uint32_t minTurns, maxTurns; // per layer
        uint32_t maxPitchUm;         // the largest actual pitch, rounded up
    };

    // Stateless 32-bit hash (lowbias32): the same numbers on the host and the
    // Teensy, for any layer, in any order
    inline uint32_t hash32(uint32_t x)
    {
        x ^= x >> 16;
        x *= 0x7feb352du;
        x ^= x >> 15;
        x *= 0x846ca68bu;
        x ^= x >> 16;
        return x;
    }

    // 0 .. range for the seed, an index and a purpose k
    inline uint32_t scatterRandom(uint32_t seed, uint32_t i, uint32_t k, uint32_t range)
    {
        uint32_t h = hash32(hash32(seed) ^ (i * 4 + k) * 0x9e3779b9u);
        return (uint32_t)(((uint64_t)h * (range + 1)) >> 32);
    }

    // Builds the layer table into `table` (maxLayers + 1 entries) and `p`,
    // or with both nullptr only fills `stats` (a preview). Turnaround i
    // starts layer i: on the near side it is random in near .. near + end,
    // on the far side in far - end .. far. Each layer's turns are its span
    // over a random pitch, rounded down, so the actual pitch is never below
    // the wire. Returns false if the area is too narrow for the end scatter
    // or the turns need more than maxLayers layers.
    inline bool makeScatter(Pattern* p, Layer* table, uint32_t maxLayers, ScatterStats* stats, const Scatter& s,
                            uint32_t guideStepsPerMm, uint32_t spindleStepsPerTurn)
    {
        auto steps = [&](uint32_t um) { return (int32_t)(((uint64_t)um * guideStepsPerMm + 500) / 1000); };
        int32_t nearSteps = steps(s.nearUm), farSteps = steps(s.farUm), endSteps = steps(s.endUm);
        uint64_t wireQ    = (((uint64_t)s.wireUm * guideStepsPerMm << Q) + 500) / 1000;
        if (s.wireUm == 0 || farSteps - nearSteps <= 2 * endSteps) return false;

        auto turnaround = [&](uint32_t i) {
            int32_t r = (int32_t)scatterRandom(s.seed, i, 0, endSteps);
            return ((i & 1) == 0) == s.firstAway ? nearSteps + r : farSteps - r;
        };

        ScatterStats st{0, UINT32_MAX, 0, 0};
        uint64_t maxPitchQ = 0;
        uint32_t turn      = 0;
        int32_t from       = turnaround(0);
        for (; turn < s.turns; st.layers++)
        {
            if (st.layers >= maxLayers) return false;
            int32_t to      = turnaround(st.layers + 1);
            uint64_t spanQ  = (uint64_t)(to > from ? to - from : from - to) << Q;
            uint64_t pitchQ = wireQ * (1000 + scatterRandom(s.seed, st.layers, 1, s.pitchPct * 10)) / 1000;
            uint32_t n      = (uint32_t)(spanQ / pitchQ);
            if (n == 0) n = 1;
            if (table) table[st.layers] = Layer{turn, from, to};
            if (spanQ / n > maxPitchQ) maxPitchQ = spanQ / n; // the actual pitch
            if (n < st.minTurns) st.minTurns = n;
            if (n > st.maxTurns) st.maxTurns = n;
            turn += n;
            from = to;
        }
        if (table) table[st.layers] = Layer{turn, from, from};
        if (st.layers == 0) st.minTurns = 0;
        st.maxPitchUm = (uint32_t)((maxPitchQ * 1000 + ((uint64_t)guideStepsPerMm << Q) - 1) /
                                   ((uint64_t)guideStepsPerMm << Q));
        if (stats) *stats = st;
        if (p)
        {
            p->startSteps          = nearSteps;
            p->pitchQ              = wireQ;
            p->turnsPerLayer       = st.maxTurns;
            p->spindleStepsPerTurn = spindleStepsPerTurn;
            p->firstAway           = s.firstAway;
            p->table               = table;
            p->tableLayers = st.layers;
        }
        return true;
    }

    // Guide travel across one layer, in steps (N * pitch, rounded down)
    inline int32_t layerSpanSteps(const Pattern& p)
    {
        return (int32_t)((p.turnsPerLayer * p.pitchQ) >> Q);
    }

    // Scatter: the last layer that starts at or before this turn
    inline uint32_t tableLayerOf(const Pattern& p, uint32_t turn)
    {
        uint32_t lo = 0, hi = p.tableLayers;
        while (hi - lo > 1)
        {
            uint32_t mid = (lo + hi) / 2;
            if (p.table[mid].startTurn <= turn) lo = mid;
            else hi = mid;
        }
        return lo;
    }

    inline uint32_t layerOf(const Pattern& p, uint32_t spindleSteps)
    {
        if (p.table) return tableLayerOf(p, spindleSteps / p.spindleStepsPerTurn);
        return spindleSteps / (p.turnsPerLayer * p.spindleStepsPerTurn);
    }

    // Layers a run of this many spindle steps winds
    inline uint32_t layerCount(const Pattern& p, uint32_t totalSteps)
    {
        if (p.table) return p.tableLayers;
        uint32_t turns = totalSteps / p.spindleStepsPerTurn;
        return (turns + p.turnsPerLayer - 1) / p.turnsPerLayer;
    }

    // Guide position (steps from home) for a spindle step count
    inline int32_t position(const Pattern& p, uint32_t spindleSteps)
    {
        if (p.table)
        {
            uint32_t L          = tableLayerOf(p, spindleSteps / p.spindleStepsPerTurn);
            const Layer& l      = p.table[L];
            uint32_t layerSteps = (p.table[L + 1].startTurn - l.startTurn) * p.spindleStepsPerTurn;
            uint32_t u          = spindleSteps - l.startTurn * p.spindleStepsPerTurn;
            if (u > layerSteps) u = layerSteps; // past the table's end: stay at its last point
            return l.fromSteps + (int32_t)((int64_t)u * (l.toSteps - l.fromSteps) / layerSteps);
        }
        uint32_t stepsPerLayer = p.turnsPerLayer * p.spindleStepsPerTurn;
        uint32_t layer         = spindleSteps / stepsPerLayer;
        uint32_t u             = spindleSteps % stepsPerLayer;
        uint64_t offQ          = (uint64_t)u * p.pitchQ / p.spindleStepsPerTurn;
        uint64_t spanQ         = p.turnsPerLayer * p.pitchQ;
        bool away              = ((layer & 1) == 0) == p.firstAway;
        return p.startSteps + (int32_t)((away ? offQ : spanQ - offQ) >> Q);
    }
}
