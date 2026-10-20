#pragma once

#include <Arduino.h>

namespace ui
{
    class Screen;

    // Starts the display and touch. Shows touch calibration on first boot
    // (or when the screen is held for 3 s during boot), else the test screen.
    void begin();

    // Call every loop pass: touch sampling, events, screen updates.
    void update();

    void show(Screen& s);
    void startCalibration();

    // Main-loop timing for the test screen: report each pass's duration.
    void noteLoopTime(uint32_t us);
    uint32_t loopMaxUs(); // worst pass over the last second
}
