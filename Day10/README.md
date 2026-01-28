# Day 10: 空中升级 (FOTA/DFU)

> **学习目标**：这是 BLE 开发中最具挑战但也最有价值的一环。你将学习如何集成 MCUboot Bootloader，并通过蓝牙将新固件无线传输到设备中，实现无需物理连接的固件更新 (Firmware Over-The-Air)。

---

## 1. 硬件准备

| 组件               | 说明                                         |
| :----------------- | :------------------------------------------- |
| **MCU**      | nRF52832 (或其他 nRF52 系列)                 |
| **Debugger** | J-Link OB / V9                               |
| **PC 端**    | VS Code (NCS v2.7.0), nRF Command Line Tools |
| **手机 APP** | **nRF Connect**                        |

---

## 2. 学习目标

本章节的目标是彻底理解并实现“双分区升级”流程。

**核心任务**：

- **集成 MCUboot**: 理解 Bootloader 如何在两个 Flash 分区（Slot 0 和 Slot 1）之间调度固件。
- **SMP 服务 (Simple Management Protocol)**: 在蓝牙应用中添加基于 CBOR 格式的传输服务，用于接收升级包。
- **固件确认机制**: 理解 "Test"（测试）与 "Confirm"（确认）状态，防止坏固件导致设备“变砖”。
- **生成升级包**: 学会区分 `merged.hex` (生产烧录) 和 `app_update.bin` (空中升级)。

---

## 3. 核心概念解析

### 3.1 MCUboot 与 Flash 布局

在 FOTA 项目中，Flash 被划分为主要两块区域：

* **Slot 0 (Primary)**: 当前正在运行的固件。
* **Slot 1 (Secondary)**: 接收新固件的暂存区。

**升级流程**:

1. 手机通过蓝牙将新固件上传到 **Slot 1**。
2. 发送命令标记 Slot 1 为 "Test" 状态。
3. 重启系统。
4. **MCUboot** 检测到 Slot 1 有新固件且标记为 Test，将 Slot 0 和 Slot 1 的内容互换 (**Swap**)。
5. 运行新固件。如果新固件启动成功并调用了“确认”函数，则交换永久生效；否则下次重启会自动回滚 (**Revert**)。

### 3.2 关键文件说明

构建 FOTA 项目时，NCS 会生成一堆文件，必须分清用途：

| 文件名                       | 用途                                                   | 什么时候用？                                          |
| :--------------------------- | :----------------------------------------------------- | :---------------------------------------------------- |
| **`zephyr.hex`**     | 仅包含应用程序（无 Bootloader）。                      | **FOTA 项目中不要直接烧录这个**，否则无法启动。 |
| **`merged.hex`**     | 包含 `MCUboot` + `App` + `Settings` 的合并文件。 | **第一次**通过 J-Link 线刷 V1 版本时使用。      |
| **`app_update.bin`** | 经过签名的二进制升级包。                               | **第二次**通过手机蓝牙 DFU 升级 V2 版本时使用。 |

---

## 4. 实现步骤详解

### 步骤 1: 修改 Kconfig 配置 (prj.conf)

这是最容易踩坑的地方。FOTA 需要大量的 Flash 操作和协议解析，**必须手动增大栈空间**，否则会导致静默失败。

```properties
# 1. 启用 MCUboot 和 MCUmgr (SMP)
CONFIG_BOOTLOADER_MCUBOOT=y
CONFIG_MCUMGR=y
CONFIG_MCUMGR_GRP_IMG=y
CONFIG_MCUMGR_GRP_OS=y
CONFIG_MCUMGR_TRANSPORT_BT=y

# 2. 必须禁用的选项 (nRF52832 资源紧张)
CONFIG_UART_CONSOLE=n   # 关闭串口控制台，腾出 UART 给 RTT 或省电
CONFIG_SERIAL=n

# 3. 【关键】增大栈空间 (防止 Stack Overflow)
# 处理 Flash 写入和 SMP 协议解析极其消耗栈
CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE=4096
CONFIG_MAIN_STACK_SIZE=4096
CONFIG_BT_RX_STACK_SIZE=2048

# 4. 优化 DFU 传输参数 (提速)
CONFIG_BT_L2CAP_TX_MTU=252
CONFIG_BT_BUF_ACL_RX_SIZE=256
CONFIG_MCUMGR_TRANSPORT_NETBUF_COUNT=4
```

### 步骤 2: 编写版本控制代码 (main.c)

我们需要两套逻辑来验证升级是否成功（例如 V1 亮红灯，V2 亮绿灯）。同时，**必须**调用确认函数。

```c
#include <zephyr/dfu/mcuboot.h> // 必须包含

// 修改此宏来切换版本: 1 = V1, 2 = V2
#define FIRMWARE_VERSION  1 

void main(void)
{
    // ... 初始化 LED ...

    // 1. 版本指示逻辑
#if FIRMWARE_VERSION == 1
    LOG_INF("Running Firmware V1 (RED)");
    gpio_pin_set_dt(&led_red, 1);
    bt_set_name("FOTA_V1");
#else
    LOG_INF("Running Firmware V2 (GREEN)");
    gpio_pin_set_dt(&led_green, 1);
    bt_set_name("FOTA_V2");
#endif

    // 2. 【关键】确认镜像
    // 只有调用此函数，Bootloader 才会认为升级成功，
    // 否则下次重启会自动回滚到旧版本。
    boot_write_img_confirmed();

    // 3. 启动蓝牙
    bt_enable(NULL);
  
    // ... 
}
```

### 步骤 3: 编译与烧录 V1 (线刷)

1. 将代码中的宏设为 `#define FIRMWARE_VERSION 1`。
2. 执行编译。
3. **烧录**：此时必须烧录包含 Bootloader 的合并文件。
   * **错误命令**: `nrfjprog --program build/zephyr/zephyr.hex ...` (会导致板子变砖，无 Bootloader)
   * **正确命令**:
     ```bash
     nrfjprog --program build/zephyr/merged.hex --chiperase --verify -f NRF52 --reset
     ```
4. 观察现象：红灯亮，蓝牙名为 "Day10_FOTA_Test"。

### 步骤 4: 编译与升级 V2 (DFU)

1. 将代码中的宏设为 `#define FIRMWARE_VERSION 2`。
2. **重要**：建议在一个新的文件夹编译 (如 `build_1`)，或者清理后重新编译，以免文件混淆。
3. 找到升级包：`build_1/zephyr/app_update.bin`。
4. **传输**:
   * 发送 `app_update.bin` 到手机。
   * 打开 **nRF Connect**。
   * 连接 "Day10_FOTA_Test"。
   * 选择 DFU（右上角） -> Select File (`app_update.bin`)。
   * 模式选择: **Test and Confirm**。
   * 点击 OK。
5. **重启**: 上传完成后，会自动重启或者手动复位。

---

## 5. 关键 API 与宏参考

| API/宏                                 | 功能           | 说明                                                                                                  |
| :------------------------------------- | :------------- | :---------------------------------------------------------------------------------------------------- |
| `boot_write_img_confirmed()`         | 确认镜像       | **FOTA 成功的金手指**。新固件必须在启动后尽早调用此函数，否则 Bootloader 会判定启动失败并回滚。 |
| `CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE` | 系统工作队列栈 | **必须增大**。默认值 (1024) 在处理 Flash 写入时极易溢出，导致升级静默失败。                     |
| `CONFIG_MCUMGR_TRANSPORT_BT`         | 蓝牙传输层     | 告诉 MCUmgr 使用蓝牙作为传输通道（还支持 UART/USB）。                                                 |

---

## 6. 踩坑与经验总结

### 坑 1: 莫名其妙的“升级无反应”

**现象**: 手机 App 显示进度条走完，日志显示 Image Index 0，但重启后还是旧版本，或者根本不重启。
**原因**: **栈溢出 (Stack Overflow)**。Flash 驱动和 MCUmgr 协议解析非常耗内存。
**解决**: 也就是本次实战中最重要的修正——在 `prj.conf` 中大幅增加 `SYSTEM_WORKQUEUE_STACK_SIZE` (建议 4096)。

### 坑 2: 烧录错文件导致“变砖”

**现象**: 烧录后板子没有任何反应，RTT 无输出。
**原因**: 在开启 `CONFIG_BOOTLOADER_MCUBOOT` 后，Flash 的 0 地址存放的是 Bootloader。如果使用 `zephyr.hex` 烧录，它会覆盖 0 地址，但它本身不是 Bootloader，导致 CPU 无法启动。
**解决**: 只要开启了 Bootloader，**有线烧录必须使用 `merged.hex`**。

### 坑 3: 回滚机制 (Revert)

**现象**: 升级 V2 成功，绿灯亮了。但在手动按复位键后，又变回了红灯 (V1)。
**原因**: 忘记在代码中调用 `boot_write_img_confirmed()`。
**原理**: MCUboot 设计了“后悔药”机制。如果新固件有 Bug 导致无法运行到确认代码，下次重启 Bootloader 会自动把旧固件换回来，防止设备远程变砖。

---

## 7. 验收标准

- [X] **V1 启动正常**: 使用 J-Link 烧录 `merged.hex` 后，红灯亮，RTT 打印 "Version 1"，手机能搜到 "FOTA_V1"。
- [X] **DFU 过程流畅**: 使用 nRF Connect App 上传 V2 固件，进度条无卡顿，上传完毕后设备自动断开连接。
- [X] **V2 启动成功**: 设备重启后，绿灯亮，RTT 打印 "Version 2"，手机能搜到 "FOTA_V2"。
- [X] **防回滚验证**: 再次手动复位设备，设备依然是 V2 版本（绿灯），证明 `boot_write_img_confirmed()` 生效。

---

## 8. 学习总结

Day 10 攻克了嵌入式开发中的一座大山——Bootloader 与 FOTA。

1. **资源是瓶颈**: 在 RAM 只有 64KB 的 nRF52832 上跑 BLE + OS + DFU，对内存管理（尤其是栈大小）要求极高。
2. **文件即生命**: 理解 Hex 和 Bin 的区别，理解 Merged 文件的构成，是脱离 IDE 进行生产部署的第一步。
3. **流程自动化**: 从手动切宏到未来使用脚本自动构建多版本，是迈向 DevOps 的方向。

**Next Step**: Day 11 - 多线程与看门狗 (Multithreading & Watchdog) —— 既然能远程升级了，我们更要保证系统不死机，或者死机后能自动复位！
