# Day 11: 多线程与看门狗 (Multithreading & Watchdog)

> **学习目标**：BLE 设备往往需要 7x24 小时运行。如何保证设备在死机后能自动恢复？如何保证用户配置（如设备名、重启次数）在掉电后不丢失？今天我们将构建系统的“保险丝”和“黑匣子”。

---

## 1. 硬件准备

| 组件               | 说明                                         |
| :----------------- | :------------------------------------------- |
| **MCU**      | nRF52832 (或其他 nRF52 系列)                 |
| **Debugger** | J-Link OB / V9                               |
| **PC 端**    | VS Code (NCS v2.7.0)                         |
| **交互**     | 开发板上的 **Button 1** (触发死机) 和 **LED 1** (状态指示) |

---

## 2. 学习目标

本章节的目标是掌握嵌入式系统的**鲁棒性**设计。

**核心任务**：

- **集成 Task WDT**: 理解 Zephyr 的任务看门狗机制，它比传统硬件看门狗更智能，能监控多个线程的健康状态。
- **NVS 数据持久化**: 使用非易失性存储系统 (Non-Volatile Storage) 保存关键数据。
- **GPIO 原生控制**: 脱离 DK Library，使用 Zephyr 原生 GPIO API 控制 LED 和按键，解决极性不匹配问题。
- **故障模拟**: 通过物理按键触发死机，验证系统的自恢复能力。

---

## 3. 核心概念解析

### 3.1 Task WDT (任务看门狗) vs 硬件 WDT

*   **硬件 WDT**: 只有一个计时器。只要有人“喂狗”，系统就不复位。缺点是：如果 BLE 线程卡死，但主线程还在喂狗，硬件看门狗发现不了。
*   **Task WDT**: Zephyr 提供的软件层。
    1.  允许注册多个**通道 (Channel)**（例如：通道 A 监控 BLE，通道 B 监控传感器）。
    2.  **规则**：只有当**所有**通道在规定时间内都完成了喂狗操作，底层的硬件看门狗才会被重置。
    3.  **优势**：任何一个关键任务卡死，都会触发系统复位。

### 3.2 NVS (Non-Volatile Storage)

NVS 是 Zephyr 建立在 Flash 驱动之上的轻量级键值对存储系统。
*   **磨损平衡**: Flash 并不是硬盘，它的每个扇区擦写次数有限（约 1 万次）。NVS 会自动轮换写入位置，延长 Flash 寿命。
*   **掉电保护**: 数据写入 Flash 后，即使完全断电，下次上电也能读取。

---

## 4. 实现步骤详解

### 步骤 1: 修改 Kconfig 配置 (prj.conf)

我们需要启用 Flash、NVS、Watchdog 以及 GPIO 支持。

```properties
# 1. 基础 BLE 配置
CONFIG_BT=y
CONFIG_BT_PERIPHERAL=y
CONFIG_BT_DEVICE_NAME="NCS_Day11_Test"

# 2. 存储相关 (Flash & NVS)
CONFIG_FLASH=y
CONFIG_FLASH_MAP=y
CONFIG_NVS=y
CONFIG_MPU_ALLOW_FLASH_WRITE=y

# 3. 系统安全 (Watchdog)
CONFIG_WATCHDOG=y
CONFIG_TASK_WDT=y

# 4. 外设驱动 (GPIO)
CONFIG_GPIO=y
# CONFIG_DK_LIBRARY=y  <-- 为了精准控制电平，本次移除了 DK 库

# 5. 调试日志 (RTT)
CONFIG_LOG=y
CONFIG_USE_SEGGER_RTT=y
```

### 步骤 2: 获取 Flash 分区信息 (代码难点)

在 NCS v2.7.0 中，获取 Flash 分区句柄必须使用标准 DeviceTree 宏，否则会报错 `undeclared`。

```c
// 错误写法 (旧版本习惯):
// fs.flash_device = NVS_PARTITION_DEVICE;

// 正确写法 (标准宏):
// 1. 获取设备树中的 storage_partition 节点
#define NVS_PARTITION  storage_partition 
// 2. 通过宏获取 Device 指针
fs.flash_device = FIXED_PARTITION_DEVICE(NVS_PARTITION);
// 3. 通过宏获取 Offset
fs.offset = FIXED_PARTITION_OFFSET(NVS_PARTITION);
```

### 步骤 3: 逻辑实现 (main.c)

我们将逻辑分为三部分：
1.  **上电自检**: 读取 NVS 中的 `REBOOT_COUNTER`，打印并加 1 写回。
2.  **正常运行**: LED 闪烁 (Heartbeat)，主循环每秒喂狗。
3.  **模拟故障**: 按下按键 -> `simulate_hang = true` -> 进入死循环且停止喂狗 -> 等待系统复位。

---

## 5. 关键 API 与宏参考

| API/宏                                     | 功能           | 说明                                                                 |
| :----------------------------------------- | :------------- | :------------------------------------------------------------------- |
| `FIXED_PARTITION_DEVICE(node)`           | 获取 Flash 设备 | **关键修复点**。用于获取 `storage_partition` 对应的 Flash 控制器句柄。 |
| `nvs_read(...)` / `nvs_write(...)`       | 读写数据       | 类似于文件系统的 Read/Write，但基于 ID (Key) 而不是文件名。          |
| `task_wdt_add(timeout, ...)`             | 添加 WDT 通道  | 注册一个新的监控通道，返回 `channel_id`。                            |
| `task_wdt_feed(channel_id)`              | 喂狗           | 必须在超时时间内调用，否则系统复位。                                 |
| `gpio_pin_set_dt(spec, val)`             | GPIO 输出      | 设置引脚电平。`val=1` 为有效电平 (Active)，取决于设备树配置。        |

---

## 6. 踩坑与经验总结

### 坑 1: LED 不亮 (极性问题)

**现象**: 使用 `dk_set_led(..., 1)` 时 LED 始终不亮，或者逻辑相反。
**原因**: 官方 DK 板通常是低电平点亮 (Active Low)，而部分自定义板子或设备树配置写成了 `GPIO_ACTIVE_HIGH`。
**解决**:
1.  **方法 A**: 修改设备树 `.overlay`，将 `GPIO_ACTIVE_HIGH` 改为 `GPIO_ACTIVE_LOW`。
2.  **方法 B (本次采用)**: 放弃 DK 库，使用原生 `gpio_pin_set_dt`，并通过代码显式控制 `1` (On) 和 `0` (Off)，确保肯定会有状态翻转。

### 坑 2: 编译报错 `NVS_PARTITION_DEVICE`

**现象**: 编译器提示变量未定义。
**原因**: Zephyr 版本更新后，宏定义规范化了。
**解决**: 必须使用 `FIXED_PARTITION_DEVICE()` 和 `FIXED_PARTITION_OFFSET()` 宏来包裹设备树节点标签。

### 坑 3: Flash 寿命焦虑

**注意**: 不要在 `while(1)` 循环中无条件调用 `nvs_write`。如果一秒写一次，Flash 几天就报废了。
**最佳实践**: 仅在**数据发生改变**且**确实需要保存**时（如用户修改设置、系统重启前）才写入 NVS。

---

## 7. 验收标准

- [x] **数据持久化**: 烧录后查看 RTT 日志，系统能正确识别是 "First Boot" 还是 "Rebooted"，且计数器 `Current Count` 随重启次数增加。
- [x] **LED 心跳**: 系统运行时，LED1 能够正常闪烁（证明主循环在跑）。
- [x] **死机触发**: 按下 Button 1 后，RTT 打印警告日志，LED 停止闪烁（保持常亮或常灭）。
- [x] **自动复位**: 等待约 5 秒后，系统自动重启，RTT 重新打印启动日志，且 NVS 计数器 +1。

---

## 8. 学习总结

Day 11 完成了从“功能开发”到“系统可靠性”的转变。

1.  **容错机制**: 哪怕代码写得再完美，硬件干扰也可能导致死机。看门狗是最后一道防线。
2.  **状态记忆**: NVS 让设备拥有了“记忆”，这是实现个性化设置（如修改蓝牙名、绑定信息）的基础。
3.  **底层控制**: 我们通过切回原生 GPIO API，更深入地理解了设备树 `phandle` 和驱动模型的映射关系。

**Next Step**: **Day 12 - 综合实战：智能门禁控制器 (架构设计)**。我们将开始为期三天的终极挑战，整合广播、连接、服务、安全、存储和多线程，从零打造一个商业级 BLE 应用！