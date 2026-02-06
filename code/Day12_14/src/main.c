#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "app_lock.h"
#include "ble_setup.h"
// #include "app_battery.h" // 待实现
LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);
int main(void)
{
    LOG_INF("Starting SmartLock Demo...");

    // 1. 关键：必须先初始化硬件和 WorkQueue
    // 如果这行没跑，k_work 没初始化，后面一调就崩
    int err = app_lock_init();
    if (err) {
        LOG_ERR("Failed to init hardware: %d", err);
        return 0;
    }

    err = app_battery_init();
    if (err) {
        LOG_ERR("Failed to init battery: %d", err);
        return 0;
    }
    // 2. 硬件就绪后，再启动蓝牙
    err = ble_setup_init();
    if (err) {
        LOG_ERR("Failed to init BLE: %d", err);
        return 0;
    }

    LOG_INF("System Boot Complete.");

    // 主线程可以休眠，RTOS 会接管
    return 0;
}