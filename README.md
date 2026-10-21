# Pickup Winder

Guitar pickup coil winder firmware: a Teensy 4.0 drives a NEMA 17 spindle through a TMC2209 and stops on the **exact** preset turn count, up to 11,000 turns at up to 1500 RPM.

Firmware: **Pickup Winder NG v1.0** (`FIRMWARE_NAME` / `FIRMWARE_VERSION` in `include/config.h`).

## Status

**Step 1 — spindle motor control: done and tested on hardware.**
1000- and 3000-turn runs at 1500 RPM (accel 600 RPM/s) completed with exact counts, a clean driver pulse check, and the driver heatsink barely warm.

Next steps (wire traverse axis and front panel UI) are planned in [TBD.md](TBD.md).

## Hardware

| Part | Notes |
|---|---|
| Teensy 4.0 | 600 MHz, PlatformIO `teensy40` |
| TMC2209 driver | R_SENSE 0.11 Ω (BTT/FYSETC-style module); heatsink + fan |
| NEMA 17 stepper | 1.8°, rated 2.8 A/phase |
| 24 V supply | for the motor (VM) |

### Wiring

| Teensy pin | TMC2209 | Notes |
|---|---|---|
| 1 (TX1) | PDN_UART | through a **1 kΩ** resistor |
| 0 (RX1) | PDN_UART | direct (single-wire UART) |
| 2 | STEP | |
| 3 | DIR | |
| 4 | EN | active LOW |
| GND | MS1, MS2 | UART address 0 |
| 5 *(optional)* | — | jumper to pin 2 for the STEP loopback self-test |

> The TMC2209 only answers on UART when the **24 V motor power is on**. If it was off at boot, power it up and type `init`.

## Driver configuration

Set over UART at boot and **verified by reading every value back** — the motor will not start on a driver that failed verification.

| Setting | Value |
|---|---|
| Mode | **spreadCycle** (`en_spreadCycle = 1`) |
| Microsteps | 16, with 256× interpolation (`intpol`) |
| Run current | 1500 mA RMS (hold current 50%) |
| Chopper | toff 4, blank time 24, hstrt 5, hend 0 |
| Step input | rising edge only (`dedge = 0`) |

## How it works

- **Step generation:** [TeensyStep4](https://github.com/luni64/TeensyStep4) (pinned to a reviewed commit) produces STEP pulses from a hardware timer (QuadTimer TMR4, raised interrupt priority), so USB, UART or display work can't disturb stepping.
- **Exact count:** one turn = 200 × 16 = **3200 steps**; 11,000 turns = 35.2 M steps (the int32 position counter holds ~671,000 turns). Each run is a single move of exactly `turns × 3200` steps.
- **Ramps:** a trapezoidal profile planned in step counts — it ramps up from ~3.75 RPM, cruises, and ramps down so the slowest step is **exactly** the last one. Ramping down is what makes an exact stop safe: no inertia overshoot at full speed. Short runs become a triangle profile and still end on the exact step.
- **Speed resolution:** the step timer ticks at 0.213 µs, so speeds are quantised: 1200 RPM runs at **1204.0**, 1500 RPM at **1489.7**. The display shows the actual speed. This does not affect the count — the count is steps, not speed × time.

## Safety and verification

- **End-of-run check:** steps taken vs. target → `OK` / `MISMATCH`.
- **Driver pulse check:** the TMC2209's own microstep counter (`MSCNT`) is read before and after each run and must match the pulses sent — catches pulses lost or added on the STEP wire.
- **Fault watch** (every 100 ms while winding): over-temperature, short to ground/supply, UART lost (3 failed reads in a row), and driver reset / charge-pump undervoltage / driver error (`GSTAT`). Any fault aborts the run, marks it **“count unreliable”** and disables the driver. An over-temperature pre-warning is logged only.
- **Optional loopback:** uncomment `STEP_LOOPBACK_PIN` in `include/config.h` and jumper pin 2 → 5 to count the real STEP pulses independently.

## Limits and defaults

| Setting | Range | Default |
|---|---|---|
| Turns | 1 – 11000 | 10 |
| RPM | 10 – 1500 | 300 |
| Accel (RPM/s) | 10 – 2000, **except 327 – 423** ¹ | 300 |

¹ In that range TeensyStep4 computes an invalid timer period for the final pause; these values are rejected.

## Build and use

Requires [PlatformIO](https://platformio.org/).

```sh
pio run                 # build
pio run -t upload       # build and flash the Teensy
pio device monitor      # serial console, 115200, local echo on
```

### Commands

| Command | When | Description |
|---|---|---|
| `turns N` | idle | number of turns |
| `rpm N` | idle | max speed (shows actual timer speed) |
| `accel N` | idle | ramp up/down rate in RPM/s |
| `dir cw\|ccw` | idle | winding direction |
| `start` | idle | start winding |
| `release` | idle | disable the driver (motor free) |
| `init` | idle | re-run the TMC2209 configuration |
| `stop` | any time | ramped stop — the count stays exact |
| `estop` | any time | immediate stop |
| `status` | any time | settings and progress |
| `drv` | any time | TMC2209 registers and status |
| `help` | any time | command list |

Settings are locked while winding — a setup command during a run is rejected. While running, progress prints every 250 ms (turns and RPM measured over the last second); a summary prints at the end.

## Testing for skipped steps

The firmware counts every step it sends and checks the driver received them, but an open-loop stepper can still **slip mechanically**. On a 1.8° motor a slip always loses a whole electrical cycle = **7.2°** of spindle rotation, so it's visible:

1. **Pointer test:** fix a thin pointer next to the spindle, mark the spindle exactly at it, run, and check the mark ends exactly at the pointer.
2. **Margin test:** at your target RPM, raise `accel` on short runs (or lower `RMS_CURRENT_MA`) until the mark is off — then run at about half of that.
3. **Load test:** repeat with a real bobbin, wire and your normal tension.
4. **Listen:** a slip sounds like a sharp clack or growl instead of a smooth whine.

## Project layout

```
platformio.ini        board, libraries (TMCStepper, TeensyStep4 pinned), monitor settings
include/config.h      pins, driver/motor constants, limits and defaults
src/main.cpp          serial command interface, progress display, fault polling
src/tmc_driver.*      TMC2209 UART setup, readback verification, status and faults
src/winder.*          exact-count winding on top of TeensyStep4, pulse check, run summary
```

All tunables (pins, current, microsteps, limits, defaults) are in `include/config.h`.

## Known limitations

- **Open loop:** a mechanical slip can't be detected automatically — StallGuard doesn't work in spreadCycle. A once-per-turn spindle sensor would add this (see [TBD.md](TBD.md)).
- **TeensyStep4 is experimental** (v0.0.3); it is pinned to a reviewed commit, and its edge cases are worked around in `src/winder.cpp`.
- **Speed is fixed per run** — RPM and turns are set before `start` and can't change mid-run (by design).
