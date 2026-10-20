import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import select
from esphome.const import CONF_ID

from . import DuepiRoomComponent, duepi_room_ns

CONF_DUEPI_ROOM_ID = "duepi_room_id"
DuepiRoomModeSelect = duepi_room_ns.class_("DuepiRoomModeSelect", select.Select, cg.Component)
MODE_OPTIONS = ["Local", "Compensation", "Compensation + PI"]

CONFIG_SCHEMA = select.select_schema(DuepiRoomModeSelect).extend(
    {
        cv.Required(CONF_DUEPI_ROOM_ID): cv.use_id(DuepiRoomComponent),
    }
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await select.register_select(var, config, options=MODE_OPTIONS)
    parent = await cg.get_variable(config[CONF_DUEPI_ROOM_ID])
    cg.add(var.set_parent(parent))
    cg.add(parent.set_mode_select(var))
