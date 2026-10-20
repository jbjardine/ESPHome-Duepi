"""Config and options flows for the Duepi room companion."""

from __future__ import annotations

from typing import Any

from homeassistant import config_entries
from homeassistant.core import callback
from homeassistant.helpers import device_registry as dr
from homeassistant.helpers import entity_registry as er
from homeassistant.helpers.selector import DeviceSelector, EntitySelector
import voluptuous as vol

from .const import (
    CONF_DEVICE_ID,
    CONF_RELAY_UNIQUE_ID,
    CONF_SOURCE_ENTITY_ID,
    DOMAIN,
    RELAY_MARKER,
    RELAY_ORIGINAL_NAME,
)


def relay_has_marker(entity: er.RegistryEntry) -> bool:
    """Return whether the registry entry retains the frozen firmware marker."""
    return entity.original_name == RELAY_ORIGINAL_NAME or entity.unique_id.endswith(
        RELAY_MARKER
    )


def find_relay_entities(hass, device_id: str) -> list[er.RegistryEntry]:
    """Find versioned Duepi relay texts registered to the selected ESPHome device."""
    registry = er.async_get(hass)
    return [
        entity
        for entity in er.async_entries_for_device(registry, device_id)
        if entity.platform == "esphome"
        and entity.domain == "text"
        and entity.disabled_by is None
        and relay_has_marker(entity)
    ]


def _is_sensor_entity(entity_id: str) -> bool:
    """Return whether an entity id names the sensor domain."""
    return entity_id.startswith("sensor.")


def _source_error(hass, entity_id: str) -> str | None:
    """Reject unknown entities and units outside the frozen Celsius/Fahrenheit set."""
    if not _is_sensor_entity(entity_id):
        return "invalid_source"
    state = hass.states.get(entity_id)
    if state is None or state.attributes.get("unit_of_measurement") is None:
        return "invalid_source"
    if state.attributes["unit_of_measurement"] not in {"°C", "°F"}:
        return "unsupported_unit"
    return None


class DuepiRoomConfigFlow(config_entries.ConfigFlow, domain=DOMAIN):
    """Configure one relay per ESPHome device and select its room sensor."""

    VERSION = 1

    async def async_step_user(
        self, user_input: dict[str, Any] | None = None
    ) -> config_entries.ConfigFlowResult:
        """Select the ESPHome module and room temperature source."""
        errors: dict[str, str] = {}

        if user_input is not None:
            device_id = user_input[CONF_DEVICE_ID]
            source_entity_id = user_input[CONF_SOURCE_ENTITY_ID]

            source_error = _source_error(self.hass, source_entity_id)
            if source_error is not None:
                errors[CONF_SOURCE_ENTITY_ID] = source_error
            else:
                relays = find_relay_entities(self.hass, device_id)
                if not relays:
                    errors["base"] = "unsupported_firmware"
                elif len(relays) > 1:
                    errors["base"] = "ambiguous_relay"
                else:
                    await self.async_set_unique_id(device_id)
                    self._abort_if_unique_id_configured()
                    device = dr.async_get(self.hass).async_get(device_id)
                    title = (
                        (device.name_by_user or device.name)
                        if device is not None
                        else device_id
                    )
                    return self.async_create_entry(
                        title=title,
                        data={
                            CONF_DEVICE_ID: device_id,
                            CONF_SOURCE_ENTITY_ID: source_entity_id,
                            CONF_RELAY_UNIQUE_ID: relays[0].unique_id,
                        },
                    )

        return self.async_show_form(
            step_id="user",
            data_schema=vol.Schema(
                {
                    vol.Required(CONF_DEVICE_ID): DeviceSelector(
                        {"integration": "esphome"}
                    ),
                    vol.Required(CONF_SOURCE_ENTITY_ID): EntitySelector(
                        {"domain": "sensor"}
                    ),
                }
            ),
            errors=errors,
        )

    @staticmethod
    @callback
    def async_get_options_flow(
        config_entry: config_entries.ConfigEntry,
    ) -> DuepiRoomOptionsFlow:
        """Return the source-replacement options flow."""
        return DuepiRoomOptionsFlow()


class DuepiRoomOptionsFlow(config_entries.OptionsFlowWithReload):
    """Allow changing the selected sensor and reload the source listener."""

    async def async_step_init(
        self, user_input: dict[str, Any] | None = None
    ) -> config_entries.ConfigFlowResult:
        """Select the replacement temperature sensor."""
        current_source = self.config_entry.options.get(
            CONF_SOURCE_ENTITY_ID, self.config_entry.data[CONF_SOURCE_ENTITY_ID]
        )

        if user_input is not None:
            source_entity_id = user_input[CONF_SOURCE_ENTITY_ID]
            source_error = _source_error(self.hass, source_entity_id)
            if source_error is None:
                return self.async_create_entry(
                    title="", data={CONF_SOURCE_ENTITY_ID: source_entity_id}
                )

        return self.async_show_form(
            step_id="init",
            data_schema=vol.Schema(
                {
                    vol.Required(
                        CONF_SOURCE_ENTITY_ID, default=current_source
                    ): EntitySelector({"domain": "sensor"})
                }
            ),
            errors=(
                {CONF_SOURCE_ENTITY_ID: source_error} if user_input is not None else {}
            ),
        )
