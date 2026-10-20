#include "ui/widgets.h"
#include "tmc_driver.h"
#include "winder.h"

namespace ui
{
    namespace
    {
        const char* statusTitle = "";

        enum class MachineState : uint8_t { Unknown, NotReady, Ready, Winding };
        MachineState shownState = MachineState::Unknown;

        MachineState machineState()
        {
            if (winder::isRunning()) return MachineState::Winding;
            return tmc::ready() ? MachineState::Ready : MachineState::NotReady;
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
        }
        else
        {
            printCentered(label, x, w, y + (h - theme::FONT_BUTTON.cap_height) / 2, theme::FONT_BUTTON, text);
        }
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
        else if (shownState == MachineState::Ready)
        {
            text  = "driver ready";
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

    void printAt(const char* text, int16_t x, int16_t y, const ILI9341_t3_font_t& font, uint16_t color)
    {
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
}
