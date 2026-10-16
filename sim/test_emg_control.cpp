// test_emg_control.cpp - host simulation tests for the EMG claw control logic.
//
// Compiles the SAME emg_control.h that ships in the Arduino sketch and runs
// it against synthetic MyoWare ENV traces (rest noise, light/hard flex
// plateaus, threshold-hover chatter, calibration runs, sensor faults).
// No hardware involved: these tests validate logic, not signal reality.
// The thresholds the logic uses here are the documented placeholders.

#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "../emg_claw/emg_control.h"

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, msg)                                  \
  do {                                                    \
    g_checks++;                                           \
    if (!(cond)) {                                        \
      g_failures++;                                       \
      std::printf("  FAIL: %s (line %d)\n", msg, __LINE__); \
    }                                                     \
  } while (0)

// Deterministic pseudo-noise in [-amp, +amp].
struct Noise {
  uint32_t s;
  explicit Noise(uint32_t seed) : s(seed) {}
  int next(int amp) {
    s = s * 1103515245u + 12345u;
    int span = 2 * amp + 1;
    return (int)((s >> 16) % (uint32_t)span) - amp;
  }
};

// Feed a constant level (plus noise) for durationMs at 10 ms steps.
static void feed(EmgController& c, int level, int noiseAmp, uint32_t& t,
                 uint32_t durationMs, Noise& n) {
  for (uint32_t elapsed = 0; elapsed < durationMs; elapsed += 10) {
    t += 10;
    c.update(level + n.next(noiseAmp), t);
  }
}

static void feedCal(EmgCalibrator& cal, int level, int noiseAmp, uint32_t& t,
                    uint32_t durationMs, Noise& n) {
  for (uint32_t elapsed = 0; elapsed < durationMs; elapsed += 10) {
    t += 10;
    cal.update(level + n.next(noiseAmp), t);
  }
}

static void test_rest_stays_open() {
  std::printf("test: rest stays open\n");
  EmgConfig cfg;
  EmgController c;
  c.begin(cfg);
  Noise n(42);
  uint32_t t = 0;
  feed(c, 40, 5, t, 3000, n);
  CHECK(c.state() == ClawState::Open, "rest noise must stay Open");
  CHECK(c.servoAngle() == cfg.openAngle, "servo parked at open angle");
  CHECK(!c.fault(), "rest must not fault");
}

static void test_light_flex_closes_and_releases() {
  std::printf("test: light flex closes, release reopens\n");
  EmgConfig cfg;
  EmgController c;
  c.begin(cfg);
  Noise n(7);
  uint32_t t = 0;
  feed(c, 40, 5, t, 1000, n);
  feed(c, 300, 8, t, 1000, n);  // above close 250, below grip 600
  CHECK(c.state() == ClawState::Closed, "light flex must be Closed");
  CHECK(c.servoAngle() == cfg.closeAngle, "servo at close angle");
  feed(c, 40, 5, t, 1000, n);
  CHECK(c.state() == ClawState::Open, "release must reopen");
  CHECK(c.servoAngle() == cfg.openAngle, "servo back at open angle");
}

static void test_hard_flex_grips_and_steps_down() {
  std::printf("test: hard flex grips, steps down through closed\n");
  EmgConfig cfg;
  EmgController c;
  c.begin(cfg);
  Noise n(9);
  uint32_t t = 0;
  feed(c, 40, 5, t, 1000, n);
  feed(c, 800, 10, t, 1000, n);
  CHECK(c.state() == ClawState::Grip, "hard flex must be Grip");
  CHECK(c.servoAngle() == cfg.gripAngle, "servo at grip angle");
  feed(c, 300, 8, t, 1000, n);
  CHECK(c.state() == ClawState::Closed, "partial release must step to Closed");
  feed(c, 40, 5, t, 1000, n);
  CHECK(c.state() == ClawState::Open, "full release must reopen");
}

static void test_hysteresis_no_chatter() {
  std::printf("test: hysteresis prevents chatter at the threshold\n");
  EmgConfig cfg;
  EmgController c;
  c.begin(cfg);
  Noise n(3);
  uint32_t t = 0;
  feed(c, 40, 5, t, 1000, n);
  // Hover right at the close threshold (250): square wave 235..265, 100 ms
  // half-period, for 3 s. Once Closed, exit needs < 225, which never
  // happens, so transitions across the whole window must be <= 2.
  int transitions = 0;
  ClawState prev = c.state();
  for (int i = 0; i < 300; i++) {
    t += 10;
    int level = ((i / 10) % 2 == 0) ? 265 : 235;
    c.update(level + n.next(2), t);
    if (c.state() != prev) {
      transitions++;
      prev = c.state();
    }
  }
  CHECK(transitions <= 2, "hovering at threshold must not chatter");
}

static void test_calibration_succeeds() {
  std::printf("test: calibration derives ordered thresholds\n");
  EmgConfig cfg;
  EmgCalibrator cal;
  Noise n(11);
  uint32_t t = 0;
  cal.begin(cfg, t);
  feedCal(cal, 40, 5, t, 2100, n);    // rest phase (2 s)
  CHECK(cal.inFlexPhase(), "should enter flex phase after 2 s rest");
  feedCal(cal, 820, 10, t, 2100, n);  // flex phase (2 s)
  CHECK(cal.done(), "calibration should be done");
  CalibrationResult r = cal.result();
  CHECK(r.ok, "calibration with strong flex must succeed");
  CHECK(r.baseline >= 30 && r.baseline <= 50, "baseline near rest mean");
  CHECK(r.flexPeak > 600, "flex peak near plateau");
  CHECK(r.closeThreshold > r.baseline, "close threshold above baseline");
  CHECK(r.gripThreshold > r.closeThreshold, "grip threshold above close");
  CHECK(r.hysteresis >= 8, "hysteresis has a floor");

  // The calibrated controller must map mid/high plateaus correctly.
  EmgController c;
  c.begin(cfg);
  c.setThresholds(r.closeThreshold, r.gripThreshold, r.hysteresis, r.baseline);
  Noise n2(13);
  uint32_t t2 = 0;
  feed(c, 40, 5, t2, 1000, n2);
  feed(c, 450, 8, t2, 1000, n2);
  CHECK(c.state() == ClawState::Closed, "mid plateau must be Closed (cal)");
  feed(c, 700, 8, t2, 1000, n2);
  CHECK(c.state() == ClawState::Grip, "high plateau must be Grip (cal)");
}

static void test_calibration_fails_on_weak_flex() {
  std::printf("test: calibration fails when flex barely exceeds rest\n");
  EmgConfig cfg;
  EmgCalibrator cal;
  Noise n(17);
  uint32_t t = 0;
  cal.begin(cfg, t);
  feedCal(cal, 40, 5, t, 2100, n);
  feedCal(cal, 90, 5, t, 2100, n);  // range ~50 < minDynamicRange 100
  CHECK(cal.done(), "calibration should finish");
  CHECK(!cal.result().ok, "weak separation must fail calibration");
}

static void test_rail_fault_and_recovery() {
  std::printf("test: sustained railed ADC faults safe, then recovers\n");
  EmgConfig cfg;
  EmgController c;
  c.begin(cfg);
  Noise n(19);
  uint32_t t = 0;
  feed(c, 40, 5, t, 500, n);
  feed(c, 1023, 0, t, 1500, n);  // railed for 1.5 s > railFaultMs 1000
  CHECK(c.fault(), "sustained rail must fault");
  CHECK(c.state() == ClawState::Fault, "state must be Fault");
  CHECK(c.targetAngle() == cfg.openAngle, "fault target must be open (release)");
  c.clearFault();
  feed(c, 40, 5, t, 500, n);
  CHECK(!c.fault(), "fault must clear");
  CHECK(c.state() == ClawState::Open, "recovered state must be Open");
}

static void test_slew_limit() {
  std::printf("test: servo angle is slew-limited\n");
  EmgConfig cfg;
  EmgController c;
  c.begin(cfg);
  Noise n(23);
  uint32_t t = 0;
  feed(c, 40, 5, t, 1000, n);
  int startAngle = c.servoAngle();
  feed(c, 800, 5, t, 50, n);  // 50 ms of hard flex
  int moved = c.servoAngle() - startAngle;
  int maxAllowed = (int)(cfg.slewDegPerSec * 0.05f) + 2;
  CHECK(moved >= 0 && moved <= maxAllowed, "angle must not jump instantly");
  feed(c, 800, 5, t, 2000, n);
  CHECK(c.servoAngle() == cfg.gripAngle, "angle must reach grip target given time");
}

static void test_angle_clamp() {
  std::printf("test: servo angle clamped to configured range\n");
  EmgConfig cfg;
  cfg.gripAngle = 170;       // deliberately beyond the clamp
  cfg.servoMaxAngle = 120;   // the clamp itself
  EmgController c;
  c.begin(cfg);
  Noise n(29);
  uint32_t t = 0;
  feed(c, 40, 5, t, 500, n);
  feed(c, 800, 5, t, 2000, n);
  CHECK(c.state() == ClawState::Grip, "must be in Grip for clamp test");
  CHECK(c.servoAngle() <= 120, "angle must never exceed servoMaxAngle");
}

int main() {
  test_rest_stays_open();
  test_light_flex_closes_and_releases();
  test_hard_flex_grips_and_steps_down();
  test_hysteresis_no_chatter();
  test_calibration_succeeds();
  test_calibration_fails_on_weak_flex();
  test_rail_fault_and_recovery();
  test_slew_limit();
  test_angle_clamp();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
