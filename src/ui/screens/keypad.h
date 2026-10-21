#pragma once

#include "ui/screen.h"
#include "ui/widgets.h"

namespace ui::screens
{
    // Numeric entry for one setting. OK passes the value to `apply` (an app::
    // setter); its error text is shown and the keypad stays open, else the
    // keypad returns to the screen it was opened from.
    class Keypad : public Screen
    {
     public:
        using Apply = const char* (*)(uint32_t);

        Keypad();
        void open(const char* title, uint32_t minValue, uint32_t maxValue, uint32_t current, Apply apply, Screen& back);
        // Same for a length in mm with up to `decimals` (1..3) decimals;
        // values and `apply` are in um.
        void openMm(const char* title, uint32_t minUm, uint32_t maxUm, uint32_t currentUm, Apply apply, Screen& back,
                    uint8_t decimals = 3);

        void enter() override;
        void onTouch(const touch::Event& e) override;

     private:
        void drawEntry();
        void drawError();
        void type(int8_t key);
        void openAs(bool mm, const char* title, uint32_t minValue, uint32_t maxValue, uint32_t current, Apply apply,
                    Screen& back);
        uint32_t value() const; // the entry, in um in mm mode

        // 0..9 are the digit keys
        enum : uint8_t { K_DOT = 10, K_DEL, K_CANCEL, K_OK, K_COUNT };
        Button buttons[K_COUNT];
        ButtonGroup group;

        const char* title   = "";
        uint32_t minValue   = 0;
        uint32_t maxValue   = 0;
        Apply apply         = nullptr;
        Screen* back        = nullptr;
        char text[16]       = "";
        bool fresh          = true; // showing the current value; first digit replaces it
        bool mm             = false;
        uint8_t decimals    = 3; // mm mode
        const char* error   = nullptr;
    };

    extern Keypad keypad;
}
