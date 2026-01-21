#include <zephyr/types.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

/* ----------------配置区域---------------- */
#define DEVICE_NAME             "MyBLE"
#define DEVICE_NAME_LEN         (sizeof(DEVICE_NAME) - 1)
#define LED0_NODE DT_ALIAS(led0)

/* 编译时检查设备树中是否有 led0 */
#if !DT_NODE_HAS_STATUS(LED0_NODE, okay)
#error "Unsupported board: led0 devicetree alias is not defined"
#endif

/* 获取 GPIO 规格 */
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

/* 
 * 定义连接参数
 * Interval: 20ms (单位 1.25ms -> 16 * 1.25 = 20ms)
 * Latency: 0
 * Timeout: 400ms (单位 10ms -> 40 * 10 = 400ms)
 */
static struct bt_le_conn_param *my_conn_params = BT_LE_CONN_PARAM(16, 16, 0, 40);

/* 全局变量：保存当前连接句柄 */
static struct bt_conn *current_conn;

/* ----------------前向声明---------------- */
static void start_advertising(void);

/* ----------------工作项：5秒后更新参数---------------- */
struct k_work_delayable update_params_work;

static void update_params_handler(struct k_work *work)
{
    int err;
    if (!current_conn) {
        return;
    }

    printk("Work Triggered: Requesting Connection Param Update...\n");

    /* 发起参数更新请求 */
    err = bt_conn_le_param_update(current_conn, my_conn_params);
    if (err) {
        printk("Connection param update failed: %d\n", err);
    } else {
        printk("Connection param update requested success.\n");
    }
}

static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        printk("Connection failed (err 0x%02x)\n", err);
        return;
    }

    printk("Connected!\n");

    /* 引用连接对象，防止被意外释放 */
    current_conn = bt_conn_ref(conn);

    /* 任务 1：点亮 LED */
    printk("LED ON\n");
    gpio_pin_set_dt(&led, 1);

    /* 任务 3：请求切换到 2M PHY (NCS v2.7.0 新写法) */
    /* 定义 PHY 参数结构体 */
    const struct bt_conn_le_phy_param param = {
        .options = BT_CONN_LE_PHY_OPT_NONE,
        .pref_tx_phy = BT_GAP_LE_PHY_2M,
        .pref_rx_phy = BT_GAP_LE_PHY_2M,
    };

    /* 发起请求，注意这里只传 conn 和 结构体指针 */
    int phy_err = bt_conn_le_phy_update(conn, &param);
    
    if (phy_err) {
        printk("PHY update request failed: %d\n", phy_err);
    } else {
        printk("PHY update to 2M requested.\n");
    }

    /* 任务 2：启动 5 秒延时，准备修改连接间隔 */
    k_work_schedule(&update_params_work, K_SECONDS(5));
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    printk("Disconnected (reason 0x%02x)\n", reason);

    if (current_conn) {
        bt_conn_unref(current_conn);
        current_conn = NULL;
    }

    /* 任务 1：熄灭 LED */
    printk("LED OFF\n");
    gpio_pin_set_dt(&led, 0);

    /* 取消未执行的工作项 */
    k_work_cancel_delayable(&update_params_work);

    /* 断开后重新广播，保持设备可见 */
    start_advertising();
}

/* 监听连接参数更新结果的回调 */
static void le_param_updated(struct bt_conn *conn, uint16_t interval,
                             uint16_t latency, uint16_t timeout)
{
    printk("Connection params updated: interval %d (%.2f ms), latency %d, timeout %d\n",
           interval, interval * 1.25, latency, timeout * 10);
}

/* 监听 PHY 更新结果的回调 */
static void le_phy_updated(struct bt_conn *conn,
                           struct bt_conn_le_phy_info *param)
{
    printk("PHY updated: TX %dM, RX %dM\n", 
           (param->tx_phy == BT_GAP_LE_PHY_2M) ? 2 : 1,
           (param->rx_phy == BT_GAP_LE_PHY_2M) ? 2 : 1);
}

/* 注册回调结构体 */
BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
    .le_param_updated = le_param_updated,
    .le_phy_updated = le_phy_updated, 
};

/* ----------------广播配置---------------- */
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static const struct bt_data sd[] = {
    BT_DATA(BT_DATA_MANUFACTURER_DATA, ((unsigned char[]){0xFF, 0xFF, 'A', 'C', 'E'}), 5),
};

static void start_advertising(void)
{
    /* 修正：使用 BT_LE_ADV_CONN，避免与手动指定的 Name 冲突 */
    int err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err) {
        printk("Advertising failed to start (err %d)\n", err);
        return;
    }
    printk("Advertising successfully started\n");
}

/* ----------------主函数---------------- */
int main(void)
{
    int ret;
    int err;

    /* 1. 检查 GPIO 控制器是否就绪 */
    if (!gpio_is_ready_dt(&led)) {
        printk("Error: LED GPIO controller is not ready\n");
        return 0;
    }

    /* 2. 配置为输出，且初始状态为非激活（灭）*/
    ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) {
        printk("Error: Failed to configure LED pin\n");
        return 0;
    }

    /* 初始化工作队列 */
    k_work_init_delayable(&update_params_work, update_params_handler);

    /* 初始化蓝牙栈 */
    err = bt_enable(NULL);
    if (err) {
        printk("Bluetooth init failed (err %d)\n", err);
        return 0;
    }

    printk("Bluetooth initialized\n");

    start_advertising();

    return 0;
}