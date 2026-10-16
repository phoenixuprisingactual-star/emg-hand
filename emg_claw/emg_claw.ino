// emg_claw.ino - EMG Hand Phase 1 firmware skeleton.
//
// Target hardware (per the project wiring schematic, wiring-schematic.html):
//   Arduino Uno (clone) + MyoWare 2 with Link Shield (ENV -> A0)
//   PCA9685 servo driver, I2C 0x40 (SDA=A4, SCL=A5), servo channels 0-4
//   5x SG90 servos on the PCA9685 V+ rail (separate 5 V supply, common GND)
//
// Control design: claw-control-design-2026-10-11.md - three states, two
// thresholds, one channel. All decision logic lives in emg_control.h and is
// exercised by the simulation tests in ../sim/ before it ever runs here.
//
// REQUIRED LIBRARY: "Adafruit PWM Servo Driver Library" (Library Manager).
// The PCA9685 must be set to 50 Hz for servos; at the chip's ~200 Hz
// default the SG90s jitter (schematic stage notes).
//
// STATUS: SKELETON. Every threshold/angle/pulse value is a placeholder to
// be replaced by calibration and bench measurement. Nothing here has run
// on real hardware yet. Follow the schematic's staged bring-up: ONE servo
// on channel 0 first (set BENCH_SINGLE_SERVO to 1).

#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>

#include "emg_control.h"

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

#define EMG_PIN A0            // MyoWare ENV via TRS tip (schematic table)
#define PCA9685_ADDR 0x40     // schematic: I2C address 0x40
#define SERVO_PWM_FREQ 50     // Hz - required for SG90 servos
#define NUM_SERVOS 5          // PCA9685 channels 0..4
#define BENCH_SINGLE_SERVO 0  // 1 = drive channel 0 only (first bench stage)

// PCA9685 pulse ticks for 0..180 degrees. PLACEHOLDERS: these are the
// Adafruit example's generic values, NOT measured for these SG90s or the
// printed claw. Bench-measure the real endpoints before connecting tendons.
#define PULSE_MIN_TICKS 150
#define PULSE_MAX_TICKS 600

#define SAMPLE_PERIOD_MS 10    // 100 Hz sampling
#define TELEMETRY_PERIOD_MS 100

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver(PCA9685_ADDR);
EmgConfig config;
EmgController controller;
EmgCalibrator calibrator;
bool calibrating = true;
uint32_t lastSampleMs = 0;
uint32_t lastTelemetryMs = 0;

uint16_t angleToTicks(int angle) {
  if (angle < 0) angle = 0;
  if (angle > 180) angle = 180;
  return (uint16_t)(PULSE_MIN_TICKS +
                    (angle * (PULSE_MAX_TICKS - PULSE_MIN_TICKS)) / 180);
}

void driveServos(int angle) {
  uint16_t ticks = angleToTicks(angle);
#if BENCH_SINGLE_SERVO
  pwm.setPWM(0, 0, ticks);
#else
  for (int ch = 0; ch < NUM_SERVOS; ch++) {
    pwm.setPWM(ch, 0, ticks);
  }
#endif
}

void startCalibration() {
  calibrator.begin(config, millis());
  calibrating = true;
  Serial.println(F("# CALIBRATION: relax your arm completely..."));
}

void finishCalibration() {
  CalibrationResult r = calibrator.result();
  if (r.ok) {
    controller.setThresholds(r.closeThreshold, r.gripThreshold, r.hysteresis,
                             r.baseline);
    Serial.print(F("# CAL OK baseline="));
    Serial.print(r.baseline);
    Serial.print(F(" flexPeak="));
    Serial.print(r.flexPeak);
    Serial.print(F(" closeT="));
    Serial.print(r.closeThreshold);
    Serial.print(F(" gripT="));
    Serial.print(r.gripThreshold);
    Serial.print(F(" hyst="));
    Serial.println(r.hysteresis);
  } else {
    Serial.print(F("# CAL FAILED (range too small: baseline="));
    Serial.print(r.baseline);
    Serial.print(F(" flexPeak="));
    Serial.print(r.flexPeak);
    Serial.println(F("). Keeping default thresholds."));
    Serial.println(F("# If A0 barely moved, turn the MyoWare gain pot "
                     "clockwise and recalibrate (send 'r')."));
  }
  calibrating = false;
}

// ---------------------------------------------------------------------------
// Setup / loop
// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);

  pwm.begin();
  pwm.setOscillatorFrequency(27000000);
  pwm.setPWMFreq(SERVO_PWM_FREQ);

  controller.begin(config);
  driveServos(controller.servoAngle());  // park at OPEN before anything else

  Serial.println(F("# EMG claw Phase 1 skeleton - UNCALIBRATED placeholders"));
  Serial.println(F("# Commands: r = recalibrate, d = dump thresholds"));
  startCalibration();
}

void loop() {
  uint32_t now = millis();

  // Serial commands (checked every loop; cheap).
  if (Serial.available()) {
    char c = (char)Serial.read();
    if (c == 'r' || c == 'R') {
      controller.clearFault();
      startCalibration();
    } else if (c == 'd' || c == 'D') {
      Serial.print(F("# closeT="));
      Serial.print(controller.closeThreshold());
      Serial.print(F(" gripT="));
      Serial.print(controller.gripThreshold());
      Serial.print(F(" hyst="));
      Serial.println(controller.hysteresis());
    }
  }

  if (now - lastSampleMs < SAMPLE_PERIOD_MS) return;
  lastSampleMs = now;

  int raw = analogRead(EMG_PIN);

  if (calibrating) {
    bool wasFlex = calibrator.inFlexPhase();
    bool running = calibrator.update(raw, now);
    if (!wasFlex && calibrator.inFlexPhase()) {
      Serial.println(F("# CALIBRATION: flex HARD and hold..."));
    }
    if (!running) finishCalibration();
    return;  // servos stay parked at OPEN during calibration
  }

  controller.update(raw, now);
  driveServos(controller.servoAngle());

  // Built-in LED: solid in GRIP, blinking on FAULT, off otherwise.
  if (controller.fault()) {
    digitalWrite(LED_BUILTIN, (now / 250) % 2 ? HIGH : LOW);
  } else if (controller.state() == ClawState::Grip) {
    digitalWrite(LED_BUILTIN, HIGH);
  } else {
    digitalWrite(LED_BUILTIN, LOW);
  }

  // Telemetry in Serial Plotter format (label:value pairs).
  if (now - lastTelemetryMs >= TELEMETRY_PERIOD_MS) {
    lastTelemetryMs = now;
    Serial.print(F("env:"));
    Serial.print(controller.envelope(), 0);
    Serial.print(F(" state:"));
    Serial.print((int)controller.state());
    Serial.print(F(" angle:"));
    Serial.print(controller.servoAngle());
    Serial.print(F(" closeT:"));
    Serial.print(controller.closeThreshold());
    Serial.print(F(" gripT:"));
    Serial.println(controller.gripThreshold());
  }
}
