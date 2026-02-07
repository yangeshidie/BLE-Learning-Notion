# Smart Access Controller (BLE) | 智能门禁控制器

> **基于 nRF52832 & Nordic Connect SDK (NCS) v2.7.0**

本项目是一个完整的 BLE 智能门禁系统演示，展示了如何使用 Zephyr RTOS 构建工业级、低功耗、高可靠性的蓝牙外设。项目涵盖了广播策略管理、高等级安全配对、自定义 GATT 服务、硬件驱动（GPIO/ADC）以及 RTOS 并发模型。

## 📖 项目背景 (Tutorial Context)

本项目是 BLE 开发学习旅程的 **Day 12 - Day 14** 综合实战成果。

* **Day 12**: 架构设计 (DeviceTree, 广播策略, GATT 表设计).
* **Day 13**: 核心代码实现 (BLE 栈初始化, 广播状态机, 硬件控制).
* **Day 14**: 压力测试与验收 (安全配对验证, 稳定性测试, Bug 修复).

---

## 🏗 系统架构 (System Architecture)

系统采用 **分层模块化设计**，将蓝牙协议栈逻辑、业务逻辑和硬件驱动严格解耦。

### 1. 硬件抽象层 (Hardware Layer)

通过 Zephyr **DeviceTree (.dts/.overlay)** 描述硬件：

* **LED 1 (Status)**: 指示蓝牙连接状态。
* **LED 2 (Lock)**: 模拟电磁锁（高电平开锁）。
* **Button 0**: 用户唤醒按键（外部下拉，中断触发）。
* **ADC (AIN3)**: 采集电位器电压，模拟电池电量。

### 2. 软件模块划分

| 模块                  | 文件               | 职责描述                                   | 关键技术                                                         |
| :-------------------- | :----------------- | :----------------------------------------- | :--------------------------------------------------------------- |
| **Main**        | `main.c`         | 系统初始化编排，确保依赖顺序。             | `System WorkQueue`, `Init Priority`                          |
| **BLE Setup**   | `ble_setup.c`    | 管理广播状态机、连接回调、安全配对回调。   | `GAP`, `Advertising`, `SMP (Just Works)`, `Settings/NVS` |
| **Service**     | `service_lock.c` | 定义 Smart Lock GATT 服务，处理数据收发。  | `GATT Macros`, `UUID`, `Notifications`                     |
| **App Lock**    | `app_lock.c`     | 处理按键事件、执行开锁动作、自动关锁逻辑。 | `GPIO`, `Interrupts`, `k_work_delayable`                   |
| **App Battery** | `app_battery.c`  | 定期采集电压并更新标准电池服务。           | `ADC (SAADC)`, `BAS Service`                                 |

### 3. 并发与事件模型 (Concurrency Model)

为了保证系统稳定性，严禁在中断服务程序 (ISR) 中执行耗时操作（如 BLE API 调用）。

* **ISR Context**: 仅做最小处理（如 `k_work_submit`）。
* **System WorkQueue**: 处理按键去抖、ADC 采样、广播切换。
* **BLE Rx Thread**: 处理来自手机的写入请求，并将动作分发给 WorkQueue。

---

## 🧠 核心知识点与技术细节

### 1. 双模广播策略 (Advertising Strategy)

为了平衡功耗与响应速度，系统实现了状态机切换：

* **Fast Advertising**: 间隔 40-50ms，持续 30秒。用于按键唤醒或断开连接后的快速重连。
* **Slow Advertising**: 间隔 1s-1.5s，永久持续。用于低功耗待机。

### 2. 安全与持久化 (Security & Bonding)

* **强制加密**: 自定义服务特征值权限设为 `BT_GATT_PERM_WRITE_ENCRYPT`。未配对设备尝试写入时，协议栈自动拒绝并触发配对流程。
* **Bonding**: 启用 `CONFIG_BT_SETTINGS=y` 和 `CONFIG_NVS=y`。配对成功后，密钥自动存储在 Flash 中。设备复位或掉电后，无需再次配对即可自动加密重连。

### 3. WorkQueue 的重要性 (Anti-Crash)

**实战教训**:

* 在 GPIO 中断中直接调用 `bt_le_adv_start` 会导致内核 panic。**解决**: 使用 `k_work_submit`。
* 在 BLE 回调中执行长延时或复杂逻辑会导致栈溢出。**解决**: 将开锁动作移至 `System WorkQueue`，并增加 `CONFIG_BT_RX_STACK_SIZE` 至 2048。

### 4. GATT 通知指针陷阱

**实战教训**:

* 调用 `bt_gatt_notify` 时如果传入 UUID 指针而不是 Attribute 指针，会导致 **Bus Fault**。
* **正确做法**: 使用 `bt_gatt_notify_uuid` 并传入 Service 的 Attribute 指针作为搜索起点。

---

## 📂 文件结构

```text
MySmartLock/
├── prj.conf                # Kconfig配置 (BT, ADC, FLASH, NVS, LOG)
├── app.overlay             # 硬件引脚映射 (LEDs, Button, ADC)
├── CMakeLists.txt          # 构建脚本
├── src/
│   ├── main.c              # 入口
│   ├── ble_setup.c         # 蓝牙管理
│   ├── service_lock.c      # 自定义服务 (Lock)
│   ├── app_lock.c          # 业务逻辑 (GPIO, WQ)
│   └── app_battery.c       # 电池逻辑 (ADC)
└── include/
    ├── ble_setup.h
    ├── service_lock.h
    ├── app_lock.h
    └── app_battery.h
```

---

## 🚀 快速开始

### 1. 硬件准备

* 开发板: nRF52832 DK (或兼容板)。
* 连接:
  * **LED 2 (Lock)** -> P0.03
  * **Button 0** -> P0.04 (外部下拉)
  * **Potentiometer** -> P0.05 (AIN3)

### 2. 编译与烧录

```bash
# 1. 编译 (指定板型)
west build -b nrf52dk_nrf52832

# 2. 擦除整片 Flash (防止旧绑定信息冲突 - 首次极其重要)
west flash --erase

# 3. 烧录
west flash
```

### 3. 验证流程 (验收标准)

1. **广播检查**:

   * 上电后，nRF Connect 扫描到 `SmartLock_Demo`。
   * RSSI 刷新很慢（慢广播）。
   * 按下按键，RSSI 刷新变快（快广播），30秒后恢复。
2. **安全配对**:

   * 连接设备。
   * 尝试开启自定义服务的 Notify 或写入数据。
   * **现象**: 手机弹出配对请求 -> 点击确认 -> 配对成功 -> 操作成功。
3. **开锁测试**:

   * 向 `Unlock Control Point` 写入 `0x01`。
   * **现象**: LED 2 点亮，手机收到 Notify `Unlocked`。
   * 3秒后，LED 2 熄灭，手机收到 Notify `Locked`。
4. **掉电记忆**:

   * 配对成功后，按 Reset 复位开发板。
   * 再次连接，**不再**弹出配对窗口，直接可以开锁。
5. **电池监测**:

   * 旋转电位器。
   * Battery Service 的电量百分比随电压变化而更新 (每10秒)。

---

## 🛠 常见问题 (Troubleshooting)

* **Bus Fault (0xbc000202)**: 通常是 `bt_gatt_notify` 参数传错，或者 WorkQueue 对象未初始化就使用。
* **Stack Overflow**: 如果日志打印过多或逻辑过深，请在 `prj.conf` 增大 `CONFIG_BT_RX_STACK_SIZE`。
* **无法配对/反复要求配对**: 执行 `west flash --erase` 清除 Flash 中的旧密钥，并在手机设置中“忽略设备”。

---
