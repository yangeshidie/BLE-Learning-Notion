#ifndef BLE_SETUP_H
#define BLE_SETUP_H

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>

typedef enum {
    ADV_MODE_FAST,
    ADV_MODE_SLOW
} adv_mode_t;

int ble_setup_init(void);

int ble_setup_start_advertising(adv_mode_t mode);

int ble_setup_stop_advertising(void);

void ble_setup_set_connection_led(bool connected);

#endif
