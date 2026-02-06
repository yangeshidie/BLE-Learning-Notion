#ifndef APP_LOCK_H
#define APP_LOCK_H

/**
 * @brief 初始化锁的硬件（GPIO, 中断, WorkQueue）
 */
int app_lock_init(void);

/**
 * @brief 执行开锁动作
 * 
 * 逻辑：
 * 1. 点亮电磁铁 LED
 * 2. 更新 BLE 状态为 "Unlocked"
 * 3. 启动 3秒 定时器
 * 4. 定时结束后自动熄灭 LED 并更新状态为 "Locked"
 */
void app_lock_open(void);

#endif // APP_LOCK_H