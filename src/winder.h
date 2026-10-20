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

    // Checks rpm/accel against what TeensyStep4's timer can produce. Prints
    // the reason and returns false if not supported.
    bool rpmSupported(uint32_t rpm);
    bool accelSupported(uint32_t accelRpmPerS);

    // The cruise speed the step timer really produces for a set rpm. The
    // timer has a 0.213 us resolution, so e.g. 1200 rpm runs at 1204.0 rpm.
    float actualRpm(uint32_t rpm);

    // Starts a run. All parameters are fixed for the duration of the run.
    // Prints the reason and returns false if the run can't start.
    bool start(const Settings& s);

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
