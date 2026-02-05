#include "app_battery.h"
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <zephyr/bluetooth/services/bas.h>

LOG_MODULE_REGISTER(app_battery, LOG_LEVEL_INF);

#define BATTERY_ADC_CHANNEL DT_IO_CHANNELS_LABEL_BY_IDX(DT_PATH(zephyr_user), 0)
#define BATTERY_ADC_CHANNEL_ID DT_IO_CHANNELS_CHANNEL_BY_IDX(DT_PATH(zephyr_user), 0)

#define ADC_RESOLUTION 10
#define ADC_GAIN ADC_GAIN_1_6
#define ADC_REFERENCE ADC_REF_VDD_1_4
#define ADC_ACQUISITION_TIME ADC_ACQ_TIME(ADC_ACQ_TIME_MICROSECONDS, 40)

#define BATTERY_VOLTAGE_MAX 4200
#define BATTERY_VOLTAGE_MIN 3300

static const struct device *adc_dev = DEVICE_DT_GET(DT_NODELABEL(adc));
static const struct adc_channel_cfg channel_cfg = {
    .gain = ADC_GAIN,
    .reference = ADC_REFERENCE,
    .acquisition_time = ADC_ACQUISITION_TIME,
    .channel_id = BATTERY_ADC_CHANNEL_ID,
    .differential = 0,
};

static int16_t m_sample_buffer[1];
static uint8_t battery_level = 100;

static struct k_timer battery_timer;

static void battery_timer_handler(struct k_timer *timer_id)
{
    app_battery_update();
}

K_TIMER_DEFINE(battery_timer, battery_timer_handler, NULL);

static int battery_sample(void)
{
    int err;
    struct adc_sequence sequence = {
        .buffer = m_sample_buffer,
        .buffer_size = sizeof(m_sample_buffer),
        .resolution = ADC_RESOLUTION,
    };

    if (!device_is_ready(adc_dev)) {
        LOG_ERR("ADC device not ready");
        return -ENODEV;
    }

    err = adc_channel_setup_dt(ADC_DT_SPEC_GET(DT_IO_CHANNELS_CTLR_BY_IDX(DT_PATH(zephyr_user), 0)));
    if (err) {
        LOG_ERR("Failed to setup ADC channel (err %d)", err);
        return err;
    }

    (void)adc_sequence_init_dt(ADC_DT_SPEC_GET(DT_IO_CHANNELS_CTLR_BY_IDX(DT_PATH(zephyr_user), 0)),
                                &sequence);

    err = adc_read(adc_dev, &sequence);
    if (err) {
        LOG_ERR("Failed to read ADC (err %d)", err);
        return err;
    }

    return 0;
}

static int32_t battery_voltage_calc(int32_t raw_value)
{
    int32_t vref_mv = 3600;
    int32_t voltage_mv;

    voltage_mv = raw_value * vref_mv / (1 << ADC_RESOLUTION);
    voltage_mv = voltage_mv * 6;

    return voltage_mv;
}

static uint8_t battery_level_calc(int32_t voltage_mv)
{
    uint8_t level;

    if (voltage_mv >= BATTERY_VOLTAGE_MAX) {
        level = 100;
    } else if (voltage_mv <= BATTERY_VOLTAGE_MIN) {
        level = 0;
    } else {
        level = (voltage_mv - BATTERY_VOLTAGE_MIN) * 100 /
                (BATTERY_VOLTAGE_MAX - BATTERY_VOLTAGE_MIN);
    }

    return level;
}

int app_battery_init(void)
{
    int err;

    if (!device_is_ready(adc_dev)) {
        LOG_ERR("ADC device not ready");
        return -ENODEV;
    }

    err = adc_channel_setup_dt(ADC_DT_SPEC_GET(DT_IO_CHANNELS_CTLR_BY_IDX(DT_PATH(zephyr_user), 0)));
    if (err) {
        LOG_ERR("Failed to setup ADC channel (err %d)", err);
        return err;
    }

    LOG_INF("Battery module initialized");

    k_timer_start(&battery_timer, K_SECONDS(5), K_SECONDS(5));

    return 0;
}

uint8_t app_battery_get_level(void)
{
    return battery_level;
}

void app_battery_update(void)
{
    int err;
    int32_t voltage_mv;
    uint8_t new_level;

    err = battery_sample();
    if (err) {
        LOG_ERR("Failed to sample battery (err %d)", err);
        return;
    }

    voltage_mv = battery_voltage_calc(m_sample_buffer[0]);
    new_level = battery_level_calc(voltage_mv);

    if (new_level != battery_level) {
        battery_level = new_level;
        LOG_INF("Battery level: %d%% (%d mV)", battery_level, voltage_mv);
        bt_bas_set_battery_level(battery_level);
    }
}
