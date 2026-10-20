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

        uint32_t target        = 0;
        bool running           = false;
        bool stopRequested     = false;
        bool estopRequested    = false;
        const char* faultMsg   = nullptr;
        Outcome outcome        = Outcome::None;
        bool enabled           = false;
        uint32_t runStartMs    = 0;
        uint32_t runEndMs      = 0;

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

    bool rpmSupported(uint32_t rpm)
    {
        if (rpm >= MIN_RPM && rpm <= MAX_RPM) return true;
        Serial.printf("ERR: rpm must be %lu..%lu\n", MIN_RPM, MAX_RPM);
        return false;
    }

    bool accelSupported(uint32_t accelRpmPerS)
    {
        if (accelRpmPerS < MIN_ACCEL_RPM_PER_S || accelRpmPerS > MAX_ACCEL_RPM_PER_S)
        {
            Serial.printf("ERR: accel must be %lu..%lu RPM/s\n", MIN_ACCEL_RPM_PER_S, MAX_ACCEL_RPM_PER_S);
            return false;
        }
        int32_t endVSqr = TS4_START_V_SQR - 2 * (int32_t)rpmToStepsPerS(accelRpmPerS);
        if (abs(endVSqr) < TS4_MIN_V_SQR)
        {
            Serial.println("ERR: accel 327..423 RPM/s is not supported by the step timer — use a value outside that range");
            return false;
        }
        return true;
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

    bool start(const Settings& st)
    {
        if (running || motor.moving())
        {
            Serial.println("ERR: already running");
            return false;
        }
        if (!tmc::ready())
        {
            Serial.println("ERR: driver not configured — fix the TMC2209 connection and run 'init'");
            return false;
        }
        if (st.turns < 1 || st.turns > MAX_TURNS)
        {
            Serial.printf("ERR: turns must be 1..%lu\n", MAX_TURNS);
            return false;
        }
        if (!rpmSupported(st.rpm) || !accelSupported(st.accelRpmPerS)) return false;

        target         = st.turns * STEPS_PER_REV;
        stopRequested  = false;
        estopRequested = false;
        faultMsg       = nullptr;
        outcome        = Outcome::None;

        mscntValid = tmc::readMscnt(&mscntStart);
        motor.setPosition(0); // count each run from zero (also keeps int32 far from overflow)
#ifdef STEP_LOOPBACK_PIN
        loopbackCount = 0;
#endif

        enableDriver(true);
        delay(5); // let the coil current settle before the first step

        motor.setProfile(rpmToStepsPerS(st.rpm), rpmToStepsPerS(st.accelRpmPerS));
        running    = true;
        runStartMs = millis();
        motor.moveRelAsync((int32_t)st.dir * (int32_t)target);
        return true;
    }

    void stop()
    {
        if (!running) return;
        stopRequested = true;
        motor.stopAsync();
    }

    void estop()
    {
        if (!running) return;
        estopRequested = true;
        motor.hardStop();
    }

    void abortOnFault(const char* reason)
    {
        if (!running) return;
        faultMsg = reason;
        motor.hardStop();
    }

    bool isRunning()
    {
        return running;
    }

    uint32_t stepsDone()
    {
        int32_t p = motor.getPosition();
        return (uint32_t)(p < 0 ? -p : p);
    }

    uint32_t targetSteps()
    {
        return target;
    }

    float turnsDone()
    {
        return (float)stepsDone() / STEPS_PER_REV;
    }

    bool update()
    {
        if (!running || motor.moving()) return false;

        running  = false;
        runEndMs = millis();
        if (faultMsg) outcome = Outcome::Faulted;
        else if (estopRequested) outcome = Outcome::EStopped;
        else if (stopRequested && stepsDone() != target) outcome = Outcome::Stopped;
        else outcome = Outcome::Completed;

        if (mscntValid) mscntValid = tmc::readMscnt(&mscntEnd);
        return true;
    }

    Outcome lastOutcome()
    {
        return outcome;
    }

    void printSummary()
    {
        uint32_t steps = stepsDone();
        uint32_t full  = steps / STEPS_PER_REV;
        uint32_t rem   = steps % STEPS_PER_REV;

        Serial.println("----------------------------------------");
        switch (outcome)
        {
            case Outcome::Completed:
                Serial.printf("RUN COMPLETE: %s\n", steps == target ? "OK" : "MISMATCH");
                break;
            case Outcome::Stopped: Serial.println("RUN STOPPED by user (ramped)"); break;
            case Outcome::EStopped: Serial.println("RUN E-STOPPED by user"); break;
            case Outcome::Faulted:
                Serial.printf("RUN ABORTED: %s — count unreliable\n", faultMsg ? faultMsg : "fault");
                break;
            case Outcome::None: Serial.println("no run yet"); return;
        }
        Serial.printf("  turns  %lu + %lu/%lu  (%.3f)  target %lu\n",
                      full, rem, STEPS_PER_REV, (double)steps / STEPS_PER_REV, target / STEPS_PER_REV);
        Serial.printf("  steps  %lu  target %lu\n", steps, target);

        // The driver's MSCNT must have moved by exactly the pulses we sent
        // (mod 1024). DIR polarity vs. MSCNT direction is not known, so both
        // signs are accepted; for whole turns the expected value is 0 anyway.
        if (outcome == Outcome::Faulted)
            Serial.println("  driver pulse check: skipped (fault)");
        else if (!mscntValid)
            Serial.println("  driver pulse check: unavailable (UART read failed)");
        else
        {
            uint16_t moved = (mscntEnd - mscntStart) & 0x3FF;
            uint16_t fwd   = (steps * MSCNT_PER_STEP) & 0x3FF;
            uint16_t rev   = (0x400 - fwd) & 0x3FF;
            if (moved == fwd || moved == rev)
                Serial.println("  driver pulse check: OK (driver received every step)");
            else
            {
                int16_t offF = (int16_t)(((moved - fwd + 512) & 0x3FF) - 512);
                int16_t offR = (int16_t)(((moved - rev + 512) & 0x3FF) - 512);
                int16_t off  = abs(offF) < abs(offR) ? offF : offR;
                Serial.printf("  driver pulse check: MISMATCH — driver saw %+d step(s) vs. sent (mod 64)\n",
                              off / (int16_t)MSCNT_PER_STEP);
            }
        }
#ifdef STEP_LOOPBACK_PIN
        uint32_t lb = loopbackCount;
        Serial.printf("  loopback pulses %lu  %s\n", lb, lb == steps ? "(match)" : "(MISMATCH)");
#endif
        Serial.printf("  time   %.1f s\n", (runEndMs - runStartMs) / 1000.0);
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
