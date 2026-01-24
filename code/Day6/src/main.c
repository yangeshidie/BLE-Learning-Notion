/*
 * Day 6 Task: NUS Throughput & Ring Buffer Management
 * Author: BLE Learner
 * Environment: nRF52832, NCS v2.7.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/logging/log.h>

#include "nus.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* ----------------配置部分---------------- */
#define UART_BUF_SIZE     1024   // UART 接收环形缓冲区大小
#define BLE_MTU_MAX       247    // 期望的 MTU 大小 (需要在 prj.conf 中同时也配置)
#define WORK_RETRY_DELAY  K_MSEC(5) // 如果 BLE 缓冲区满，多久后重试

/* ----------------硬件定义---------------- */
/* 获取 Overlay 中定义的别名 */
static const struct gpio_dt_spec led_conn = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec led_act  = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);

/* 获取默认 Console UART 设备 (通常是 uart0) */
static const struct device *uart_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

/* ----------------全局变量---------------- */
/* 定义环形缓冲区 */
RING_BUF_DECLARE(uart_ring_buf, UART_BUF_SIZE);

/* 定义处理 BLE 发送的工作项 (Delayed Work 用于重试机制) */
static struct k_work_delayable ble_tx_work;

/* 当前 BLE 连接句柄 */
static struct bt_conn *current_conn;
static uint16_t current_mtu = 23; // 默认 MTU，连接后会更新

/* ----------------函数声明---------------- */
static void uart_cb(const struct device *dev, void *user_data);
static void ble_tx_work_handler(struct k_work *work);
/* 定义 MTU 交换参数和回调 */
static struct bt_gatt_exchange_params mtu_exchange_params = {
    .func = NULL,  // MTU交换完成后的回调
};

/* MTU 更新回调 */
static void mtu_exchange_cb(struct bt_conn *conn, uint8_t err,
                            struct bt_gatt_exchange_params *params)
{
    if (!err) {
        LOG_INF("MTU exchange completed successfully");
    } else {
        LOG_ERR("MTU exchange failed (err %d)", err);
    }
}

/* 连接参数更新回调 */
static void le_param_updated(struct bt_conn *conn, uint16_t interval,
                              uint16_t latency, uint16_t timeout)
{
    LOG_INF("Connection params updated: interval=%d, latency=%d, timeout=%d",
            interval, latency, timeout);
}
/* ============================================================
 *  UART 处理逻辑 (生产者)
 * ============================================================ */

/* UART 初始化 */
static int uart_init(void)
{
    if (!device_is_ready(uart_dev)) {
        LOG_ERR("UART device not ready");
        return -1;
    }

    /* 配置中断回调 */
    uart_irq_callback_user_data_set(uart_dev, uart_cb, NULL);
    uart_irq_rx_enable(uart_dev);

    return 0;
}

/* UART 中断回调函数 */
static void uart_cb(const struct device *dev, void *user_data)
{
    uint8_t recv_buf[64];
    int recv_len;

    uart_irq_update(dev);

    if (uart_irq_rx_ready(dev)) {
        /* 从硬件 FIFO 读取数据 */
        recv_len = uart_fifo_read(dev, recv_buf, sizeof(recv_buf));
        
        if (recv_len > 0) {
            /* 
             * 关键点：将数据放入 RingBuffer 
             * RingBuffer 是 ISR 和 WorkQueue 之间的桥梁
             */
            int written = ring_buf_put(&uart_ring_buf, recv_buf, recv_len);
            
            if (written < recv_len) {
                LOG_WRN("RingBuffer Full! Dropped %d bytes", recv_len - written);
                // 实际工程中这里可能需要更严重的错误处理
            }

            /* 触发 System Work Queue 进行 BLE 发送处理 */
            /* 使用 k_work_schedule 如果是 delayable work */
            k_work_schedule(&ble_tx_work, K_NO_WAIT);
            
            /* 闪烁 LED 表示有 Activity */
            gpio_pin_toggle_dt(&led_act);
        }
    }
}

/* ============================================================
 *  BLE 处理逻辑 (消费者)
 * ============================================================ */

/* 
 * 核心任务：从 RingBuffer 取数据发给 BLE
 * 包含了流控逻辑：如果 BLE 返回忙，则不消耗 Buffer，稍后重试。
 */
static void ble_tx_work_handler(struct k_work *work)
{
    uint8_t *data_ptr;
    uint32_t len;
    int err;

    if (!current_conn) {
        // 如果没有连接，丢弃缓冲区数据，防止溢出
        ring_buf_reset(&uart_ring_buf);
        return;
    }

    /* 循环处理，直到 Buffer 为空或 BLE 缓冲区满 */
    while (1) {
        /* 
         * 1. 获取 Buffer 中的数据指针（Claim），但不立即移除(Consume)
         *    这样如果发送失败，我们只需取消 Claim，数据还在 Buffer 中
         */
        len = ring_buf_get_claim(&uart_ring_buf, &data_ptr, current_mtu - 3);
        
        if (len == 0) {
            // Buffer 空了，退出任务
            ring_buf_get_finish(&uart_ring_buf, 0);
            break; 
        }

        /* 3. 尝试通过 BLE 发送 */
        err = my_nus_send(current_conn, data_ptr, len);

        if (err == -EAGAIN || err == -ENOMEM) {
            /* 
             * 重点流控逻辑：
             * 发送失败（资源不足），说明协议栈 Buffer 满了。
             * 1. 释放 Claim，告诉 RingBuffer 我们没消费任何数据 (size = 0)。
             * 2. 调度延时任务，稍后再试。
             */
            ring_buf_get_finish(&uart_ring_buf, 0); 
            LOG_DBG("BLE Stack Full, retrying later...");
            k_work_schedule(&ble_tx_work, WORK_RETRY_DELAY);
            break; // 跳出循环，让出 CPU
        } else if (err < 0) {
            /* 其他错误，可能是连接断开等，消费掉数据以免死循环 */
            LOG_ERR("BLE Send Error: %d", err);
            ring_buf_get_finish(&uart_ring_buf, len);
        } else {
            /* 
             * 发送成功：
             * 确认消费掉已发送的 len 长度
             */
            ring_buf_get_finish(&uart_ring_buf, len);
        }
    }
}

/* NUS 接收到手机数据回调 */
static void nus_received_cb(struct bt_conn *conn, const uint8_t *data, uint16_t len)
{
    /* 直接透传回 UART TX */
    /* 注意：uart_poll_out 是阻塞函数，高速大量数据时建议也用 RingBuffer + TX ISR */
    /* 但对于 Day 6 任务，RX (手机到设备) 数据量通常较小，此处简化处理 */
    for (int i = 0; i < len; i++) {
        uart_poll_out(uart_dev, data[i]);
    }
    gpio_pin_toggle_dt(&led_act);
}

/* NUS 初始化结构体 */
static struct my_nus_cb nus_callbacks = {
    .received = nus_received_cb,
    .send_enabled = NULL,
};

/* ============================================================
 *  BLE 连接管理
 * ============================================================ */


static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        LOG_ERR("Connection failed (err 0x%02x)", err);
    } else {
        LOG_INF("Connected");
        current_conn = bt_conn_ref(conn);
        gpio_pin_set_dt(&led_conn, 1); // LED 亮
    }
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    LOG_INF("Disconnected (reason 0x%02x)", reason);
    if (current_conn) {
        bt_conn_unref(current_conn);
        current_conn = NULL;
    }
    gpio_pin_set_dt(&led_conn, 0); // LED 灭
}

/* 3. 在连接回调中添加参数更新回调 */
BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
    .le_param_updated = le_param_updated,
};

/* ============================================================
 *  主函数
 * ============================================================ */

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, MY_NUS_UUID_SERVICE_VAL),
};

int main(void)
{
    int err;

    /* 1. 硬件初始化 */
    if (!gpio_is_ready_dt(&led_conn) || !gpio_is_ready_dt(&led_act)) {
        return 0;
    }
    gpio_pin_configure_dt(&led_conn, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&led_act, GPIO_OUTPUT_INACTIVE);
    
    err = uart_init();
    if (err) return 0;

    /* 初始化 WorkQueue 任务 */
    k_work_init_delayable(&ble_tx_work, ble_tx_work_handler);

    /* 2. BLE 初始化 */
    err = bt_enable(NULL);
    if (err) {
        LOG_ERR("Bluetooth init failed (err %d)", err);
        return 0;
    }
    err = my_nus_init(&nus_callbacks);
    if (err) {
        LOG_ERR("NUS init failed (err %d)", err);
        return 0;
    }

    LOG_INF("Bluetooth initialized, starting advertising...");

    /* 3. 开始广播 */
    err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err) {
        LOG_ERR("Advertising failed to start (err %d)", err);
        return 0;
    }

    /* 主循环空转，工作都在 ISR 和 WorkQueue 中 */
    while (1) {
        k_sleep(K_FOREVER);
    }
    return 0;
}