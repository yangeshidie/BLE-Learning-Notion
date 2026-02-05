#include "app_lock.h"
#include "app_battery.h"
#include "ble_setup.h"
#include "service_lock.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void)
{
    int err;

    LOG_INF("Starting Smart Lock Controller...");

    err = app_lock_init();
    if (err) {
        LOG_ERR("Failed to initialize lock module (err %d)", err);
        return err;
    }

    err = app_battery_init();
    if (err) {
        LOG_ERR("Failed to initialize battery module (err %d)", err);
        return err;
    }

    err = ble_setup_init();
    if (err) {
        LOG_ERR("Failed to initialize BLE module (err %d)", err);
        return err;
    }

    err = service_lock_init();
    if (err) {
        LOG_ERR("Failed to initialize lock service (err %d)", err);
        return err;
    }

    err = ble_setup_start_advertising(ADV_MODE_SLOW);
    if (err) {
        LOG_ERR("Failed to start advertising (err %d)", err);
        return err;
    }

    LOG_INF("Smart Lock Controller started successfully");

    while (1) {
        k_sleep(K_SECONDS(1));
    }

    return 0;
}
