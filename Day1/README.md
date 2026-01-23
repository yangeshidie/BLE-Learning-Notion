# Day 1: 环境搭建与 Zephyr RTOS 初体验

> **学习目标**：从零搭建 nRF Connect SDK (NCS) 开发环境，理解 Zephyr 的设备树 (DeviceTree) 机制，并在非官方 nRF52832 核心板上点亮 LED。

---

## 1. 硬件准备

| 组件 | 说明 |
|------|------|
| **MCU** | nRF52832 核心板 (非官方 DK，自定义引脚) |
| **Debugger** | J-Link OB / V9 (SWD 接口) |
| **外设** | 面包板, LED x2 (接 P0.02, P0.03), 杜邦线若干 |
| **环境** | Windows 11 + VS Code |

---

## 2. 环境搭建

摒弃了复杂的命令行安装，直接使用 VS Code 插件的一站式方案。

### 2.1 安装基础工具

1. 安装 Git
2. 安装 Python 3.x
3. 安装 nRF Connect for Desktop

### 2.2 VS Code 插件安装

安装 **nRF Connect for VS Code Extension Pack**

### 2.3 SDK 下载

1. 打开 VS Code 左侧 `nRF Connect` 图标
2. 点击 **Install Toolchain** (自动安装 GCC/CMake/Ninja)
   - 如果电脑上已经有 GCC/CMake/Ninja 工具链，依然建议使用自动安装的工具链
   - 这样不会污染环境，同时 SDK 对工具链有版本要求
3. 点击 **Install SDK** (选择 **v2.7.0**)

---

## 3. 创建第一个工程 (Blinky)

### 3.1 创建步骤

1. 在 nRF 插件面板中选择 **"Create a new application"**
2. 选择 **"Copy a sample"** -> 搜索 `blinky`
3. **Board**: 选择 `nrf52dk_nrf52832` (作为基础模板)

### 3.2 核心难点：适配自定义硬件 (Overlay)

由于使用的是核心板，LED 引脚与官方 DK 板 (P0.17) 不同。**不要直接修改 `main.c` 中的引脚号**，Zephyr 的哲学是"硬件描述与代码逻辑分离"。

#### 编写 Overlay 文件

在工程根目录新建文件 `nrf52dk_nrf52832.overlay` (文件名必须与 Board 名称一致)

```dts
/ {
    aliases {
        led0 = &my_led_1;
        led1 = &my_led_2;
    };

    leds {
        compatible = "gpio-leds";
    
        my_led_1: led_1 {
            gpios = <&gpio0 2 GPIO_ACTIVE_HIGH>;
            label = "LED 1";
        };

        my_led_2: led_2 {
            gpios = <&gpio0 3 GPIO_ACTIVE_HIGH>;
            label = "LED 2";
        };
    };
};
```

### 3.3 C 代码实现 (main.c)

为了体现 Zephyr RTOS 的多任务特性，我们不再使用裸机的 `while(1)` 循环，而是创建两个独立的线程来分别控制两个 LED。

#### RTOS 知识点

- `K_THREAD_DEFINE`: 静态线程创建
- `k_msleep`: 阻塞延时

```c
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(day1_app, LOG_LEVEL_INF);

static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec led2 = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);

#define STACK_SIZE 512
#define THREAD_PRIORITY 7

void led1_thread_entry(void *p1, void *p2, void *p3)
{
    if (!gpio_is_ready_dt(&led1)) return;
    gpio_pin_configure_dt(&led1, GPIO_OUTPUT_ACTIVE);

    while (1) {
        gpio_pin_toggle_dt(&led1);
        LOG_INF("Task A executing...");
        k_msleep(500);
    }
}

void led2_thread_entry(void *p1, void *p2, void *p3)
{
    if (!gpio_is_ready_dt(&led2)) return;
    gpio_pin_configure_dt(&led2, GPIO_OUTPUT_INACTIVE);

    while (1) {
        gpio_pin_toggle_dt(&led2);
        LOG_INF("Task B executing...");
        k_msleep(1000);
    }
}

K_THREAD_DEFINE(led1_id, STACK_SIZE, led1_thread_entry, NULL, NULL, NULL, THREAD_PRIORITY, 0, 0);
K_THREAD_DEFINE(led2_id, STACK_SIZE, led2_thread_entry, NULL, NULL, NULL, THREAD_PRIORITY, 0, 0);

int main(void)
{
    LOG_INF("System Start - Main thread will exit now.");
    return 0;
}
```

---

## 4. 关键 API 参考

| API | 功能 | 使用场景 |
|-----|------|----------|
| `GPIO_DT_SPEC_GET` | 从设备树获取 GPIO 配置 | 获取 LED 引脚定义 |
| `gpio_is_ready_dt` | 检查 GPIO 设备是否就绪 | 初始化前检查硬件状态 |
| `gpio_pin_configure_dt` | 配置 GPIO 引脚方向和初始状态 | 设置为输出模式 |
| `gpio_pin_toggle_dt` | 翻转 GPIO 引脚电平 | LED 闪烁 |
| `K_THREAD_DEFINE` | 静态创建并启动线程 | 编译时分配内存创建任务 |
| `k_msleep` | 毫秒级延时阻塞 | 让出 CPU 时间片 |

---

## 5. 踩坑与经验总结

### 坑 1: Build Configuration 选错板子

**现象**: 编译报错 `DT_N_ALIAS_led1... undeclared`

**原因**: 创建配置时，Board 选成了其他型号（如 `acn52832`），导致同名的 `.overlay` 文件未被加载

**解决**: Build Configuration 中的 Board 必须严格选择 **`nrf52dk_nrf52832`**

### 坑 2: 晶振引脚冲突 (死机)

**现象**: 烧录成功，但板子无反应

**原因**: 误将 LED 接在了 **P0.00 / P0.01**。这两个引脚是 nRF52 的低速晶振 (LFXO) 引脚。Zephyr 启动时试图驱动晶振，同时 GPIO 驱动试图点灯，导致硬件死锁

**解决**:
- **方案 A** (推荐): 避开 P0.00/01，将 LED 移至 P0.02/03 (保留外部晶振)
- **方案 B**: 若必须用 P0.00/01 当 IO，需在 `prj.conf` 中禁用 LFXO 并启用内部 RC 振荡器

### 坑 3: 配置不生效

**现象**: 修改了 Overlay 或 prj.conf，但行为没变

**解决**: 必须使用 **Pristine Build** (编译按钮旁边的转圈图标) 来强制重新生成 CMake 缓存

### 坑 4: 开启日志后程序崩溃 (栈溢出)

**现象**: 代码逻辑正确，但加入 `LOG_INF` 后 LED 停止闪烁，或 RTT 打印 `USAGE FAULT`

**原因**: 格式化字符串和日志子系统需要消耗大量栈空间。初始设置的 `500` 字节栈空间不足以支撑日志打印

**解决**: 将线程栈大小至少增加到 **1024** 字节

```c
/* 错误示范 */
#define MY_STACK_SIZE 500 

/* 正确做法 (带 Log) */
#define MY_STACK_SIZE 1024
```

---

## 6. 验收标准

- [ ] 成功通过 VS Code 编译并烧录固件
- [ ] 核心板 P0.02 和 P0.03 上的 LED 实现 500ms 交替闪烁
- [ ] RTT Viewer 能看到两个线程的日志输出
- [ ] 理解 Zephyr 的核心逻辑：**DTS (描述硬件) + Kconfig (配置功能) + C Code (业务逻辑)**

---

## 7. 学习总结

Day 1 完成了开发环境的搭建和第一个 RTOS 程序的编写。通过 Blinky 示例，我们掌握了：

1. **硬件抽象层 (HAL)**: 使用 Device Tree Overlay 适配自定义硬件，实现硬件描述与代码逻辑的解耦
2. **多任务编程**: 使用 `K_THREAD_DEFINE` 创建独立线程，理解 OS 调度机制
3. **模块化设计**: LED1 和 LED2 的控制逻辑完全独立，互不干扰

---

**Next Step**: Day 2 - 蓝牙广播 (Advertising) 与 抓包实战
