# TODO — BUGFIX implementation checklist

Derived from `BUGFIX.plan`. Each task is small and independently commit-able.
`make -C test` must pass before and after every commit.

Legend: **[H]** host tests · **[HW]** hardware test · **[C]** configuration/docs only ·
_(optional)_ = not required for the success criterion.

---

## Phase 1 — Correctness bugs

### 1.1 FAULT must not override MANUAL/UIT — `BUGFIX.plan` §1

- [x] **1.1.1** Add failing host tests first: `test_manual_overrides_fault`,
      `test_uit_absolute_during_sensor_fault`,
      `test_manual_to_auto_with_bad_sensors_enters_fault` in
      `test/test_controller.cpp`, registered in `main()`. **[H]**
- [x] **1.1.2** In `include/orcon_controller.h` `update()`, check
      `in.mode != Mode::AUTO` before `any_bad`; move the `any_bad` → FAULT
      branch inside the AUTO path. **[H]**
- [x] **1.1.3** Set `out.fault = any_bad;` (was `state_ == State::FAULT`) so the
      `Problem` diagnostic survives the precedence change. No YAML edit. **[H]**
- [x] **1.1.4** Confirm the full existing suite still passes unmodified
      (`test_nan_enters_fault_and_recovers`,
      `test_manual_roundtrip_returns_to_idle`). **[H]**
      Note: these two, plus every other pre-existing test, needed one new
      line each (`c.configure(Config());`) once 2.1.2 landed — the
      `configured_` guard means a bare `Controller c;` no longer evaluates.
      Behavioural assertions in both tests are unchanged.
- [x] **1.1.5** Update `ARCHITECTURE.md` lines 82 / 143 / 172: manual precedence
      is absolute; `Problem` now means "control sensors bad", not "state ==
      FAULT". **[C]**

### 1.2 Verification on device — `BUGFIX.plan` §1

- [ ] **1.2.1** Boot with the select restored to `HOOG`; confirm 85 %
      immediately, no 15 %/~80 s window. **[HW]**
- [ ] **1.2.2** Select `UIT` during SGP41 stabilisation; confirm the fan stops
      (the `target_speed <= 0` path in `orcon.yaml:314-323`). **[HW]**

---

## Phase 2 — Boot sequence

### 2.1 Controller no-op until configured — `BUGFIX.plan` §2 (2b)

- [x] **2.1.1** Add host test `test_update_is_noop_before_configure`, including
      the assertion that the gated call does not consume the cooldown. **[H]**
- [x] **2.1.2** Add `bool configured_` to `Controller`, set in `configure()`;
      early-return from `update()` with `reason = "not_configured"`,
      `speed_changed = false`, before touching `evaluated_once_`/`last_eval_ms_`
      /latches. **[H]**

### 2.2 Prime timers in `seed()` — `BUGFIX.plan` §2 (edge case)

- [x] **2.2.1** Add host tests `test_seed_primes_hold_timer` and
      `test_seed_primes_boost_dwell`. **[H]**
- [x] **2.2.2** Extend `seed()` to take `now_ms` and prime `hold_until_ms_` /
      `boost_entered_ms_`; document that `configure()` must be called first.
      **[H]**
- [x] **2.2.3** Update the `seed(...)` call in `orcon.yaml:48-50` to pass
      `millis()`. **[C]**

### 2.3 Run configure/seed early — `BUGFIX.plan` §2 (2a)

- [x] **2.3.1** Split `orcon.yaml:9-51` `on_boot` into an early
      `priority: 800` block containing the existing configure/seed lambda
      verbatim, and a default-priority block keeping `delay: 15s` +
      `script.execute: evaluate_air_quality`. **[C]**
- [x] **2.3.2** `esphome config` + `esphome compile` clean. **[C]**
      Verified: `esphome config orcon.yaml` → "Configuration is valid!";
      `esphome compile orcon.yaml` → "Successfully compiled program."
- [ ] **2.3.3** On device: confirm the boot log shows the restored
      `ctrl_state`/`current_target_speed` and not the globals' `initial_value`
      — if priority 800 runs before globals restore, lower it to the highest
      priority that still reads restored values. **[HW]**
- [ ] **2.3.4** On device: reboot parked in HOLD at night speed (25 %) and
      confirm the first evaluation uses the restored state and YAML config.
      **[HW]**
- [x] **2.3.5** Update `ARCHITECTURE.md` boot-sequence description and
      `ARCHITECTURE.md:222` (open-defects pointer). **[C]**

---

## Phase 3 — Configuration changes

- [x] **3.1** `orcon.yaml:154`: `humidity_disagreement_margin` `"15"` → `"10"`.
      `BUGFIX.plan` §3. **[C]**
- [x] **3.2** Update the `ARCHITECTURE.md:212` tuning table (value + drop the
      "likely too loose" note). **[C]**
- [ ] **3.3** On device: one full day plus one shower cycle with
      `Sensor Disagreement` staying off; fall back to `"12"` if it proves noisy.
      **[HW]**
- [x] **3.4** Add a v2.0.1 section to `CHANGELOG.md` covering Phases 1–3. **[C]**

---

## Phase 4 — Optional improvements

- [ ] **4.1** _(decision task, not implementation)_ Resolve the five open
      questions in `BUGFIX.plan` §8 "Decision still required": which options are
      in scope, boost-cap value/target/re-arm, adaptive-baseline window/freeze/
      offsets/persistence, dew point replace-or-alongside, and whether the HA
      outdoor reference is wanted at all. Record the outcome in `.plan`. **[C]**
      **No implementation task exists until this is answered.**
- [ ] **4.2** _(optional, recommended not to do)_ Debounce near-simultaneous
      sensor triggers into a single evaluation — `BUGFIX.plan` §4. Cosmetic log
      noise only; the proposed `mode: restart` + leading delay changes the timing
      of every evaluation. Leave open unless the noise becomes a real annoyance.
      **[C]**

---

## Phase 5 — Validation

Hardware checks from `BUGFIX.plan` §6. Run 5.2 and 5.3 **after** Phases 1–2, as
both touch those paths.

- [ ] **5.1** Flash and confirm boot; first evaluation logged. **[HW]**
- [ ] **5.2** Autonomy: stop Home Assistant entirely; confirm valid SNTP time,
      continued AUTO evaluations, correct day/night profile, working mode changes
      via the ESPHome web GUI. **[HW]**
- [ ] **5.3** Persistence: power-cycle while in HOOG → boots into HOOG; clear the
      restore value → falls back to AUTO. **[HW]**
- [ ] **5.4** Fail-safe: disconnect I²C → FAULT within `staleness_timeout_ms`,
      fan at 15 %, clean recovery. **[HW]**
- [ ] **5.5** HA entity_ids unchanged across the v1.0 → v2.0.0 filter split —
      check all four presentation sensors. **[HW]**
- [ ] **5.6** Tacho calibration pass: log RPM at commanded
      0/15/25/30/35/40/55/85 %. Record data only; no `fan_fault` entity in
      scope. **[HW]**
- [x] **5.7** Mark `BUGFIX.md` items 5 and 7 as closed/no-action and refresh
      `ARCHITECTURE.md:231` so they are not re-verified. **[C]**
      `BUGFIX.md` already carried "RESOLVED"/"No Action Required" status
      headings for items 5 and 7. `ARCHITECTURE.md`'s Known Gaps entry that
      pointed at items 1/2 as open defects was replaced with a pointer to
      item 8 (the only defect still open).

---

## Phase 6 — Fan never actually started on a fresh boot (found in v2.0.1 rollout)

`BUGFIX.md` item 9. `orcon5.log`: fan stayed physically off (0 rpm) from boot
until a manual mode picked a speed different from the seeded default; the
first real AUTO/FAULT evaluation computed the same speed (15%) as the seeded
`current_speed_`, so `speed_changed` never fired and the ALWAYS_OFF fan was
never actually commanded.

- [x] **6.1** Add host test `test_first_evaluation_always_commands_fan`. **[H]**
- [x] **6.2** Add `bool commanded_once_` to `Controller`; `speed_changed =
      (target_speed != current_speed_) || !commanded_once_`, set
      `commanded_once_ = true` alongside. **[H]**
- [x] **6.3** Confirm full suite passes (`make -C test`) and `esphome config`
      stays clean. **[H]/[C]**
- [x] **6.4** Record the finding and fix in `BUGFIX.md` (item 9) and
      `CHANGELOG.md` (v2.0.1). **[C]**
- [ ] **6.5** On device: fresh flash (or cleared restore values), confirm
      `'Fan' >> ON` and a non-zero tacho reading appear on the very first
      evaluation, without needing a manual mode switch first. **[HW]**

---

## No action required (tracked, deliberately not scheduled)

- `fan_speed_high_day` (40 %) — ruled out as a cause by CO₂ and tacho evidence.
- Item 5 — full shower release/HOLD/dwell cycle, confirmed working on device.
- Item 7 — BOOST held by the hysteresis dead-band is intended behaviour.
