#ifndef APP_BATTERY_H
#define APP_BATTERY_H

/**
 * @brief 初始化电池采样模块
 * 
 * 启动一个定时任务，每隔一段时间读取 ADC 并更新 BLE 电量
 */
int app_battery_init(void);

#endif // APP_BATTERY_H