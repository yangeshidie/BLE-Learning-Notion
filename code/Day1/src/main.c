#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

/* 注册日志模块，方便调试 */
LOG_MODULE_REGISTER(day1_app, LOG_LEVEL_INF);

/* 1. 硬件定义
 * 依然使用设备树获取 LED 硬件信息
 */
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec led2 = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);

/* 定义线程栈大小和优先级 */
#define MY_STACK_SIZE 1024 
#define MY_PRIORITY 7

/* 
 * 2. 线程处理函数 1 (控制红色 LED)
 * 这是一个死循环任务，相当于 FreeRTOS 的 Task
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
 * 注意：它的频率和线程 1 不同，体现了多任务调度的独立性
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
 * 宏参数：线程名, 栈大小, 入口函数, 参数1, 参数2, 参数3, 优先级, 选项, 延时启动时间
 * 这比 FreeRTOS 的 xTaskCreate 更方便，编译期自动分配内存。
 */
K_THREAD_DEFINE(led1_tid, MY_STACK_SIZE, led1_thread_func, NULL, NULL, NULL, MY_PRIORITY, 0, 0);
K_THREAD_DEFINE(led2_tid, MY_STACK_SIZE, led2_thread_func, NULL, NULL, NULL, MY_PRIORITY, 0, 0);

/*
 * main 函数在 Zephyr 中也是一个线程。
 * 既然业务逻辑都去子线程了，main 函数初始化完就可以结束了，或者做一些看门狗喂狗工作。
 */
int main(void)
{
    LOG_INF("Day 1 RTOS Blinky Start!");
    
    // 即使 main 结束，上面定义的静态线程依然会由调度器管理并运行
    return 0;
}