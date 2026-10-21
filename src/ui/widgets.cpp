#include "ui/widgets.h"
#include "app.h"
#include "tmc_driver.h"
#include "winder.h"

namespace ui
{
    namespace
    {
        const char* statusTitle = "";

        enum class MachineState : uint8_t { Unknown, NotReady, Ready, ReadyHolding, Winding, WindingHot, Paused };
        MachineState shownState = MachineState::Unknown;

        // Font sizes for fitting text, largest first
        const ILI9341_t3_font_t* const LADDER[] = {&Arial_28_Bold, &Arial_24_Bold, &Arial_16_Bold,
                                                   &Arial_14_Bold, &Arial_12,      &Arial_11};

        MachineState machineState()
        {
            if (winder::isMoving()) return app::driverHot() ? MachineState::WindingHot : MachineState::Winding;
            if (winder::state() == winder::State::Paused) return MachineState::Paused;
            if (!tmc::spindle.ready()) return MachineState::NotReady;
            // Outside a run the spindle is free, except in the minutes after one
            return winder::driverEnabled() ? MachineState::ReadyHolding : MachineState::Ready;
        }
    }

    void Button::draw(bool highlighted) const
    {
        auto& d        = tft();
        uint16_t fill  = !enabled ? theme::BUTTON_OFF : highlighted ? theme::BUTTON_HI : value ? theme::FIELD : color;
        uint16_t text  = enabled ? theme::FG : theme::TEXT_OFF;
        d.fillRoundRect(x, y, w, h, 8, fill);
        d.drawRoundRect(x, y, w, h, 8, enabled ? theme::MUTED : theme::BUTTON);
        if (value)
        {
            int16_t ty = y + (h - theme::FONT_LABEL.cap_height) / 2;
            printAt(label, x + 12, ty, theme::FONT_LABEL, enabled ? theme::MUTED : theme::TEXT_OFF);
            printRight(value, x + w - 12, y + (h - theme::FONT_VALUE.cap_height) / 2, theme::FONT_VALUE, text);
            return;
        }

        // One or two lines, each centred, in a font that fits both
        char lines[2][40];
        const char* nl = strchr(label, '\n');
        uint8_t n      = nl ? 2 : 1;
        strlcpy(lines[0], label, nl ? min<size_t>(nl - label + 1, sizeof(lines[0])) : sizeof(lines[0]));
        if (nl) strlcpy(lines[1], nl + 1, sizeof(lines[1]));
        const char* wider = n == 2 && textWidth(lines[1], theme::FONT_BUTTON) > textWidth(lines[0], theme::FONT_BUTTON)
                                ? lines[1]
                                : lines[0];
        const ILI9341_t3_font_t& f = fitFont(wider, w - 16, theme::FONT_BUTTON);
        int16_t blockH             = (n - 1) * f.line_space + f.cap_height;
        int16_t ty                 = y + (h - blockH) / 2;
        for (uint8_t i = 0; i < n; i++) printCentered(lines[i], x, w, ty + i * f.line_space, f, text);
    }

    void ButtonGroup::drawAll() const
    {
        for (uint8_t i = 0; i < count; i++) buttons[i].draw();
    }

    int8_t ButtonGroup::handle(const touch::Event& e)
    {
        if (e.type == touch::Type::Down)
        {
            for (uint8_t i = 0; i < count; i++)
                if (buttons[i].enabled && buttons[i].contains(e.pos))
                {
                    down = i;
                    buttons[i].draw(true);
                    break;
                }
        }
        else if (e.type == touch::Type::Up && down >= 0)
        {
            int8_t i = down;
            down     = -1;
            buttons[i].draw();
            if (buttons[i].enabled && buttons[i].contains(e.pos)) return i;
        }
        return -1;
    }

    void drawStatusBar(const char* title)
    {
        statusTitle = title;
        shownState  = machineState();

        auto& d = tft();
        d.fillRect(0, 0, theme::W, theme::STATUS_H, theme::STATUS_BG);
        printAt(title, 8, 7, theme::FONT_TITLE, theme::FG);

        const char* text = "driver not ready";
        uint16_t color   = theme::ERR;
        if (shownState == MachineState::Winding)
        {
            text  = "winding";
            color = theme::WARN;
        }
        else if (shownState == MachineState::WindingHot)
        {
            text  = "winding - HOT";
            color = theme::ERR;
        }
        else if (shownState == MachineState::Paused)
        {
            text  = "paused";
            color = theme::WARN;
        }
        else if (shownState == MachineState::Ready)
        {
            text  = "ready - motor free";
            color = theme::OK;
        }
        else if (shownState == MachineState::ReadyHolding)
        {
            text  = "ready - holding";
            color = theme::OK;
        }
        printRight(text, theme::W - 8, 7, theme::FONT_TITLE, color);
    }

    void refreshStatusBar()
    {
        if (shownState != MachineState::Unknown && machineState() != shownState) drawStatusBar(statusTitle);
    }

    int16_t textWidth(const char* text, const ILI9341_t3_font_t& font)
    {
        auto& d = tft();
        d.setFont(font);
        return d.measureTextWidth(reinterpret_cast<const uint8_t*>(text));
    }

    const ILI9341_t3_font_t& fitFont(const char* text, int16_t maxW, const ILI9341_t3_font_t& preferred)
    {
        if (textWidth(text, preferred) <= maxW) return preferred;
        const ILI9341_t3_font_t* f = &preferred;
        for (const ILI9341_t3_font_t* c : LADDER)
        {
            if (c->cap_height >= preferred.cap_height) continue;
            f = c;
            if (textWidth(text, *c) <= maxW) break;
        }
        return *f;
    }

    void printFit(const char* text, int16_t x, int16_t w, int16_t y, const ILI9341_t3_font_t& preferred, uint16_t color)
    {
        printCentered(text, x, w, y, fitFont(text, w, preferred), color);
    }

    void drawMessage(const char* text, int16_t y, int16_t h, uint16_t color)
    {
        constexpr int16_t MAX_W = theme::W - 16;
        tft().fillRect(0, y, theme::W, h, theme::BG);
        if (!text || !*text) return;

        const ILI9341_t3_font_t& body  = theme::FONT_BODY;
        const ILI9341_t3_font_t& small = theme::FONT_SMALL;
        if (textWidth(text, body) <= MAX_W && theme::inkHeight(body) <= h)
            printCentered(text, 0, theme::W, y + (h - theme::inkHeight(body)) / 2, body, color);
        else if (textWidth(text, small) <= MAX_W || small.line_space + theme::inkHeight(small) > h)
            printCentered(text, 0, theme::W, y + (h - theme::inkHeight(small)) / 2, small, color);
        else
            printWrapped(text, 8, y + (h - small.line_space - theme::inkHeight(small)) / 2, MAX_W, small.line_space,
                         small, color);
    }

    void printAt(const char* text, int16_t x, int16_t y, const ILI9341_t3_font_t& font, uint16_t color)
    {
        // Debug aid: report text that runs off the screen (it is clipped,
        // never wrapped). Each text is reported once in a row.
        static char lastReported[48];
        if (*text && (x < 0 || x + textWidth(text, font) > theme::W) && strncmp(text, lastReported, sizeof(lastReported) - 1))
        {
            strlcpy(lastReported, text, sizeof(lastReported));
            Serial.printf("UI: text too wide at x %d: \"%s\"\n", x, text);
        }

        auto& d = tft();
        d.setFont(font);
        d.setTextColor(color);
        d.setCursor(x, y);
        d.print(text);
    }

    void printCentered(const char* text, int16_t x, int16_t w, int16_t y, const ILI9341_t3_font_t& font, uint16_t color)
    {
        printAt(text, x + (w - textWidth(text, font)) / 2, y, font, color);
    }

    void printRight(const char* text, int16_t right, int16_t y, const ILI9341_t3_font_t& font, uint16_t color)
    {
        printAt(text, right - textWidth(text, font), y, font, color);
    }

    uint8_t printWrapped(const char* text, int16_t x, int16_t y, int16_t w, int16_t lineH,
                         const ILI9341_t3_font_t& font, uint16_t color)
    {
        char line[64];
        uint8_t lines = 0;
        const char* p = text;
        while (*p)
        {
            // Take as many whole words as fit in w
            size_t len = 0, fit = 0;
            while (p[len])
            {
                size_t end = len;
                while (p[end] && p[end] != ' ') end++;
                if (end >= sizeof(line)) break;
                memcpy(line, p, end);
                line[end] = '\0';
                if (fit && textWidth(line, font) > w) break;
                fit = end;
                len = p[end] ? end + 1 : end;
            }
            if (!fit) fit = len = strnlen(p, sizeof(line) - 1); // single word too long: print it anyway
            memcpy(line, p, fit);
            line[fit] = '\0';
            printAt(line, x, y + lines * lineH, font, color);
            lines++;
            p += len;
        }
        return lines;
    }

    const char* fmtThousands(uint32_t v, char* buf, size_t size)
    {
        char digits[12];
        int n = snprintf(digits, sizeof(digits), "%lu", v);
        size_t o = 0;
        for (int i = 0; i < n && o + 1 < size; i++)
        {
            if (i && (n - i) % 3 == 0 && o + 2 < size) buf[o++] = ',';
            buf[o++] = digits[i];
        }
        buf[o] = '\0';
        return buf;
    }

    const char* fmtDuration(uint32_t ms, char* buf, size_t size)
    {
        uint32_t s = ms / 1000;
        if (s >= 3600) snprintf(buf, size, "%lu:%02lu:%02lu", s / 3600, (s / 60) % 60, s % 60);
        else snprintf(buf, size, "%lu:%02lu", s / 60, s % 60);
        return buf;
    }

    void drawTurnCounter(uint32_t turns, uint32_t target, uint16_t color)
    {
        // Number right-aligned so the layout never shifts; target beside it.
        // The font follows the target, so it stays the same for the whole
        // run: "9,999" is 200 px in 60 pt, "11,000" would be 243, so 5-digit
        // targets use 48 pt (196 px), on the same baseline.
        constexpr int16_t NUMBER_RIGHT = 212;
        const ILI9341_t3_font_t& f     = target >= 10000 ? theme::FONT_BIG5 : theme::FONT_BIG;
        int16_t baseline               = COUNTER_Y + theme::FONT_BIG.cap_height;
        char a[16], b[20], t[16];
        tft().fillRect(0, COUNTER_Y, theme::W, PROGRESS_Y - COUNTER_Y - 2, theme::BG);
        printRight(fmtThousands(turns, a, sizeof(a)), NUMBER_RIGHT, baseline - f.cap_height, f, color);
        snprintf(b, sizeof(b), "/ %s", fmtThousands(target, t, sizeof(t)));
        printAt(b, NUMBER_RIGHT + 8, baseline - theme::FONT_VALUE.cap_height, theme::FONT_VALUE, theme::MUTED);
    }

    void drawProgressBar(uint32_t done, uint32_t total, uint16_t color)
    {
        constexpr int16_t X = 8, W = 304, H = 12;
        auto& d = tft();
        int16_t fill = total ? (int16_t)((uint64_t)(W - 4) * min(done, total) / total) : 0;
        d.drawRect(X, PROGRESS_Y, W, H, theme::MUTED);
        d.fillRect(X + 2, PROGRESS_Y + 2, fill, H - 4, color);
        d.fillRect(X + 2 + fill, PROGRESS_Y + 2, W - 4 - fill, H - 4, theme::BG);
    }
}
