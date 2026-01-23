#include <zephyr/types.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>

/*
 * 1. 定义广播名称
 * 
 * 宏: CONFIG_BT_DEVICE_NAME
 * 功能: Kconfig 中定义的设备名称，默认为 "Zephyr"
 *      可以在 prj.conf 中通过 CONFIG_BT_DEVICE_NAME="MyBLE" 修改
 * 
 * 宏: sizeof(str)
 * 功能: 返回字符串的字节长度，包括结尾的 '\0'
 */
#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

/*
 * 2. 自定义 Manufacturer Data (厂商数据)
 * 
 * 格式: [Company ID low byte] [Company ID high byte] [Data...]
 * 
 * Company ID:
 * - 0xFFFF 是测试用的 Company ID (Bluetooth SIG 保留)
 * - 实际产品需要向 Bluetooth SIG 申请正式的 Company ID
 * 
 * Data:
 * - 0x41, 0x43, 0x45 对应 ASCII 码的 "ACE"
 * - 可以放入任意自定义数据，最多 28 字节(因为广播包总共 31 字节)
 * 
 * 宏: BT_DATA_BYTES(type, ...)
 * 功能: 构造一个包含字节数据的 bt_data 结构体
 * 参数1: type - 数据类型，BT_DATA_MANUFACTURER_DATA 表示厂商特定数据
 * 参数2...: 可变参数，直接写字节值，如 0xFF, 0xFF, 0x41, 0x43, 0x45
 */
static const struct bt_data ad[] = {
    /*
     * Flags: 广播标志位
     * 
     * 宏: BT_DATA_BYTES(type, ...)
     * 功能: 同上，构造字节数据
     * 
     * 标志位组合:
     * - BT_LE_AD_GENERAL: 一般可发现模式
     *   表示设备可以被任何设备扫描到，不受限
     * - BT_LE_AD_NO_BREDR: 不支持经典蓝牙
     *   表示设备只支持 BLE，不支持 BR/EDR (经典蓝牙)
     * 
     * 组合使用: (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)
     * 表示: 一般可发现 + 仅 BLE
     */
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    
    /*
     * Name: 广播全名
     * 
     * 宏: BT_DATA(type, data, data_len)
     * 功能: 构造一个包含指针数据的 bt_data 结构体
     * 参数1: type - 数据类型，BT_DATA_NAME_COMPLETE 表示完整的设备名称
     * 参数2: data - 数据指针，指向字符串或字节数组
     * 参数3: data_len - 数据长度
     * 
     * 注意: 如果名字太长(超过 31 字节)，需要使用 BT_DATA_NAME_SHORT(短名称)
     */
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),

    /*
     * Manufacturer Data: 厂商特定数据
     * 
     * 类型: BT_DATA_MANUFACTURER_DATA (0xFF)
     * 数据: 0xFF 0xFF 0x41 0x43 0x45
     *       - 0xFF 0xFF: Company ID (测试用)
     *       - 0x41 0x43 0x45: "ACE" (自定义数据)
     */
    BT_DATA_BYTES(BT_DATA_MANUFACTURER_DATA, 0xFF, 0xFF, 0x41, 0x43, 0x45),
};

/*
 * 3. 扫描响应数据 (Scan Response Data)
 * 
 * 当手机"主动扫描"(Active Scanning)时，设备会回复这个包
 * 
 * 用途:
 * - 广播包(ad)只有 31 字节，如果放不下所有数据，可以把部分数据放在这里
 * - 通常放 128-bit UUID、设备名称等
 * 
 * 本例中暂时留空，或者可以放个短名
 */
static const struct bt_data sd[] = {
    // 暂时留空
};

int main(void)
{
    int err;

    printk("Starting Advertising Demo\n");

    /*
     * 初始化蓝牙协议栈
     * 
     * API: bt_enable(callback)
     * 功能: 初始化蓝牙控制器和主机协议栈
     * 参数: callback - 初始化完成回调函数指针，NULL 表示同步初始化
     *        如果传入 NULL，函数会阻塞直到初始化完成
     *        如果传入回调函数，初始化是异步的
     * 返回值: 0 表示成功，负数表示错误码
     * 
     * 注意: 必须在任何蓝牙 API 调用之前调用此函数
     */
    err = bt_enable(NULL);
    if (err) {
        printk("Bluetooth init failed (err %d)\n", err);
        return 0;
    }

    printk("Bluetooth initialized\n");

    /*
     * 4. 开始广播
     * 
     * API: bt_le_adv_start(param, ad, ad_len, sd, sd_len)
     * 功能: 启动 LE 广播
     * 参数1: param - 广播参数，BT_LE_ADV_CONN 表示可连接广播
     *        BT_LE_ADV_CONN_NAME: 可连接 + 自动包含设备名
     *        BT_LE_ADV_CONN: 仅可连接，不自动包含名字
     *        注意: 如果手动在 ad[] 中添加了名字，不要用 BT_LE_ADV_CONN_NAME
     * 参数2: ad - 广播数据数组指针
     * 参数3: ad_len - 广播数据数组长度
     * 参数4: sd - 扫描响应数据数组指针
     * 参数5: sd_len - 扫描响应数据数组长度
     * 返回值: 0 表示成功，负数表示错误码
     * 
     * 宏: ARRAY_SIZE(arr)
     * 功能: 计算数组的元素个数
     * 参数: arr - 数组名
     * 返回值: 数组元素个数
     * 
     * 常见错误码:
     * - -22 (EINVAL): 参数错误，通常是 ad[] 中重复添加了名字
     */
    err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    
    // 如果你想手动控制参数（例如省电），可以用 struct bt_le_adv_param
    // err = bt_le_adv_start(BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONNECTABLE, 
    //                       BT_GAP_ADV_FAST_INT_MIN_2, BT_GAP_ADV_FAST_INT_MAX_2, NULL), 
    //                       ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));

    if (err) {
        printk("Advertising failed to start (err %d)\n", err);
        return 0;
    }

    printk("Advertising successfully started\n");

    /*
     * 主线程可以去睡大觉了，蓝牙在 Controller 线程运行
     * 
     * API: k_sleep(timeout)
     * 功能: 使当前线程休眠指定时间
     * 参数: timeout - 休眠时间，K_SECONDS(1) 表示 1 秒，K_FOREVER 表示永久休眠
     * 
     * 注意: 这是一个死循环，防止 main 函数返回
     */
    for (;;) {
        k_sleep(K_SECONDS(1));
    }
    return 0;
}
