/*
 * Module: My NUS Implementation
 * Description: 实现 NUS GATT 服务
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>

#include "nus.h"

LOG_MODULE_REGISTER(my_nus, LOG_LEVEL_ERR);

static struct my_nus_cb nus_cb;

/* TX Characteristic (Notify) 的配置改变回调 (CCC Write) */
static void on_cccd_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    bool notif_enabled = (value == BT_GATT_CCC_NOTIFY);
    LOG_INF("NUS Notifications %s", notif_enabled ? "enabled" : "disabled");
}

/* RX Characteristic (Write) 的写入回调 */
static ssize_t on_receive_data(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                               const void *buf, uint16_t len, uint16_t offset,
                               uint8_t flags)
{
    LOG_DBG("Received %d bytes", len);
    if (nus_cb.received) {
        nus_cb.received(conn, buf, len);
    }
    return len;
}

/* 
 * 定义 GATT 服务 
 * 结构：
 * 1. Primary Service 声明
 * 2. RX Characteristic (Write | Write Without Response)
 * 3. TX Characteristic (Notify) + CCC (Client Characteristic Configuration)
 */
BT_GATT_SERVICE_DEFINE(my_nus_svc,
    BT_GATT_PRIMARY_SERVICE(BT_UUID_MY_NUS_SERVICE),

    /* RX: 手机写入数据到这里 */
    BT_GATT_CHARACTERISTIC(BT_UUID_MY_NUS_RX,
                           BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE,
                           NULL, on_receive_data, NULL),

    /* TX: 设备通知数据给手机 */
    BT_GATT_CHARACTERISTIC(BT_UUID_MY_NUS_TX,
                           BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_NONE, 
                           NULL, NULL, NULL),
    
    /* TX 的 CCC 描述符，允许手机开启/关闭通知 */
    BT_GATT_CCC(on_cccd_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

int my_nus_init(struct my_nus_cb *callbacks)
{
    if (!callbacks) {
        return -EINVAL;
    }
    nus_cb = *callbacks;
    return 0;
}

int my_nus_send(struct bt_conn *conn, const uint8_t *data, uint16_t len)
{
    int err;
    
    LOG_DBG("my_nus_send: conn=%p, len=%d", (void *)conn, len);
    
    /* 使用 bt_gatt_notify_uuid 发送数据 */
    err = bt_gatt_notify_uuid(conn, BT_UUID_MY_NUS_TX, NULL, data, len);
    
    if (err) {
        LOG_ERR("bt_gatt_notify_uuid failed: %d", err);
    }
    
    return err;
}