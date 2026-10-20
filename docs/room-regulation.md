# Optional room-temperature regulation

The room-regulation package is opt-in. It uses the ESPHome Duepi component to
keep a separate room target and, when selected, adjust only the stove's native
temperature setpoint. It does not set fan speed or power. Existing installations
retain their existing entities and behavior until this package is added.

The package, base Duepi package, and external component must all come from the
same Duepi fork and ref. The examples pin all three to
`codjardine/remote-room-regulation`; upstream `main` does not yet contain the
required raw observation hooks.

## Install

Start from an existing Duepi ESPHome configuration and keep its current base
package. Add the optional package and external component using the same fork
and ref as the base package:

```yaml
packages:
  duepi:
    url: https://github.com/jbjardine/ESPHome-Duepi.git
    file: components/packages.yaml
    ref: codjardine/remote-room-regulation
  room_regulation:
    url: https://github.com/jbjardine/ESPHome-Duepi.git
    file: components/packages/room_regulation.yaml
    ref: codjardine/remote-room-regulation

external_components:
  - source:
      type: git
      url: https://github.com/jbjardine/ESPHome-Duepi.git
      ref: codjardine/remote-room-regulation
    components: [duepi_room]
```

Keep the existing Duepi substitutions (`min_temp`, `max_temp`, `temp_unit`,
`tx_pin_esp`, and `rx_pin_esp`) in the node configuration. With the optional
package, the minimum and maximum may use Celsius or Fahrenheit according to
`temp_unit`; the component converts them once to integer Celsius limits for
control and UART validation. The examples contain complete
ESP-01 1 MB and LOLIN C3 Mini configurations and can also be used as a starting
point: [ESP-01](../examples/room-regulation-esp8266.yaml) and
[C3 Mini](../examples/room-regulation-lolin-c3-mini.yaml).

The configuration expects local ESPHome secrets for the API encryption key,
Wi-Fi credentials, OTA password, and fallback access point password. Do not
copy the dummy values from `tests/fixtures/secrets.yaml` into a device
configuration.

Compile and install through the usual ESPHome workflow after checking the
resolved configuration. No room regulation is active on first install: the
mode starts in **Local**. If a previous external mode is restored, it still
waits for fresh stove and room-source observations before acting.

## Controls and diagnostics

The optional package adds these entities:

| Entity | Purpose |
| --- | --- |
| `Room target` climate | Holds the desired room temperature independently from the stove's native setpoint. Its displayed Heat/Off follows the latest recognized raw stove phase; an unknown phase leaves the last display unchanged. This is observation only. Explicit Heat and Off calls use the existing native mode path. Off immediately selects Local. Heat keeps the selected mode and does not activate regulation from Local. |
| `Room regulation mode` select | **Local**, **Compensation**, or **Compensation + PI**. This is the armed/manual regulation choice. Local leaves native setpoint choices alone. Compensation applies the room-temperature offset. PI adds a bounded integral correction. |
| `Reactivate room compensation` button | Selects **Compensation** through the existing mode selector. It does not send a Heat/Off command or turn on the stove. |
| `Room temperature sensor` text (`duepi_room_source`) | Optional Home Assistant `sensor.*` entity ID used for room measurements. It starts empty, is restored across reboot, and can be changed on the device without recompiling. Empty disables native room-source requests. |
| `Room fallback target` number | Native setpoint used once when the selected regulation mode loses a usable room source. Default: 20 °C (68 °F); it is limited to the configured stove minimum and maximum. |
| `Room regulation status` text sensor | Operating reason: `local`, `waiting_stove`, `waiting_source`, `regulating`, `fallback`, `bridge`, `stove_inactive`, `awaiting_confirmation`, `command_unconfirmed`, or `uart_busy`. |
| `Room source age` sensor | Age reported by Home Assistant for the selected room sensor, in seconds. The firmware's default maximum is 15 minutes. |
| `Room PI correction` sensor | Current bounded PI correction as a Celsius difference (`Δ°C`), without an absolute-temperature device class. It stays zero in Local and Compensation modes. |
| `Room computed native target` sensor | The computed native setpoint before the RF2 write request. |
| `Duepi Room Relay v1` text | Legacy versioned JSON relay input retained for compatibility. It is ignored while `Room temperature sensor` is non-empty. Do not use both transports for the same source. |

The optional package reads the selected source through the standard ESPHome
Home Assistant integration. Enable the per-device `allow_service_calls`
permission for this ESPHome device in Home Assistant. The firmware requests a
measurement after the API connects, after the source changes, and every
60 seconds while a source is selected, including in **Local** mode. It uses
the official `homeassistant.action` with an inline `data_template`; Home
Assistant renders the template and calls
`esphome.<device_name_with_hyphens_replaced_by_underscores>_duepi_room_receive`
on the same device. That native action accepts one string named `payload`.
No Home Assistant add-on, custom component, script, automation, token, or
second connection is required.

Enter the source's entity ID in `Room temperature sensor`, for example
`sensor.living_room_temperature`. The text field is deliberate: it avoids a
custom entity picker and lets the source be changed without compiling, but it
requires copying the entity ID accurately. Clear the field to stop requests
and restore the legacy relay input path. The inline template reads the source
value and C/F unit, converts the temperature to Celsius, then sends `temp_c`
and the age derived from its report timestamp; polling does not make an old source
reading fresh. Changing the source resets its freshness and PI history; the
firmware waits for a report from the newly selected source before using it.
The firmware still enforces the maximum age. Package
configuration and template tests do not prove that a real device's request
and callback complete end to end; verify that transport during the authorized
field test before enabling compensation.

### Stable temperatures and source loss

An unchanged temperature is still a new measurement when the sensor actually
reports it again. Home Assistant's `last_reported` is the last HA state write;
some integrations, including MQTT, skip that write for identical values.

If the source has a `duepi_sample_timestamp` attribute, Duepi uses that report
date (ISO 8601, including its time zone) instead of `last_reported`. An invalid,
missing-value or future attribute is rejected; a recent HA state write cannot
override an old or invalid explicit date. Sources without that attribute retain
the existing `last_reported` behavior. This is a source contract, not automatic
detection of whether an arbitrary integration still has a working sensor.

The default source maximum remains **15 minutes**, independently of the
**180-second HA relay lease**. A new report at the same temperature gets a new
sample ID. HA's 60-second polling keeps the same sample ID and advances its age;
it neither refreshes the measurement nor advances PI integration.

For a Zigbee2MQTT source, merge the per-device options in
[`room-source-zigbee2mqtt.yaml`](../examples/room-source-zigbee2mqtt.yaml), using
the existing device address/name/topic. This mapping excludes temperature from
the state cache, so humidity/battery messages and startup cache publications
cannot acquire a new temperature timestamp. Z2M can insert `temperature: null`
for a missing value; the mapping ignores that placeholder and retains the old
temperature and timestamp. Keep the device state topic non-retained and clear
any retained state left by an older configuration before commissioning.

Configure a real periodic temperature report comfortably below the 15-minute
limit, for example a maximum interval of **300 seconds**. A sleeping battery
sensor may need waking to accept this setting. Confirm both the reporting
readback and successive actual reports, including equal temperatures. An
acknowledgment alone is insufficient. Do not compensate for an incompatible
sensor by renewing its timestamp on every HA poll or every humidity message.
Leave regulation Local until source freshness is demonstrated.

When the regulation mode is **Local**, a native Lovelace conditional card can
explain why room compensation is paused and how to resume it. Replace the
example entity ID with the one created for the installed ESPHome device:

```yaml
type: conditional
conditions:
  - condition: state
    entity: select.room_regulation_mode
    state: Local
card:
  type: markdown
  content: >-
    Mode manuel — compensation désactivée. Cliquez sur Réactiver la compensation pour reprendre la régulation avec la sonde de la pièce. Ce bouton n'allume pas le poêle.
```

The firmware calculates and exposes absolute temperatures in canonical
Celsius. The inline Home Assistant template accepts Celsius and Fahrenheit
room sensors and converts values to Celsius before sending them through the
native action. Home Assistant can display the climate and absolute-temperature
entities in the user's preferred unit. The optional package accepts `C`,
`°C`, `F`, or `°F` for the configured limits. It converts Fahrenheit lower
limits upward and upper limits downward to the integer-Celsius range the
current UART implementation uses. For example, 50–95 °F becomes 10–35 °C. PI
gains, integral state, and correction remain Celsius differences; the
correction sensor is explicitly marked `Δ°C` so Home Assistant will not apply
an absolute-temperature offset. `Room source age` is the age of HA's latest
report, not proof of when the physical sensor acquired its value.

For a Fahrenheit-configured installation, set the existing substitutions like
this before including the optional package:

```yaml
substitutions:
  min_temp: "50"
  max_temp: "95"
  temp_unit: "°F"
```

Fahrenheit configuration, Fahrenheit room sensors, and Fahrenheit Home
Assistant display do not demonstrate that a stove firmware uses Fahrenheit on
its serial protocol. The current Duepi UART path interprets the observed native
temperature and RF2 target values as Celsius. A screen showing °F cannot
establish a different wire convention; if such firmware is encountered, it
requires capture evidence and an adapter change. This feature does not
auto-detect or send an unverified Fahrenheit protocol.

Automatic control requires fresh raw stove temperature and operating state, a
usable room report, and an idle UART. The firmware uses raw RD1000 temperature
before legacy filtering and raw RD9000 phase; only the stable burning phase
`0x0201` is eligible. Bridge takeover, alarms, and stale observations suspend
automatic output and reset PI state as defined by the protocol. An unavailable
UART suspends writes. A manual native setpoint change or an explicit Off command selects
Local. Received stove status never creates a Heat or Off command.

The known cleaning phases `0x0401` and `0x0200` pause automatic writes and PI
integration while retaining its accumulated integral for less than two minutes.
Changing between these phases does not renew that window. On return to stable
burning, new RD1000 and RC6000 observations are required; the PI time baseline
restarts there, so the cleaning interval is never integrated. A longer cleaning,
unknown phase, actual stop, alarm, or stale/invalid input still resets the PI.
This behavior does not add automatic stove shutdowns or change the PI gains.

The equation without PI is:

```text
error = room_target - room_temperature
native_target = raw_stove_temperature + error
```

The result is rounded once to the nearest integer (positive half values round
up) and clamped to the configured limits after conversion to integer Celsius.
In **Compensation + PI**, the bounded integral correction is added to this
result. Initial tuning
defaults are `Kp = 0.10`, `Ki = 0.025 per hour`, integral limit `0.5 °C`, and
correction limit `1 °C`; the mode selector starts in Local, so PI is opt-in.
Change advanced tuning values in a local copy of
`components/packages/room_regulation.yaml` before using that package.

## Development checks

Create a pinned local ESPHome runtime once:

```sh
ESPHOME_VENV_PATH="${ESPHOME_VENV_PATH:-.venv-esphome}"
python3.13 -m venv "$ESPHOME_VENV_PATH"
"$ESPHOME_VENV_PATH/bin/python" -m pip install "esphome==2026.9.1"
```

Run the native controller suite and focused YAML contracts with the same
runtime. Set ARDUINOJSON_INCLUDE to the ArduinoJson 7.4.3 source directory
available on the development machine:

```sh
ESPHOME_VENV_PATH="${ESPHOME_VENV_PATH:-.venv-esphome}"
ARDUINOJSON_INCLUDE="${ARDUINOJSON_INCLUDE:-/Users/jbjardine/.cache/duepi-room-native/ArduinoJson-7.4.3/src}"
env ESPHOME_PYTHON="$ESPHOME_VENV_PATH/bin/python" \
  ARDUINOJSON_INCLUDE="$ARDUINOJSON_INCLUDE" \
  sh tests/native/run_tests.sh
python3 tests/fixtures/test_room_integration.py
ESPHOME_BIN="$ESPHOME_VENV_PATH/bin/esphome" python3 tests/fixtures/test_room_native_units.py
```

The native payload-template tests use the official Home Assistant Core
2026.10.2 fixtures and render the package's actual Jinja template. To recreate
the sparse fixture checkout and Python 3.14 environment:

```sh
HA_CORE_SRC="${HA_CORE_SRC:-/tmp/duepi-ha-core-2026.10.2}"
HA_TEST_VENV="${HA_TEST_VENV:-.venv-ha}"
git clone --depth 1 --branch 2026.10.2 --filter=blob:none --sparse \
  https://github.com/home-assistant/core.git "$HA_CORE_SRC"
git -C "$HA_CORE_SRC" sparse-checkout set tests
git -C "$HA_CORE_SRC" rev-parse HEAD
python3.14 -m venv "$HA_TEST_VENV"
"$HA_TEST_VENV/bin/python" -m pip install -r requirements-test-ha.txt

PYTHONPATH="$HA_CORE_SRC" PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 \
  "$HA_TEST_VENV/bin/python" -m pytest -q tests/ha/test_native_payload_template.py
```

Compile the four development fixtures with the pinned local runtime:

```sh
ESPHOME_VENV_PATH="${ESPHOME_VENV_PATH:-.venv-esphome}"
"$ESPHOME_VENV_PATH/bin/esphome" compile tests/fixtures/legacy_esp01_1m.yaml
"$ESPHOME_VENV_PATH/bin/esphome" compile tests/fixtures/room_esp01_1m.yaml
"$ESPHOME_VENV_PATH/bin/esphome" compile tests/fixtures/room_esp01_1m_fahrenheit.yaml
"$ESPHOME_VENV_PATH/bin/esphome" compile tests/fixtures/room_lolin_c3_mini.yaml
```

All fixture secrets are intentionally fake. Firmware outputs under
`tests/fixtures/.esphome/` are validation artifacts and **must not be flashed**
to a stove controller. A later authorized field test must compile the chosen
node configuration with the user's existing local secrets; those secrets are
not part of these fixtures or this repository change.

The ESP-01 1 MB OTA layout was measured with ESPHome 2026.9.1 after compiling
the current fixtures. The Arduino linker script sets FS_start to 0x402FB000,
giving a staging end of 1,028,096 bytes. The backend rounds the current image
and incoming compressed image to 4,096-byte sectors; ESPHome compresses OTA
uploads with gzip level 9. The fixture sizes are:

| Image | .bin | gzip OTA | .bin rounded | gzip rounded |
| --- | ---: | ---: | ---: | ---: |
| Legacy ESP-01 | 520,816 | 363,841 | 524,288 | 364,544 |
| Optional C | 560,288 | 389,591 | 561,152 | 393,216 |
| Optional F | 560,304 | 389,603 | 561,152 | 393,216 |

Legacy→optional C or F uses 917,504 bytes and leaves 110,592 bytes. Optional
C/F→optional C/F uses 954,368 bytes and leaves 73,728 bytes. The real ESP-01
build produced a 560,288-byte OTA image and 389,622-byte gzip payload (393,216
bytes rounded). The C3 build produced a 1,039,744-byte OTA image. Current
implementation results are recorded in the
[native validation report](superpowers/validation/2026-10-20-duepi-native.md);
the earlier report remains available as historical validation at
`superpowers/validation/2026-10-20-duepi-room.md`. These margins assume the
currently installed image is the fixture shown; they do not measure an
unknown device image and are not an OTA attempt or OTA success claim.

## Supervised field protocol

Use this protocol only for a separately authorized test on the intended stove.
Before enabling regulation, back up the configuration and verify the isolation
behavior for the exact installed VTherm version. Do not assume that disabling
or unloading it avoids an Off command or other Off side effect; stop if a safe
isolation path cannot be verified. Disable only that stove's VTherm, never
radiator controls. Measure and record the selected room source's reporting
cadence before the test so its freshness can be interpreted. Start in
**Compensation** with PI disabled.

At three observations at least one minute apart, verify the raw local stove
temperature, HA room temperature, computed error and native target, RC6000
readback, and RD4000 actual combustion power. Check that UART polling and any
official-app bridge traffic remain undisturbed. The temperature feedback only
confirms the setpoint path; RD4000 is the independent evidence of actual stove
power response. Respect the five-minute minimum interval between correction
writes in both external modes; do not conclude that the target is failing to
decrease before at least one such interval has elapsed with fresh source
reports. Consider **Compensation + PI** only as a second stage after the offset
path has been reviewed. These checks do not establish thermal accuracy or
comfort performance.
