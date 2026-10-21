#include "traverse.h"
#include "config.h"
#include "tmc_driver.h"
#include "winder.h"

namespace traverse
{
    namespace
    {
        static_assert(TRAVERSE_MICROSTEPS == MICROSTEPS, "pulse check assumes the same microsteps on both axes");

        constexpr int32_t TRAVEL_STEPS = (int64_t)TRAVERSE_TRAVEL_UM * TRAVERSE_STEPS_PER_MM / 1000;
        constexpr int32_t UNHOMED_JOG_UM = 5000;

        int32_t umToSteps(int32_t um)
        {
            return (int32_t)(((int64_t)um * TRAVERSE_STEPS_PER_MM + (um < 0 ? -500 : 500)) / 1000);
        }

        // Moves run a trapezoid in Q24 fixed point: steps per tick, and steps
        // per tick per tick
        constexpr uint32_t ONE = 1u << 24;
        constexpr uint32_t speedQ(uint32_t umPerS)
        {
            return (uint64_t)umPerS * TRAVERSE_STEPS_PER_MM * ONE / 1000 / TRAVERSE_TICK_HZ;
        }
        constexpr uint32_t ACCEL_Q = (uint64_t)TRAVERSE_ACCEL_UM_S2 * TRAVERSE_STEPS_PER_MM * ONE / 1000 /
                                     TRAVERSE_TICK_HZ / TRAVERSE_TICK_HZ;
        constexpr uint32_t V_MIN_Q = speedQ(HOMING_SLOW_UM_S); // start / end speed
        static_assert(ACCEL_Q > 0, "traverse acceleration too small for the tick rate");

        enum class Mode : uint8_t { Idle, Move, Follow };

        IntervalTimer timer;

        // Shared with the step ISR. The loop only writes the move/follow
        // parameters while mode is Idle, then sets mode last.
        volatile Mode mode     = Mode::Idle;
        volatile int32_t pos   = 0;
        volatile bool isHomed  = false;
        volatile int32_t moveTarget;
        volatile uint32_t vMaxQ;
        volatile bool stopOnEndstop, stopRequested, endstopHit, limitStop;
        volatile bool followLimit;
        volatile int32_t followMaxErr;
        traverse_math::Pattern pattern;

        // ISR only
        uint32_t vQ = 0, phase = 0;
        int8_t dirNow = 0;

        bool enabled          = false;
        bool moveActive       = false;
        const char* errorText = nullptr;

        enum class Homing : uint8_t { None, ClearSwitch, Approach, BackOff, Slow, Park };
        Homing homing = Homing::None;

        FASTRUN bool endstopNow()
        {
            return digitalReadFast(PIN_ENDSTOP) == ENDSTOP_TRIGGERED_LEVEL;
        }

        enum class StepResult : uint8_t { Stepped, DirChanged, Limit };

        // One step in direction dir (+1 / -1). After a DIR change the step
        // waits for the next tick (DIR setup time, and a clean reversal).
        FASTRUN StepResult stepToward(int8_t dir)
        {
            int32_t next = pos + dir;
            if (isHomed && (next < 0 || next > TRAVEL_STEPS)) return StepResult::Limit;
            if (dir != dirNow)
            {
                digitalWriteFast(PIN_TRAV_DIR, (dir > 0) != TRAVERSE_DIR_INVERT ? HIGH : LOW);
                dirNow = dir;
                return StepResult::DirChanged;
            }
            digitalWriteFast(PIN_TRAV_STEP, HIGH);
            delayNanoseconds(200); // TMC2209 needs >= 100 ns
            digitalWriteFast(PIN_TRAV_STEP, LOW);
            pos = next;
            return StepResult::Stepped;
        }

        FASTRUN void tickMove()
        {
            if (stopOnEndstop && endstopNow())
            {
                endstopHit = true;
                mode       = Mode::Idle;
                return;
            }
            int32_t remaining = moveTarget - pos;
            if (remaining == 0)
            {
                mode = Mode::Idle;
                return;
            }

            // Brake when the distance left is what it takes to slow down
            uint32_t stopDist = ((uint64_t)vQ * vQ / (2ull * ACCEL_Q)) >> 24;
            if (stopRequested || (uint32_t)abs(remaining) <= stopDist)
            {
                if (vQ > V_MIN_Q + ACCEL_Q) vQ -= ACCEL_Q;
                else if (stopRequested)
                {
                    mode = Mode::Idle;
                    return;
                }
                else vQ = V_MIN_Q;
            }
            else if (vQ < vMaxQ) vQ = min<uint32_t>(vQ + ACCEL_Q, vMaxQ);

            phase += vQ;
            if (phase < ONE) return;
            switch (stepToward(remaining > 0 ? 1 : -1))
            {
                case StepResult::Stepped: phase -= ONE; break;
                case StepResult::DirChanged: break; // step on the next tick
                case StepResult::Limit:
                    limitStop = true;
                    mode      = Mode::Idle;
                    break;
            }
        }

        FASTRUN void tickFollow()
        {
            int32_t target = traverse_math::position(pattern, winder::stepsDone());
            int32_t err    = target - pos;
            if (err == 0) return;
            if (abs(err) > followMaxErr) followMaxErr = abs(err);
            if (stepToward(err > 0 ? 1 : -1) == StepResult::Limit) followLimit = true;
        }

        FASTRUN void tick()
        {
            if (mode == Mode::Move) tickMove();
            else if (mode == Mode::Follow) tickFollow();
        }

        void startMove(int32_t targetSteps, uint32_t speedUmS, bool untilEndstop)
        {
            enableDriver(true);
            moveTarget    = targetSteps;
            vMaxQ         = max(speedQ(speedUmS), V_MIN_Q);
            stopOnEndstop = untilEndstop;
            stopRequested = false;
            endstopHit    = false;
            limitStop     = false;
            vQ            = V_MIN_Q;
            phase         = 0;
            moveActive    = true;
            mode          = Mode::Move; // last: hands the move to the ISR
        }

        Event failHoming(const char* why)
        {
            homing    = Homing::None;
            errorText = why;
            return Event::HomingFailed;
        }

        // The next step of the homing sequence, after a move ended
        Event stepHoming()
        {
            if (stopRequested) return failHoming("homing stopped");
            switch (homing)
            {
                case Homing::ClearSwitch:
                    if (endstopNow()) return failHoming("endstop stuck or wire broken");
                    homing = Homing::Approach;
                    startMove(pos - umToSteps(HOMING_SEARCH_UM), HOMING_FAST_UM_S, true);
                    break;
                case Homing::Approach:
                    if (!endstopHit) return failHoming("endstop not found");
                    homing = Homing::BackOff;
                    startMove(pos + umToSteps(HOMING_BACKOFF_UM), HOMING_FAST_UM_S, false);
                    break;
                case Homing::BackOff:
                    if (endstopNow()) return failHoming("endstop did not release");
                    homing = Homing::Slow;
                    startMove(pos - 2 * umToSteps(HOMING_BACKOFF_UM), HOMING_SLOW_UM_S, true);
                    break;
                case Homing::Slow:
                    if (!endstopHit) return failHoming("endstop not found on the slow approach");
                    pos     = 0;
                    isHomed = true;
                    homing  = Homing::Park;
                    startMove(umToSteps(HOMING_CLEAR_UM), HOMING_FAST_UM_S, false);
                    break;
                case Homing::Park: homing = Homing::None; return Event::Homed;
                case Homing::None: break;
            }
            return Event::None;
        }

        const char* checkMotion()
        {
            if (mode == Mode::Follow) return "not while winding";
            if (homing != Homing::None || mode == Mode::Move) return "traverse is moving";
            if (!tmc::traverse.ready()) return "traverse driver not ready - check 24 V, then re-init";
            return nullptr;
        }
    }

    void begin()
    {
        pinMode(PIN_TRAV_EN, OUTPUT);
        enableDriver(false);
        pinMode(PIN_TRAV_STEP, OUTPUT);
        digitalWriteFast(PIN_TRAV_STEP, LOW);
        pinMode(PIN_TRAV_DIR, OUTPUT);
        pinMode(PIN_ENDSTOP, INPUT); // the module has its own pull-up

        // Below the spindle's step timer (16), above USB and UART
        timer.priority(32);
        timer.begin(tick, 1000000.0f / TRAVERSE_TICK_HZ);
    }

    Event update()
    {
        if (mode == Mode::Move || !moveActive) return Event::None;
        moveActive = false;
        if (homing != Homing::None) return stepHoming();
        if (limitStop) errorText = "soft limit reached";
        return Event::MoveDone;
    }

    const char* home()
    {
        if (const char* err = checkMotion()) return err;
        errorText = nullptr;
        isHomed   = false;
        pos       = 0; // relative until the switch is found
        if (endstopNow())
        {
            // Already on the switch: move off it first
            homing = Homing::ClearSwitch;
            startMove(2 * umToSteps(HOMING_BACKOFF_UM), HOMING_FAST_UM_S, false);
        }
        else
        {
            homing = Homing::Approach;
            startMove(-umToSteps(HOMING_SEARCH_UM), HOMING_FAST_UM_S, true);
        }
        return nullptr;
    }

    const char* jog(int32_t um)
    {
        if (const char* err = checkMotion()) return err;
        if (!isHomed && abs(um) > UNHOMED_JOG_UM) return "not homed: jog at most 5 mm";
        int32_t target = pos + umToSteps(um);
        if (isHomed && (target < 0 || target > TRAVEL_STEPS)) return "outside 0..50 mm";
        errorText = nullptr;
        startMove(target, TRAVERSE_MOVE_UM_S, false);
        return nullptr;
    }

    const char* moveTo(int32_t um)
    {
        return moveToSteps(umToSteps(um));
    }

    const char* moveToSteps(int32_t steps)
    {
        if (const char* err = checkMotion()) return err;
        if (!isHomed) return "traverse not homed - home it first";
        if (steps < 0 || steps > TRAVEL_STEPS) return "outside 0..50 mm";
        errorText = nullptr;
        startMove(steps, TRAVERSE_MOVE_UM_S, false);
        return nullptr;
    }

    void stop()
    {
        if (mode == Mode::Move) stopRequested = true;
    }

    void follow(const traverse_math::Pattern& p)
    {
        if (mode != Mode::Idle) return;
        enableDriver(true);
        pattern      = p;
        followLimit  = false;
        followMaxErr = 0;
        mode         = Mode::Follow; // last: hands the pattern to the ISR
    }

    void stopFollow()
    {
        if (mode == Mode::Follow) mode = Mode::Idle;
    }

    bool following() { return mode == Mode::Follow; }
    bool limitHit() { return followLimit; }
    int32_t maxFollowError() { return followMaxErr; }
    bool homed() { return isHomed; }
    bool busy() { return homing != Homing::None || mode == Mode::Move; }
    int32_t positionSteps() { return pos; }
    float positionMm() { return (float)pos / TRAVERSE_STEPS_PER_MM; }
    bool endstopTriggered() { return endstopNow(); }
    const char* lastError() { return errorText; }

    void enableDriver(bool on)
    {
        if (!on)
        {
            mode    = Mode::Idle;
            homing  = Homing::None;
            isHomed = false; // the slide may be moved by hand while free
        }
        enabled = on;
        digitalWriteFast(PIN_TRAV_EN, on ? LOW : HIGH);
    }

    bool driverEnabled() { return enabled; }
}
