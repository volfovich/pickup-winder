#pragma once

#include <Arduino.h>

namespace tmc
{
    enum class Fault : uint8_t {
        None,
        NoComm,         // UART not answering (VM lost?)
        OverTemp,       // ot
        ShortToGround,  // s2ga / s2gb
        ShortToSupply,  // s2vsa / s2vsb
        DriverReset,    // GSTAT.reset: driver lost its configuration (VM dip?)
        Undervoltage,   // GSTAT.uv_cp: charge pump undervoltage
        DriverError,    // GSTAT.drv_err: driver shut down
    };

    // Driver configuration and status, as shown by `drv` and the Driver screen.
    struct Status
    {
        // Raw registers
        uint32_t ioin, gconf, chopconf, drvStatus;
        uint8_t gstat;
        uint16_t mscnt;

        // Decoded
        uint8_t version;
        bool enPinOff; // EN pin high: outputs off
        bool spreadCycle, pdnDisable, mstepReg, iScaleAnalog;
        uint16_t microsteps;
        bool intpol, dedge;
        uint8_t toff, hstrt, hend, tblClocks; // hstrt/hend as written (register values)
        uint16_t runMa, holdMa; // RMS current set for running / holding
        uint8_t csActual;
        uint16_t currentMa;     // RMS current the driver uses right now (from cs_actual)
        bool standstill;
        bool otpw, ot, s2ga, s2gb, s2vsa, s2vsb, ola, olb;
        uint8_t tempOver; // highest threshold passed: 0 (below 120 °C), 120, 143, 150 or 157
        bool reset, drvErr, uvCp;
    };

    // A full status takes STATUS_READS UART reads of ~5 ms each, so
    // readStatus() does one of them per call (part 0..STATUS_READS-1).
    constexpr uint8_t STATUS_READS = 6;

    const char* faultName(Fault f);

    struct Config
    {
        const char* name; // "spindle", "traverse"
        uint8_t address;  // UART address set by MS1 / MS2
        float rSense;
        uint16_t currentMa; // run current (RMS), default
        uint8_t holdPct;    // standstill current, % of the run current
        uint16_t microsteps;
        bool stealthChop;
        // Hybrid (with stealthChop false): stealthChop below this step rate,
        // standstill included, spreadCycle above. 0 = off.
        uint32_t stealthBelowStepsPerS;

        bool usesStealthChop() const { return stealthChop || stealthBelowStepsPerS; }
        const char* modeName() const
        {
            return stealthChop ? "stealthChop" : stealthBelowStepsPerS ? "hybrid" : "spreadCycle";
        }
    };

    // One TMC2209 on the shared single-wire UART (TMC_SERIAL).
    class Driver
    {
     public:
        explicit Driver(const Config& cfg);

        // Configures the driver (chopper mode, current, microsteps) and
        // verifies it by reading it back. Returns true on success; quiet
        // = no serial message if the driver doesn't answer at all.
        bool begin(bool quiet = false);
        bool ready() const { return configured; } // begin() configured and verified it

        // Reads GSTAT and DRV_STATUS and reports a fault that makes the step
        // count unreliable. Sets *otWarning (may be nullptr) on the
        // over-temperature pre-warning.
        Fault poll(bool* otWarning);

        // Reads MSCNT, the driver's own microstep counter (0..1023). It moves by
        // 256 / microsteps for every STEP pulse received. False on a UART error.
        bool readMscnt(uint16_t* out);

        // One part of the status (see STATUS_READS). False on a UART error.
        bool readStatus(Status* s, uint8_t part);

        // Prints the configuration and status to Serial.
        void printStatus();

        // Run current (RMS). Written at once if the driver is configured,
        // else at the next begin(). False if the driver didn't take it.
        bool setCurrent(uint16_t mA);
        uint16_t current() const { return runMa; }

        const Config& config() const { return cfg; }

     private:
        bool writeCurrent();

        const Config cfg;
        uint16_t runMa;
        bool configured = false;
    };

    extern Driver spindle;  // address 0, spreadCycle
    extern Driver traverse; // address 1, hybrid: stealthChop only at standstill / very low speed
}
