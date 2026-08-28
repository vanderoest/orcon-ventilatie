# BUGFIX.md — known open defects

Defects that were found and fixed (FAULT/MANUAL precedence, the boot-time
config/seed race, the fan never starting on a fresh boot, MANUAL speed
leaking into AUTO cooldown, a manual excursion misread as a shower) are
documented in `CHANGELOG.md`, not here — this file tracks only what is
**still open**.

---

## 8. Seasonal baseline drift — fixed absolute RH thresholds are the wrong tool (design decision needed)

**Status:** analysed, not implemented. Approach to be decided before any code is written. See `TODO.md` task **D-1**.

### The observation

A shower BOOST triggered at 08:36:49 did not release until ~11:23 — **roughly three hours** — and only because a window was opened. The state machine behaved exactly as specified throughout. The problem is what the specification asks it to compare against.

### Why it happened

The two RH triggers behave completely differently with respect to baseline:

| Trigger | Reference | Seasonally robust? |
|---|---|---|
| dRH/dt (shower detection) | *Rolling* 5-min baseline (`rh_baseline()`, `components/orcon/orcon_controller.h`) | **Yes** — self-adapting |
| Absolute RH latch (60% / 55%) | Fixed constants | **No** — encodes one season's assumption |

The rate detector fires on a +3 %RH *rise* regardless of starting point: 35→38 in winter behaves identically to 55→58 in summer. It needs no seasonal tuning, ever. It also releases on its own once RH plateaus, because its rolling baseline drifts up to meet the current value.

The absolute latch is the fragile one, and its failure modes are asymmetric:

- **Winter** (dry indoor baseline ~35–45%): a shower may peak at 52% and never reach the 60% assert point. The absolute latch never fires — but the rate detector still catches the shower, and release is immediate afterwards. **Degrades gracefully; not a real problem.**
- **Summer / humid weather** (baseline 58–62%): the absolute latch asserts and stays asserted more or less permanently, because indoor RH never falls below 55% unaided. The fan boosts for hours, pushing in outdoor air that is just as humid — noise and energy for no moisture removal. **This is what produced the three-hour boost.** The measured baseline was ~54%, so a post-shower plateau at 59–60% sat pinned above the release point with nowhere to go.

The mechanism didn't misbehave. But for most of those three hours it was doing something physically pointless: ventilating humid air with equally humid air.

**Ruled out as causes:** fan speed and airflow. CO₂ fell steadily throughout (509→459 ppm) and the tachometer held 1150–1220 rpm at commanded 40% — real air exchange at the commanded rate. Raising `fan_speed_high_day` would not have shortened the boost.

**Tachometer fact confirmed for v2.1.2:** the ebm-papst R3G190-RC05-20
connection diagram specifies an open-collector tach output with exactly one
pulse per revolution. ESPHome reports pulse-counter frequency as pulses/min, so
the value is directly RPM (`10 Hz = 600 RPM`) without a scale filter. This
supports the RPM interpretation above; calibration is still needed only to
define the normal RPM range for each commanded percentage. Source:
[motor datasheet, connection diagram page 4](https://www.fansco.com/datasheets/ebmpapst/R3G190-RC05-20.pdf).

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

### Decision still required

Before any implementation task is created:
1. Which options are in scope for the next release?
2. Option 2: cap value; expiry to HOLD or IDLE; re-arm rule.
3. Option 1: baseline window length; freeze-during-BOOST yes/no; assert/release offsets; persistence across reboot.
4. Whether Option 3 replaces the RH thresholds or runs alongside them.
5. Whether Option 4 is wanted at all, given the autonomy constraint.

---

## Small maintenance backlog — intentionally deferred

**Status:** non-critical cleanup found during the v2.1.1 review. None of these
items changes current controller behaviour; leave them for a separate
maintenance pass.

- **Repository hygiene:** `components/orcon/__pycache__/__init__.cpython-314.pyc`
  is tracked, while `.gitignore` has no `__pycache__/` or `*.pyc` rule.
- **Secrets documentation:** `README.md` says `cp secrets.yaml secrets.yaml`
  and calls the result gitignored, but `secrets.yaml` is actually a tracked
  dummy placeholder. Document a safe local-secrets workflow without treating
  the committed placeholder values as leaked credentials.
- **Stale documentation references:** the verification command in
  `ARCHITECTURE.md` still names the former `include` path, and current source
  comments/docs still refer to the no-longer-present `.plan` design file.
- **External-component validation:** `components/orcon/__init__.py` is only a
  header loader with an empty schema. YAML tunables (threshold ordering,
  speeds, durations and hours) therefore receive no component-specific range
  or relationship validation. `Config::staleness_timeout_ms` is also assigned
  but the actual freshness check currently uses the YAML substitution
  directly, leaving two apparent sources for that value.
- **Strapping-pin documentation:** ESPHome warns that the existing GPIO12
  (UART TX) and GPIO15 (fan PWM) assignments are ESP32 strapping pins. The
  current hardware is left unchanged; document the electrical pull-up/down
  constraints and prefer non-strapping pins in a future board revision.
- **Automated checks:** there is no CI/integration assertion for ESPHome boot
  ordering or publication-based staleness, and no automatic parity check
  between `project.version` and `orcon::kHeaderVersion`. The host shower test
  allows either BOOST or HOLD after release and does not conclusively prove
  that the shower latch eventually clears.
