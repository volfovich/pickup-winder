# Next steps — Step 2: wire traverse + front panel UI

Step 1 (spindle with exact turn counting) is done — see [README.md](README.md).
Step 2 adds a wire traverse that lays the wire in even layers, and a standalone touch front panel. It's split so the traverse can be built and tested over serial first, then the UI added on top.

## Phase A — Traverse hardware (parts on hand)

- **Motor / rail:** **28T0601-50** miniature linear actuator slide — NEMA 11 (28 mm) stepper, T6 lead screw with **1 mm lead**, **50 mm effective stroke**, **0.6 A per phase**.
  - At 16 microsteps: **3200 steps/mm = 0.31 µm per step** — far finer than any pickup wire.
  - Pickup bobbins are ~7–15 mm wide, so 50 mm of travel is plenty. Soft travel limits 0–50 mm after homing.
- **Driver:** the spare **TMC2209** (not the A4988):
  - shares the existing UART line — set **address 1** by tying its MS1 to 3.3 V (MS2 to GND);
  - **stealthChop** — quiet, ideal for slow traverse moves (the spindle stays in spreadCycle);
  - **400 mA RMS** (~67% of the 0.6 A rating — stays cool, plenty of force through a 1 mm screw), as a config constant;
  - configured and verified by readback like the spindle driver.
- **Homing:** the slide has no endstop, so add the **3-wire mechanical endstop module** (on hand — the common 3D-printer type with VCC / GND / SIG, a pull-up resistor and an LED). Sensorless homing is not suitable with a 1 mm lead screw: the screw's mechanical advantage hides the stall from the motor and could jam the nut.
  - **Power it from 3.3 V, not 5 V** — Teensy 4.0 pins are not 5 V tolerant, and the module pulls SIG up to its VCC.
  - Mount it so it triggers ~1 mm **before** the slide's mechanical hard stop, at the home end (default: the **motor end = 0 mm**; configurable).
  - Wire it so the switch is **normally closed** if the module allows: a broken wire then reads as "triggered" and homing fails safe instead of driving into the hard stop.
  - Signal polarity differs between modules, so it's a config constant, checked with an `endstop` status command before the first homing.
  - Homing sequence: fast approach → back off 1 mm → slow re-approach → set 0. Repeatability check: home 10× in a row.

### Pins (Teensy 4.0)

Step 1 pins unchanged: 0/1 UART, 2 spindle STEP, 3 spindle DIR, 4 spindle EN (5 optional loopback).

| Function | Pin |
|---|---|
| Traverse STEP / DIR / EN | 6 / 7 / 22 |
| Endstop SIG (module VCC → 3.3 V, GND → GND) | 21 |
| TFT SCK / MOSI / MISO — shared with touch T_CLK / T_DIN / T_OUT | 13 / 11 / 12 |
| TFT CS / DC / RST | 10 / 9 / 8 |
| Touch T_CS / T_IRQ | 14 / 15 |
| SD card CS *(reserved — presets/logs later)* | 16 |
| TFT LED (backlight) | **3.3 V directly** — it can draw 50–100 mA, too much for a Teensy pin (dimming would need a transistor) |

Free: 17, 18, 19, 20, 23 (no hardware buttons — the UI is touch only).

The display's full wiring, in the module's pin order, is in [UI.md](UI.md#2-hardware-and-wiring). The SD card socket has its own pins on the other side of the module (SD_CS → 16 when used, SD_MOSI / SD_MISO / SD_SCK → 11 / 12 / 13).

## Phase B — Traverse firmware (testable over serial)

**Core idea: the traverse position is a fixed function of the spindle's step count**, not an independently timed motion.

```
pitch       = wire diameter (+ optional spacing %)   e.g. 0.071 mm for 42 AWG
turnsLayer  = floor(bobbinWidth / pitch)
t           = spindleSteps / 3200                  (exact, fractional turns)
layer       = floor(t / turnsLayer)
u           = t − layer · turnsLayer
x           = start + (layer even ? u · pitch : width − u · pitch)
```

- A **40 kHz timer interrupt** (PIT / IntervalTimer — a different timer from TeensyStep4's TMR) reads the spindle's live step count, computes `x` in integer math, and steps the traverse toward it. The fastest traverse rate needed is ~5,700 steps/s (42 AWG at 1500 RPM = 1.8 mm/s).
- **No TeensyStep4 changes** — the spindle position is already readable.

Why this design:
- the traverse follows the spindle's ramps automatically, so wire spacing is identical at any speed;
- after a stop/resume (or a wire break) the traverse returns exactly to where the wire belongs, because its position comes from the turn count;
- the turn count is untouched — the spindle stays the only master;
- scatter winding later is just a different `x(t)` function.

### Work items

- [ ] `src/traverse.*` — follower interrupt; ramped moves for homing, jog and go-to-start; soft limits 0–50 mm; layer/position readout
- [ ] `src/tmc_driver.*` — refactor for two drivers: address 0 spindle (spreadCycle, 1500 mA), address 1 traverse (stealthChop, 400 mA); same verification and fault watch for both
- [ ] `src/machine.*` — one state machine (Idle → Homing → Positioning → Winding → Paused / Done / Fault) and one command API shared by serial and the UI
- [ ] Pause / resume — pause is a ramped stop; resume continues the remaining turns; the total count stays exact across pauses (also needed by the UI — see [UI.md §6](UI.md#6-firmware-change-needed-pause--resume))
- [ ] Settings saved in EEPROM — turns, RPM, accel, direction, wire diameter, bobbin width, start offset, traverse start direction
- [ ] Serial commands — `home`, `jog ±mm`, `goto mm`, `wire 0.071`, `width 12.5`, `start-offset 3.2`, `pause`, `resume`

### Tests

- [ ] Home 10× in a row — the position repeats
- [ ] Wind without wire; measure the carriage at layer reversals (calipers / dial indicator)
- [ ] `stop` mid-layer, then `resume` — the carriage continues from where it was
- [ ] Count and driver pulse check stay `OK` on both axes at 1500 RPM

## Phase C — Touch front panel UI

**Full plan: [UI.md](UI.md).** In short:

- **Hardware (on hand, wired):** 2.8" ILI9341 SPI TFT with **HR2046 resistive touch** (XPT2046-compatible) and SD card socket. **No hardware buttons** — touch only; the 24 V power switch is the emergency off.
- **Libraries (bundled with Teensyduino):** `ILI9341_t3`, `XPT2046_Touchscreen`, `EEPROM`.
- **Screens:** Idle, Setup, Keypad, Running (huge counter + huge STOP), Paused (RESUME / FINISH), Done, Fault, Driver, Touch calibration; after Phase B: Bobbin, Jog / Home; later: Presets.
- **Phases UI-1 … UI-7** — from display bring-up to presets; UI-1 to UI-5 don't need the traverse.

## Later / ideas

- Scatter winding (vary pitch or turnaround points per layer, for tone)
- Presets / recipes on the SD card
- Once-per-turn spindle index sensor (hall or optical) for real-time mechanical skip detection
- Patch TeensyStep4's timer to a finer prescaler for closer-to-setpoint RPM (e.g. ~1199.8 instead of 1204.0)

## Open questions

- [ ] Which end of the slide should be home? (Default: motor end = 0 mm.)
- [x] Endstop: none on the slide — adding the 3-wire mechanical endstop module (on hand), powered from 3.3 V
- [x] Touch: confirmed — T_IRQ / T_OUT / T_DIN / T_CS / T_CLK pins, HR2046 (XPT2046-compatible) controller
- [x] Traverse motor current: 0.6 A/phase → run at 400 mA RMS
- [x] Screen: 2.8" ILI9341 + HR2046 touch (on hand)
