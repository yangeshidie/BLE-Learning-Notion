#include "my_service.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(my_srv, LOG_LEVEL_INF);

static uint8_t my_value[64] = {0x11, 0x22, 0x33, 0x44};

static ssize_t on_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                        const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
    uint8_t *value = attr->user_data;
    if (offset + len > sizeof(my_value)) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }
    memcpy(value + offset, buf, len);
    LOG_INF("Data written: len=%d", len);
    return len;
}

static ssize_t on_read(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                       void *buf, uint16_t len, uint16_t offset)
{
    const uint8_t *value = attr->user_data;
    return bt_gatt_attr_read(conn, attr, buf, len, offset, value, sizeof(my_value));
}

// --- [Day 5 新增] CCCD 配置回调 ---
/* 
 * 当手机开启或关闭 Notify 时，协议栈自动调用此函数。
 * value 参数:
 *   BT_GATT_CCC_NOTIFY (1) - 开启通知
 *   0                      - 关闭通知
 */
static void on_cccd_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    bool notif_enabled = (value == BT_GATT_CCC_NOTIFY);
    LOG_INF("Notifications %s", notif_enabled ? "enabled" : "disabled");
}

// --- GATT 服务定义 ---
BT_GATT_SERVICE_DEFINE(my_service,
    // [Index 0] 主服务声明
    BT_GATT_PRIMARY_SERVICE(MY_SERVICE_UUID),

    // [Index 1] 特征值声明 (Read/Write)
    // [Index 2] 特征值属性值 (存放 my_value)
    BT_GATT_CHARACTERISTIC(MY_CHAR_RW_UUID,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                           on_read, on_write, my_value),

    // --- [Day 5 新增] Notify 特征值 ---
    
    // [Index 3] 特征值声明 (Notify)
    // [Index 4] 特征值属性值 (这里不需要 user_data 绑定变量，因为我们是主动推数据)
    // 属性: ONLY NOTIFY
    // 权限: NONE (因为手机不能读也不能写这个值，只能被动接收)
    BT_GATT_CHARACTERISTIC(MY_CHAR_NOTIFY_UUID,
                           BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_NONE,
                           NULL, NULL, NULL),

    // [Index 5] CCCD (客户端配置描述符)
    // 手机必须能读写这个描述符才能开启通知，所以需要 READ | WRITE 权限
    BT_GATT_CCC(on_cccd_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

// --- 初始化函数 ---
int my_service_init(void)
{
    return 0;
}
// --- [Day 5 新增] 发送通知实现 ---
// 临时替换 my_service.c 中的发送函数
int my_service_send_button_notify(struct bt_conn *conn, uint8_t button_state)
{
    const struct bt_gatt_attr *target_attr = NULL;

    for (size_t i = 0; i < my_service.attr_count; i++) {
        const struct bt_gatt_attr *attr = &my_service.attrs[i];
        
        if (!bt_uuid_cmp(attr->uuid, MY_CHAR_NOTIFY_UUID)) {
            target_attr = attr;
            break;
        }
    }

    if (!target_attr) {
        LOG_ERR("Target Attribute NOT found!");
        return -ENOENT;
    }

    const struct bt_gatt_attr *char_decl_attr = target_attr - 1;

    bool is_subscribed = bt_gatt_is_subscribed(conn, char_decl_attr, BT_GATT_CCC_NOTIFY);
    
    if (is_subscribed) {
        LOG_INF(">>> Subscribed! Sending data...");
        return bt_gatt_notify(conn, char_decl_attr, &button_state, sizeof(button_state));
    } else {
        LOG_WRN(">>> Not subscribed (CCCD=0)");
        return -EACCES;
    }
}
