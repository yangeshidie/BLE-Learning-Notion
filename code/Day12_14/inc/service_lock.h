#ifndef SERVICE_LOCK_H
#define SERVICE_LOCK_H

#include <stdint.h>
#include <zephyr/bluetooth/gatt.h>

int service_lock_init(void);

int service_lock_send_status(uint8_t status);

#endif
