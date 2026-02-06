#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/services/bas.h> // 标准电池服务接口

#include "app_battery.h"

LOG_MODULE_REGISTER(app_battery, LOG_LEVEL_INF);

/* ----------------配置参数---------------- */
#define BATTERY_MEASURE_INTERVAL_MS 10000 // 每10秒检测一次 (测试用，实际产品可设为几分钟)

/* 
 * 电池电压范围假设：
 * 模拟最大值 3.0V = 100%
 * 模拟最小值 2.0V = 0% 
 * (根据你的电位器调节范围调整)
 */
#define BATTERY_VOLTAGE_MAX_MV  3000
#define BATTERY_VOLTAGE_MIN_MV  2000

/* ----------------硬件节点获取---------------- */
// 获取我们在 app.overlay 中定义的 zephyr,user -> io-channels
static const struct adc_dt_spec adc_channel = ADC_DT_SPEC_GET(DT_PATH(zephyr_user));

/* ----------------变量定义---------------- */
static struct k_work_delayable battery_work;
static int16_t adc_buffer[1]; // 存放采样结果

/* ----------------ADC 采样与转换逻辑---------------- */

static void battery_sample_handler(struct k_work *work)
{
    int err;
    int32_t val_mv;
    uint8_t battery_level;

    // 1. 启动采样
    struct adc_sequence sequence = {
        .buffer = adc_buffer,
        .buffer_size = sizeof(adc_buffer),
        // 分辨率通常 10-bit 或 12-bit，这里由 DT 配置决定，默认通常是 12
    };
    
    // 必须手动把 DT 中的参数填入 sequence
    adc_sequence_init_dt(&adc_channel, &sequence);

    err = adc_read(adc_channel.dev, &sequence);
    if (err) {
        LOG_ERR("ADC read failed (err %d)", err);
        goto reschedule;
    }

    // 2. 转换为毫伏 (mV)
    val_mv = adc_buffer[0];
    err = adc_raw_to_millivolts_dt(&adc_channel, &val_mv);
    if (err) {
        LOG_ERR("ADC convert failed (err %d)", err);
        goto reschedule;
    }

    LOG_INF("ADC Voltage: %d mV", val_mv);

    // 3. 计算百分比 (简单的线性映射)
    if (val_mv >= BATTERY_VOLTAGE_MAX_MV) {
        battery_level = 100;
    } else if (val_mv <= BATTERY_VOLTAGE_MIN_MV) {
        battery_level = 0;
    } else {
        battery_level = (uint8_t)((val_mv - BATTERY_VOLTAGE_MIN_MV) * 100 / 
                                  (BATTERY_VOLTAGE_MAX_MV - BATTERY_VOLTAGE_MIN_MV));
    }

    // 4. 更新 BLE Battery Service
    // bt_bas_set_battery_level 是 Zephyr 内置函数
    // 它会自动检查是否连接、是否开启 Notify，然后推送数据
    bt_bas_set_battery_level(battery_level);
    LOG_INF("Reported Battery Level: %d%%", battery_level);

reschedule:
    // 5. 重新调度下一次采样
    k_work_reschedule(&battery_work, K_MSEC(BATTERY_MEASURE_INTERVAL_MS));
}

/* ----------------初始化---------------- */

int app_battery_init(void)
{
    int err;

    // 1. 检查设备是否就绪
    if (!adc_is_ready_dt(&adc_channel)) {
        LOG_ERR("ADC controller not ready");
        return -ENODEV;
    }

    // 2. 配置通道 (Channel Setup)
    // 根据 overlay 中的配置应用到硬件
    err = adc_channel_setup_dt(&adc_channel);
    if (err) {
        LOG_ERR("ADC setup failed (err %d)", err);
        return err;
    }

    // 3. 初始化定时任务
    k_work_init_delayable(&battery_work, battery_sample_handler);

    // 4. 立即启动第一次采样
    k_work_reschedule(&battery_work, K_NO_WAIT);

    LOG_INF("Battery Monitor Initialized");
    return 0;
}