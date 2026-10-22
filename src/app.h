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
        uint8_t awg;       // 42 / 43 / 44 (WIRE_SIZES in config.h)
        bool heavy;        // heavy-build insulation
        uint32_t widthUm;  // bobbin inner width, flange to flange
        uint32_t marginUm; // kept clear inside both flanges
        uint32_t flangeUm; // flange 1 (nearer the rail's home end): carriage position, from home,
                           // where the wire meets its inner face. Flange 2 = flange 1 + width.
        bool firstAway;    // the first layer moves away from home
        // Scatter winding (traverse_math::makeScatter): each layer's pitch is
        // wire x 1 .. 1 + pitch %, its turnarounds up to endScatterUm inside
        // the margins; the same seed winds the same pattern again
        bool scatter;
        uint8_t pitchScatterPct;
        uint32_t endScatterUm;
        uint32_t seed;

        bool operator==(const TraverseSettings& o) const
        {
            return on == o.on && awg == o.awg && heavy == o.heavy && widthUm == o.widthUm &&
                   marginUm == o.marginUm && flangeUm == o.flangeUm && firstAway == o.firstAway &&
                   scatter == o.scatter && pitchScatterPct == o.pitchScatterPct && endScatterUm == o.endScatterUm &&
                   seed == o.seed;
        }
        bool operator!=(const TraverseSettings& o) const { return !(*this == o); }
    };

    // What the guide winds with the current settings
    struct Winding
    {
        uint32_t pitchUm;       // wire diameter incl. insulation
        uint32_t startUm;       // near edge of the winding, from home (flange + margin)
        uint32_t spanUm;        // turnsPerLayer x pitch; scatter: margin to margin
        uint32_t turnsPerLayer; // 0: no room between the margins; scatter: the fewest in a layer
        uint32_t maxTurnsPerLayer;
        uint32_t maxPitchUm;    // the guide's fastest travel per turn
        uint32_t layers;        // for the set turns
    };

    // A named set of settings for one kind of wind: everything in Setup,
    // Bobbin and Scatter, the taught flanges included. Not the spindle
    // current or the touch calibration (they belong to the machine).
    constexpr uint8_t PRESET_SLOTS   = 10;
    constexpr size_t PRESET_NAME_MAX = 16;
    struct Preset
    {
        char name[PRESET_NAME_MAX + 1];
        winder::Settings s;
        TraverseSettings t;
    };

    void begin(); // loads settings and presets from EEPROM (defaults if empty/invalid)

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
    const char* setAwg(uint32_t awg);
    const char* setHeavy(bool heavy);
    const char* setWidth(uint32_t um);  // lengths round to 0.1 mm
    const char* setMargin(uint32_t um);
    // The flanges are taught on the Guide screen: the carriage position where
    // the wire meets each flange's inner face. Setting flange 1 keeps flange 2
    // where it is; setting flange 2 sets the inner width.
    const char* setFlange1(uint32_t um);
    const char* setFlange2(uint32_t um);
    const char* setFlange1Here(); // the guide's current position (needs homing)
    const char* setFlange2Here();
    const char* setFirstAway(bool away);
    const char* setScatter(bool on);
    const char* setPitchScatter(uint32_t pct);
    const char* setEndScatter(uint32_t um); // rounds to 0.1 mm
    const char* setSeed(uint32_t seed);
    const char* newSeed(); // a random one

    // Presets (EEPROM). Slots are 0..PRESET_SLOTS-1; names 1..16 printable
    // characters. Loading replaces the current settings (and saves them).
    const Preset* preset(uint8_t slot); // nullptr: empty
    const char* savePreset(uint8_t slot, const char* name);
    const char* loadPreset(uint8_t slot);
    const char* deletePreset(uint8_t slot);
    int8_t activePreset(); // slot last loaded or saved, -1: none (kept across reboots)
    bool presetModified(); // the current settings differ from the active preset
    Winding winding();
    // Checks that the winding fits the bobbin, the slide and the RPM
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
    winder::PulseCheck traversePulseCheck(int16_t* offsetSteps); // Pending just after the run
    bool traversePulseChecked();                                 // the pending check finished this pass
    void traverseMscnt(uint16_t* start, uint16_t* end, int32_t* netSteps); // raw values behind the check
}
