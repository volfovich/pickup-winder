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
namespace traverse_math
{
    constexpr uint32_t Q = 16; // fixed point fraction bits for the pitch

    struct Pattern
    {
        int32_t startSteps;           // the near edge of the winding, steps from home
        uint64_t pitchQ;              // guide travel per spindle turn, steps << Q
        uint32_t turnsPerLayer;       // N
        uint32_t spindleStepsPerTurn;
        bool firstAway;               // the first layer moves away from home
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
        return true;
    }

    // Guide travel across one layer, in steps (N * pitch, rounded down)
    inline int32_t layerSpanSteps(const Pattern& p)
    {
        return (int32_t)((p.turnsPerLayer * p.pitchQ) >> Q);
    }

    inline uint32_t layerOf(const Pattern& p, uint32_t spindleSteps)
    {
        return spindleSteps / (p.turnsPerLayer * p.spindleStepsPerTurn);
    }

    // Guide position (steps from home) for a spindle step count
    inline int32_t position(const Pattern& p, uint32_t spindleSteps)
    {
        uint32_t stepsPerLayer = p.turnsPerLayer * p.spindleStepsPerTurn;
        uint32_t layer         = spindleSteps / stepsPerLayer;
        uint32_t u             = spindleSteps % stepsPerLayer;
        uint64_t offQ          = (uint64_t)u * p.pitchQ / p.spindleStepsPerTurn;
        uint64_t spanQ         = p.turnsPerLayer * p.pitchQ;
        bool away              = ((layer & 1) == 0) == p.firstAway;
        return p.startSteps + (int32_t)((away ? offQ : spanQ - offQ) >> Q);
    }
}
