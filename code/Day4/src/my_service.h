#ifndef MY_SERVICE_H_
#define MY_SERVICE_H_

#include <zephyr/types.h>
#include <stddef.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

/**
 * @brief 自定义服务 UUID
 * UUID String: d5a6e878-df0c-442d-83b6-200384e51921
 */
#define MY_SERVICE_UUID_VAL \
	BT_UUID_128_ENCODE(0xd5a6e878, 0xdf0c, 0x442d, 0x83b6, 0x200384e51921)

/**
 * @brief 自定义特征值 UUID (Read/Write)
 * UUID String: d5a6e879-df0c-442d-83b6-200384e51921
 * 注意：通常我们会让 Characteristic 的 UUID 与 Service 只差 1 个 bit 或 byte，方便辨识
 */
#define MY_CHAR_UUID_VAL \
	BT_UUID_128_ENCODE(0xd5a6e879, 0xdf0c, 0x442d, 0x83b6, 0x200384e51921)

// 将宏封装成 Zephyr 的 UUID 结构体指针，方便后续调用
#define MY_SERVICE_UUID BT_UUID_DECLARE_128(MY_SERVICE_UUID_VAL)
#define MY_CHAR_UUID    BT_UUID_DECLARE_128(MY_CHAR_UUID_VAL)

/**
 * @brief 初始化服务的函数声明
 * 我们将在 main.c 中调用它
 */
int my_service_init(void);

#endif /* MY_SERVICE_H_ */