# TODO — open items only

Completed work is recorded in `CHANGELOG.md` and `BUGFIX.md`; only genuinely
outstanding items live here. `make -C test` must pass before and after every
commit.

**Before trusting any device log:** confirm the boot log shows
`controller header 2.0.3` and a fresh `compiled on` stamp. If it doesn't, the
build used a stale header and nothing else in that log means anything. The
marker appears twice — at priority 800 (serial console only) and again after
the 15 s delay (visible over the network API).

---

## Hardware verification — the v2.0.1–2.0.3 fixes

- [ ] **HW-1** Manual is absolute: boot with the select restored to `HOOG` →
      85 % immediately, no 15 %/~80 s FAULT window. Then select `UIT` during
      SGP41 stabilisation → fan actually stops. `BUGFIX.md` #1.
- [ ] **HW-2** Manual → AUTO *inside* the 30 s cooldown → fan drops to the AUTO
      speed immediately, rather than holding the manual speed until the
      cooldown expires. `BUGFIX.md` #10.
- [ ] **HW-3** Manual excursion → back to AUTO → no spurious BOOST as RH
      settles. Then confirm a **real** shower still triggers BOOST — this is
      the one that matters most, since the fix works by deliberately forgetting
      RH history. `BUGFIX.md` #11.
- [ ] **HW-4** Boot seeding: confirm the priority-800 log reports the restored
      `ctrl_state`/`current_target_speed`, not the globals' `initial_value`.
      Serial console only. If priority 800 turns out to run *before* the
      globals restore, lower it to the highest priority that still reads
      restored values. `BUGFIX.md` #2.
- [ ] **HW-5** Reboot parked in HOLD at night speed (25 %) → the first
      evaluation uses the restored state and the YAML config.

## Hardware verification — carried over, never run

- [ ] **HW-6** Autonomy: stop Home Assistant entirely → valid SNTP time, AUTO
      evaluations continue, correct day/night profile, and mode changes via the
      ESPHome web GUI take effect.
- [ ] **HW-7** Persistence: power-cycle while in HOOG → boots into HOOG; clear
      the restore value → falls back to AUTO.
- [ ] **HW-8** Fail-safe: disconnect the I²C bus → FAULT within
      `staleness_timeout_ms`, fan at 15 %, clean recovery.
- [ ] **HW-9** HA entity_ids unchanged across the v1.0 → v2.0.0 filter split —
      check all four presentation sensors.
- [ ] **HW-10** Tacho calibration: log RPM at commanded
      0/15/25/30/35/40/55/85 %. Record data only; no `fan_fault` entity is in
      scope. Prerequisite for any future commanded-vs-actual RPM check.
- [ ] **HW-11** `Sensor Disagreement` stays off across a full day plus one
      shower at the new margin of 10; fall back to `"12"` if it proves noisy.
      `BUGFIX.md` #3.

## Open decision — blocks all work on seasonal RH drift

- [ ] **D-1** Answer the five questions in `BUGFIX.plan` §8: which options are
      in scope; boost-cap value / expiry target / re-arm rule; adaptive-baseline
      window / freeze-during-BOOST / offsets / persistence; whether dew point
      replaces or runs alongside the RH thresholds; and whether the HA outdoor
      reference is wanted at all given the autonomy constraint. Record the
      outcome in `BUGFIX.plan`. **No implementation task exists until this is
      answered.** **[C]**

## Deferred, deliberately not scheduled

- [ ] **X-1** Debounce near-simultaneous sensor triggers into a single
      evaluation (`BUGFIX.plan` §4). Cosmetic log noise only, and the proposed
      `mode: restart` + leading delay would change the timing of *every*
      evaluation. Recommendation stands: leave as-is unless the noise becomes a
      real annoyance. **[C]**
