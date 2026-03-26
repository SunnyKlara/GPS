# GPS测速仪项目 —— 进度跟踪

> 最后更新: 2026-03-26 16:06

---

## 当前总体进度: 75%

```
████████████████░░░░░░ 75%
```

---

## 一、各阶段完成情况

### ✅ Phase 1: PC模拟环境 + 业务逻辑 (100%)
- [x] NMEA-0183解析器 (`nmea_parser.c`)
- [x] 速度计算+低通滤波+里程累计 (`app_speed.c`)
- [x] 7段数码管显示逻辑 (`app_display.c`)
- [x] BLE HID媒体控制逻辑 (`app_ble_hid.c`)
- [x] 按键扫描+消抖+长按检测 (`app_key.c`)
- [x] 主循环调度 (`app_main.c`)
- [x] HAL硬件抽象层接口 (`hal.h`)
- [x] PC模拟HAL实现 (`hal_sim.c`)
- [x] CLI命令行模拟器 (`sim_main.c`)
- [x] GUI图形模拟器 (`sim_gui.c`, Windows GDI)
- [x] 单元测试: NMEA解析 + 速度计算 (`test_nmea.c`, `test_speed.c`)

### ✅ Phase 2: 文档 + SDK分析 (100%)
- [x] 开发指南Part1 (`DEVELOPMENT_GUIDE_PART1.md`)
- [x] 开发指南Part2 (`DEVELOPMENT_GUIDE_PART2.md`)
- [x] SDK分析文档 (`SDK_ANALYSIS.md`)
- [x] 下一步操作指南 (`NEXT_STEPS_GUIDE.md`)
- [x] 参考资料网址 (`参考资料网址`)

### ✅ Phase 3a: SDK环境搭建 (100%)
- [x] 下载fw-AC63_BT_SDK
- [x] 安装杰理编译器 (`C:\JL\pi32\bin\clang.exe` v4.0.1)
- [x] 分析SDK目录结构和bd19板级配置
- [x] 分析app_keyboard.c的HID发送机制
- [x] 分析SDK的GPIO/UART/I2C/Flash API
- [x] 修改board_config.h选择AC6323A
- [x] 默认HID工程编译验证通过 → `jl_isd.fw`

### ✅ Phase 3b: HAL真机层 + 集成准备 (100%)
- [x] 创建hal_ac6323a.c真机HAL代码模板
- [x] HID Consumer Key位域映射 (Usage Code → SDK Bitfield)
- [x] SDK集成指南文档 (`SDK_INTEGRATION_GUIDE.md`)
- [x] 完整的API映射表 (hal.h ↔ SDK API)
- [x] 文件复制清单 (15个文件)
- [x] 4个SDK文件的修改方案

### 🔴 Phase 3c: 引脚配置 + 代码集成 (0%) ← 当前卡点
- [ ] **获取PCB引脚分配** (需要用户提供)
- [ ] 修改board_ac6323a_demo_cfg.h引脚配置
- [ ] 修改board_ac6323a_demo.c添加UART1+5按键
- [ ] 集成GPS代码到app_keyboard.c
- [ ] 将15个源文件复制到SDK工程
- [ ] 最终编译验证

### ⬜ Phase 4: 硬件联调 (0%)
- [ ] 烧录固件到AC6323A
- [ ] 串口调试log验证
- [ ] GPS数据接收验证
- [ ] I2C LED显示验证
- [ ] 按键功能验证
- [ ] BLE HID配对+媒体控制验证
- [ ] 功耗测试

---

## 二、关键文件清单

### 项目文档
| 文件 | 说明 |
|------|------|
| `PROJECT_STATUS.md` | 本文件，项目进度跟踪 |
| `SDK_INTEGRATION_GUIDE.md` | SDK集成详细指南（API映射+引脚+修改清单）|
| `SDK_ANALYSIS.md` | SDK架构分析 |
| `DEVELOPMENT_GUIDE_PART1.md` | 开发指南上篇 |
| `DEVELOPMENT_GUIDE_PART2.md` | 开发指南下篇 |
| `NEXT_STEPS_GUIDE.md` | 操作步骤指南 |

### 项目源码 (gps_speedometer/)
| 目录 | 文件 | 说明 |
|------|------|------|
| app/ | app_main.c/h | 主循环调度 |
| app/ | app_speed.c/h | 速度+里程计算 |
| app/ | app_display.c/h | 数码管显示 |
| app/ | app_ble_hid.c/h | BLE HID媒体控制 |
| app/ | app_key.c/h | 按键扫描 |
| lib/ | nmea_parser.c/h | NMEA协议解析 |
| config/ | config.h | 全局配置 |
| platform/ | hal.h | HAL接口定义 |
| platform/platform_sim/ | hal_sim.c | PC模拟HAL |
| platform/platform_sim/ | sim_main.c | CLI模拟器 |
| platform/platform_sim/ | sim_gui.c | GUI模拟器 |
| platform/platform_ac6323a/ | hal_ac6323a.c | ★ 真机HAL (已创建) |
| tests/ | test_nmea.c, test_speed.c | 单元测试 |

### SDK修改文件 (fw-AC63_BT_SDK/)
| 文件 | 状态 | 修改内容 |
|------|------|---------|
| apps/hid/board/bd19/board_config.h | ✅ 已修改 | 选择AC6323A |
| apps/hid/board/bd19/board_ac6323a_demo_cfg.h | ⬜ 待修改 | 引脚配置 |
| apps/hid/board/bd19/board_ac6323a_demo.c | ⬜ 待修改 | 添加UART1+按键 |
| apps/hid/examples/keyboard/app_keyboard.c | ⬜ 待修改 | 集成GPS逻辑 |

---

## 三、BLE HID蓝牙控制 —— 技术方案

### 已确认可行的关键点

1. **SDK已内置完整BLE HID协议栈**
   - HOGP (HID over GATT Protocol) 已实现
   - 配对/绑定/重连 SDK自动处理
   - Report Map (Consumer Control) 已定义在app_keyboard.c

2. **Consumer Control Report Map (SDK原生)**
   ```
   Report ID = 1, Report Count = 16 bits
   Bit0: Volume+     (0x0001)
   Bit1: Volume-     (0x0002)
   Bit2: Play/Pause  (0x0004)
   Bit3: Mute        (0x0008)
   Bit4: Prev Track  (0x0010)
   Bit5: Next Track  (0x0020)
   Bit6: Fast Forward(0x0040)
   Bit7: Rewind      (0x0080)
   ```

3. **发送函数（SDK已提供）**
   ```c
   // BLE模式发送
   ble_hid_data_send(1, &key_msg, 2);   // report_id=1, 2字节位域
   // EDR模式发送
   edr_hid_data_send(1, &key_msg, 2);
   ```

4. **我们的映射（hal_ac6323a.c已实现）**
   ```
   HID_CONSUMER_PLAY_PAUSE (0xCD)  → SDK bit2 (0x0004)
   HID_CONSUMER_NEXT_TRACK (0xB5)  → SDK bit5 (0x0020)
   HID_CONSUMER_PREV_TRACK (0xB6)  → SDK bit4 (0x0010)
   HID_CONSUMER_VOLUME_UP  (0xE9)  → SDK bit0 (0x0001)
   HID_CONSUMER_VOLUME_DOWN(0xEA)  → SDK bit1 (0x0002)
   ```

5. **按键发送流程**
   ```
   用户按键 → app_key扫描 → app_main回调
     → app_ble_hid_send_action() → hal_ble_hid_send_key()
       → map_hid_key_to_sdk_bitfield() → ble_hid_data_send()
         → 手机收到媒体控制命令
   ```

---

## 四、待解决的唯一阻塞项

### 🔴 PCB引脚分配 (需要用户提供)

需要确定8个引脚：
```
1. GPS(AT6558A) TX → AC6323A ?     (推荐PA2, UART1 RX)
2. I2C SCL (TM5020A) → ?           (推荐PA9)
3. I2C SDA (TM5020A) → ?           (推荐PA8)
4. KEY0 播放/暂停 → ?
5. KEY1 上一曲    → ?
6. KEY2 下一曲    → ?
7. KEY3 音量+     → ?
8. KEY4 音量-     → ?
```

拿到引脚信息后，预计 **1-2小时** 完成代码集成和编译。

---

## 五、模拟器使用指南

### 5.1 编译和启动

```powershell
# ========== GUI模拟器（推荐）==========
# 方法1：PowerShell直接运行
cd D:\Users\26054\Desktop\GPS\gps_speedometer
powershell -ExecutionPolicy Bypass -File .\build.ps1 gui

# 方法2：如果已经编译过，直接打开exe
D:\Users\26054\Desktop\GPS\gps_speedometer\build\gps_gui.exe

# ========== CLI命令行模拟器 ==========
cd D:\Users\26054\Desktop\GPS\gps_speedometer
powershell -ExecutionPolicy Bypass -File .\build.ps1 sim
# 或直接运行
D:\Users\26054\Desktop\GPS\gps_speedometer\build\gps_sim.exe

# ========== 单元测试 ==========
cd D:\Users\26054\Desktop\GPS\gps_speedometer
powershell -ExecutionPolicy Bypass -File .\build.ps1 test

# ========== 清除编译产物 ==========
cd D:\Users\26054\Desktop\GPS\gps_speedometer
powershell -ExecutionPolicy Bypass -File .\build.ps1 clean
```

### 5.2 GUI模拟器界面说明

```
┌─────────────────────────────────────┐
│  ┌───┬───┬───┬───┬───┬───┐         │
│  │ 8 │ 8 │ 8 │ 8 │ 8 │ 8 │  ← 6位7段数码管  │
│  └───┴───┴───┴───┴───┴───┘         │
│                                     │
│  ● GPS状态(绿/红)  ● BLE状态(蓝/灰) │
│  Speed: 0.0 km/h                    │
│  Mileage: 0.000 km                  │
│                                     │
│  [MUSIC CONTROL]                    │
│  Play/Pause  Prev  Next  Vol+ Vol-  │
│                                     │
│  [FUNCTIONS]                        │
│  BLE Toggle  Speed+10  Speed-10     │
│  Mode  Reset Mileage                │
│                                     │
│  ═══════●══════════════  ← 速度滑块  │
│  0              150            300   │
└─────────────────────────────────────┘
```

### 5.3 键盘快捷键

| 按键 | 功能 | 对应真机按键 |
|------|------|------------|
| **1** | 播放/暂停 | KEY0 |
| **2** | 上一曲 | KEY1 |
| **3** | 下一曲 | KEY2 |
| **4** | 音量+ | KEY3 |
| **5** | 音量- | KEY4 |
| **B** | BLE连接/断开切换 | - |
| **↑** | 速度+5 km/h | - |
| **↓** | 速度-5 km/h | - |
| **M** | 切换显示模式 | 长按KEY0 |
| **R** | 重置里程 | 长按KEY1 |
| **Q** | 退出模拟器 | - |

### 5.4 操作示例

1. **看速度显示**：拖动底部滑块 或 按↑↓键改变模拟速度
2. **切换显示**：按 M 键，循环：速度 → 里程 → 时间 → 最高速度
3. **测试蓝牙**：先按 B 连接BLE，再按 1~5 发送媒体控制命令
4. **重置里程**：按 R 键清零里程

---

## 六、SDK编译命令速查

```powershell
# ========== 编译SDK固件 ==========
# 必须在SDK根目录下执行
cd D:\Users\26054\Desktop\GPS\fw-AC63_BT_SDK
D:\Users\26054\Desktop\GPS\fw-AC63_BT_SDK\tools\utils\make.exe ac632n_hid

# ========== 清除SDK编译产物 ==========
cd D:\Users\26054\Desktop\GPS\fw-AC63_BT_SDK
D:\Users\26054\Desktop\GPS\fw-AC63_BT_SDK\tools\utils\make.exe clean_ac632n_hid

# ========== 编译器版本 ==========
C:\JL\pi32\bin\clang.exe --version

# ========== 生成的固件位置 ==========
# fw-AC63_BT_SDK\cpu\bd19\tools\jl_isd.fw   (烧录用)
# fw-AC63_BT_SDK\cpu\bd19\tools\jl_isd.ufw  (OTA升级用)
```

---

## 七、项目目录结构总览

```
D:\Users\26054\Desktop\GPS\
│
├── PROJECT_STATUS.md              ← ★ 本文件（项目进度跟踪）
├── SDK_INTEGRATION_GUIDE.md       ← SDK集成详细指南
├── SDK_ANALYSIS.md                ← SDK架构分析
├── DEVELOPMENT_GUIDE_PART1.md     ← 开发指南上篇
├── DEVELOPMENT_GUIDE_PART2.md     ← 开发指南下篇
├── NEXT_STEPS_GUIDE.md            ← 操作步骤指南
├── README.md                      ← 项目简介
├── 参考资料网址                    ← 杰理官方资料链接
│
├── gps_speedometer/               ← ★ 我们的项目源码
│   ├── build.ps1                  ← PowerShell编译脚本
│   ├── build.bat                  ← BAT编译脚本
│   ├── Makefile                   ← Linux/Mac编译
│   ├── config/config.h            ← 全局配置
│   ├── app/                       ← 应用层代码
│   │   ├── app_main.c/h           ← 主循环
│   │   ├── app_speed.c/h          ← 速度/里程
│   │   ├── app_display.c/h        ← 显示控制
│   │   ├── app_ble_hid.c/h        ← BLE HID
│   │   └── app_key.c/h            ← 按键扫描
│   ├── lib/nmea_parser.c/h        ← NMEA解析
│   ├── platform/
│   │   ├── hal.h                  ← HAL接口定义
│   │   ├── platform_sim/          ← PC模拟实现
│   │   │   ├── hal_sim.c
│   │   │   ├── sim_main.c         ← CLI模拟器
│   │   │   └── sim_gui.c          ← GUI模拟器
│   │   └── platform_ac6323a/      ← 真机HAL实现
│   │       └── hal_ac6323a.c      ← ★ 已创建
│   ├── tests/                     ← 单元测试
│   └── build/                     ← 编译输出
│       ├── gps_gui.exe            ← GUI模拟器
│       ├── gps_sim.exe            ← CLI模拟器
│       └── gps_test.exe           ← 测试程序
│
└── fw-AC63_BT_SDK/                ← ★ 杰理SDK
    ├── apps/hid/                  ← HID应用工程
    │   ├── board/bd19/            ← AC632N/AC6323A板级
    │   │   ├── board_config.h     ← ✅ 已改为AC6323A
    │   │   ├── board_ac6323a_demo_cfg.h  ← ⬜ 待改引脚
    │   │   └── board_ac6323a_demo.c      ← ⬜ 待加UART1
    │   ├── examples/keyboard/
    │   │   └── app_keyboard.c     ← ⬜ 待集成GPS逻辑
    │   └── include/app_config.h   ← HID应用配置
    ├── cpu/bd19/tools/            ← 下载/烧录工具
    │   └── jl_isd.fw             ← 编译输出固件
    ├── include_lib/driver/cpu/bd19/asm/  ← SDK驱动头文件
    │   ├── gpio.h, uart.h
    │   ├── iic_soft.h, iic_hw.h
    │   └── ...
    └── tools/utils/make.exe       ← SDK自带编译工具
```
