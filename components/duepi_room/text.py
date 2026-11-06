import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text
from esphome.const import CONF_ID, CONF_MODE, CONF_NAME

from . import DuepiRoomComponent, duepi_room_ns

CONF_DUEPI_ROOM_ID = "duepi_room_id"
DUEPI_ROOM_RELAY_NAME = "Duepi Room Relay v1"
DUEPI_ROOM_RELAY_ID = "duepi_room_relay"
DuepiRoomRelayText = duepi_room_ns.class_("DuepiRoomRelayText", text.Text, cg.Component)


def validate_marker(config):
    if config[CONF_NAME] != DUEPI_ROOM_RELAY_NAME:
        raise cv.Invalid(f"relay name is fixed to '{DUEPI_ROOM_RELAY_NAME}'")
    if config[CONF_ID].id != DUEPI_ROOM_RELAY_ID:
        raise cv.Invalid(f"relay id is fixed to '{DUEPI_ROOM_RELAY_ID}'")
    return config


CONFIG_SCHEMA = cv.All(
    text.text_schema(DuepiRoomRelayText, mode="TEXT").extend(
        {
            cv.Required(CONF_DUEPI_ROOM_ID): cv.use_id(DuepiRoomComponent),
            cv.Optional(CONF_ID, default=DUEPI_ROOM_RELAY_ID): cv.declare_id(DuepiRoomRelayText),
            cv.Optional(CONF_NAME, default=DUEPI_ROOM_RELAY_NAME): cv.one_of(DUEPI_ROOM_RELAY_NAME),
        }
    ),
    validate_marker,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await text.register_text(var, config, min_length=0, max_length=254)
    parent = await cg.get_variable(config[CONF_DUEPI_ROOM_ID])
    cg.add(var.set_parent(parent))
    cg.add(parent.set_relay_text(var))
