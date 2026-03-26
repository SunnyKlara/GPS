# GPS测速仪 —— 完整开发文档（上篇：项目分析与架构）

> **芯片方案**: AC6323A(杰理主控+BLE) + AT6558A(中科微GPS) + TM5020A(LED驱动)  
> **文档版本**: v1.0 | 2026-03-26

---

## 1. 项目总览

### 1.1 核心功能

| 功能 | 描述 |
|------|------|
| **实时测速** | GPS获取速度，6位LED数码管显示(km/h) |
| **里程累计** | 速度积分计算，掉电Flash保存 |
| **最高速度** | 记录本次行程最高速度 |
| **时间显示** | GPS授时北京时间(UTC+8) |
| **BLE音乐控制** | BLE HID Consumer Control控制手机音乐 |
| **多模式显示** | 按键切换速度/里程/时间/最高速度 |

### 1.2 四阶段开发策略

```
Phase 1 ✅ PC模拟环境 → NMEA解析 + 速度计算 + 里程 + LED模拟
Phase 2 ✅ BLE HID模拟 + 按键 + Windows GUI模拟器
Phase 3 🔜 拿到杰理SDK → 替换HAL层适配真机 ← 当前阶段
Phase 4 🔜 硬件联调 + 烧录 + 量产测试
```

---

## 2. 硬件架构

### 2.1 系统框图

```
┌───────────────────────────────────────────┐
│              GPS测速仪 PCB                 │
│                                            │
│  AT6558A ──UART(9600)──> AC6323A(主控+BLE) │
│  GPS/BDS定位              │    │    │      │
│                     I2C   │  GPIO  BLE     │
│                      │    │    │    │      │
│                  TM5020A  │  5按键  手机    │
│                  6位数码管 │        音乐控制 │
└───────────────────────────────────────────┘
```

### 2.2 芯片参数

**AC6323A（杰理主控）**：自研内核，内置Flash(256-512KB)，RAM(16-32KB)，BLE 5.0 HID，多路UART/I2C/GPIO，3.3V供电，闭源SDK+专用编译器。

**AT6558A（中科微GPS）**：GPS+BDS+GLONASS多星座，NMEA-0183输出@9600bps，冷启动~35秒，热启动~1秒，精度2.5m CEP。

**TM5020A（天马LED驱动）**：I2C接口，默认地址0x48，驱动6位×8段共阴数码管。

### 2.3 NMEA $GNRMC格式

```
$GNRMC,083000.00,A,3939.9000,N,11616.4000,E,30.0,45.5,260326,,,A*43
       时间UTC   状态 纬度    N/S 经度      E/W 速度  航向  日期
                A有效                       (节)
                V无效
```

### 2.4 按键功能映射

| 按键ID | 短按 | 长按(1.5秒) |
|--------|------|------------|
| 0 | BLE播放/暂停 | 切换显示模式 |
| 1 | BLE上一曲 | 清零里程 |
| 2 | BLE下一曲 | 清零最高速度 |
| 3 | BLE音量+ | - |
| 4 | BLE音量- | - |

---

## 3. 软件架构

### 3.1 四层分离

```
┌─────────────────────────────────────┐
│  应用层 app/ (纯业务逻辑，不依赖硬件) │
│  app_main / app_speed / app_display │
│  app_ble_hid / app_key              │
├─────────────────────────────────────┤
│  通用库 lib/ (纯算法，完全可移植)     │
│  nmea_parser                        │
├─────────────────────────────────────┤
│  HAL接口 platform/hal.h (13个函数)   │
├─────────────────────────────────────┤
│  平台实现 platform/xxx/             │
│  platform_sim/(PC) 或               │
│  platform_ac6323a/(真机，待创建)     │
└─────────────────────────────────────┘
```

### 3.2 HAL接口清单（hal.h定义的13个函数）

```c
// UART (GPS通信)
void hal_uart_init(uint32_t baudrate, hal_uart_rx_cb_t rx_callback);
void hal_uart_send(const uint8_t *data, uint16_t len);

// I2C (LED驱动)
void hal_i2c_init(void);
bool hal_i2c_write(uint8_t addr, const uint8_t *data, uint16_t len);
bool hal_i2c_read(uint8_t addr, uint8_t *data, uint16_t len);

// GPIO (按键)
void hal_gpio_init(void);
bool hal_gpio_read(uint8_t pin);  // true=按下

// Flash (掉电存储)
void hal_flash_init(void);
bool hal_flash_write(uint32_t addr, const uint8_t *data, uint16_t len);
bool hal_flash_read(uint32_t addr, uint8_t *data, uint16_t len);

// BLE
void hal_ble_init(const char *device_name, hal_ble_connect_cb_t cb);
bool hal_ble_is_connected(void);
bool hal_ble_hid_send_key(uint8_t key_code);
void hal_ble_process(void);

// 系统
uint32_t hal_get_tick_ms(void);
void hal_delay_ms(uint32_t ms);
void hal_system_init(void);
```

### 3.3 数据流

```
GPS卫星 → AT6558A(NMEA) → UART RX中断 → nmea_parser_feed()逐字节
  → 解析完整$GNRMC → app_speed_update(speed, valid)
  → 低通滤波+里程积分+最高速度
  → app_display_update()每100ms → 生成段码
  → hal_i2c_write(0x48, seg_data, 6) → TM5020A → 数码管
```

### 3.4 主循环（每10ms）

```c
app_main_loop():
  ├── app_key_scan()           // 按键扫描消抖
  ├── app_ble_hid_process()    // BLE协议栈轮询
  └── if (100ms到)
      └── app_display_update() // 刷新LED显示
// GPS数据由UART中断异步接收处理
```

---

## 4. 各模块代码详解

### 4.1 NMEA解析器 (lib/nmea_parser.c, 213行)

**状态机**：`IDLE→'$'→RECEIVING→'*'→CHECKSUM1→CHECKSUM2→校验→解析`

- 支持：$GNRMC(GPS+BDS)、$GPRMC(GPS)、$BDRMC(北斗)
- 校验和：$与*之间所有字符XOR
- 速度转换：1节 = 1.852 km/h
- 经纬度：ddmm.mmmm格式转度

### 4.2 测速与里程 (app/app_speed.c, 105行)

- **低通滤波**：`filtered = 0.3×raw + 0.7×prev`（抑制GPS跳变）
- **静止判定**：< 1.0 km/h 视为静止显示0
- **里程积分**：`distance += speed × dt`（速度×时间）
- **Flash存储**：地址0x1000，Magic 0xA5A5A5A5 + float里程，30秒自动保存

### 4.3 显示管理 (app/app_display.c, 132行)

四种模式：速度(1位小数) / 里程(2位小数) / 时间(HH.MM.SS) / 最高速度

段码表(共阴)：0=0x3F, 1=0x06, 2=0x5B, 3=0x4F, 4=0x66, 5=0x6D, 6=0x7D, 7=0x07, 8=0x7F, 9=0x6F, '-'=0x40, 小数点=0x80(OR)

### 4.4 BLE HID (app/app_ble_hid.c, 77行)

USB HID Consumer Control码：播放0xCD, 下一曲0xB5, 上一曲0xB6, 音量+0xE9, 音量-0xEA

优势：手机不需要APP，系统原生支持BLE HID

### 4.5 按键 (app/app_key.c, 74行)

消抖：连续2次(20ms)同电平确认。长按：持续1.5秒触发，释放不再触发短按。

---

## 5. 已完成工作清单

| 项目 | 状态 |
|------|------|
| 四层HAL隔离架构 | ✅ |
| NMEA解析器(含校验和) | ✅ |
| 速度低通滤波+里程积分 | ✅ |
| 最高速度记录 | ✅ |
| 数码管段码生成(6位) | ✅ |
| 四种显示模式切换 | ✅ |
| 按键消抖+长按检测 | ✅ |
| BLE HID控制逻辑 | ✅ |
| PC模拟HAL(文件Flash/printf LED) | ✅ |
| CLI模拟器(键盘交互) | ✅ |
| GUI模拟器(Win32数码管+按钮+滑块) | ✅ |
| 单元测试(9用例：NMEA×4+速度×5) | ✅ |
| 构建脚本(Makefile/bat/ps1) | ✅ |

**编译命令**：
```powershell
cd d:\Users\26054\Desktop\GPS\gps_speedometer
.\build.ps1        # GUI模拟器(默认)
.\build.ps1 sim    # CLI模拟器
.\build.ps1 test   # 单元测试
```

---

*下篇见 DEVELOPMENT_GUIDE_PART2.md → SDK接入详细步骤、HAL真机实现、联调经验、踩坑记录、项目总结*
