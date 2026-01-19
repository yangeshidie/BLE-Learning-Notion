# Day 1: 环境搭建与 Zephyr RTOS 初体验 (Blinky)

> **目标**：从零搭建 nRF Connect SDK (NCS) 开发环境，理解 Zephyr 的设备树 (DeviceTree) 机制，并在非官方 nRF52832 核心板上点亮 LED。

## 1. 硬件准备

* **MCU**: nRF52832 核心板 (非官方 DK，自定义引脚)
* **Debugger**: J-Link OB / V9 (SWD 接口)
* **外设**: 面包板, LED x2 (接 P0.02, P0.03), 杜邦线若干
* **环境**: Windows 11 + VS Code

## 2. 环境搭建 (最佳实践)

*摒弃了复杂的命令行安装，直接使用 VS Code 插件的一站式方案。*

1. **安装基础工具**: Git, Python 3.x, nRF Connect for Desktop.
2. **VS Code 插件**: 安装 **nRF Connect for VS Code Extension Pack**。
3. **SDK 下载**:
   * 打开左侧 `nRF Connect` 图标。
   * 点击 **Install Toolchain** (自动安装 GCC/CMake/Ninja)。
     * 如果电脑上已经有了GCC/Cmake/Ninja工具链，依然建议使用自动安装的工具链，不会污染环境，同时SDK对工具链有版本要求。
   * 点击 **Install SDK** (选择 **v2.7.0**)。

## 3. 创建第一个工程 (Blinky)

1. 在 nRF 插件面板中选择 **"Create a new application"**。
2. 选择 **"Copy a sample"** -> 搜索 `blinky`。
3. **Board**: 选择 `nrf52dk_nrf52832` (作为基础模板)。

## 4. 核心难点：适配自定义硬件 (Overlay)

由于使用的是核心板，LED 引脚与官方 DK 板 (P0.17) 不同。**不要直接修改 `main.c` 中的引脚号**，Zephyr 的哲学是“硬件描述与代码逻辑分离”。

### 4.1 编写 Overlay 文件

在工程根目录新建文件 `nrf52dk_nrf52832.overlay` (文件名必须与 Board 名称一致)。

```dts
/ {
    /* 别名映射：将代码中的 led0/led1 映射到具体的硬件节点 */
    aliases {
        led0 = &my_led_1;
        led1 = &my_led_2;
    };

    leds {
        compatible = "gpio-leds";
      
        /* LED 1 接在 P0.02, 高电平点亮 */
        my_led_1: led_1 {
            gpios = <&gpio0 2 GPIO_ACTIVE_HIGH>;
            label = "LED 1";
        };

        /* LED 2 接在 P0.03, 高电平点亮 */
        my_led_2: led_2 {
            gpios = <&gpio0 3 GPIO_ACTIVE_HIGH>;
            label = "LED 2";
        };
    };
};
```

### 4.2 修改 C 代码 (main.c)

利用 Zephyr 的设备树 API 控制两个 LED 交替闪烁。

```c
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

/* 从设备树获取硬件信息，无需在代码里写死 "2" 或 "3" */
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec led2 = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);

int main(void)
{
    if (!gpio_is_ready_dt(&led1) || !gpio_is_ready_dt(&led2)) {
        return 0;
    }

    gpio_pin_configure_dt(&led1, GPIO_OUTPUT_ACTIVE);
    gpio_pin_configure_dt(&led2, GPIO_OUTPUT_INACTIVE);

    while (1) {
        gpio_pin_toggle_dt(&led1);
        gpio_pin_toggle_dt(&led2);
        k_msleep(500); // Zephyr 标准延时 API
    }
    return 0;
}
```

## 5. 编译与避坑指南 (Troubleshooting)

### 坑 1: Build Configuration 选错板子

* **现象**: 编译报错 `DT_N_ALIAS_led1... undeclared`。
* **原因**: 创建配置时，Board 选成了其他型号（如 `acn52832`），导致同名的 `.overlay` 文件未被加载。
* **解决**: Build Configuration 中的 Board 必须严格选择 **`nrf52dk_nrf52832`**。

### 坑 2: 晶振引脚冲突 (死机)

* **现象**: 烧录成功，但板子无反应。
* **原因**: 误将 LED 接在了 **P0.00 / P0.01**。这两个引脚是 nRF52 的低速晶振 (LFXO) 引脚。Zephyr 启动时试图驱动晶振，同时 GPIO 驱动试图点灯，导致硬件死锁。
* **解决**:
  * **方案 A**: 避开 P0.00/01，将 LED 移至 P0.02/03 (推荐，保留外部晶振)。
  * **方案 B**: 若必须用 P0.00/01 当 IO，需在 `prj.conf` 中禁用 LFXO 并启用内部 RC 振荡器。

### 坑 3: 配置不生效

* **现象**: 修改了 Overlay 或 prj.conf，但行为没变。
* **解决**: 必须使用 **Pristine Build** (编译按钮旁边的转圈图标) 来强制重新生成 CMake 缓存。

## 6. 成果展示

* 成功通过 VS Code 编译并烧录固件。
* 核心板 P0.02 和 P0.03 上的 LED 实现 500ms 交替闪烁。
* 掌握了 Zephyr 的核心逻辑：**DTS (描述硬件) + Kconfig (配置功能) + C Code (业务逻辑)**。

---

**Next Step**: Day 2 - 蓝牙广播 (Advertising) 与 抓包实战。
