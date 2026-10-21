# Wire traverse

The wire guide lays the wire in even layers across the bobbin.

**Status:** **wired and brought up on hardware** (checklist below, all 9 steps). Homing repeats, jogs measure 1.00 mm, and dry winds at 300 and 1500 RPM end exactly at the pattern's position with both pulse checks `OK` and a maximum guide lag of 1 microstep, also across six STOP / RESUMEs in one run. Still to do: a real wind with wire, to check that the layers lie flat. The guide is **off by default** (`traverse off`): the machine winds as before until it's switched on.

Overall roadmap: [TBD.md](TBD.md). Spindle and serial console: [README.md](README.md). Touch UI: [UI.md](UI.md).

## Hardware (parts on hand)

- **Motor / rail:** **28T0601-50** miniature linear actuator slide: a NEMA 11 (28 mm) stepper, T6 lead screw with **1 mm lead**, **50 mm effective stroke**, **0.6 A per phase**.
  - At 16 microsteps: **3200 steps/mm = 0.31 µm per step**, far finer than any pickup wire.
  - Pickup bobbins are ~7–15 mm wide, so 50 mm of travel is plenty. Soft travel limits 0–50 mm after homing.
- **Driver:** the spare **TMC2209** (not the A4988):
  - shares the existing UART line; **address 1** by tying its MS1 to 3.3 V (MS2 to GND);
  - **spreadCycle**: the guide reverses at full speed at every layer end, and stealthChop copes worse with sudden speed changes. A missed traverse step would shift every later layer, so reliability wins over noise. `TRAVERSE_STEALTHCHOP` in `config.h` switches it;
  - **400 mA RMS** (~67% of the 0.6 A rating: stays cool, plenty of force through a 1 mm screw), hold 50%;
  - configured and verified by readback like the spindle driver.
- **Homing:** the slide has no endstop, so it gets the **3-wire mechanical endstop module** (on hand: the common 3D-printer type with VCC / GND / SIG, a pull-up resistor and an LED). Sensorless homing doesn't suit a 1 mm lead screw: the screw's mechanical advantage hides the stall from the motor and could jam the nut.
  - **Power it from 3.3 V, not 5 V.** Teensy 4.0 pins are not 5 V tolerant, and the module pulls SIG up to its VCC.
  - **Home is the motor end = 0 mm.** Mount the switch so it triggers ~1 mm **before** the slide's mechanical hard stop.
  - Wire the switch **normally closed** if the module allows: a broken wire then reads as "triggered", and homing fails safe instead of driving into the hard stop.
  - Signal polarity differs between modules: `ENDSTOP_TRIGGERED_LEVEL` in `config.h`, checked with the `endstop` command before the first homing. The module on this build pulls SIG **low** when pressed (`0`).

### Pins (Teensy 4.0)

Spindle pins unchanged: 0/1 UART, 2 STEP, 3 DIR, 4 EN (5 optional loopback).

| Function | Pin |
|---|---|
| Traverse STEP / DIR / EN | 6 / 7 / 22 |
| Endstop SIG (module VCC → 3.3 V, GND → GND) | 21 |
| TFT SCK / MOSI / MISO, shared with touch T_CLK / T_DIN / T_OUT | 13 / 11 / 12 |
| TFT CS / DC / RST | 10 / 9 / 8 |
| Touch T_CS / T_IRQ | 14 / 15 |
| SD card CS *(reserved: presets / logs later)* | 16 |
| TFT LED (backlight) | **3.3 V directly**: it can draw 50–100 mA, too much for a Teensy pin |

Free: 17, 18, 19, 20, 23 (no hardware buttons: the UI is touch only).

The display's full wiring, in the module's pin order, is in [UI.md](UI.md#2-hardware-and-wiring). The SD card socket has its own pins on the other side of the module (SD_CS → 16 when used, SD_MOSI / SD_MISO / SD_SCK → 11 / 12 / 13).

### Wiring (step by step)

1. **Power off** (24 V and USB).
2. **Traverse TMC2209 address 1:** MS1 → **3.3 V**, MS2 → **GND**. On BTT-style modules these are the MS1 / MS2 pins (or solder jumpers). The spindle driver stays at MS1 = MS2 = GND (address 0).
3. **UART:** connect the traverse driver's **PDN_UART** to the **same node** as the spindle driver's: Teensy RX1 (pin 0), and through the existing 1 kΩ to TX1 (pin 1). One line, two drivers.
4. **Logic:** VIO → 3.3 V, GND → GND. **STEP → 6, DIR → 7, EN → 22.** **Recommended: 10 kΩ from EN to 3.3 V**, so the driver stays off while the Teensy is unpowered, booting or uploading (see README, EN pull-up).
5. **Motor power:** VM → 24 V, GND → power GND, with a 100 µF capacitor at the driver if the module has none. **Never plug or unplug the motor with 24 V on.**
6. **Motor:** the 28T0601-50's 4 wires → A1 A2 / B1 B2 (one coil per pair; check with an ohmmeter).
7. **Endstop module:** VCC → **3.3 V** (not 5 V), GND → GND, SIG → **21**. Mount it at the **motor end**, triggering ~1 mm before the hard stop.

### Bring-up checklist (after wiring)

1. `drv`: both drivers answer; the traverse at address 1 says "configured and verified".
2. `endstop`, then press the switch by hand and `endstop` again: it must read "open", then "PRESSED". If it's the other way round, flip `ENDSTOP_TRIGGERED_LEVEL` in `config.h`.
3. `jog 1`: the carriage must move **away from the motor**. If it moves toward it, set `TRAVERSE_DIR_INVERT = true` (needed on this build).
4. `jog 1` / `jog -1` measured with calipers: 1.00 mm each.
5. Keep a hand near the 24 V switch: `home` (or HOME on the Guide screen). It approaches the switch, backs off, re-approaches slowly and parks at 1.0 mm. Repeat 10×: the position repeats.
6. `goto 25`, `goto 0.5`: smooth moves, no noise from the lead screw end. Try the jog buttons on the Guide screen.
7. **Teach the bobbin** (see below): mount a bobbin; on the Guide screen jog until the wire meets the inside of flange 1, tap **SET FLANGE 1**; jog to flange 2, tap **SET FLANGE 2**. Check with **GO TO FLANGE** (tap twice: flange 1, then flange 2) that the wire lines up with both. Set `awg`, `insulation`, `margin` on the Bobbin screen.
8. `traverse on`, then a dry wind (no wire) at 500 RPM, then at the top speed, 1200 RPM (the bring-up ran 300 and 1500, before the range was narrowed). Watch the reversals: they should stay one margin inside each flange. Both pulse checks `OK`.
9. `stop` mid-layer, then `resume`: the guide continues exactly where it was.

## How the bobbin is located

The bobbin sits on its mount on the winder motor. The wire comes from the tensioner over the two V-pulleys on the traverse carriage onto the bobbin. The firmware only knows the **carriage position on the rail** (mm from the home switch); it can't know where the bobbin is relative to the rail. So the bobbin is **taught**:

- **Flange 1** is the flange nearer the rail's home end; **flange 2** the other one.
- For each, jog the carriage until the wire from the pulleys meets the flange's **inner** face, and tap SET FLANGE 1 / 2. The stored value is that carriage position.
- The **inner width** follows: flange 2 − flange 1. It can also be typed if you know it (that moves flange 2).
- Setting flange 1 again keeps flange 2 where it was.
- Teach once per bobbin type / mount setup. Redo it only if the rail, the mount or the bobbin type changes.

With the **margin** kept clear inside both flanges, the guide winds from flange 1 + margin to flange 2 − margin.

## On the screen

Full screen descriptions: [UI.md](UI.md#bobbin).

- **Guide** (Idle → GUIDE): live carriage position, jog −10 … +10 mm, **HOME** (STOP while moving), **SET FLANGE 1 / 2** to teach the bobbin, **GO TO FLANGE 1 / 2** to check it.
- **Bobbin** (Setup → BOBBIN, or Guide → BOBBIN): guide on / off, first layer away / toward, AWG 42 / 43 / 44, single / heavy insulation, margin, inner width, the taught flanges, and the result: "0.071 mm wire: 140 turns/layer, 22 layers", or in red why it won't fit.
- **Idle** shows the bobbin to wind ("42 AWG, 12.0 mm, 22 layers"), and **HOME GUIDE!** in orange while the guide is on but not homed.
- **START** with the guide on first moves the guide to the start position (START becomes CANCEL), then starts the spindle.
- **Running** shows **layer n / N**; **Done** shows the spindle and guide pulse checks.
- **Diagnostics** (Setup → DIAGNOSTICS) shows the guide driver, homed state, position and the endstop; **GUIDE** opens the guide driver's registers.

## How it works

### The guide position is a function of the spindle's step count

It is not an independently timed motion. For spindle step count `S` (3200 per turn):

```
pitch  = wire diameter incl. insulation           from AWG + insulation, e.g. 0.071 mm
start  = flange + margin                           near edge of the winding
N      = floor((width − 2 · margin) / pitch)       turns per layer
layer  = S / (N · 3200)
u      = S mod (N · 3200)                          spindle steps into this layer
off    = u · pitch / 3200                          guide travel into this layer
x      = start + off                               layers moving away from home
x      = start + N · pitch − off                   layers moving toward home
```

- `flange` is flange 1's taught carriage position, `width` the inner width (flange 2 − flange 1), and `margin` is kept clear inside both flanges. The first layer's direction is the `layer away|toward` setting.
- `src/traverse_math.h` does this in integer maths: pitch in Q16 traverse steps per turn, 64-bit products. It has no Arduino dependencies, so the host simulation compiles the same file.
- **Host test** (11,000 turns, 7 cases from 20 µm wire on a 48 mm bobbin to 500 µm wire with 2 turns per layer):
  - reversals land exactly on `start` and `start + N · pitch`;
  - the guide never needs more than 1 step per spindle step;
  - the layer span is within 0.25 µm of N × wire.

Why this design:
- the guide follows the spindle's ramps automatically, so the wire spacing is the same at any speed;
- after STOP / RESUME, E-STOP or a wire break, the guide is exactly where the wire belongs, because its position comes from the turn count;
- the turn count is untouched: the spindle stays the only master;
- scatter winding later is just a different `x(S)` function.

### Step generation (`src/traverse.cpp`)

- **One 40 kHz `IntervalTimer` (PIT) interrupt** makes every traverse step: at most one step per tick (12.5 mm/s). It runs at NVIC priority 32, below the spindle's TeensyStep4 timer (16), so it can never delay a spindle step, and above USB / UART.
- Modes:
  - **Follow** (winding): each tick reads the spindle's live step count, computes `x`, and steps toward it. 42 AWG at 1500 RPM needs ~5,700 steps/s, 14% of the tick rate.
  - **Move** (homing, jog, go to start): a trapezoid profile in Q24 fixed point. 10 mm/s, 50 mm/s², start / end speed 0.5 mm/s.
  - **Idle.**
- Step pulse 200 ns (the TMC2209 needs ≥ 100 ns). After a DIR change, the step waits for the next tick.
- **Soft limits 0…50 mm are enforced inside the interrupt** once homed. In Follow mode a limit hit aborts the run.
- There is no TeensyStep4 change: the spindle's position counter is already readable.

### Homing (toward the motor end)

1. If the switch is already pressed: move 2 mm off it first. Still pressed → **"endstop stuck or wire broken"**.
2. Fast approach at 5 mm/s until the switch triggers, checked every tick. Nothing within 52 mm → **"endstop not found"**.
3. Back off 1 mm. Still pressed → **"endstop did not release"**.
4. Slow approach at 0.5 mm/s → that point is **0 mm**.
5. Park at 1.0 mm, clear of the switch (0.5 mm left this switch's lever resting on the carriage).

Before homing, `jog` works without limits but at most ±5 mm per command; `goto` and START need homing. Switching the traverse driver off (`release`, a re-init, a traverse fault) forgets the home position.

### A run with the traverse on

1. **START** checks the spindle (as without the traverse), then:
   - the traverse driver is ready and homed;
   - there is room between the margins, and the bobbin is on the slide (`flange + width ≤ 50 mm`);
   - the guide speed `wire × RPM / 60` is ≤ 6 mm/s;
   - the traverse driver has no fault.
2. The guide **moves to the start position** `x(0)`: flange 1 + margin when the first layer moves away from home, otherwise the far end of the winding area (flange 1 + margin + turns per layer × wire, at most flange 2 − margin). On the screen START becomes **CANCEL** meanwhile; serial `stop` cancels too.
3. The **spindle starts**, then the guide switches to Follow (the spindle has made at most one step by then).
4. Pause / resume / E-STOP need nothing extra: the guide holds while the spindle stands.
5. The **fault watch polls the two drivers alternately**, one driver (GSTAT + DRV_STATUS) per 100 ms tick, so the loop blocks no longer than with the spindle alone. A traverse fault or soft-limit hit aborts the run like a spindle fault; the Fault screen names it ("traverse: ...").
6. **The run summary** adds a **traverse pulse check** (MSCNT before / after against the net guide steps), the raw MSCNT values, and the guide's maximum lag in steps.
   - Both MSCNT reads wait **300 ms** for the guide to settle: after the move to the start position (before the spindle starts), and after the run (the Done screen shows "guide pulse check  ..." meanwhile). With `intpol` the driver spreads each step over the previous step interval, so a read right after a step can land between two microsteps. At rest at 16 microsteps MSCNT is always 8 + 16·k; the serial line marks other values "(off-grid)".
   - Found in bring-up: reading the start value straight after positioning gave a false MISMATCH (+0) while the guide had received every step.

### Settings and commands

Saved in EEPROM (settings version 3; older settings keep the spindle values, the guide goes to its defaults). Lengths are kept in 0.1 mm steps. Also on the Bobbin screen.

| Command | Setting | Range / default |
|---|---|---|
| `traverse on\|off` | use the guide for winding | off |
| `awg 42\|43\|44` | wire size | 42 |
| `insulation single\|heavy` | insulation build (sets the diameter with the AWG) | single |
| `flange1 MM\|here` | carriage position where the wire meets flange 1's inner face; `here` = the current position; keeps flange 2 | 0–48, default 5.0 |
| `flange2 MM\|here` | same for flange 2; sets the inner width | flange 1 + 2 … 50, default 17.0 |
| `width MM` | inner width, flange to flange (moves flange 2) | 2–48, default 12.0 |
| `margin MM` | kept clear inside both flanges | 0–5, default 1.0 |
| `layer away\|toward` | first layer direction | away |

Wire diameters including insulation (`WIRE_SIZES` in `config.h`; nominal, adjust to your wire): single build 42 = 0.071, 43 = 0.063, 44 = 0.056 mm; heavy build 42 = 0.076, 43 = 0.069, 44 = 0.061 mm.

Motion: `home`, `jog MM` (e.g. `jog -0.5`), `goto MM`. Status: `endstop`, `status` (homed, position, layer n / N during a run), `drv` (both drivers).

## Work items

- [x] `src/traverse.*`: step ISR with Move and Follow modes; homing with fail-safe checks; soft limits in the ISR
- [x] `src/traverse_math.h`: the position function, host-tested
- [x] `src/tmc_driver.*`: `tmc::spindle` (address 0) and `tmc::traverse` (address 1), same verification for both
- [x] Run flow in `src/app.*`: positioning → spindle start → follow; traverse faults abort the run; alternating fault watch
- [x] Pause / resume: done in UI-4; the guide needs nothing extra
- [x] Settings in EEPROM, serial commands, traverse pulse check and lag in the summary
- [x] Hardware bring-up (checklist above), 2026-10-21
- [x] UI-6: Guide, Bobbin and Diagnostics screens, flange teaching, layer n / N on Running, guide pulse check on Done (see [UI.md](UI.md))
- [ ] EN pull-ups (10 kΩ to 3.3 V) on both drivers

## Tests (on hardware)

- [x] Home 10× in a row: the position repeats
- [x] Wind without wire at 300 and 1500 RPM: the run ends at the pattern's position to the step (e.g. 1000 turns → 9.068 mm), maximum lag 1 microstep
- [ ] Measure the carriage at layer reversals (calipers / dial indicator)
- [x] `stop` mid-layer, then `resume`: the carriage continues from where it was (six pauses in one 1500 RPM run, end position and pulse checks exact)
- [x] Count and driver pulse checks stay `OK` on both axes at 1500 RPM
- [ ] Real wind with wire: the layers lie flat (else adjust the pitch in `WIRE_SIZES`)
