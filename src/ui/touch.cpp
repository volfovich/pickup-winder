#include "ui/touch.h"
#include "config.h"
#include "ui/affine.h"
#include <EEPROM.h>
#include <XPT2046_Touchscreen.h>

namespace ui::touch
{
    namespace
    {
        XPT2046_Touchscreen ts(PIN_TOUCH_CS, PIN_TOUCH_IRQ);

        constexpr uint32_t SAMPLE_MS     = 10;   // ~100 Hz
        constexpr uint8_t DEBOUNCE       = 2;    // matching samples to change state
        constexpr int16_t Z_FIRM         = 500;  // only firm samples count for averages
        constexpr uint32_t LONG_PRESS_MS = 1000;
        constexpr uint8_t AVG_LEN        = 4;    // position smoothing

        // Calibration stored in EEPROM. Tied to the display rotation, so a
        // rotation change forces a new calibration.
        struct CalData
        {
            uint32_t magic;
            uint16_t version;
            uint16_t rotation;
            Affine affine;
            uint32_t check;
        };
        constexpr uint32_t CAL_MAGIC   = 0x54434831; // "TCH1"
        constexpr uint16_t CAL_VERSION = 1;
        static_assert(sizeof(CalData) <= 64, "touch calibration must fit its EEPROM slot");

        Affine cal{};
        bool haveCal = false;

        bool pressed        = false;
        uint8_t agree       = 0;
        uint32_t lastSample = 0;
        uint32_t downMs     = 0;
        bool longSent       = false;
        Raw latest{0, 0, 0};

        int32_t sumX = 0, sumY = 0, nFirm = 0;   // whole-press average (calibration)
        int16_t histX[AVG_LEN], histY[AVG_LEN]; // recent samples (position)
        uint8_t histN = 0, histI = 0;
        Point pos{0, 0};

        Event queue[4];
        uint8_t qHead = 0, qCount = 0;

        uint32_t checksum(const CalData& d)
        {
            // FNV-1a over everything before `check`
            const uint8_t* p = reinterpret_cast<const uint8_t*>(&d);
            uint32_t h       = 2166136261u;
            for (size_t i = 0; i < offsetof(CalData, check); i++) h = (h ^ p[i]) * 16777619u;
            return h;
        }

        void loadCalibration()
        {
            CalData d;
            EEPROM.get(EEPROM_ADDR_TOUCH_CAL, d);
            haveCal = d.magic == CAL_MAGIC && d.version == CAL_VERSION && d.rotation == DISPLAY_ROTATION &&
                      d.check == checksum(d);
            if (haveCal) cal = d.affine;
        }

        void push(Type t, Raw raw)
        {
            if (qCount == sizeof(queue) / sizeof(queue[0])) return; // full: drop
            queue[(qHead + qCount++) % 4] = Event{t, pos, raw};
        }

        void updatePosition(int16_t rx, int16_t ry)
        {
            histX[histI] = rx;
            histY[histI] = ry;
            histI        = (histI + 1) % AVG_LEN;
            if (histN < AVG_LEN) histN++;
            int32_t ax = 0, ay = 0;
            for (uint8_t i = 0; i < histN; i++)
            {
                ax += histX[i];
                ay += histY[i];
            }
            pos = map(ax / histN, ay / histN);
        }
    }

    void begin()
    {
        ts.begin();
        loadCalibration();
    }

    void update()
    {
        uint32_t now = millis();
        if (now - lastSample < SAMPLE_MS) return;
        lastSample = now;

        bool touched = ts.touched();
        if (touched)
        {
            TS_Point p = ts.getPoint();
            latest     = Raw{p.x, p.y, p.z};
        }

        agree = (touched != pressed) ? agree + 1 : 0;
        if (agree >= DEBOUNCE)
        {
            agree   = 0;
            pressed = touched;
            if (pressed)
            {
                downMs   = now;
                longSent = false;
                sumX = sumY = nFirm = 0;
                histN = histI = 0;
                updatePosition(latest.x, latest.y);
                push(Type::Down, latest);
            }
            else
            {
                Raw avg = nFirm ? Raw{(int16_t)(sumX / nFirm), (int16_t)(sumY / nFirm), Z_FIRM} : latest;
                push(Type::Up, avg);
            }
        }

        if (pressed && touched)
        {
            updatePosition(latest.x, latest.y);
            if (latest.z >= Z_FIRM)
            {
                sumX += latest.x;
                sumY += latest.y;
                nFirm++;
            }
            if (!longSent && now - downMs >= LONG_PRESS_MS)
            {
                longSent = true;
                push(Type::LongPress, latest);
            }
        }
    }

    bool pollEvent(Event* e)
    {
        if (!qCount) return false;
        *e    = queue[qHead];
        qHead = (qHead + 1) % 4;
        qCount--;
        return true;
    }

    bool isPressed() { return pressed; }
    Point position() { return pos; }
    Raw lastRaw() { return latest; }
    bool touchedNow() { return ts.touched(); }
    bool calibrated() { return haveCal; }

    bool solveCalibration(const Raw raw[3], const Point screen[3])
    {
        float rx[3], ry[3], sx[3], sy[3];
        for (int i = 0; i < 3; i++)
        {
            rx[i] = raw[i].x;
            ry[i] = raw[i].y;
            sx[i] = screen[i].x;
            sy[i] = screen[i].y;
        }
        Affine a;
        if (!solveAffine(rx, ry, sx, sy, a)) return false;

        cal     = a;
        haveCal = true;
        return true;
    }

    void saveCalibration()
    {
        CalData d{CAL_MAGIC, CAL_VERSION, DISPLAY_ROTATION, cal, 0};
        d.check = checksum(d);
        EEPROM.put(EEPROM_ADDR_TOUCH_CAL, d);
    }

    void reloadCalibration()
    {
        loadCalibration();
    }

    Point map(int16_t rx, int16_t ry)
    {
        if (!haveCal) // rough guess until calibrated
            return Point{(int16_t)(rx * 320 / 4096), (int16_t)(ry * 240 / 4096)};
        float x = cal.mapX(rx, ry);
        float y = cal.mapY(rx, ry);
        return Point{(int16_t)constrain(x, 0.0f, 319.0f), (int16_t)constrain(y, 0.0f, 239.0f)};
    }
}
