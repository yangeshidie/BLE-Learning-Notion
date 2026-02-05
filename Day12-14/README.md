
---

### 1. 硬件抽象层与引脚定义 (DeviceTree Design)

基于你提供的硬件资源（LED 高电平点亮、按键高电平有效且有外部下拉），我们在 DeviceTree (`.dts` 或 `.overlay`) 中进行如下逻辑映射：

* **LED 0 (Status LED)**: 用于指示蓝牙状态（广播中/已连接）。
  * *逻辑*: `GPIO_ACTIVE_HIGH`
* **LED 1 (Lock Actuator)**: 模拟开锁电磁铁。
  * *逻辑*: `GPIO_ACTIVE_HIGH`
* **Button 0 (Wake-up/Trigger)**: 门内开锁键 或 唤醒快速广播。
  * *逻辑*: `GPIO_ACTIVE_HIGH` (配合外部下拉电阻，软件配置 `bias-pull-down` 确保双重稳定，或 `bias-disable` 如果外部电阻足够强)。
* **ADC (Battery Monitor)**: 读取串联电位器的电压。
  * *配置*: 使用 `saadc`，配置合适的分压系数（Gain）和参考电压（Internal 0.6V），将模拟电压映射为 0-100% 的电量。

---

### 2. 蓝牙广播策略设计 (Advertising Strategy)

为了平衡功耗（电池供电）与连接响应速度，采用 **双模广播策略**。

#### 状态机逻辑：

1. **系统上电**: 默认进入 **慢速广播 (Slow Advertising)**。
2. **按键触发**: 当用户按下 Button 0 时，停止当前广播，切换到 **快速广播 (Fast Advertising)**。
3. **超时机制**: 快速广播持续 **30秒**（可配置）。若无设备连接，自动回退到 **慢速广播**。
4. **建立连接**: 广播自动停止。
5. **断开连接**: 自动进入 **快速广播**（为了方便调试或再次重连），超时后回退到 **慢速广播**。

#### 参数设计：

| 模式                       | 广播间隔 (Interval)                               | 持续时间 (Duration) | 目的                                   |
| :------------------------- | :------------------------------------------------ | :------------------ | :------------------------------------- |
| **Fast Advertising** | **40 ms** (Min) - **50 ms** (Max)     | 30 秒               | 极速发现，提升用户体验，响应按键唤醒。 |
| **Slow Advertising** | **1000 ms** (Min) - **1500 ms** (Max) | 永久 (0)            | 极低功耗待机，保持设备“在线”但省电。 |

---

### 3. GATT 服务与特征值设计 (GATT Profile)

我们需要定义三个 Service。对于自定义 Service，建议使用 128-bit UUID。

#### A. Device Information Service (DIS) - 标准服务

* **UUID**: `0x180A`
* **内容**: Manufacturer Name, Model Number, Firmware Revision.
* **目的**: 验收时在 nRF Connect 中展示设备身份。

#### B. Battery Service (BAS) - 标准服务

* **UUID**: `0x180F`
* **Characteristic**: Battery Level (`0x2A19`)
  * *Properties*: **Read | Notify**
  * *Security*: None (通常电量可以公开读取) 或 Encrypted Read。
  * *逻辑*: 关联 ADC 采样任务，电位器电压变化时，若超过阈值变化，推送 Notify。

#### C. Smart Lock Service (SLS) - 自定义服务

* **Service UUID**: `12345678-1234-5678-1234-56789ABC0000` (示例)
* **Characteristic 1: Lock Control Point (控制点)**
  * **UUID**: `...0001`
  * **Properties**: **Write** (or Write Without Response)
  * **Permissions**: **Write Encrypted** (必须加密链路才能写入)
  * **Payload**: `0x01` = 开锁, `0x02` = 上锁 (预留)。
* **Characteristic 2: Lock Status (状态)**
  * **UUID**: `...0002`
  * **Properties**: **Read | Notify**
  * **Permissions**: **Read Encrypted**
  * **Payload**: `0x00` = Locked, `0x01` = Unlocked.

---

### 4. 安全策略设计 (Security Manager)

这是门禁系统的核心。必须强制链路加密。

* **Pairing Strategy (配对策略)**:
  * 由于 nRF52832 开发板（本案）没有屏幕和数字键盘，只能使用 **LESC Just Works** 或 **Legacy Just Works**。
  * *注意*: 虽然 Just Works 防不住 MITM（中间人攻击），但符合当前硬件条件。
* **Bonding (绑定)**: **Required (Enable)**.
  * 配对后保存密钥，下次重连自动加密，无需再次配对。
* **Security Level Enforcement**:
  * 在 GATT 特征值定义中，将读写权限设置为 `BT_GATT_PERM_WRITE_ENCRYPT`。
  * 这意味着：手机连接后，若尝试写入“开锁指令”，协议栈会返回 "Insufficient Authentication"，手机系统会自动触发配对弹窗。

---

### 5. 软件架构与并发控制 (Software Architecture)

基于 Zephyr RTOS，采用事件驱动模型。严禁在 ISR 或 Bluetooth Callback 中执行延时操作。

#### 核心模块划分：

1. **Main Thread (System Init)**:

   * 初始化 GPIO, ADC, Bluetooth。
   * 配置并启动初始广播。
   * 进入主循环（或休眠）。
2. **Bluetooth Callbacks (ISR Context)**:

   * `connected`/`disconnected`: 更新 LED0 状态，重置广播状态机。
   * `security_changed`: 记录加密状态，只有加密成功才允许处理业务逻辑。
   * `gatt_write_callback`: **关键点**。收到开锁指令后，**不要**直接操作 LED 延时。而是提交一个任务到 **System Work Queue**。
3. **Work Queue Item: Unlock Action (开锁任务)**:

   * 这是一个 `struct k_work_delayable`。
   * **逻辑**:
     1. 设置 LED1 (Lock) 为 High (开锁)。
     2. 更新 GATT Status 为 Unlocked，并发送 Notify。
     3. 使用 `k_work_reschedule` 延时 3秒（模拟电磁铁动作时间）。
     4. (延时回调中) 设置 LED1 为 Low (关锁)。
     5. 更新 GATT Status 为 Locked，并发送 Notify。
4. **ADC Work Queue (电量采集)**:

   * 使用 `k_work_delayable` 或 Timer。
   * 每隔 10秒 或 60秒 唤醒一次，读取电位器电压，转换百分比，调用 `bt_gatt_notify`。

---

### 6. 验收与测试设计 (Verification Plan)

针对 Day 14 的验收，设计以下检查点：

1. **广播切换验证**:
   * 静置 1 分钟，使用 nRF Connect 扫描，应看到广播间隔很长（~1s）。
   * 按下按键，扫描到的 RSSI 刷新率明显变快（~50ms）。
2. **安全流程验证**:
   * 使用未配对的手机连接。
   * 尝试读取 Battery Level -> 成功。
   * 尝试写入 Lock Control Point -> **失败** (且手机弹出配对请求)。
   * 同意配对 -> 再次写入 -> **成功** -> LED1 点亮 3秒后熄灭。
3. **并发压力测试**:
   * 在 LED1 点亮（开锁中）的时候，再次发送开锁指令，系统不应崩溃，应该重置定时器或忽略。
   * 在 LED1 点亮的时候，快速断开蓝牙，LED1 必须能在 3秒后自动熄灭（Work Queue 独立于蓝牙连接状态）。

### 总结 (Summary)

* **核心难点**: 正确处理 ISR 上下文到 Thread 上下文的切换（使用 Work Queue），以及正确配置 GATT 权限以触发安全配对流程。
* **硬件注意**: nRF52832 无 USB，Day 13 代码实现时请配置 **RTT (Real Time Transfer)** 进行日志打印，而不是 UART（除非你用了 USB 转串口模块）。

这套设计完全符合 NCS v2.7.0 的规范，结构清晰，足以支撑后续的代码堆叠。
