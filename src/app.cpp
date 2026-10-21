#include "app.h"
#include "config.h"
#include "tmc_driver.h"
#include <EEPROM.h>

namespace app
{
    namespace
    {
        winder::Settings current{DEFAULT_TURNS, DEFAULT_RPM, DEFAULT_ACCEL_RPM_PER_S, winder::Dir::CW};
        TraverseSettings trav{false, DEFAULT_WIRE_UM, DEFAULT_WIDTH_UM, DEFAULT_OFFSET_UM, true};

        // Settings in EEPROM, versioned and checksummed
        struct Stored
        {
            uint32_t magic;
            uint16_t version;
            winder::Settings s;
            TraverseSettings t;
            uint16_t currentMa; // spindle run current
            uint32_t check;
        };
        struct StoredV1 // before the traverse
        {
            uint32_t magic;
            uint16_t version;
            winder::Settings s;
            uint32_t check;
        };
        constexpr uint32_t SETTINGS_MAGIC   = 0x57494E44; // "WIND"
        constexpr uint16_t SETTINGS_VERSION = 2;

        // Driver fault watch while winding: spindle and traverse take turns
        uint32_t lastPollMs  = 0;
        uint32_t pollCount   = 0;
        uint8_t commFailures[2] = {0, 0};
        bool otWarned        = false;

        // Spindle release after a run (paused runs keep holding)
        bool idleReleaseArmed = false;
        uint32_t idleSinceMs  = 0;
        constexpr uint8_t COMM_FAILURES_TO_ABORT = 3;

        // The run with the traverse
        traverse_math::Pattern pattern{};
        bool positioningNow     = false;
        const char* startErr    = nullptr;
        bool travRun            = false;
        bool travMscntValid     = false;
        uint16_t travMscntStart = 0, travMscntEnd = 0;
        int32_t travStartPos    = 0, travEndPos = 0;
        traverse::Event travEvent = traverse::Event::None;

        constexpr const char* BUSY = "not allowed during a run";

        template <typename T>
        uint32_t checksum(const T& d)
        {
            // FNV-1a over everything before `check`
            const uint8_t* p = reinterpret_cast<const uint8_t*>(&d);
            uint32_t h       = 2166136261u;
            for (size_t i = 0; i < offsetof(T, check); i++) h = (h ^ p[i]) * 16777619u;
            return h;
        }

        bool valid(const winder::Settings& s)
        {
            return !winder::checkTurns(s.turns) && !winder::checkRpm(s.rpm) && !winder::checkAccel(s.accelRpmPerS) &&
                   (s.dir == winder::Dir::CW || s.dir == winder::Dir::CCW);
        }

        bool valid(const TraverseSettings& t)
        {
            return t.wireUm >= MIN_WIRE_UM && t.wireUm <= MAX_WIRE_UM && t.widthUm >= MIN_WIDTH_UM &&
                   t.widthUm <= MAX_WIDTH_UM && t.offsetUm <= TRAVERSE_TRAVEL_UM;
        }

        void save()
        {
            Stored d;
            memset(&d, 0, sizeof(d)); // padding too, for a stable checksum
            d.magic   = SETTINGS_MAGIC;
            d.version = SETTINGS_VERSION;
            d.s       = current;
            d.t         = trav;
            d.currentMa = tmc::spindle.current();
            d.check     = checksum(d);
            EEPROM.put(EEPROM_ADDR_SETTINGS, d);
        }

        bool makeRunPattern()
        {
            return traverse_math::makePattern(&pattern, trav.wireUm, trav.widthUm, trav.offsetUm, trav.firstAway,
                                              TRAVERSE_STEPS_PER_MM, STEPS_PER_REV);
        }

        void watchDriver()
        {
            if (!winder::isMoving() || millis() - lastPollMs < FAULT_POLL_MS) return;
            lastPollMs = millis();

            if (travRun && traverse::limitHit())
            {
                winder::abortOnFault("traverse reached a soft limit");
                return;
            }

            // One driver per poll: each UART read blocks the loop ~5 ms
            uint8_t i         = travRun && (pollCount++ & 1) ? 1 : 0;
            tmc::Driver& drv  = i ? tmc::traverse : tmc::spindle;
            bool otpw         = false;
            tmc::Fault fault  = drv.poll(&otpw);

            if (fault == tmc::Fault::NoComm)
            {
                if (++commFailures[i] < COMM_FAILURES_TO_ABORT) return; // ride out a single noisy read
            }
            else
            {
                commFailures[i] = 0;
            }

            if (fault != tmc::Fault::None)
            {
                static char reason[64];
                if (i) snprintf(reason, sizeof(reason), "traverse: %s", tmc::faultName(fault));
                winder::abortOnFault(i ? reason : tmc::faultName(fault));
                return;
            }
            if (otpw && !otWarned)
            {
                otWarned = true;
                Serial.printf("WARNING: %s driver over-temperature pre-warning (otpw)\n", drv.config().name);
            }
        }

        void resetWatch()
        {
            lastPollMs      = millis();
            commFailures[0] = commFailures[1] = 0;
        }

        const char* startSpindle()
        {
            if (const char* err = winder::start(current)) return err;
            resetWatch();
            otWarned = false;
            return nullptr;
        }

        // Positioning done: start the spindle with the guide following
        void startWithTraverse()
        {
            positioningNow = false;
            if (traverse::positionSteps() != traverse_math::position(pattern, 0))
            {
                startErr = traverse::lastError() ? traverse::lastError() : "traverse did not reach the start";
                Serial.printf("ERR: %s\n", startErr);
                return;
            }
            travMscntValid = tmc::traverse.readMscnt(&travMscntStart);
            travStartPos   = traverse::positionSteps();
            if ((startErr = startSpindle()))
            {
                Serial.printf("ERR: %s\n", startErr);
                return;
            }
            traverse::follow(pattern); // the spindle has made at most a step by now
            travRun = true;
        }

        void endTraverseRun()
        {
            traverse::stopFollow();
            travEndPos = traverse::positionSteps();
            if (winder::lastOutcome() == winder::Outcome::Faulted) travMscntValid = false;
            else if (travMscntValid) travMscntValid = tmc::traverse.readMscnt(&travMscntEnd);
        }
    }

    void begin()
    {
        Stored d;
        EEPROM.get(EEPROM_ADDR_SETTINGS, d);
        if (d.magic == SETTINGS_MAGIC && d.version == SETTINGS_VERSION && d.check == checksum(d) && valid(d.s) &&
            valid(d.t) && d.currentMa >= MIN_CURRENT_MA && d.currentMa <= MAX_CURRENT_MA)
        {
            current = d.s;
            trav    = d.t;
            tmc::spindle.setCurrent(d.currentMa); // before the driver is configured: used by begin()
            return;
        }

        StoredV1 v1;
        EEPROM.get(EEPROM_ADDR_SETTINGS, v1);
        if (v1.magic == SETTINGS_MAGIC && v1.version == 1 && v1.check == checksum(v1) && valid(v1.s))
        {
            current = v1.s;
            save(); // as version 2, traverse settings at their defaults
            Serial.println("settings: upgraded (traverse settings added, traverse off)");
            return;
        }
        Serial.println("settings: none saved yet (or invalid) - using defaults");
    }

    winder::Event update()
    {
        travEvent = traverse::update();
        if (positioningNow && !traverse::busy()) startWithTraverse();

        watchDriver();
        winder::Event e = winder::update();
        if (e == winder::Event::Ended)
        {
            idleReleaseArmed = true;
            idleSinceMs      = millis();
            if (travRun) endTraverseRun();
            if (winder::lastOutcome() == winder::Outcome::Faulted)
            {
                winder::enableDriver(false);
                const char* why = winder::faultReason();
                if (travRun && why && !strncmp(why, "traverse", 8)) traverse::enableDriver(false);
            }
        }

        // Nobody needs the spindle held long after a run: free it, no heat
        if (idleReleaseArmed && !winder::inRun() && !positioningNow && millis() - idleSinceMs >= IDLE_RELEASE_MS)
        {
            idleReleaseArmed = false;
            if (winder::driverEnabled())
            {
                winder::enableDriver(false);
                Serial.printf("spindle released after %lu min idle (motor free)\n", IDLE_RELEASE_MS / 60000);
            }
        }
        return e;
    }

    traverse::Event traverseEvent()
    {
        return travEvent;
    }

    const winder::Settings& settings()
    {
        return current;
    }

    const TraverseSettings& traverseSettings()
    {
        return trav;
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

    const char* setCurrent(uint32_t mA)
    {
        static char msg[40];
        if (winder::inRun() || positioningNow) return BUSY;
        if (mA < MIN_CURRENT_MA || mA > MAX_CURRENT_MA)
        {
            snprintf(msg, sizeof(msg), "current must be %u..%u mA", MIN_CURRENT_MA, MAX_CURRENT_MA);
            return msg;
        }
        if (!tmc::spindle.setCurrent(mA)) return "driver did not take it - check 24 V, then re-init";
        save();
        return nullptr;
    }

    uint16_t spindleCurrent()
    {
        return tmc::spindle.current();
    }

    bool driverHot()
    {
        return otWarned;
    }

    const char* setTraverseOn(bool on)
    {
        if (winder::inRun() || positioningNow) return BUSY;
        trav.on = on;
        save();
        return nullptr;
    }

    const char* setWire(uint32_t um)
    {
        static char msg[40];
        if (winder::inRun() || positioningNow) return BUSY;
        if (um < MIN_WIRE_UM || um > MAX_WIRE_UM)
        {
            snprintf(msg, sizeof(msg), "wire must be 0.%03lu..0.%03lu mm", MIN_WIRE_UM, MAX_WIRE_UM);
            return msg;
        }
        trav.wireUm = um;
        save();
        return nullptr;
    }

    const char* setWidth(uint32_t um)
    {
        static char msg[40];
        if (winder::inRun() || positioningNow) return BUSY;
        if (um < MIN_WIDTH_UM || um > MAX_WIDTH_UM)
        {
            snprintf(msg, sizeof(msg), "width must be %lu..%lu mm", MIN_WIDTH_UM / 1000, MAX_WIDTH_UM / 1000);
            return msg;
        }
        trav.widthUm = um;
        save();
        return nullptr;
    }

    const char* setOffset(uint32_t um)
    {
        if (winder::inRun() || positioningNow) return BUSY;
        if (um > TRAVERSE_TRAVEL_UM) return "offset must be 0..50 mm";
        trav.offsetUm = um;
        save();
        return nullptr;
    }

    const char* setFirstAway(bool away)
    {
        if (winder::inRun() || positioningNow) return BUSY;
        trav.firstAway = away;
        save();
        return nullptr;
    }

    const char* checkTraverse()
    {
        static char msg[48];
        if (trav.widthUm < trav.wireUm) return "width is less than one wire";
        uint32_t turnsPerLayer = trav.widthUm / trav.wireUm;
        if (trav.offsetUm + turnsPerLayer * trav.wireUm > TRAVERSE_TRAVEL_UM)
            return "winding goes past 50 mm - reduce offset or width";
        float followUmS = trav.wireUm * winder::actualRpm(current.rpm) / 60.0f;
        if (followUmS > TRAVERSE_MAX_FOLLOW_UM_S)
        {
            snprintf(msg, sizeof(msg), "wire too thick for %lu rpm (guide max %lu mm/s)", current.rpm,
                     TRAVERSE_MAX_FOLLOW_UM_S / 1000);
            return msg;
        }
        return nullptr;
    }

    const char* start()
    {
        if (positioningNow) return "moving to the start position";
        startErr = nullptr;
        travRun  = false;
        if (!trav.on) return startSpindle();

        if (const char* err = winder::canStart(current)) return err;
        if (!tmc::traverse.ready()) return "traverse driver not ready - check 24 V, then re-init";
        if (!traverse::homed()) return "traverse not homed - home it first";
        if (const char* err = checkTraverse()) return err;
        tmc::Fault f = tmc::traverse.poll(nullptr);
        if (f != tmc::Fault::None)
        {
            static char reason[64];
            snprintf(reason, sizeof(reason), "traverse: %s", tmc::faultName(f));
            return reason;
        }
        if (!makeRunPattern()) return "width is less than one wire";
        if (const char* err = traverse::moveToSteps(traverse_math::position(pattern, 0))) return err;
        positioningNow = true;
        return nullptr;
    }

    bool positioning()
    {
        return positioningNow;
    }

    const char* startError()
    {
        return startErr;
    }

    const char* resume()
    {
        if (travRun && !tmc::traverse.ready()) return "traverse driver not ready - check 24 V, then re-init";
        if (const char* err = winder::resume()) return err;
        resetWatch();
        return nullptr;
    }

    const char* finish()
    {
        return winder::finish();
    }

    void stop()
    {
        if (positioningNow)
        {
            positioningNow = false;
            startErr       = "start cancelled";
        }
        traverse::stop();
        winder::stop();
    }

    void estop()
    {
        positioningNow = false;
        traverse::stop();
        winder::estop();
    }

    const char* home()
    {
        if (winder::inRun() || positioningNow) return BUSY;
        return traverse::home();
    }

    const char* jog(int32_t um)
    {
        if (winder::inRun() || positioningNow) return BUSY;
        return traverse::jog(um);
    }

    const char* moveTo(int32_t um)
    {
        if (winder::inRun() || positioningNow) return BUSY;
        return traverse::moveTo(um);
    }

    const char* release()
    {
        if (winder::inRun() || positioningNow) return BUSY;
        winder::enableDriver(false);
        traverse::enableDriver(false);
        return nullptr;
    }

    const char* reinitDriver()
    {
        if (winder::inRun() || positioningNow) return BUSY;
        bool ok = tmc::spindle.begin();
        // The traverse is optional until switched on. Re-configuring it may
        // reset its microstep position, so it must be homed again.
        if (tmc::traverse.begin(!trav.on) || trav.on) traverse::enableDriver(false);
        if (!ok) return "driver setup failed - check 24 V and UART";
        if (trav.on && !tmc::traverse.ready()) return "traverse driver setup failed - check 24 V and UART";
        return nullptr;
    }

    bool traverseRun()
    {
        return travRun;
    }

    const traverse_math::Pattern& runPattern()
    {
        return pattern;
    }

    winder::PulseCheck traversePulseCheck(int16_t* offsetSteps)
    {
        if (offsetSteps) *offsetSteps = 0;
        if (winder::lastOutcome() == winder::Outcome::Faulted) return winder::PulseCheck::Skipped;
        if (!travMscntValid) return winder::PulseCheck::Unavailable;
        return winder::checkMscnt(travMscntStart, travMscntEnd, (uint32_t)abs(travEndPos - travStartPos), offsetSteps);
    }
}
