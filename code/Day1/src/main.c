#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

/*
 * 日志模块注册
 * API: LOG_MODULE_REGISTER(name, level)
 * 功能: 注册一个日志模块，用于后续使用 LOG_INF/LOG_ERR 等宏输出日志
 * 参数1: name - 模块名称，用于在日志中标识来源
 * 参数2: level - 默认日志级别 (LOG_LEVEL_DBG, LOG_LEVEL_INF, LOG_LEVEL_WRN, LOG_LEVEL_ERR)
 */
LOG_MODULE_REGISTER(day1_app, LOG_LEVEL_INF);

/*
 * 1. 硬件定义
 * 
 * API: GPIO_DT_SPEC_GET(node_id, prop)
 * 功能: 从设备树中获取 GPIO 设备规格信息，返回 struct gpio_dt_spec 结构体
 * 参数1: node_id - 设备树节点标识符，DT_ALIAS(led0) 表示查找别名为 led0 的节点
 * 参数2: prop - 要获取的属性，gpios 表示 GPIO 引脚配置
 * 
 * 返回值: struct gpio_dt_spec - 包含端口设备指针、引脚号、标志位等信息
 * 
 * 注意: 这里使用的是设备树别名机制，代码中不直接写死引脚号，而是通过 DTS 配置
 */
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec led2 = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);

/*
 * 定义线程栈大小和优先级
 * 
 * 栈大小: 1024 字节，用于存储线程的局部变量、函数调用栈等
 * 优先级: 7 (数值越小优先级越高，Zephyr 中 0 为最高优先级)
 * 
 * 注意: 栈大小需要根据实际使用情况调整，过小会导致栈溢出
 */
#define MY_STACK_SIZE 1024 
#define MY_PRIORITY 7

/*
 * 2. 线程处理函数 1 (控制红色 LED)
 * 
 * 线程函数原型: void thread_func(void *p1, void *p2, void *p3)
 * 参数 p1/p2/p3: 线程启动时传入的参数，本例中未使用
 * 
 * API: gpio_is_ready_dt(spec)
 * 功能: 检查 GPIO 设备是否已初始化并就绪
 * 参数: spec - GPIO 设备规格结构体指针
 * 返回值: true 表示就绪，false 表示未就绪
 * 
 * API: gpio_pin_configure_dt(spec, flags)
 * 功能: 配置 GPIO 引脚的方向和初始状态
 * 参数1: spec - GPIO 设备规格结构体指针
 * 参数2: flags - 配置标志位
 *         GPIO_OUTPUT_ACTIVE - 输出模式，初始状态为激活(高电平)
 *         GPIO_OUTPUT_INACTIVE - 输出模式，初始状态为非激活(低电平)
 *         GPIO_INPUT - 输入模式
 * 返回值: 0 表示成功，负数表示错误码
 * 
 * API: gpio_pin_toggle_dt(spec)
 * 功能: 翻转 GPIO 引脚的电平状态(高->低，低->高)
 * 参数: spec - GPIO 设备规格结构体指针
 * 返回值: 0 表示成功，负数表示错误码
 * 
 * API: k_msleep(ms)
 * 功能: 使当前线程休眠指定的毫秒数，让出 CPU 给其他线程
 * 参数: ms - 休眠时间(毫秒)
 * 
 * 注意: 这是一个死循环任务，相当于 FreeRTOS 的 Task
 */
void led1_thread_func(void *p1, void *p2, void *p3)
{
    if (!gpio_is_ready_dt(&led1)) {
        LOG_ERR("LED1 device not ready");
        return;
    }

    gpio_pin_configure_dt(&led1, GPIO_OUTPUT_ACTIVE);

    while (1) {
        gpio_pin_toggle_dt(&led1);
        LOG_INF("Thread 1: Toggle LED 1 (500ms)");
        k_msleep(500); // 线程主动让出 CPU 500ms
    }
}

/*
 * 3. 线程处理函数 2 (控制绿色 LED)
 * 
 * 注意: 它的频率和线程 1 不同，体现了多任务调度的独立性
 * 两个线程可以同时运行，互不干扰，由调度器分配 CPU 时间片
 */
void led2_thread_func(void *p1, void *p2, void *p3)
{
    if (!gpio_is_ready_dt(&led2)) {
        LOG_ERR("LED2 device not ready");
        return;
    }

    gpio_pin_configure_dt(&led2, GPIO_OUTPUT_INACTIVE);

    while (1) {
        gpio_pin_toggle_dt(&led2);
        LOG_INF("Thread 2: Toggle LED 2 (1000ms)");
        k_msleep(1000); // 线程主动让出 CPU 1000ms
    }
}

/*
 * 4. 静态创建线程 (Zephyr 的精髓)
 * 
 * 宏: K_THREAD_DEFINE(name, stack_size, entry, p1, p2, p3, prio, options, delay)
 * 功能: 在编译时静态分配线程栈和线程控制块，并启动线程
 * 参数1: name - 线程名称(用于调试)
 * 参数2: stack_size - 栈大小(字节)
 * 参数3: entry - 线程入口函数指针
 * 参数4: p1 - 传递给线程函数的第一个参数
 * 参数5: p2 - 传递给线程函数的第二个参数
 * 参数6: p3 - 传递给线程函数的第三个参数
 * 参数7: prio - 线程优先级(数值越小优先级越高)
 * 参数8: options - 线程选项(通常为 0)
 * 参数9: delay - 启动延迟(单位为毫秒，0 表示立即启动)
 * 
 * 优势: 
 * - 比 FreeRTOS 的 xTaskCreate 更方便，编译期自动分配内存
 * - 避免了动态内存分配(malloc)的风险，提高系统稳定性
 * - 线程在 main 函数执行前就已经启动
 */
K_THREAD_DEFINE(led1_tid, MY_STACK_SIZE, led1_thread_func, NULL, NULL, NULL, MY_PRIORITY, 0, 0);
K_THREAD_DEFINE(led2_tid, MY_STACK_SIZE, led2_thread_func, NULL, NULL, NULL, MY_PRIORITY, 0, 0);

/*
 * main 函数在 Zephyr 中也是一个线程
 * 
 * 既然业务逻辑都去子线程了，main 函数初始化完就可以结束了，或者做一些看门狗喂狗工作
 * 
 * 注意: 即使 main 函数返回，上面定义的静态线程依然会由调度器管理并继续运行
 */
int main(void)
{
    LOG_INF("Day 1 RTOS Blinky Start!");
    
    // 即使 main 结束，上面定义的静态线程依然会由调度器管理并运行
    return 0;
}
