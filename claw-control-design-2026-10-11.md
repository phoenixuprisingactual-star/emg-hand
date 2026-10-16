# Claw Control Design — Phase 1 (decided 2026-10-11)

Phase 1 claw control is **three states on one channel, two thresholds, no classifier**.

## The decision (Renegade, 2026-10-11)

- **Rest** = claw open
- **Light flex** = claw closes
- **Hard flex** = claw grips tight

That's it. Two thresholds on the single MyoWare 2 signal. No multi-gesture classification, no reject gate, no latency penalty.

## Why not a classifier (tradeoff studied 2026-10-11)

An sEMG study (three-channel sEMG + Random Forest classifier) brought in via the feed showed the honest tradeoff: false actuations down to **5%**, but the safety layer (confidence-threshold reject gate + majority voting window) costs **1.95 seconds** of latency from gesture to movement.

That layer pays for *multi-gesture* — distinguishing fist vs pinch vs point vs open palm across multiple muscle groups. Phase 1 doesn't need it: with one channel, rest/light/hard flex is the whole vocabulary, and the classifier's cost (two seconds of delay) buys reliability Phase 1 doesn't require. If the design ever scales to true multi-gesture (individual finger intent), the reject-gate concept is the safety layer to revisit — with the latency cost budgeted up front.

## Hardware target

Tripod claw STL draft generated 2026-10-11 (`tripod_claw_gen.py` → `tripod_claw_v1.stl`), draft quality. Proof that the agent can produce STL geometry; not a finished claw.

## Sensors

MyoWare 2 (single channel) is Phase 1. FMG (force-sensing resistors) is favored for the later wearable claw (sweat-tolerant, and claws don't need fine finger mapping) — see the project's FMG wearable reference notes. The Phase 1 threshold/calibration work transfers to FMG unchanged: sample analog values, set thresholds, map to open/close.
