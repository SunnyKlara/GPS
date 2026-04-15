# 10 - GPS模块嫁接到杰理SDK的具体方案

## 核心思路

**SDK是老大，GPS是插件。** 不是把SDK塞进我们的框架，而是把我们的代码挂到SDK上。

```
SDK原有流程:                         嫁接后:
                                    
app_main() → hidkey_app_start()      app_main() → hidkey_app_start()
             ├── btstack_init()                   ├── btstack_init()
             ├── sys_key_event_enable()           ├── sys_key_event_enable()
             └── (结束)                            ├── gps_module_init()     ← 新增
                                                  └── sys_timer_add(gps_loop, 100ms) ← 新增

SDK按键事件:                         嫁接后:
KEY_EVENT → hidkey_app_key_deal_test()
         → 查表发HID                  KEY_EVENT → gps_key_handler()  ← 替换
                                               → 短按: 发HID音乐控制
                                               → 长按: 切换显示模式/清零里程

SDK BLE状态:                         嫁接后:
BLE_ST_CONNECT → (SDK处理)           BLE_ST_CONNECT → gps_ble_notify(true)  ← 新增
BLE_ST_DISCONN → (SDK处理)           BLE_ST_DISCONN → gps_ble_notify(false) ← 新增
```

## 哪些文件要动

### SDK中需要修改的文件（3个）：

| 文件 | 改什么 |
|------|--------|
| `board_ac6323a_demo_cfg.h` | 加UART1配置(GPS)，改按键引脚 |
| `board_ac6323a_demo.c` | 加UART1初始化结构体 |
| `app_keyboard.c` | 插入GPS初始化、改按键处理、加BLE状态通知 |

### 我们的代码需要调整的文件（2个需改，其余不动）：

| 文件 | 改什么 |
|------|--------|
| `app_key.c` | **可以删掉** — SDK的按键框架更完善，直接用SDK的 |
| `app_main.c` | 去掉自己的按键扫描，让SDK驱动 |
| `app_speed.c` | 不用改 |
| `app_display.c` | 不用改 |
| `app_ble_hid.c` | 不用改（但hal层的send_key会桥接到SDK的ble_hid_data_send） |
| `nmea_parser.c` | 不用改 |
| `hal_ac6323a.c` | 已经写好了，基本不用改 |

## 关键改动详解

### 改动1：app_keyboard.c — 插入GPS模块

在 `hidkey_app_start()` 末尾加两行：

```c
// ========= 文件: app_keyboard.c =========
// 在 hidkey_app_start() 函数末尾，sys_key_event_enable() 之后添加：

#include "gps/app_main.h"      // 我们的GPS头文件

// GPS测速仪定时回调（每100ms调用一次）
static void gps_timer_callback(void *param)
{
    app_main_loop();  // 驱动GPS主循环：解析NMEA、刷新显示、保存里程
}

// --- 在 hidkey_app_start() 末尾添加 ---
    app_main_init();                                    // GPS模块初始化
    sys_timer_add(NULL, gps_timer_callback, 100);       // 100ms定时驱动
```

### 改动2：app_keyboard.c — 替换按键处理

```c
// ========= 文件: app_keyboard.c =========
// 替换 hidkey_app_key_deal_test() 函数

#include "gps/app_display.h"
#include "gps/app_speed.h"
#include "gps/app_ble_hid.h"

static void hidkey_app_key_deal_test(u8 key_type, u8 key_value)
{
    u16 key_msg = 0;
    u16 key_msg_up = 0;

    // === 短按：发送HID媒体控制 ===
    if (key_type == KEY_EVENT_CLICK) {
        key_msg = hid_key_click_table[key_value];
        if (key_msg) {
            // 原有SDK发送逻辑不变
            if (bt_hid_mode == HID_MODE_EDR) {
                bt_comm_edr_sniff_clean();
                edr_hid_data_send(1, (u8 *)&key_msg, 2);
                edr_hid_data_send(1, (u8 *)&key_msg_up, 2);
            } else {
                ble_hid_data_send(1, &key_msg, 2);
                ble_hid_data_send(1, &key_msg_up, 2);
            }
        }
        return;
    }

    // === 长按：GPS功能操作 ===
    if (key_type == KEY_EVENT_LONG) {
        switch (key_value) {
        case 0:  // KEY0长按 → 切换显示模式
            app_display_next_mode();
            break;
        case 1:  // KEY1长按 → 清零里程
            app_speed_reset_mileage();
            break;
        case 2:  // KEY2长按 → 清零最高速度
            app_speed_reset_max();
            break;
        }
        return;
    }

    // === HOLD：音量持续调节 ===
    if (key_type == KEY_EVENT_HOLD) {
        key_msg = hid_key_hold_table[key_value];
        if (key_msg) {
            if (bt_hid_mode == HID_MODE_EDR) {
                bt_comm_edr_sniff_clean();
                edr_hid_data_send(1, (u8 *)&key_msg, 2);
            } else {
                ble_hid_data_send(1, &key_msg, 2);
            }
        }
        return;
    }

    // === 松开：释放按键 ===
    if (key_type == KEY_EVENT_UP) {
        if (bt_hid_mode == HID_MODE_EDR) {
            edr_hid_data_send(1, (u8 *)&key_msg_up, 2);
        } else {
            ble_hid_data_send(1, &key_msg_up, 2);
        }
        return;
    }

    // === 双击KEY0：切换BLE/EDR模式（保留SDK原功能）===
    if (key_type == KEY_EVENT_DOUBLE_CLICK && key_value == 0) {
        is_hidkey_active = 1;
        if (HID_MODE_BLE == bt_hid_mode) {
            hidkey_app_select_btmode(HID_MODE_EDR);
        } else {
            hidkey_app_select_btmode(HID_MODE_BLE);
        }
        os_time_dly(WAIT_DISCONN_TIME_MS / 10);
        p33_soft_reset();
        while (1);
    }

    // === 三击KEY0：关机（保留SDK原功能）===
    if (key_type == KEY_EVENT_TRIPLE_CLICK && key_value == 0) {
        hidkey_power_event_to_user(POWER_EVENT_POWER_SOFTOFF);
    }
}
```

### 改动3：app_keyboard.c — 加BLE连接状态通知

在SDK的BLE状态回调中，加一行通知GPS模块：

```c
// ========= 文件: ble_hogp.c 或 app_keyboard.c =========
// 找到BLE连接状态变化的处理位置（SDK中的hogp回调或connection_status_handler）

// 在 BT_STATUS_FIRST_CONNECTED / BLE_ST_CONNECT 分支中添加：
extern void gps_ble_connection_notify(bool connected);

case BLE_ST_CONNECT:
    // ... SDK原有处理 ...
    gps_ble_connection_notify(true);   // ← 新增
    break;

case BLE_ST_DISCONN:
    // ... SDK原有处理 ...
    gps_ble_connection_notify(false);  // ← 新增
    break;
```

### 改动4：board_ac6323a_demo_cfg.h — 加GPS串口

```c
// ========= 文件: board_ac6323a_demo_cfg.h =========
// 在UART0配置后面新增：

// GPS UART配置
#define TCFG_UART1_ENABLE               ENABLE_THIS_MOUDLE
#define TCFG_UART1_TX_PORT              NO_CONFIG_PORT      // GPS不需要发送
#define TCFG_UART1_RX_PORT              IO_PORTA_02         // TODO: 根据原理图
#define TCFG_UART1_BAUDRATE             9600
```

### 改动5：我们的 app_main.c 适配

去掉自己的按键扫描（SDK已处理），简化主循环：

```c
// ========= 文件: gps/app_main.c =========
// 修改 app_main_loop()，去掉按键扫描

void app_main_loop(void)
{
    uint32_t now = hal_get_tick_ms();
    
    // 不需要按键扫描了 —— SDK的按键框架已经处理
    // app_key_scan();   ← 删掉这行
    
    // BLE处理也不需要了 —— SDK自动处理
    // hal_ble_process(); ← 删掉这行
    
    // 只保留显示刷新
    if (now - s_last_display_ms >= DISPLAY_UPDATE_INTERVAL_MS) {
        s_last_display_ms = now;
        app_display_update();
    }
}
```

## 最终目录结构

```
fw-AC63_BT_SDK/apps/hid/
├── app_main.c                          # SDK入口（不改）
├── examples/
│   └── keyboard/
│       └── app_keyboard.c              # ★ 需要修改：挂载GPS模块
├── board/bd19/
│   ├── board_config.h                  # 已改好：选择AC6323A
│   ├── board_ac6323a_demo_cfg.h        # ★ 需要修改：加UART1
│   └── board_ac6323a_demo.c            # ★ 需要修改：加UART1结构体
├── modules/bt/
│   └── ble_hogp.c                      # ★ 需要修改：加BLE状态通知
└── gps/                                # ★ 新增目录：我们的GPS代码
    ├── app_main.c/h                    # GPS主逻辑（简化版）
    ├── app_speed.c/h                   # 测速里程（不改）
    ├── app_display.c/h                 # 显示管理（不改）
    ├── app_ble_hid.c/h                 # BLE HID（不改，但底层桥接到SDK）
    ├── nmea_parser.c/h                 # NMEA解析（不改）
    ├── gps_config.h                    # 配置（不改）
    ├── hal.h                           # HAL接口（不改）
    └── hal_ac6323a.c                   # HAL实现（已写好）
```

## 已有代码vs SDK功能的冲突对照表

| 功能 | 我们写的 | SDK已有的 | 谁赢？ |
|------|---------|----------|--------|
| 系统初始化 | `hal_system_init()` | `board_init()` | SDK赢，我们的变空函数 |
| 按键扫描 | `app_key.c` | `key_driver` | SDK赢，删掉我们的 |
| 按键消抖 | 自己写的20ms消抖 | SDK内置 | SDK赢 |
| 按键事件分发 | 自己的回调 | `SYS_KEY_EVENT` | SDK赢 |
| BLE初始化 | `hal_ble_init()` | `btstack_init()` | SDK赢 |
| BLE广播 | 没写 | SDK自动管理 | SDK赢 |
| BLE HID发送 | `hal_ble_hid_send_key()` | `ble_hid_data_send()` | 我们调SDK的 |
| Report Map | 自己定义的 | `hidkey_report_map[]` | SDK的（已包含Consumer Control） |
| UART (GPS) | `hal_uart_init()` | SDK有UART驱动 | 合作：用SDK的UART1驱动 |
| I2C (LED) | `hal_i2c_write()` | SDK有soft_iic | 合作：用SDK的soft_iic |
| Flash存储 | `hal_flash_write()` | `syscfg_write()` | 合作：用SDK的VM系统 |
| NMEA解析 | `nmea_parser.c` | 无 | **我们独有** |
| 测速算法 | `app_speed.c` | 无 | **我们独有** |
| 显示管理 | `app_display.c` | 无 | **我们独有** |
| 定时器 | `hal_get_tick_ms()` | `sys_timer_add()` | SDK的定时器框架 |
| 主循环 | `app_main_loop()` | SDK RTOS事件循环 | SDK驱动，定时调我们的 |

## 一句话总结

**SDK管蓝牙、按键、电源、RTOS调度；我们管GPS解析、测速计算、LED显示。
两者通过3个接口点连接：定时回调(100ms)、按键事件转发、BLE状态通知。**
