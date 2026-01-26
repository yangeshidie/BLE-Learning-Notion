#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>    
#include <zephyr/logging/log.h>       

#include "my_service.h"


LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);


static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, MY_SERVICE_UUID_VAL),
};

static const struct bt_data sd[] = {
    // 将设备名称放入扫描响应包中
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};



static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey);
static void auth_cancel(struct bt_conn *conn);

static struct bt_conn_auth_cb auth_cb = {
    .passkey_display = auth_passkey_display,
    .cancel = auth_cancel,
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
    LOG_WRN("Pairing cancelled: %s", addr);
}


int main(void)
{
    int err;
    LOG_INF("Starting BLE Secure Peripheral");

    err = bt_enable(NULL);
    if (err) {
        LOG_ERR("Bluetooth init failed (err %d)", err);
        return 0; 
    }

    // 在 bt_enable 之后注册回调
    err = bt_conn_auth_cb_register(&auth_cb);
    if (err) {
        LOG_ERR("Auth callback registration failed (err %d)", err);
        return 0;
    }

    LOG_INF("Bluetooth initialized");

    my_service_init();

    err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err) {
        LOG_ERR("Advertising failed to start (err %d)", err);
        return 0;
    }
    LOG_INF("Advertising successfully started...");

    // main 函数在 Zephyr 中执行完后线程就结束了，所以通常不返回
    while (1) {
        k_msleep(1000); // 线程主动让出 CPU 1000ms
    }
    return 0;
}