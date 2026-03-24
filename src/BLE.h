#ifndef BLE_H
#define BLE_H

#include <zephyr/types.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>

#if defined(CONFIG_BT)
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/drivers/uart.h>
#ifdef CONFIG_UART_ASYNC_ADAPTER
#include <uart_async_adapter.h>
#endif
#endif

#if defined(CONFIG_BT)
#if defined(CONFIG_BT_NUS_THREAD_STACK_SIZE)
#define BLE_NUS_STACK_SIZE CONFIG_BT_NUS_THREAD_STACK_SIZE
#else
#define BLE_NUS_STACK_SIZE 2048
#endif
#define STACKSIZE BLE_NUS_STACK_SIZE
#define PRIORITY 7
#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)
#if defined(CONFIG_BT_NUS_UART_BUFFER_SIZE)
#define BLE_NUS_UART_BUF_SIZE CONFIG_BT_NUS_UART_BUFFER_SIZE
#else
#define BLE_NUS_UART_BUF_SIZE 256
#endif
#define UART_BUF_SIZE BLE_NUS_UART_BUF_SIZE
#if defined(CONFIG_BT_NUS_UART_RX_WAIT_TIME)
#define UART_WAIT_FOR_RX CONFIG_BT_NUS_UART_RX_WAIT_TIME
#else
#define UART_WAIT_FOR_RX 50000
#endif
#else
#define STACKSIZE 256
#define PRIORITY 7
#define DEVICE_NAME "Chronos"
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)
#define UART_BUF_SIZE 40
#define UART_WAIT_FOR_RX 50000
#endif

#define RUN_LED_BLINK_INTERVAL 1000

#define UART_WAIT_FOR_BUF_DELAY K_MSEC(50)

#if defined(CONFIG_BT)
#ifdef CONFIG_UART_ASYNC_ADAPTER
UART_ASYNC_ADAPTER_INST_DEFINE(async_adapter);
#else
#define async_adapter NULL
#endif
#include <dk_buttons_and_leds.h>
#define RUN_STATUS_LED DK_LED1
#define CON_STATUS_LED DK_LED2
#define KEY_PASSKEY_ACCEPT DK_BTN1_MSK
#define KEY_PASSKEY_REJECT DK_BTN2_MSK
#else
#define RUN_STATUS_LED 0
#define CON_STATUS_LED 0
#define KEY_PASSKEY_ACCEPT 0
#define KEY_PASSKEY_REJECT 0
#endif

extern struct k_sem ble_init_ok;
#if defined(CONFIG_BT)
struct bt_nus_cb;
extern struct bt_nus_cb nus_cb;
void ble_advertising_work_init(void);
extern struct k_work_delayable uart_work;
extern const struct device *uart;
void uart_work_handler(struct k_work *item);
bool uart_test_async_api(const struct device *dev);
void configure_gpio(void);
void error(void);
void uart_cb(const struct device *dev, struct uart_event *evt, void *user_data);
int uart_init(void);
#endif
void ble_write_thread(void);

#if defined(CONFIG_BT)
extern struct k_work adv_work;
extern struct bt_conn *current_conn;
extern struct bt_conn *auth_conn;
extern const struct bt_data ad[];
extern const struct bt_data sd[];
extern const size_t ad_len;
extern const size_t sd_len;
extern bool ble_data_ready;
void adv_work_handler(struct k_work *work);
void advertising_start(void);
void connected(struct bt_conn *conn, uint8_t err);
void disconnected(struct bt_conn *conn, uint8_t reason);
void recycled_cb(void);
void bt_receive_cb(struct bt_conn *conn, const uint8_t *const data, uint16_t len);

#ifdef CONFIG_BT_NUS_SECURITY_ENABLED
void security_changed(struct bt_conn *conn, bt_security_t level,
		      enum bt_security_err err);
#endif

#if defined(CONFIG_BT_NUS_SECURITY_ENABLED)
void auth_passkey_display(struct bt_conn *conn, unsigned int passkey);
void auth_passkey_confirm(struct bt_conn *conn, unsigned int passkey);
void auth_cancel(struct bt_conn *conn);
void pairing_complete(struct bt_conn *conn, bool bonded);
void pairing_failed(struct bt_conn *conn, enum bt_security_err reason);
extern struct bt_conn_auth_cb conn_auth_callbacks;
extern struct bt_conn_auth_info_cb conn_auth_info_callbacks;
#else
extern struct bt_conn_auth_cb conn_auth_callbacks;
extern struct bt_conn_auth_info_cb conn_auth_info_callbacks;
#endif

#ifdef CONFIG_BT_NUS_SECURITY_ENABLED
void num_comp_reply(bool accept);
void button_changed(uint32_t button_state, uint32_t has_changed);
#endif
#endif /* CONFIG_BT */

struct uart_data_t {
	void *fifo_reserved;
	uint8_t data[UART_BUF_SIZE];
	uint16_t len;
};

#endif /* BLE_H */
