#if defined(CONFIG_BT)

#include "ble_session.h"
#include "BLE.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <bluetooth/services/nus.h>
#include <zephyr/settings/settings.h>

LOG_MODULE_REGISTER(ble_session, LOG_LEVEL_INF);

/* P1.04 — reed switch (pull-up; short to GND when magnet present). */
#define REED_GPIO_NODE  DT_NODELABEL(gpio1)
#define REED_PIN        4

static const struct device *reed_port;
static struct gpio_callback reed_gpio_cb;
static struct k_work_delayable reed_debounce_work;
static struct k_work_delayable session_inactivity_work;

static bool session_active;
static bool ble_thread_released;
static bool nus_initialized;
static bool settings_have_loaded;

static void session_stop_internal(void);
static void inactivity_handler(struct k_work *work);
static void reed_debounce_handler(struct k_work *work);

void ble_session_feed_activity(void)
{
	if (session_active) {
		k_work_reschedule(&session_inactivity_work, K_MINUTES(5));
	}
}

static void inactivity_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	if (session_active) {
		LOG_INF("BLE session: 5 min inactivity, stopping");
		session_stop_internal();
	}
}

static void session_stop_internal(void)
{
	int err;

	k_work_cancel_delayable(&reed_debounce_work);
	k_work_cancel_delayable(&session_inactivity_work);

	dk_set_led_off(RUN_STATUS_LED);

	err = bt_le_adv_stop();
	if (err && err != -EALREADY) {
		LOG_WRN("Adv stop err %d", err);
	}

	if (current_conn) {
		err = bt_conn_disconnect(current_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		if (err && err != -ENOTCONN) {
			LOG_WRN("Disconnect err %d", err);
		}
	}

	err = bt_disable();
	if (err) {
		LOG_WRN("bt_disable err %d", err);
	}

	nus_initialized = false;
	session_active = false;
	LOG_INF("BLE session off");
}

static int session_start_internal(void)
{
	int err;

	if (session_active) {
		return 0;
	}

	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("bt_enable failed: %d", err);
		return err;
	}

	if (IS_ENABLED(CONFIG_SETTINGS) && !settings_have_loaded) {
		settings_have_loaded = true;
		settings_load();
	}

	if (!nus_initialized) {
		err = bt_nus_init(&nus_cb);
		if (err) {
			LOG_ERR("bt_nus_init failed: %d", err);
			(void)bt_disable();
			return err;
		}
		nus_initialized = true;
	}

	if (!ble_thread_released) {
		k_sem_give(&ble_init_ok);
		ble_thread_released = true;
	}

	advertising_start();
	session_active = true;
	k_work_reschedule(&session_inactivity_work, K_MINUTES(5));
	dk_set_led_on(RUN_STATUS_LED);
	LOG_INF("BLE session on (advertising)");
	return 0;
}

static void reed_debounce_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (!reed_port || !device_is_ready(reed_port)) {
		return;
	}

	if (session_active) {
		LOG_INF("Reed: closing BLE session");
		session_stop_internal();
	} else {
		LOG_INF("Reed: opening BLE session");
		(void)session_start_internal();
	}
}

static void reed_gpio_isr(const struct device *port, struct gpio_callback *cb,
			  uint32_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);
	k_work_reschedule(&reed_debounce_work, K_MSEC(50));
}

void ble_session_init(void)
{
	int err;

	reed_port = DEVICE_DT_GET(REED_GPIO_NODE);
	if (!device_is_ready(reed_port)) {
		LOG_ERR("GPIO1 not ready for reed (P1.04)");
		return;
	}

	err = gpio_pin_configure(reed_port, REED_PIN, GPIO_INPUT | GPIO_PULL_UP);
	if (err) {
		LOG_ERR("Reed pin cfg err %d", err);
		return;
	}

	err = gpio_pin_interrupt_configure(reed_port, REED_PIN, GPIO_INT_EDGE_BOTH);
	if (err) {
		LOG_ERR("Reed IRQ cfg err %d", err);
		return;
	}

	gpio_init_callback(&reed_gpio_cb, reed_gpio_isr, BIT(REED_PIN));
	gpio_add_callback(reed_port, &reed_gpio_cb);

	k_work_init_delayable(&reed_debounce_work, reed_debounce_handler);
	k_work_init_delayable(&session_inactivity_work, inactivity_handler);

	ble_advertising_work_init();

	dk_set_led_off(RUN_STATUS_LED);

	LOG_INF("Reed on P1.04: toggle to enable/disable BLE");
}

#endif /* CONFIG_BT */
