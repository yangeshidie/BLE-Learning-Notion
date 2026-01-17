# BLE-Learning-Notion
关于一些学习蓝牙协议栈的笔记和程序代码

**14天“特种兵”式工程级 BLE 学习计划**。
**核心目标**：从环境搭建到完成一个包含 **自定义服务、高吞吐传输、安全配对、低功耗管理、OTA升级** 的商业级 Demo。

---

# **⚔️ 核心守则 (The Golden Rules)**

1.  **Callback Is God（回调即上帝）**：BLE 是全异步的。**永远不要**在代码里写 `send(); delay(100);`。发送是否成功，只有在 `on_sent` 回调里才知道。一切逻辑必须由事件（Event）驱动。
2.  **中断上下文禁区**：BLE 协议栈的回调通常运行在中断或高优先级线程中。**严禁**在回调函数（如 `bt_gatt_cb`）里做耗时操作（如打印长 Log、I2C 读写、复杂的浮点运算）。请使用 `k_work_submit` 扔到工作队列去处理。
3.  **信任抓包，不信 Log**：代码打印 "Send OK" 只是代表你把数据塞给了协议栈的 Buffer，不代表发到了空中。**Wireshark 抓到的才是真理**。
4.  **配置驱动开发**：在 NCS (Zephyr) 中，70% 的功能开关在 `prj.conf` (Kconfig) 和 `.dts` (DeviceTree) 里，只有 30% 在 C 代码里。遇到功能跑不通，先查配置。

---

# **任务编排**
## **第一阶段：协议栈架构与链路层 (Day 1 - Day 3)**
**目标**：跑通环境，理解“广播”与“连接”的本质，学会使用抓包器。

*   **Day 1: 环境配置与 RTOS 映射**
    *   **任务**：
        1.  安装 VS Code + nRF Connect Extension。
        2.  在 Toolchain Manager 安装最新 NCS (v2.x.x)我使用的是v2.7.0。
        3.  编译并烧录 `samples/basic/blinky` 确认硬件正常。
        4.  **关键映射**：花 2 小时阅读 Zephyr 文档，建立心智映射：
            *   FreeRTOS `xTaskCreate` -> Zephyr `k_thread_create`
            *   FreeRTOS `xQueueSend` -> Zephyr `k_msgq_put`
            *   FreeRTOS `BinarySemaphore` -> Zephyr `k_sem`
    *   **验收**：能通过修改 `prj.conf` 开启/关闭 Log 功能 (`CONFIG_LOG=y`)，并在 RTT Viewer 中看到输出。

*   **Day 2: 广播 (Advertising) 与 抓包**
    *   **任务**：
        1.  **刷写 Dongle**：将 nRF52840 Dongle 刷入 **nRF Sniffer** 固件，配置好 Wireshark 插件。
        2.  **Legacy 广播**：跑通 `bluetooth/peripheral` 例程。
        3.  **自定义广播**：修改代码，在 Manufacturer Data (0xFF) 字段中放入你的名字（Hex 编码）。
    *   **验收**：
        1.  手机 nRF Connect APP 扫描到名为 "MyBLE" 的设备。
        2.  **Wireshark** 抓到广播包，看到 `ADV_IND` 数据包中包含你名字的 Hex 数据。

*   **Day 3: 连接参数与状态机**
    *   **任务**：
        1.  **连接回调**：在 `connected` 和 `disconnected` 回调中添加 LED 状态指示。
        2.  **参数协商**：编写代码，在连接建立 5 秒后，主动请求修改连接参数：
            *   Interval: 20ms (快速) vs 500ms (低功耗)
            *   Latency: 0
            *   Timeout: 400ms
        3.  **PHY 切换**：在连接后请求切换到 **2M PHY**。
    *   **验收**：抓包确认 `LL_CONNECTION_UPDATE_IND` 报文出现，且抓包软件显示 Delta Time 变为了你设定的值。

---

## **第二阶段：GATT 服务与数据吞吐 (Day 4 - Day 7)**
**目标**：抛弃标准服务，完全手写自定义协议，并把速度拉满。

*   **Day 4: 手写自定义 GATT 服务**
    *   **任务**：
        1.  **定义服务**：使用宏 `BT_GATT_SERVICE_DEFINE` 创建一个 Service。
        2.  **UUID**：生成两个 128-bit 随机 UUID（一个用于 Service，一个用于 Characteristic）。
        3.  **特征值**：创建一个支持 **Read** 和 **Write** 的特征值。
    *   **验收**：手机连接后，能读取到特征值的初始值；手机写入数据后，板子串口能打印出收到的数据。

*   **Day 5: 异步通信 (Notify/Indicate)**
    *   **任务**：
        1.  **CCCD**：给特征值添加 `BT_GATT_CCC` 属性（这是 Notify 必须的）。
        2.  **按键上报**：实现按下板子按键，通过 `bt_gatt_notify` 发送一个计数值给手机。
        3.  **订阅检测**：在发送前，必须检查 CCCD 是否已被手机使能，否则不发送。
    *   **验收**：手机 Enable Notify 后，按下按键，手机屏幕实时滚动收到数据。

*   **Day 6: NUS 透传与缓冲区管理 (工程重点)**
    *   **任务**：
        1.  **移植 NUS**：参考 `peripheral_uart`，实现 UART 到 BLE 的双向透传。
        2.  **RingBuffer**：引入 `sys/ring_buffer.h`，处理 UART 高速中断接收，并在 System Work Queue 中取出数据发给 BLE。
        3.  **流控**：处理 `bt_gatt_notify` 返回 `-EAGAIN` 或 `-ENOMEM` 的情况（这代表协议栈 Buffer 满了，需要等待）。
    *   **验收**：电脑串口助手狂发数据，手机端接收不丢包、不乱序、不崩溃。

*   **Day 7: 吞吐量优化与 MTU/DLE**
    *   **任务**：
        1.  **配置**：修改 `prj.conf`：
            *   `CONFIG_BT_L2CAP_TX_MTU=247` (GATT 最大载荷)
            *   `CONFIG_BT_CTLR_DATA_LENGTH_MAX=251` (链路层长包)
        2.  **协商**：在代码中处理 MTU Exchange 回调。
    *   **验收**：抓包看到 `ATT_MTU_REQ` 为 247，且数据包长度明显变长。实测传输速度超过 20kB/s (1M PHY) 或 40kB/s (2M PHY)。

---

## **第三阶段：安全、功耗与生产化 (Day 8 - Day 11)**
**目标**：让你的 Demo 具备商业产品的基本素质。

*   **Day 8: 安全配对 (SMP)**
    *   **任务**：
        1.  **开启 SMP**：配置 `CONFIG_BT_SMP=y`。
        2.  **Passkey Pairing**：实现“显示配对码”模式。手机连接时，板子串口打印 6 位数字，手机输入正确才允许绑定。
        3.  **权限控制**：修改 GATT 特征值属性为 `BT_GATT_PERM_READ_ENCRYPT`，确保未加密链路无法读取数据。
    *   **验收**：未绑定时读取特征值报错 "Insufficient Authentication"；绑定后读取正常。抓包看到加密后的数据变成乱码。

*   **Day 9: 极致低功耗 (Low Power)**
    *   **任务**：
        1.  **PM Device**：学习如何关闭不用的外设（UART, SPI 等）。
        2.  **测量**：使用万用表（或 PPK）测量电流。
        3.  **优化**：将广播间隔调至 1000ms，在空闲时关闭 Log 模块。
    *   **验收**：广播期间平均电流压到 **20uA 以下**（具体视板载外设而定，目的是学会通过关闭外设降低底电流）。

*   **Day 10: 空中升级 (FOTA/DFU)**
    *   **任务**：这是最难的一天，请保持耐心。
    *   1.  **MCUboot**：在配置中启用 MCUboot。
    *   2.  **SMP Server**：集成 Simple Management Protocol (SMP) 服务。
    *   3.  **升级测试**：编译两个版本固件（V1 亮红灯，V2 亮绿灯）。
    *   **验收**：通过手机 nRF Connect APP 将 V2 固件推送到板子，板子自动重启并亮绿灯。

*   **Day 11: 多线程与看门狗**
    *   **任务**：
        1.  **看门狗**：集成 Task WDT，监控 BLE 线程和主逻辑线程。
        2.  **Flash 存储**：使用 NVS (Non-Volatile Storage) 保存用户设置（如设备名称），掉电不丢失。
    *   **验收**：模拟死机（`while(1)`），看门狗能复位系统；重启后读取到 NVS 中的数据。

---

## **第四阶段：终极实战项目 (Day 12 - Day 14)**
**目标**：综合运用，独立开发。

*   **项目题目：智能门禁控制器**
    *   **Day 12: 架构设计**
        *   设计广播策略（平时慢广播，按键触发快广播）。
        *   设计 GATT 表（包含 Battery Service, Device Info Service, 自定义控制 Service）。
        *   设计安全策略（必须绑定才能开锁）。
    *   **Day 13: 代码实现**
        *   堆代码。注意使用 Work Queue 处理开锁电磁铁（LED模拟）的动作，不要阻塞蓝牙协议栈。
    *   **Day 14: 压力测试与抓包验收**
        *   **测试**：用 Dongle 配合 nRF Connect PC 端，编写脚本或手动快速反复连接、断开、写入数据，测试稳定性。
        *   **抓包**：抓取完整的“广播 -> 连接 -> 密钥交换 -> 加密通信 -> 断开”全过程，保存为 `.pcapng` 文件。

---

# **每日检查清单**
在每天结束时，问自己三个问题：
1.  **Did I Sniff?** 我今天抓包确认数据了吗？
2.  **Does it Panic?** 如果我连续断开重连 10 次，系统会 HardFault 吗？
3.  **Is it Low Power?** 我是否在不需要的时候关闭了高耗能外设（尤其是 UART）？
