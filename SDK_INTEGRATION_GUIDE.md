# GPS测速仪 —— SDK真机集成详细指南

> 基于实际SDK源码分析，包含精确的API调用、文件修改、引脚配置
> 2026-03-26

---

## 一、SDK关键发现（基于实际代码分析）

### 1.1 ★ SDK中已有AC6323A专用板级文件！

```
fw-AC63_BT_SDK/apps/hid/board/bd19/
├── board_config.h                    ← 板级选择开关
├── board_ac6323a_demo.c              ← ★ AC6323A板级初始化代码
├── board_ac6323a_demo_cfg.h          ← ★ AC6323A硬件配置（引脚/外设/蓝牙）
└── board_ac6323a_demo_global_build_cfg.h ← 编译配置
```

### 1.2 SDK可用的IO口（AC6323A）

```
PORTA: PA0 ~ PA9  (共10个)
PORTB: PB0 ~ PB9  (共10个)
PORTD: PD0 ~ PD4  (共5个，部分受限)
USB:   DP, DM, DP1, DM1
总计约25个GPIO
```

### 1.3 SDK的UART引脚映射（来自gpio.h）

```
UART0 (默认调试串口):
  ch0: PA5_TX / PA6_RX
  ch1: PB7_TX / PB8_RX
  ch2: PA7_TX / PA8_RX
  当前配置: PA0_TX (通过output_channel), 无RX

UART1 (可用于GPS):
  ch0: PB5_TX / PB6_RX
  ch2: PA1_TX / PA2_RX    ← 推荐GPS用这组
  ch3: USBDP  / USBDM

UART2 (可用于GPS):
  ch0: PA3_TX / PA4_RX
  ch3: PA9_TX / PA10_RX   ← 注意：与I2C默认引脚冲突！
```

### 1.4 SDK的I2C引脚（来自board_ac6323a_demo_cfg.h）

```
软件I2C（当前默认）:
  SCL = IO_PORTA_09 (PA9)
  SDA = IO_PORTA_10 (PA10) ← 注意：AC6323A没有PA10！最大PA9
  延时 = 50

硬件I2C端口选择:
  'A': DP   / DM
  'B': PA9  / PA10     ← 当前选择（但PA10可能不存在于AC6323A）
  'C': PA7  / PA8
  'D': PA5  / PA6
```

> ⚠️ **重要提示**：AC6323A的GPIO可能比AC632N少！需要根据实际原理图确认可用IO。

### 1.5 SDK的核心API映射表

| 我们的HAL函数 | SDK实际API | 头文件 |
|--------------|-----------|--------|
| `hal_system_init()` | SDK自动初始化(`board_init()`) | - |
| `hal_get_tick_ms()` | `timer_get_ms()` 或 `jiffies_to_msecs(jiffies)` | system/timer.h |
| `hal_delay_ms()` | `os_time_dly(ms/10)` 或 `delay_ms()` | os/os_api.h |
| `hal_uart_init()` | `uart_init(&uart1_data)` | asm/uart.h |
| `hal_uart_send()` | `uart_send_bytes()` | device/uart.h |
| `hal_i2c_init()` | `soft_iic_init(0)` | asm/iic_soft.h |
| `hal_i2c_write()` | `soft_iic_start()`+`soft_iic_tx_byte()`+`soft_iic_stop()` | asm/iic_soft.h |
| `hal_i2c_read()` | `soft_iic_start()`+`soft_iic_rx_byte()`+`soft_iic_stop()` | asm/iic_soft.h |
| `hal_gpio_init()` | `gpio_set_direction()`+`gpio_set_pull_up()` | asm/gpio.h |
| `hal_gpio_read()` | `gpio_read(IO_PORTx_xx)` | asm/gpio.h |
| `hal_flash_init()` | SDK的VM系统自动初始化 | - |
| `hal_flash_read()` | `syscfg_read(id, buf, len)` | - |
| `hal_flash_write()` | `syscfg_write(id, buf, len)` | - |
| `hal_ble_init()` | SDK HID框架自动初始化 | - |
| `hal_ble_is_connected()` | `ble_hid_is_connected()` 或全局状态变量 | le_common.h |
| `hal_ble_hid_send_key()` | `ble_hid_data_send(1, &key_msg, 2)` | app_keyboard.c |
| `hal_ble_process()` | SDK RTOS自动处理 | - |

---

## 二、board_config.h 修改步骤

文件路径：`fw-AC63_BT_SDK/apps/hid/board/bd19/board_config.h`

### 修改前（默认选择AC632N_DEMO）：
```c
#define CONFIG_BOARD_AC632N_DEMO          // ← 当前启用的
// #define CONFIG_BOARD_AC6323A_DEMO      // ← 需要改为这个
```

### 修改后：
```c
// #define CONFIG_BOARD_AC632N_DEMO       // ← 注释掉
#define CONFIG_BOARD_AC6323A_DEMO         // ← 取消注释，启用AC6323A
```

---

## 三、board_ac6323a_demo_cfg.h 需要修改的配置

文件路径：`fw-AC63_BT_SDK/apps/hid/board/bd19/board_ac6323a_demo_cfg.h`

### 3.1 UART配置（需新增GPS串口）

当前SDK只配置了UART0用于调试打印，我们需要**新增UART1用于GPS数据接收**：

```c
// === 保留原有UART0调试配置 ===
#define TCFG_UART0_ENABLE               ENABLE_THIS_MOUDLE
#define TCFG_UART0_RX_PORT              NO_CONFIG_PORT
#define TCFG_UART0_TX_PORT              IO_PORTA_00
#define TCFG_UART0_BAUDRATE             1000000

// === 新增：GPS UART1配置 ===
#define TCFG_UART1_ENABLE               ENABLE_THIS_MOUDLE
#define TCFG_UART1_TX_PORT              NO_CONFIG_PORT          // GPS不需要发送
#define TCFG_UART1_RX_PORT              IO_PORTA_02             // ★ GPS模块TX连接到此引脚
#define TCFG_UART1_BAUDRATE             9600                    // AT6558默认波特率
```

> ⚠️ `IO_PORTA_02` 需要根据实际PCB原理图确认！UART1 ch2的RX就是PA2。

### 3.2 I2C配置（用于TM5020A LED驱动）

```c
// 根据实际PCB修改引脚
#define TCFG_SW_I2C0_CLK_PORT           IO_PORTA_09     // ★ 根据原理图修改
#define TCFG_SW_I2C0_DAT_PORT           IO_PORTA_08     // ★ 根据原理图修改（PA10可能不存在）
#define TCFG_SW_I2C0_DELAY_CNT          50
```

### 3.3 按键配置（改用IO Key）

```c
// === 禁用AD Key ===
#define TCFG_ADKEY_ENABLE               DISABLE_THIS_MOUDLE  // ★ 改为DISABLE

// === 启用IO Key ===
#define TCFG_IOKEY_ENABLE               ENABLE_THIS_MOUDLE   // ★ 改为ENABLE

// === 配置5个IO Key引脚（根据原理图修改）===
#define KEY_NUM                         5                     // ★ 改为5

#define TCFG_IOKEY_POWER_CONNECT_WAY    ONE_PORT_TO_LOW
#define TCFG_IOKEY_POWER_ONE_PORT       IO_PORTB_00           // KEY0: 播放/暂停

#define TCFG_IOKEY_PREV_CONNECT_WAY     ONE_PORT_TO_LOW
#define TCFG_IOKEY_PREV_ONE_PORT        IO_PORTB_01           // KEY1: 上一曲

#define TCFG_IOKEY_NEXT_CONNECT_WAY     ONE_PORT_TO_LOW
#define TCFG_IOKEY_NEXT_ONE_PORT        IO_PORTB_02           // KEY2: 下一曲

// 需要新增KEY3和KEY4的定义
#define TCFG_IOKEY_VOLUP_CONNECT_WAY    ONE_PORT_TO_LOW
#define TCFG_IOKEY_VOLUP_ONE_PORT       IO_PORTB_03           // KEY3: 音量+

#define TCFG_IOKEY_VOLDN_CONNECT_WAY    ONE_PORT_TO_LOW
#define TCFG_IOKEY_VOLDN_ONE_PORT       IO_PORTB_04           // KEY4: 音量-
```

> ⚠️ 以上引脚号 `IO_PORTB_00~04` 是**占位符**，必须根据PCB原理图替换！

### 3.4 蓝牙配置（保持不变，已正确）

```c
#define TCFG_USER_BLE_ENABLE            1   // BLE使能 ✓
#define TCFG_USER_EDR_ENABLE            1   // EDR使能 ✓（双模）
#define USER_SUPPORT_PROFILE_HID        1   // HID Profile ✓
```

### 3.5 关闭不需要的模块（减少资源占用）

```c
#define TCFG_CHARGE_ENABLE              DISABLE_THIS_MOUDLE  // 不需要充电管理
#define TCFG_PWMLED_ENABLE              DISABLE_THIS_MOUDLE  // 不需要PWM LED
#define TCFG_HID_AUTO_SHUTDOWN_TIME     0                     // 不自动关机
#define TCFG_AUTO_SHUT_DOWN_TIME        0                     // 不自动关机
```

---

## 四、board_ac6323a_demo.c 需要修改的部分

### 4.1 新增UART1配置结构（GPS串口）

在现有UART0配置后面添加：

```c
/************************** GPS UART1 config ****************************/
#if TCFG_UART1_ENABLE
UART1_PLATFORM_DATA_BEGIN(uart1_data)
    .tx_pin = TCFG_UART1_TX_PORT,
    .rx_pin = TCFG_UART1_RX_PORT,
    .baudrate = TCFG_UART1_BAUDRATE,
    .flags = 0,  // 普通串口，非调试
UART1_PLATFORM_DATA_END()
#endif
```

### 4.2 新增IO Key定义（5个按键）

修改 `iokey_list[]` 数组，增加到5个按键：

```c
#if TCFG_IOKEY_ENABLE
const struct iokey_port iokey_list[] = {
    { .connect_way = TCFG_IOKEY_POWER_CONNECT_WAY,
      .key_type.one_io.port = TCFG_IOKEY_POWER_ONE_PORT,
      .key_value = 0, },  // KEY0: 播放/暂停
    { .connect_way = TCFG_IOKEY_PREV_CONNECT_WAY,
      .key_type.one_io.port = TCFG_IOKEY_PREV_ONE_PORT,
      .key_value = 1, },  // KEY1: 上一曲
    { .connect_way = TCFG_IOKEY_NEXT_CONNECT_WAY,
      .key_type.one_io.port = TCFG_IOKEY_NEXT_ONE_PORT,
      .key_value = 2, },  // KEY2: 下一曲
    { .connect_way = TCFG_IOKEY_VOLUP_CONNECT_WAY,
      .key_type.one_io.port = TCFG_IOKEY_VOLUP_ONE_PORT,
      .key_value = 3, },  // KEY3: 音量+
    { .connect_way = TCFG_IOKEY_VOLDN_CONNECT_WAY,
      .key_type.one_io.port = TCFG_IOKEY_VOLDN_ONE_PORT,
      .key_value = 4, },  // KEY4: 音量-
};
#endif
```

---

## 五、app_keyboard.c 集成修改方案

文件路径：`fw-AC63_BT_SDK/apps/hid/examples/keyboard/app_keyboard.c`

### 5.1 核心修改点

在 `hidkey_app_start()` 函数中加入我们的GPS初始化：

```c
static void hidkey_app_start()
{
    // ... 原有SDK蓝牙初始化代码保持不变 ...

    sys_key_event_enable();

    // ========== 新增：GPS测速仪初始化 ==========
    gps_speedometer_init();
    // 注册10ms定时回调驱动主循环
    sys_timer_add(NULL, gps_speedometer_loop, 10);
    // =============================================
}
```

### 5.2 新增GPS定时回调函数

```c
// GPS测速仪主循环（由SDK定时器驱动，每10ms调用一次）
static void gps_speedometer_loop(void *param)
{
    // 调用我们的主循环
    app_main_loop();
}

// GPS测速仪初始化
static void gps_speedometer_init(void)
{
    app_main_init();
}
```

### 5.3 修改按键处理

在 `hidkey_app_key_deal_test()` 中，替换原有的按键映射为我们的逻辑：

```c
static void hidkey_app_key_deal_test(u8 key_type, u8 key_value)
{
    // 将SDK按键事件转换为我们的key_event_t
    key_event_t evt;
    evt.key_id = key_value;

    if (key_type == KEY_EVENT_CLICK) {
        evt.event = KEY_EVENT_SHORT_PRESS;
    } else if (key_type == KEY_EVENT_LONG) {
        evt.event = KEY_EVENT_LONG_PRESS;
    } else {
        return;
    }

    // 调用我们的按键回调
    extern void gps_key_event_callback(key_event_t *event);
    gps_key_event_callback(&evt);
}
```

### 5.4 HID数据发送函数（供HAL层调用）

```c
// 暴露给HAL层的BLE HID发送函数
void gps_ble_hid_send(u16 key_msg)
{
    u16 key_msg_up = 0;

    if (bt_hid_mode == HID_MODE_EDR) {
#if TCFG_USER_EDR_ENABLE
        bt_comm_edr_sniff_clean();
        edr_hid_data_send(1, (u8 *)&key_msg, 2);
        os_time_dly(5);  // 50ms
        edr_hid_data_send(1, (u8 *)&key_msg_up, 2);
#endif
    } else {
#if TCFG_USER_BLE_ENABLE
        ble_hid_data_send(1, &key_msg, 2);
        os_time_dly(5);  // 50ms
        ble_hid_data_send(1, &key_msg_up, 2);
#endif
    }
}

// 暴露给HAL层的BLE连接状态查询
bool gps_ble_is_connected(void)
{
    // 检查BLE或EDR是否已连接
    if (bt_hid_mode == HID_MODE_BLE) {
        return ble_hid_is_connected();
    } else {
        return edr_hid_is_connected();
    }
}
```

---

## 六、SDK Consumer Key位域定义（来自实际代码）

```c
// 来自 app_keyboard.c 第129-137行（实际SDK代码）
#define CONSUMER_VOLUME_INC             0x0001  // bit0  音量+
#define CONSUMER_VOLUME_DEC             0x0002  // bit1  音量-
#define CONSUMER_PLAY_PAUSE             0x0004  // bit2  播放/暂停
#define CONSUMER_MUTE                   0x0008  // bit3  静音
#define CONSUMER_SCAN_PREV_TRACK        0x0010  // bit4  上一曲
#define CONSUMER_SCAN_NEXT_TRACK        0x0020  // bit5  下一曲
#define CONSUMER_SCAN_FRAME_FORWARD     0x0040  // bit6  快进
#define CONSUMER_SCAN_FRAME_BACK        0x0080  // bit7  倒退
```

我们的HAL层映射：
```c
// hal_ac6323a.c 中的映射
static u16 map_hid_key_to_consumer(uint8_t key_code)
{
    switch (key_code) {
    case HID_CONSUMER_VOLUME_UP:    return CONSUMER_VOLUME_INC;       // 0x0001
    case HID_CONSUMER_VOLUME_DOWN:  return CONSUMER_VOLUME_DEC;       // 0x0002
    case HID_CONSUMER_PLAY_PAUSE:   return CONSUMER_PLAY_PAUSE;       // 0x0004
    case HID_CONSUMER_MUTE:         return CONSUMER_MUTE;             // 0x0008
    case HID_CONSUMER_PREV_TRACK:   return CONSUMER_SCAN_PREV_TRACK;  // 0x0010
    case HID_CONSUMER_NEXT_TRACK:   return CONSUMER_SCAN_NEXT_TRACK;  // 0x0020
    default: return 0;
    }
}
```

---

## 七、GPS UART数据接收方案

### 7.1 方案：使用UART1中断接收

```c
// 在board_ac6323a_demo.c中注册UART1
// 在hal_ac6323a.c中实现接收回调

static uart_rx_callback_t g_gps_rx_cb = NULL;

// UART1接收中断回调
static void uart1_rx_isr(u8 *buf, u16 len)
{
    if (g_gps_rx_cb) {
        for (u16 i = 0; i < len; i++) {
            g_gps_rx_cb(buf[i]);
        }
    }
}

bool hal_uart_init(uint32_t baudrate, uart_rx_callback_t callback)
{
    g_gps_rx_cb = callback;

    // 使用设备驱动方式打开UART1
    // 或直接配置UART1寄存器
    // 具体方式取决于SDK版本

    return true;
}
```

### 7.2 备选方案：轮询方式

如果中断方式复杂，可在10ms定时回调中轮询UART1接收缓冲区：

```c
static void gps_speedometer_loop(void *param)
{
    // 检查UART1是否有数据
    u8 buf[64];
    int len = uart1_read(buf, sizeof(buf));
    if (len > 0 && g_gps_rx_cb) {
        for (int i = 0; i < len; i++) {
            g_gps_rx_cb(buf[i]);
        }
    }

    app_main_loop();
}
```

---

## 八、Flash存储方案（用SDK的VM系统替代直接Flash读写）

SDK提供了`syscfg_read/write` API（即VM虚拟存储），比直接操作Flash更安全：

```c
// 在user_cfg_id.h中定义我们的存储ID
#define CFG_GPS_MILEAGE_INFO    0x50    // 里程数据存储ID

// hal_ac6323a.c中实现
typedef struct {
    float mileage;
    float max_speed;
    uint32_t checksum;
} mileage_store_t;

bool hal_flash_write(uint32_t addr, const void *data, uint32_t len)
{
    mileage_store_t store;
    memcpy(&store, data, sizeof(store));
    int ret = syscfg_write(CFG_GPS_MILEAGE_INFO, (u8 *)&store, sizeof(store));
    return (ret == sizeof(store));
}

bool hal_flash_read(uint32_t addr, void *data, uint32_t len)
{
    int ret = syscfg_read(CFG_GPS_MILEAGE_INFO, (u8 *)data, len);
    return (ret > 0);
}
```

---

## 九、完整文件修改清单

### SDK中需要修改的文件（共4个）：

| # | 文件路径 | 修改内容 |
|---|---------|---------|
| 1 | `apps/hid/board/bd19/board_config.h` | 第17行注释，第21行取消注释 |
| 2 | `apps/hid/board/bd19/board_ac6323a_demo_cfg.h` | 新增UART1、修改I2C引脚、启用IO Key、禁用AD Key |
| 3 | `apps/hid/board/bd19/board_ac6323a_demo.c` | 新增UART1结构体、扩展iokey_list到5个 |
| 4 | `apps/hid/examples/keyboard/app_keyboard.c` | 新增GPS初始化/循环、修改按键处理、暴露HID发送函数 |

### 需要新增到SDK工程的文件（从我们的项目复制）：

| # | 源文件 | 复制到SDK的位置 |
|---|--------|---------------|
| 1 | `gps_speedometer/app/app_main.c` | `apps/hid/gps/app_main.c` |
| 2 | `gps_speedometer/app/app_main.h` | `apps/hid/gps/app_main.h` |
| 3 | `gps_speedometer/app/app_speed.c` | `apps/hid/gps/app_speed.c` |
| 4 | `gps_speedometer/app/app_speed.h` | `apps/hid/gps/app_speed.h` |
| 5 | `gps_speedometer/app/app_display.c` | `apps/hid/gps/app_display.c` |
| 6 | `gps_speedometer/app/app_display.h` | `apps/hid/gps/app_display.h` |
| 7 | `gps_speedometer/app/app_ble_hid.c` | `apps/hid/gps/app_ble_hid.c` |
| 8 | `gps_speedometer/app/app_ble_hid.h` | `apps/hid/gps/app_ble_hid.h` |
| 9 | `gps_speedometer/app/app_key.c` | `apps/hid/gps/app_key.c` |
| 10 | `gps_speedometer/app/app_key.h` | `apps/hid/gps/app_key.h` |
| 11 | `gps_speedometer/lib/nmea_parser.c` | `apps/hid/gps/nmea_parser.c` |
| 12 | `gps_speedometer/lib/nmea_parser.h` | `apps/hid/gps/nmea_parser.h` |
| 13 | `gps_speedometer/config/config.h` | `apps/hid/gps/gps_config.h` |
| 14 | `gps_speedometer/platform/hal.h` | `apps/hid/gps/hal.h` |
| 15 | 新建 | `apps/hid/gps/hal_ac6323a.c` ← 真机HAL实现 |

### CodeBlocks工程配置

打开 `AC632N_hid.cbp`，将上述15个文件添加到工程中（右键 → Add files）。

---

## 十、仍然需要你提供的信息

### ★ PCB引脚分配（最关键）

```
请告诉我以下信息（查看PCB原理图或板子丝印）：

1. GPS模块(AT6558A)的TX引脚连接到AC6323A的哪个引脚？
   → 推荐使用PA2 (UART1 RX)

2. LED驱动(TM5020A)的I2C连接到AC6323A的哪两个引脚？
   SCL → ?
   SDA → ?

3. 5个按键各连接到AC6323A的哪个引脚？
   KEY0(播放/暂停) → ?
   KEY1(上一曲)    → ?
   KEY2(下一曲)    → ?
   KEY3(音量+)     → ?
   KEY4(音量-)     → ?

4. TM5020A的I2C从机地址是多少？
   → 常见值: 0x48 或 0x68 (查Datasheet)
```

拿到这些信息后，我将：
1. 完成 `hal_ac6323a.c` 的全部代码
2. 修改SDK的板级配置文件中的实际引脚
3. 确保所有代码可以编译通过
