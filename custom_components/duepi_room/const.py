"""Constants for the Duepi room companion."""

from datetime import timedelta

DOMAIN = "duepi_room"

CONF_DEVICE_ID = "device_id"
CONF_SOURCE_ENTITY_ID = "source_entity_id"
CONF_RELAY_UNIQUE_ID = "relay_unique_id"

RELAY_MARKER = "duepi_room_relay_v1"
RELAY_ORIGINAL_NAME = "Duepi Room Relay v1"
RELAY_MAX_BYTES = 254
HEARTBEAT_INTERVAL = timedelta(seconds=60)
