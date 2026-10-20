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

    // Prints the driver configuration and status to Serial.
    void printStatus();
}
