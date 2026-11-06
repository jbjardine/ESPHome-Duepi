import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate
from esphome.const import CONF_ID

from . import DuepiRoomComponent, duepi_room_ns

CONF_DUEPI_ROOM_ID = "duepi_room_id"
DuepiRoomClimate = duepi_room_ns.class_("DuepiRoomClimate", climate.Climate, cg.Component)

CONFIG_SCHEMA = climate.climate_schema(DuepiRoomClimate).extend(
    {
        cv.Required(CONF_DUEPI_ROOM_ID): cv.use_id(DuepiRoomComponent),
    }
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await climate.register_climate(var, config)
    parent = await cg.get_variable(config[CONF_DUEPI_ROOM_ID])
    cg.add(var.set_parent(parent))
    cg.add(parent.set_room_climate(var))
