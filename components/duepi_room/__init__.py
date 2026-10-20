from esphome import automation
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_TRIGGER_ID

AUTO_LOAD = ["climate", "json", "number", "select", "sensor", "text", "text_sensor"]

duepi_room_ns = cg.esphome_ns.namespace("duepi_room")
DuepiRoomComponent = duepi_room_ns.class_("DuepiRoomComponent", cg.PollingComponent)
RF2TargetTrigger = duepi_room_ns.class_("RF2TargetTrigger", automation.Trigger.template(cg.int_))
NativeModeCommandTrigger = duepi_room_ns.class_(
    "NativeModeCommandTrigger", automation.Trigger.template(cg.bool_)
)

CONF_NATIVE_MIN = "native_min"
CONF_NATIVE_MAX = "native_max"
CONF_FALLBACK_TARGET = "fallback_target"
CONF_MAX_SOURCE_AGE = "max_source_age"
CONF_PI = "pi"
CONF_KP = "kp"
CONF_KI_PER_HOUR = "ki_per_hour"
CONF_INTEGRAL_LIMIT = "integral_limit"
CONF_CORRECTION_LIMIT = "correction_limit"
CONF_ON_RF2_TARGET = "on_rf2_target"
CONF_ON_NATIVE_MODE_COMMAND = "on_native_mode_command"

PI_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_KP, default=0.10): cv.float_range(min=0.0),
        cv.Optional(CONF_KI_PER_HOUR, default=0.025): cv.float_range(min=0.0),
        cv.Optional(CONF_INTEGRAL_LIMIT, default=0.5): cv.float_range(min=0.0),
        cv.Optional(CONF_CORRECTION_LIMIT, default=1.0): cv.float_range(min=0.0),
    }
)


def validate_config(config):
    if config[CONF_NATIVE_MIN] >= config[CONF_NATIVE_MAX]:
        raise cv.Invalid("native_min must be less than native_max")
    fallback = config[CONF_FALLBACK_TARGET]
    if not config[CONF_NATIVE_MIN] <= fallback <= config[CONF_NATIVE_MAX]:
        raise cv.Invalid("fallback_target must be within native_min and native_max")
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(DuepiRoomComponent),
            cv.Required(CONF_NATIVE_MIN): cv.int_range(min=1, max=99),
            cv.Required(CONF_NATIVE_MAX): cv.int_range(min=2, max=100),
            cv.Optional(CONF_FALLBACK_TARGET, default=20.0): cv.float_,
            cv.Optional(CONF_MAX_SOURCE_AGE, default="15min"): cv.positive_time_period_seconds,
            cv.Optional(CONF_PI, default={}): PI_SCHEMA,
            cv.Optional(CONF_ON_RF2_TARGET): automation.validate_automation(
                {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(RF2TargetTrigger)}
            ),
            cv.Optional(CONF_ON_NATIVE_MODE_COMMAND): automation.validate_automation(
                {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(NativeModeCommandTrigger)}
            ),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(cv.polling_component_schema("1s")),
    validate_config,
)


async def to_code(config):
    cg.add_define("USE_DUEPI_ROOM")
    pi = config[CONF_PI]
    # TimePeriodSeconds exposes total_seconds as an integer property.
    max_source_age_seconds = config[CONF_MAX_SOURCE_AGE].total_seconds
    var = cg.new_Pvariable(
        config[CONF_ID],
        config[CONF_NATIVE_MIN],
        config[CONF_NATIVE_MAX],
        config[CONF_FALLBACK_TARGET],
        max_source_age_seconds,
        pi[CONF_KP],
        pi[CONF_KI_PER_HOUR],
        pi[CONF_INTEGRAL_LIMIT],
        pi[CONF_CORRECTION_LIMIT],
    )
    await cg.register_component(var, config)

    for conf in config.get(CONF_ON_RF2_TARGET, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(cg.int_, "target")], conf)

    for conf in config.get(CONF_ON_NATIVE_MODE_COMMAND, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(cg.bool_, "heat")], conf)
