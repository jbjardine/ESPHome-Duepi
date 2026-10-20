"""Exercise the package's native HA action template with the official HA renderer."""

from __future__ import annotations

from datetime import timedelta
import json
from pathlib import Path

import yaml
import pytest
from homeassistant.helpers.template import Template
from homeassistant.util import dt as dt_util


ROOT = Path(__file__).resolve().parents[2]


class PackageLoader(yaml.SafeLoader):
    """Load ESPHome's package YAML while treating ESPHome tags as scalar data."""


def _unknown_tag(loader, _tag_suffix, node):
    if isinstance(node, yaml.ScalarNode):
        return loader.construct_scalar(node)
    if isinstance(node, yaml.SequenceNode):
        return loader.construct_sequence(node)
    return loader.construct_mapping(node)


PackageLoader.add_multi_constructor("!", _unknown_tag)


def _native_receive_action():
    package = yaml.load(
        (ROOT / "components/packages/room_regulation.yaml").read_text(),
        Loader=PackageLoader,
    )
    triggers = package["duepi_room"].get("on_source_request", [])
    if isinstance(triggers, dict):
        triggers = [triggers]
    for item in triggers:
        for action in item.get("then", []):
            if "homeassistant.action" in action:
                return action["homeassistant.action"]
    raise AssertionError("room package has no native homeassistant.action")


async def test_native_action_payload_is_renderable_by_home_assistant(hass):
    """The actual package payload, not a copied test template, uses HA Jinja."""
    action = _native_receive_action()
    assert "data_template" in action
    assert "payload" in action["data_template"]


async def _render_payload(hass, source, generation, seq):
    template = Template(_native_receive_action()["data_template"]["payload"], hass)
    rendered = template.async_render(
        {"source": source, "generation": generation, "seq": seq}
    )
    return rendered, json.loads(rendered)


@pytest.mark.parametrize(
    ("unit", "raw_value", "expected_c"),
    [("°C", "20.5", 20.5), ("°F", "68.9", 20.5)],
)
async def test_native_template_serializes_celsius_and_fahrenheit_as_v1_json(
    hass, monkeypatch, unit, raw_value, expected_c
):
    now = dt_util.utcnow()
    reported_at = now - timedelta(seconds=75)
    monkeypatch.setattr(dt_util, "utcnow", lambda: now)
    hass.states.async_set(
        "sensor.room_temperature",
        raw_value,
        {"unit_of_measurement": unit},
        timestamp=reported_at.timestamp(),
    )

    rendered, packet = await _render_payload(
        hass, "sensor.room_temperature", "0123456789abcdef0123456789abcdef", 7
    )

    assert packet == {
        "v": 1,
        "generation": "0123456789abcdef0123456789abcdef",
        "seq": 7,
        "valid": True,
        "temp_c": pytest.approx(expected_c),
        "age_s": 75,
        "sample_id": packet["sample_id"],
    }
    assert isinstance(packet["v"], int)
    assert isinstance(packet["seq"], int)
    assert isinstance(packet["valid"], bool)
    assert isinstance(packet["age_s"], int)
    assert isinstance(packet["temp_c"], (int, float))
    assert isinstance(packet["sample_id"], str) and packet["sample_id"].isdigit()
    assert len(rendered.encode("ascii")) < 255


@pytest.mark.parametrize(
    ("source", "state", "attributes", "reported_offset"),
    [
        ("sensor.room_temperature", "unknown", {"unit_of_measurement": "°C"}, 0),
        ("sensor.room_temperature", "20.0", {"unit_of_measurement": "K"}, 0),
        ("sensor.room_temperature", "1e308", {"unit_of_measurement": "°F"}, 0),
        (
            "sensor.room_temperature",
            "20.0",
            {"unit_of_measurement": "°C", "restored": True},
            0,
        ),
        ("sensor.room_temperature", "nan", {"unit_of_measurement": "°C"}, 0),
        ("sensor.room_temperature", "20.0", {"unit_of_measurement": "°C"}, 30),
    ],
)
async def test_invalid_and_future_source_reports_stay_invalid(
    hass, monkeypatch, source, state, attributes, reported_offset
):
    now = dt_util.utcnow()
    reported_at = now + timedelta(seconds=reported_offset)
    monkeypatch.setattr(dt_util, "utcnow", lambda: now)
    hass.states.async_set(source, state, attributes, timestamp=reported_at.timestamp())

    _rendered, packet = await _render_payload(
        hass, source, "fedcba9876543210fedcba9876543210", 1
    )

    assert packet["valid"] is False
    assert "temp_c" not in packet
    assert packet["age_s"] == 0
    assert packet["sample_id"].isdigit()


async def test_missing_source_is_explicitly_invalid_without_sample_id(hass):
    _rendered, packet = await _render_payload(
        hass, "sensor.missing_room_sensor", "fedcba9876543210fedcba9876543210", 1
    )
    assert packet["valid"] is False
    assert "temp_c" not in packet
    assert packet["sample_id"] == ""


async def test_old_report_keeps_its_age_and_same_value_new_report_changes_sample_id(
    hass, monkeypatch
):
    now = dt_util.utcnow()
    old_report = now - timedelta(hours=2)
    monkeypatch.setattr(dt_util, "utcnow", lambda: now)
    hass.states.async_set(
        "sensor.room_temperature",
        "20.0",
        {"unit_of_measurement": "°C"},
        timestamp=old_report.timestamp(),
    )
    _old_rendered, old_packet = await _render_payload(
        hass, "sensor.room_temperature", "0123456789abcdef0123456789abcdef", 1
    )
    _same_rendered, same_packet = await _render_payload(
        hass, "sensor.room_temperature", "0123456789abcdef0123456789abcdef", 2
    )
    assert old_packet["valid"] is True
    assert old_packet["age_s"] == 7200
    assert same_packet["sample_id"] == old_packet["sample_id"]

    new_report = old_report + timedelta(seconds=30)
    hass.states.async_set(
        "sensor.room_temperature",
        "20.0",
        {"unit_of_measurement": "°C"},
        timestamp=new_report.timestamp(),
    )
    _new_rendered, new_packet = await _render_payload(
        hass, "sensor.room_temperature", "0123456789abcdef0123456789abcdef", 3
    )
    assert new_packet["sample_id"] != old_packet["sample_id"]
    assert new_packet["age_s"] == 7170


async def test_native_action_uses_dynamic_node_service_and_string_callback_contract(hass):
    action = _native_receive_action()
    package_text = (ROOT / "components/packages/room_regulation.yaml").read_text()
    api_action = next(
        item
        for item in yaml.load(package_text, Loader=PackageLoader)["api"]["actions"]
        if item["action"] == "duepi_room_receive"
    )

    assert "App.get_name()" in action["action"]
    assert '"esphome." + node_name + "_duepi_room_receive"' in action["action"]
    assert api_action["variables"] == {"payload": "string"}
    assert "receive_native_source_payload(payload)" in api_action["then"][0]["lambda"]
    assert "homeassistant_services: true" in package_text
