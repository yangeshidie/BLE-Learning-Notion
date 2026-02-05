#ifndef APP_BATTERY_H
#define APP_BATTERY_H

#include <stdint.h>

int app_battery_init(void);

uint8_t app_battery_get_level(void);

void app_battery_update(void);

#endif
