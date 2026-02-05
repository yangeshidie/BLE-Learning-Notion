#ifndef APP_LOCK_H
#define APP_LOCK_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    LOCK_STATE_LOCKED,
    LOCK_STATE_UNLOCKED
} lock_state_t;

typedef enum {
    LOCK_ACTION_UNLOCK,
    LOCK_ACTION_LOCK
} lock_action_t;

int app_lock_init(void);

void app_lock_execute_action(lock_action_t action);

lock_state_t app_lock_get_state(void);

void app_lock_trigger_fast_advertising(void);

#endif
