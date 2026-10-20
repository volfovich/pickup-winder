#pragma once

#include <Arduino.h>
#include "winder.h"

// Application layer shared by the serial console and the touch UI: owns the
// settings (persisted in EEPROM), the actions, and the safety jobs that run
// every loop pass (driver fault watch, end-of-run handling).
//
// Actions return nullptr on success, else a short reason to show the user.
namespace app
{
    void begin(); // loads settings from EEPROM (defaults if empty/invalid)

    // Call every loop pass. Returns true once when a run has just finished
    // (see winder::lastOutcome() / winder::printSummary()).
    bool update();

    const winder::Settings& settings();

    const char* setTurns(uint32_t turns);
    const char* setRpm(uint32_t rpm);
    const char* setAccel(uint32_t accelRpmPerS);
    const char* setDir(winder::Dir dir);

    const char* start();
    void stop();  // ramped stop, count stays exact
    void estop(); // stop at the next step edge

    const char* release();      // disable the driver (motor free)
    const char* reinitDriver(); // re-run TMC2209 configuration
}
