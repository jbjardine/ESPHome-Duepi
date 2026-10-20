"""Build and send snapshots over the standard Home Assistant text service."""

from __future__ import annotations

import asyncio
from datetime import datetime, timezone
import json
import logging
from math import isfinite
from typing import Any
from uuid import uuid4

from homeassistant.config_entries import ConfigEntry
from homeassistant.const import STATE_UNAVAILABLE, STATE_UNKNOWN, UnitOfTemperature
from homeassistant.core import Event, HomeAssistant, State
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers import entity_registry as er
from homeassistant.helpers.event import (
    async_track_state_change_event,
    async_track_time_interval,
)
from homeassistant.util import dt as dt_util
from homeassistant.util.unit_conversion import TemperatureConverter

from .const import (
    CONF_RELAY_UNIQUE_ID,
    CONF_SOURCE_ENTITY_ID,
    HEARTBEAT_INTERVAL,
    RELAY_MARKER,
    RELAY_MAX_BYTES,
    RELAY_ORIGINAL_NAME,
)

_LOGGER = logging.getLogger(__name__)
_MAX_COUNTER = 0xFFFFFFFF
_EPOCH = datetime(1970, 1, 1, tzinfo=timezone.utc)
_INVALID_STATES = {STATE_UNKNOWN, STATE_UNAVAILABLE}


def _sample_id(last_reported: datetime | None) -> str:
    """Format a HA report timestamp as exact decimal epoch microseconds."""
    if last_reported is None:
        return ""
    delta = dt_util.as_utc(last_reported) - _EPOCH
    epoch_us = (
        delta.days * 86_400_000_000 + delta.seconds * 1_000_000 + delta.microseconds
    )
    return str(epoch_us) if 1 <= epoch_us <= 99_999_999_999_999_999_999 else ""


def _age_seconds(last_reported: datetime | None, now: datetime) -> int:
    """Return nonnegative integer HA report age, saturated to the wire range."""
    if last_reported is None:
        return 0
    age = int((dt_util.as_utc(now) - dt_util.as_utc(last_reported)).total_seconds())
    return min(_MAX_COUNTER, max(0, age))


def _temperature_celsius(state: State | None) -> float | None:
    """Validate a sensor state and convert supported units to Celsius."""
    if (
        state is None
        or state.state in _INVALID_STATES
        or state.attributes.get("restored") is True
    ):
        return None

    unit = state.attributes.get("unit_of_measurement")
    if unit not in {UnitOfTemperature.CELSIUS, UnitOfTemperature.FAHRENHEIT}:
        return None
    try:
        value = float(state.state)
        value_c = TemperatureConverter.convert(value, unit, UnitOfTemperature.CELSIUS)
    except (TypeError, ValueError, HomeAssistantError):
        return None
    return value_c if isfinite(value_c) else None


def build_snapshot(
    state: State | None,
    *,
    generation: str,
    seq: int,
    now: datetime,
) -> dict[str, Any]:
    """Create the protocol-v1 state snapshot, excluding invalid temperatures."""
    last_reported = state.last_reported if state is not None else None
    temperature = _temperature_celsius(state)
    packet: dict[str, Any] = {
        "v": 1,
        "generation": generation,
        "seq": seq,
        "valid": temperature is not None,
        "age_s": _age_seconds(last_reported, now),
        "sample_id": _sample_id(last_reported),
    }
    if temperature is not None:
        packet["temp_c"] = temperature
    return packet


def serialize_snapshot(packet: dict[str, Any]) -> str:
    """Serialize compact ASCII JSON and enforce the frozen relay byte limit."""
    message = json.dumps(packet, separators=(",", ":"), allow_nan=False)
    if len(message.encode("utf-8")) > RELAY_MAX_BYTES:
        raise ValueError("Duepi relay message exceeds the 254-byte contract")
    return message


def _relay_is_valid(entry: er.RegistryEntry, config_entry: ConfigEntry) -> bool:
    """Confirm persisted relay identity still belongs to this ESPHome device."""
    return (
        entry.domain == "text"
        and entry.platform == "esphome"
        and entry.device_id == config_entry.data.get("device_id")
        and entry.disabled_by is None
        and (
            entry.original_name == RELAY_ORIGINAL_NAME
            or entry.unique_id.endswith(RELAY_MARKER)
        )
    )


class RelayRuntime:
    """Own the source listener and serialized sender for one HA config entry."""

    def __init__(self, hass: HomeAssistant, config_entry: ConfigEntry) -> None:
        self.hass = hass
        self.config_entry = config_entry
        self.source_entity_id = config_entry.options.get(
            CONF_SOURCE_ENTITY_ID, config_entry.data[CONF_SOURCE_ENTITY_ID]
        )
        self.generation = uuid4().hex
        self.seq = 0
        self._remove_source_listener = None
        self._remove_relay_listener = None
        self._remove_registry_listener = None
        self._remove_heartbeat = None
        self._send_lock = asyncio.Lock()
        self._relay_entity_id: str | None = None
        self._relay_online = False
        self._stopping = False

    @property
    def relay_entry(self) -> er.RegistryEntry | None:
        """Resolve the persisted unique id against the live entity registry."""
        registry = er.async_get(self.hass)
        entity_id = registry.async_get_entity_id(
            "text", "esphome", self.config_entry.data[CONF_RELAY_UNIQUE_ID]
        )
        if entity_id is None:
            return None
        entity = registry.async_get(entity_id)
        if entity is None or not _relay_is_valid(entity, self.config_entry):
            return None
        return entity

    async def async_start(self) -> None:
        """Register focused listeners and publish the current source snapshot."""
        relay = self.relay_entry
        if relay is None:
            raise ValueError("The configured Duepi Room Relay v1 entity is missing")
        self._relay_entity_id = relay.entity_id
        relay_state = self.hass.states.get(relay.entity_id)
        self._relay_online = (
            relay_state is not None and relay_state.state not in _INVALID_STATES
        )
        self._remove_source_listener = async_track_state_change_event(
            self.hass, self.source_entity_id, self._async_source_changed
        )
        self._remove_relay_listener = async_track_state_change_event(
            self.hass, self._relay_entity_id, self._async_relay_changed
        )
        self._remove_registry_listener = self.hass.bus.async_listen(
            er.EVENT_ENTITY_REGISTRY_UPDATED, self._async_registry_updated
        )
        self._remove_heartbeat = async_track_time_interval(
            self.hass, self._async_heartbeat, HEARTBEAT_INTERVAL
        )
        await self._async_send_latest()

    async def async_stop(self) -> None:
        """Remove all listeners and wait for an in-flight text service call."""
        self._stopping = True
        for remove in (
            self._remove_source_listener,
            self._remove_relay_listener,
            self._remove_registry_listener,
            self._remove_heartbeat,
        ):
            if remove is not None:
                remove()
        self._remove_source_listener = None
        self._remove_relay_listener = None
        self._remove_registry_listener = None
        self._remove_heartbeat = None
        async with self._send_lock:
            pass

    async def _async_source_changed(self, event: Event) -> None:
        """Send promptly when HA reports a changed source state."""
        await self._async_send_latest()

    async def _async_heartbeat(self, now: datetime) -> None:
        """Re-read last_reported so unchanged values retain distinct identities."""
        await self._async_send_latest(now=now)

    async def _async_relay_changed(self, event: Event) -> None:
        """Send only the newest snapshot when the relay returns online."""
        new_state = event.data.get("new_state")
        if new_state is None or new_state.state in _INVALID_STATES:
            self._relay_online = False
            return
        if not self._relay_online:
            self._relay_online = True
            await self._async_send_latest()

    async def _async_registry_updated(self, event: Event) -> None:
        """Follow a user rename without losing the relay reconnect listener."""
        if self._relay_entity_id is None:
            return
        if (
            event.data.get("entity_id") != self._relay_entity_id
            and event.data.get("old_entity_id") != self._relay_entity_id
        ):
            return
        was_online = self._relay_online
        relay = self.relay_entry
        new_entity_id = relay.entity_id if relay is not None else None
        if new_entity_id == self._relay_entity_id:
            return
        if self._remove_relay_listener is not None:
            self._remove_relay_listener()
            self._remove_relay_listener = None
        self._relay_entity_id = new_entity_id
        if new_entity_id is None:
            self._relay_online = False
            return
        self._remove_relay_listener = async_track_state_change_event(
            self.hass, new_entity_id, self._async_relay_changed
        )
        relay_state = self.hass.states.get(new_entity_id)
        self._relay_online = (
            relay_state is not None and relay_state.state not in _INVALID_STATES
        )
        if self._relay_online and not was_online:
            await self._async_send_latest()

    async def _async_send_latest(self, *, now: datetime | None = None) -> None:
        """Serialize service calls, resolving current registry identity each time."""
        async with self._send_lock:
            if self._stopping:
                return
            relay = self.relay_entry
            if relay is None:
                return
            relay_state = self.hass.states.get(relay.entity_id)
            if relay_state is None or relay_state.state in _INVALID_STATES:
                return
            if self.seq >= _MAX_COUNTER:
                self.generation = uuid4().hex
                self.seq = 0
            next_seq = self.seq + 1
            packet = build_snapshot(
                self.hass.states.get(self.source_entity_id),
                generation=self.generation,
                seq=next_seq,
                now=now or dt_util.utcnow(),
            )
            try:
                message = serialize_snapshot(packet)
            except (TypeError, ValueError):
                _LOGGER.exception("Could not serialize Duepi relay snapshot")
                return
            try:
                await self.hass.services.async_call(
                    "text",
                    "set_value",
                    {"entity_id": relay.entity_id, "value": message},
                    blocking=True,
                )
            except HomeAssistantError:
                _LOGGER.debug(
                    "Duepi relay text service is not available", exc_info=True
                )
                return
            self.seq = next_seq
