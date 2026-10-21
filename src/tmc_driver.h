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

    // Configures the TMC2209 over UART (spreadCycle, current, microsteps) and
    // verifies the configuration by reading it back. Returns true on success.
    bool begin();

    // True once begin() configured and verified the driver.
    bool ready();

    // Reads DRV_STATUS and reports a fault that makes the step count unreliable.
    // Sets *otWarning if the over-temperature pre-warning is active.
    Fault poll(bool* otWarning);

    const char* faultName(Fault f);

    // Reads MSCNT, the driver's own microstep counter (0..1023). It moves by
    // 256 / MICROSTEPS for every STEP pulse the driver receives. Returns false
    // on a UART error.
    bool readMscnt(uint16_t* out);

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

    // A full status takes STATUS_READS UART reads of ~5 ms each. readStatus()
    // does one of them (part 0..STATUS_READS-1), so the UI can spread a
    // refresh over several loop passes. Returns false on a UART error.
    constexpr uint8_t STATUS_READS = 6;
    bool readStatus(Status* s, uint8_t part);

    // Prints the driver configuration and status to Serial.
    void printStatus();
}
