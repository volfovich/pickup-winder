#pragma once

#include "ui/screen.h"
#include "ui/widgets.h"

namespace ui::screens
{
    // Text entry (preset names): digits, letters, '-' and space, up to
    // maxLen characters. The first letter is a capital, the rest lowercase;
    // SHIFT capitalises the next letter. OK passes the text to `apply`; its
    // error text is shown and the keyboard stays open, else it returns to
    // the screen it was opened from.
    class Keyboard : public Screen
    {
     public:
        using Apply = const char* (*)(const char*);

        Keyboard();
        void open(const char* title, const char* initial, uint8_t maxLen, Apply apply, Screen& back);

        void enter() override;
        void onTouch(const touch::Event& e) override;

     private:
        void drawStatus();
        void drawEntry();
        void setShift(bool on);
        void type(char c);

        static constexpr uint8_t DIGITS = 10, LETTERS = 26;
        // Digits, then letters A..Z, then the other keys
        enum : uint8_t {
            K_DIGIT0 = 0,
            K_LETTER_A = DIGITS,
            K_DASH = DIGITS + LETTERS,
            K_SHIFT, K_DEL, K_CANCEL, K_SPACE, K_OK, K_COUNT
        };
        static constexpr uint8_t MAX_TEXT = 24;
        Button buttons[K_COUNT];
        ButtonGroup group;

        const char* title = "";
        uint8_t maxLen    = 16;
        Apply apply       = nullptr;
        Screen* back      = nullptr;
        char text[MAX_TEXT + 1] = "";
        bool shift        = true;
        const char* error = nullptr;
    };

    extern Keyboard keyboard;
}
