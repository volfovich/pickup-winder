# Scatter winding

Scatter winding gives every layer of the coil its own **pitch** (spacing between turns) and its own **turnaround points** near the flanges, instead of laying identical, tightly packed layers. The variation is pseudo-random but comes from a **seed**: the same seed winds exactly the same coil again.

**Status:** implemented and host-tested (`test/traverse_math_host.cpp`), **not yet tried on hardware**. The pending hardware tests are at the end. Scatter is **off by default**.

Related: the guide hardware, even-layer winding and the run flow are in [TRAVERSE.md](TRAVERSE.md); the Scatter screen is in [UI.md](UI.md#scatter); the roadmap is in [TBD.md](TBD.md).

## Why scatter-wind

A machine-wound coil with even layers is very regular. Each turn lies right next to the previous one, and each layer sits in the grooves of the one below. Two things follow from that:

- **Self-capacitance is high.** Turns far apart in the winding order (e.g. layer 1 and layer 2 at the same spot) lie right against each other, separated only by the insulation. Together with the coil's inductance, that capacitance sets the pickup's resonant peak.
- **The wire piles up at the flanges.** Every layer reverses at the same place, so the ends build up "shoulders".

Traditional hand-guided pickups are irregular: the winder's hand moves at varying speed and turns around at varying points. Pickup makers use the term "scatter winding" for that irregularity. It is commonly credited with lower self-capacitance and a brighter, more open tone. That is general pickup-making practice; this machine has not been measured yet.

This machine copies the two irregularities in a controlled way:

| | What varies | Effect on the coil |
|---|---|---|
| **Pitch scatter** (P %) | the spacing of the turns, once per layer | layers don't nest in each other's grooves; air gaps between turns; layers cross each other at slightly different angles |
| **End scatter** (E mm) | where each layer turns around | the reversals are spread over a band instead of one line: no shoulders at the flanges, tapered coil ends |

Both are optional and work independently: P = 0 gives end scatter only, E = 0 gives pitch scatter only, and with both 0 you get even layers over the full room between the margins.

## The settings

| Setting | Range | Default | Meaning |
|---|---|---|---|
| Scatter on / off | | off | off = the even-layer formula from [TRAVERSE.md](TRAVERSE.md#how-it-works), unchanged |
| **Pitch +% (P)** | 0–100 | 30 | each layer's pitch is drawn from wire × 1.00 … wire × (1 + P/100), in 0.1 % steps |
| **Ends mm (E)** | 0–3.0 (0.1 mm steps) | 0.5 | each turnaround lands somewhere from 0 to E **inside** the margin, on its side |
| **Seed** | 0–99999 | 1 | picks the pattern; **NEW SEED** / `scatter seed new` picks a random, different one |

Limits and defaults are in `include/config.h` (`MAX_PITCH_SCATTER_PCT`, `MAX_END_SCATTER_UM`, `MAX_SCATTER_SEED`, `MAX_SCATTER_LAYERS`, `DEFAULT_…`).

The pattern also depends on the ordinary guide settings (the Bobbin screen):

- **wire diameter** from AWG + insulation (`WIRE_SIZES`, e.g. 42 AWG single = 0.071 mm). This is the smallest pitch scatter will ever use;
- **flange 1, inner width, margin**, which give the winding area `near = flange 1 + margin` … `far = flange 2 − margin`;
- **first layer away / toward** home;
- **turns**, which set how many layers are generated.

## Geometry

Rail positions grow to the right (home = the motor end is to the left). With the first layer **away**:

```
 flange 1                                                         flange 2
   |<-margin->|                                             |<-margin->|
   |          near                                       far          |
   |          |<-- E -->|                         |<-- E -->|          |
   |          [ near band ]                       [ far band  ]        |
   |          |         |                         |         |          |
   |             T0 o----------- layer 0 ------------>o T1             |
   |                T2 o<---------- layer 1 ----------                 |
   |           T2 o------------- layer 2 ----------------->o T3        |
   |                    T4 o<----------- layer 3 ----------            |
   |                       ...                                         |
```

- **Turnaround T*i*** is where layer *i* starts. Layer *i* runs from T*i* to T*i+1*, so every layer ends exactly where the next one begins.
- Even-numbered turnarounds lie in the **near band** `[near, near + E]` and odd ones in the **far band** `[far − E, far]` (the other way round with the first layer **toward** home).
- The wire never goes outside `near … far`: the margins stay clear, as with even layers.
- Unlike even winding, a scatter layer always runs across the full room between the bands. Even winding uses `N × wire` from `near`, which can stop a fraction of a pitch short of `far`.

## The algorithm

Everything is in `src/traverse_math.h`. It is pure integer maths with no Arduino dependencies, so the Teensy and the host test compile the same code.

### 1. Units

- Lengths are converted from µm to **traverse steps**: 3200 steps/mm (`TRAVERSE_STEPS_PER_MM`, 0.31 µm per step), rounded to the nearest step.
- The wire pitch becomes `wireQ`: traverse steps per spindle turn in **Q16** fixed point (×65536), so a pitch of 0.071 mm is 227.2 steps, kept with a fraction of 1/65536 step.
- The spindle makes 3200 steps per turn (`STEPS_PER_REV`).

### 2. Random numbers from the seed

```cpp
hash32(x)                       // lowbias32: xor-shift / multiply integer hash
scatterRandom(seed, i, k, range)
    h = hash32( hash32(seed) ^ (i*4 + k) * 0x9e3779b9 )
    return (h * (range + 1)) >> 32       // 0 .. range inclusive
```

- **Stateless:** the number for layer *i* doesn't depend on any number drawn before it. There is no generator state to carry and nothing that could differ between the Teensy and the host. Any layer can be computed alone, in any order.
- `i` is the layer / turnaround index. `k` keeps the separate random streams apart: **k = 0** for turnarounds, **k = 1** for pitches (`i*4 + k` leaves room for two more). Multiplying by the golden-ratio constant `0x9e3779b9` spreads neighbouring indices over the whole 32-bit range before the second hash.
- **0 … range** comes from a 64-bit multiply and taking the top 32 bits instead of a `%`. The distribution is uniform to within (range + 1) / 2³², which is negligible.

### 3. Turnarounds

```cpp
r = scatterRandom(seed, i, 0, endSteps)          // 0 .. E, in steps
T(i) = near side ? near + r : far − r
near side  <=>  (i is even) == firstAway
```

So the turnarounds alternate sides, and each one lands uniformly anywhere in its band, at a resolution of one traverse step (0.31 µm).

### 4. Each layer's pitch and turn count

For layer L, from `T(L)` to `T(L+1)`:

```cpp
span   = |T(L+1) − T(L)|                                            // steps
pitch  = wire × (1000 + scatterRandom(seed, L, 1, P × 10)) / 1000   // the target
n      = floor(span / pitch), at least 1                            // turns in this layer
actual = span / n                                                   // the pitch actually wound
```

- The target pitch is uniform between wire × 1.000 and wire × (1 + P/100), in steps of 0.1 %.
- The turns are **rounded down**, so the layer is wound at `actual = span / n`. That is slightly **coarser** than the target, never finer. As a result **the pitch never goes below the wire diameter**, so the turns never land on top of each other within a layer.
- Rounding down by at most one turn means `actual < 2 × target` (`actual` is usually only a fraction of a percent above the target). `config.h` has a static_assert that even 2 × the coarsest possible pitch needs fewer than one guide step per spindle step.
- The guide moves continuously across a layer: it travels `actual` per spindle turn, interpolated per spindle step (section 6). So a layer is a steady helix at its own pitch, not stepped turn by turn.

### 5. Building the table

`makeScatter()` repeats step 4 layer after layer until the layers hold at least the set turns:

```cpp
turn = 0; from = T(0)
for L = 0, 1, 2, ... while turn < turns:
    to = T(L + 1)
    n  = (step 4)
    table[L] = { startTurn = turn, fromSteps = from, toSteps = to }
    turn += n; from = to
table[layers] = { turn, from, from }        // closing entry: ends the last layer
```

- A `Layer` is 12 bytes: the turn the layer starts at and the guide positions at its two ends.
- The **last layer is usually only partly wound**: the run stops at the set turn count, somewhere across it.
- In the same pass it collects `ScatterStats`: the layer count, the fewest and most turns in a layer, and the largest actual pitch (rounded up to whole µm).
- **Preview mode:** called with `p` and `table` both `nullptr`, it computes only the stats. The Bobbin / Scatter screens and the serial `traverse` printout use this. It runs the identical code, so **the numbers on the screen are those of the coil that will be wound**. The host test checks that the preview equals the real table.
- **Refusals** (returns false): the area `far − near` is not wider than `2 × E`, or more than `maxLayers` (4096) layers would be needed. `app.cpp` checks more strictly first (`endScatterTooLarge()`: width ≤ 2 × margin + 2 × E + 2 × wire), so at least two wire diameters of room remain between the bands.
- `Pattern` is filled for the step ISR: `table`, `tableLayers`, `spindleStepsPerTurn`, plus `startSteps = near`, `pitchQ = wire`, `turnsPerLayer = most turns in a layer` for code that expects even-layer fields.

### 6. Following the spindle (the step ISR)

During a run, `traverse.cpp`'s 40 kHz interrupt (`tickFollow()`) reads the spindle's live step count `S` and calls `traverse_math::position(pattern, S)`. With a table, that is:

```cpp
turn = S / 3200
L    = the last layer with table[L].startTurn <= turn     // binary search
n    = table[L+1].startTurn − table[L].startTurn
u    = S − table[L].startTurn × 3200                       // spindle steps into the layer
x    = from + u × (to − from) / (n × 3200)                 // linear interpolation
```

- The binary search takes at most 12 iterations for 4096 layers, which is cheap at 40 kHz.
- If `S` ran past the table's end, `u` is clamped and the guide stays at the last point.
- The ISR then makes at most one step toward `x`. The host test checks at every spindle step that `x` never changes by more than one step, so the guide keeps up exactly (the bring-up lag was 1 microstep).
- **The guide position is still a fixed function of the spindle step count**, as with even layers. So everything that relies on that keeps working: ramps, pause / resume, E-STOP, a wire break, both pulse checks, and the **layer n / N** on the Running screen (`layerOf` / `layerCount` use the same table).

### 7. Where it runs and memory

- `app.cpp` keeps `scatterTable[MAX_SCATTER_LAYERS + 1]`: 4097 × 12 bytes ≈ **48 KB** of RAM.
- It is written **only at START** (`makeRunPattern()`), before the guide moves to `position(pattern, 0)` = T(0). After that only the ISR reads it. All scatter settings are locked during a run (`BUSY`), so the table can't change under the ISR.
- START also runs `checkTraverse()`, whose guide-speed check (≤ 6 mm/s, `TRAVERSE_MAX_FOLLOW_UM_S`) uses the **largest actual pitch** in the table rather than the wire diameter.

## A worked example

The defaults: 42 AWG single (0.071 mm), flange 1 at 5.0 mm, inner width 12.0 mm, margin 1.0 mm, so `near` = 6.0 mm and `far` = 16.0 mm, 10 mm of room. P = 30 %, E = 0.5 mm, first layer away, 8000 turns. (Computed with `traverse_math.h` on the host.)

**Seed 1**: 67 layers, 103–134 turns/layer, max pitch 93 µm.

| Layer | Start turn | From → to (mm) | Span (mm) | Target pitch (µm) | Turns | Actual pitch (µm) |
|---|---|---|---|---|---|---|
| 0 | 0 | 6.174 → 15.742 | 9.568 | 78.8 | 121 | 79.08 |
| 1 | 121 | 15.742 → 6.492 | 9.249 | 76.7 | 120 | 77.08 |
| 2 | 241 | 6.492 → 15.756 | 9.263 | 79.2 | 116 | 79.86 |
| 3 | 357 | 15.756 → 6.360 | 9.396 | 83.2 | 112 | 83.89 |
| 4 | 469 | 6.360 → 15.577 | 9.217 | 74.1 | 124 | 74.33 |
| 5 | 593 | 15.577 → 6.397 | 9.180 | 79.8 | 115 | 79.83 |
| 6 | 708 | 6.397 → 15.820 | 9.423 | 75.8 | 124 | 75.99 |
| 7 | 832 | 15.820 → 6.066 | 9.754 | 85.1 | 114 | 85.56 |
| … | | | | | | |
| 66 (last) | 7915 | | | | 106 | only 85 wound |

Things to see in it:
- Near turnarounds fall between 6.0 and 6.5 mm, far ones between 15.5 and 16.0 mm.
- Each layer's actual pitch is a little above its target and never below 71 µm.
- Turns per layer vary with both the span and the pitch.

**Seed 4711**, same bobbin: 70 layers, 102–132 turns/layer. Its first layers are 110, 108, 107, 120, 122 … turns, a completely different coil.

**Compared with even winding** on the same bobbin: 140 turns/layer, 58 layers. Scatter puts fewer turns in a layer on average, so the **coil gets taller** for the same turn count.

How P and E change it (same bobbin, 8000 turns, seed 1):

| | E = 0 | E = 0.5 mm | E = 1.5 mm |
|---|---|---|---|
| **P = 0 %** | 58 layers, 140 turns/layer (even) | 61 layers, 126–140 | 68 layers, 99–138 |
| **P = 10 %** | 60 layers, 128–140 | 63 layers, 117–137 | 70 layers, 92–135 |
| **P = 30 %** | 64 layers, 108–140 | **67 layers, 103–134** | 75 layers, 80–130 |
| **P = 60 %** | 70 layers, 88–140 | 74 layers, 84–133 | 83 layers, 67–130 |
| **P = 100 %** | 78 layers, 71–140 | 82 layers, 68–132 | 92 layers, 55–127 |

The largest pitch rises with P: about 72 µm at P = 0, 79 at 10 %, 93 at 30 %, 114 at 60 % and 143 at 100 %. At 1200 RPM even 143 µm/turn is only 2.9 mm/s of guide speed, well under the 6 mm/s limit.

## Properties worth knowing

- **Repeatable:** the same seed with the same wire, bobbin (flanges, width, margin), first-layer direction, P and E winds the identical coil, e.g. the second pickup of a matched pair. Store the whole set as a [preset](UI.md#presets).
- **The turn count only adds or removes layers at the end.** The random draws depend only on the seed and the layer index, so 8000 and 10000 turns with the same seed share their first 67 layers exactly (checked on the host). The bigger coil is the smaller one plus more layers on top.
- **The bobbin geometry changes the coil, but the draws stay the same.** Changing the wire, width or margin moves the bands and changes every layer's turn count, even with the same seed. A "matched pair" needs the same Bobbin settings too.
- **Spindle direction doesn't matter.** CW / CCW (e.g. a reverse-wound pickup) winds the same guide pattern.
- **Seed 0 is a valid seed**, like any other.
- **Not random within a layer.** The pitch is constant across one layer, and the guide reverses abruptly at each turnaround, as with even layers. The randomness is per layer only.
- **Uniform distributions.** Turnarounds are uniform across their band and pitches uniform across their range, with no clustering or bias toward the middle.
- **Taller coils.** Expect roughly 5–60 % more layers than even winding, depending on P and E (the table above). Make sure the finished coil still fits under the flanges' rim / cover.

## Using it

### On the screen

**Setup → BOBBIN → SCATTER** (locked during a run):

```
| [ Scatter      ON ]   [ Pitch +%      30 ]       |
| [ Ends mm     0.5 ]   [ Seed        4711 ]       |
|  Each layer: pitch 0.071 - 0.092 mm, turns up    |
|  to 0.5 mm inside the margins.                   |
|        91 - 122 turns/layer, 104 layers          |
|  [     BACK     ]          [   NEW SEED   ]      |
```

- The two description lines restate the settings in real units: the target pitch range and the end band.
- The summary line is the **preview**: the fewest and most turns in a layer and the layer count for the set turns. If it doesn't fit, the line is red with the reason.
- Idle's bobbin line ends in ", scatter" while scatter is on, and Running shows layer n / N from the table.
- Screen details: [UI.md](UI.md#scatter).

### Serial

```
scatter on | off
scatter pitch 30        # P, 0..100 %
scatter ends 0.5        # E, 0..3 mm (rounded to 0.1)
scatter seed 4711       # 0..99999
scatter seed new        # a random seed, never the current one
traverse                # prints the settings, incl. "scatter ON: ..." and "winds ... : 91-122 turns/layer, 104 layers"
```

After a run the summary prints `scatter seed N, L layers`. The settings are saved in EEPROM (settings version 4) and in presets.

### Errors (red on the Bobbin / Scatter screens, and refused at START)

| Message | Cause | Fix |
|---|---|---|
| `end scatter too large for this bobbin` | inner width ≤ 2 × margin + 2 × E + 2 × wire | smaller E or margin |
| `scatter: too many layers (max 4096)` | very narrow room and/or a large P with many turns | wider room, smaller P or E, fewer turns |
| `guide too slow for N rpm (max 6 mm/s)` | largest pitch × RPM / 60 is over 6 mm/s | lower RPM or P (unlikely with pickup wire) |

### Choosing values

- **Start from the defaults** (P = 30 %, E = 0.5 mm). They give a clearly irregular coil with about 15 % more layers than even winding on a typical 12 mm bobbin.
- **P** sets how loose the coil is. Small P (5–15 %) is close to even winding with some de-nesting; large P (60–100 %) is loose and builds a tall coil fast.
- **E** sets how strongly the coil ends taper. Keep it small compared with the room (0.3–1 mm on a 10 mm room). A large E shortens many layers and adds layers.
- **Seed:** once you like a coil, note the seed (or save a preset) to repeat it.

## Verifying

Host test: it builds and checks the table and every spindle step for 7 scatter cases (up to 11,000 turns, 56–76 µm wire, 1–44 mm of room, P 0–100 %, E 0–3 mm), plus the two refusals:

```
c++ -std=c++17 -O2 -Isrc test/traverse_math_host.cpp -o /tmp/tm && /tmp/tm
```

It checks that:
- the layers hold the turns;
- every turnaround lies in its band;
- each layer ends where the next one starts;
- every layer has at least one turn and the pitch is never below the wire;
- the stats' max pitch bounds every layer;
- at every spindle step the guide moves at most one step and stays inside the area;
- the guide sits exactly on the turnaround at each layer boundary;
- layers advance one at a time, and the layer count is right;
- the preview stats equal the table's;
- the same seed gives the same table, and another seed gives another one.

Still to do on hardware (from [TRAVERSE.md](TRAVERSE.md#tests-on-hardware)):
- [ ] Scatter dry winds at 500 and 1200 RPM (+30 %, 0.5 mm): pulse checks `OK`, max lag ≤ 2 microsteps, turnarounds vary inside the margins; `stop` / `resume` mid-layer; the same seed twice ends at the same position
- [ ] Scatter wind with wire
