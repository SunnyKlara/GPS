# GPS测速仪 —— 完整开发文档（下篇：SDK接入与实战经验）

> 接上篇 DEVELOPMENT_GUIDE_PART1.md

---

## 6. 拿到SDK后的完整开发流程

### 6.1 总路线图与时间估算

```
Step 1: SDK环境搭建 .................... 1-2天
Step 2: 创建真机工程 .................... 1天
Step 3: 实现真机HAL层(核心) ............ 3-5天
Step 4: 集成应用层代码 .................. 1天
Step 5: 分模块硬件联调 .................. 3-5天
Step 6: 整体联调+优化 .................. 2-3天
Step 7: 量产准备(可选) .................. 1-2天
─────────────────────────────────────────
合计约 12-19 个工作日
```

### 6.2 Step 1: SDK环境搭建（1-2天）

杰理SDK通常包含以下结构：
```
AC6323A_SDK/
├── tools/
│   ├── compiler/        # 杰理专用编译器
│   ├── downloader/      # 烧录工具
│   └── config_tool/     # 芯片配置工具
├── include_lib/
│   ├── system/          # 系统API
│   ├── btstack/         # 蓝牙协议栈API
│   └── driver/          # 外设驱动API
├── libs/                # 预编译库(.a)
├── apps/                # 示例应用
│   ├── hid/             # ★ HID示例（重点参考）
│   ├── spp_le/          # SPP/BLE示例
│   └── common/          # 通用模块
└── cpu/                 # CPU架构相关
```

**操作步骤**：
1. 解压SDK到 `D:\JL_SDK\AC6323A_SDK\`（路径无中文无空格）
2. 安装杰理编译器（SDK自带，添加到PATH或在工程中指定路径）
3. 安装USB烧录驱动（tools/downloader/中）
4. 编译SDK的HID示例工程 → 确认工具链OK
5. 烧录到开发板 → 确认烧录流程OK
6. 手机蓝牙搜索 → 确认BLE广播正常

### 6.3 Step 2: 创建真机工程（1天）

**推荐方式：基于SDK的HID示例修改**

```
1. 复制 apps/hid/ → apps/gps_speedometer/
2. 清理示例中不需要的代码
3. 在工程中创建目录结构：
   apps/gps_speedometer/
   ├── app/          ← 从本项目复制
   ├── lib/          ← 从本项目复制
   ├── config/       ← 从本项目复制
   └── platform/
       └── platform_ac6323a/
           └── hal_ac6323a.c  ← 新建（核心工作）
4. 修改工程Makefile，添加源文件和include路径
5. 在config.h中取消注释 #define PLATFORM_AC6323A 并注释掉 PLATFORM_SIM
6. 编译 → 解决所有编译错误（主要是include路径）
```

**引脚配置（需查PCB原理图确认）**：
```c
// UART → GPS模块AT6558A
#define GPS_UART_TX_PIN   PAx   // AC6323A TX（可不接）
#define GPS_UART_RX_PIN   PAx   // AC6323A RX ← AT6558A TX

// I2C → LED驱动TM5020A
#define LED_I2C_SCL_PIN   PAx
#define LED_I2C_SDA_PIN   PAx

// GPIO → 5个按键（上拉，按下低电平）
#define KEY0_PIN  PAx  // 播放/暂停
#define KEY1_PIN  PAx  // 上一曲
#define KEY2_PIN  PAx  // 下一曲
#define KEY3_PIN  PAx  // 音量+
#define KEY4_PIN  PAx  // 音量-
```

### 6.4 Step 3: 实现真机HAL层（3-5天，核心工作）

创建 `platform/platform_ac6323a/hal_ac6323a.c`，逐个实现hal.h中的13+个函数。

---

## 7. HAL层真机适配——逐函数实现指南

### 7.1 系统函数

```c
/* hal_system_init() */
void hal_system_init(void)
{
    // SDK系统初始化通常在main前已完成
    // 这里做额外配置：看门狗、低功耗等
}

/* hal_get_tick_ms() */
uint32_t hal_get_tick_ms(void)
{
    // 使用SDK提供的系统tick
    // 杰理SDK可能叫: timer_get_ms() / sys_timer_get_ms() / jl_timer_get_ms()
    // 查SDK头文件确认准确函数名
}

/* hal_delay_ms() */
void hal_delay_ms(uint32_t ms)
{
    // SDK延时函数
    // 如果有RTOS: os_time_dly(ms)
    // 裸机: delay_ms(ms) 或忙等
    // ⚠️ 不要用忙等，会阻塞BLE协议栈
}
```

### 7.2 UART（接GPS模块AT6558A）

```c
static hal_uart_rx_cb_t s_rx_cb = NULL;

// SDK的UART接收中断回调
static void uart_rx_isr(uint8_t *buf, uint16_t len)
{
    if (s_rx_cb) {
        s_rx_cb(buf, len);
    }
}

void hal_uart_init(uint32_t baudrate, hal_uart_rx_cb_t rx_callback)
{
    s_rx_cb = rx_callback;
    // 杰理SDK UART初始化（伪代码，API名需查文档）:
    // struct uart_config cfg;
    // cfg.baud_rate = baudrate;      // 9600
    // cfg.tx_pin = GPS_UART_TX_PIN;
    // cfg.rx_pin = GPS_UART_RX_PIN;
    // cfg.rx_cbuf_size = 256;        // 接收缓冲256字节
    // cfg.rx_callback = uart_rx_isr;
    // jl_uart_init(UART_CH0, &cfg);
}

void hal_uart_send(const uint8_t *data, uint16_t len)
{
    // jl_uart_send(UART_CH0, data, len);
    // 本项目不需要向GPS发数据，可空实现
    (void)data; (void)len;
}
```

**关键注意**：
- AT6558A上电自动发NMEA，AC6323A只需接收
- 必须用**中断接收**，轮询会丢数据
- 缓冲区建议256字节（NMEA一帧最长~82字节）
- 确认波特率两端一致（默认都是9600）

### 7.3 I2C（接TM5020A LED驱动）

```c
void hal_i2c_init(void)
{
    // 方案A: 硬件I2C
    // jl_i2c_init(I2C_CH0, LED_I2C_SCL_PIN, LED_I2C_SDA_PIN, 100000);

    // 方案B: GPIO模拟I2C（更通用，推荐作为备选）
    // gpio_set_output(LED_I2C_SCL_PIN);
    // gpio_set_output(LED_I2C_SDA_PIN);
}

bool hal_i2c_write(uint8_t addr, const uint8_t *data, uint16_t len)
{
    // 方案A: SDK硬件I2C
    // return jl_i2c_write(I2C_CH0, addr, data, len) == 0;
    
    // 方案B: GPIO模拟I2C
    // i2c_start();
    // i2c_send_byte(addr << 1);  // 注意7bit地址左移
    // for (int i = 0; i < len; i++) i2c_send_byte(data[i]);
    // i2c_stop();
    // return true;
}
```

**GPIO模拟I2C参考实现**：
```c
static void sda_high(void) { gpio_set_high(LED_I2C_SDA_PIN); }
static void sda_low(void)  { gpio_set_low(LED_I2C_SDA_PIN);  }
static void scl_high(void) { gpio_set_high(LED_I2C_SCL_PIN); }
static void scl_low(void)  { gpio_set_low(LED_I2C_SCL_PIN);  }
static void i2c_delay(void){ delay_us(5); } // 100KHz

static void i2c_start(void) {
    sda_high(); scl_high(); i2c_delay();
    sda_low();  i2c_delay();
    scl_low();  i2c_delay();
}
static void i2c_stop(void) {
    sda_low();  scl_high(); i2c_delay();
    sda_high(); i2c_delay();
}
static void i2c_send_byte(uint8_t byte) {
    for (int i = 7; i >= 0; i--) {
        if (byte & (1 << i)) sda_high(); else sda_low();
        scl_high(); i2c_delay();
        scl_low();  i2c_delay();
    }
    // ACK
    sda_high(); scl_high(); i2c_delay(); scl_low();
}
```

**注意**：
- TM5020A默认I2C地址0x48，但I2C协议中地址是7位+1位R/W，发送时需要 `0x48 << 1 = 0x90`（写）
- 有些SDK的I2C API已经做了左移处理，有些没有，需确认
- 如果硬件I2C不稳定，果断切GPIO模拟

### 7.4 GPIO（按键输入）

```c
static const uint8_t KEY_PINS[KEY_NUM] = {
    KEY0_PIN, KEY1_PIN, KEY2_PIN, KEY3_PIN, KEY4_PIN
};

void hal_gpio_init(void)
{
    for (int i = 0; i < KEY_NUM; i++) {
        // jl_gpio_set_mode(KEY_PINS[i], GPIO_INPUT_PULLUP);
        // 配置为输入+内部上拉
    }
}

bool hal_gpio_read(uint8_t pin)
{
    if (pin >= KEY_NUM) return false;
    // bool level = jl_gpio_read(KEY_PINS[pin]);
    // return !level;  // ⚠️ 上拉电路，低电平=按下，需取反
    return false;
}
```

**注意**：hal.h定义 `true=按下`，但上拉电路按下是低电平，所以必须取反。

### 7.5 Flash（掉电存储里程）

```c
void hal_flash_init(void)
{
    // 初始化SDK Flash/NV存储接口
}

bool hal_flash_write(uint32_t addr, const uint8_t *data, uint16_t len)
{
    // ★ 强烈推荐方案: 使用SDK的NV(Non-Volatile)存储API
    // NV API内部做了磨损均衡，不会写坏Flash
    // return jl_nv_write(NV_TAG_USER_0, data, len) == 0;
    
    // 备选方案: 直接Flash读写
    // ⚠️ Flash写前必须先擦除（整扇区，通常4KB）
    // ⚠️ 频繁擦写会减少寿命（通常10万次）
    // jl_flash_erase_sector(USER_FLASH_BASE + addr);
    // return jl_flash_write(USER_FLASH_BASE + addr, data, len) == 0;
}

bool hal_flash_read(uint32_t addr, uint8_t *data, uint16_t len)
{
    // return jl_nv_read(NV_TAG_USER_0, data, len) == 0;
    // 或
    // return jl_flash_read(USER_FLASH_BASE + addr, data, len) == 0;
}
```

**⚠️ Flash寿命计算**：
- 当前30秒写一次 → 每天2880次 → 每年~105万次
- Flash典型寿命10万次 → **直接写一年就会坏！**
- **必须**用NV API（带磨损均衡），或自己实现环形写入
- 如果NV API不可用，建议改为**5分钟保存一次**（每年约10万次，刚好）

### 7.6 BLE HID（最复杂，详细展开）

```c
static hal_ble_connect_cb_t s_connect_cb = NULL;

// BLE事件回调（SDK注册）
static void ble_event_handler(uint8_t event)
{
    switch (event) {
    case BLE_EVT_CONNECTED:
        if (s_connect_cb) s_connect_cb(true);
        break;
    case BLE_EVT_DISCONNECTED:
        if (s_connect_cb) s_connect_cb(false);
        break;
    }
}

void hal_ble_init(const char *device_name, hal_ble_connect_cb_t cb)
{
    s_connect_cb = cb;
    // 1. 设置设备名
    // jl_ble_set_name(device_name);
    
    // 2. 注册HID Report Map（见下方描述符）
    // jl_ble_hid_set_report_map(hid_report_map, sizeof(hid_report_map));
    
    // 3. 注册事件回调
    // jl_ble_register_event_callback(ble_event_handler);
    
    // 4. 开始广播
    // jl_ble_start_advertising();
}

bool hal_ble_is_connected(void)
{
    // return jl_ble_is_connected();
    return false;
}

bool hal_ble_hid_send_key(uint8_t key_code)
{
    // Consumer Control Report: [Report_ID, Usage_Low, Usage_High]
    uint8_t report[3];
    
    // 按下
    report[0] = 1;          // Report ID
    report[1] = key_code;   // Usage Low Byte
    report[2] = 0x00;       // Usage High Byte
    // jl_ble_hid_send_report(report, 3);
    
    hal_delay_ms(50);       // 按下持续50ms
    
    // 释放（必须发全0，否则手机认为一直按着）
    report[1] = 0x00;
    report[2] = 0x00;
    // jl_ble_hid_send_report(report, 3);
    
    return true;
}

void hal_ble_process(void)
{
    // 杰理SDK BLE协议栈通常在中断/RTOS任务中自动运行
    // 裸机模式可能需要: jl_ble_loop();
}
```

**HID Report Descriptor（必须正确注册）**：
```c
static const uint8_t hid_report_map[] = {
    0x05, 0x0C,       // Usage Page (Consumer)
    0x09, 0x01,       // Usage (Consumer Control)
    0xA1, 0x01,       // Collection (Application)
    0x85, 0x01,       //   Report ID (1)
    0x15, 0x00,       //   Logical Minimum (0)
    0x26, 0xFF, 0x03, //   Logical Maximum (1023)
    0x19, 0x00,       //   Usage Minimum (0)
    0x2A, 0xFF, 0x03, //   Usage Maximum (1023)
    0x75, 0x10,       //   Report Size (16 bits)
    0x95, 0x01,       //   Report Count (1)
    0x81, 0x00,       //   Input (Data, Array, Absolute)
    0xC0              // End Collection
};
```
这个描述符告诉手机：我是一个媒体控制器，发送16位的Consumer Usage Code。

**BLE GATT服务结构**：
```
GPS-Speedometer (Peripheral)
├── GAP Service (0x1800) — 设备名、外观
├── HID Service (0x1812) — 核心
│   ├── HID Information (0x2A4A)
│   ├── Report Map (0x2A4B) ← 放上面的描述符
│   ├── Report (0x2A4D) ← 发送按键数据
│   │   └── CCCD (通知使能)
│   ├── HID Control Point (0x2A4C)
│   └── Protocol Mode (0x2A4E)
├── Battery Service (0x180F) [可选]
└── Device Info Service (0x180A) [可选]
```

---

## 8. 主入口适配

杰理SDK的主入口与标准C不同，通常是SDK框架调用你的初始化函数。

### 8.1 裸机模式

```c
// SDK的入口函数（名字需查文档）
void app_start(void)
{
    app_main_init();  // 你的初始化
}

// SDK的主循环回调（或定时器回调）
void app_loop(void)  // 可能叫 app_task() / user_main_loop()
{
    app_main_loop();  // 你的主循环
}
```

### 8.2 RTOS模式（如果SDK用了RTOS）

```c
// 创建一个任务跑你的业务逻辑
static void gps_task(void *param)
{
    app_main_init();
    while (1) {
        app_main_loop();
        os_time_dly(SYS_TICK_MS);  // 10ms
    }
}

void app_start(void)
{
    os_task_create(gps_task, "gps", 512, 0, NULL);
}
```

### 8.3 config.h平台切换

```c
// 拿到SDK后修改 config/config.h:
#define PLATFORM_AC6323A        // 取消注释，启用真机
// #define PLATFORM_SIM         // 注释掉，禁用模拟

// 在代码中用条件编译区分:
#ifdef PLATFORM_AC6323A
    #include "platform_ac6323a/hal_ac6323a.c"
#else
    #include "platform_sim/hal_sim.c"
#endif
```

---

## 9. 硬件联调流程（Step 5详细展开）

### 9.1 联调顺序（由简到难，必须严格按顺序）

```
★ 第一步：点灯（验证工具链+烧录）
  配置一个GPIO输出，驱动LED闪烁
  目的：确认编译→烧录→运行的完整链路OK

★ 第二步：调试串口（后续所有调试的基础）
  配置一路UART输出printf（不是GPS那路！另一路！）
  接USB转串口到PC，串口助手115200bps查看
  目的：有日志输出才能排查后续问题
  ⚠️ 如果没有多余串口引脚，用SWD调试接口

★ 第三步：GPS UART接收
  配置UART RX接GPS模块
  在中断回调中把原始数据printf到调试串口
  确认收到 $GNRMC,... 等NMEA语句
  然后接入nmea_parser，打印解析结果

★ 第四步：按键GPIO
  读取5个按键引脚状态
  串口打印按键ID和事件（短按/长按）
  验证消抖和长按逻辑

★ 第五步：I2C LED显示
  发送数据到TM5020A
  先显示固定数字"888888"（全段测试）
  再接入app_display显示GPS速度

★ 第六步：Flash读写
  写入一个测试值，断电重启后读出
  验证里程保存和恢复功能

★ 第七步：BLE HID（最后）
  初始化BLE，手机搜索连接
  发送Consumer Control按键
  用手机播放音乐测试
```

### 9.2 调试工具清单

| 工具 | 用途 | 推荐 |
|------|------|------|
| USB转串口模块 | 调试串口日志 | CP2102/CH340 |
| 串口助手软件 | 查看串口数据 | SSCOM/Putty |
| 逻辑分析仪 | 抓UART/I2C波形 | Saleae/DSLogic |
| 万用表 | 测量电压/通断 | 任意数字万用表 |
| nRF Connect APP | BLE调试 | 手机App Store搜索 |
| 杰理烧录器 | 固件烧录 | SDK自带 |

### 9.3 GPS户外实测检查清单

```
□ 到空旷地方（操场、天台），远离高楼
□ 冷启动后等待定位（可能需要30-60秒）
□ 确认串口打印 is_valid=true
□ 静止时速度应显示0
□ 步行时速度约4-6 km/h
□ 骑车时速度约15-25 km/h
□ 开车时对比车速表，误差应<5%
□ 进出隧道测试GPS丢失和恢复
□ 长时间(>1小时)运行稳定性
```

---

## 10. 常见问题与踩坑记录

### 10.1 GPS问题

| 问题 | 原因 | 解决 |
|------|------|------|
| 长时间无法定位 | 室内/天线问题 | 到空旷室外，检查天线焊接 |
| 速度跳变 | 信号差/多径 | 滤波系数调小(0.15)，增加滤波级数 |
| $GNRMC全是V | 未定位 | 等冷启动35秒，检查天线 |
| 收到乱码 | 波特率不对 | 确认两端都是9600 |
| 数据丢失 | 缓冲溢出 | 加大缓冲区，确保中断及时处理 |
| 里程不准 | GPS精度限制 | 正常，GPS测速精度约±0.1m/s |

### 10.2 BLE问题

| 问题 | 原因 | 解决 |
|------|------|------|
| 手机搜不到 | 广播未启动 | 检查广播初始化和发射功率 |
| iOS搜不到 | iOS过滤 | 广播数据包含HID Service UUID(0x1812) |
| 能搜到连不上 | GATT表错误 | 检查HID Service定义 |
| 连上但按键无效 | Report Map错误 | 核对HID描述符 |
| 只按了一下连续响应 | 未发释放Report | 按下后必须发全0释放 |
| 断连频繁 | 连接参数 | 调整连接间隔(15-30ms) |
| 每次要重新配对 | 绑定未保存 | SDK开启绑定信息Flash存储 |

### 10.3 显示问题

| 问题 | 原因 | 解决 |
|------|------|------|
| 数码管不亮 | I2C通信失败 | 逻辑分析仪抓I2C波形 |
| I2C无ACK | 地址错误 | 确认7bit/8bit地址格式，试0x48或0x90 |
| 显示乱码 | 段码映射错 | 对照TM5020A手册段码定义 |
| 亮度不均 | 驱动电流 | 调TM5020A亮度寄存器 |
| 闪烁 | 刷新率低 | 确保100ms刷新一次 |

### 10.4 编译/系统问题

| 问题 | 原因 | 解决 |
|------|------|------|
| 函数未定义 | SDK API名不同 | 查SDK头文件找正确函数名 |
| 链接错误 | 缺SDK库 | 检查链接脚本，添加.a文件 |
| Flash不够 | 代码太大 | 优化代码，关不用的功能，-Os编译 |
| RAM不够 | 变量太多 | 减小缓冲区，优化数据结构 |
| float慢 | 无FPU | 用定点数替代(性能不足时) |
| 死机/HardFault | 栈溢出/空指针 | 加大栈，添加断言检查 |

---

## 11. 测试策略

### 11.1 现有单元测试（9个用例）

**NMEA解析器测试 (test_nmea.c)**：
1. `test_nmea_valid_gnrmc` — 有效GNRMC全字段解析验证
2. `test_nmea_invalid_status` — V状态(无效定位)处理
3. `test_nmea_checksum_error` — 校验和错误拒绝
4. `test_nmea_gprmc_compat` — GPRMC兼容性

**速度计算测试 (test_speed.c)**：
1. `test_speed_basic` — 初始状态(0速/0里程/静止)
2. `test_speed_update` — 速度更新+滤波收敛到60km/h
3. `test_speed_filter` — 60→100突变被滤波抑制
4. `test_speed_gps_invalid` — GPS失效时速度清零
5. `test_speed_max` — 最高速度记录与清零

### 11.2 真机功能测试清单

```
基本功能:
  □ 冷启动定位时间 < 60秒
  □ 热启动定位时间 < 5秒
  □ 速度精度（与车速表对比，误差<5%）
  □ 里程精度（与车辆里程表对比，误差<10%）
  □ 里程掉电保存（断电重启里程不丢）
  □ 四种显示模式正常切换
  □ 时间显示正确(UTC+8)

BLE功能:
  □ Android手机配对连接
  □ iOS手机配对连接
  □ 播放/暂停有效
  □ 上一曲/下一曲有效
  □ 音量+/-有效
  □ 断开后自动广播可重连

按键功能:
  □ 5个按键短按响应(<200ms)
  □ 长按播放键切换显示模式
  □ 长按上一曲清零里程
  □ 长按下一曲清零最高速度

稳定性:
  □ 连续运行8小时无死机
  □ GPS信号丢失恢复正常
  □ BLE反复连断不死机
  □ 温度测试(-10°C ~ 60°C)

边界条件:
  □ 高速(>100km/h)显示正常
  □ 低速/静止正确归零
  □ 里程累计到999.99正常
  □ 最高速度到999.9正常
```

---

## 12. 量产注意事项

### 12.1 烧录

- 杰理通常提供**USB批量烧录器**（一拖多）
- 固件加密：防抄板，SDK通常支持
- 出厂参数：里程清零，Flash初始化
- BLE MAC地址：确保每台设备唯一

### 12.2 出厂测试

```
1. 供电 → 电流<50mA
2. LED → 全段点亮"888888"检查缺笔
3. 按键 → 逐个验证
4. GPS → 模拟信号或等定位
5. BLE → 自动化测试设备验证广播
6. Flash → 写入读出校验
```

### 12.3 成本参考

- AC6323A: ~1-3元
- AT6558A GPS模块: ~5-15元
- TM5020A: ~0.5-2元
- 6位数码管: ~1-3元
- PCB+器件+壳: ~5-10元
- 整机BOM: **约15-30元**

---

## 13. 项目深度总结

### 13.1 架构设计的核心价值

本项目采用 **HAL四层分离架构** 是最大的设计亮点：

```
核心原则: 业务逻辑 100% 不依赖硬件

实际效果:
  app/ + lib/ → 纯C标准库，在PC上完整运行和测试
  hal.h       → 仅13个函数接口，换平台只需重写这些
  platform/   → PC模拟和真机完全独立，互不影响

量化价值:
  没有硬件的情况下 → 已完成约70%开发工作
  拿到SDK后 → 只需实现HAL层约30%工作
  如果换芯片方案(比如换成STM32) → app/和lib/完全不用改
```

### 13.2 开发方法论总结

```
传统方式: 等硬件 → 搭环境 → 写驱动 → 写业务 → 调试
  缺点: 硬件不到无法开始，所有调试在板子上，效率极低

本项目方式: 设计HAL → PC模拟 → 全部业务+测试 → SDK到手 → HAL适配 → 联调
  优点:
  ✅ 不等硬件就能开始，节省数周时间
  ✅ 核心逻辑PC反复验证，BUG少
  ✅ GUI模拟器可视化调试，效率高10倍
  ✅ 单元测试保证代码质量
  ✅ 联调只关注HAL层，问题定位精准
```

### 13.3 各模块风险评估

| 模块 | 代码量 | 真机适配风险 | 原因 |
|------|--------|-------------|------|
| nmea_parser | 213行 | 🟢低 | 纯算法，已测试 |
| app_speed | 105行 | 🟢低 | 纯算法，已测试 |
| app_key | 74行 | 🟢低 | 纯算法，GPIO简单 |
| app_display | 132行 | 🟡中 | 需验证TM5020A实际时序 |
| app_main | 113行 | 🟡中 | 需适配SDK主循环框架 |
| app_ble_hid | 77行 | 🔴高 | 完全依赖SDK BLE协议栈 |
| hal_ac6323a | 待写 | 🔴高 | 需要学习杰理SDK API |

### 13.4 风险应对策略

**BLE（最高风险）**：
1. SDK中找HID示例 → 先跑通示例 → 再改为Consumer Control
2. 手机装nRF Connect app用于BLE调试
3. 如果HID搞不定 → 先做SPP透传+手机APP方案保底

**I2C/TM5020A（中风险）**：
1. 优先用硬件I2C
2. 如果硬件I2C有问题 → 用GPIO模拟I2C
3. 用逻辑分析仪抓波形排查

**SDK学习曲线（中风险）**：
1. 先跑SDK所有示例，理解框架
2. 加入杰理技术交流群/论坛
3. SDK文档+头文件注释是主要参考

### 13.5 下一步行动清单

拿到SDK后，按以下顺序执行：

```
□ 1. 解压SDK，安装编译器和烧录工具
□ 2. 编译运行SDK自带HID示例
□ 3. 手机连接HID示例，确认BLE OK
□ 4. 创建gps_speedometer工程（基于HID示例）
□ 5. 创建 hal_ac6323a.c 文件
□ 6. 实现 hal_system_init + hal_get_tick_ms + hal_delay_ms
□ 7. 实现调试串口 printf
□ 8. 编译烧录，确认串口有输出
□ 9. 实现 hal_uart_init（GPS接收）
□ 10. 确认串口打印出NMEA原始数据
□ 11. 接入nmea_parser，打印解析结果
□ 12. 实现 hal_gpio_init + hal_gpio_read
□ 13. 测试按键输入
□ 14. 实现 hal_i2c_init + hal_i2c_write
□ 15. 数码管显示固定数字测试
□ 16. 集成app层，显示GPS速度
□ 17. 实现 hal_flash_write + hal_flash_read
□ 18. 测试里程掉电保存
□ 19. 实现 hal_ble 系列函数
□ 20. 手机连接测试BLE HID
□ 21. 全功能集成测试
□ 22. 户外实测GPS定位和速度
□ 23. 长时间稳定性测试
□ 24. 修复所有问题，准备量产
```

---

## 14. 关键代码速查表

### config.h 关键参数

| 参数 | 值 | 含义 |
|------|-----|------|
| SYS_TICK_MS | 10 | 主循环间隔 |
| GPS_UART_BAUDRATE | 9600 | GPS串口波特率 |
| GPS_TIMEZONE_OFFSET | 8 | UTC+8 |
| SPEED_FILTER_ALPHA | 0.3 | 滤波系数 |
| SPEED_MIN_VALID_KMH | 1.0 | 静止阈值 |
| MILEAGE_SAVE_INTERVAL_S | 30 | 里程保存间隔 |
| MILEAGE_FLASH_ADDR | 0x1000 | Flash地址 |
| DISPLAY_DIGITS | 6 | 数码管位数 |
| DISPLAY_REFRESH_MS | 100 | 显示刷新率 |
| KEY_DEBOUNCE_MS | 20 | 按键消抖 |
| KEY_LONG_PRESS_MS | 1500 | 长按时间 |
| BLE_DEVICE_NAME | "GPS-Speedometer" | 蓝牙名 |

### HID Consumer Control码

| 功能 | Code | 宏名 |
|------|------|------|
| 播放/暂停 | 0xCD | HID_CONSUMER_PLAY_PAUSE |
| 下一曲 | 0xB5 | HID_CONSUMER_NEXT_TRACK |
| 上一曲 | 0xB6 | HID_CONSUMER_PREV_TRACK |
| 音量+ | 0xE9 | HID_CONSUMER_VOLUME_UP |
| 音量- | 0xEA | HID_CONSUMER_VOLUME_DOWN |

### 数码管段码

```
0=0x3F  1=0x06  2=0x5B  3=0x4F  4=0x66
5=0x6D  6=0x7D  7=0x07  8=0x7F  9=0x6F
'-'=0x40  空白=0x00  小数点=|0x80
```

---

*文档完。配合上篇 DEVELOPMENT_GUIDE_PART1.md 使用。*
