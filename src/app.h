#pragma once

#include <Arduino.h>
#include "traverse.h"
#include "winder.h"

// Application layer shared by the serial console and the touch UI: owns the
// settings (persisted in EEPROM), the actions, and the safety jobs that run
// every loop pass (driver fault watch, end-of-run handling).
//
// Actions return nullptr on success, else a short reason to show the user.
namespace app
{
    // Wire guide settings. Off = the spindle winds alone (guide by hand).
    struct TraverseSettings
    {
        bool on;
        uint32_t wireUm;   // wire diameter incl. insulation = the pitch
        uint32_t widthUm;  // winding width between the flanges
        uint32_t offsetUm; // near (home side) edge of the winding, from home
        bool firstAway;    // the first layer moves away from home

        bool operator==(const TraverseSettings& o) const
        {
            return on == o.on && wireUm == o.wireUm && widthUm == o.widthUm && offsetUm == o.offsetUm &&
                   firstAway == o.firstAway;
        }
        bool operator!=(const TraverseSettings& o) const { return !(*this == o); }
    };

    void begin(); // loads settings from EEPROM (defaults if empty/invalid)

    // Call every loop pass. Returns winder::Event::Paused / Ended once when a
    // run pauses or ends (see winder::lastOutcome() / winder::printSummary()).
    winder::Event update();
    // The traverse event of this loop pass (homed, homing failed, move done)
    traverse::Event traverseEvent();

    const winder::Settings& settings();
    const TraverseSettings& traverseSettings();

    const char* setTurns(uint32_t turns);
    const char* setRpm(uint32_t rpm);
    const char* setAccel(uint32_t accelRpmPerS);
    const char* setDir(winder::Dir dir);
    // Spindle run current (RMS), applied to the driver at once
    const char* setCurrent(uint32_t mA);
    uint16_t spindleCurrent();

    const char* setTraverseOn(bool on);
    const char* setWire(uint32_t um);
    const char* setWidth(uint32_t um);
    const char* setOffset(uint32_t um);
    const char* setFirstAway(bool away);
    // Checks that the wire / width / offset fit the slide and the RPM
    const char* checkTraverse();

    // With the traverse on, START first moves the guide to the start
    // position (positioning() is true meanwhile), then starts the spindle.
    // A failure after that is in startError().
    const char* start();
    bool positioning();
    const char* startError();
    const char* resume(); // wind the remaining turns of a paused run
    const char* finish(); // end a paused run early
    void stop();          // ramped stop → paused, count stays exact
    void estop();         // stop at the next step edge → paused

    const char* home();
    const char* jog(int32_t um);
    const char* moveTo(int32_t um);

    // A driver reported its over-temperature pre-warning during this run
    bool driverHot();

    const char* release();      // disable both drivers (motors free)
    const char* reinitDriver(); // re-run TMC2209 configuration (both drivers)

    // The current / last run used the traverse: its pattern and pulse check
    bool traverseRun();
    const traverse_math::Pattern& runPattern();
    winder::PulseCheck traversePulseCheck(int16_t* offsetSteps);
}
