#pragma once

#include <stdint.h>

#define FIRMWARE_NAME    "Pickup Winder NG"
#define FIRMWARE_VERSION "1.0"

// ---------------------------------------------------------------------------
// Pins (Teensy 4.0)
// ---------------------------------------------------------------------------
// TMC2209 single-wire UART: Teensy TX1 (pin 1) --[1k]--> PDN_UART <-- Teensy RX1 (pin 0)
#define TMC_SERIAL Serial1
constexpr uint32_t TMC_BAUD = 115200;

constexpr uint8_t PIN_STEP = 2;
constexpr uint8_t PIN_DIR  = 3;
constexpr uint8_t PIN_EN   = 4; // active LOW

// Optional self-test: jumper PIN_STEP to this pin to count the actual STEP
// pulses with an independent interrupt. Comment out to disable.
// #define STEP_LOOPBACK_PIN 5

// Display (ILI9341) + touch (HR2046 / XPT2046) on the shared SPI bus:
// MOSI 11, MISO 12, SCK 13. Backlight LED and VCC to 3.3 V.
constexpr uint8_t PIN_TFT_CS    = 10;
constexpr uint8_t PIN_TFT_DC    = 9;
constexpr uint8_t PIN_TFT_RST   = 8;
constexpr uint8_t PIN_TOUCH_CS  = 14;
constexpr uint8_t PIN_TOUCH_IRQ = 15;

// Landscape with the module's pin header on the left. If the picture is
// upside down, use 1 instead (and recalibrate touch).
constexpr uint8_t DISPLAY_ROTATION = 3;

// ---------------------------------------------------------------------------
// EEPROM layout (Teensy 4.0: 1080 bytes)
// ---------------------------------------------------------------------------
constexpr int EEPROM_ADDR_TOUCH_CAL = 0;  // 64 bytes reserved
constexpr int EEPROM_ADDR_SETTINGS  = 64; // settings (UI-2)

// ---------------------------------------------------------------------------
// TMC2209 driver
// ---------------------------------------------------------------------------
constexpr uint8_t  DRIVER_ADDR    = 0;     // MS1 = MS2 = GND
constexpr float    R_SENSE        = 0.11f; // ohms — check your module (BTT/FYSETC: 0.11)
// Motor is rated 2.8 A/phase; the TMC2209 is the limit (~1.77 A RMS max with
// 0.11 ohm sense resistors). 1500 mA needs a heatsink and airflow on the driver.
constexpr uint16_t RMS_CURRENT_MA = 1500;  // motor run current (RMS)

// ---------------------------------------------------------------------------
// Motor / mechanics
// ---------------------------------------------------------------------------
constexpr uint32_t FULL_STEPS_PER_REV = 200; // 1.8 deg motor
constexpr uint32_t MICROSTEPS         = 16;  // driver interpolates to 256 internally
constexpr uint32_t STEPS_PER_REV      = FULL_STEPS_PER_REV * MICROSTEPS;

// ---------------------------------------------------------------------------
// Limits and defaults
// ---------------------------------------------------------------------------
constexpr uint32_t MIN_RPM   = 10; // TeensyStep4's 16-bit timer can't go below ~72 steps/s
constexpr uint32_t MAX_RPM   = 1500;
constexpr uint32_t MAX_TURNS = 11000;

constexpr uint32_t MIN_ACCEL_RPM_PER_S = 10;
constexpr uint32_t MAX_ACCEL_RPM_PER_S = 2000;

constexpr uint32_t DEFAULT_TURNS           = 10;
constexpr uint32_t DEFAULT_RPM             = 300;
constexpr uint32_t DEFAULT_ACCEL_RPM_PER_S = 300; // conservative to avoid stalls

// TeensyStep4 counts position in int32_t
static_assert((uint64_t)MAX_TURNS * STEPS_PER_REV <= INT32_MAX, "MAX_TURNS * STEPS_PER_REV must fit in int32");
// TeensyStep4's TMR timer with 8 us pulses tops out around 118 kHz; keep well below
static_assert((uint64_t)MAX_RPM * STEPS_PER_REV / 60 <= 100000, "step rate too high for TeensyStep4");

// ---------------------------------------------------------------------------
// Reporting
// ---------------------------------------------------------------------------
constexpr uint32_t PROGRESS_INTERVAL_MS = 250;
constexpr uint32_t FAULT_POLL_MS        = 100;
