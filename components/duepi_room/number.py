import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number
from esphome.const import CONF_ID, UNIT_CELSIUS

from . import DuepiRoomComponent, duepi_room_ns

CONF_DUEPI_ROOM_ID = "duepi_room_id"
DuepiRoomFallbackNumber = duepi_room_ns.class_(
    "DuepiRoomFallbackNumber", number.Number, cg.Component
)

CONFIG_SCHEMA = number.number_schema(
    DuepiRoomFallbackNumber,
    unit_of_measurement=UNIT_CELSIUS,
    device_class="temperature",
).extend(
    {
        cv.Required(CONF_DUEPI_ROOM_ID): cv.use_id(DuepiRoomComponent),
    }
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    parent = await cg.get_variable(config[CONF_DUEPI_ROOM_ID])
    await number.register_number(
        var,
        config,
        min_value=0.0,
        max_value=100.0,
        step=0.5,
    )
    cg.add(var.set_parent(parent))
    cg.add(parent.set_fallback_number(var))
    cg.add(var.traits.set_min_value(parent.native_min()))
    cg.add(var.traits.set_max_value(parent.native_max()))
