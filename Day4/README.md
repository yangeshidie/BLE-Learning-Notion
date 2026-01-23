# Day 4: 自定义 GATT 服务 (Custom GATT Service)

> **学习目标**：进入属性层（ATT/GATT），实现真正的数据交互。创建自定义服务，包含可读、可写的特征值，掌握模块化代码结构和 UUID 机制。

---

## 1. 硬件准备

| 组件 | 说明 |
|------|------|
| **MCU** | nRF52832 核心板 |
| **Debugger** | J-Link OB / V9 (SWD 接口) |
| **手机 APP** | nRF Connect (用于测试 GATT 服务) |

---

## 2. 学习目标

在完成了链路层（Link Layer）的控制后，Day 4 的目标是进入属性层（ATT/GATT），实现真正的数据交互。

**核心任务**：
- 创建一个自定义服务，包含一个可读、可写的特征值（Characteristic）
- 模块化代码结构（`.c/.h` 分离）
- 理解 UUID 机制，掌握 Read/Write 回调处理

**注**：编译和烧录时采用与 Day 3 相同的方法。

---

## 3. 核心概念：GATT 层级架构

GATT (Generic Attribute Profile) 定义了两个 BLE 设备之间如何交换数据。可以将其想象成一个"文件系统"：

### 3.1 Service (服务)

类似于"文件夹"。它是一组逻辑上相关的特征值的集合。

- **官方服务**: 如心率 (Heart Rate)、电量 (Battery Service)，使用 16-bit UUID
- **自定义服务**: 我们今天实现的，必须使用 **128-bit UUID**

### 3.2 Characteristic (特征值)

类似于"文件"。它是实际存放数据的容器。

它包含：
- **Value** (数据本身)
- **Properties** (权限：读/写/通知)
- **Descriptor** (描述符)

---

## 4. 关键技术点与 API 解析

### 4.1 128-bit UUID 的定义

在 Zephyr 中，UUID 使用宏进行定义。虽然 BLE 传输是小端序（Little Endian），但 Zephyr 的宏允许我们按人类可读的大端序书写。

**API / 宏**：
```c
#define BT_UUID_128_ENCODE(w32, w16, w16, w16, w48) ...
```

**作用**: 将 5 部分的 hex 值转换为内部的 UUID 数组格式

**示例**：
```c
BT_UUID_128_ENCODE(0xd5a6e878, 0xdf0c, 0x442d, 0x83b6, 0x200384e51921)
```

### 4.2 静态定义 GATT 服务

Zephyr 使用"宏魔法"在编译阶段静态分配内存，而不是在运行时动态创建。这极大提高了嵌入式系统的稳定性。

**API / 宏**: `BT_GATT_SERVICE_DEFINE`

```c
BT_GATT_SERVICE_DEFINE(svc_name, attributes...);
```

**参数**:
- **参数 1**: 服务名称（随意起，仅用于代码引用）
- **参数 2...**: 属性列表，通常包含 `BT_GATT_PRIMARY_SERVICE` 和 `BT_GATT_CHARACTERISTIC`

### 4.3 特征值定义与属性

```c
BT_GATT_CHARACTERISTIC(uuid, props, perms, read_cb, write_cb, user_data)
```

**参数说明**:
- **uuid**: 特征值的 ID
- **props (属性)**: 告诉手机这个特征支持什么操作（`BT_GATT_CHRC_READ`, `BT_GATT_CHRC_WRITE` 等）
- **perms (权限)**: 告诉协议栈操作是否需要加密/配对（`BT_GATT_PERM_READ`, `BT_GATT_PERM_WRITE`）
- **read_cb / write_cb**: 读写回调函数指针
- **user_data**: 绑定的上下文数据（通常指向我们的数据缓存数组）

### 4.4 广播自定义服务

为了让手机在扫描阶段（未连接时）识别出设备具备该服务，需要在广播包中包含 Service UUID。

**关键数据结构**：
```c
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, MY_SERVICE_UUID_VAL),
};
```

---

## 5. 踩坑与实战经验：Read 回调的陷阱

在 Day 4 的实战中，我们遇到了一个经典问题：**手机读取到的数据不完整或为空。**

### 5.1 问题根源

Zephyr 提供的默认回调函数 `bt_gatt_attr_read`（注意此处指默认行为）在某些简写宏中，默认将 `user_data` 视为 **C 字符串**，使用 `strlen()` 计算长度。

如果数据是二进制（如 `0x00, 0x01`），`strlen` 遇到 `0x00` 就会停止，导致截断。

### 5.2 解决方案：手写 `on_read`

对于二进制数据或定长数组，必须实现自定义 Read 回调，并显式指定长度。

**正确实现**：
```c
static ssize_t on_read(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                       void *buf, uint16_t len, uint16_t offset)
{
    const uint8_t *value = attr->user_data;
    return bt_gatt_attr_read(conn, attr, buf, len, offset, value, sizeof(my_value));
}
```

### 5.3 API 辨析：`bt_gatt_read` vs `bt_gatt_attr_read`

这是一个易混淆点：

- **`bt_gatt_read` (Client API)**: 你是主机，你想去读别人的数据
- **`bt_gatt_attr_read` (Server Helper)**: 你是外设，别人来读你，你用这个函数帮忙把数据打包（处理 Offset 和 Buffer 边界）。**我们在 `on_read` 中使用的是这个。**

---

## 6. 关键 API 参考

| API | 功能 | 使用场景 |
|-----|------|----------|
| `BT_UUID_128_ENCODE` | 编码 128-bit UUID | 定义自定义服务/特征值 UUID |
| `BT_GATT_SERVICE_DEFINE` | 静态定义 GATT 服务 | 创建自定义服务 |
| `BT_GATT_PRIMARY_SERVICE` | 声明主服务 | 标识服务类型 |
| `BT_GATT_CHARACTERISTIC` | 声明特征值 | 定义特征值属性和回调 |
| `bt_gatt_attr_read` | 服务器端读取辅助函数 | 在 Read 回调中打包数据 |
| `bt_gatt_attr_write` | 服务器端写入辅助函数 | 在 Write 回调中处理数据 |
| `BT_DATA_UUID128_ALL` | 广播 128-bit UUID | 在广播包中包含服务 UUID |

---

## 7. 验收标准

执行 `Pristine Build` 并烧录后，使用 nRF Connect APP 验证：

- [ ] 扫描：Raw Data 中应包含 128-bit Service UUID
- [ ] 连接：能够发现 Unknown Service
- [ ] 写入：向特征值写入 `AA BB CC`，RTT Viewer 应打印对应的 Hex Dump
- [ ] 读取：读取特征值，应准确返回之前写入的数据（包含 `0x00` 也能正常读取）

![1769056437479](image/README/1769056437479.png)

---

## 8. 学习总结

Day 4 完成了 GATT 层的基础搭建。通过自定义服务，我们掌握了：

1. **GATT 架构**: 理解 Service、Characteristic、Descriptor 的层级关系
2. **UUID 机制**: 掌握 128-bit UUID 的定义和使用
3. **回调处理**: 正确实现 Read/Write 回调，处理二进制数据
4. **模块化设计**: 将服务代码分离到独立的 `.c/.h` 文件中

---

**Next Step**: Day 5 - 异步通信 (Notify) 与 硬件中断集成
