#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/settings/settings.h>

#include "ble_setup.h"

// 注册日志模块
LOG_MODULE_REGISTER(ble_setup, LOG_LEVEL_INF);

/* ----------------配置参数---------------- */
#define FAST_ADV_DURATION_SEC   30 // 快速广播持续30秒

// 快速广播参数: Interval 40ms - 50ms
static const struct bt_le_adv_param *adv_param_fast =
    BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONNECTABLE,
                    BT_GAP_ADV_FAST_INT_MIN_2,
                    BT_GAP_ADV_FAST_INT_MAX_2,
                    NULL);

// 慢速广播参数: Interval 1000ms - 1500ms
static const struct bt_le_adv_param *adv_param_slow =
    BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONNECTABLE,
                    1600, // 1600 * 0.625us = 1000ms
                    2400, // 2400 * 0.625us = 1500ms
                    NULL);

/* ----------------全局变量---------------- */
static struct bt_conn *current_conn = NULL; // 当前连接句柄
static struct k_work_delayable adv_mode_work; // 用于广播超时切换的定时任务

/* 
 * 广播数据包 (Advertising Data)
 * 包含: Flags, Device Name, Battery Service UUID
 * 注意: 自定义 Service UUID 通常较长，建议放在 Scan Response 中，或者只放部分
 */
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
    BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_BAS_VAL)), 
};

/* ----------------内部函数声明---------------- */
static void start_advertising_slow(void);
static void start_advertising_fast(void);
static void security_changed(struct bt_conn *conn, bt_security_t level,
                             enum bt_security_err err);
/* ----------------WorkQueue 回调---------------- */
// 当快速广播 30秒超时后执行此函数
static void adv_timeout_handler(struct k_work *work)
{
    LOG_INF("Fast advertising timeout. Switching to SLOW advertising.");
    
    // 停止当前广播
    bt_le_adv_stop();
    
    // 切换到慢速广播 (永久运行，直到连接或掉电)
    start_advertising_slow();
}

/* ----------------辅助函数---------------- */
static void start_advertising_fast(void)
{
    int err = bt_le_adv_start(adv_param_fast, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err) {
        if (err == -EALREADY) return; // 已经在广播中，忽略
        LOG_ERR("Failed to start fast advertising (err %d)", err);
    } else {
        LOG_INF("Fast advertising started (30s timeout)");
        // 启动/重置 30秒 倒计时
        k_work_reschedule(&adv_mode_work, K_SECONDS(FAST_ADV_DURATION_SEC));
    }
}

static void start_advertising_slow(void)
{
    int err = bt_le_adv_start(adv_param_slow, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err) {
        LOG_ERR("Failed to start slow advertising (err %d)", err);
    } else {
        LOG_INF("Slow advertising started (Infinite)");
    }
    // 慢速广播不需要超时处理，取消任何挂起的定时器
    k_work_cancel_delayable(&adv_mode_work);
}

/* ----------------连接回调 (Connection Callbacks)---------------- */
static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        LOG_ERR("Connection failed (err 0x%02x)", err);
        return;
    }

    LOG_INF("Connected");
    current_conn = bt_conn_ref(conn);

    // 连接成功后，停止广播超时计时器
    k_work_cancel_delayable(&adv_mode_work);
    
    // 可以在这里调用 LED 控制函数点亮 LED0 (建议通过回调或 extern 实现)
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    LOG_INF("Disconnected (reason 0x%02x)", reason);

    if (current_conn) {
        bt_conn_unref(current_conn);
        current_conn = NULL;
    }

    // 断开连接后，立即进入快速广播以便重连
    start_advertising_fast();
}

// 注册连接回调结构体
BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
    .security_changed = security_changed,
};

/* ----------------安全回调 (Security Callbacks)---------------- */
static void security_changed(struct bt_conn *conn, bt_security_t level,
                             enum bt_security_err err)
{
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    if (!err) {
        LOG_INF("Security changed: %s level %u", addr, level);
    } else {
        LOG_ERR("Security failed: %s level %u err %d", addr, level, err);
    }
}

static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey)
{
    // 本项目使用 Just Works，不应触发此回调，但保留作为调试
    LOG_INF("Passkey display: %u", passkey);
}

static void auth_cancel(struct bt_conn *conn)
{
    LOG_INF("Pairing cancelled");
}

// 注册安全回调
static struct bt_conn_auth_cb auth_cb_display = {
    .passkey_display = auth_passkey_display,
    .passkey_entry = NULL,
    .cancel = auth_cancel,
};

static void auth_pairing_complete(struct bt_conn *conn, bool bonded)
{
    LOG_INF("Pairing Complete. Bonded: %d", bonded);
}

static struct bt_conn_auth_info_cb auth_cb_info = {
    .pairing_complete = auth_pairing_complete,
    .pairing_failed = NULL // 可以添加错误处理
};

/* ----------------对外接口实现---------------- */

int ble_setup_init(void)
{
    int err;

    // 1. 初始化定时器 (保持不变)
    k_work_init_delayable(&adv_mode_work, adv_timeout_handler);

    // 2. 注册回调 (保持不变)
    bt_conn_auth_cb_register(&auth_cb_display);
    bt_conn_auth_info_cb_register(&auth_cb_info);

    // 3. 启用蓝牙栈
    err = bt_enable(NULL);
    if (err) {
        LOG_ERR("Bluetooth init failed (err %d)", err);
        return err;
    }


    if (IS_ENABLED(CONFIG_BT_SETTINGS)) {
        LOG_INF("Loading settings from Flash...");
        settings_load(); 
    }
    // ==========================================

    LOG_INF("Bluetooth initialized");

    // 4. 启动广播
    start_advertising_slow(); 

    return 0;
}

void ble_setup_start_fast_adv(void)
{
    // 如果已经连接，不允许切换广播模式
    if (current_conn) {
        LOG_WRN("Cannot start fast adv: Already connected");
        return;
    }

    // 停止当前可能正在运行的慢速广播
    bt_le_adv_stop();
    
    // 启动快速广播
    start_advertising_fast();
}

bool ble_is_connected(void)
{
    return (current_conn != NULL);
}