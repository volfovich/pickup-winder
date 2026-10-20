#pragma once

#include <Arduino.h>

namespace ui::touch
{
    struct Point
    {
        int16_t x, y;
    };

    struct Raw
    {
        int16_t x, y, z;
    };

    enum class Type : uint8_t { Down, Up, LongPress };

    struct Event
    {
        Type type;
        Point pos; // calibrated screen position
        Raw raw;   // Up: average of the firm samples of the whole press
    };

    void begin();

    // Call every loop pass; samples the panel at ~100 Hz.
    void update();

    // Takes the next event from a small queue. Returns false if none.
    bool pollEvent(Event* e);

    bool isPressed();
    Point position();   // current position while pressed
    Raw lastRaw();      // latest raw sample (for the test screen)
    bool touchedNow();  // un-debounced panel state (boot hold detection)

    bool calibrated();
    // Solves the calibration from three raw readings taken at three screen
    // points and uses it right away (not saved). Returns false if the
    // readings were unusable.
    bool solveCalibration(const Raw raw[3], const Point screen[3]);
    void saveCalibration();   // writes the active calibration to EEPROM
    void reloadCalibration(); // drops an unsaved calibration
    Point map(int16_t rawX, int16_t rawY);
}
