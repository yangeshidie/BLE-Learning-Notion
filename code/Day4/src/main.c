#include <zephyr/types.h>
#include <stddef.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>

// 引入我们刚才写的服务头文件
#include "my_service.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

// --- 广播数据配置 ---

/* 
 * ad (Advertising Data): 广播包数据，最大 31 字节。
 * 这里我们放入了两个重要元素：
 * 1. Flags: 说明设备模式（如：有限发现/一般发现，不支持经典蓝牙）。
 * 2. UUID: 将我们的 128-bit 自定义服务 UUID 放入广播包。
 *    注意：128-bit UUID 占 16 字节，加上类型头，非常占空间。
 */
static const struct bt_data ad[] = {
    // 设置广播 Flags
    // BT_LE_AD_GENERAL: 一般可发现模式 (手机可以一直扫描到)
    // BT_LE_AD_NO_BREDR: 不支持经典蓝牙 (仅 BLE)
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),

    /* 
     * API 解析: BT_DATA_BYTES
     * 功能: 将一串字节序列构造成符合 BLE 格式的广播数据结构 (struct bt_data)。
     * 参数1: 数据类型 (Data Type)。
     *        BT_DATA_UUID128_ALL 表示“完整的 128-bit 服务 UUID 列表”。
     *        这意味着设备只支持这一个服务，或者列表里包含了所有支持的服务。
     * 参数2: 具体的 UUID 值 (来自 my_service.h 中的宏)。
     */
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, MY_SERVICE_UUID_VAL),
};

/* 
 * sd (Scan Response Data): 扫描响应数据。
 * 当手机主动发起“扫描请求”时，设备回复的数据。
 * 通常把“设备名称”放在这里，以节省广播包 (ad) 的空间。
 */
static const struct bt_data sd[] = {
    // 将设备名称放入扫描响应包中
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

// --- 系统初始化与主循环 ---

void main(void)
{
    int err;

    LOG_INF("Starting Bluetooth Peripheral GATT Demo");

    // 1. 初始化蓝牙协议栈
    // API: bt_enable(callback)
    // 功能: 初始化控制器和主机协议栈。如果传入 NULL，则同步初始化，成功返回 0。
    err = bt_enable(NULL);
    if (err) {
        LOG_ERR("Bluetooth init failed (err %d)", err);
        return;
    }
    LOG_INF("Bluetooth initialized");

    // 2. 初始化我们的自定义服务
    // 虽然 BT_GATT_SERVICE_DEFINE 是静态宏，但如果有动态逻辑可以在这里调用 init
    my_service_init();

    // 3. 开始广播
    // API: bt_le_adv_start(param, ad, ad_len, sd, sd_len)
    // 参数1: 广播参数 (BT_LE_ADV_CONN_NAME 表示可连接广播，并尝试在广播包中包含名字)
    //        *注：因为我们手动定义了 ad 和 sd，这里用 BT_LE_ADV_CONN 即可
    // 参数2/3: 广播数据数组及其大小
    // 参数4/5: 扫描响应数据数组及其大小
    err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err) {
        LOG_ERR("Advertising failed to start (err %d)", err);
        return;
    }

    LOG_INF("Advertising successfully started...");
    LOG_INF("Waiting for connection...");

    // 主线程可以休眠了，所有的逻辑现在都由 BLE 协议栈线程和回调函数驱动
    while (1) {
        k_sleep(K_FOREVER);
    }
}