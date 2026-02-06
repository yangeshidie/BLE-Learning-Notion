#include <stddef.h>
#include <stdint.h>
#include <zephyr/types.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

#include "service_lock.h"
#include "app_lock.h"

LOG_MODULE_REGISTER(service_lock, LOG_LEVEL_INF);

/* ---------------- UUID 定义 ---------------- */
// 自定义 Service UUID: 12345678-1234-5678-1234-56789ABC0000
#define LOCK_SVC_UUID_VAL \
    BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789ABC0000)

// Control Point UUID (Write): ...0001
#define LOCK_CTRL_UUID_VAL \
    BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789ABC0001)

// Status UUID (Notify): ...0002
#define LOCK_STATUS_UUID_VAL \
    BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789ABC0002)

static struct bt_uuid_128 lock_svc_uuid = BT_UUID_INIT_128(LOCK_SVC_UUID_VAL);
static struct bt_uuid_128 lock_ctrl_uuid = BT_UUID_INIT_128(LOCK_CTRL_UUID_VAL);
static struct bt_uuid_128 lock_status_uuid = BT_UUID_INIT_128(LOCK_STATUS_UUID_VAL);

/* ---------------- 状态变量 ---------------- */
static bool notify_enabled = false;
static uint8_t current_lock_status = 0; // 0=Locked, 1=Unlocked

/* ---------------- 回调函数 ---------------- */

// CCCD (Client Characteristic Configuration) 改变时的回调
static void lock_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    notify_enabled = (value == BT_GATT_CCC_NOTIFY);
    LOG_INF("Notification %s", notify_enabled ? "enabled" : "disabled");
}

// 写入回调：接收手机发来的开锁指令
static ssize_t write_lock_ctrl(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                               const void *buf, uint16_t len, uint16_t offset,
                               uint8_t flags)
{
    const uint8_t *val = buf;

    // 简单校验数据长度
    if (len != 1) {
        LOG_WRN("Invalid write length: %d", len);
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    // 0x01 = 开锁指令
    if (val[0] == 0x01) {
        LOG_INF("Received Unlock Command from App");
        // 调用应用层接口执行硬件动作
        app_lock_open();
    } else {
        LOG_WRN("Unknown command: 0x%02x", val[0]);
    }

    return len;
}

// 读取回调：获取当前锁状态
static ssize_t read_lock_status(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                void *buf, uint16_t len, uint16_t offset)
{
    // current_lock_status 由 app_lock 逻辑间接控制，或者通过 service_lock_send_status 更新
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &current_lock_status, sizeof(current_lock_status));
}

/* ---------------- GATT 服务定义 ---------------- */
BT_GATT_SERVICE_DEFINE(smart_lock_svc,
    BT_GATT_PRIMARY_SERVICE(&lock_svc_uuid),

    // Characteristic 1: Control Point (Write Only)
    // 关键点：BT_GATT_PERM_WRITE_ENCRYPT -> 必须加密链路才能写入（强制配对）
    BT_GATT_CHARACTERISTIC(&lock_ctrl_uuid.uuid,
                           BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_WRITE_ENCRYPT, 
                           NULL, write_lock_ctrl, NULL),

    // Characteristic 2: Status (Read | Notify)
    // 关键点：BT_GATT_PERM_READ_ENCRYPT -> 必须加密链路才能读取
    BT_GATT_CHARACTERISTIC(&lock_status_uuid.uuid,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ_ENCRYPT,
                           read_lock_status, NULL, NULL),
    
    // CCCD (用于开启 Notify)
    BT_GATT_CCC(lock_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE_ENCRYPT),
);

/* ---------------- 对外接口 ---------------- */

int service_lock_send_status(bool is_unlocked)
{
    current_lock_status = is_unlocked ? 1 : 0;

    if (!notify_enabled) {
        return 0; // 客户端没订阅，无需发送
    }

    // 发送 Notify
    return bt_gatt_notify_uuid(NULL, 
                           &lock_status_uuid.uuid, 
                           smart_lock_svc.attrs, 
                           &current_lock_status, 
                           sizeof(current_lock_status));
}