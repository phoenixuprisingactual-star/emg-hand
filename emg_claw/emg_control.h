// emg_control.h - EMG claw control logic, Phase 1.
//
// Pure C++ with no Arduino dependencies so the exact code that ships in the
// sketch is exercised by the host simulation tests in ../sim/.
//
// Design source: claw-control-design-2026-10-11.md (project design doc)
//   Three states on ONE channel, two thresholds, no classifier:
//     rest       -> claw OPEN
//     light flex -> claw CLOSED
//     hard flex  -> claw GRIP (tight)
//   No reject gate, no majority-vote window (that layer buys multi-gesture
//   reliability Phase 1 does not need, at ~2 s of latency Phase 1 rejects).
//   Chatter control is hysteresis only.
//
// Hardware source: wiring-schematic.html (project wiring schematic)
//   MyoWare 2 ENV output -> Arduino A0 (0-5 V -> ADC 0..1023).
//   Baseline = average A0 for 2 s at startup (schematic definition, verbatim).
//
// EVERY numeric value in EmgConfig is a bench PLACEHOLDER until it is
// measured on Renegade's arm with the real sensor. Calibration replaces the
// thresholds at runtime; the defaults only exist so the bench stages can run
// before calibration does.

#pragma once

#include <cstdint>

struct EmgConfig {
  // --- Signal conditioning -------------------------------------------------
  float emaAlpha = 0.25f;  // Envelope smoothing. PLACEHOLDER: tune on arm.

  // --- Default thresholds (ADC counts 0..1023) -----------------------------
  // Used until a calibration succeeds. PLACEHOLDERS.
  int defaultCloseThreshold = 250;
  int defaultGripThreshold = 600;
  int defaultHysteresis = 25;

  // --- Calibration-derived thresholds --------------------------------------
  // Thresholds sit at these fractions of the measured dynamic range
  // (flexPeak - baseline). PLACEHOLDER fractions: validate on arm.
  float closeFraction = 0.35f;
  float gripFraction = 0.70f;
  float hysteresisFraction = 0.06f;  // of dynamic range, floor of 8 counts
  int minDynamicRange = 100;         // calibration fails below this separation
  uint32_t restPhaseMs = 2000;       // schematic: 2-second baseline
  uint32_t flexPhaseMs = 2000;       // hard-flex capture window

  // --- Servo targets (degrees) ---------------------------------------------
  // PLACEHOLDERS until the printed hand's real range is measured. The
  // schematic requires software clamping to the hand's measured range
  // before the first full close; servoMaxAngle is that clamp.
  int openAngle = 10;
  int closeAngle = 80;
  int gripAngle = 110;
  int servoMinAngle = 0;
  int servoMaxAngle = 120;  // deliberately well under 180 until measured
  float slewDegPerSec = 240.0f;  // max servo travel speed toward target

  // --- Fault detection -------------------------------------------------------
  int railHigh = 1020;          // ADC at/above this counts as railed
  uint32_t railFaultMs = 1000;  // sustained rail -> fault (short / stuck ADC)
  uint32_t zeroFaultMs = 2000;  // sustained exact 0 -> fault (ENV unplugged).
                                // Armed only if calibrated baseline >= 5 counts,
                                // so a genuinely quiet rest signal can't trip it.
};

enum class ClawState : uint8_t { Open = 0, Closed = 1, Grip = 2, Fault = 3 };

class EmgController {
 public:
  void begin(const EmgConfig& cfg) {
    cfg_ = cfg;
    closeT_ = cfg.defaultCloseThreshold;
    gripT_ = cfg.defaultGripThreshold;
    hyst_ = cfg.defaultHysteresis;
    state_ = ClawState::Open;
    ema_ = 0.0f;
    angle_ = (float)clampAngle(cfg.openAngle);
    target_ = cfg.openAngle;
    lastMs_ = 0;
    haveLast_ = false;
    railMs_ = 0;
    zeroMs_ = 0;
    fault_ = false;
    zeroFaultArmed_ = false;
  }

  // Apply calibration-derived thresholds. Also arms the zero-fault check
  // against the measured baseline (see EmgConfig::zeroFaultMs).
  void setThresholds(int closeT, int gripT, int hyst, int baseline) {
    closeT_ = closeT;
    gripT_ = gripT;
    hyst_ = hyst;
    zeroFaultArmed_ = baseline >= 5;
  }

  void clearFault() {
    fault_ = false;
    railMs_ = 0;
    zeroMs_ = 0;
    if (state_ == ClawState::Fault) state_ = ClawState::Open;
  }

  // Feed one ADC sample (0..1023) with a monotonic millisecond clock.
  void update(int rawAdc, uint32_t nowMs) {
    uint32_t dt = haveLast_ ? (nowMs - lastMs_) : 0;
    lastMs_ = nowMs;
    haveLast_ = true;

    if (rawAdc < 0) rawAdc = 0;
    if (rawAdc > 1023) rawAdc = 1023;

    // Envelope smoothing (single-pole EMA on the MyoWare ENV signal).
    ema_ += cfg_.emaAlpha * ((float)rawAdc - ema_);

    // --- Fault supervision -------------------------------------------------
    if (rawAdc >= cfg_.railHigh) {
      railMs_ += dt;
    } else {
      railMs_ = 0;
    }
    if (rawAdc == 0) {
      zeroMs_ += dt;
    } else {
      zeroMs_ = 0;
    }
    if (!fault_) {
      if (railMs_ >= cfg_.railFaultMs) fault_ = true;
      if (zeroFaultArmed_ && zeroMs_ >= cfg_.zeroFaultMs) fault_ = true;
    }

    // --- State machine (hysteresis only, per design doc) -------------------
    if (fault_) {
      state_ = ClawState::Fault;
      target_ = cfg_.openAngle;  // fail safe: release the claw
    } else {
      float env = ema_;
      switch (state_) {
        case ClawState::Open:
          if (env > (float)gripT_) {
            state_ = ClawState::Grip;
          } else if (env > (float)closeT_) {
            state_ = ClawState::Closed;
          }
          break;
        case ClawState::Closed:
          if (env > (float)gripT_) {
            state_ = ClawState::Grip;
          } else if (env < (float)(closeT_ - hyst_)) {
            state_ = ClawState::Open;
          }
          break;
        case ClawState::Grip:
          if (env < (float)(closeT_ - hyst_)) {
            state_ = ClawState::Open;
          } else if (env < (float)(gripT_ - hyst_)) {
            state_ = ClawState::Closed;
          }
          break;
        case ClawState::Fault:
          state_ = ClawState::Open;  // unreachable while fault_ set; safety net
          break;
      }
      switch (state_) {
        case ClawState::Open: target_ = cfg_.openAngle; break;
        case ClawState::Closed: target_ = cfg_.closeAngle; break;
        case ClawState::Grip: target_ = cfg_.gripAngle; break;
        case ClawState::Fault: target_ = cfg_.openAngle; break;
      }
    }

    // --- Slew limiting + clamp ----------------------------------------------
    int clampedTarget = clampAngle(target_);
    float maxStep = cfg_.slewDegPerSec * ((float)dt / 1000.0f);
    if (haveLast_ && dt > 0) {
      if (angle_ < (float)clampedTarget) {
        angle_ += maxStep;
        if (angle_ > (float)clampedTarget) angle_ = (float)clampedTarget;
      } else if (angle_ > (float)clampedTarget) {
        angle_ -= maxStep;
        if (angle_ < (float)clampedTarget) angle_ = (float)clampedTarget;
      }
    } else {
      angle_ = (float)clampedTarget;
    }
  }

  ClawState state() const { return state_; }
  bool fault() const { return fault_; }
  float envelope() const { return ema_; }
  int servoAngle() const { return (int)(angle_ + 0.5f); }
  int targetAngle() const { return target_; }
  int closeThreshold() const { return closeT_; }
  int gripThreshold() const { return gripT_; }
  int hysteresis() const { return hyst_; }

 private:
  int clampAngle(int a) const {
    if (a < cfg_.servoMinAngle) return cfg_.servoMinAngle;
    if (a > cfg_.servoMaxAngle) return cfg_.servoMaxAngle;
    return a;
  }

  EmgConfig cfg_;
  int closeT_, gripT_, hyst_;
  ClawState state_ = ClawState::Open;
  float ema_ = 0.0f;
  float angle_ = 0.0f;
  int target_ = 0;
  uint32_t lastMs_ = 0;
  bool haveLast_ = false;
  uint32_t railMs_ = 0;
  uint32_t zeroMs_ = 0;
  bool fault_ = false;
  bool zeroFaultArmed_ = false;
};

// ---------------------------------------------------------------------------
// Calibration: two timed phases, matching the schematic's startup flow.
//   Phase 1 (rest):  average A0 for 2 s = resting baseline (schematic, verbatim)
//   Phase 2 (flex):  hard flex held for 2 s; peak of the smoothed envelope is
//                    the top of the dynamic range.
// Thresholds are then placed at configured fractions of (flexPeak - baseline).
// If the separation is too small the calibration FAILS and the controller
// keeps its default thresholds - a failed calibration must never silently
// produce a twitchy claw. (Schematic fallback if A0 barely moves: turn the
// MyoWare gain pot clockwise, then recalibrate.)
// ---------------------------------------------------------------------------

struct CalibrationResult {
  bool ok = false;
  int baseline = 0;
  int flexPeak = 0;
  int closeThreshold = 0;
  int gripThreshold = 0;
  int hysteresis = 0;
};

class EmgCalibrator {
 public:
  void begin(const EmgConfig& cfg, uint32_t nowMs) {
    cfg_ = cfg;
    phaseStartMs_ = nowMs;
    phase_ = Phase::Rest;
    sum_ = 0;
    count_ = 0;
    flexEma_ = 0.0f;
    flexPeak_ = 0.0f;
    baseline_ = 0;
    done_ = false;
    result_ = CalibrationResult();
  }

  // Returns true while still running. Feed every sample.
  bool update(int rawAdc, uint32_t nowMs) {
    if (done_) return false;
    if (rawAdc < 0) rawAdc = 0;
    if (rawAdc > 1023) rawAdc = 1023;
    uint32_t elapsed = nowMs - phaseStartMs_;

    if (phase_ == Phase::Rest) {
      sum_ += (uint32_t)rawAdc;
      count_++;
      if (elapsed >= cfg_.restPhaseMs) {
        baseline_ = count_ > 0 ? (int)(sum_ / count_) : 0;
        phase_ = Phase::Flex;
        phaseStartMs_ = nowMs;
        flexEma_ = (float)baseline_;
        flexPeak_ = (float)baseline_;
      }
      return true;
    }

    // Flex phase: track the peak of a lightly smoothed envelope so a single
    // ADC spike cannot inflate the range.
    flexEma_ += 0.3f * ((float)rawAdc - flexEma_);
    if (flexEma_ > flexPeak_) flexPeak_ = flexEma_;
    if (elapsed >= cfg_.flexPhaseMs) {
      finish();
      return false;
    }
    return true;
  }

  bool inRestPhase() const { return !done_ && phase_ == Phase::Rest; }
  bool inFlexPhase() const { return !done_ && phase_ == Phase::Flex; }
  bool done() const { return done_; }
  CalibrationResult result() const { return result_; }

 private:
  enum class Phase : uint8_t { Rest, Flex };

  void finish() {
    done_ = true;
    int peak = (int)(flexPeak_ + 0.5f);
    int range = peak - baseline_;
    result_.baseline = baseline_;
    result_.flexPeak = peak;
    if (range < cfg_.minDynamicRange) {
      result_.ok = false;
      return;
    }
    result_.ok = true;
    result_.closeThreshold = baseline_ + (int)(cfg_.closeFraction * (float)range);
    result_.gripThreshold = baseline_ + (int)(cfg_.gripFraction * (float)range);
    int h = (int)(cfg_.hysteresisFraction * (float)range);
    result_.hysteresis = h < 8 ? 8 : h;
  }

  EmgConfig cfg_;
  Phase phase_ = Phase::Rest;
  uint32_t phaseStartMs_ = 0;
  uint32_t sum_ = 0;
  uint32_t count_ = 0;
  float flexEma_ = 0.0f;
  float flexPeak_ = 0.0f;
  int baseline_ = 0;
  bool done_ = false;
  CalibrationResult result_;
};
