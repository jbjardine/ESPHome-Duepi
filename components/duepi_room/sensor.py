import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    CONF_DEVICE_CLASS,
    CONF_ID,
    CONF_UNIT_OF_MEASUREMENT,
    DEVICE_CLASS_DURATION,
    DEVICE_CLASS_TEMPERATURE,
    UNIT_CELSIUS,
    UNIT_SECOND,
)

from . import DuepiRoomComponent, duepi_room_ns

CONF_DUEPI_ROOM_ID = "duepi_room_id"
CONF_METRIC = "metric"
DuepiRoomDiagnosticSensor = duepi_room_ns.class_("DuepiRoomDiagnosticSensor", sensor.Sensor)
DiagnosticMetric = duepi_room_ns.enum("DiagnosticMetric")
METRICS = {
    "source_age": DiagnosticMetric.SOURCE_AGE,
    "pi_correction": DiagnosticMetric.PI_CORRECTION,
    "computed_target": DiagnosticMetric.COMPUTED_TARGET,
}


def validate_metric(config):
    metric = config[CONF_METRIC]
    if metric == "source_age":
        config[CONF_UNIT_OF_MEASUREMENT] = UNIT_SECOND
        config[CONF_DEVICE_CLASS] = DEVICE_CLASS_DURATION
        config["accuracy_decimals"] = 0
    elif metric == "pi_correction":
        config[CONF_UNIT_OF_MEASUREMENT] = "Δ°C"
        config.pop(CONF_DEVICE_CLASS, None)
        config["accuracy_decimals"] = 2
    else:
        config[CONF_UNIT_OF_MEASUREMENT] = UNIT_CELSIUS
        config[CONF_DEVICE_CLASS] = DEVICE_CLASS_TEMPERATURE
        config["accuracy_decimals"] = 0
    return config


CONFIG_SCHEMA = cv.All(
    sensor.sensor_schema(DuepiRoomDiagnosticSensor).extend(
        {
            cv.Required(CONF_DUEPI_ROOM_ID): cv.use_id(DuepiRoomComponent),
            cv.Required(CONF_METRIC): cv.enum(METRICS, lower=True),
        }
    ),
    validate_metric,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await sensor.register_sensor(var, config)
    cg.add(var.set_state_class(sensor.STATE_CLASSES["measurement"]))
    parent = await cg.get_variable(config[CONF_DUEPI_ROOM_ID])
    cg.add(var.set_parent(parent))
    cg.add(var.set_metric(config[CONF_METRIC]))
    cg.add(parent.add_diagnostic_sensor(var))
