#include <zephyr/kernel.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/task_wdt/task_wdt.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/fs/nvs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(Day11, LOG_LEVEL_INF);

static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec led2 = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

static struct gpio_callback button_cb_data;

// ================= 配置部分 =================
#define NVS_PARTITION		storage_partition 

#define REBOOT_COUNTER_ID   1
#define WDT_TIMEOUT_MS      5000

// 定义硬件看门狗节点
#define WDT_NODE DT_ALIAS(watchdog0)

static struct nvs_fs fs;
static int wdt_channel_id;
static bool simulate_hang = false;

// ================= NVS 初始化与读写 =================
void init_nvs_and_count(void)
{
    struct flash_pages_info info;
    int rc;
    uint32_t reboot_counter = 0;

    // 使用 FIXED_PARTITION_DEVICE 宏，参数是设备树节点标签 (storage_partition)
    fs.flash_device = FIXED_PARTITION_DEVICE(NVS_PARTITION);
    
    if (!device_is_ready(fs.flash_device)) {
        LOG_ERR("Flash device not ready");
        return;
    }
    
    // 使用 FIXED_PARTITION_OFFSET 宏
    fs.offset = FIXED_PARTITION_OFFSET(NVS_PARTITION);

    // 获取该 Flash 区域的页信息（扇区大小）
    rc = flash_get_page_info_by_offs(fs.flash_device, fs.offset, &info);
    if (rc) {
        LOG_ERR("Unable to get page info");
        return;
    }
    
    fs.sector_size = info.size;
    fs.sector_count = 3U; // 使用 3 个扇区 (Pages)

    // 挂载 NVS
    rc = nvs_mount(&fs);
    if (rc) {
        LOG_ERR("NVS Mount failed: %d", rc);
        return;
    }

    // 读取数据
    rc = nvs_read(&fs, REBOOT_COUNTER_ID, &reboot_counter, sizeof(reboot_counter));
    if (rc > 0) {
        LOG_INF(">> SYSTEM REBOOTED! Current Count: %d &lt;&lt;", reboot_counter);
        reboot_counter++;
    } else {
        LOG_INF("&gt;> First Boot (or NVS empty). Setting Count to 1 <<");
        reboot_counter = 1;
    }

    // 写回数据
    rc = nvs_write(&fs, REBOOT_COUNTER_ID, &reboot_counter, sizeof(reboot_counter));
    if (rc < 0) {
        LOG_ERR("Failed to write to NVS");
    }
}

// ================= 按键回调 =================
static void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    if (pins & BIT(button.pin)) {
        LOG_WRN("!!! Button Pressed: Simulating Firmware FREEZE !!!");
        simulate_hang = true;
    }
}

// ================= 主函数 =================
int main(void)
{
    int err;

    // 1. 系统启动，先读写 Flash
    init_nvs_and_count();

    // 2. 初始化LED
    if (!device_is_ready(led1.port)) {
        LOG_ERR("LED1 device not ready");
        return 0;
    }
    if (!device_is_ready(led2.port)) {
        LOG_ERR("LED2 device not ready");
        return 0;
    }

    err = gpio_pin_configure_dt(&led1, GPIO_OUTPUT_ACTIVE);
    if (err < 0) {
        LOG_ERR("Failed to configure LED1");
        return 0;
    }

    err = gpio_pin_configure_dt(&led2, GPIO_OUTPUT_INACTIVE);
    if (err < 0) {
        LOG_ERR("Failed to configure LED2");
        return 0;
    }

    // 初始化按键
    if (!device_is_ready(button.port)) {
        LOG_ERR("Button device not ready");
        return 0;
    }

    err = gpio_pin_configure_dt(&button, GPIO_INPUT);
    if (err < 0) {
        LOG_ERR("Failed to configure button");
        return 0;
    }

    err = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
    if (err < 0) {
        LOG_ERR("Failed to configure button interrupt");
        return 0;
    }

    gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
    err = gpio_add_callback(button.port, &button_cb_data);
    if (err < 0) {
        LOG_ERR("Failed to add button callback");
        return 0;
    }

    // 3. 初始化 Task Watchdog
    const struct device *hw_wdt_dev = DEVICE_DT_GET(WDT_NODE);
    if (!device_is_ready(hw_wdt_dev)) {
        LOG_ERR("WDT hardware not ready");
        return 0;
    }

    err = task_wdt_init(hw_wdt_dev);
    if (err) {
        LOG_ERR("WDT init failed: %d", err);
        return 0;
    }

    // 注册一个通道，5000ms 超时
    wdt_channel_id = task_wdt_add(WDT_TIMEOUT_MS, NULL, NULL);
    if (wdt_channel_id < 0) {
        LOG_ERR("Could not add WDT channel");
        return 0;
    }

    LOG_INF("System Running. Press Button (P0.04) to freeze system.");

    // 4. 主循环
    while (1) {
        if (!simulate_hang) {
            // --- 正常状态 ---
            
            // 喂狗 (告诉系统我还活着)
            task_wdt_feed(wdt_channel_id);
            
            // 翻转 LED1 (P0.02) 表示正在运行
            gpio_pin_set_dt(&led1, 1); 
            k_sleep(K_MSEC(100));
            gpio_pin_set_dt(&led1, 0);
            
            // 打印心跳（减少日志刷屏，每1秒打印一次即可）
            // k_sleep 已在上面用了100ms，这里再睡900ms
            k_sleep(K_MSEC(900)); 
            LOG_INF("Feeding Dog... (System Healthy)");
        
        } else {
            // --- 模拟死机状态 ---
            LOG_WRN("System Halted. Watchdog should trigger in 5 seconds...");
            
            // LED 常亮或常灭，不再闪烁
            gpio_pin_set_dt(&led1, 1);
            
            // 关键点：这里是一个死循环，且不调用 task_wdt_feed
            // 也可以简单的 while(1); 但为了能看到日志，我们只在这里通过 sleep 阻塞
            while(1) {
                // 不喂狗！
                k_sleep(K_SECONDS(1));
            }
        }
    }
    return 0;
}