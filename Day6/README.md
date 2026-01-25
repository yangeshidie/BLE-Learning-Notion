# Day 6: NUS 透传与缓冲区管理 (工程重点)

> **学习目标**：从"单字符通知"升级为"高速双向透传"。实现 UART 与 BLE 之间的无缝数据转发，引入 RingBuffer 解决高速数据丢包问题，实现流控确保系统稳定。

---

## 1. 硬件准备

| 组件 | 说明 |
|------|------|
| **MCU** | nRF52832 核心板 |
| **Debugger** | J-Link OB / V9 (SWD 接口) |
| **外设** | 板载 UART (通常是 Console 串口) |
| **PC 端** | 串口助手 (如 PuTTY, Tera Term) + nRF Connect APP |

---

## 2. 学习目标

本章节的目标是实现**高速双向透传**，将 BLE 变成"无线 UART"。

**核心任务**：
- **BLE 层面**: 实现 **NUS (Nordic UART Service)** 标准服务，支持双向数据透传
- **缓冲层面**: 引入 **RingBuffer** 解决 UART 高速接收与 BLE 发送速率不匹配的问题
- **异步层面**: 使用 **System Work Queue** 解耦中断上下文与 BLE 发送逻辑
- **稳定性层面**: 实现 **流控机制**，处理 BLE 协议栈 Buffer 满的情况

---

## 3. 核心概念解析

### 3.1 NUS (Nordic UART Service)

NUS 是 Nordic Semiconductor 定义的标准 GATT 服务，用于实现 UART 透传功能。它包含两个特征值：

| 特征值 | UUID | 属性 | 方向 |
|--------|------|------|------|
| RX | `6E400002` | WRITE | 手机 → 设备 |
| TX | `6E400003` | NOTIFY | 设备 → 手机 |

**优势**: 使用标准 UUID，nRF Connect APP 可直接识别为 UART 服务，无需手动输入 UUID。

### 3.2 为什么要用 RingBuffer？

**问题场景**: 假设电脑以 115200 波特率狂发数据，而 BLE 正在处理一次连接参数更新导致 100ms 内无法发送。

- 如果直接在 UART ISR 中等待 BLE 发送完成 → **中断嵌套过深，系统卡死**
- 如果 UART ISR 只接收不处理 → **新数据覆盖旧数据 → 丢包**

**RingBuffer 解决方案**:
```
┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│   UART ISR  │────▶│  RingBuffer │────▶│ Work Queue  │
│  (快速接收)  │     │  (临时存储) │     │ (慢慢发送)  │
└─────────────┘     └─────────────┘     └─────────────┘
```

ISR 只需 5us 将数据放入 RingBuffer，立即返回处理下一个字节。工作队列从 RingBuffer 取出数据，耐心等待 BLE 协议栈就绪后再发送。

### 3.3 流控处理 (-EAGAIN / -ENOMEM)

`bt_gatt_notify` 并非总是成功。当 BLE 协议栈内部 Buffer 满时，会返回：

- **-EAGAIN**: 临时不可用，需要重试
- **-ENOMEM**: 内存不足

**错误处理策略**:
```c
int err = bt_gatt_notify(conn, data, len);
if (err == -EAGAIN || err == -ENOMEM) {
    // 协议栈忙，5ms 后重试
    k_work_schedule(&nus_send_work, K_MSEC(5));
} else if (err) {
    // 其他错误，记录日志
    LOG_ERR("NUS send failed (err=%d)", err);
}
```

---

## 4. 实现步骤详解

### 步骤 1: 定义 NUS 服务

**文件**: `nus.h`

```c
#define NUS_SERVICE_UUID_VAL \
    BT_UUID_128_ENCODE(0x6e400001, 0xb5a3, 0xf393, 0xe0a9, 0xe50e24dcca9e)

#define NUS_CHAR_RX_UUID_VAL \
    BT_UUID_128_ENCODE(0x6e400002, 0xb5a3, 0xf393, 0xe0a9, 0xe50e24dcca9e)

#define NUS_CHAR_TX_UUID_VAL \
    BT_UUID_128_ENCODE(0x6e400003, 0xb5a3, 0xf393, 0xe0a9, 0xe50e24dcca9e)
```

**关键点**:
- 使用 Nordic 官方定义的 NUS UUID
- RX 特征值支持 WRITE，手机发送数据
- TX 特征值支持 NOTIFY，设备推送数据

### 步骤 2: 实现 GATT 服务

**文件**: `nus.c`

```c
BT_GATT_SERVICE_DEFINE(nus_service,
    BT_GATT_PRIMARY_SERVICE(NUS_SERVICE_UUID),

    BT_GATT_CHARACTERISTIC(NUS_CHAR_RX_UUID,
                           BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_WRITE,
                           NULL, on_rx_write, NULL),

    BT_GATT_CHARACTERISTIC(NUS_CHAR_TX_UUID,
                           BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_NONE,
                           NULL, NULL, NULL),

    BT_GATT_CCC(on_cccd_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);
```

**关键点**:
- RX 特征值不需要 `user_data`，因为我们只是透传
- TX 特征值挂载 CCCD，允许手机开启通知
- 必须在 WRITE 回调中接收手机发来的数据

### 步骤 3: 接收手机数据并转发到 UART

**文件**: `nus.c`

```c
static ssize_t on_rx_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                           const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
    const uint8_t *data = buf;
    LOG_INF("Received data from phone: len=%d", len);

    nus_data_received((uint8_t *)data, len);

    return len;
}
```

**文件**: `main.c`

```c
void nus_data_received(uint8_t *data, uint16_t len)
{
    int err = send_uart_data(data, len);
    if (err == -EBUSY) {
        // UART 忙，工作队列稍后重试
        k_work_schedule(&nus_send_work, K_MSEC(1));
    }
}
```

### 步骤 4: 配置 UART 中断与 RingBuffer

**文件**: `main.c`

```c
#define RX_BUF_SIZE 256
#define RING_BUFFER_SIZE 1024

RING_BUFFER_DECLARE(rx_ringbuf, RING_BUFFER_SIZE);

static uint8_t uart_rx_buf[RX_BUF_SIZE];

static void uart_rx_buf_cb(const struct device *dev, uint8_t *buf, size_t len,
                           int error)
{
    // 将接收到的每个字节放入 RingBuffer
    for (size_t i = 0; i < len; i++) {
        uint32_t ret = ring_buffer_put(&rx_ringbuf, &buf[i], 1);
        if (ret == 0) {
            LOG_WRN("Ring buffer full, dropping byte 0x%02x", buf[i]);
        }
    }
}
```

**关键点**:
- UART 配置为中断驱动模式
- 每次接收满 64 字节（或超时）触发回调
- 回调中立即将数据存入 RingBuffer，ISR 快速返回

### 步骤 5: 实现工作队列发送机制

**文件**: `main.c`

```c
static struct k_work_delayable nus_send_work;

static void nus_send_work_handler(struct k_work *work)
{
    uint8_t buffer[247];  // 最大 MTU
    uint16_t buffer_len = 0;

    // 从 RingBuffer 取数据
    uint32_t ret = ring_buffer_get(&rx_ringbuf, buffer, sizeof(buffer), &buffer_len);
    if (ret == 0 || buffer_len == 0) {
        return;
    }

    int err = nus_send(current_conn, buffer, buffer_len);
    if (err == -EAGAIN || err == -ENOMEM) {
        // BLE 栈忙，5ms 后重试
        k_work_schedule(&nus_send_work, K_MSEC(5));
    } else if (err) {
        LOG_ERR("NUS send failed (err=%d)", err);
    }
}
```

**关键点**:
- 工作队列在系统线程中运行，不阻塞中断
- 每次取最多 20 字节（nRF52832 MTU=23, ATT 头部占 3 字节）
- 发送失败时使用 `k_work_schedule` 延迟重试

### 步骤 6: 主循环检测数据

**文件**: `main.c`

```c
while (1) {
    k_sleep(K_MSEC(10));

    // 检查 RingBuffer 是否有数据
    uint8_t dummy_buf[1];
    uint16_t dummy_len = 0;
    uint32_t ret = ring_buffer_get(&rx_ringbuf, dummy_buf, sizeof(dummy_buf), &dummy_len);
    if (ret != 0 && dummy_len > 0) {
        // 有数据，触发工作队列发送
        k_work_schedule(&nus_send_work, K_NO_WAIT);
    }
}
```

**关键点**:
- 每 10ms 检查一次 RingBuffer
- 如果有数据，触发异步发送
- 工作队列确保发送不会阻塞主循环

### 步骤 7: UART TX 流控

**文件**: `main.c`

```c
static atomic_t uart_busy = ATOMIC_INIT(0);

static void uart_tx_done(const struct device *dev, int error)
{
    atomic_set(&uart_busy, 0);
}

static int send_uart_data(const uint8_t *data, uint16_t len)
{
    if (atomic_test_and_set(&uart_busy)) {
        return -EBUSY;
    }

    return uart_tx(uart_dev, data, len, SYS_FOREVER_MS);
}
```

**关键点**:
- 使用 atomic 变量标记 UART 是否正在发送
- 避免在回调完成前再次调用 `uart_tx`
- TX 完成后在回调中清除 busy 标志

---

## 5. 关键 API 参考

| API | 功能 | 使用场景 |
|-----|------|----------|
| `RING_BUFFER_DECLARE` | 声明 Ring Buffer | 声明静态环形缓冲区 |
| `ring_buffer_put` | 放入数据 | UART ISR 中快速存储接收数据 |
| `ring_buffer_get` | 取出数据 | 工作队列中批量取出发送 |
| `k_work_init_delayable` | 初始化延迟工作 | 初始化可延迟执行的工作项 |
| `k_work_schedule` | 调度工作 | 触发异步任务执行 |
| `uart_callback_set` | 设置 UART 回调 | 配置 TX/RX 完成回调 |
| `uart_rx_enable` | 启用 RX 中断 | 启动 UART 接收 |
| `atomic_test_and_set` | 原子操作 | UART TX 流控标志位操作 |
| `bt_gatt_notify` | 发送通知 | BLE 数据发送 |
| `bt_gatt_is_subscribed` | 检查订阅状态 | 发送前验证客户端是否已订阅 |

---

## 6. 踩坑与经验总结

### 坑 1: RingBuffer 溢出

**问题**: 高速数据灌入时，RingBuffer 很快写满，导致数据丢失

**解决**:
- 根据实际吞吐量调整 `RING_BUFFER_SIZE`（Day6 使用 1024 字节）
- 在 `ring_buffer_put` 失败时记录日志，便于监控
- 考虑使用更复杂的流控（如 XON/XOFF），但 BLE 场景通常不需要

### 坑 2: UART TX 阻塞

**问题**: 直接在回调中调用 `uart_tx` 发送大块数据，导致回调执行时间过长

**解决**:
- 使用 `uart_tx` 的异步模式（timeout = `SYS_FOREVER_MS`）
- 通过 `UART_TX_DONE` 回调通知发送完成
- 使用 atomic 变量防止重复调用

### 坑 3: BLE 发送阻塞 ISR

**问题**: 在 `uart_rx_buf_cb` 中直接调用 `nus_send`，可能导致 HardFault

**原因**: `bt_gatt_notify` 可能获取 Mutex，而中断上下文不允许

**解决**: ISR 中只做 `ring_buffer_put`，发送逻辑放在 Work Queue 中执行

### 坑 4: -EAGAIN 死循环

**问题**: BLE 协议栈一直返回 -EAGAIN，工作队列无限重试

**解决**: 添加重试间隔（5ms），避免占用过多 CPU 资源。正常情况下，BLE 栈会在几毫秒内恢复

### 坑 5: 设备树 UART 冲突

**问题**: `DT_CHOSEN(zephyr_shell_uart)` 与 `CONFIG_UART_CONSOLE=y` 冲突

**解决**: 在 `prj.conf` 中禁用 Console UART：
```conf
CONFIG_UART_CONSOLE=n
```

---

## 7. 验收标准

### ✅ 实际测试结果

| 测试场景 | 结果 | 说明 |
|---------|------|------|
| 7字节/20ms | ✅ 正常，无丢包 | 稳定传输 |
| 8字节/20ms | ⚠️ 不稳定 | 触发边界条件 |
| 7字节/15ms | ❌ 开始丢包 | 超过连接间隔限制 |
| 手机→设备 | ✅ 正常 | BLE RX透传正常 |
| 设备→手机 | ✅ 正常 | BLE TX透传正常 |

### 📊 理论极限分析

#### 7.1 BLE传输物理限制

**当前配置参数**：
```c
连接间隔 = 24ms (从日志: interval=24)
MTU = 23字节 (默认，未进行MTU交换)
有效载荷 = MTU - 3(ATT头) = 20字节
```

**理论吞吐量计算**：
```
单次连接事件最大数据 = 20字节
连接间隔 = 24ms = 0.024秒

理论最大吞吐量 = 20字节 / 0.024秒 = 833 字节/秒
```

#### 7.2 实际测试分析

**测试1：7字节/20ms（稳定）**
```
发送速率 = 7字节 / 20ms = 350 字节/秒
利用率 = 350 / 833 = 42%
```
✅ **结论**：在理论极限的42%负载下，系统稳定运行

**测试2：7字节/15ms（丢包）**
```
发送速率 = 7字节 / 15ms = 467 字节/秒
利用率 = 467 / 833 = 56%
```
❌ **结论**：超过连接间隔限制，RingBuffer积压导致BLE协议栈Buffer溢出

**时间线分析**：
```
0ms   ---- UART收到7字节 ----
15ms  ---- UART收到7字节 ---- (前一个还没发完)
24ms  === BLE传输机会 === (只能发一个)
30ms  ---- UART收到7字节 ----
45ms  ---- UART收到7字节 ---- (三个都在排队)
48ms  === BLE传输机会 === (仍然只能发一个)
```
**问题**：RingBuffer积压，触发流控（-EAGAIN），但来不及处理导致丢包

#### 7.3 BLE缓冲区容量分析

**prj.conf配置**：
```conf
CONFIG_BT_L2CAP_TX_MTU=247          # L2CAP层MTU
CONFIG_BT_BUF_ACL_TX_SIZE=251         # 单个ACL包大小
CONFIG_BT_BUF_ACL_TX_COUNT=10         # ACL TX缓冲区数量
CONFIG_BT_L2CAP_TX_BUF_COUNT=10      # L2CAP TX缓冲区数量
```

**总TX缓冲区容量**：
```
ACL层缓冲区 = 10 × 251 = 2510字节
L2CAP层缓冲区 = 10 × 247 = 2470字节
```

**缓冲区溢出时间**（假设UART以115200波特率接收）：
```
UART接收速度 = 115200 / 10 = 11520 字节/秒
BLE发送速度 = 833 字节/秒
净积压速度 = 11520 - 833 = 10687 字节/秒

缓冲区填满时间 = 2510 / 10687 ≈ 0.23秒
```

**结论**：在高速数据流下，缓冲区会在0.23秒内填满，必须依赖流控机制。

#### 7.4 RingBuffer容量分析

**代码配置**：
```c
#define UART_BUF_SIZE 1024   // UART接收环形缓冲区大小
RING_BUF_DECLARE(uart_ring_buf, UART_BUF_SIZE);
```

**RingBuffer容量**：
```
容量 = 1024字节
填满时间 = 1024 / 10687 ≈ 0.1秒
```

**结论**：RingBuffer容量合理，能在BLE缓冲区填满前提供临时存储。

#### 7.5 性能瓶颈总结

| 瓶颈 | 当前值 | 理论极限 | 优化方向 |
|------|--------|---------|---------|
| 连接间隔 | 24ms | 7.5ms | 主动协商参数 |
| MTU | 23字节 | 247字节 | Day7 MTU交换 |
| 有效载荷 | 20字节 | 244字节 | Day7 DLE |
| 吞吐量 | 833 B/s | 32 KB/s | Day7完整优化 |

### 7.6 验收结论

**✅ Day6核心要求：通过**

| 验收项 | 状态 | 说明 |
|--------|------|------|
| 编译通过，无警告 | ✅ | 代码质量良好 |
| 手机识别NUS服务 | ✅ | UUID正确 |
| 手机Enable TX Notify | ✅ | CCCD工作正常 |
| 电脑→手机透传 | ✅ | 7字节/20ms稳定 |
| 手机→电脑透传 | ✅ | 双向通信正常 |
| 流控机制 | ✅ | -EAGAIN正确处理 |
| 系统不崩溃 | ✅ | 异步解耦成功 |

**⚠️ 性能边界：符合预期**

- 7字节/20ms（350 B/s）稳定运行
- 15ms间隔开始丢包（符合物理限制）
- 8字节偶发不稳定（边界条件）

**🎓 学习价值**：

本次测试完美验证了：
1. **BLE异步特性**：连接间隔决定传输频率，无法通过软件突破
2. **RingBuffer作用**：解耦高速UART接收和低速BLE发送
3. **流控重要性**：-EAGAIN/-ENOMEM处理是系统稳定的保障
4. **物理限制理解**：833 B/s是当前配置下的理论极限

**🚀 优化建议**（Day7任务）：

1. **MTU交换**：从23字节提升到247字节
   - 有效载荷：20字节 → 244字节（12.2倍提升）
   
2. **DLE优化**：链路层数据长度从27字节扩展到251字节
   
3. **连接参数协商**：请求更短的连接间隔
   - 从24ms降到7.5ms（3.2倍提升）
   
4. **理论极限**（Day7目标）：
   ```
   吞吐量 = 244字节 / 0.0075秒 = 32.5 KB/s
   ```

---

## 8. 扩展测试

### 测试 1: 极限吞吐量

```bash
# 电脑端用 Python 脚本疯狂发数据
python -c "
import serial
ser = serial.Serial('COM3', 115200)
while True:
    ser.write(b'X' * 1000)
"
```

**观察**:
- RTT Log 显示 RingBuffer 状态
- Wireshark 抓包查看 BLE 吞吐率

### 测试 2: 长时间稳定性

运行 30 分钟，期间反复连接/断开，观察是否丢包或崩溃。

---

## 9. 学习总结

Day 6 完成了从"字符通知"到"高速透传"的升级。通过 NUS + RingBuffer + Work Queue 的组合，我们掌握了：

1. **NUS 服务**: 理解标准 BLE 透传服务的结构和 UUID
2. **RingBuffer**: 解决生产者（UART ISR）与消费者（BLE 发送）速率不匹配的问题
3. **异步解耦**: 使用 Work Queue 将耗时操作从 ISR 剥离，确保系统实时性
4. **流控处理**: 正确处理 BLE 协议栈 Buffer 满的情况

**核心知识点**:
- **ISR 安全**: 永远不要在中断上下文中调用可能阻塞的 API
- **生产者-消费者模型**: RingBuffer 是解决速率不匹配的标准方案
- **工作队列**: Zephyr 的线程间异步通信机制

---

**Next Step**: Day 7 - 吞吐量优化与 MTU/DLE
