# GPS测速仪 —— SDK资料分析与真机接入指南

> 基于项目PDF、聊天截图、杰理官方GitHub及在线文档的深度分析
> 2026-03-26

---

## 1. 项目文件资料分析

### 1.1 聊天截图 (mmexport1774493225313.jpg)

微信聊天记录确认了项目需求：
- "我想搞个测速仪" / "芯片组都有" / "主要是软件"
- 芯片方案：AC6323A + AT6558A + TM5020A
- 杰理蓝牙芯片 + 中科微GPS芯片 + 天马微电子LED驱动芯片
- **硬件已有，只需做软件开发**

### 1.2 PDF文件分析

两份PDF均为**中科微GPS芯片数据手册**（非杰理资料）：

**AT6558(Icof(中科微)).pdf (2.9MB)** — AT6558芯片Datasheet：
- 中科微第四代低功耗GNSS SOC
- 支持6大卫星系统：GPS + BDS + GLONASS + GALILEO + QZSS + SBAS
- 32个跟踪通道，多星座联合定位
- NMEA-0183 v4.0 输出，默认9600bps
- 冷启动TTFF约35秒，热启动约1秒，精度2.5m CEP
- 供电2.7-3.6V，跟踪功耗约25mA

**ATGM336H(Icof(中科微)).pdf (1.6MB)** — ATGM336H模块Datasheet：
- 基于AT6558的成品GPS模块，尺寸9.7×10.1mm
- 内置天线LNA + SAW + TCXO
- 可选1PPS脉冲输出
- 可通过UART配置波特率、更新频率(1-10Hz)、输出语句

> 注：PDF为二进制格式，无法用文本工具提取，以上基于公开资料补充。

---

## 2. 杰理AC63 SDK官方资料（核心发现）

### 2.1 重大发现：SDK已开源

杰理已将AC63系列SDK完全开源在GitHub！

| 资源 | 链接 |
|------|------|
| GitHub仓库 | https://github.com/Jieli-Tech/fw-AC63_BT_SDK |
| Gitee镜像 | https://gitee.com/Jieli-Tech/fw-AC63_BT_SDK |
| 官方文档 | https://doc.zh-jieli.com/AC63/zh-cn/master/index.html |
| 编译工具 | https://doc.zh-jieli.com/Tools/zh-cn/dev_tools/dev_env/index.html |
| HID文档 | https://doc.zh-jieli.com/AC63/zh-cn/master/module_demo/hid/index.html |
| HID协议说明 | https://doc.zh-jieli.com/AC63/zh-cn/master/module_example/BT/hid.html |
| 技术交流群(钉钉) | 3群: 107855006323 |

### 2.2 SDK支持的芯片

AC63系列：AC631N / AC635N / AC636N / AC637N / **AC632N**

AC6323A属于**AC632N系列**，对应板级目录为 `bd19`。

### 2.3 SDK工程结构

```
fw-AC63_BT_SDK/
├── apps/                    # 应用工程
│   ├── hid/                 # ★ HID应用（我们要用这个）
│   │   └── board/
│   │       └── bd19/        # ★ AC632N板级（对应AC6323A）
│   │           └── AC632N_hid.cbp  # CodeBlocks工程文件
│   ├── spp_le/              # SPP/BLE透传应用
│   └── mesh/                # Mesh组网应用
├── cpu/
│   └── bd19/
│       └── tools/
│           └── AC632N_config_tool/  # ★ 配置工具（设蓝牙名/地址/功率）
├── include_lib/             # SDK头文件
├── libs/                    # 预编译库(.a)
├── doc/                     # 文档
│   ├── datasheet/           # 芯片数据手册
│   ├── architure/           # SDK架构说明
│   └── FAQ/                 # 常见问题
└── tools/                   # 工具链
```

### 2.4 SDK三种应用Case（互斥，只能选一个）

| Case | 宏定义 | 适用领域 |
|------|--------|---------|
| **HID** ★ | CONFIG_APP_KEYBOARD等 | 遥控器/自拍器/键盘/鼠标 |
| SPP_LE | - | 透传/数传/信标/FindMy |
| Mesh | - | 物联网节点/天猫精灵 |

**我们选择 HID Case**，在其中选择 `CONFIG_APP_KEYBOARD`（HID按键）子示例。

---

## 3. ★★★ 最关键发现：SDK已有完整的媒体控制HID描述符

SDK的HID按键示例 (`app_keyboard.c`) 中已经包含了**完全符合我们需求**的Consumer Control Report Map：

```c
// 来自杰理SDK apps/hid/app_keyboard.c
static const u8 hidkey_report_map[] = {
    0x05, 0x0C,        // Usage Page (Consumer)
    0x09, 0x01,        // Usage (Consumer Control)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1)
    0x09, 0xE9,        //   Usage (Volume Increment)   ← 音量+
    0x09, 0xEA,        //   Usage (Volume Decrement)   ← 音量-
    0x09, 0xCD,        //   Usage (Play/Pause)         ← 播放/暂停
    0x09, 0xE2,        //   Usage (Mute)               ← 静音
    0x09, 0xB6,        //   Usage (Scan Previous Track)← 上一曲
    0x09, 0xB5,        //   Usage (Scan Next Track)    ← 下一曲
    0x09, 0xB3,        //   Usage (Fast Forward)       ← 快进
    0x09, 0xB4,        //   Usage (Rewind)             ← 倒退
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)   ← 每个功能占1bit
    0x95, 0x10,        //   Report Count (16) ← 共16bit=2字节
    0x81, 0x02,        //   Input (Data,Var,Abs)
    0xC0,              // End Collection
};
```

### 3.1 ★ 与我们项目的HID方案对比

| 对比项 | 我们原来的设计 | SDK实际方案 |
|--------|--------------|------------|
| Report格式 | 16-bit Usage Code(一次发一个码) | 16-bit 位域(每个功能占1bit) |
| 数据长度 | 2字节(Usage低+高) | 2字节(bit0-bit15) |
| 发送音量+ | `{0xE9, 0x00}` | `{0x01, 0x00}` (bit0=1) |
| 发送音量- | `{0xEA, 0x00}` | `{0x02, 0x00}` (bit1=1) |
| 发送播放暂停 | `{0xCD, 0x00}` | `{0x04, 0x00}` (bit2=1) |
| 发送上一曲 | `{0xB6, 0x00}` | `{0x10, 0x00}` (bit4=1) |
| 发送下一曲 | `{0xB5, 0x00}` | `{0x20, 0x00}` (bit5=1) |
| 释放 | `{0x00, 0x00}` | `{0x00, 0x00}` (相同) |

### 3.2 ★ SDK的HID数据发送方式（必须按此修改）

SDK使用**位域**方式，每个功能对应一个bit：

```
Byte 0 (低字节):
  bit0 = Volume Increment (音量+)
  bit1 = Volume Decrement (音量-)
  bit2 = Play/Pause (播放/暂停)
  bit3 = Mute (静音)
  bit4 = Scan Previous Track (上一曲)
  bit5 = Scan Next Track (下一曲)
  bit6 = Fast Forward (快进)
  bit7 = Rewind (倒退)

Byte 1 (高字节):
  bit8-bit15 = 保留(0)
```

**发送示例**：
```c
// 发送"播放/暂停"
uint8_t report[2] = {0x04, 0x00};  // bit2=1
ble_hid_key_deal_test(report);
delay_ms(50);
// 释放
report[0] = 0x00; report[1] = 0x00;
ble_hid_key_deal_test(report);
```

### 3.3 ★ 我们的hal_ble_hid_send_key需要适配的映射表

```c
// 真机HAL中的映射（替换原来的Usage Code方案）
bool hal_ble_hid_send_key(uint8_t key_code)
{
    uint8_t report[2] = {0, 0};

    switch (key_code) {
    case HID_CONSUMER_VOLUME_UP:    report[0] = 0x01; break; // bit0
    case HID_CONSUMER_VOLUME_DOWN:  report[0] = 0x02; break; // bit1
    case HID_CONSUMER_PLAY_PAUSE:   report[0] = 0x04; break; // bit2
    case HID_CONSUMER_MUTE:         report[0] = 0x08; break; // bit3
    case HID_CONSUMER_PREV_TRACK:   report[0] = 0x10; break; // bit4
    case HID_CONSUMER_NEXT_TRACK:   report[0] = 0x20; break; // bit5
    default: return false;
    }

    // 发送按下
    ble_hid_key_deal_test(report);  // SDK的BLE HID发送函数
    hal_delay_ms(50);

    // 发送释放
    report[0] = 0x00;
    ble_hid_key_deal_test(report);

    return true;
}
```

---

## 4. SDK开发环境搭建详细步骤

### 4.1 工具安装

```
1. 下载SDK源码
   git clone https://github.com/Jieli-Tech/fw-AC63_BT_SDK.git
   或从Gitee: git clone https://gitee.com/Jieli-Tech/fw-AC63_BT_SDK.git

2. 安装杰理编译工具
   下载: https://doc.zh-jieli.com/Tools/zh-cn/dev_tools/dev_env/index.html
   安装后编译器会注册到系统

3. 安装USB烧录驱动
   SDK目录下 cpu/bd19/tools/ 中有烧录工具

4. IDE选择
   方式A: CodeBlocks（推荐，杰理官方支持）
     → 打开 apps/hid/board/bd19/AC632N_hid.cbp
   方式B: Makefile
     → 双击 tools/make_prompt.bat → 输入 make target
```

### 4.2 首次编译运行

```
1. 打开 apps/hid/board/bd19/AC632N_hid.cbp (CodeBlocks)

2. 配置app_config.h:
   #define CONFIG_APP_KEYBOARD  1  // 选择HID按键case

3. 配置board_config.h:
   #define CONFIG_BOARD_AC632N_DEMO  // AC632N默认板级

4. 编译 (Ctrl+F9 或 Build)

5. 连接USB烧录器，芯片进入下载模式

6. 烧录固件

7. 上电，手机蓝牙搜索设备连接

8. 调试串口波特率: 1000000 bps (注意！不是115200)
   打印口默认: PA1
```

### 4.3 配置工具

蓝牙名称/地址/功率等通过**配置工具**设置：
```
位置: cpu/bd19/tools/AC632N_config_tool/
文件: AC632N_配置工具入口(Config Tools Entry).jlxproj
功能:
  - 设置蓝牙设备名 (我们设为 "GPS-Speedometer")
  - 设置蓝牙MAC地址
  - 设置发射功率
  - 设置充电参数
  - 设置电压提醒
  ⚠️ 每次修改后必须保存并重新编译下载
```

---

## 5. SDK核心架构（基于官方文档）

### 5.1 APP注册与运行框架

```c
// SDK的APP注册机制 (app_keyfob.c为例)
static const struct application_operation app_hid_ops = {
    .state_machine = state_machine,   // 状态机
    .event_handler = event_handler,   // 事件处理
};

REGISTER_APPLICATION(app_hid) = {
    .name   = "keyfob",
    .action = ACTION_KEYFOB,
    .ops    = &app_hid_ops,
    .state  = APP_STA_DESTROY,
};
```

### 5.2 状态机流程

```
APP_STA_CREATE → app_start()
  ├── 时钟初始化
  ├── 蓝牙模式选择 (BLE/EDR/双模)
  ├── 按键消息使能
  └── 其他初始化

APP_STA_START → 进入主运行状态
  └── event_handler() 循环处理事件

APP_STA_DESTROY → 清理退出
```

### 5.3 事件处理机制

```
外部事件(按键/BLE等)
  → 系统定时器中断采集
  → 打包为 struct sys_event
  → sys_event_notify() 通知
  → event_handler() 统一入口
     ├── 蓝牙连接事件 → bt_connection_status_event_handler()
     ├── 按键事件     → key_event_handler()
     └── 其他事件
```

### 5.4 BLE HID发送函数

```c
// BLE模式发送
ble_hid_key_deal_test(key_msg);

// EDR模式发送（经典蓝牙）
edr_hid_key_deal_test(key_msg);

// 判断当前模式
if (bt_hid_mode == HID_MODE_EDR) {
    edr_hid_key_deal_test(key_msg);
} else {
    ble_hid_key_deal_test(key_msg);  // BLE模式
}
```

---

## 6. 关键配置文件说明

### 6.1 app_config.h — 选择应用Case

```c
// 只能选一个！
#define CONFIG_APP_KEYBOARD      1  // ★ HID按键（我们用这个）
// #define CONFIG_APP_KEYFOB     1  // 自拍器
// #define CONFIG_APP_MOUSE      1  // 鼠标
// #define CONFIG_APP_STANDARD_KEYBOARD 1 // 标准键盘
// #define CONFIG_APP_PAGE_TURNER 1  // 翻页器
// #define CONFIG_APP_GAMEBOX    1  // 吃鸡王座
// #define CONFIG_APP_REMOTE_CONTROL 1 // 语音遥控
```

### 6.2 board_config.h — 选择板级

```c
#define CONFIG_BOARD_AC632N_DEMO   // ★ AC632N默认板级
```

### 6.3 board_ac632n_demo_cfg.h — 模块开关（★非常重要）

这个文件包含所有硬件模块的宏开关，需要在这里配置：
- UART引脚和使能
- I2C引脚
- GPIO按键引脚
- BLE参数
- 调试串口

---

## 7. 将我们的代码接入SDK的具体方案

### 7.1 方案A（推荐）：在SDK HID工程中嵌入我们的代码

```
1. 基于 apps/hid/ 工程
2. 在app_keyboard.c的app_start()中调用 app_main_init()
3. 在event_handler的定时事件中调用 app_main_loop()
4. 替换SDK的按键处理为我们的app_key逻辑
5. 在board_ac632n_demo_cfg.h中配置UART/I2C/GPIO引脚
6. 实现hal_ac6323a.c调用SDK的底层API
```

### 7.2 方案B：从SDK中提取BLE HID代码到我们的框架

```
不推荐 — SDK的BLE协议栈深度绑定其框架，难以剥离
```

### 7.3 具体集成步骤

```
Step 1: 在SDK工程中添加我们的源文件
  apps/hid/
  ├── app_keyboard.c          ← 修改，接入我们的逻辑
  ├── gps/                    ← 新建目录
  │   ├── app_main.c/h        ← 从本项目复制
  │   ├── app_speed.c/h
  │   ├── app_display.c/h
  │   ├── app_ble_hid.c/h
  │   ├── app_key.c/h
  │   ├── nmea_parser.c/h
  │   ├── config.h
  │   └── hal_ac6323a.c       ← 新写，调用SDK API

Step 2: 修改app_keyboard.c的app_start()
  void app_start() {
      // ... SDK原有初始化 ...
      app_main_init();  // 加入我们的初始化
  }

Step 3: 添加定时回调
  // 注册10ms定时器
  sys_timer_add(NULL, gps_timer_callback, 10);
  void gps_timer_callback(void *param) {
      app_main_loop();  // 每10ms调用
  }

Step 4: 在board_ac632n_demo_cfg.h配置引脚
  #define GPS_UART_RX_PIN   IO_PORTA_XX  // 查原理图
  #define LED_I2C_SCL_PIN   IO_PORTA_XX
  #define LED_I2C_SDA_PIN   IO_PORTA_XX
  #define KEY0_PIN          IO_PORTA_XX
  ...

Step 5: 用配置工具设蓝牙名为 "GPS-Speedometer"
```

---

## 8. SDK按键方式说明

SDK支持两种按键：

### 8.1 AD Key（模拟按键，SDK默认）
- 多个按键通过不同阻值的电阻分压到同一个ADC引脚
- 一个IO口支持多个按键
- 默认配置在 board_ac632n_demo_cfg.h

### 8.2 IO Key（GPIO按键，我们用这种）
- 每个按键一个独立GPIO
- 更简单直观
- 需要在board配置中切换为IO Key模式

```c
// board_ac632n_demo_cfg.h中配置IO Key
#define TCFG_IOKEY_ENABLE    1  // 使能IO Key
#define TCFG_ADKEY_ENABLE    0  // 禁用AD Key

// 定义IO Key引脚
#define TCFG_IOKEY_POWER_ONE_PORT   IO_PORTA_XX
#define TCFG_IOKEY_PREV_ONE_PORT    IO_PORTA_XX
#define TCFG_IOKEY_NEXT_ONE_PORT    IO_PORTA_XX
```

---

## 9. 调试注意事项

### 9.1 调试串口
- **波特率：1000000 bps**（不是常见的115200！）
- 默认打印IO：PA1
- 需要USB转串口模块支持1M波特率（CP2102支持，CH340部分型号支持）

### 9.2 下载模式
- 芯片需要进入下载模式才能烧录
- 具体进入方式查SDK文档和板子硬件设计

### 9.3 nRF Connect调试
- 手机安装 nRF Connect app
- 可以查看BLE广播数据、GATT服务表、HID Report

---

## 10. 总结：拿到SDK后的精确行动清单

```
□  1. git clone SDK仓库
□  2. 安装杰理编译工具
□  3. 用CodeBlocks打开 apps/hid/board/bd19/AC632N_hid.cbp
□  4. 编译HID按键示例（不改代码先编译通过）
□  5. 烧录到开发板，手机连接测试原始HID功能
□  6. 在SDK工程中创建gps/目录，复制我们的app/lib代码
□  7. 创建hal_ac6323a.c
□  8. 在board_ac632n_demo_cfg.h中配置引脚
□  9. 实现hal_system_init + hal_get_tick_ms + hal_delay_ms
□ 10. 实现hal_uart_init（GPS接收）
□ 11. 实现hal_i2c_init + hal_i2c_write（LED驱动）
□ 12. 实现hal_gpio_init + hal_gpio_read（按键）
□ 13. 实现hal_flash（用SDK NV存储API）
□ 14. 实现hal_ble系列（包装SDK的ble_hid_key_deal_test）
□ 15. 修改HID Report发送为位域格式
□ 16. 在app_keyboard.c中集成app_main_init/loop
□ 17. 用配置工具设蓝牙名"GPS-Speedometer"
□ 18. 编译烧录，分模块联调
□ 19. 户外GPS实测
□ 20. 稳定性测试
```
