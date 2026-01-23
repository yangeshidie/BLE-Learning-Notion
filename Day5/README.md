# Day 5: 异步通信 (Notify) 与 硬件中断集成

> **学习目标**：将设备从"被动响应者"升级为"主动发送者"。实现 Characteristic 的 Notify (通知) 属性，集成板载按键，使用 GPIO 中断捕获物理事件。

---

## 1. 硬件准备

| 组件 | 说明 |
|------|------|
| **MCU** | nRF52832 核心板 |
| **Debugger** | J-Link OB / V9 (SWD 接口) |
| **外设** | 按键 (接 P0.04) |
| **手机 APP** | nRF Connect (用于测试 Notify) |

---

## 2. 学习目标

本章节的目标是将设备从"被动响应者"升级为"主动发送者"。

**核心任务**：
- **BLE 层面**: 实现 Characteristic 的 **Notify** (通知) 属性，允许设备在数据变化时主动推送给手机，而无需手机轮询 (Polling)
- **硬件层面**: 集成板载按键，使用 GPIO 中断 (Interrupt) 捕获物理事件
- **逻辑层面**: 将硬件中断与 BLE 发送逻辑绑定，实现"按下按键 -> 手机收到数据"的闭环

---

## 3. 核心概念解析

### 3.1 为什么要用 Notify 而不是 Read？

- **省电**: 如果手机每 200ms 来读一次数据（轮询），双方都要不断唤醒射频，功耗极高
- **实时性**: Notify 由设备端发起，数据一变化立即发送，延迟最低
- **CCCD (Client Characteristic Configuration Descriptor)**:
  - BLE 规范要求，服务器不能随意给客户端发数据
  - 必须由客户端（手机）写入 CCCD (Handle `0x2902`)，值设为 `0x0001`，设备才被允许发送 Notify
  - 因此，开发时**必须**在特征值后挂载 `BT_GATT_CCC`

### 3.2 硬件中断 (Interrupt)

为了实时响应按键，我们不能在 `main` 循环里 `while(1)` 轮询引脚电平（浪费 CPU）。

配置 GPIO 中断后，只有按键按下那一瞬间，CPU 才会介入处理。

---

## 4. 实现步骤详解

### 步骤 1: 修改设备树 (Device Tree)

我们需要定义按键的硬件连接方式。

**文件**: `nrf52dk_nrf52832.overlay`

```dts
/ {
    buttons {
        compatible = "gpio-keys";
        button_custom: button_0 {
            gpios = <&gpio0 4 (GPIO_ACTIVE_HIGH | GPIO_PULL_DOWN)>;
            label = "User Button";
        };
    };
};
```

**关键点**:
- **P0.04**: 按键接在 P0.04 引脚
- **GPIO_ACTIVE_HIGH**: 按下为高电平
- **GPIO_PULL_DOWN**: 启用内部下拉电阻，防止松开时引脚悬空导致信号抖动

### 步骤 2: 定义 GATT 服务与 CCCD

**文件**: `my_service.c`

我们需要一个只读（对于手机来说不可写）、支持通知的特征值。

**API**: `BT_GATT_CCC(callback, permissions)`

Zephyr 提供的宏，自动处理标准的 CCCD 逻辑。

- `callback`: 当手机开启/关闭通知时触发，用于记录日志或改变设备状态

```c
BT_GATT_SERVICE_DEFINE(my_service,
    BT_GATT_PRIMARY_SERVICE(MY_SERVICE_UUID),
    
    BT_GATT_CHARACTERISTIC(MY_CHAR_RW_UUID,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                           on_read, on_write, my_value),
    
    BT_GATT_CHARACTERISTIC(MY_CHAR_NOTIFY_UUID,
                           BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_NONE,
                           NULL, NULL, NULL),
    
    BT_GATT_CCC(on_cccd_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);
```

**关键点**:
- **Notify 特征值**: 属性为 `BT_GATT_CHRC_NOTIFY`，权限为 `BT_GATT_PERM_NONE`（手机不能读写）
- **CCCD 描述符**: 必须紧跟在 Notify 特征值后面，权限为 `BT_GATT_PERM_READ | BT_GATT_PERM_WRITE`

### 步骤 3: 实现"智能"发送函数

**文件**: `my_service.c`

在早期的尝试中，我们使用硬编码索引（`attrs[4]`）来获取特征值，这导致了后续添加属性时的 bug。**最佳实践是使用 UUID 动态查找 Attribute**。

```c
int my_service_send_button_notify(struct bt_conn *conn, uint8_t button_state)
{
    const struct bt_gatt_attr *attr = NULL;

    for (size_t i = 0; i < my_service.attr_count; i++) {
        if (!bt_uuid_cmp(my_service.attrs[i].uuid, MY_CHAR_NOTIFY_UUID)) {
            attr = &my_service.attrs[i];
            break;
        }
    }

    if (!attr) {
        return -ENOENT;
    }

    const struct bt_gatt_attr *char_decl_attr = attr - 1;

    if (bt_gatt_is_subscribed(conn, char_decl_attr, BT_GATT_CCC_NOTIFY)) {
        return bt_gatt_notify(conn, char_decl_attr, &button_state, sizeof(button_state));
    } else {
        return -EACCES;
    }
}
```

**关键点**:
- **UUID 查找**: 遍历 GATT 表，通过 UUID 找到特征值属性值
- **特征值声明**: `char_decl_attr = attr - 1`，向前回退一个位置获取特征值声明
- **订阅检查**: 使用 `bt_gatt_is_subscribed` 检查 CCCD 是否已使能
- **发送通知**: 使用 `bt_gatt_notify` 发送数据，传入特征值声明而非属性值

### 步骤 4: 硬件与蓝牙的结合

**文件**: `main.c`

在 `main.c` 中配置 GPIO 中断，并在中断回调（ISR）中调用发送函数。

```c
void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    static int64_t last_time = 0;
    int64_t now = k_uptime_get();
    
    if (now - last_time < 200) {
        return;
    }
    last_time = now;

    app_button_count++;
    
    my_service_send_button_notify(current_conn, app_button_count);
}
```

**关键点**:
- **去抖动**: 使用时间间隔过滤，防止机械按键抖动导致多次触发
- **连接句柄**: 使用 `current_conn` 确保检查的是当前连接的订阅状态

### 步骤 5: 连接跟踪

**文件**: `main.c`

为了确保 `bt_gatt_is_subscribed` 能正确检查订阅状态，我们需要跟踪当前活动的连接。

```c
static struct bt_conn *current_conn = NULL;

static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        LOG_ERR("Connection failed (err %u)", err);
    } else {
        LOG_INF("Connected");
        current_conn = conn;
    }
}

static void disconnected(struct bt_conn *conn, uint8_t err)
{
    LOG_INF("Disconnected (err %u)", err);
    if (current_conn == conn) {
        current_conn = NULL;
    }
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};
```

**关键点**:
- **引用计数**: 在 `connected` 回调中保存连接指针，在 `disconnected` 中清空
- **状态管理**: 确保发送时使用的是有效的连接句柄

---

## 5. 关键 API 参考

| API | 功能 | 使用场景 |
|-----|------|----------|
| `BT_GATT_CCC` | 声明客户端配置描述符 | 必须紧跟在 `NOTIFY` 或 `INDICATE` 属性的特征值后面 |
| `bt_gatt_notify` | 发送通知数据 | 当设备端数据发生变化，需要主动推给手机时使用 |
| `bt_gatt_is_subscribed` | 检查订阅状态 | 在发送前调用。既是为了省电，也是为了遵守 BLE 规范 |
| `bt_uuid_cmp` | 比较两个 UUID | 用于在 GATT 表中动态查找特定的特征值 |
| `gpio_pin_interrupt_configure_dt` | 配置 GPIO 中断 | 设置触发条件（如 `GPIO_INT_EDGE_TO_ACTIVE` 按下触发） |
| `gpio_init_callback` | 初始化 GPIO 回调结构体 | 绑定回调函数和引脚掩码 |
| `gpio_add_callback` | 添加 GPIO 回调 | 将回调注册到 GPIO 驱动中 |
| `k_uptime_get` | 获取系统运行时间 | 用于去抖动处理 |

---

## 6. 踩坑与经验总结

### 坑 1: 索引陷阱

**问题**: 使用 `&my_service.attrs[4]` 硬编码索引获取特征值

**后果**: 当修改 GATT 表（如增加描述符）或 UUID 定义冲突时，索引指向错误位置，导致 `bt_gatt_is_subscribed` 永远返回 false

**解决**: **永远使用 UUID 遍历查找 Attribute**，不要依赖固定索引

### 坑 2: UUID 冲突

**问题**: 复制粘贴代码时，Notify 特征值和 Read/Write 特征值使用了完全相同的 UUID

**后果**: 查找逻辑总是找到第一个特征值（RW），而不是 Notify 特征值，导致状态检查错误

**解决**: 确保每个特征值有唯一的 UUID（例如修改最后一位字节）

### 坑 3: 连接句柄 (Connection Handle)

**问题**: `bt_gatt_is_subscribed(NULL, ...)` 在某些情况下行为不符合预期

**解决**: 在 `main.c` 中通过 `connected` 回调保存 `struct bt_conn *` 指针，并在调用 GATT 函数时显式传入该指针。这能确保协议栈检查的是**当前特定连接**的订阅状态

### 坑 4: 特征值声明 vs 特征值属性值

**问题**: 传入 `bt_gatt_is_subscribed` 和 `bt_gatt_notify` 的是特征值属性值，而不是特征值声明

**后果**: CCCD 是绑定在特征值声明上的，传入属性值会导致订阅检查失败

**解决**: 找到特征值属性值后，向前回退一个位置获取特征值声明：`char_decl_attr = attr - 1`

### 坑 5: 按键悬空 (Floating Pin)

**问题**: 按键松开时触发无数次中断或乱码

**解决**: 在设备树中配置 `GPIO_PULL_DOWN`（对于 Active High 按键）或 `GPIO_PULL_UP`（对于 Active Low 按键）

---

## 7. 验收标准

- [ ] 手机 Enable Notify 后，RTT 显示 "Notifications enabled"
- [ ] 按下按键，RTT 显示 "Button pressed! Count: X"
- [ ] 按下按键，手机屏幕实时滚动收到数据
- [ ] 手机 Disable Notify 后，按下按键不再发送数据

---

## 8. 学习总结

Day 5 完成了从"被动响应"到"主动推送"的升级。通过 Notify 机制，我们掌握了：

1. **CCCD 机制**: 理解客户端配置描述符的作用和实现
2. **UUID 动态查找**: 避免硬编码索引，提高代码健壮性
3. **连接管理**: 正确跟踪和管理连接对象的生命周期
4. **硬件中断**: 集成 GPIO 中断，实现实时事件响应
5. **去抖动处理**: 使用时间间隔过滤机械按键抖动

**核心知识点**:
- **特征值声明 vs 特征值属性值**: 理解 GATT 表的结构，正确使用 `bt_gatt_is_subscribed` 和 `bt_gatt_notify`
- **订阅检查**: 在发送前必须检查 CCCD 是否已使能
- **连接句柄**: 使用有效的连接句柄确保协议栈能正确检查订阅状态

---

**Next Step**: Day 6 - NUS 透传与缓冲区管理
