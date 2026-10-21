#include "winder.h"
#include "config.h"
#include "tmc_driver.h"
#include <teensystep4.h>

namespace winder
{
    namespace
    {
        // Adds a few safe accessors to TeensyStep4's stepper. The members used
        // here are protected in TS4::StepperBase.
        class WinderStepper : public TS4::Stepper
        {
         public:
            using TS4::Stepper::Stepper;

            // isMoving is written by the step ISR; force a fresh read
            bool moving() const { return *(volatile const bool*)&isMoving; }

            // Immediate stop that lets the step ISR shut the timer down itself
            // on its next step event: no step after this call, no race with
            // the ISR over the timer.
            void hardStop()
            {
                noInterrupts();
                if (isMoving)
                {
                    mode     = mode_t::target;
                    accEnd   = 0;
                    decStart = 0;
                    s_tgt    = s;
                }
                interrupts();
            }

            // Sets max speed (steps/s) and acceleration (steps/s^2).
            // Stepper::setAcceleration() computes vMax*vMax in int32, which
            // overflows above ~46k steps/s, so acc is set directly.
            void setProfile(int32_t vMaxSteps, uint32_t accSteps)
            {
                setMaxSpeed(vMaxSteps);
                acc = accSteps;
            }
        };

        WinderStepper motor(PIN_STEP, PIN_DIR);

        State st             = State::Idle;
        Settings run{};
        uint32_t target      = 0;
        bool stopRequested   = false;
        const char* faultMsg = nullptr;
        Outcome outcome      = Outcome::None;
        Event pending        = Event::None;
        bool enabled         = false;
        uint32_t segStartMs  = 0; // current Running segment
        uint32_t segStartSteps = 0;
        uint32_t windingDone = 0; // ms spent moving in earlier segments

        // Speed over the last second: samples every 250 ms while moving
        constexpr uint8_t SPEED_SAMPLES   = 5;
        constexpr uint32_t SPEED_PERIOD_MS = 250;
        struct Sample
        {
            uint32_t steps;
            uint32_t us;
        };
        Sample speed[SPEED_SAMPLES];
        uint8_t speedCount     = 0;
        uint32_t lastSpeedMs   = 0;

        // Driver-side pulse check: MSCNT before and after the run
        constexpr uint16_t MSCNT_PER_STEP = 256 / MICROSTEPS;
        static_assert(256 % MICROSTEPS == 0, "MICROSTEPS must divide 256");
        bool mscntValid     = false;
        uint16_t mscntStart = 0;
        uint16_t mscntEnd   = 0;

#ifdef STEP_LOOPBACK_PIN
        volatile uint32_t loopbackCount = 0;
        void loopbackISR() { loopbackCount = loopbackCount + 1; }
#endif

        uint32_t rpmToStepsPerS(uint32_t rpm)
        {
            return (rpm * STEPS_PER_REV + 30) / 60;
        }

        uint32_t steps()
        {
            int32_t p = motor.getPosition();
            return (uint32_t)(p < 0 ? -p : p);
        }

        void sampleSpeed(bool restart)
        {
            Sample now{steps(), micros()};
            if (restart) speedCount = 0;
            else if (millis() - lastSpeedMs < SPEED_PERIOD_MS) return;
            lastSpeedMs = millis();
            if (speedCount == SPEED_SAMPLES)
            {
                memmove(speed, speed + 1, (SPEED_SAMPLES - 1) * sizeof(Sample));
                speedCount--;
            }
            speed[speedCount++] = now;
        }

        // One Running segment: the whole run, or what's left after a pause.
        // The position counter carries on, so the total stays exact.
        void beginSegment()
        {
            stopRequested = false;
            enableDriver(true);
            delay(5); // let the coil current settle before the first step

            motor.setProfile(rpmToStepsPerS(run.rpm), rpmToStepsPerS(run.accelRpmPerS));
            st            = State::Running;
            segStartMs    = millis();
            segStartSteps = steps();
            sampleSpeed(true);
            motor.moveRelAsync((int32_t)run.dir * (int32_t)(target - steps()));
        }

        void endRun(Outcome o)
        {
            st      = State::Done;
            outcome = o;
            pending = Event::Ended;
            if (o != Outcome::Faulted && mscntValid) mscntValid = tmc::spindle.readMscnt(&mscntEnd);
        }

        // TeensyStep4 starts and ends a move at v^2 = 200^2 steps^2/s^2 and
        // computes the speed of the last step as sqrt(|200^2 - 2a|). Its TMR
        // timer (16 bit, 4.6875 MHz) can't produce periods below ~72 steps/s,
        // so accelerations that put the last step near zero speed are refused.
        constexpr int32_t TS4_START_V_SQR = 200 * 200;
        constexpr int32_t TS4_MIN_V_SQR   = 72 * 72;

        // TeensyStep4 TMR timing: 150 MHz / 32 clock, 8 us pulse = 38 ticks.
        // Each timer phase lasts COMP1 + 1 ticks.
        constexpr double TS4_TICK_HZ       = 150e6 / 32;
        constexpr uint16_t TS4_PULSE_TICKS = 38;
    }

    float actualRpm(uint32_t rpm)
    {
        // Same period calculation as TmrTimer::updateFrequency()
        uint16_t period = TS4_TICK_HZ / rpmToStepsPerS(rpm) - TS4_PULSE_TICKS - 1.5f;
        uint32_t ticks  = (TS4_PULSE_TICKS + 1) + (period + 1);
        return TS4_TICK_HZ / ticks * 60.0 / STEPS_PER_REV;
    }

    const char* checkTurns(uint32_t turns)
    {
        static char msg[32];
        if (turns >= 1 && turns <= MAX_TURNS) return nullptr;
        snprintf(msg, sizeof(msg), "turns must be 1..%lu", MAX_TURNS);
        return msg;
    }

    const char* checkRpm(uint32_t rpm)
    {
        static char msg[32];
        if (rpm >= MIN_RPM && rpm <= MAX_RPM) return nullptr;
        snprintf(msg, sizeof(msg), "rpm must be %lu..%lu", MIN_RPM, MAX_RPM);
        return msg;
    }

    const char* checkAccel(uint32_t accelRpmPerS)
    {
        static char msg[40];
        if (accelRpmPerS < MIN_ACCEL_RPM_PER_S || accelRpmPerS > MAX_ACCEL_RPM_PER_S)
        {
            snprintf(msg, sizeof(msg), "accel must be %lu..%lu RPM/s", MIN_ACCEL_RPM_PER_S, MAX_ACCEL_RPM_PER_S);
            return msg;
        }
        int32_t endVSqr = TS4_START_V_SQR - 2 * (int32_t)rpmToStepsPerS(accelRpmPerS);
        if (abs(endVSqr) < TS4_MIN_V_SQR) return "accel 327..423 RPM/s: step timer limit";
        return nullptr;
    }

    void begin()
    {
        pinMode(PIN_EN, OUTPUT);
        enableDriver(false);
        pinMode(PIN_STEP, OUTPUT);
        digitalWriteFast(PIN_STEP, LOW);

        TS4::begin(); // uses TMR4 (TMRModule<3>)
        // Step timing must not be held up by USB or UART interrupts
        NVIC_SET_PRIORITY(IRQ_QTIMER4, 16);

#ifdef STEP_LOOPBACK_PIN
        pinMode(STEP_LOOPBACK_PIN, INPUT);
        attachInterrupt(digitalPinToInterrupt(STEP_LOOPBACK_PIN), loopbackISR, RISING);
#endif
    }

    const char* canStart(const Settings& s)
    {
        if (inRun() || motor.moving()) return "run in progress - resume or finish it";
        if (!tmc::spindle.ready()) return "driver not ready - check 24 V, then re-init";
        tmc::Fault f = tmc::spindle.poll(nullptr); // e.g. 24 V switched off and on since the last re-init
        if (f != tmc::Fault::None) return tmc::faultName(f);
        if (const char* err = checkTurns(s.turns)) return err;
        if (const char* err = checkRpm(s.rpm)) return err;
        return checkAccel(s.accelRpmPerS);
    }

    const char* start(const Settings& s)
    {
        if (const char* err = canStart(s)) return err;

        run         = s;
        target      = s.turns * STEPS_PER_REV;
        faultMsg    = nullptr;
        outcome     = Outcome::None;
        windingDone = 0;

        mscntValid = tmc::spindle.readMscnt(&mscntStart);
        motor.setPosition(0); // count each run from zero (also keeps int32 far from overflow)
#ifdef STEP_LOOPBACK_PIN
        loopbackCount = 0;
#endif
        beginSegment();
        return nullptr;
    }

    const char* resume()
    {
        if (st != State::Paused) return "nothing to resume";
        if (!tmc::spindle.ready()) return "driver not ready - check 24 V, then re-init";
        tmc::Fault f = tmc::spindle.poll(nullptr);
        if (f != tmc::Fault::None) return tmc::faultName(f);
        beginSegment();
        return nullptr;
    }

    const char* finish()
    {
        if (st != State::Paused) return "no paused run to finish";
        endRun(Outcome::EndedEarly);
        return nullptr;
    }

    void stop()
    {
        if (st != State::Running || stopRequested) return;
        stopRequested = true;
        motor.stopAsync();
    }

    void estop()
    {
        if (st == State::Running) motor.hardStop();
    }

    void abortOnFault(const char* reason)
    {
        if (st != State::Running) return;
        faultMsg = reason;
        motor.hardStop();
    }

    State state() { return st; }
    bool isMoving() { return st == State::Running; }
    bool inRun() { return st == State::Running || st == State::Paused; }
    bool stopping() { return st == State::Running && stopRequested; }
    bool resumed() { return inRun() && segStartSteps > 0; }

    uint32_t stepsDone() { return steps(); }
    uint32_t targetSteps() { return target; }
    float turnsDone() { return (float)steps() / STEPS_PER_REV; }
    const Settings& runSettings() { return run; }
    const char* faultReason() { return faultMsg; }

    float measuredRpm()
    {
        if (st != State::Running || speedCount < 2) return 0;
        const Sample& a = speed[0];
        const Sample& b = speed[speedCount - 1];
        return (double)(b.steps - a.steps) / STEPS_PER_REV * 60e6 / (b.us - a.us);
    }

    uint32_t windingMs()
    {
        return windingDone + (st == State::Running ? millis() - segStartMs : 0);
    }

    Event update()
    {
        if (st == State::Running)
        {
            sampleSpeed(false);
            if (!motor.moving())
            {
                windingDone += millis() - segStartMs;
                if (faultMsg) endRun(Outcome::Faulted);
                else if (steps() >= target) endRun(Outcome::Completed);
                else
                {
                    st      = State::Paused;
                    pending = Event::Paused;
                }
            }
        }
        Event e = pending;
        pending = Event::None;
        return e;
    }

    Outcome lastOutcome()
    {
        return outcome;
    }

    PulseCheck pulseCheck(int16_t* offsetSteps)
    {
        // The driver's MSCNT must have moved by exactly the pulses we sent
        // (mod 1024). DIR polarity vs. MSCNT direction is not known, so both
        // signs are accepted; for whole turns the expected value is 0 anyway.
        if (offsetSteps) *offsetSteps = 0;
        if (outcome == Outcome::Faulted) return PulseCheck::Skipped;
        if (!mscntValid) return PulseCheck::Unavailable;
        return checkMscnt(mscntStart, mscntEnd, steps(), offsetSteps);
    }

    PulseCheck checkMscnt(uint16_t before, uint16_t after, uint32_t stepsSent, int16_t* offsetSteps)
    {
        if (offsetSteps) *offsetSteps = 0;
        uint16_t moved = (after - before) & 0x3FF;
        uint16_t fwd   = (stepsSent * MSCNT_PER_STEP) & 0x3FF;
        uint16_t rev   = (0x400 - fwd) & 0x3FF;
        if (moved == fwd || moved == rev) return PulseCheck::Ok;

        int16_t offF = (int16_t)(((moved - fwd + 512) & 0x3FF) - 512);
        int16_t offR = (int16_t)(((moved - rev + 512) & 0x3FF) - 512);
        if (offsetSteps) *offsetSteps = (abs(offF) < abs(offR) ? offF : offR) / (int16_t)MSCNT_PER_STEP;
        return PulseCheck::Mismatch;
    }

    void printSummary()
    {
        uint32_t n    = steps();
        uint32_t full = n / STEPS_PER_REV;
        uint32_t rem  = n % STEPS_PER_REV;

        Serial.println("----------------------------------------");
        switch (outcome)
        {
            case Outcome::Completed: Serial.printf("RUN COMPLETE: %s\n", n == target ? "OK" : "MISMATCH"); break;
            case Outcome::EndedEarly: Serial.println("RUN ENDED EARLY by user"); break;
            case Outcome::Faulted:
                Serial.printf("RUN ABORTED: %s — count unreliable\n", faultMsg ? faultMsg : "fault");
                break;
            case Outcome::None: Serial.println("no run yet"); return;
        }
        Serial.printf("  turns  %lu + %lu/%lu  (%.3f)  target %lu\n", full, rem, STEPS_PER_REV,
                      (double)n / STEPS_PER_REV, target / STEPS_PER_REV);
        Serial.printf("  steps  %lu  target %lu\n", n, target);

        int16_t off;
        switch (pulseCheck(&off))
        {
            case PulseCheck::Ok: Serial.println("  driver pulse check: OK (driver received every step)"); break;
            case PulseCheck::Mismatch:
                Serial.printf("  driver pulse check: MISMATCH — driver saw %+d step(s) vs. sent (mod 64)\n", off);
                break;
            case PulseCheck::Unavailable: Serial.println("  driver pulse check: unavailable (UART read failed)"); break;
            case PulseCheck::Skipped: Serial.println("  driver pulse check: skipped (fault)"); break;
        }
#ifdef STEP_LOOPBACK_PIN
        uint32_t lb = loopbackCount;
        Serial.printf("  loopback pulses %lu  %s\n", lb, lb == n ? "(match)" : "(MISMATCH)");
#endif
        Serial.printf("  winding time  %.1f s\n", windingDone / 1000.0);
        Serial.println("----------------------------------------");
    }

    void enableDriver(bool on)
    {
        enabled = on;
        digitalWriteFast(PIN_EN, on ? LOW : HIGH);
    }

    bool driverEnabled()
    {
        return enabled;
    }
}
