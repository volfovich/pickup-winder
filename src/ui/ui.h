#pragma once

#include <Arduino.h>

namespace ui
{
    class Screen;

    // Starts the display and touch and shows the splash screen, which then
    // leads to touch calibration on first boot (or when the screen is held
    // for 3 s during boot), else to Idle.
    void begin();

    // setup() calls markBooted() when it is done (driver configured); the
    // splash screen waits for it. ui::update() may run before that.
    void markBooted();
    bool booted();

    // Call every loop pass: touch sampling, events, screen updates.
    void update();

    void show(Screen& s);
    void startCalibration();

    // Main-loop timing for the test screen: report each pass's duration.
    void noteLoopTime(uint32_t us);
    uint32_t loopMaxUs(); // worst pass over the last second
}
