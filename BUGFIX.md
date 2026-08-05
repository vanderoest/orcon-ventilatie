# BUGFIX.md — findings from field logs `orcon.log` … `orcon4.log`

See chat history for the full walkthrough; this is the consolidated punch list.

**Implementation status (v2.0.1, see `BUGFIX.plan` and `TODO.md`):** bugs #1
and #2 are fixed and host-tested; recommendation #1 (`humidity_disagreement_margin`
→ 10) is applied. First on-device boot of v2.0.1 surfaced a further defect —
item 9 below, the fan never actually starting on a fresh boot — which is now
also fixed and host-tested. This document is **not fully closed**: item 8
(seasonal baseline drift) is analysed but explicitly not implemented — it
needs a design decision from the user before any code is written — and every
`[HW]` item in `TODO.md` (on-device verification of #1, #2, #3, #9, and the
item-6 checklist) still needs a physical unit. See `TODO.md` for the exact
checklist state.

---

## Recommended changes (thresholds & settings)

Based on all three logs (boot → shower at ~08:33 → 08:50, i.e. ~17 minutes of continuous post-shower data across `orcon2.log`+`orcon3.log`), here's what I'd actually change versus leave alone:

1. **`humidity_disagreement_margin`: lower from 15 to ~10.** Two independent stretches of data (`orcon2.log`, `orcon3.log`) now show a *stable, repeatable* 6–8.5 point gap between SHT4x and SCD4x humidity across the whole range observed (ambient ~54% up to post-shower ~68%) — see item 3 below. At margin=15 the `Sensor Disagreement` diagnostic can never fire for this hardware pair in practice; a real fault would need to diverge by more than 15 points to be caught. 10 still comfortably clears the observed ~8.5-point maximum while giving the diagnostic a real chance to flag an actual problem later (a sensor stuck, disconnected, or badly drifted). Config-only change (`orcon.yaml` substitution `humidity_disagreement_margin`), no logic change.

2. **Fixed absolute RH thresholds (60% / 55%) do not survive seasonal baseline drift — the main open design question.** `orcon4.log` completed the cycle and revealed that the shower BOOST ran for **≈3 hours** (08:36 → ~11:23) and only ended because a window was opened. The mechanism worked correctly; the *threshold* was the problem. See item 8 below for the full reasoning and four candidate solutions. This is the highest-value change to make, but it needs a decision on approach first — no change applied yet.
3. **BOOST speed (`fan_speed_high_day` 40%): no change.** Ruled out as a cause. During the long boost, CO₂ fell steadily (509→459 ppm, ≈-6.6 ppm/min) and the tachometer held rock-steady at 1150–1220 rpm — real air exchange at commanded speed. The fan was doing its job; the air it was exchanging with simply wasn't dry enough to help. Raising the speed would not have shortened the boost.
4. **Fix Bugs #1 and #2 below before relying on this day to day.** Unrelated to thresholds, but the FAULT/MANUAL priority bug (item 1) is a real behavioral defect, not a tuning question, and worth fixing before the settings above matter much in daily use.

---

## Bugs

### 1. FAULT overrides MANUAL/UIT — violates the "manual is absolute" decision — FIXED

**Status:** fixed in v2.0.1. `Controller::update()` now checks `in.mode != Mode::AUTO` before `any_bad`; manual modes (including `UIT`) are never overridden by a sensor fault. `out.fault` now reports "control sensors are bad" independent of state. Host-tested (`test_manual_overrides_fault`, `test_uit_absolute_during_sensor_fault`, `test_manual_to_auto_with_bad_sensors_enters_fault`). On-device verification (boot restored to `HOOG`; `UIT` during sensor warm-up) still open — `TODO.md` 1.2.

**Source:** `orcon.log`, lines 236–380. Restored select fires `HOOG` at 08:26:28, but the controller reports `state=FAULT speed=15 reason=sensor_stale_or_invalid` and holds the fan at 15% for ~82s (while the SGP41 completes its 90-sample stabilization and SCD4x produces its first reading), instead of the requested 85%. It only honors `HOOG` once all four sensors go valid at 08:27:50.

**Cause:** `include/orcon_controller.h:121-128` — `Controller::update()` checks `any_bad` (sensor staleness/NaN) unconditionally *before* checking `in.mode != Mode::AUTO`:

```cpp
const bool any_bad = !in.voc_ok || !in.co2_ok || !in.rh_ok || !in.nox_ok;
if (any_bad) {
  state_ = State::FAULT; ...
} else if (in.mode != Mode::AUTO) {
  state_ = State::MANUAL; ...
```

Manual modes never read the sensors — they map directly to a fixed speed — so sensor staleness has no business blocking them. But because FAULT is checked first, **any manual selection made while a sensor is warming up (every boot, ~80–90s) or glitches later is silently overridden to the FAULT idle speed instead of the requested speed.**

**Why this matters:** `.plan`'s decisions table states, as a non-negotiable, already-agreed decision: *"UIT is absolute... it never overrides the user's UIT selection."* `ARCHITECTURE.md` says the same for all manual modes ("thresholds are not evaluated and the fan holds the mode's fixed speed regardless of air quality"). This is a direct violation of that decision, not a style nit — a `UIT` command during a sensor hiccup would leave the fan running at 15% instead of off.

**Suggested fix:** check mode before sensor validity. If `mode != AUTO`, always set `state_ = MANUAL` / `target_speed = manual_speed(mode)`, regardless of `any_bad`. Sensor validity should only gate the AUTO path. The `Problem`/FAULT diagnostic can still be raised independently of the state that drives fan speed (e.g. a separate "sensors read bad, AUTO won't work correctly if you switch back" flag), so the information isn't lost — it just stops being the same variable that decides fan speed.

### 2. Boot-time config/seed race (latent, not yet observed causing a visible symptom) — FIXED

**Status:** fixed in v2.0.1. `configure()`/`seed()` now run at `on_boot` priority 800, before Wi-Fi/API and other components' `setup()`. `Controller::update()` is additionally a no-op (`reason = "not_configured"`) until `configure()` has run, as a fail-safe independent of ESPHome's priority ordering. `seed()` now also primes the HOLD/BOOST timers relative to boot-time `millis()`. Host-tested (`test_update_is_noop_before_configure`, `test_seed_primes_hold_timer`, `test_seed_primes_boost_dwell`). On-device verification (confirm restored globals are read before priority-800 seeding, and a HOLD-parked reboot uses restored state) still open — `TODO.md` 2.3.3/2.3.4.

**Source:** `orcon.log`, cross-referencing timestamps. The select's restore fires its `on_value` (line 236, 08:26:28 — ~6s after logger init, during ESPHome component `setup()`), which runs a real `evaluate_air_quality` evaluation. But `orcon.yaml:9-49`'s `on_boot:` block — which loads YAML substitutions into `orcon::Config` via `configure()` and seeds `state_`/`current_speed_` from the restored globals via `seed()` (`orcon.yaml:47-49`) — only runs after a 15s delay, and the `on_boot` trigger itself fires after Wi-Fi connects, later still (~08:26:37+ based on the wifi/mDNS/API log entries preceding it).

That means the first one or two evaluations after boot run against the **header's hardcoded default `Config`** (`include/orcon_controller.h:35-58`) and a **default, unseeded `state_`/`current_speed_`** (`IDLE`/`15`), not the YAML-tuned values or the actually-restored `ctrl_state`/`current_target_speed`.

**Why it hasn't visibly broken anything yet:** the header's defaults happen to numerically match the current YAML substitutions, and the restored state in both captured sessions happened to be `IDLE`/`15` (or got overridden by bug #1 anyway). Nothing currently exposes the gap.

**When it would bite:** if a substitution is ever tuned away from the header's default, or the device reboots while parked in `HOLD` at a non-default speed (e.g. night hold 25%) — the pre-seed evaluation(s) would briefly run with the wrong thresholds/speeds/state for a few seconds until `on_boot` catches up.

**Suggested fix:** move `configure()`/`seed()` to run before any component that can trigger an evaluation — e.g. give the boot lambda an early `on_boot: priority:` (before `WIFI`/`AFTER_CONNECTION`), or gate `evaluate_air_quality` to no-op until a "configured" flag is set.

---

## Confirmed working (positive findings, no action needed)

- **Shower (dRH/dt) detection fired correctly under a real shower** (`orcon2.log`, line 157): `state=BOOST speed=40 reason=boost_triggered` at 08:36:49 with `rh=57` — **below** the 60% absolute `rh_assert` threshold. VOC (102), CO₂ (507), NOx (1) were all well under their own assert thresholds too, so the trigger can only have been the dRH/dt latch (`include/orcon_controller.h:229`, `shower_rate_assert_pct = 3.0`): RH held flat at 54% from 08:33:47–08:36:17, then rose to 57% within about 3 minutes — a +3%RH move inside the 5-minute window, right at the configured trigger. This is exactly the point of shower detection (catching the rise before the absolute level is crossed) and it worked as designed in the field.
- The `rh_sample_min_interval_ms` guard (`include/orcon_controller.h:48,105`) correctly kept the 10-slot ring buffer spanning ~5 minutes despite `update()`/`note_rh()` being called far more often than every 30s (up to ~4 sensors × their own on_value cadence) — confirms that safeguard was necessary and is functioning.
- Persistence, cooldown gating, hysteresis (no oscillation while RH sat between assert/release bands), and manual round-trips all behaved per spec across all three logs.
- **`orcon3.log`: hysteresis dead-band held BOOST correctly, exactly as designed** (see item 6 below) — the fan didn't chatter in and out of BOOST while RH sat between 55% and 60%, which is precisely what the assert/release split is for. It looks like "stuck" from the outside, but it's the intended behavior, not a malfunction.
- **`orcon3.log`: CO₂ decline (509→459 ppm over ~8 minutes) and a rock-steady tachometer (~1150–1220 rpm at commanded 40%)** together indicate the fan is running as commanded and real air exchange is happening — the RH plateau is not explained by a fan or airflow problem.

---

## Potential improvements (not bugs)

### 3. `humidity_disagreement_margin` (15 points) is confirmed too loose for this sensor pairing — see recommendation 1 above — APPLIED

**Status:** applied in v2.0.1. Substitution changed 15 → 10 (`orcon.yaml`). Config-only, no logic change. On-device observation (a full day plus one shower cycle, confirming `Sensor Disagreement` stays off) still open — `TODO.md` 3.3.

**Source:** `orcon2.log` and `orcon3.log`. Across both — the shower itself and the 8.5 minutes afterward — SHT4x humidity reads consistently 6–8.5 percentage points lower than SCD4x humidity (e.g. `orcon2.log` 08:38:48: SHT4x 61.18% vs SCD4x 67.82%; `orcon3.log` throughout: control-path RH 59-60% vs SCD4x Humidity 67.0–67.5%). The gap is stable across the full range of absolute humidity observed (~54% to ~68%), which points to a fixed calibration offset between the two sensors (SCD4x's onboard RH sensor is typically less accurate than a dedicated SHT4x) rather than noise or a shower artifact. The `Sensor Disagreement` binary_sensor (`orcon.yaml:154,342`) never fires because the gap never reaches the 15-point margin. See recommendation 1 at the top of this document.

### 4. Occasional duplicate evaluations within ~10ms (cosmetic)

**Source:** `orcon2.log`/`orcon3.log`, e.g. `orcon2.log` lines 130–131 (08:36:17.501/.511), and repeatedly in `orcon3.log` (e.g. lines 55–56, 159–160, 193–194, 258–259). When two sensors' `on_value` triggers land within a few ms of each other, `evaluate_air_quality` runs twice back-to-back — the first passes the cooldown gate and evaluates for real, the second is correctly caught by the cooldown gate a moment later. Harmless (cooldown does its job), but produces duplicate log lines, occasionally with slightly different sensor snapshots between the two (a real new reading landing in the gap, not a bug). Low priority; could debounce simultaneous multi-sensor triggers into a single evaluation if log noise becomes a real annoyance.

### 5. Full shower release/HOLD/dwell cycle — RESOLVED, confirmed on-device

`orcon4.log` (11:26–11:30) captures the tail of the cycle and confirms it completes correctly end to end:

```
11:26–11:28  HOLD, 35%     rh=55
11:28:47     hold_expired → IDLE, 15%
11:29:19     IDLE, stable
```

BOOST released once RH crossed below the 55% release threshold, dwell was already satisfied, HOLD started, ran its full 300 s, and expired cleanly to IDLE. The state machine, hysteresis release, dwell and hold timer all behave as designed on real hardware — matching the host tests (`test_shower_detection_fires_and_clears`, `test_boost_min_dwell_blocks_early_release`).

The mechanism is sound. What this log *did* expose is that the boost lasted ≈3 hours and needed a window opened to end — a threshold-tuning problem, not a logic problem. See item 8.

### 6. Open verification — steps never run on hardware

Carried over from the (now retired) build checklist. Everything below was implemented and verified as far as possible without a physical unit (host tests pass, `esphome config` and a full `esphome compile` both succeed), but these checks need the device:

- Flash and confirm boot.
- **Autonomy test:** stop Home Assistant entirely; confirm the log shows valid SNTP time, AUTO evaluations continue, the day/night profile is still correct, and mode changes via the ESPHome web GUI take effect.
- **Persistence:** power-cycle while in HOOG and confirm it boots back into HOOG; clear the restore value and confirm fallback to AUTO.
- **Fail-safe:** disconnect the I²C bus; confirm FAULT within the staleness timeout, fan at 15%, and clean recovery.
- **HA entity_ids unchanged** across the v1.0→v2.0.0 filter split — check each of the four presentation sensors.
- **Tacho calibration pass:** log RPM at commanded 0/15/25/30/35/40/55/85%. This is the prerequisite for the commanded-vs-actual RPM band check and a `fan_fault` entity; it cannot be fabricated and needs the unit running across the speed range.

### 7. Why `orcon3.log` looks "stuck" in BOOST — hysteresis dead-band, not a bug

**Source:** `orcon3.log`, entire file (08:41:32–08:50:02). RH sits at 59–60% the whole time: just under the 60% `rh_assert` (so it can't re-trigger BOOST further) but well above the 55% `rh_release` (so the already-latched RH assert/release pair — `include/orcon_controller.h:220-223` — never clears). The shower-rate latch has almost certainly already released on its own during this window: its release condition compares against a *rolling* 5-minute baseline (`rh_baseline()`, `include/orcon_controller.h:203-217`), so once RH has been flat for longer than the window, the "baseline" it compares against drifts up to meet the current value, and the rate-of-change naturally reads ~0. That's expected behavior for a rate detector, not a bug — but it does mean that once the rate latch clears, **the absolute RH latch is what's actually holding BOOST**, and it will keep holding until RH crosses below 55%, however long that takes. Given the CO₂ and tachometer evidence above (air exchange and fan speed both look normal), this is consistent with ordinary post-shower evaporation, not a control or ventilation-capacity problem. `orcon4.log` later confirmed the release does eventually happen — but took ≈3 hours and a window. See item 8.

---

### 8. Seasonal baseline drift — fixed absolute RH thresholds are the wrong tool (design decision needed)

**Status:** analysed, not implemented. Approach to be decided before any code is written.

### The observation

`orcon4.log` closed the loop: the shower BOOST that triggered at 08:36:49 (`orcon2.log`) did not release until ~11:23 — **roughly three hours** — and only because a window was opened. The state machine behaved exactly as specified throughout. The problem is what the specification asks it to compare against.

### Why it happened

The two RH triggers behave completely differently with respect to baseline:

| Trigger | Reference | Seasonally robust? |
|---|---|---|
| dRH/dt (shower detection) | *Rolling* 5-min baseline (`rh_baseline()`, `include/orcon_controller.h:203-217`) | **Yes** — self-adapting |
| Absolute RH latch (60% / 55%) | Fixed constants | **No** — encodes one season's assumption |

The rate detector fires on a +3 %RH *rise* regardless of starting point: 35→38 in winter behaves identically to 55→58 in summer. It needs no seasonal tuning, ever. It also releases on its own once RH plateaus, because its rolling baseline drifts up to meet the current value (item 7).

The absolute latch is the fragile one, and its failure modes are asymmetric:

- **Winter** (dry indoor baseline ~35–45%): a shower may peak at 52% and never reach the 60% assert point. The absolute latch never fires — but the rate detector still catches the shower, and release is immediate afterwards. **Degrades gracefully; not a real problem.**
- **Summer / humid weather** (baseline 58–62%): the absolute latch asserts and stays asserted more or less permanently, because indoor RH never falls below 55% unaided. The fan boosts for hours, pushing in outdoor air that is just as humid — noise and energy for no moisture removal. **This is what produced the three-hour boost.** The measured baseline was ~54%, so a post-shower plateau at 59–60% sat pinned above the release point with nowhere to go.

The mechanism didn't misbehave. But for most of those three hours it was doing something physically pointless: ventilating humid air with equally humid air.

**Ruled out as causes:** fan speed and airflow. CO₂ fell steadily throughout (509→459 ppm) and the tachometer held 1150–1220 rpm at commanded 40% — real air exchange at the commanded rate. Raising `fan_speed_high_day` would not have shortened the boost.

### Candidate solutions

All four are worth having and they compose; they are not mutually exclusive. Listed by effort/benefit ratio.

**Option 1 — Adaptive baseline for the absolute latch.** Replace the fixed 60/55 constants with *offsets* above a long-run RH median (tracked over hours to days): e.g. assert at baseline + 6, release at baseline + 2. Self-tunes across seasons with no user input, no new hardware, and no external dependency. Reuses the ring-buffer concept already present in the header, just over a much longer window. Would have ended the three-hour boost on its own, because the plateau itself would have become the new baseline.
*Open questions:* window length (hours? a rolling day?), whether the baseline should freeze while in BOOST to avoid chasing its own tail, and what offsets to use.

**Option 2 — Maximum BOOST duration cap.** Regardless of *why* BOOST latched, fall back to HOLD/IDLE after a ceiling (e.g. 30–45 min). A pure safety valve: it bounds worst-case behaviour from any cause — a stuck sensor, a humid week, a threshold that no longer suits the season. Cheap, easy to test, and complements every other option here.
*Open question:* the cap value, and whether expiry should go to HOLD (gentler) or straight to IDLE.

**Option 3 — Absolute humidity or dew point instead of relative humidity.** Physically the most correct measure of "is there actually more water in this air". Computable today — temperature is already available from both SHT4x and SCD4x, so no new hardware. Dew point is far less seasonally skewed than RH, since it doesn't move when only temperature changes.
*Caveat:* ventilation only *removes* moisture when outdoor absolute humidity is below indoor, which cannot be known without an outdoor reference. So this sharpens the measurement but does not by itself solve the "ventilating humid with humid" problem — it pairs naturally with Option 4.

**Option 4 — Outdoor reference from Home Assistant weather.** The physically ideal input: compare indoor vs outdoor absolute humidity and only boost when ventilation can actually help. Directly solves the root problem.
*Caveat:* reintroduces a Home Assistant dependency, which the architecture explicitly forbids as a *precondition*. Only acceptable as **optional enrichment with a complete fallback** to on-device logic when HA is unavailable — i.e. the controller must still work fully without it. That is meaningfully more machinery than Options 1–3, and it must not be allowed to become a hard dependency.

### Suggested sequencing (if all four are wanted)

1 and 2 first — they are self-contained, host-testable, and between them fix the observed failure and bound the worst case. 3 next, as a measurement upgrade that makes the thresholds more meaningful. 4 last, as an optional accuracy layer on top, gated behind a hard requirement that losing HA degrades cleanly back to 1–3.

---

## 9. Fan never actually started on a fresh boot — found during v2.0.1 rollout, FIXED

**Source:** `orcon5.log`, first real-hardware boot of v2.0.1. From boot (13:24:13) until the user manually selected `HOOG` at 13:34:29 — over ten minutes — `Controller State` cycled `FAULT` → `IDLE` and `Commanded Fan Speed` read `15.0%` throughout, but `Fan Tacho` stayed at `0.00 rpm` the entire time and no `'Fan' >> ON` log line was ever emitted. The moment `HOOG` (85%) was selected, `'Fan' >> ON, Speed: 85` appeared immediately and the tacho confirmed real rotation; switching back to `AUTO` afterwards also worked (`'Fan' >> ON, Speed: 15`). This matches the report: "manual profiles work, back to automatic works" — only the untouched-since-boot AUTO/FAULT path was silently inert.

**Cause:** `Controller::update()` only issues `out.speed_changed = true` (and thus only `orcon.yaml`'s `fan_motor.turn_on()`/`make_call()` fires) when the newly computed `target_speed` differs from `current_speed_`. On a fresh boot, `current_speed_` is seeded to `15` (the persisted global's `initial_value`), and the very first real evaluation — while sensors are still stabilizing — computes `state=FAULT`, `target_speed=speed_fault=15`. `15 == 15`, so `speed_changed` was false and the fan command was never issued. But `orcon.yaml`'s `fan_motor` is `restore_mode: ALWAYS_OFF` — the *physical* fan always boots off regardless of what `current_speed_` remembers — so the controller believed it was already running at 15% while the hardware sat off, indefinitely, until some later evaluation computed a genuinely different speed (here, a manual mode selection).

**Why v2.0.1 made this deterministic:** before the boot-race fix (item 2, this document), the exact sequence of early evaluations before `configure()`/`seed()` ran was somewhat timing-dependent, so this could sometimes go unnoticed. With `configure()`/`seed()` now running first and deterministically at `on_boot` priority 800, the first real evaluation on every fresh install reliably computes `FAULT`/`15`, matching the seeded `15` every time — so the dormant defect became a guaranteed failure on first boot.

**Fix:** `Controller` gained a `commanded_once_` flag. `speed_changed` is now `(target_speed != current_speed_) || !commanded_once_`, forcing exactly one real fan command on the first evaluation after `configure()`, regardless of whether the computed target happens to equal the seeded speed. Host-tested (`test_first_evaluation_always_commands_fan`).
