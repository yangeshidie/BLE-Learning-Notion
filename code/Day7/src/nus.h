/*
 * Copyright (c) 2024 Learning BLE
 * Module: NUS (Nordic UART Service)
 * Description: 手动实现 NUS 服务头文件，用于声明 API 和 UUID
 */

#ifndef NUS_H_
#define NUS_H_

#include <zephyr/types.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

/** @brief NUS Service UUID: 6E400001-B5A3-F393-E0A9-E50E24DCCA9E */
#define MY_NUS_UUID_SERVICE_VAL \
    BT_UUID_128_ENCODE(0x6E400001, 0xB5A3, 0xF393, 0xE0A9, 0xE50E24DCCA9E)

/** @brief NUS RX Characteristic UUID (Write): ...0002... */
#define MY_NUS_UUID_RX_VAL \
    BT_UUID_128_ENCODE(0x6E400002, 0xB5A3, 0xF393, 0xE0A9, 0xE50E24DCCA9E)

/** @brief NUS TX Characteristic UUID (Notify): ...0003... */
#define MY_NUS_UUID_TX_VAL \
    BT_UUID_128_ENCODE(0x6E400003, 0xB5A3, 0xF393, 0xE0A9, 0xE50E24DCCA9E)

#define BT_UUID_MY_NUS_SERVICE  BT_UUID_DECLARE_128(MY_NUS_UUID_SERVICE_VAL)
#define BT_UUID_MY_NUS_RX       BT_UUID_DECLARE_128(MY_NUS_UUID_RX_VAL)
#define BT_UUID_MY_NUS_TX       BT_UUID_DECLARE_128(MY_NUS_UUID_TX_VAL)

/**
 * @brief 收到数据时的回调函数定义
 * @param conn 连接句柄
 * @param data 接收到的数据指针
 * @param len 数据长度
 */
typedef void (*my_nus_received_cb_t)(struct bt_conn *conn, const uint8_t *data, uint16_t len);

/**
 * @brief NUS 初始化配置结构体
 */
struct my_nus_cb {
    my_nus_received_cb_t received;  /**< 当手机发数据给设备时调用 */
    void (*send_enabled)(void);     /**< 当手机订阅了 TX 通知时调用 (可选) */
};

/**
 * @brief 初始化 NUS 服务
 * @param callbacks 回调函数结构体
 * @return 0 成功, 负数 失败
 */
int my_nus_init(struct my_nus_cb *callbacks);

/**
 * @brief 发送数据给手机 (通过 Notify)
 * @param conn 连接对象 (NULL 则广播给所有已连接且订阅的设备，通常 NUS 是一对一)
 * @param data 数据指针
 * @param len 数据长度
 * @return 0 成功, -ENOMEM/-EAGAIN 缓冲区满
 */
int my_nus_send(struct bt_conn *conn, const uint8_t *data, uint16_t len);

#endif /* NUS_H_ */