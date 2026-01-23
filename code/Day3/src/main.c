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

/*
 * 定义设备名称
 * 
 * 宏: CONFIG_BT_DEVICE_NAME
 * 功能: Kconfig 中定义的设备名称，默认为 "Zephyr"
 *      可以在 prj.conf 中通过 CONFIG_BT_DEVICE_NAME="MyBLE" 修改
 */
#define DEVICE_NAME             "MyBLE"
#define DEVICE_NAME_LEN         (sizeof(DEVICE_NAME) - 1)

/*
 * 获取设备树中别名为 led0 的节点
 * 
 * 宏: DT_ALIAS(alias)
 * 功能: 获取设备树中指定别名的节点标识符
 * 参数: alias - 别名名称，如 "led0"
 * 返回值: 节点标识符，可用于后续的 DT_* 宏
 * 
 * 宏: DT_NODE_HAS_STATUS(node, status)
 * 功能: 编译时检查节点是否存在且状态为指定值
 * 参数1: node - 节点标识符
 * 参数2: status - 状态值，"okay" 表示节点可用
 * 返回值: 1 表示条件满足，0 表示不满足
 * 
 * 宏: #error message
 * 功能: 编译时输出错误信息并停止编译
 * 用途: 强制要求设备树中必须包含 led0 节点
 */
#define LED0_NODE DT_ALIAS(led0)

#if !DT_NODE_HAS_STATUS(LED0_NODE, okay)
#error "Unsupported board: led0 devicetree alias is not defined"
#endif

/*
 * 获取 GPIO 规格
 * 
 * API: GPIO_DT_SPEC_GET(node_id, prop)
 * 功能: 从设备树中获取 GPIO 设备规格信息
 * 参数1: node_id - 设备树节点标识符
 * 参数2: prop - 要获取的属性，gpios 表示 GPIO 引脚配置
 * 返回值: struct gpio_dt_spec - 包含端口设备指针、引脚号、标志位等信息
 */
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

/*
 * 定义连接参数
 * 
 * 宏: BT_LE_CONN_PARAM(min, max, latency, timeout)
 * 功能: 创建连接参数结构体
 * 参数1: min - 最小连接间隔 (单位 1.25ms)
 * 参数2: max - 最大连接间隔 (单位 1.25ms)
 * 参数3: latency - 从设备延迟 (Slave Latency)，允许跳过的连接事件数
 * 参数4: timeout - 监督超时 (单位 10ms)
 * 
 * 本例参数:
 * - Interval: 20ms (16 * 1.25 = 20ms)
 * - Latency: 0 (不跳过任何连接事件)
 * - Timeout: 400ms (40 * 10 = 400ms)
 * 
 * 注意: 连接参数需要双方协商，实际值可能不同
 */
static struct bt_le_conn_param *my_conn_params = BT_LE_CONN_PARAM(16, 16, 0, 40);

/*
 * 全局变量：保存当前连接句柄
 * 
 * struct bt_conn: 连接对象结构体，由蓝牙协议栈管理
 * 用途: 保存当前活动的连接，用于后续操作(如发送数据、更新参数)
 */
static struct bt_conn *current_conn;

/* ----------------前向声明---------------- */

static void start_advertising(void);

/* ----------------工作项：5秒后更新参数---------------- */

/*
 * 工作项结构体
 * 
 * struct k_work_delayable: 可延时的工作项，用于在指定时间后执行任务
 * 用途: 在连接建立 5 秒后，请求更新连接参数
 * 
 * 工作队列机制:
 * - Zephyr 使用工作队列来处理需要在上下文之外执行的任务
 * - 延时工作项可以指定延迟时间，时间到后自动提交到系统工作队列
 */
struct k_work_delayable update_params_work;

/*
 * 工作项处理函数
 * 
 * 原型: void work_handler(struct k_work *work)
 * 参数: work - 指向触发的工作项结构体
 * 
 * API: k_work_cancel_delayable(work)
 * 功能: 取消一个已调度但未执行的延时工作项
 * 参数: work - 指向工作项结构体
 * 返回值: 0 表示成功取消，负数表示工作项正在执行或已完成
 */
static void update_params_handler(struct k_work *work)
{
    int err;
    if (!current_conn) {
        return;
    }

    printk("Work Triggered: Requesting Connection Param Update...\n");

    /*
     * 发起参数更新请求
     * 
     * API: bt_conn_le_param_update(conn, param)
     * 功能: 请求更新连接参数
     * 参数1: conn - 连接对象指针
     * 参数2: param - 指向连接参数结构体
     * 返回值: 0 表示请求已发送，负数表示错误
     * 
     * 注意: 此函数只是发送请求，实际参数更新由双方协商决定
     *       协商结果会通过 le_param_updated 回调通知
     */
    err = bt_conn_le_param_update(current_conn, my_conn_params);
    if (err) {
        printk("Connection param update failed: %d\n", err);
    } else {
        printk("Connection param update requested success.\n");
    }
}

/*
 * 连接建立回调
 * 
 * 原型: void connected(struct bt_conn *conn, uint8_t err)
 * 参数1: conn - 新建立的连接对象指针
 * 参数2: err - 错误码，0 表示成功，非 0 表示连接失败
 * 
 * API: bt_conn_ref(conn)
 * 功能: 增加连接对象的引用计数
 * 参数: conn - 连接对象指针
 * 返回值: 连接对象指针(与输入相同)
 * 
 * 引用计数机制:
 * - 防止连接对象在使用过程中被协议栈释放
 * - 每次引用需要调用 bt_conn_ref，使用完后调用 bt_conn_unref
 * - 引用计数为 0 时，协议栈才会释放连接对象
 */
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
    
    /*
     * 定义 PHY 参数结构体
     * 
     * struct bt_conn_le_phy_param: PHY 更新参数结构体
     * 成员:
     * - options: PHY 选项，通常设为 BT_CONN_LE_PHY_OPT_NONE
     * - pref_tx_phy: 首选的发送 PHY 模式
     * - pref_rx_phy: 首选的接收 PHY 模式
     * 
     * PHY 模式选项:
     * - BT_GAP_LE_PHY_1M: 1 Mbps 物理层
     * - BT_GAP_LE_PHY_2M: 2 Mbps 物理层 (Bluetooth 5.0)
     * - BT_GAP_LE_PHY_CODED: 编码 PHY (未来扩展)
     */
    const struct bt_conn_le_phy_param param = {
        .options = BT_CONN_LE_PHY_OPT_NONE,
        .pref_tx_phy = BT_GAP_LE_PHY_2M,
        .pref_rx_phy = BT_GAP_LE_PHY_2M,
    };

    /*
     * 发起 PHY 更新请求
     * 
     * API: bt_conn_le_phy_update(conn, param)
     * 功能: 请求更新连接的 PHY 模式
     * 参数1: conn - 连接对象指针
     * 参数2: param - 指向 PHY 参数结构体
     * 返回值: 0 表示请求已发送，负数表示错误
     * 
     * 注意: 此函数只是发送请求，实际 PHY 更新由双方协商决定
     *       协商结果会通过 le_phy_updated 回调通知
     */
    int phy_err = bt_conn_le_phy_update(conn, &param);
    
    if (phy_err) {
        printk("PHY update request failed: %d\n", phy_err);
    } else {
        printk("PHY update to 2M requested.\n");
    }

    /* 任务 2：启动 5 秒延时，准备修改连接间隔 */
    
    /*
     * API: k_work_schedule(work, delay)
     * 功能: 调度一个工作项在指定时间后执行
     * 参数1: work - 指向工作项结构体
     * 参数2: delay - 延迟时间，K_SECONDS(5) 表示 5 秒
     * 返回值: 0 表示成功，负数表示工作项已在队列中
     * 
     * 注意: 延时工作项会自动提交到系统工作队列
     */
    k_work_schedule(&update_params_work, K_SECONDS(5));
}

/*
 * 连接断开回调
 * 
 * 原型: void disconnected(struct bt_conn *conn, uint8_t reason)
 * 参数1: conn - 已断开的连接对象指针
 * 参数2: reason - 断开原因码
 * 
 * API: bt_conn_unref(conn)
 * 功能: 减少连接对象的引用计数
 * 参数: conn - 连接对象指针
 * 返回值: 无
 * 
 * 注意: 必须与 bt_conn_ref 配对使用，避免内存泄漏
 */
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

/*
 * 监听连接参数更新结果的回调
 * 
 * 原型: void le_param_updated(struct bt_conn *conn, uint16_t interval, uint16_t latency, uint16_t timeout)
 * 参数1: conn - 连接对象指针
 * 参数2: interval - 新的连接间隔 (单位 1.25ms)
 * 参数3: latency - 新的从设备延迟
 * 参数4: timeout - 新的监督超时 (单位 10ms)
 * 
 * 注意: 此回调在参数协商成功后调用，表示双方已达成一致
 */
static void le_param_updated(struct bt_conn *conn, uint16_t interval,
                             uint16_t latency, uint16_t timeout)
{
    printk("Connection params updated: interval %d (%.2f ms), latency %d, timeout %d\n",
           interval, interval * 1.25, latency, timeout * 10);
}

/*
 * 监听 PHY 更新结果的回调
 * 
 * 原型: void le_phy_updated(struct bt_conn *conn, struct bt_conn_le_phy_info *param)
 * 参数1: conn - 连接对象指针
 * 参数2: param - 指向 PHY 信息结构体
 * 
 * struct bt_conn_le_phy_info:
 * - tx_phy: 发送 PHY 模式
 * - rx_phy: 接收 PHY 模式
 * - phy_pref: 首选的 PHY 模式
 * 
 * PHY 模式值:
 * - BT_GAP_LE_PHY_1M: 1 Mbps
 * - BT_GAP_LE_PHY_2M: 2 Mbps
 * - BT_GAP_LE_PHY_CODED: 编码 PHY
 */
static void le_phy_updated(struct bt_conn *conn,
                           struct bt_conn_le_phy_info *param)
{
    printk("PHY updated: TX %dM, RX %dM\n", 
           (param->tx_phy == BT_GAP_LE_PHY_2M) ? 2 : 1,
           (param->rx_phy == BT_GAP_LE_PHY_2M) ? 2 : 1);
}

/*
 * 注册回调结构体
 * 
 * 宏: BT_CONN_CB_DEFINE(name)
 * 功能: 定义并注册一个连接回调结构体
 * 参数: name - 回调结构体名称
 * 
 * struct bt_conn_cb:
 * - connected: 连接建立回调
 * - disconnected: 连接断开回调
 * - le_param_updated: 连接参数更新回调
 * - le_phy_updated: PHY 更新回调
 * - ... (其他可选回调)
 * 
 * 注意: 此宏利用链接器脚本，将结构体注册到协议栈的回调列表中
 */
BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
    .le_param_updated = le_param_updated,
    .le_phy_updated = le_phy_updated, 
};

/* ----------------广播配置---------------- */

/*
 * 广播数据数组
 * 
 * 注意: 这里使用 BT_LE_ADV_CONN 而不是 BT_LE_ADV_CONN_NAME
 * 原因: 我们手动在 ad[] 中添加了名字，避免重复导致错误 -22
 */
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

/*
 * 扫描响应数据数组
 * 
 * 注意: 这里使用复合字面量 ((unsigned char[]){...}) 来初始化数据
 *       因为 BT_DATA 宏需要指针参数
 */
static const struct bt_data sd[] = {
    BT_DATA(BT_DATA_MANUFACTURER_DATA, ((unsigned char[]){0xFF, 0xFF, 'A', 'C', 'E'}), 5),
};

/*
 * 启动广播函数
 * 
 * API: bt_le_adv_start(param, ad, ad_len, sd, sd_len)
 * 功能: 启动 LE 广播
 * 参数1: param - 广播参数，BT_LE_ADV_CONN 表示可连接广播
 * 参数2: ad - 广播数据数组指针
 * 参数3: ad_len - 广播数据数组长度
 * 参数4: sd - 扫描响应数据数组指针
 * 参数5: sd_len - 扫描响应数据数组长度
 * 返回值: 0 表示成功，负数表示错误码
 * 
 * 注意: 修正使用 BT_LE_ADV_CONN，避免与手动指定的 Name 冲突
 */
static void start_advertising(void)
{
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

    /*
     * 1. 检查 GPIO 控制器是否就绪
     * 
     * API: gpio_is_ready_dt(spec)
     * 功能: 检查 GPIO 设备是否已初始化并就绪
     * 参数: spec - GPIO 设备规格结构体指针
     * 返回值: true 表示就绪，false 表示未就绪
     */
    if (!gpio_is_ready_dt(&led)) {
        printk("Error: LED GPIO controller is not ready\n");
        return 0;
    }

    /*
     * 2. 配置为输出，且初始状态为非激活（灭）
     * 
     * API: gpio_pin_configure_dt(spec, flags)
     * 功能: 配置 GPIO 引脚的方向和初始状态
     * 参数1: spec - GPIO 设备规格结构体指针
     * 参数2: flags - 配置标志位
     *         GPIO_OUTPUT_INACTIVE - 输出模式，初始状态为非激活(低电平)
     * 返回值: 0 表示成功，负数表示错误码
     */
    ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) {
        printk("Error: Failed to configure LED pin\n");
        return 0;
    }

    /*
     * 初始化工作队列
     * 
     * API: k_work_init_delayable(work, handler)
     * 功能: 初始化一个可延时的工作项
     * 参数1: work - 指向工作项结构体
     * 参数2: handler - 工作项处理函数指针
     * 
     * 注意: 必须在使用工作项之前调用此函数
     */
    k_work_init_delayable(&update_params_work, update_params_handler);

    /*
     * 初始化蓝牙栈
     * 
     * API: bt_enable(callback)
     * 功能: 初始化蓝牙控制器和主机协议栈
     * 参数: callback - 初始化完成回调函数指针，NULL 表示同步初始化
     * 返回值: 0 表示成功，负数表示错误码
     */
    err = bt_enable(NULL);
    if (err) {
        printk("Bluetooth init failed (err %d)\n", err);
        return 0;
    }

    printk("Bluetooth initialized\n");

    start_advertising();

    return 0;
}
