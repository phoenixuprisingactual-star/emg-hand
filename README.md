# EMG Hand — Phase 1 Firmware

**Status (2026-10-15): SKELETON, simulation-tested, NOT hardware-validated.**
A solo hobby build, shared publicly in case it is useful to other builders.
Phase 1 is a one-channel EMG bench claw; a tendon-driven hand is the
longer-term goal. Hardware state as of 2026-10-15: the controller board and
servo driver are in hand, the EMG sensor and electrodes arrive Oct 16-17, so
nothing in this repo has run on real hardware yet.
What was checked, and when: the control logic (`emg_control.h`) was compiled
with g++ and exercised by 9 host simulation cases / 33 checks, all passing, on
2026-10-12. The Arduino sketch wrapper (`emg_claw.ino`) has **not** been
compiled yet: this machine could not download the Arduino AVR core (TLS
failure to the package index), so the sketch's first compile happens in the
Arduino IDE on Renegade's Windows PC. The sketch uses only documented
Adafruit_PWMServoDriver calls, but treat the first IDE compile as a real
verification step, not a formality. No part of this firmware has run on the
real MyoWare, Uno, or servos. This banner must be re-earned after board day;
do not carry it forward on faith.

## Files

| File | What it is |
|---|---|
| `emg_claw/emg_claw.ino` | Arduino sketch: sampling, calibration prompts, servo driving, telemetry |
| `emg_claw/emg_control.h` | All control logic (state machine, thresholds, calibration math). No Arduino dependencies; the sim tests compile this exact file |
| `sim/test_emg_control.cpp` | Host tests: synthetic ENV traces — rest noise, flex plateaus, threshold chatter, calibration success/failure, rail fault, slew limit, angle clamp |
| `sim/run_tests.sh` | Builds and runs the tests (`g++ -std=c++17`) |

## Design (from `../claw-control-design-2026-10-11.md`)

Three states on one channel, two thresholds, **no classifier**:

| Signal | State | Claw |
|---|---|---|
| Rest | Open | open |
| Light flex | Closed | closes |
| Hard flex | Grip | grips tight |

Chatter control is hysteresis only. There is deliberately **no** reject gate
and **no** majority-vote window: that safety layer costs ~1.95 s of latency
and buys multi-gesture reliability Phase 1 does not need.

## Wiring assumptions (from `../wiring-schematic.html` — do not improvise)

- MyoWare 2 + Link Shield: output select switch → **ENV**, ENV → **A0** via
  TRS tip (ring = 5 V, sleeve = GND). MyoWare powered from Arduino 5 V.
- PCA9685 at I2C **0x40** (SDA = A4, SCL = A5), servos on channels **0–4**,
  PWM frequency set to **50 Hz** in the sketch (the chip's ~200 Hz default
  makes SG90s jitter).
- Servo power comes from the separate 5 V rail on the PCA9685 V+ terminal,
  **never** the Arduino 5 V pin. All grounds common.
- Library: **Adafruit PWM Servo Driver Library** (install in the IDE's
  Library Manager before compiling).

## Calibration (runs at every boot, ~4 s)

1. `# CALIBRATION: relax your arm completely...` — baseline = average A0
   for 2 s (the schematic's definition, verbatim).
2. `# CALIBRATION: flex HARD and hold...` — 2 s capture; the peak of the
   smoothed envelope sets the top of the dynamic range.
3. Thresholds are placed at fractions of (flexPeak − baseline). If the range
   is too small, calibration **fails**, default thresholds are kept, and the
   sketch prints the schematic's own advice: if A0 barely moved, turn the
   MyoWare gain pot clockwise and recalibrate (send `r` over serial).

Serial commands: `r` = recalibrate, `d` = dump current thresholds.
Telemetry (Serial Plotter format) streams `env`, `state`, `angle`, and both
thresholds at 10 Hz. Built-in LED: solid = Grip, blinking = Fault.

Safety behaviour in the logic: sustained railed ADC (≥1020 for 1 s) or a
sustained exact-0 reading (2 s, armed only after a calibration with
baseline ≥ 5) raises a fault and releases the claw to the open angle.
Servo motion is slew-rate limited and hard-clamped to
`servoMinAngle..servoMaxAngle`.

## Placeholder values — ALL must be replaced by measurement

| Value | Placeholder | Replace when |
|---|---|---|
| Default close / grip thresholds | 250 / 600 (ADC) | Calibration on the real arm (automatic) |
| Threshold fractions (close / grip / hysteresis) | 0.35 / 0.70 / 0.06 of range | After watching real flex separation |
| EMA smoothing alpha | 0.25 | Bench tuning for noise vs response |
| Open / close / grip angles | 10° / 80° / 110° | When the printed claw's range is measured |
| Servo clamp max | 120° | Schematic rule: clamp to the hand's measured range before first full close |
| PCA9685 pulse ticks (0°/180°) | 150 / 600 | Bench-measure the actual SG90 endpoints |
| Slew rate | 240°/s | Comfort/safety tuning on the bench |

## Bench bring-up (aligns with the schematic's stage order)

0. Prerequisite: install the **Arduino IDE** (free, arduino.cc → Software).
   As of 2026-10-12 Renegade has not downloaded it yet; he installs it on
   his Windows PC, then installs the **CH340 USB-serial driver** (his
   RobotShop Uno clone uses a CH340 chip — without this driver the IDE
   cannot see the board at all; see the datasheet walkthrough, section 3),
   then installs the Adafruit PWM Servo Driver Library via
   the IDE's Library Manager, then copies this `emg_claw/` folder over and
   opens `emg_claw.ino`.
1. Compile in the Arduino IDE; fix any library/API issues there first.
2. Set `BENCH_SINGLE_SERVO` to 1 in the sketch. Wire **one** servo on
   channel 0 with the 5 V rail per the schematic (multimeter polarity and
   common-ground checks from the schematic come first).
3. Run calibration with electrodes on; confirm Serial Plotter separation
   between rest and flex before trusting any threshold.
4. Flex → servo sweep confirms the full EMG→motion path. Then set
   `BENCH_SINGLE_SERVO` back to 0 and add the remaining servos + the
   470 µF cap per the schematic stage notes.
5. Only after the hand's angle range is measured: set the clamp and state
   angles, then connect tendons.
