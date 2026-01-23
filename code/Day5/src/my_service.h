#ifndef MY_SERVICE_H_
#define MY_SERVICE_H_

#include <zephyr/types.h>
#include <stddef.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

/**
 * @brief 自定义服务 UUID
 * UUID: d5a6e878-df0c-442d-83b6-200384e51921
 */
#define MY_SERVICE_UUID_VAL \
    BT_UUID_128_ENCODE(0xd5a6e878, 0xdf0c, 0x442d, 0x83b6, 0x200384e51921)

/**
 * @brief 特征值 1: Read/Write
 * UUID: ...e879...
 */
#define MY_CHAR_RW_UUID_VAL \
    BT_UUID_128_ENCODE(0xd5a6e879, 0xdf0c, 0x442d, 0x83b6, 0x200384e51921)

/**
 * @brief [Day 5 新增] 特征值 2: Notify (Button State)
 * UUID: ...e87a... (最后一位变为 a)
 */
#define MY_CHAR_NOTIFY_UUID_VAL \
    BT_UUID_128_ENCODE(0xd5a6e87a, 0xdf0c, 0x442d, 0x83b6, 0x200384e51921)

#define MY_SERVICE_UUID      BT_UUID_DECLARE_128(MY_SERVICE_UUID_VAL)
#define MY_CHAR_RW_UUID      BT_UUID_DECLARE_128(MY_CHAR_RW_UUID_VAL)
#define MY_CHAR_NOTIFY_UUID  BT_UUID_DECLARE_128(MY_CHAR_NOTIFY_UUID_VAL)

/**
 * @brief 初始化服务的函数声明
 */
int my_service_init(void);

/**
 * @brief [Day 5 新增] 发送按键状态通知
 * 
 * @param conn 指向连接对象的指针 (如果传 NULL，则发给所有已订阅的客户端)
 * @param button_state 按键计数值或状态
 * @return 0 成功, 负数表示错误码
 */
int my_service_send_button_notify(struct bt_conn *conn, uint8_t button_state);

#endif /* MY_SERVICE_H_ */