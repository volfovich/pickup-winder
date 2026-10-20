#include "ui/widgets.h"
#include "tmc_driver.h"

namespace ui
{
    void Button::draw(bool highlighted) const
    {
        auto& d = tft();
        d.fillRoundRect(x, y, w, h, 8, highlighted ? theme::BUTTON_HI : color);
        d.drawRoundRect(x, y, w, h, 8, theme::MUTED);
        int16_t textH = theme::FONT_BUTTON.cap_height;
        printCentered(label, x, w, y + (h - textH) / 2, theme::FONT_BUTTON, theme::FG);
    }

    void drawStatusBar(const char* title)
    {
        auto& d = tft();
        d.fillRect(0, 0, theme::W, theme::STATUS_H, theme::STATUS_BG);
        d.setFont(theme::FONT_TITLE);
        d.setTextColor(theme::FG);
        d.setCursor(8, 7);
        d.print(title);

        bool ready         = tmc::ready();
        const char* status = ready ? "driver ready" : "driver not ready";
        d.setTextColor(ready ? theme::OK : theme::ERR);
        d.setCursor(theme::W - 8 - d.measureTextWidth(status), 7);
        d.print(status);
    }

    void printCentered(const char* text, int16_t x, int16_t w, int16_t y, const ILI9341_t3_font_t& font, uint16_t color)
    {
        auto& d = tft();
        d.setFont(font);
        d.setTextColor(color);
        d.setCursor(x + (w - (int16_t)d.measureTextWidth(text)) / 2, y);
        d.print(text);
    }
}
