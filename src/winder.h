#pragma once

#include <Arduino.h>

namespace winder
{
    enum class Dir : int8_t { CW = 1, CCW = -1 };

    // A run: start → Running ⇄ Paused (stop / resume) → Done.
    enum class State : uint8_t {
        Idle,    // no run yet
        Running, // motor moving
        Paused,  // stopped with turns left: resume() or finish()
        Done,    // run over, see lastOutcome()
    };

    enum class Outcome : uint8_t {
        None,       // no run finished yet
        Completed,  // reached the target turn count
        EndedEarly, // paused, then finished by the user
        Faulted,    // driver fault — count unreliable
    };

    enum class Event : uint8_t { None, Paused, Ended };

    enum class PulseCheck : uint8_t {
        Ok,          // the driver received every step sent
        Mismatch,    // it didn't (see offset)
        Unavailable, // UART read failed
        Skipped,     // run faulted
    };

    struct Settings
    {
        uint32_t turns;
        uint32_t rpm;
        uint32_t accelRpmPerS;
        Dir dir;

        bool operator==(const Settings& o) const
        {
            return turns == o.turns && rpm == o.rpm && accelRpmPerS == o.accelRpmPerS && dir == o.dir;
        }
        bool operator!=(const Settings& o) const { return !(*this == o); }
    };

    void begin();

    // Validate a setting: nullptr if OK, else a short reason (also covers
    // what TeensyStep4's timer can't produce).
    const char* checkTurns(uint32_t turns);
    const char* checkRpm(uint32_t rpm);
    const char* checkAccel(uint32_t accelRpmPerS);

    // The cruise speed the step timer really produces for a set rpm. The
    // timer has a 0.213 us resolution, so e.g. 1200 rpm runs at 1204.0 rpm.
    float actualRpm(uint32_t rpm);

    // Run control. Each returns nullptr on success, else the reason.
    const char* start(const Settings& s); // settings are fixed for the whole run
    const char* resume();                 // Paused → Running: wind the remaining turns
    const char* finish();                 // Paused → Done (ended early)

    void stop();  // ramped stop → Paused (or Done if it lands on the target)
    void estop(); // stop at the next step edge → Paused; the count stays exact
    void abortOnFault(const char* reason);

    State state();
    bool isMoving(); // steps are being generated
    bool inRun();    // Running or Paused: settings are locked
    bool stopping(); // a ramped stop was requested and the motor is still moving
    bool resumed();  // the current segment continues a paused run

    uint32_t stepsDone();   // steps of the current/last run, across pauses
    uint32_t targetSteps(); // steps requested for the current/last run
    float turnsDone();
    const Settings& runSettings();
    float measuredRpm();    // over the last second while moving, else 0
    uint32_t windingMs();   // time spent moving in this run (pauses excluded)
    const char* faultReason();

    // Call from loop(). Returns Paused / Ended once when that happens.
    Event update();
    Outcome lastOutcome();
    PulseCheck pulseCheck(int16_t* offsetSteps); // for the last ended run
    void printSummary();

    void enableDriver(bool on);
    bool driverEnabled();
}
