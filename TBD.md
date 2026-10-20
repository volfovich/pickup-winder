# Next steps — Step 2: wire traverse + front panel UI

Step 1 (spindle with exact turn counting) is done — see [README.md](README.md).
Step 2 adds a wire traverse that lays the wire in even layers, and a standalone front panel. It's split so the traverse can be built and tested over serial while the UI parts are on order.

## Phase A — Traverse hardware (parts on hand)

- **Motor / rail:** NEMA 11 on a 100 mm rail with a **1 mm lead screw**. At 16 microsteps that's **3200 steps/mm = 0.31 µm per step** — far finer than any pickup wire.
- **Driver:** the spare **TMC2209** (not the A4988):
  - shares the existing UART line — set **address 1** by tying its MS1 to 3.3 V (MS2 to GND);
  - **stealthChop** — quiet, ideal for slow traverse moves (the spindle stays in spreadCycle);
  - configured and verified by readback like the spindle driver.
- **Homing:** a **microswitch endstop** (~$1). Sensorless homing is not suitable with a 1 mm lead screw: the screw's mechanical advantage hides the stall from the motor and could jam the nut.

### Pins (Teensy 4.0)

Step 1 pins unchanged: 0/1 UART, 2 spindle STEP, 3 spindle DIR, 4 spindle EN.

| Function | Pin |
|---|---|
| Traverse STEP | 6 |
| Traverse DIR | 7 |
| Traverse EN | 22 (separate from the spindle) |
| Endstop (to GND, internal pull-up) | 21 |
| *Reserved for Phase C:* TFT (SPI) | 8 RST, 9 DC, 10 CS, 11 MOSI, 12 MISO, 13 SCK |
| *Reserved:* encoder A / B / push | 15 / 16 / 17 |
| *Reserved:* START / STOP buttons | 18 / 19 |
| *Reserved:* E-STOP sense | 20 |

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

- [ ] `src/traverse.*` — follower interrupt; ramped moves for homing, jog and go-to-start; layer/position readout
- [ ] `src/tmc_driver.*` — refactor for two drivers: address 0 spindle (spreadCycle), address 1 traverse (stealthChop, own current); same verification and fault watch for both
- [ ] `src/machine.*` — one state machine (Idle → Homing → Positioning → Winding → Paused / Done / Fault) and one command API shared by serial and the future UI
- [ ] Pause / resume — pause is a ramped stop; resume continues the remaining turns; the total count stays exact across pauses
- [ ] Settings saved in EEPROM — turns, RPM, accel, direction, wire diameter, bobbin width, start offset, traverse start direction
- [ ] Serial commands — `home`, `jog ±mm`, `goto mm`, `wire 0.071`, `width 12.5`, `start-offset 3.2`, `pause`, `resume`

### Tests

- [ ] Home 10× in a row — the position repeats
- [ ] Wind without wire; measure the carriage at layer reversals (calipers / dial indicator)
- [ ] `stop` mid-layer, then `resume` — the carriage continues from where it was
- [ ] Count and driver pulse check stay `OK` on both axes at 1500 RPM

## Phase C — Front panel UI (parts to buy)

### Parts

- [ ] **2.8" ILI9341 SPI colour TFT** (~$10) — library `ILI9341_t3n` (Teensy-optimised, frame buffer, no flicker)
- [ ] **Rotary encoder** with push button
- [ ] **Two large arcade buttons** — START/PAUSE (green), STOP (red)
- [ ] **Mushroom E-stop** that pulls both drivers' EN pins high **in hardware**, plus a sense wire to pin 20 so the firmware knows. Software must not be the only E-stop.

### Screens

- **Run** — huge turn counter, target, actual RPM, layer n/N, carriage position bar, progress bar
- **Setup** — turns, RPM, accel, direction, wire, width, start offset (encoder editing)
- **Jog / Home** — set the bobbin start position by eye
- **Presets** *(later)* — saved recipes, e.g. "Strat neck – 42AWG – 8000T"

Drawing happens in the main loop; both motors are stepped by timer interrupts, so the display can't disturb stepping or the count.

## Later / ideas

- Scatter winding (vary pitch or turnaround points per layer, for tone)
- Presets / recipes
- Once-per-turn spindle index sensor (hall or optical) for real-time mechanical skip detection
- Patch TeensyStep4's timer to a finer prescaler for closer-to-setpoint RPM (e.g. ~1199.8 instead of 1204.0)

## Open questions

- [ ] Does the rail already have an endstop, or does one need to be added?
- [ ] NEMA 11 rated current (from its label) — plan to run it at ~60–70% of rating
- [ ] Confirm the UI parts above, then write an exact shopping list
