# BUGFIX.md — findings from field logs `orcon.log`, `orcon2.log`, `orcon3.log`

Analysis only, no code changed. See chat history for the full walkthrough; this is the consolidated punch list.

---

## Recommended changes (thresholds & settings)

Based on all three logs (boot → shower at ~08:33 → 08:50, i.e. ~17 minutes of continuous post-shower data across `orcon2.log`+`orcon3.log`), here's what I'd actually change versus leave alone:

1. **`humidity_disagreement_margin`: lower from 15 to ~10.** Two independent stretches of data (`orcon2.log`, `orcon3.log`) now show a *stable, repeatable* 6–8.5 point gap between SHT4x and SCD4x humidity across the whole range observed (ambient ~54% up to post-shower ~68%) — see item 3 below. At margin=15 the `Sensor Disagreement` diagnostic can never fire for this hardware pair in practice; a real fault would need to diverge by more than 15 points to be caught. 10 still comfortably clears the observed ~8.5-point maximum while giving the diagnostic a real chance to flag an actual problem later (a sensor stuck, disconnected, or badly drifted). Config-only change (`orcon.yaml` substitution `humidity_disagreement_margin`), no logic change.

2. **RH assert/release (60% / 55%) and BOOST speed (`fan_speed_high_day` 40%): no change yet — let it run longer first.** `orcon3.log` shows RH plateaued at 59–60% for the entire 8.5-minute capture, sitting inside the hysteresis dead-band (above the 55% release point, just under the 60% assert point) — see the orcon3 analysis below. This reads as normal post-shower physics, not an under-ventilation problem: CO₂ fell steadily the whole time (509→459 ppm, ≈-6.6 ppm/min), proving real air exchange is happening at 40%, and the fan tachometer held rock-steady around 1150–1220 rpm the entire time — nothing here points at insufficient airflow or a stuck fan. A wet bathroom (walls, mirror, floor, towels) commonly keeps radiating humidity for 10–20+ minutes after the water is off; 8.5 minutes of plateau isn't long enough to conclude the room "isn't clearing" or that 40% is too weak. **Recommendation: capture one more log through to an actual release (RH crossing back below 55% → HOLD → IDLE).** Only reconsider raising `fan_speed_high_day` or nudging the release threshold up if RH is still sitting above ~58% a full 20–30 minutes after the peak in a repeat session.
3. **Fix Bugs #1 and #2 below before relying on this day to day.** Unrelated to thresholds, but the FAULT/MANUAL priority bug (item 1) is a real behavioral defect, not a tuning question, and worth fixing before the settings above matter much in daily use.

---

## Bugs

### 1. FAULT overrides MANUAL/UIT — violates the "manual is absolute" decision

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

### 2. Boot-time config/seed race (latent, not yet observed causing a visible symptom)

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

### 3. `humidity_disagreement_margin` (15 points) is confirmed too loose for this sensor pairing — see recommendation 1 above

**Source:** `orcon2.log` and `orcon3.log`. Across both — the shower itself and the 8.5 minutes afterward — SHT4x humidity reads consistently 6–8.5 percentage points lower than SCD4x humidity (e.g. `orcon2.log` 08:38:48: SHT4x 61.18% vs SCD4x 67.82%; `orcon3.log` throughout: control-path RH 59-60% vs SCD4x Humidity 67.0–67.5%). The gap is stable across the full range of absolute humidity observed (~54% to ~68%), which points to a fixed calibration offset between the two sensors (SCD4x's onboard RH sensor is typically less accurate than a dedicated SHT4x) rather than noise or a shower artifact. The `Sensor Disagreement` binary_sensor (`orcon.yaml:154,342`) never fires because the gap never reaches the 15-point margin. See recommendation 1 at the top of this document.

### 4. Occasional duplicate evaluations within ~10ms (cosmetic)

**Source:** `orcon2.log`/`orcon3.log`, e.g. `orcon2.log` lines 130–131 (08:36:17.501/.511), and repeatedly in `orcon3.log` (e.g. lines 55–56, 159–160, 193–194, 258–259). When two sensors' `on_value` triggers land within a few ms of each other, `evaluate_air_quality` runs twice back-to-back — the first passes the cooldown gate and evaluates for real, the second is correctly caught by the cooldown gate a moment later. Harmless (cooldown does its job), but produces duplicate log lines, occasionally with slightly different sensor snapshots between the two (a real new reading landing in the gap, not a bug). Low priority; could debounce simultaneous multi-sensor triggers into a single evaluation if log noise becomes a real annoyance.

### 5. Full shower release/HOLD/dwell cycle still not observed on-device

Across `orcon2.log` + `orcon3.log`, ~17 minutes of continuous post-trigger data (08:36:49 → 08:50:02) never reached the RH-release threshold (55%) — RH peaked around 61% and has only come down to 59-60% so far. The eventual drop below 55% → dwell met → `HOLD` → timer expiry → `IDLE` sequence after a real shower still hasn't been confirmed on-device, only via the host test suite (`test_shower_detection_fires_and_clears`, `test_boost_min_dwell_blocks_early_release`). Per recommendation 2 above, capture a longer session through to actual release before concluding anything needs tuning.

### 6. Open verification — steps never run on hardware

Carried over from the (now retired) build checklist. Everything below was implemented and verified as far as possible without a physical unit (host tests pass, `esphome config` and a full `esphome compile` both succeed), but these checks need the device:

- Flash and confirm boot.
- **Autonomy test:** stop Home Assistant entirely; confirm the log shows valid SNTP time, AUTO evaluations continue, the day/night profile is still correct, and mode changes via the ESPHome web GUI take effect.
- **Persistence:** power-cycle while in HOOG and confirm it boots back into HOOG; clear the restore value and confirm fallback to AUTO.
- **Fail-safe:** disconnect the I²C bus; confirm FAULT within the staleness timeout, fan at 15%, and clean recovery.
- **HA entity_ids unchanged** across the v1.0→v2.0.0 filter split — check each of the four presentation sensors.
- **Tacho calibration pass:** log RPM at commanded 0/15/25/30/35/40/55/85%. This is the prerequisite for the commanded-vs-actual RPM band check and a `fan_fault` entity; it cannot be fabricated and needs the unit running across the speed range.

### 7. Why `orcon3.log` looks "stuck" in BOOST — hysteresis dead-band, not a bug

**Source:** `orcon3.log`, entire file (08:41:32–08:50:02). RH sits at 59–60% the whole time: just under the 60% `rh_assert` (so it can't re-trigger BOOST further) but well above the 55% `rh_release` (so the already-latched RH assert/release pair — `include/orcon_controller.h:220-223` — never clears). The shower-rate latch has almost certainly already released on its own during this window: its release condition compares against a *rolling* 5-minute baseline (`rh_baseline()`, `include/orcon_controller.h:203-217`), so once RH has been flat for longer than the window, the "baseline" it compares against drifts up to meet the current value, and the rate-of-change naturally reads ~0. That's expected behavior for a rate detector, not a bug — but it does mean that once the rate latch clears, **the absolute RH latch is what's actually holding BOOST**, and it will keep holding until RH crosses below 55%, however long that takes. Given the CO₂ and tachometer evidence above (air exchange and fan speed both look normal), this is consistent with ordinary post-shower evaporation, not a control or ventilation-capacity problem. Revisit only per the condition in recommendation 2.
