# Duepi external room sensor and optional PI

Approved by JB on 20 October 2026. This is the binding specification for the implementation plan.

## Goal and boundary

Users select a temperature sensor from Home Assistant, choose their desired room temperature, and ESPHome adjusts the stove's native temperature setpoint. No user-calculated offset is needed. Classical PI is an optional extra, disabled by default. Implement it independently; do not copy Versatile Thermostat code.

All product code lives in ESPHome-Duepi. DUEPI_EVO is outside this change. Work on `codjardine/remote-room-regulation` from current upstream main; push only to `origin` (jbjardine/ESPHome-Duepi). No upstream push or PR. No live Home Assistant writes, restart, upload, or stove commands during development.

## Architecture and public controls

- An opt-in ESPHome external component and YAML package. Existing installations without the package retain their entity IDs and behavior.
- A new room climate entity keeps its room target separate from the native `Stove` target read by RC6000.
- A mode select: Local, Compensation, Compensation + PI. First installation starts Local; restored external mode must still wait for fresh postboot inputs.
- A configurable native fallback target, default 20 degrees C, constrained to the configured stove limits.
- A configurable maximum source age, default 15 minutes.
- Diagnostics: operating reason/status, source age, PI correction, and computed native target.
- The room climate's explicit user heat/off commands reuse the existing native path. Automatic regulation never generates heat/off commands.
- Explicit OFF on the room climate immediately selects Local and resets I. HEAT preserves the selected regulation mode, cannot reactivate external control from Local, and requires fresh post-command burning observations. Received stove-mode observations never invoke the user-command callback.
- Native Celsius support in v1. HA sources in Celsius or Fahrenheit are accepted and normalized to Celsius. Reject unsupported native units at configuration time for the opt-in feature.

The HA companion has a device selector and a temperature-sensor selector, one config entry per ESPHome module, and an options flow to replace the sensor. It creates no climate or duplicate device. It discovers the versioned text relay by stable registry identity, not a user-edited entity ID/name. Unsupported firmware is rejected. All calculations and control state stay on ESPHome.

## Calculation

`error = room_target - room_temperature`

`base = measured_stove_temperature + error`

Use real, fresh RD1000 values before the existing moving-average filter. Boot placeholder values are not measurements. Never use the current native setpoint as the base: that would accumulate corrections.

With PI disabled, PI correction and integral are zero. With PI enabled:

`I_candidate = I + Ki * error * elapsed_hours`

`correction = clamp(Kp * error + I, -correction_limit, +correction_limit)`

Initial experimental advanced YAML defaults: Kp 0.10, Ki 0.025 per hour, integral limit 0.5 degrees C, correction limit 1 degree C. Integrate only on distinct accepted source samples; heartbeat alone does not integrate. First sample after a reset has zero elapsed integration time. Prevent integration that worsens correction or native-output saturation; permit unwinding. Reset integral on target/source/mode changes, reboot, loss of usable inputs, inactive stove, bridge takeover, or manual takeover. Do not persist the integral.

Apply a single final nearest-integer rounding (positive half ties upward), then clamp to configured native min/max. Limits are not hardcoded to JB's stove. For measured stove 23 and room target 20: room 18 -> native 25; room 20 -> native 23; room 21 -> native 22.

## Freshness, ownership, and stove state

- Relay heartbeat 60 seconds; relay silence deadline 180 seconds.
- Effective source age equals supplied age plus local monotonic elapsed time. Heartbeats must not make an old source sample fresh.
- Raw stove temperature, native target, and decoded operating state must be postboot and fresh (45 seconds maximum). Never regulate from a synthetic or restored observation.
- Initial stable burning state is raw RD9000 `0x0201`. Suspend automatic writes/integration during off, startup, cleaning, cooldown, alarm, or unknown state. Re-entry requires fresh inputs.
- Minimum interval between automatic RF2 writes: 300 seconds. Do not resend an unchanged integer target. Fallback may bypass the interval once while UART is free.
- Source invalid/stale or relay silence: clear I, apply native fallback once when control owns the stove and writing is possible, without changing heat/off state. Resume automatically with fresh data unless manually overridden.
- Local mode never applies a fallback or overwrites native user choices.
- Manual native target commands from legacy HA climate, physical buttons, or MyDPremote latch Local and preserve the manual target; external mode requires explicit reactivation.
- While a bridge client is connected, issue no automatic UART command (including fallback). Reset I. On disconnect, invalidate observations and resynchronize. Changed native target versus the pre-bridge confirmed target means manual takeover; otherwise external control may resume with fresh observations.
- Track automatic RF2 intent separately from RC6000 feedback. Matching RC6000 confirms success, not the command ACK. Stop further correction and expose command-unconfirmed after 30 seconds without confirmation. A new external-mode activation clears this fault.
- Outside pending own writes, two stable divergent native readbacks indicate manual takeover; never fight a new native target with periodic writes.

Only RF2 is produced by the automatic controller. Do not change P1-P5, fan/extractor commands, factory parameters, or the existing protocol parser beyond the required observation hooks.

## Tests and delivery

Deterministic controller and parser tests, focused HA fixture tests, and ESPHome 2026.9.1 validation/builds: legacy config, opt-in ESP-01 1 MB (including OTA size fit), and opt-in ESP32-C3. No full HA suite. Test dependencies are development only.

Deliver tested code, build evidence, installation instructions and a separate field-test protocol before any deployment. That protocol must avoid competing VTherm control, begin in Compensation without PI, verify RC6000 and real applied power RD4000 on multiple spaced observations, and keep radiators/factory settings unchanged. Sparse source reports may force fallback; precise 20 degree regulation and PI benefit are not established by software tests.
