#ifndef BLE_SETUP_H
#define BLE_SETUP_H

#include <stdbool.h>

/**
 * @brief 初始化蓝牙协议栈，配置回调，并启动初始广播
 * @return 0 表示成功，负数表示错误码
 */
int ble_setup_init(void);

/**
 * @brief 强制启动快速广播 (Fast Advertising)
 * 
 * 用于按键唤醒场景。如果当前已连接，此函数无效。
 * 快速广播持续一段时间后会自动切换回慢速广播。
 */
void ble_setup_start_fast_adv(void);

/**
 * @brief 获取当前蓝牙连接状态
 */
bool ble_is_connected(void);

#endif // BLE_SETUP_H