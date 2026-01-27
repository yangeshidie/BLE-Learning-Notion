#include <zephyr/types.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

// 定义广播数据 
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
    BT_DATA_BYTES(BT_DATA_MANUFACTURER_DATA, 0xFF, 0xFF, 0x41, 0x43, 0x45),
};

static const struct bt_data sd[] = {
    // 暂时留空
};

/*
 * 广播间隔计算公式: Time = Value * 0.625 ms
 * 
 * 我们想要 2000ms (2秒) 的间隔:
 * 2000 / 0.625 = 3200 (0x0C80)
 */
static struct bt_le_adv_param *adv_param = BT_LE_ADV_PARAM(
    BT_LE_ADV_OPT_CONNECTABLE | BT_LE_ADV_OPT_USE_NAME, // 选项：可连接 + 使用设备名
    3200, // Min Interval: 3200 * 0.625ms = 2000ms
    3200, // Max Interval: 3200 * 0.625ms = 2000ms
    NULL  // Peer Address (定向广播才需要)
);

int main(void)
{
    int err;

    // 初始化蓝牙
    err = bt_enable(NULL);
    if (err) {
        return 0; // 出错直接退出，不做打印处理以便省电测试
    }


    
    err = bt_le_adv_start(
        BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONNECTABLE, 3200, 3200, NULL), 
        ad, ARRAY_SIZE(ad), 
        sd, ARRAY_SIZE(sd)
    );
    
    if (err) {
        return 0;
    }

    // 原代码: k_sleep(K_SECONDS(1)); // 每秒醒一次，浪费电
    // 新代码: K_FOREVER
    // 主线程任务已完成，永久挂起，把 CPU 权交给 Idle Thread (系统休眠)
    for (;;) {
        k_sleep(K_FOREVER); 
    }
    return 0;
}