#pragma once

#include "app.h"
#include "ui/screen.h"
#include "ui/widgets.h"

namespace ui::screens
{
    // Wire guide settings: guide on/off, first-layer direction, wire (AWG
    // and insulation), margin at the flanges, inner width (keypad in mm,
    // 0.1 mm steps), and the flanges' carriage positions (taught on the Guide
    // screen; tapping them opens it), with the resulting turns per layer.
    // Locked while a run is in progress.
    class Bobbin : public Screen
    {
     public:
        Bobbin();
        void enter() override;
        void update() override;
        void onTouch(const touch::Event& e) override;

     private:
        void load(); // settings + lock state -> button values
        void drawSummary();

        enum : uint8_t {
            F_ON, F_FIRST, F_AWG, F_INSUL, F_MARGIN, F_WIDTH, F_FLANGES, BTN_BACK, BTN_GUIDE, BTN_COUNT
        };
        static constexpr uint8_t FIELDS = 7;
        Button buttons[BTN_COUNT];
        ButtonGroup group;
        char values[FIELDS][16];
        app::TraverseSettings shown{};
        uint32_t shownRpm    = 0;
        uint32_t shownTurns  = 0;
        bool shownLocked     = false;
        uint32_t lastCheckMs = 0;
    };

    extern Bobbin bobbin;
}
