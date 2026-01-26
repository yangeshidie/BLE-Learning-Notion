# Day 8: 安全配对 (SMP)

> **学习目标**：从“能通信”迈向“安全通信”。通过配置蓝牙安全管理器 (SMP)，实现基于 Passkey (配对码) 的加密连接，并为 GATT 服务添加访问权限控制，防止未经授权的数据窃取。

---

## 1. 硬件准备

| 组件                        | 说明                                             |
| :-------------------------- | :----------------------------------------------- |
| **MCU**               | 任何 nRF52/nRF53/nRF91 系列开发板                |
| **Debugger**          | J-Link OB / V9                                   |
| **PC 端**             | J-Link RTT Viewer 或支持 RTT 的 IDE (如 VS Code) |
| **手机 APP**          | nRF Connect for Mobile                           |
| **(可选) BLE 抓包器** | nRF Sniffer + Wireshark                          |

---

## 2. 学习目标

本章节的目标是将你的 BLE 设备从一个“公开的信箱”转变为一个需要“带钥匙进入”的“保险箱”。

**核心任务**：

- **启用 SMP**: 开启安全管理器，为 BLE 连接提供加密和认证的基础框架。
- **实现 Passkey 配对**: 让设备在配对时生成一个 6 位随机数，只有在手机端正确输入该数字后才允许建立加密链路。这是防止“中间人攻击 (MITM)”的关键。
- **GATT 权限控制**: 为敏感的特征值设置访问权限，确保只有在加密链路下才能进行读写，否则协议栈会自动拒绝请求。

---

## 3. 核心概念解析

### 3.1 SMP：你的蓝牙保镖

**SMP (Security Manager Protocol)** 是 BLE 协议栈中专门负责安全的部分。它的职责包括：

* **配对 (Pairing)**: 双方设备交换信息，生成一个临时的会话密钥 (Session Key)，用于加密当前连接的所有通信。
* **绑定 (Bonding)**: 在配对成功后，将生成的长期密钥 (Long Term Key, LTK) 保存到非易失性存储器（Flash）中。下次重连时，双方可以直接使用保存的密钥恢复加密，无需再次配对。
* **密钥分发**: 分发用于加密、身份识别等的多种密钥。

### 3.2 IO Capabilities：决定配对方式的“能力清单”

配对方式不是单方面决定的，而是由连接双方的“输入输出能力 (IO Capabilities)”协商而成。

| IO 能力                   | 描述                                           |
| :------------------------ | :--------------------------------------------- |
| **DisplayOnly**     | 只有显示能力 (如我们的板子，通过串口/RTT 打印) |
| `DisplayYesNo`          | 有显示屏和“是/否”按钮                        |
| `KeyboardOnly`          | 只有输入能力                                   |
| **KeyboardDisplay** | 既有键盘又有显示屏 (如智能手机)                |
| `NoInputNoOutput`       | 无输入输出能力 (如一个简单的传感器)            |

在本次任务中，我们的组合是：

* **开发板**: `DisplayOnly`
* **手机**: `KeyboardDisplay`

根据蓝牙规范，这种组合会自动触发 **Passkey Entry** 配对模式。

### 3.3 安全等级与 GATT 权限

Zephyr/NCS 为特征值定义了不同的权限等级。

| 宏                            | 描述                                  | 安全等级                                |
| :---------------------------- | :------------------------------------ | :-------------------------------------- |
| `BT_GATT_PERM_READ`         | 允许任何人读取                        | **Level 1** (无安全)              |
| `BT_GATT_PERM_READ_ENCRYPT` | **加密**后才能读取              | **Level 2** (加密，防窃听)        |
| `BT_GATT_PERM_READ_AUTHEN`  | **认证**后才能读取 (如 Passkey) | **Level 3** (加密+认证，防中间人) |

本次任务的目标是达到 **Level 3** 的安全标准。

---

## 4. 实现步骤详解

### 步骤 1: 修改 Kconfig 配置 (prj.conf)

首先，我们需要在项目中启用安全管理器模块。

```conf
# 1. 启用安全管理器 (SMP)
CONFIG_BT_SMP=y

# 2. (可选但强烈推荐) 开启绑定功能，掉电保存密钥
# 这需要 Flash 和 Settings 模块的支持
CONFIG_BT_SETTINGS=y
CONFIG_FLASH=y
CONFIG_FLASH_PAGE_LAYOUT=y
CONFIG_SETTINGS=y
```

### 步骤 2: 注册认证回调 (Authentication Callbacks)

**文件**: `main.c`

这是实现 Passkey 模式的核心。我们需要告诉协议栈，当需要显示配对码或配对被取消时，应该调用哪些函数。

```c
#include <zephyr/bluetooth/conn.h>
#include <zephyr/logging/log.h>

/* 1. 实现回调函数 */
// 当需要显示 Passkey 时调用
static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey)
{
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    // 注意格式化输出 %06u，确保打印 6 位数字，不足的前面补 0
    LOG_INF("Passkey for %s: %06u", addr, passkey);
}

// 当配对被取消时调用
static void auth_cancel(struct bt_conn *conn)
{
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_WRN("Pairing cancelled: %s", addr);
}

/* 2. 定义回调结构体 */
static struct bt_conn_auth_cb auth_cb = {
    .passkey_display = auth_passkey_display,
    .cancel = auth_cancel,
};

/* 3. 在 main 函数的初始化部分注册回调 */
// 必须在 bt_enable() 之后调用
err = bt_conn_auth_cb_register(&auth_cb);
if (err) {
    LOG_ERR("Auth callback registration failed (err %d)", err);
    return 0;
}
```

### 步骤 3: 为 GATT 特征值设置安全权限

**文件**: `my_service.c` (或你的 GATT 服务定义文件)

修改特征值定义，将读写权限从公开 (`BT_GATT_PERM_READ`) 升级为需要加密认证。

```c
// 之前的定义 (不安全)
// BT_GATT_CHARACTERISTIC(MY_CHAR_UUID,
//                        BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
//                        BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
//                        on_read, on_write, my_value),

// 修改后的定义 (安全)
BT_GATT_CHARACTERISTIC(MY_CHAR_UUID,
                       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
                       // 读和写都需要在加密和认证过的链路上进行
                       BT_GATT_PERM_READ_AUTHEN | BT_GATT_PERM_WRITE_AUTHEN,
                       on_read, on_write, my_value),
```

**注意**: `BT_GATT_PERM_READ_AUTHEN` 已经隐含了加密的要求。这是比 `_ENCRYPT` 更高的安全级别。

---

## 5. 关键 API 与宏参考

| API/宏                         | 功能           | 使用场景                                             |
| :----------------------------- | :------------- | :--------------------------------------------------- |
| `CONFIG_BT_SMP=y`            | 启用安全管理器 | 项目配置，所有安全功能的基础。                       |
| `struct bt_conn_auth_cb`     | 认证回调结构体 | 定义处理 Passkey 显示、确认、取消等事件的函数。      |
| `bt_conn_auth_cb_register()` | 注册认证回调   | 在 `main` 初始化时调用，将你的回调函数告诉协议栈。 |
| `BT_GATT_PERM_READ_AUTHEN`   | GATT 读权限宏  | 定义特征值，要求必须在认证过的链路上才能读取。       |
| `BT_GATT_PERM_WRITE_AUTHEN`  | GATT 写权限宏  | 定义特征值，要求必须在认证过的链路上才能写入。       |

---

## 6. 踩坑与经验总结

### 坑 1: 回调函数原型不匹配

**现象**: 编译时报 `incompatible pointer type` 或 `conflicting types for 'auth_passkey_display'`。

**原因**: 你自己实现的 `auth_passkey_display` 函数签名与 `struct bt_conn_auth_cb` 中定义的函数指针类型不一致。例如，忘记了 `struct bt_conn *conn` 这个参数。

**解决**: 严格参考 `zephyr/bluetooth/conn.h` 中 `struct bt_conn_auth_cb` 的定义，确保你的回调函数原型与之完全匹配。

### 坑 2: 忘记在手机端清除绑定信息

**现象**: 修改了代码后，重新烧录，但手机一连接就成功了，没有弹出 Passkey 输入框。

**原因**: 手机上还保存着上一次（可能是不安全的）绑定信息 (Bonding)。手机会尝试用旧的密钥恢复连接。

**解决**: 在测试安全相关功能时，养成一个好习惯：**每次修改代码后，都在手机的系统蓝牙设置中 "忘记" 或 "取消配对" 该设备**，确保每次都是一次全新的配对流程。

### 坑 3: 权限设置不完整 (只加密了读，没加密写)

**现象**: 未配对时读取失败，但写入却成功了。

**原因**: 特征值权限被设置为 `BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE`。这留下了一个安全漏洞，允许攻击者在未加密的情况下篡改数据。

**解决**: 确保一个需要保护的特征值，其所有敏感操作（读、写、通知）都设置了同等级别的安全权限，如 `BT_GATT_PERM_READ_AUTHEN | BT_GATT_PERM_WRITE_AUTHEN`。

---

## 7. 验收标准

- [X] **未配对时访问被拒**: 使用 nRF Connect App 连接，不进行配对。尝试读取特征值，手机 App 自动弹出 Passkey 输入框 (或在 Log 中显示 `GATT ERROR 0x5 INSUFFICIENT_AUTHENTICATION`)。
- [X] **Passkey 流程正确**: App 弹出输入框的同时，开发板的 RTT/串口日志**必须**打印出 6 位的 Passkey。
- [X] **配对成功**: 在手机上输入日志中打印的 Passkey，提示配对/绑定成功。
- [X] **配对后访问正常**: 配对成功后，再次读/写该特征值，操作成功，数据收发正常。
- [X] **(可选) 加密验证**: 使用 BLE 抓包器，确认在配对后，所有 GATT 报文的 L2CAP Payload 部分均显示为 `Encrypted Data` (乱码)。

---

## 8. 学习总结

Day 8 我们为 BLE 通信加上了最重要的一道锁。通过 SMP，我们不再是“裸奔”的数据传输，而是有了身份认证和加密保护。

1. **安全是协商的结果**: 配对方式由双方的 IO 能力共同决定，不是单方面强制的。
2. **权限是服务器的规则**: 作为 Peripheral (Server)，我们有权为自己的数据设定访问门槛。
3. **测试严谨性**: 安全功能的测试尤其需要注意清除旧状态（清除手机绑定），确保测试环境的纯净。

**核心流程**:

`GATT Read` → `Server: "Insufficient Authentication"` → `Client: "OK, let's Pair"` → `Server: "Here is my Passkey"` → `Client: "Input correct Passkey"` → `Encrypted Link Established` → `GATT Read OK`

---

**Next Step**: Day 9 - 极致低功耗 (Low Power)
