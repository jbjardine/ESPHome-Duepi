"""Runtime relay behavior through Home Assistant's config entries and services."""

from __future__ import annotations

import asyncio
from datetime import timedelta
import json

from homeassistant.config_entries import ConfigEntryState
from homeassistant.data_entry_flow import FlowResultType
from homeassistant.const import STATE_UNAVAILABLE
from homeassistant.helpers import device_registry as dr
from homeassistant.helpers import entity_registry as er
from homeassistant.helpers.selector import EntitySelector
from homeassistant.util import dt as dt_util
import pytest

from tests.common import MockConfigEntry, async_fire_time_changed


DOMAIN = "duepi_room"
RELAY_MARKER = "duepi_room_relay_v1"
RELAY_NAME = "Duepi Room Relay v1"
DEVICE_ID = "duepi-test-module"
SENSOR_ID = "sensor.room_temperature"


RELAY_UNIQUE_ID = f"AA11_{RELAY_MARKER}"
REPLACEMENT_SENSOR_ID = "sensor.room_temperature_2"


def add_relay(hass):
    """Register the ESPHome module and stable text relay identity."""
    esp_entry = MockConfigEntry(
        domain="esphome", title="Duepi module", entry_id="esphome_duepi"
    )
    esp_entry.add_to_hass(hass)
    device = dr.async_get(hass).async_get_or_create(
        config_entry_id=esp_entry.entry_id,
        identifiers={("esphome", DEVICE_ID)},
        name="Duepi module",
    )
    relay = er.async_get(hass).async_get_or_create(
        "text",
        "esphome",
        RELAY_UNIQUE_ID,
        config_entry=esp_entry,
        device_id=device.id,
        original_name=RELAY_NAME,
    )
    hass.states.async_set(relay.entity_id, "ready")
    return device, relay


def add_entry(hass, device, relay, source_entity_id=SENSOR_ID):
    """Create one Duepi entry paired to the registered relay unique id."""
    entry = MockConfigEntry(
        domain=DOMAIN,
        title="Duepi module",
        unique_id=device.id,
        data={
            "device_id": device.id,
            "source_entity_id": source_entity_id,
            "relay_unique_id": relay.unique_id,
        },
    )
    entry.add_to_hass(hass)
    return entry


def capture_text_service(hass):
    """Install a test text.set_value service and return its calls."""
    calls = []

    async def set_value(call):
        calls.append(call)

    hass.services.async_register("text", "set_value", set_value)
    return calls


def payloads(calls):
    """Decode the wire payloads from public service calls."""
    return [json.loads(call.data["value"]) for call in calls]


async def setup_runtime(hass, entry):
    """Set up the real integration entry using Home Assistant's manager."""
    assert await hass.config_entries.async_setup(entry.entry_id)
    await hass.async_block_till_done()
    assert entry.state is ConfigEntryState.LOADED


async def test_setup_sends_serialized_snapshot_to_renamed_relay(
    hass, duepi_custom_components
):
    """The stable registry unique id resolves the current relay entity id."""
    device, relay = add_relay(hass)
    registry = er.async_get(hass)
    registry.async_update_entity(
        relay.entity_id, new_entity_id="text.user_renamed_relay"
    )
    hass.states.async_set("text.user_renamed_relay", "ready")
    hass.states.async_set(SENSOR_ID, "68", {"unit_of_measurement": "°F"})
    calls = capture_text_service(hass)
    entry = add_entry(hass, device, relay)

    await setup_runtime(hass, entry)

    assert len(calls) == 1
    assert calls[0].data["entity_id"] == "text.user_renamed_relay"
    packet = payloads(calls)[0]
    assert packet["v"] == 1
    assert len(packet["generation"]) == 32
    assert packet["seq"] == 1
    assert packet["valid"] is True
    assert packet["temp_c"] == pytest.approx(20.0)
    assert packet["age_s"] == 0
    assert packet["sample_id"].isdigit()
    assert len(calls[0].data["value"].encode("utf-8")) <= 254


async def test_missing_relay_state_waits_until_relay_is_ready(
    hass, duepi_custom_components
):
    """The text service is not called until the relay entity has reported ready."""
    device, relay = add_relay(hass)
    hass.states.async_remove(relay.entity_id)
    calls = capture_text_service(hass)
    entry = add_entry(hass, device, relay)

    await setup_runtime(hass, entry)

    assert calls == []
    hass.states.async_set(relay.entity_id, "ready")
    await hass.async_block_till_done()

    assert len(calls) == 1


async def test_missing_source_is_explicitly_invalid_without_sample_id(
    hass, duepi_custom_components
):
    """A missing sensor report cannot reuse or invent a valid sample."""
    device, relay = add_relay(hass)
    calls = capture_text_service(hass)
    entry = add_entry(hass, device, relay)

    await setup_runtime(hass, entry)

    packet = payloads(calls)[0]
    assert packet["valid"] is False
    assert "temp_c" not in packet
    assert packet["sample_id"] == ""


@pytest.mark.parametrize(
    ("value", "attributes"),
    [
        ("unavailable", {"unit_of_measurement": "°C"}),
        ("20.0", {"unit_of_measurement": "K"}),
        ("nan", {"unit_of_measurement": "°C"}),
        ("20.0", {"unit_of_measurement": "°C", "restored": True}),
    ],
)
async def test_invalid_sources_are_sent_explicitly(
    hass, duepi_custom_components, value, attributes
):
    """Unavailable, restored, non-finite and unsupported sources invalidate."""
    device, relay = add_relay(hass)
    hass.states.async_set(SENSOR_ID, value, attributes)
    calls = capture_text_service(hass)
    entry = add_entry(hass, device, relay)

    await setup_runtime(hass, entry)

    packet = payloads(calls)[0]
    assert packet["valid"] is False
    assert "temp_c" not in packet
    assert packet["age_s"] == 0


async def test_heartbeat_preserves_sample_identity_and_reports_age(
    hass, duepi_custom_components, monkeypatch
):
    """Heartbeats age the last report and do not make it look newly sampled."""
    device, relay = add_relay(hass)
    hass.states.async_set(SENSOR_ID, "20.0", {"unit_of_measurement": "°C"})
    calls = capture_text_service(hass)
    entry = add_entry(hass, device, relay)
    reported_at = hass.states.get(SENSOR_ID).last_reported

    await setup_runtime(hass, entry)
    initial = payloads(calls)[0]

    now = reported_at + timedelta(seconds=60)
    monkeypatch.setattr(dt_util, "utcnow", lambda: now)
    async_fire_time_changed(hass, now, fire_all=True)
    await hass.async_block_till_done()

    heartbeat = payloads(calls)[-1]
    assert heartbeat["seq"] == 2
    assert heartbeat["sample_id"] == initial["sample_id"]
    assert heartbeat["age_s"] == 60
    assert reported_at is not None


async def test_old_report_age_is_transmitted_for_firmware_freshness_policy(
    hass, duepi_custom_components, monkeypatch
):
    """HA transmits actual report age without imposing a fixed stale threshold."""
    device, relay = add_relay(hass)
    now = dt_util.utcnow()
    reported_at = now - timedelta(minutes=30, seconds=5)
    monkeypatch.setattr(dt_util, "utcnow", lambda: now)
    hass.states.async_set(
        SENSOR_ID,
        "20.0",
        {"unit_of_measurement": "°C"},
        timestamp=reported_at.timestamp(),
    )
    calls = capture_text_service(hass)
    entry = add_entry(hass, device, relay)

    await setup_runtime(hass, entry)

    packet = payloads(calls)[0]
    assert packet["valid"] is True
    assert packet["temp_c"] == 20.0
    actual_report = hass.states.get(SENSOR_ID).last_reported
    assert actual_report is not None
    expected_age_s = int((now - actual_report).total_seconds())
    assert packet["age_s"] == expected_age_s
    assert packet["age_s"] >= 1805


async def test_unchanged_value_report_gets_a_distinct_sample_id(
    hass, duepi_custom_components, monkeypatch
):
    """HA last_reported distinguishes a new report even when value is stable."""
    device, relay = add_relay(hass)
    hass.states.async_set(SENSOR_ID, "20.0", {"unit_of_measurement": "°C"})
    calls = capture_text_service(hass)
    entry = add_entry(hass, device, relay)

    await setup_runtime(hass, entry)
    initial = payloads(calls)[0]

    base_time = hass.states.get(SENSOR_ID).last_reported
    report_time = base_time + timedelta(seconds=30)
    hass.states.async_set(
        SENSOR_ID,
        "20.0",
        {"unit_of_measurement": "°C"},
        timestamp=report_time.timestamp(),
    )
    heartbeat_time = base_time + timedelta(seconds=60)
    monkeypatch.setattr(dt_util, "utcnow", lambda: heartbeat_time)
    async_fire_time_changed(hass, heartbeat_time, fire_all=True)
    await hass.async_block_till_done()

    latest = payloads(calls)[-1]
    assert latest["sample_id"] != initial["sample_id"]
    assert latest["age_s"] == 30


async def test_reconnect_sends_only_latest_snapshot_and_unload_cleans_listeners(
    hass, duepi_custom_components
):
    """A reconnect publishes current data once; unload stops source/heartbeat sends."""
    device, relay = add_relay(hass)
    hass.states.async_set(SENSOR_ID, "20.0", {"unit_of_measurement": "°C"})
    calls = capture_text_service(hass)
    entry = add_entry(hass, device, relay)

    await setup_runtime(hass, entry)
    relay_entity_id = er.async_get(hass).async_get(relay.id).entity_id
    hass.states.async_set(relay_entity_id, STATE_UNAVAILABLE)
    hass.states.async_set(SENSOR_ID, "21.0", {"unit_of_measurement": "°C"})
    hass.states.async_set(SENSOR_ID, "22.0", {"unit_of_measurement": "°C"})
    async_fire_time_changed(hass, dt_util.utcnow() + timedelta(seconds=60))
    await hass.async_block_till_done()
    assert len(calls) == 1

    hass.states.async_set(relay_entity_id, "ready")
    await hass.async_block_till_done()
    assert len(calls) == 2
    assert payloads(calls)[-1]["temp_c"] == 22.0

    assert await hass.config_entries.async_unload(entry.entry_id)
    hass.states.async_set(SENSOR_ID, "23.0", {"unit_of_measurement": "°C"})
    async_fire_time_changed(hass, dt_util.utcnow() + timedelta(seconds=120))
    await hass.async_block_till_done()
    assert len(calls) == 2


async def test_relay_rename_while_loaded_rebinds_reconnect_listener(
    hass, duepi_custom_components
):
    """The entry follows registry renames and detects the renamed relay reconnect."""
    device, relay = add_relay(hass)
    hass.states.async_set(SENSOR_ID, "20.0", {"unit_of_measurement": "°C"})
    calls = capture_text_service(hass)
    entry = add_entry(hass, device, relay)
    await setup_runtime(hass, entry)

    old_entity_id = relay.entity_id
    new_entity_id = "text.renamed_while_loaded"
    hass.states.async_set(old_entity_id, STATE_UNAVAILABLE)
    er.async_get(hass).async_update_entity(old_entity_id, new_entity_id=new_entity_id)
    hass.states.async_set(new_entity_id, "ready")
    await hass.async_block_till_done()

    assert len(calls) == 2
    assert calls[-1].data["entity_id"] == new_entity_id


async def test_options_replace_source_reload_generation_and_listener(
    hass, duepi_custom_components
):
    """Replacing the source reloads cleanly and removes the old listener."""
    device, relay = add_relay(hass)
    hass.states.async_set(SENSOR_ID, "20.0", {"unit_of_measurement": "°C"})
    hass.states.async_set(REPLACEMENT_SENSOR_ID, "70.0", {"unit_of_measurement": "°F"})
    calls = capture_text_service(hass)
    entry = add_entry(hass, device, relay)

    await setup_runtime(hass, entry)
    first_generation = payloads(calls)[0]["generation"]
    result = await hass.config_entries.options.async_init(entry.entry_id)
    assert result["type"] == FlowResultType.FORM
    selector = next(iter(result["data_schema"].schema.values()))
    assert isinstance(selector, EntitySelector)
    assert selector.config["domain"] == ["sensor"]

    result = await hass.config_entries.options.async_configure(
        result["flow_id"], {"source_entity_id": REPLACEMENT_SENSOR_ID}
    )
    await hass.async_block_till_done()

    assert result["type"] == FlowResultType.CREATE_ENTRY
    assert entry.options["source_entity_id"] == REPLACEMENT_SENSOR_ID
    assert len(calls) == 2
    replacement_packet = payloads(calls)[-1]
    assert replacement_packet["generation"] != first_generation
    assert replacement_packet["seq"] == 1
    assert replacement_packet["temp_c"] == pytest.approx(21.111111)

    hass.states.async_set(SENSOR_ID, "30.0", {"unit_of_measurement": "°C"})
    await hass.async_block_till_done()
    assert len(calls) == 2

    hass.states.async_set(REPLACEMENT_SENSOR_ID, "23.0", {"unit_of_measurement": "°C"})
    await hass.async_block_till_done()
    assert len(calls) == 3
    assert payloads(calls)[-1]["temp_c"] == 23.0


async def test_text_service_calls_are_serialized_and_waiting_send_uses_latest(
    hass, duepi_custom_components
):
    """An in-flight blocking call cannot overlap or queue an old snapshot."""
    device, relay = add_relay(hass)
    hass.states.async_set(SENSOR_ID, "20.0", {"unit_of_measurement": "°C"})
    calls = []
    first_call_started = asyncio.Event()
    release_first_call = asyncio.Event()
    active_calls = 0
    max_active_calls = 0

    async def set_value(call):
        nonlocal active_calls, max_active_calls
        active_calls += 1
        max_active_calls = max(max_active_calls, active_calls)
        calls.append(call)
        try:
            if len(calls) == 1:
                first_call_started.set()
                await release_first_call.wait()
        finally:
            active_calls -= 1

    hass.services.async_register("text", "set_value", set_value)
    entry = add_entry(hass, device, relay)

    setup_task = asyncio.create_task(hass.config_entries.async_setup(entry.entry_id))
    await first_call_started.wait()
    hass.states.async_set(SENSOR_ID, "22.0", {"unit_of_measurement": "°C"})
    await asyncio.sleep(0)
    assert len(calls) == 1

    release_first_call.set()
    assert await setup_task
    await hass.async_block_till_done()

    assert max_active_calls == 1
    assert len(calls) == 2
    assert payloads(calls)[0]["temp_c"] == 20.0
    assert payloads(calls)[1]["temp_c"] == 22.0


async def test_unload_waits_for_current_send_and_prevents_late_callbacks(
    hass, duepi_custom_components
):
    """Unload drains an in-flight call and blocks callbacks already waiting."""
    device, relay = add_relay(hass)
    hass.states.async_set(SENSOR_ID, "20.0", {"unit_of_measurement": "°C"})
    calls = []
    second_call_started = asyncio.Event()
    release_second_call = asyncio.Event()

    async def set_value(call):
        calls.append(call)
        if len(calls) == 2:
            second_call_started.set()
            await release_second_call.wait()

    hass.services.async_register("text", "set_value", set_value)
    entry = add_entry(hass, device, relay)
    await setup_runtime(hass, entry)

    hass.states.async_set(SENSOR_ID, "21.0", {"unit_of_measurement": "°C"})
    await second_call_started.wait()
    unload_task = asyncio.create_task(hass.config_entries.async_unload(entry.entry_id))
    await asyncio.sleep(0)
    assert not unload_task.done()

    hass.states.async_set(SENSOR_ID, "22.0", {"unit_of_measurement": "°C"})
    release_second_call.set()
    assert await unload_task
    await hass.async_block_till_done()

    assert len(calls) == 2
    assert payloads(calls)[-1]["temp_c"] == 21.0
