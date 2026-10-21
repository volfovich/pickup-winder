#pragma once

#include "app.h"
#include "ui/screen.h"
#include "ui/widgets.h"

namespace ui::screens
{
    // Scatter winding, from Bobbin → SCATTER: on / off, the pitch scatter
    // (each layer's pitch is wire x 1 .. 1 + %), the end scatter (turnarounds
    // up to this far inside the margins, keypad in mm) and the seed (keypad,
    // or NEW SEED for a random one), with the resulting turns per layer and
    // layers. Locked while a run is in progress.
    class Scatter : public Screen
    {
     public:
        Scatter();
        void enter() override;
        void update() override;
        void onTouch(const touch::Event& e) override;

     private:
        void load(); // settings + lock state -> button values
        void drawSummary();
        void redraw();

        enum : uint8_t { F_ON, F_PITCH, F_ENDS, F_SEED, BTN_BACK, BTN_NEW, BTN_COUNT };
        static constexpr uint8_t FIELDS = 4;
        Button buttons[BTN_COUNT];
        ButtonGroup group;
        char values[FIELDS][16];
        app::TraverseSettings shown{};
        uint32_t shownTurns  = 0;
        uint32_t shownRpm    = 0;
        bool shownLocked     = false;
        uint32_t lastCheckMs = 0;
    };

    extern Scatter scatter;
}
