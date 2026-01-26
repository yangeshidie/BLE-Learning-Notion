#include "my_service.h"
#include <zephyr/logging/log.h>

// 注册日志模块，名称为 "my_srv"
LOG_MODULE_REGISTER(my_srv, LOG_LEVEL_INF);

// 1. 定义特征值的数据缓冲区 (例如 64 字节)
static uint8_t my_value[64] = {0x11, 0x22, 0x33, 0x44}; // 给个初始值，方便 Read 测试

// 2. 实现 Write 回调函数
// 当手机向 Characteristic 写入数据时，协议栈会调用此函数
static ssize_t on_write(struct bt_conn *conn,
                        const struct bt_gatt_attr *attr,
                        const void *buf,
                        uint16_t len,
                        uint16_t offset,
                        uint8_t flags)
{
    // attr->user_data 指向的是我们在定义服务时绑定的变量 (即 my_value)
    uint8_t *value = attr->user_data;

    // 检查写入长度是否越界
    if (offset + len > sizeof(my_value)) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    // 将数据复制到我们的缓冲区
    memcpy(value + offset, buf, len);

    // 打印接收到的数据 (Hex 格式)
    LOG_INF("Data written: len=%d, data=", len);
    LOG_HEXDUMP_INF(buf, len, "Payload");

    // 返回实际写入的长度
    return len;
}

// 自定义 Read 回调
static ssize_t on_read(struct bt_conn *conn,
                       const struct bt_gatt_attr *attr,
                       void *buf,
                       uint16_t len,
                       uint16_t offset)
{
    const uint8_t *value = attr->user_data;

    LOG_INF("Read request received");

    // 这里必须调用 bt_gatt_attr_read (带 attr 字样)
    // 它的作用是帮你处理 buf 的边界检查、offset 偏移和数据复制
    return bt_gatt_attr_read(conn, attr, buf, len, offset, value, sizeof(my_value));
}
// 3. 定义 GATT 服务
// 这是一个宏，会在编译时静态创建一个 GATT 服务结构体
BT_GATT_SERVICE_DEFINE(my_service,
    // (1) 服务声明：主服务 (Primary Service)
    BT_GATT_PRIMARY_SERVICE(MY_SERVICE_UUID),

    // (2) 特征值定义
    // 参数1: 特征值的 UUID
    // 参数2: 属性 (Properties) - 告诉手机这个特征支持 Read 和 Write
    // 参数3: 权限 (Permissions) - 告诉协议栈，读写是否需要加密/认证 (这里设为加密)
    // 参数4: Read 回调函数 - 使用系统默认的 bt_gatt_attr_read，它会直接读取 user_data
    // 参数5: Write 回调函数 - 使用我们自定义的 on_write
    // 参数6: user_data - 绑定的数据缓冲区
    BT_GATT_CHARACTERISTIC(MY_CHAR_UUID,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT,
                           on_read, on_write, my_value),
);

// 初始化函数 (目前 GATT 是静态定义的，这个函数可以留空，或者做一些额外逻辑)
int my_service_init(void)
{
    return 0;
}