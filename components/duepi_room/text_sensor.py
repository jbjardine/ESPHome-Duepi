import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor
from esphome.const import CONF_ID, ENTITY_CATEGORY_DIAGNOSTIC

from . import DuepiRoomComponent, duepi_room_ns

CONF_DUEPI_ROOM_ID = "duepi_room_id"
DuepiRoomStatusSensor = duepi_room_ns.class_("DuepiRoomStatusSensor", text_sensor.TextSensor)

CONFIG_SCHEMA = text_sensor.text_sensor_schema(
    DuepiRoomStatusSensor,
    entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
).extend(
    {
        cv.Required(CONF_DUEPI_ROOM_ID): cv.use_id(DuepiRoomComponent),
    }
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await text_sensor.register_text_sensor(var, config)
    parent = await cg.get_variable(config[CONF_DUEPI_ROOM_ID])
    cg.add(var.set_parent(parent))
    cg.add(parent.set_status_sensor(var))
