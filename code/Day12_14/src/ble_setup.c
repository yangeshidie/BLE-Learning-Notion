#include "ble_setup.h"
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <string.h>

LOG_MODULE_REGISTER(ble_setup, LOG_LEVEL_INF);

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

static struct bt_le_adv_param *adv_param;
static struct bt_data adv_data[2];
static struct bt_data sd_data[1];

static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

static struct k_work adv_restart_work;

static void adv_restart_work_handler(struct k_work *work);

K_WORK_DEFINE(adv_restart_work, adv_restart_work_handler);

static struct bt_conn *current_conn;

static void adv_restart_work_handler(struct k_work *work)
{
    int err = bt_le_adv_start(adv_param, adv_data, ARRAY_SIZE(adv_data),
                              sd_data, ARRAY_SIZE(sd_data));
    if (err) {
        LOG_ERR("Advertising failed to restart (err %d)", err);
    } else {
        LOG_INF("Advertising restarted");
    }
}

static void connected(struct bt_conn *conn, uint8_t err)
{
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    if (err) {
        LOG_ERR("Connection failed (err %u)", err);
        return;
    }

    LOG_INF("Connected %s", addr);

    current_conn = bt_conn_ref(conn);

    ble_setup_set_connection_led(true);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    LOG_INF("Disconnected: %s (reason %u)", addr, reason);

    if (current_conn == conn) {
        bt_conn_unref(current_conn);
        current_conn = NULL;
    }

    ble_setup_set_connection_led(false);

    k_work_submit(&adv_restart_work);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};

static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey)
{
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    LOG_INF("Passkey for %s: %06u", addr, passkey);
}

static void auth_cancel(struct bt_conn *conn)
{
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    LOG_INF("Pairing cancelled: %s", addr);
}

static struct bt_conn_auth_cb conn_auth_callbacks = {
    .passkey_display = auth_passkey_display,
    .cancel = auth_cancel,
};

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    LOG_INF("Pairing completed: %s, bonded: %d", addr, bonded);
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    LOG_ERR("Pairing failed: %s, reason %d", addr, reason);
}

static struct bt_conn_auth_info_cb conn_auth_info_callbacks = {
    .pairing_complete = pairing_complete,
    .pairing_failed = pairing_failed,
};

int ble_setup_init(void)
{
    int err;

    if (!gpio_is_ready_dt(&led0)) {
        LOG_ERR("LED0 device not ready");
        return -ENODEV;
    }

    err = gpio_pin_configure_dt(&led0, GPIO_OUTPUT_INACTIVE);
    if (err) {
        LOG_ERR("Failed to configure LED0 (err %d)", err);
        return err;
    }

    err = bt_enable(NULL);
    if (err) {
        LOG_ERR("Bluetooth init failed (err %d)", err);
        return err;
    }

    LOG_INF("Bluetooth initialized");

    bt_conn_auth_cb_register(&conn_auth_callbacks);
    bt_conn_auth_info_cb_register(&conn_auth_info_callbacks);

    return 0;
}

int ble_setup_start_advertising(adv_mode_t mode)
{
    static uint8_t adv_buf[BT_GAP_ADV_MAX_EXT_DATA_LEN];
    static uint8_t sd_buf[BT_GAP_ADV_MAX_EXT_DATA_LEN];
    int err;

    err = ble_setup_stop_advertising();
    if (err && err != -ENOENT) {
        return err;
    }

    if (mode == ADV_MODE_FAST) {
        static struct bt_le_adv_param fast_adv_param =
            BT_LE_ADV_PARAM_INIT(BT_LE_ADV_OPT_CONNECTABLE |
                                 BT_LE_ADV_OPT_USE_IDENTITY,
                                 30,
                                 60,
                                 NULL);
        adv_param = &fast_adv_param;
        LOG_INF("Starting fast advertising");
    } else {
        static struct bt_le_adv_param slow_adv_param =
            BT_LE_ADV_PARAM_INIT(BT_LE_ADV_OPT_CONNECTABLE |
                                 BT_LE_ADV_OPT_USE_IDENTITY,
                                 800,
                                 1000,
                                 NULL);
        adv_param = &slow_adv_param;
        LOG_INF("Starting slow advertising");
    }

    adv_data[0] = BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN);
    adv_data[1] = BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR));

    sd_data[0] = BT_DATA_BYTES(BT_DATA_UUID16_ALL,
                               BT_UUID_16_ENCODE(BT_UUID_BAS_VAL));

    err = bt_le_adv_start(adv_param, adv_data, ARRAY_SIZE(adv_data),
                          sd_data, ARRAY_SIZE(sd_data));
    if (err) {
        LOG_ERR("Advertising failed to start (err %d)", err);
        return err;
    }

    LOG_INF("Advertising successfully started");
    return 0;
}

int ble_setup_stop_advertising(void)
{
    int err = bt_le_adv_stop();
    if (err && err != -ENOENT) {
        LOG_ERR("Advertising failed to stop (err %d)", err);
        return err;
    }
    return 0;
}

void ble_setup_set_connection_led(bool connected)
{
    int err = gpio_pin_set_dt(&led0, connected ? 1 : 0);
    if (err) {
        LOG_ERR("Failed to set LED0 (err %d)", err);
    }
}
