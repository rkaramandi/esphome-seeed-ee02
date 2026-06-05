import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import display, spi
from esphome import pins
from esphome.const import (
    CONF_ID,
    CONF_RESET_PIN,
    CONF_DC_PIN,
    CONF_BUSY_PIN,
    CONF_LAMBDA,
    CONF_PAGES,
)

DEPENDENCIES = ["spi"]
AUTO_LOAD = ["display"]

CONF_CS_MASTER_PIN = "cs_master_pin"
CONF_CS_SLAVE_PIN = "cs_slave_pin"
CONF_POWER_PIN = "power_pin"

ee02_epaper_ns = cg.esphome_ns.namespace("ee02_epaper")
EE02Epaper = ee02_epaper_ns.class_(
    "EE02Epaper", cg.PollingComponent, spi.SPIDevice, display.DisplayBuffer
)

CONFIG_SCHEMA = cv.All(
    display.FULL_DISPLAY_SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(EE02Epaper),
            cv.Required(CONF_RESET_PIN): pins.gpio_output_pin_schema,
            cv.Required(CONF_DC_PIN): pins.gpio_output_pin_schema,
            cv.Required(CONF_BUSY_PIN): pins.gpio_input_pin_schema,
            cv.Required(CONF_POWER_PIN): pins.gpio_output_pin_schema,
            cv.Required(CONF_CS_MASTER_PIN): pins.gpio_output_pin_schema,
            cv.Required(CONF_CS_SLAVE_PIN): pins.gpio_output_pin_schema,
        }
    )
    .extend(cv.polling_component_schema("never"))
    # CS handled manually (two of them) so don't let spi own a single CS line.
    .extend(spi.spi_device_schema(cs_pin_required=False)),
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await display.register_display(var, config)
    await spi.register_spi_device(var, config)

    reset = await cg.gpio_pin_expression(config[CONF_RESET_PIN])
    cg.add(var.set_reset_pin(reset))
    dc = await cg.gpio_pin_expression(config[CONF_DC_PIN])
    cg.add(var.set_dc_pin(dc))
    busy = await cg.gpio_pin_expression(config[CONF_BUSY_PIN])
    cg.add(var.set_busy_pin(busy))
    power = await cg.gpio_pin_expression(config[CONF_POWER_PIN])
    cg.add(var.set_power_pin(power))
    csm = await cg.gpio_pin_expression(config[CONF_CS_MASTER_PIN])
    cg.add(var.set_cs_master_pin(csm))
    css = await cg.gpio_pin_expression(config[CONF_CS_SLAVE_PIN])
    cg.add(var.set_cs_slave_pin(css))

    if CONF_LAMBDA in config:
        lambda_ = await cg.process_lambda(
            config[CONF_LAMBDA], [(display.DisplayRef, "it")], return_type=cg.void
        )
        cg.add(var.set_writer(lambda_))
