"""Duepi Room Companion integration."""

from __future__ import annotations

from homeassistant.config_entries import ConfigEntry, ConfigEntryNotReady
from homeassistant.core import HomeAssistant

from .const import DOMAIN
from .relay import RelayRuntime


async def async_setup(hass: HomeAssistant, config: dict) -> bool:
    """Set up the integration."""
    return True


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    """Start the selected source listener and relay heartbeat for one module."""
    runtime = RelayRuntime(hass, entry)
    try:
        await runtime.async_start()
    except ValueError as err:
        raise ConfigEntryNotReady(
            "The configured Duepi relay is not registered"
        ) from err
    hass.data.setdefault(DOMAIN, {})[entry.entry_id] = runtime
    return True


async def async_unload_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    """Stop listeners and drain the current send before unloading."""
    runtime: RelayRuntime | None = hass.data.get(DOMAIN, {}).pop(entry.entry_id, None)
    if runtime is not None:
        await runtime.async_stop()
    return True
