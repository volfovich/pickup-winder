#include "ui/screens/keypad.h"
#include "ui/ui.h"

namespace ui::screens
{
    Keypad keypad;

    namespace
    {
        constexpr uint8_t MAX_DIGITS = 6;

        // "12.345" for um, with 1..3 decimals (rounded)
        void formatMm(uint32_t um, uint8_t decimals, char* buf, size_t size)
        {
            uint32_t unit = decimals == 1 ? 100 : decimals == 2 ? 10 : 1;
            uint32_t v    = (um + unit / 2) / unit;
            uint32_t div  = 1000 / unit;
            const char* fmt = decimals == 1 ? "%lu.%01lu" : decimals == 2 ? "%lu.%02lu" : "%lu.%03lu";
            snprintf(buf, size, fmt, v / div, v % div);
        }

        // Left panel: entry box, error text, CANCEL / OK. Right: 3x4 keys.
        constexpr int16_t ENTRY_X = 4, ENTRY_Y = 32, ENTRY_W = 144, ENTRY_H = 46;
        constexpr int16_t ERROR_Y = 84, ERROR_H = 48;
        constexpr int16_t PAD_X = 152, PAD_Y = 30, PAD_W = 52, PAD_H = 50, PAD_GAP = 4, PAD_DY = 52;

        constexpr Button key(uint8_t col, uint8_t row, const char* label)
        {
            return Button{(int16_t)(PAD_X + col * (PAD_W + PAD_GAP)), (int16_t)(PAD_Y + row * PAD_DY), PAD_W, PAD_H, label};
        }
    }

    Keypad::Keypad()
        : buttons{
              key(1, 3, "0"), key(0, 2, "1"), key(1, 2, "2"), key(2, 2, "3"), key(0, 1, "4"),
              key(1, 1, "5"), key(2, 1, "6"), key(0, 0, "7"), key(1, 0, "8"), key(2, 0, "9"),
              key(0, 3, "."), key(2, 3, "DEL"),
              {4, 136, 144, 46, "CANCEL"},
              {4, 188, 144, 48, "OK", theme::GO},
          },
          group(buttons, K_COUNT)
    {
    }

    void Keypad::open(const char* t, uint32_t minV, uint32_t maxV, uint32_t current, Apply fn, Screen& from)
    {
        openAs(false, t, minV, maxV, current, fn, from);
    }

    void Keypad::openMm(const char* t, uint32_t minUm, uint32_t maxUm, uint32_t currentUm, Apply fn, Screen& from,
                        uint8_t dec)
    {
        decimals = dec < 1 ? 1 : dec > 3 ? 3 : dec;
        openAs(true, t, minUm, maxUm, currentUm, fn, from);
    }

    void Keypad::openAs(bool asMm, const char* t, uint32_t minV, uint32_t maxV, uint32_t current, Apply fn,
                        Screen& from)
    {
        title    = t;
        minValue = minV;
        maxValue = maxV;
        apply    = fn;
        back     = &from;
        mm       = asMm;
        if (mm) formatMm(current, decimals, text, sizeof(text));
        else snprintf(text, sizeof(text), "%lu", current);
        fresh = true;
        error = nullptr;
        show(*this);
    }

    uint32_t Keypad::value() const
    {
        if (!mm) return strtoul(text, nullptr, 10);
        uint32_t um = 0, scale = 1000;
        const char* p = text;
        for (; isdigit(*p); p++) um = um * 10 + (*p - '0');
        um *= 1000;
        if (*p == '.')
            for (p++; isdigit(*p) && scale > 1; p++) um += (*p - '0') * (scale /= 10);
        return um;
    }

    void Keypad::enter()
    {
        static char range[48];
        auto& d = tft();
        d.fillRect(0, 0, theme::W, theme::STATUS_H, theme::STATUS_BG);
        printAt(title, 8, 7, theme::FONT_TITLE, theme::FG);
        if (mm)
        {
            char a[16], b[16];
            formatMm(minValue, decimals, a, sizeof(a));
            formatMm(maxValue, decimals, b, sizeof(b));
            snprintf(range, sizeof(range), "%s .. %s mm", a, b);
        }
        else snprintf(range, sizeof(range), "%lu .. %lu", minValue, maxValue);
        printRight(range, theme::W - 8, 7, theme::FONT_TITLE, theme::MUTED);

        buttons[K_DOT].enabled = mm;
        drawEntry();
        drawError();
        group.reset();
        group.drawAll();
    }

    void Keypad::drawEntry()
    {
        auto& d = tft();
        d.fillRoundRect(ENTRY_X, ENTRY_Y, ENTRY_W, ENTRY_H, 8, theme::FIELD);
        d.drawRoundRect(ENTRY_X, ENTRY_Y, ENTRY_W, ENTRY_H, 8, theme::ACCENT);
        printRight(text, ENTRY_X + ENTRY_W - 12, ENTRY_Y + (ENTRY_H - theme::FONT_ENTRY.cap_height) / 2, theme::FONT_ENTRY,
                   fresh ? theme::MUTED : theme::FG);
    }

    void Keypad::drawError()
    {
        tft().fillRect(ENTRY_X, ERROR_Y, ENTRY_W, ERROR_H, theme::BG);
        if (error) printWrapped(error, ENTRY_X + 2, ERROR_Y, ENTRY_W - 4, theme::FONT_SMALL.line_space,
                                theme::FONT_SMALL, theme::ERR);
    }

    // Edits the entry text: digits, one decimal point (mm mode) with up to
    // `decimals` after it, DEL. No leading zeros.
    void Keypad::type(int8_t k)
    {
        if (fresh) text[0] = '\0';
        fresh            = false;
        size_t len       = strlen(text);
        const char* dot  = strchr(text, '.');
        size_t digits    = len - (dot ? 1 : 0);
        if (k == K_DEL)
        {
            if (len) text[len - 1] = '\0';
        }
        else if (k == K_DOT)
        {
            if (dot || len + 2 >= sizeof(text)) return;
            if (!len) text[len++] = '0'; // ".5" -> "0.5"
            text[len]     = '.';
            text[len + 1] = '\0';
        }
        else if (dot && (size_t)(text + len - dot - 1) >= decimals)
        {
            return;
        }
        else if (len == 1 && text[0] == '0') // no leading zeros
        {
            text[0] = '0' + k;
        }
        else if (digits < MAX_DIGITS && len + 1 < sizeof(text))
        {
            text[len]     = '0' + k;
            text[len + 1] = '\0';
        }
    }

    void Keypad::onTouch(const touch::Event& e)
    {
        int8_t k = group.handle(e);
        if (k < 0) return;

        if (k <= 9 || k == K_DEL || k == K_DOT)
        {
            type(k);
            if (error)
            {
                error = nullptr;
                drawError();
            }
            drawEntry();
        }
        else if (k == K_CANCEL)
        {
            show(*back);
        }
        else if (k == K_OK)
        {
            error = text[0] ? apply(value()) : "enter a value";
            if (error) drawError();
            else show(*back);
        }
    }
}
