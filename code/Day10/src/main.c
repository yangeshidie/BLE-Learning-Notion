#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/dfu/mcuboot.h> // 必须包含
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main);

// ==========================================
//在此处修改宏来切换版本行为
// 1 = V1 固件 (亮红灯 led0)
// 2 = V2 固件 (亮绿灯 led1)
#define FIRMWARE_VERSION  2
// ==========================================

/* 获取设备树中的 LED 定义 */
static const struct gpio_dt_spec led_red  = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec led_green = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);

/* 广播数据 */
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    /* 动态广播名在下面 bt_set_name 设置，这里只放 Flags */
};

void main(void)
{
    int err;

    // 1. 初始化 LED
    if (!gpio_is_ready_dt(&led_red) || !gpio_is_ready_dt(&led_green)) {
        LOG_ERR("LEDs not ready");
        return;
    }
    gpio_pin_configure_dt(&led_red, GPIO_OUTPUT_ACTIVE);
    gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_ACTIVE);

    // 先全灭
    gpio_pin_set_dt(&led_red, 0);
    gpio_pin_set_dt(&led_green, 0);

    // 2. 根据版本号亮灯
#if FIRMWARE_VERSION == 1
    LOG_INF("Firmware V1 Running: Red LED ON");
    gpio_pin_set_dt(&led_red, 1); // V1 亮红灯
    // 设置蓝牙名称
    bt_set_name("FOTA_V1_Red");
#else
    LOG_INF("Firmware V2 Running: Green LED ON");
    gpio_pin_set_dt(&led_green, 1); // V2 亮绿灯
    bt_set_name("FOTA_V2_Green");
#endif

    // 3. 【关键】确认镜像
    // 告诉 Bootloader：“我启动成功了，请把这次升级标记为永久有效”
    // 如果不调用这个，重启后会回滚到旧版本
    boot_write_img_confirmed();

    // 4. 启动蓝牙
    err = bt_enable(NULL);
    if (err) {
        LOG_ERR("Bluetooth init failed (err %d)", err);
        return;
    }

    // 5. 开启广播
    err = bt_le_adv_start(BT_LE_ADV_CONN_NAME, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err) {
        LOG_ERR("Advertising failed to start (err %d)", err);
        return;
    }

    LOG_INF("Advertising started...");

    // 主循环：打印一下心跳，方便 RTT 观察
    while (1) {
        k_sleep(K_SECONDS(2));
        LOG_INF("System is alive. Version: %d", FIRMWARE_VERSION);
    }
}