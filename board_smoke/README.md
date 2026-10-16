# Board smoke test - Uno + PCA9685 only

A pre-servo stage: prove the controller board and servo driver work
before anything that can be damaged is connected. The sketch never
writes PWM registers and never drives an output, so it is safe to run
with the PCA9685 wired for logic power only.

## What this stage proves

1. The CH340 driver + Arduino IDE can see and program the Uno clone
   (see the main README's bring-up step 0 - the driver is the known
   board-day trap).
2. `emg_claw/emg_claw.ino` compiles in the real IDE. Its first compile
   is a verification step, not a formality: only the host-side control
   logic has been simulation-tested so far (see the main README's
   status banner).
3. The PCA9685 answers on I2C at 0x40 with logic power only.

## Wiring (only these four connections)

Per `wiring-schematic.html`: Uno 5V → PCA9685 VCC, Uno GND → PCA9685
GND, Uno A4 → PCA9685 SDA, Uno A5 → PCA9685 SCL. Leave the V+ screw
terminal disconnected and all servo channels empty. Servo power never
comes from the Uno.

## Pass

Open `board_smoke.ino`, upload it, open Serial Monitor at 9600 baud.
Pass = `# PCA9685 FOUND at 0x40 - stage 1 PASS` repeating every ~5 s,
with the built-in LED blinking. Compile `emg_claw.ino` in the IDE at
this stage too, but do not upload it - without the MyoWare on A0 it
would run a meaningless calibration; a clean compile is the result
this step is after.

If 0x40 does not answer: re-check wire order and that VCC is actually
fed, then re-run. Do not troubleshoot past wiring at this stage -
nothing else is connected that could be at fault.

## Not in this stage

No servos, no servo rail, no MyoWare, no calibration, no thresholds.
The main README's placeholder table stays untouched; all of it waits
on measured hardware.
