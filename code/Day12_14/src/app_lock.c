#include "app_lock.h"
#include "ble_setup.h"
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(app_lock, LOG_LEVEL_INF);

static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

static lock_state_t current_state = LOCK_STATE_LOCKED;
static struct gpio_callback button_cb_data;

static struct k_work_delayable lock_action_work;

static void lock_action_work_handler(struct k_work *work)
{
    int err;
    lock_action_t *action = k_work_delayable_from_work(work);

    if (*action == LOCK_ACTION_UNLOCK) {
        err = gpio_pin_set_dt(&led1, 1);
        if (err) {
            LOG_ERR("Failed to unlock (err %d)", err);
            return;
        }
        current_state = LOCK_STATE_UNLOCKED;
        LOG_INF("Lock UNLOCKED");
    } else {
        err = gpio_pin_set_dt(&led1, 0);
        if (err) {
            LOG_ERR("Failed to lock (err %d)", err);
            return;
        }
        current_state = LOCK_STATE_LOCKED;
        LOG_INF("Lock LOCKED");
    }
}

K_WORK_DELAYABLE_DEFINE(lock_action_work, lock_action_work_handler);

static void button_pressed(const struct device *dev, struct gpio_callback *cb,
                            uint32_t pins)
{
    LOG_INF("Button pressed - triggering fast advertising");
    app_lock_trigger_fast_advertising();
}

int app_lock_init(void)
{
    int err;

    if (!gpio_is_ready_dt(&led1)) {
        LOG_ERR("LED1 device not ready");
        return -ENODEV;
    }

    err = gpio_pin_configure_dt(&led1, GPIO_OUTPUT_INACTIVE);
    if (err) {
        LOG_ERR("Failed to configure LED1 (err %d)", err);
        return err;
    }

    if (!gpio_is_ready_dt(&button)) {
        LOG_ERR("Button device not ready");
        return -ENODEV;
    }

    err = gpio_pin_configure_dt(&button, GPIO_INPUT);
    if (err) {
        LOG_ERR("Failed to configure button (err %d)", err);
        return err;
    }

    err = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
    if (err) {
        LOG_ERR("Failed to configure button interrupt (err %d)", err);
        return err;
    }

    gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
    err = gpio_add_callback_dt(&button, &button_cb_data);
    if (err) {
        LOG_ERR("Failed to add button callback (err %d)", err);
        return err;
    }

    LOG_INF("Lock module initialized");

    return 0;
}

void app_lock_execute_action(lock_action_t action)
{
    static lock_action_t action_storage;

    action_storage = action;
    k_work_schedule(&lock_action_work, K_MSEC(100));
}

lock_state_t app_lock_get_state(void)
{
    return current_state;
}

void app_lock_trigger_fast_advertising(void)
{
    int err = ble_setup_start_advertising(ADV_MODE_FAST);
    if (err) {
        LOG_ERR("Failed to start fast advertising (err %d)", err);
    }
}
