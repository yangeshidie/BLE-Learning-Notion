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
#include <zephyr/drivers/gpio.h> // <--- 新增 GPIO 驱动头文件

#include "my_service.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

// --- 硬件定义 ---
// 获取设备树中别名为 sw0 的节点
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET_OR(DT_ALIAS(sw0), gpios, {0});
// 定义一个 GPIO 回调结构体变量
static struct gpio_callback button_cb_data;

// --- 全局变量 ---
static uint8_t app_button_count = 0; // 按键计数器
static struct bt_conn *current_conn = NULL; // 当前连接

// --- 连接回调 ---
static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        LOG_ERR("Connection failed (err %u)", err);
    } else {
        LOG_INF("Connected");
        current_conn = conn;
    }
}

static void disconnected(struct bt_conn *conn, uint8_t err)
{
    LOG_INF("Disconnected (err %u)", err);
    if (current_conn == conn) {
        current_conn = NULL;
    }
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};

// --- 广播数据 (保持不变) ---
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, MY_SERVICE_UUID_VAL),
};

static const struct bt_data sd[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

// --- [Day 5] 按键中断回调函数 ---
/*
 * 注意：这是在中断上下文 (ISR) 中运行的。
 * 在这里应尽量少做耗时操作。
 * bt_gatt_notify 在 Zephyr 中通常可以在 ISR 中安全调用，
 * 但如果是复杂逻辑，建议使用 k_work_submit 放到工作队列中处理。
 */
void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    // 简单的防抖动处理 (非必须，但推荐)：
    // 如果机械按键抖动严重，会导致一瞬间发送多次 Notify
    // 这里用简单的 Kernel uptime 过滤一下，间隔小于 200ms 的触发视为抖动
    static int64_t last_time = 0;
    int64_t now = k_uptime_get();
    if (now - last_time < 200) {
        return; 
    }
    last_time = now;

    // 逻辑处理
    app_button_count++;
    LOG_INF("Button pressed! Count: %d", app_button_count);

    // 发送 BLE 通知
    // 传入 NULL 表示发给所有订阅了的 Client
    my_service_send_button_notify(current_conn, app_button_count);
}

// --- 初始化按键 ---
void init_button(void)
{
    int ret;

    // 1. 检查设备是否就绪
    if (!gpio_is_ready_dt(&button)) {
        LOG_ERR("Error: button device %s is not ready", button.port->name);
        return;
    }

    // 2. 配置引脚为输入 (GPIO_PULL_DOWN 已在设备树中定义，这里会自动应用)
    ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
    if (ret != 0) {
        LOG_ERR("Error %d: failed to configure %s pin %d", ret, button.port->name, button.pin);
        return;
    }

    // 3. 配置中断：边沿触发，也就是按下那一瞬间 (Active 状态)
    ret = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
    if (ret != 0) {
        LOG_ERR("Error %d: failed to configure interrupt on %s pin %d", ret, button.port->name, button.pin);
        return;
    }

    // 4. 初始化回调结构体，并绑定回调函数
    gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
    
    // 5. 将回调添加到 GPIO 驱动中
    gpio_add_callback(button.port, &button_cb_data);
    
    LOG_INF("Button initialized at P0.%02d", button.pin);
}

// --- 主函数 ---
void main(void)
{
    int err;

    LOG_INF("Starting Bluetooth Peripheral GATT Demo (Day 5)");

    // 初始化硬件
    init_button(); // <--- 调用按键初始化

    // 初始化蓝牙
    err = bt_enable(NULL);
    if (err) {
        LOG_ERR("Bluetooth init failed (err %d)", err);
        return;
    }

    my_service_init();

    err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err) {
        LOG_ERR("Advertising failed to start (err %d)", err);
        return;
    }

    LOG_INF("Advertising started...");

    while (1) {
        k_sleep(K_FOREVER);
    }
}