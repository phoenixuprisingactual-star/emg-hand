// board_smoke.ino — EMG Hand board-only smoke test
// Purpose: verify the RobotShop Uno clone + PCA9685 BEFORE any servo,
// MyoWare, or servo power rail exists on the bench.
// It does three things and nothing else:
//   1. Blinks the built-in LED (board is alive, upload worked).
//   2. Scans the I2C bus and reports every address that answers.
//   3. Specifically flags whether the PCA9685 answers at 0x40.
// It never writes PWM registers and never drives an output, so it is
// safe to run with the PCA9685 wired for logic power only.
//
// Wiring (from ../wiring-schematic.html, verified 2026-10-14):
//   Uno 5V  -> PCA9685 VCC   (logic power only, ~10 mA — NOT servo power)
//   Uno GND -> PCA9685 GND
//   Uno A4  -> PCA9685 SDA
//   Uno A5  -> PCA9685 SCL
//   PCA9685 V+ screw terminal: LEAVE DISCONNECTED for this test.
//   Servo channels 0-4: leave empty.
//
// Expected serial output (9600 baud):
//   "PCA9685 FOUND at 0x40"  -> board day stage 1 passes.
//   "0x40 NOT found"         -> check wiring order and that VCC is fed,
//                               then re-run; do not proceed to servos.

#include <Wire.h>

#define PCA9685_ADDR 0x40

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(9600);
  while (!Serial) { /* wait for USB serial on some boards */ }
  Wire.begin();
  Serial.println("# board_smoke: I2C scan starting");
}

void loop() {
  bool foundPca = false;
  int foundCount = 0;

  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    byte err = Wire.endTransmission();
    if (err == 0) {
      foundCount++;
      Serial.print("# found device at 0x");
      if (addr < 16) Serial.print("0");
      Serial.println(addr, HEX);
      if (addr == PCA9685_ADDR) foundPca = true;
    }
  }

  if (foundCount == 0) {
    Serial.println("# NO I2C devices found - check VCC/GND/SDA/SCL wiring");
  }
  if (foundPca) {
    Serial.println("# PCA9685 FOUND at 0x40 - stage 1 PASS");
  } else {
    Serial.println("# 0x40 NOT found - PCA9685 not answering yet");
  }

  // Heartbeat blink so the board's state is visible without serial.
  digitalWrite(LED_BUILTIN, HIGH);
  delay(200);
  digitalWrite(LED_BUILTIN, LOW);
  delay(4800);  // re-scan every ~5 s; move a wire, watch the result change
}
