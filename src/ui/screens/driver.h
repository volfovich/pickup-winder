#pragma once

#include "tmc_driver.h"
#include "ui/screen.h"
#include "ui/widgets.h"

namespace ui::screens
{
    // Live TMC2209 configuration and status (the same data as serial `drv`)
    // and RE-INIT. Reads one register per loop pass, so the UART reads
    // (~5 ms each) never stall the loop for long.
    class Driver : public Screen
    {
     public:
        Driver();
        void enter() override;
        void update() override;
        void onTouch(const touch::Event& e) override;

     private:
        void drawStatus(bool comm);
        void drawLine(uint8_t i, const char* text, uint16_t color);

        enum : uint8_t { BTN_REINIT, BTN_BACK, BTN_COUNT };
        Button buttons[BTN_COUNT];
        ButtonGroup group;

        static constexpr uint8_t LINES = 9;
        char shown[LINES][48];
        uint16_t shownColor[LINES];

        tmc::Status reading{};
        uint8_t part          = 0;
        uint32_t cycleStartMs = 0;
        bool refreshNow       = true;
    };

    extern Driver driver;
}
