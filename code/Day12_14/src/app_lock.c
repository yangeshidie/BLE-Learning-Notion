#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include "app_lock.h"
#include "service_lock.h"
#include "ble_setup.h" 

LOG_MODULE_REGISTER(app_lock, LOG_LEVEL_INF);

/* ---------------- 硬件定义 ---------------- */
static const struct gpio_dt_spec led_lock = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
static const struct gpio_dt_spec button   = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

/* ---------------- 变量定义 ---------------- */
static struct gpio_callback button_cb_data;
static struct k_work_delayable lock_work;  // 开锁延时任务
static struct k_work button_work;          // [新增] 按键处理任务
static struct k_work open_door_work;
/* ---------------- 内部函数: Work Queue Handlers ---------------- */
static void open_door_work_handler(struct k_work *work)
{
    LOG_INF("Executing Unlock Sequence in System Thread");

    // 1. 硬件动作
    gpio_pin_set_dt(&led_lock, 1);

    // 2. 蓝牙状态更新 (在这里调用是安全的)
    service_lock_send_status(true);

    // 3. 调度自动关锁 (3秒)
    // 先取消可能存在的旧任务
    k_work_cancel_delayable(&lock_work);
    k_work_reschedule(&lock_work, K_SECONDS(3));
}
// [新增] 专门处理按键事件的任务 (运行在 System Work Queue 线程中)
static void button_work_handler(struct k_work *work)
{
    LOG_INF("Processing button event in thread context");
    // 在这里调用 BLE API 是安全的
    ble_setup_start_fast_adv();
}

// 自动关锁任务
static void lock_autoclose_handler(struct k_work *work)
{
    LOG_INF("Timeout: Locking door automatically.");
    gpio_pin_set_dt(&led_lock, 0);
    service_lock_send_status(false);
}

/* ---------------- 中断回调 (ISR) ---------------- */
// 注意：ISR 必须尽可能快，不能包含耗时或阻塞操作
static void button_pressed(const struct device *dev, struct gpio_callback *cb,
                           uint32_t pins)
{
    // 不要在这里调用 ble_setup_start_fast_adv() !!!
    // 而是提交任务给 WorkQueue
    k_work_submit(&button_work);
}

/* ---------------- 对外接口 ---------------- */

void app_lock_open(void)
{
    // 不要直接执行动作，而是提交任务
    // 这会立即返回，释放 BT RX 线程
    LOG_INF("Received Unlock Request -> Submitting to WorkQueue");
    k_work_submit(&open_door_work);
}

int app_lock_init(void)
{
    //int ret;

    // --- 1. 初始化所有 WorkQueue ---
    k_work_init_delayable(&lock_work, lock_autoclose_handler);
    k_work_init(&button_work, button_work_handler);
    k_work_init(&open_door_work, open_door_work_handler); // [新增] 初始化

    // --- 2. 初始化 LED (保持不变) ---
    if (!gpio_is_ready_dt(&led_lock)) return -ENODEV;
    gpio_pin_configure_dt(&led_lock, GPIO_OUTPUT_INACTIVE);

    // --- 3. 初始化 Button (保持不变) ---
    if (!gpio_is_ready_dt(&button)) return -ENODEV;
    gpio_pin_configure_dt(&button, GPIO_INPUT);
    gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
    gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
    gpio_add_callback(button.port, &button_cb_data);

    LOG_INF("App Lock Hardware Initialized");
    return 0;
}