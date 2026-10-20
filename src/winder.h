#pragma once

#include <Arduino.h>

namespace winder
{
    enum class Dir : int8_t { CW = 1, CCW = -1 };

    enum class Outcome : uint8_t {
        None,      // no run finished yet
        Completed, // reached the target turn count
        Stopped,   // user ramped stop
        EStopped,  // user emergency stop
        Faulted,   // driver fault — count unreliable
    };

    struct Settings
    {
        uint32_t turns;
        uint32_t rpm;
        uint32_t accelRpmPerS;
        Dir dir;
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

    // Starts a run. All parameters are fixed for the duration of the run.
    // Returns nullptr on success, else the reason the run can't start.
    const char* start(const Settings& s);

    void stop();                    // ramped stop, count stays exact
    void estop();                   // stop at the next step edge, no ramp
    void abortOnFault(const char* reason);

    bool isRunning();
    uint32_t stepsDone();           // steps taken in the current/last run
    uint32_t targetSteps();         // steps requested for the current/last run
    float turnsDone();

    // Call from loop(). Returns true once when a run has just finished.
    bool update();
    Outcome lastOutcome();
    void printSummary();

    void enableDriver(bool on);
    bool driverEnabled();
}
