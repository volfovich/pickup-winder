#pragma once

#include "ui/screen.h"

namespace ui::screens
{
    // Touch calibration: tap three crosses to solve the mapping, then a
    // fourth to check it. Saved to EEPROM only when the check passes.
    class Calibrate : public Screen
    {
     public:
        void enter() override;
        void update() override;
        void onTouch(const touch::Event& e) override;

     private:
        void drawStep();
        void restart(const char* why);

        uint8_t step       = 0;
        bool waitRelease   = false;
        uint32_t doneMs    = 0;
        touch::Raw raw[3];
    };

    extern Calibrate calibrate;
}
