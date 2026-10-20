"""Config-flow behavior for the Duepi room companion."""

from __future__ import annotations

from homeassistant.config_entries import SOURCE_USER
from homeassistant.helpers import device_registry as dr
from homeassistant.helpers import entity_registry as er
from homeassistant.helpers.selector import DeviceSelector, EntitySelector
import pytest
import voluptuous as vol

from tests.common import MockConfigEntry


DOMAIN = "duepi_room"
RELAY_MARKER = "duepi_room_relay_v1"
RELAY_NAME = "Duepi Room Relay v1"
DEVICE_ID = "duepi-test-module"
SENSOR_ID = "sensor.room_temperature"


def add_esphome_relay(
    hass,
    *,
    unique_id: str = f"AA11_{RELAY_MARKER}",
    original_name: str | None = RELAY_NAME,
    device_id: str = DEVICE_ID,
):
    """Register a selected ESPHome device and its relay text entity."""
    esp_entry = MockConfigEntry(
        domain="esphome",
        title="Duepi module",
        entry_id=f"esphome_{device_id}",
    )
    esp_entry.add_to_hass(hass)
    device = dr.async_get(hass).async_get_or_create(
        config_entry_id=esp_entry.entry_id,
        identifiers={("esphome", device_id)},
        name="Duepi module",
    )
    relay = er.async_get(hass).async_get_or_create(
        "text",
        "esphome",
        unique_id,
        config_entry=esp_entry,
        device_id=device.id,
        original_name=original_name,
    )
    return device, relay


async def start_config_flow(hass, device_id: str, sensor_id: str = SENSOR_ID):
    """Submit the initial selector values."""
    return await hass.config_entries.flow.async_init(
        DOMAIN,
        context={"source": SOURCE_USER},
        data={"device_id": device_id, "source_entity_id": sensor_id},
    )


@pytest.mark.parametrize("selector_key", ["device_id", "source_entity_id"])
async def test_config_flow_shows_esphome_device_and_temperature_sensor_selectors(
    hass, duepi_custom_components, selector_key: str
) -> None:
    """The public flow asks for one module and one sensor."""
    result = await hass.config_entries.flow.async_init(
        DOMAIN, context={"source": SOURCE_USER}
    )

    assert result["type"] == "form"
    schema = result["data_schema"].schema
    selectors = {
        key.schema: value
        for key, value in schema.items()
        if isinstance(key, vol.Marker)
    }

    if selector_key == "device_id":
        assert isinstance(selectors["device_id"], DeviceSelector)
        assert selectors["device_id"].config["integration"] == "esphome"
    else:
        assert isinstance(selectors["source_entity_id"], EntitySelector)
        assert selectors["source_entity_id"].config["domain"] == ["sensor"]


async def test_config_flow_rejects_esphome_device_without_fixed_relay_marker(
    hass, duepi_custom_components
) -> None:
    """Modules without the versioned text marker cannot be paired."""
    device, _ = add_esphome_relay(
        hass,
        unique_id="AA11_another_text",
        original_name="Some other text",
    )
    hass.states.async_set(SENSOR_ID, "20.0", {"unit_of_measurement": "°C"})

    result = await start_config_flow(hass, device.id)

    assert result["type"] == "form"
    assert result["errors"] == {"base": "unsupported_firmware"}


async def test_config_flow_rejects_sensor_with_unsupported_temperature_unit(
    hass, duepi_custom_components
) -> None:
    """A sensor with a non-Celsius/Fahrenheit unit cannot be paired."""
    device, _ = add_esphome_relay(hass)
    hass.states.async_set(SENSOR_ID, "293.15", {"unit_of_measurement": "K"})

    result = await start_config_flow(hass, device.id)

    assert result["type"] == "form"
    assert result["errors"] == {"source_entity_id": "unsupported_unit"}


async def test_config_flow_accepts_fixed_original_name_when_unique_id_is_opaque(
    hass, duepi_custom_components
) -> None:
    """The original firmware name remains a stable discovery marker."""
    device, relay = add_esphome_relay(
        hass, unique_id="AA11_opaque", original_name=RELAY_NAME
    )
    hass.states.async_set(SENSOR_ID, "20.0", {"unit_of_measurement": "°C"})

    result = await start_config_flow(hass, device.id)

    assert result["type"] == "create_entry"
    entry = hass.config_entries.async_entry_for_domain_unique_id(DOMAIN, device.id)
    assert entry is not None
    assert entry.data["relay_unique_id"] == relay.unique_id


async def test_config_flow_rejects_ambiguous_fixed_relay_markers(
    hass, duepi_custom_components
) -> None:
    """Two fixed relay markers on one ESPHome device are not guessed between."""
    device, _ = add_esphome_relay(hass)
    esp_entry = hass.config_entries.async_get_entry(f"esphome_{DEVICE_ID}")
    er.async_get(hass).async_get_or_create(
        "text",
        "esphome",
        f"BB22_{RELAY_MARKER}",
        config_entry=esp_entry,
        device_id=device.id,
        original_name=RELAY_NAME,
    )
    hass.states.async_set(SENSOR_ID, "20.0", {"unit_of_measurement": "°C"})

    result = await start_config_flow(hass, device.id)

    assert result["type"] == "form"
    assert result["errors"] == {"base": "ambiguous_relay"}


async def test_config_flow_persists_relay_unique_id_and_prevents_duplicate_module(
    hass, duepi_custom_components
) -> None:
    """A device has one entry whose relay identity survives entity renames."""
    device, relay = add_esphome_relay(hass, original_name="Renamed by user")
    hass.states.async_set(SENSOR_ID, "20.0", {"unit_of_measurement": "°C"})

    result = await start_config_flow(hass, device.id)

    assert result["type"] == "create_entry"
    entry = hass.config_entries.async_entry_for_domain_unique_id(DOMAIN, device.id)
    assert entry is not None
    assert entry.unique_id == device.id
    assert entry.data["device_id"] == device.id
    assert entry.data["relay_unique_id"] == relay.unique_id

    duplicate = await start_config_flow(hass, device.id)

    assert duplicate["type"] == "abort"
    assert duplicate["reason"] == "already_configured"
