#include "ui/screens/keyboard.h"
#include "ui/ui.h"

namespace ui::screens
{
    Keyboard keyboard;

    namespace
    {
        // Entry line under the status bar, then five rows of keys
        constexpr int16_t ENTRY_X = 4, ENTRY_Y = 28, ENTRY_W = 312, ENTRY_H = 32;
        constexpr int16_t ROW_Y = 66, ROW_DY = 34, PAD_H = 32;
        constexpr int16_t PAD_X = 5, PAD_DX = 31, PAD_W = 29;
        constexpr int16_t SHIFT_W = 45; // row 4: SHIFT, Z..M, DEL

        // The QWERTY letter rows (keys 1..3; row 2 ends with '-')
        constexpr const char* ROWS[] = {"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};

        const char* const DIGIT_LABELS[] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
        const char* const UPPER[] = {"A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
                                     "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z"};
        const char* const LOWER[] = {"a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m",
                                     "n", "o", "p", "q", "r", "s", "t", "u", "v", "w", "x", "y", "z"};

        constexpr Button key(int16_t x, uint8_t row, int16_t w, const char* label)
        {
            return Button{x, (int16_t)(ROW_Y + row * ROW_DY), w, PAD_H, label};
        }
    }

    Keyboard::Keyboard() : group(buttons, K_COUNT)
    {
        // Row 0: 1..9, 0
        for (uint8_t i = 0; i < DIGITS; i++)
            buttons[K_DIGIT0 + (i + 1) % DIGITS] = key(PAD_X + i * PAD_DX, 0, PAD_W, DIGIT_LABELS[(i + 1) % DIGITS]);
        // Rows 1..3: letters (row 3 after SHIFT)
        for (uint8_t r = 0; r < 3; r++)
        {
            int16_t x0 = r == 2 ? PAD_X + SHIFT_W + 2 : PAD_X;
            for (uint8_t i = 0; ROWS[r][i]; i++)
            {
                uint8_t l                = ROWS[r][i] - 'A';
                buttons[K_LETTER_A + l] = key(x0 + i * PAD_DX, r + 1, PAD_W, UPPER[l]);
            }
        }
        buttons[K_DASH]   = key(PAD_X + 9 * PAD_DX, 2, PAD_W, "-");
        buttons[K_SHIFT]  = key(PAD_X, 3, SHIFT_W, "SHIFT");
        int16_t delX      = PAD_X + SHIFT_W + 2 + 7 * PAD_DX;
        buttons[K_DEL]    = key(delX, 3, PAD_X + 9 * PAD_DX + PAD_W - delX, "DEL");
        buttons[K_CANCEL] = key(PAD_X, 4, 80, "CANCEL");
        buttons[K_SPACE]  = key(PAD_X + 84, 4, 140, "SPACE");
        buttons[K_OK]     = key(PAD_X + 228, 4, 80, "OK");
        buttons[K_OK].color = theme::GO;
    }

    void Keyboard::open(const char* t, const char* initial, uint8_t len, Apply fn, Screen& from)
    {
        title  = t;
        maxLen = len < MAX_TEXT ? len : MAX_TEXT;
        apply  = fn;
        back   = &from;
        strlcpy(text, initial, (size_t)maxLen + 1);
        error = nullptr;
        show(*this);
    }

    void Keyboard::enter()
    {
        setShift(!text[0]);
        drawStatus();
        drawEntry();
        group.reset();
        group.drawAll();
    }

    // Title left; the length, or the error in red, right
    void Keyboard::drawStatus()
    {
        char count[24];
        auto& d = tft();
        d.fillRect(0, 0, theme::W, theme::STATUS_H, theme::STATUS_BG);
        printAt(title, 8, 7, theme::FONT_TITLE, theme::FG);
        snprintf(count, sizeof(count), "%u/%u", (unsigned)strlen(text), maxLen);
        printRight(error ? error : count, theme::W - 8, 7, error ? theme::FONT_SMALL : theme::FONT_TITLE,
                   error ? theme::ERR : theme::MUTED);
    }

    void Keyboard::drawEntry()
    {
        char shown[MAX_TEXT + 2];
        snprintf(shown, sizeof(shown), "%s_", text);
        auto& d = tft();
        d.fillRoundRect(ENTRY_X, ENTRY_Y, ENTRY_W, ENTRY_H, 8, theme::FIELD);
        d.drawRoundRect(ENTRY_X, ENTRY_Y, ENTRY_W, ENTRY_H, 8, theme::ACCENT);
        printAt(shown, ENTRY_X + 10, ENTRY_Y + (ENTRY_H - theme::FONT_VALUE.cap_height) / 2, theme::FONT_VALUE,
                theme::FG);
    }

    // The letter keys show the case they type
    void Keyboard::setShift(bool on)
    {
        shift = on;
        for (uint8_t l = 0; l < LETTERS; l++) buttons[K_LETTER_A + l].label = on ? UPPER[l] : LOWER[l];
        buttons[K_SHIFT].color = on ? theme::BUTTON_HI : theme::BUTTON;
    }

    void Keyboard::type(char c)
    {
        size_t len = strlen(text);
        if (len >= maxLen) return;
        text[len]     = c;
        text[len + 1] = '\0';
    }

    void Keyboard::onTouch(const touch::Event& e)
    {
        int8_t k = group.handle(e);
        if (k < 0) return;

        bool wasShift = shift;
        if (k == K_CANCEL)
        {
            show(*back);
            return;
        }
        if (k == K_OK)
        {
            error = apply(text);
            if (!error) show(*back);
            else drawStatus();
            return;
        }
        if (k < DIGITS) type('0' + k);
        else if (k < K_DASH)
        {
            type((shift ? 'A' : 'a') + (k - K_LETTER_A));
            setShift(false);
        }
        else if (k == K_DASH) type('-');
        else if (k == K_SPACE) type(' ');
        else if (k == K_SHIFT) setShift(!shift);
        else if (k == K_DEL)
        {
            size_t len = strlen(text);
            if (len) text[len - 1] = '\0';
            if (!text[0]) setShift(true); // a new first letter is a capital again
        }

        error = nullptr;
        drawStatus();
        drawEntry();
        if (shift != wasShift)
            for (uint8_t i = K_LETTER_A; i <= K_SHIFT; i++) buttons[i].draw();
    }
}
