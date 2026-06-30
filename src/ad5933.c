/*
 * AD5933 — Phase A: Zephyr I2C register access (no sweep yet).
 */
#if defined(CONFIG_CHRONOS_ZMEAS)

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>

#include "ad5933.h"
#include "zmeas_gpio.h"

LOG_MODULE_REGISTER(ad5933, LOG_LEVEL_INF);

#define I2C22_NODE DT_NODELABEL(i2c22)

#if !DT_NODE_HAS_STATUS(I2C22_NODE, okay)
#error "chronos: i2c22 must be okay in board overlay (AD5933 bus)"
#endif

static const struct i2c_dt_spec ad5933_i2c = {
	.bus = DEVICE_DT_GET(I2C22_NODE),
	.addr = AD5933_I2C_ADDR_7BIT,
};

static int ad5933_read_reg(const uint8_t reg, uint8_t *buf, const size_t len)
{
	if (!device_is_ready(ad5933_i2c.bus)) {
		LOG_ERR("I2C bus not ready");
		return -ENODEV;
	}

	return i2c_write_read_dt(&ad5933_i2c, &reg, 1, buf, len);
}

int ad5933_phase_a_probe(bool connect_switch)
{
	uint8_t ctrl[2];
	uint8_t status;
	int err;

	zmeas_gpio_init();
	zmeas_gpio_switch_set(connect_switch);

	if (connect_switch) {
		k_msleep(10);
	}

	err = ad5933_read_reg(AD5933_REG_CONTROL, ctrl, sizeof(ctrl));
	if (err != 0) {
		LOG_ERR("AD5933 control read failed: %d", err);
		return err;
	}

	err = ad5933_read_reg(AD5933_REG_STATUS, &status, 1);
	if (err != 0) {
		LOG_ERR("AD5933 status read failed: %d", err);
		return err;
	}

	const uint16_t control = (uint16_t)(((uint16_t)ctrl[0] << 8) | ctrl[1]);

	LOG_INF("AD5933 Phase A OK: switch=%s control=0x%04x status=0x%02x",
		connect_switch ? "connect" : "disconnect",
		control, status);

	return 0;
}

#else /* !CONFIG_CHRONOS_ZMEAS */

#include <zephyr/kernel.h>

#include "ad5933.h"

int ad5933_phase_a_probe(bool connect_switch)
{
	ARG_UNUSED(connect_switch);
	return -ENOTSUP;
}

#endif /* CONFIG_CHRONOS_ZMEAS */
