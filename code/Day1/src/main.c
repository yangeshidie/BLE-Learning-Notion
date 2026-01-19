#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

/* 1. 从设备树获取 LED 节点信息
 * DT_ALIAS(led0) 对应我们在 overlay 里定义的 led0 (P0.01)
 * DT_ALIAS(led1) 对应我们在 overlay 里定义的 led1 (P0.02)
 */
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec led2 = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);

int main(void)
{
    /* 2. 检查硬件是否就绪 */
    if (!gpio_is_ready_dt(&led1) || !gpio_is_ready_dt(&led2)) {
        return 0;
    }

    /* 3. 配置引脚为输出 */
    gpio_pin_configure_dt(&led1, GPIO_OUTPUT_ACTIVE); // 初始点亮
    gpio_pin_configure_dt(&led2, GPIO_OUTPUT_INACTIVE); // 初始熄灭

    while (1) {
        /* 4. 翻转两个 LED */
        gpio_pin_toggle_dt(&led1);
        gpio_pin_toggle_dt(&led2);

        /* 延时 500ms (这是 Zephyr 的标准延时函数) */
        k_msleep(500);
    }
    return 0;
}