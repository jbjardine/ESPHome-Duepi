# Duepi room relay v1 contract

Frozen before parallel implementation. Firmware and HA consume this same contract.

## Relay discovery and transport

- Firmware text ID: `duepi_room_relay`; fixed firmware name: `Duepi Room Relay v1`.
- Firmware text object identifier must end in `duepi_room_relay_v1`; preserve this marker across node/friendly-name changes. It must remain enabled and writable through the normal ESPHome integration.
- HA discovery: selected ESPHome device's registered text entities; identify the fixed firmware marker using original_name or ESPHome unique_id marker, never the user-edited name/entity_id. Refuse zero or ambiguous matches. Persist the relay unique_id and resolve its current entity_id through the entity registry on subsequent use.
- Use only `text.set_value` with an explicit entity_id, blocking service call, serialized sends, and no second API client.
- Relay accepts at most 254 UTF-8 bytes (the payload is ASCII). Do not publish the JSON back as an HA state; a short stable ready/status value suffices.

## JSON message

Example:

```json
{"v":1,"generation":"0123456789abcdef0123456789abcdef","seq":1,"valid":true,"temp_c":19.4,"age_s":4,"sample_id":"1792485000123456"}
```

- `v`: integer exactly 1.
- `generation`: 32 lowercase hex characters, generated anew at companion setup/reload and source replacement. A changed generation resets source/integral history.
- `seq`: integer 1..4294967295, strictly increasing within a generation. Reject duplicate/out-of-order packets without extending the relay lease.
- `valid`: JSON boolean.
- `temp_c`: finite numeric Celsius measurement; required when valid, omitted when invalid.
- `age_s`: nonnegative integer 0..4294967295, source report age at transmission.
- `sample_id`: decimal string of the HA source's last_reported UTC epoch microseconds (1..20 digits), or empty string for an invalid source with no report. Distinct real HA reports at an unchanged temperature must still produce distinct IDs.
- All fields above except temp_c are required. Unknown fields may be ignored; missing/wrong types, oversized messages, nonfinite values, and unsupported versions invalidate the current source and cannot retain heating ownership silently.
- An invalid source is sent explicitly with valid=false, not retained as its previous good value.

The age means HA report age, not guaranteed physical sample age. Preserve that distinction in documentation/diagnostics. Reject unknown/unavailable, restored state, unsupported units and nonfinite values. Heartbeats re-read the HA state so unchanged-value reports can be detected without a global state_reported listener. They retain the same sample_id until last_reported changes and recompute age_s; they do not reset source age by themselves.

Only the most recent snapshot is transmitted on availability/reconnection; no replay queue of old measurements. Cancel/drain the old source listener and sender before a generation replacement/unload. Firmware requires a new postboot packet before control, regardless of restored mode/target.

## Firmware state surface

Internal mode values: local, compensation, pi. User labels: Local, Compensation, Compensation + PI.

Diagnostic status values: local, waiting_stove, waiting_source, regulating, fallback, bridge, stove_inactive, awaiting_confirmation, command_unconfirmed, uart_busy. Status plus numeric diagnostics must explain why the module is or is not writing. HA companion adds no climate and no stove-control actions.

Automatic writer contract: request an integer native target only after guards pass; route through the shared RF2 path; mark pending at actual UART emission, and confirm only from a fresh RC6000 read. Expose manual-target and native-readback observation hooks separately. Startup, parser synchronization, and own automatic requests must not be interpreted as manual commands.
