#include <zephyr/types.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>

/* 
 * 1. 定义广播名称 
 * 这里使用了 Kconfig 中的宏，方便统一管理
 */
#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

/* 
 * 2. 自定义 Manufacturer Data 
 * 格式：[Company ID low] [Company ID high] [Data...]
 * 0xFFFF 是测试用的 Company ID。
 * 0x41, 0x43, 0x45 对应 "ACE"
 */
static const struct bt_data ad[] = {
    // Flags: 一般设为 General Discoverable (一般可被发现) + BR/EDR Not Supported (不支持经典蓝牙)
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    
    // Name: 广播全名
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),

    // Manufacturer Data: 类型 0xFF
    // 这里的 {} 初始化对应 {Company ID Lo, Company ID Hi, 'A', 'C', 'E'}
    BT_DATA_BYTES(BT_DATA_MANUFACTURER_DATA, 0xFF, 0xFF, 0x41, 0x43, 0x45),
};

/* 
 * 3. 扫描响应数据 (Scan Response)
 * 当手机“主动扫描”时，设备会回复这个包。通常放 128-bit UUID 等放不下的数据。
 * 今天先留空，或者简单放个短名。
 */
static const struct bt_data sd[] = {
    // 暂时留空
};

int main(void)
{
    int err;

    printk("Starting Advertising Demo\n");

    /* 初始化蓝牙协议栈 */
    err = bt_enable(NULL);
    if (err) {
        printk("Bluetooth init failed (err %d)\n", err);
        return 0;
    }

    printk("Bluetooth initialized\n");

    /* 
     * 4. 开始广播 
     * BT_LE_ADV_CONN_NAME: 
     *    - 可连接 (Connectable)
     *    - 使用代码中定义的名称
     *    - 默认广播间隔 (约 100ms)
     */
    err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    
    // 如果你想手动控制参数（例如省电），可以用 struct bt_le_adv_param
    // err = bt_le_adv_start(BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONNECTABLE, 
    //                       BT_GAP_ADV_FAST_INT_MIN_2, BT_GAP_ADV_FAST_INT_MAX_2, NULL), 
    //                       ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));

    if (err) {
        printk("Advertising failed to start (err %d)\n", err);
        return 0;
    }

    printk("Advertising successfully started\n");

    // 主线程可以去睡大觉了，蓝牙在 Controller 线程运行
    for (;;) {
        k_sleep(K_SECONDS(1));
    }
    return 0;
}