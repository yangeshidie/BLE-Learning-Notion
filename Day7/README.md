# Day 7: 吞吐量优化与 MTU/DLE

> **学习目标**：突破 BLE 默认的“20字节/包”和低速限制。通过配置 MTU、DLE 和 2M PHY，结合连接参数优化，实现 BLE 数据的高速、大包传输。

---

## 1. 硬件准备

| 组件               | 说明                                               |
| ------------------ | -------------------------------------------------- |
| **MCU**      | nRF52832 / nRF52840 (支持 2M PHY)                  |
| **Debugger** | J-Link OB / V9                                     |
| **PC 端**    | 支持**高波特率** (460800+) 的 USB 转串口模块 |
| **手机 APP** | nRF Connect (用于查看 PHY/MTU 和测试速率)          |

---

## 2. 学习目标

本章节的目标是将 BLE 传输从“慢速通道”升级为“高速公路”。

**核心任务**：

- **逻辑层 (GATT)**: 增大 **MTU** (Maximum Transmission Unit)，实现单次 API 调用发送 244 字节数据
- **链路层 (Link Layer)**: 启用 **DLE** (Data Length Extension)，允许空口数据包不分片直接传输
- **物理层 (PHY)**: 启用 **2M PHY**，将无线传输符号率翻倍
- **系统瓶颈**: 识别并解除 UART 波特率和 Log 日志对吞吐量的限制

---

## 3. 核心概念解析

### 3.1 MTU vs DLE：集装箱与公路

很多初学者容易混淆这两个概念，它们决定了“一次能发多少数据”。

- **ATT MTU (逻辑层)**: 就像 **集装箱的大小**。
  - 默认 23 字节（有效载荷 20 字节）。
  - 增大到 247 字节后，有效载荷为 244 字节。这意味着应用层只需调用一次 `bt_nus_send`，无需在代码中循环拆包。
- **DLE (链路层)**: 就像 **公路的单次通行能力**。
  - 即使集装箱很大（MTU=247），如果公路很窄（DLE 默认 27 字节），集装箱会被拆成 10 个小碎片在空中发送，效率依然很低。
  - 启用 DLE（251 字节）后，大集装箱可以**一次性**通过无线电发出。

### 3.2 2M PHY (蓝牙 5.0 特性)

- **1M PHY**: 经典蓝牙速度，理论极限约 70-80 kB/s。
- **2M PHY**: 符号率翻倍，理论极限约 140 kB/s（受限于帧间隔，实测约提升 1.6 倍）。
- **注意**: 必须手机和设备都支持蓝牙 5.0+ 才能协商成功。

### 3.3 木桶效应：UART 瓶颈

BLE 吞吐量再高，如果数据源头（UART）进水太慢，总速度也上不去。

- **115200 波特率**: 极限约 11.5 kB/s。即使 BLE 能跑 100 kB/s，系统吞吐量也被锁死在 11.5 kB/s。
- **921600 波特率**: 极限约 90 kB/s。足以喂饱 2M PHY。

---

## 4. 实现步骤详解

### 步骤 1: 修改 Kconfig 配置 (prj.conf)

我们需要“全方位”地解除限制：增大包长、增加缓冲区、启用高速模式。

```conf
# 1. 增大 MTU (逻辑大包)
CONFIG_BT_L2CAP_TX_MTU=247
CONFIG_BT_BUF_ACL_RX_SIZE=251

# 2. 启用 DLE (物理长帧)
CONFIG_BT_CTLR_DATA_LENGTH_MAX=251
CONFIG_BT_BUF_ACL_TX_SIZE=251

# 3. 启用 2M PHY
CONFIG_BT_PHY_UPDATE=y
CONFIG_BT_USER_PHY_UPDATE=y

# 4. 增加发送缓冲区 (防止频繁 -ENOMEM)
CONFIG_BT_L2CAP_TX_BUF_COUNT=20
CONFIG_BT_BUF_ACL_TX_COUNT=20

# 5. (可选) 关闭日志以节省 CPU
# CONFIG_LOG=n
```

### 步骤 2: MTU 协商与全局变量更新

**文件**: `main.c`

连接建立后，作为从机主动发起 MTU 协商。**关键是要更新应用层的分包大小**。

```c
/* 全局变量，默认 23，协商后更新 */
static uint16_t current_mtu = 23;

static void exchange_func(struct bt_conn *conn, uint8_t att_err,
                          struct bt_gatt_exchange_params *params)
{
    if (!att_err) {
        current_mtu = bt_gatt_get_mtu(conn);
        LOG_INF("MTU updated to %d", current_mtu);
    }
}

static struct bt_gatt_exchange_params exchange_params = {
    .func = exchange_func,
};

/* 在 connected 回调中调用 */
bt_gatt_exchange_mtu(conn, &exchange_params);
```

### 步骤 3: 请求 2M PHY

**文件**: `main.c`

同样在连接建立后，请求切换物理层速率。

```c
/* 在 connected 回调中调用 */
const struct bt_conn_le_phy_param preferred_phy = {
    .options = BT_CONN_LE_PHY_OPT_NONE,
    .pref_tx_phy = BT_GAP_LE_PHY_2M,
    .pref_rx_phy = BT_GAP_LE_PHY_2M,
};
bt_conn_le_phy_update(conn, &preferred_phy);
```

### 步骤 4: 消费者逻辑适配大包

**文件**: `main.c` (WorkQueue Handler)

Day 6 的 RingBuffer 消费者逻辑需要动态适配 `current_mtu`。

```c
/* 动态获取尽可能多的数据 */
uint32_t send_len = current_mtu - 3; // 减去 ATT 头部
len = ring_buf_get_claim(&uart_ring_buf, &data_ptr, send_len);
```

---

## 5. 关键 API 参考

| API                         | 功能             | 使用场景                                        |
| --------------------------- | ---------------- | ----------------------------------------------- |
| `bt_gatt_exchange_mtu`    | 发起 MTU 协商    | 连接建立后立即调用，告知手机“我想发大包”      |
| `bt_gatt_get_mtu`         | 获取当前 MTU 值  | 协商回调中调用，用于更新全局发送逻辑            |
| `bt_conn_le_phy_update`   | 请求 PHY 更新    | 请求切换到 2M 物理层                            |
| `bt_conn_le_param_update` | 请求连接参数更新 | 请求更短的连接间隔 (如 15ms) 以提高发包频率     |
| `ring_buf_get_claim`      | 零拷贝获取数据   | 直接获取 Buffer 内部指针，避免 memcpy，提升效率 |

---

## 6. 踩坑与经验总结

### 坑 1: 虚假的“瓶颈” (UART Baudrate)

**现象**: 代码改得完美无缺，MTU 247, PHY 2M 全开了，但速度死活卡在 11kB/s。

**原因**: 电脑串口波特率还在 115200。**这是物理瓶颈**。

**解决**: 修改设备树 (`current-speed`) 和 PC 串口助手，将波特率提至 **460800** 或 **921600**。

### 坑 2: 观测者效应 (Log Overhead)

**现象**: 开启 Debug 日志时，RingBuffer 频繁溢出，速度不稳定；关闭日志后速度飞升。

**原因**: 打印日志（特别是串口日志）极其消耗 CPU 和时间。在高吞吐场景下，CPU 忙于打印 "Sent 244 bytes"，没时间真正去搬运数据。

**解决**: 验收测试时，务必设置 `CONFIG_LOG=n` 或 `CONFIG_LOG_DEFAULT_LEVEL=0`。

### 坑 3: 疯狂的 -12 (Busy Wait)

**现象**: 日志刷屏 `bt_gatt_notify failed: -12`，且时间戳间隔极短（<1ms）。

**原因**: 当协议栈返回 `-ENOMEM` (Buffer Full) 时，代码未做延时直接重试，导致 CPU 100% 占用空转，不仅费电还可能阻塞 UART 中断。

**解决**: 在重试逻辑中加入 `break` 跳出循环，并确保 `k_work_schedule` 有足够的延迟（如 5-10ms）。

### 坑 4: 硬件流控缺失 (Missing RTS/CTS)

**现象**: 波特率 921600，速度很快但文件传输校验失败（丢包）。

**原因**: BLE 发送偶尔会卡顿（无线重传），此时 UART 继续全速进数据，RingBuffer 瞬间被填满。如果没有 RTS 硬件流控让电脑闭嘴，数据必丢。

**解决**:

1. 必须连接 RTS/CTS 引脚并启用 `hw-flow-control`。
2. 如果硬件不支持，必须降低 UART 波特率，使其小于 BLE 的平均吞吐量（例如使用 460800）。

---

## 7. 验收标准

- [ ] **日志检查**: 看到 `MTU: 247` 和 `PHY: 2M` 的确认信息
- [ ] **App 确认**: nRF Connect App 中显示 PHY 为 2M，Data Length 为 251
- [ ] **速度达标**: 实际文件传输速率稳定在 **20 kB/s** 以上 (基于 230400+ 波特率)
- [ ] **数据完整**: 发送 300KB+ 的大文件，接收端校验无误（无丢包、无乱序）

---

## 8. 学习总结

Day 7 完成了 BLE 性能的极致压榨。我们认识到高吞吐量不仅仅是改一个参数，而是一个系统工程：

1. **木桶理论**: CPU 处理能力、UART 波特率、BLE PHY 速率、MTU 大小，任何一环都可能成为瓶颈。
2. **异步思维**: 理解协议栈的 `-ENOMEM` 是正常的流控信号，而不是错误，需要优雅地处理重试。
3. **观测干扰**: 在高性能嵌入式开发中，Log 本身就是最大的性能杀手之一。

**核心公式**:

$$
吞吐量 = \frac{Payload (244)}{TotalTime} \times \frac{1}{Interval}
$$

(同时受限于 UART 进水速度和 CPU 处理效率)

---

**Next Step**: Day 8 - 安全配对 (SMP)
