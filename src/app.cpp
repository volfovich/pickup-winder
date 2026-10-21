#include "app.h"
#include "config.h"
#include "tmc_driver.h"
#include <EEPROM.h>

namespace app
{
    namespace
    {
        winder::Settings current{DEFAULT_TURNS, DEFAULT_RPM, DEFAULT_ACCEL_RPM_PER_S, winder::Dir::CW};

        // Settings in EEPROM, versioned and checksummed
        struct Stored
        {
            uint32_t magic;
            uint16_t version;
            winder::Settings s;
            uint32_t check;
        };
        constexpr uint32_t SETTINGS_MAGIC   = 0x57494E44; // "WIND"
        constexpr uint16_t SETTINGS_VERSION = 1;

        // Driver fault watch while winding
        uint32_t lastPollMs  = 0;
        uint8_t commFailures = 0;
        bool otWarned        = false;
        constexpr uint8_t COMM_FAILURES_TO_ABORT = 3;

        constexpr const char* BUSY = "not allowed during a run";

        uint32_t checksum(const Stored& d)
        {
            // FNV-1a over everything before `check`
            const uint8_t* p = reinterpret_cast<const uint8_t*>(&d);
            uint32_t h       = 2166136261u;
            for (size_t i = 0; i < offsetof(Stored, check); i++) h = (h ^ p[i]) * 16777619u;
            return h;
        }

        bool valid(const winder::Settings& s)
        {
            return !winder::checkTurns(s.turns) && !winder::checkRpm(s.rpm) && !winder::checkAccel(s.accelRpmPerS) &&
                   (s.dir == winder::Dir::CW || s.dir == winder::Dir::CCW);
        }

        void save()
        {
            Stored d{SETTINGS_MAGIC, SETTINGS_VERSION, current, 0};
            d.check = checksum(d);
            EEPROM.put(EEPROM_ADDR_SETTINGS, d);
        }

        void watchDriver()
        {
            if (!winder::isMoving() || millis() - lastPollMs < FAULT_POLL_MS) return;
            lastPollMs = millis();

            bool otpw        = false;
            tmc::Fault fault = tmc::poll(&otpw);

            if (fault == tmc::Fault::NoComm)
            {
                if (++commFailures < COMM_FAILURES_TO_ABORT) return; // ride out a single noisy read
            }
            else
            {
                commFailures = 0;
            }

            if (fault != tmc::Fault::None)
            {
                winder::abortOnFault(tmc::faultName(fault));
                return;
            }
            if (otpw && !otWarned)
            {
                otWarned = true;
                Serial.println("WARNING: driver over-temperature pre-warning (otpw)");
            }
        }
    }

    void begin()
    {
        Stored d;
        EEPROM.get(EEPROM_ADDR_SETTINGS, d);
        if (d.magic == SETTINGS_MAGIC && d.version == SETTINGS_VERSION && d.check == checksum(d) && valid(d.s))
            current = d.s;
        else
            Serial.println("settings: none saved yet (or invalid) — using defaults");
    }

    winder::Event update()
    {
        watchDriver();
        winder::Event e = winder::update();
        if (e == winder::Event::Ended && winder::lastOutcome() == winder::Outcome::Faulted) winder::enableDriver(false);
        return e;
    }

    const winder::Settings& settings()
    {
        return current;
    }

    const char* setTurns(uint32_t turns)
    {
        if (winder::inRun()) return BUSY;
        if (const char* err = winder::checkTurns(turns)) return err;
        current.turns = turns;
        save();
        return nullptr;
    }

    const char* setRpm(uint32_t rpm)
    {
        if (winder::inRun()) return BUSY;
        if (const char* err = winder::checkRpm(rpm)) return err;
        current.rpm = rpm;
        save();
        return nullptr;
    }

    const char* setAccel(uint32_t accelRpmPerS)
    {
        if (winder::inRun()) return BUSY;
        if (const char* err = winder::checkAccel(accelRpmPerS)) return err;
        current.accelRpmPerS = accelRpmPerS;
        save();
        return nullptr;
    }

    const char* setDir(winder::Dir dir)
    {
        if (winder::inRun()) return BUSY;
        current.dir = dir;
        save();
        return nullptr;
    }

    const char* start()
    {
        if (const char* err = winder::start(current)) return err;
        lastPollMs   = millis();
        commFailures = 0;
        otWarned     = false;
        return nullptr;
    }

    const char* resume()
    {
        if (const char* err = winder::resume()) return err;
        lastPollMs   = millis();
        commFailures = 0;
        return nullptr;
    }

    const char* finish()
    {
        return winder::finish();
    }

    void stop()
    {
        winder::stop();
    }

    void estop()
    {
        winder::estop();
    }

    const char* release()
    {
        if (winder::inRun()) return BUSY;
        winder::enableDriver(false);
        return nullptr;
    }

    const char* reinitDriver()
    {
        if (winder::inRun()) return BUSY;
        return tmc::begin() ? nullptr : "driver setup failed - check 24 V and UART";
    }
}
