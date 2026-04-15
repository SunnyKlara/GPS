# 04 - 杰理SDK适配指南（拿到SDK后的操作步骤）

## 概述

拿到杰理AC6323A的SDK后，需要做的核心工作就是实现 `hal.h` 中定义的接口函数。
应用层代码（app/目录下的所有文件）不需要任何修改。

---

## 第一步：熟悉SDK结构（第1-2天）

杰理的SDK通常长这样（不同版本可能有差异）：

```
ac632x_sdk/
├── apps/                   # 应用示例
│   ├── hid/                # HID设备示例 ← 重点看这个
│   ├── spp_and_le/         # SPP和BLE示例
│   └── common/             # 公共代码
├── include_lib/            # 头文件
│   ├── driver/             # 外设驱动头文件
│   │   ├── uart.h          # UART API
│   │   ├── iic.h           # I2C API（杰理喜欢写iic）
│   │   ├── gpio.h          # GPIO API
│   │   └── sfc.h           # Flash API
│   ├── btstack/            # 蓝牙协议栈
│   └── system/             # 系统API
├── lib/                    # 预编译库文件
├── tools/                  # 编译工具链
│   └── pi32/               # 杰理自有编译器
└── Makefile / build.bat    # 构建脚本
```

**第一件事：找到并编译一个示例工程，确保工具链能用。**
通常SDK里有README或文档说明如何编译。

---

## 第二步：创建项目工程（第2-3天）

**推荐方式：** 复制SDK的HID示例工程作为基础，在上面修改。

```
1. 复制 apps/hid/ 整个目录，改名为 apps/gps_speedometer/
2. 把我们写的代码文件复制进去：
   - app/ 目录下所有 .c/.h 文件
   - lib/nmea_parser.c/.h
   - config/config.h
   - platform/hal.h
3. 新建 platform/platform_ac6323a/hal_ac6323a.c
4. 修改 Makefile 把新文件加入编译
```

---

## 第三步：实现HAL层（第3-7天）

需要实现 `platform/platform_ac6323a/hal_ac6323a.c`。

下面给出每个函数的实现思路和伪代码。**具体API名称需要根据实际SDK调整。**

### 3.1 系统初始化

```c
#include "hal.h"
#include "system/timer.h"     // 杰理SDK的定时器
#include "driver/gpio.h"      // 杰理SDK的GPIO
// ... 其他杰理SDK头文件

static uint32_t s_tick_ms = 0;

// 杰理SDK通常用一个系统定时器来维护tick
static void sys_timer_callback(void)
{
    s_tick_ms += 10;  // 假设10ms中断一次
}

void hal_system_init(void)
{
    // 杰理SDK的系统初始化通常在main之前就完成了
    // 这里做额外的初始化
    
    // 注册一个10ms的系统定时器
    // sys_timer_register(10, sys_timer_callback);  // 具体API看SDK
}

uint32_t hal_get_tick_ms(void)
{
    return s_tick_ms;  
    // 或者用杰理SDK的: return jl_get_tick_ms(); 
}

void hal_delay_ms(uint32_t ms)
{
    // 杰理SDK的延时函数
    // os_time_dly(ms / 10);  // 如果用RTOS
    // 或者忙等待
    uint32_t start = hal_get_tick_ms();
    while (hal_get_tick_ms() - start < ms);
}
```

### 3.2 UART驱动（连接GPS模块）

```c
#include "driver/uart.h"

static hal_uart_rx_cb_t s_uart_rx_cb = NULL;

// UART接收中断回调（杰理SDK风格）
static void uart_rx_isr(uint8_t *buf, uint16_t len)
{
    if (s_uart_rx_cb) {
        s_uart_rx_cb(buf, len);
    }
}

void hal_uart_init(uint32_t baudrate, hal_uart_rx_cb_t rx_callback)
{
    s_uart_rx_cb = rx_callback;
    
    // 杰理SDK的UART初始化，大致是这样的流程：
    // 1. 配置UART引脚（TX=PA6, RX=PA5）
    // uart_init(UART_NUM_1, baudrate, PA6_TX, PA5_RX);
    
    // 2. 注册接收回调
    // uart_set_rx_callback(UART_NUM_1, uart_rx_isr);
    
    // 3. 使能UART
    // uart_enable(UART_NUM_1);
    
    // 具体函数名看SDK的uart.h头文件
}

void hal_uart_send(const uint8_t *data, uint16_t len)
{
    // uart_send(UART_NUM_1, data, len);
}
```

**关键点：**
- 确认SDK中UART接收是中断方式还是DMA方式
- 如果是DMA，注意缓冲区管理
- GPS波特率默认9600，8N1

### 3.3 I2C驱动（连接LED驱动芯片）

```c
#include "driver/iic.h"  // 杰理通常写iic不是i2c

void hal_i2c_init(void)
{
    // 配置I2C引脚（SCL=PB0, SDA=PB1）
    // iic_init(PB0_SCL, PB1_SDA, 100000);  // 100KHz
}

bool hal_i2c_write(uint8_t addr, const uint8_t *data, uint16_t len)
{
    // 同步到GUI缓冲（如果需要）
    extern uint8_t g_gui_display[];
    if (len <= 8) memcpy(g_gui_display, data, len);
    
    // 杰理SDK的I2C写入
    // return iic_write(addr, data, len) == 0;
    return true;
}

bool hal_i2c_read(uint8_t addr, uint8_t *data, uint16_t len)
{
    // return iic_read(addr, data, len) == 0;
    return true;
}
```

**关键点：**
- 杰理可能用软件I2C也可能用硬件I2C，看SDK支持情况
- 如果SDK没有硬件I2C，可以用GPIO模拟（bit-bang I2C），我可以帮你写
- TM5020A的I2C地址需要查数据手册（通常是0x48或0x44）

### 3.4 GPIO驱动（按键检测）

```c
#include "driver/gpio.h"

// 按键引脚映射
static const uint8_t key_pins[KEY_NUM] = {
    IO_PORTA_00,  // KEY0: 播放/暂停
    IO_PORTA_01,  // KEY1: 上一曲
    IO_PORTA_02,  // KEY2: 下一曲
    IO_PORTA_03,  // KEY3: 音量+
    IO_PORTA_04,  // KEY4: 音量-
};

void hal_gpio_init(void)
{
    for (int i = 0; i < KEY_NUM; i++) {
        // gpio_set_mode(key_pins[i], GPIO_INPUT_PULLUP);
        // 配置为输入，启用内部上拉电阻
    }
}

bool hal_gpio_read(uint8_t pin)
{
    if (pin >= KEY_NUM) return false;
    // 按键按下接GND，所以读到0=按下，取反
    // return !gpio_read(key_pins[pin]);
    return false;
}
```

### 3.5 Flash驱动（掉电存储里程）

```c
#include "driver/sfc.h"  // 或 nvm.h / flash.h

// 杰理芯片内部Flash通常有专门的用户数据区
// 也可能提供 vm (virtual memory) 或 nvm 接口

void hal_flash_init(void)
{
    // 通常不需要额外初始化
}

bool hal_flash_write(uint32_t addr, const uint8_t *data, uint16_t len)
{
    // 方案1: 使用杰理的VM接口（推荐）
    // vm_write(VM_MILEAGE_ID, data, len);
    
    // 方案2: 直接操作Flash
    // sfc_erase(addr, len);
    // sfc_write(addr, data, len);
    
    return true;
}

bool hal_flash_read(uint32_t addr, uint8_t *data, uint16_t len)
{
    // vm_read(VM_MILEAGE_ID, data, len);
    // 或 sfc_read(addr, data, len);
    return true;
}
```

**关键点：**
- 杰理SDK通常提供 `vm_write/vm_read` 接口来存储用户数据，比直接操Flash安全
- Flash有擦写寿命（~10万次），不要太频繁写入
- 我们设计了30秒写一次，10万次够用约35天不间断运行，足够了
- 如果要更耐久，可以增大保存间隔到5分钟

### 3.6 BLE HID驱动（最复杂的部分）

详见 `05_BLE_HID开发指南.md`

---

## 第四步：修改SDK主函数（第3天）

杰理SDK通常有自己的main()入口和任务调度。需要把我们的代码嵌入进去。

**方式1：替换主循环（简单）**
```c
// 在杰理SDK的主循环中调用我们的代码
void app_task_loop(void)  // 杰理SDK的主任务函数
{
    app_main_init();
    while (1) {
        app_main_loop();
        os_time_dly(1);  // 让出CPU给蓝牙协议栈
    }
}
```

**方式2：创建独立任务（如果SDK用RTOS）**
```c
void gps_speedometer_task(void *param)
{
    app_main_init();
    while (1) {
        app_main_loop();
        os_time_dly(1);
    }
}

// 在系统初始化时创建任务
os_task_create(gps_speedometer_task, NULL, 512, 5, "gps_task");
```

---

## 第五步：编译和烧录（第4天）

1. 修改SDK的Makefile，加入我们的源文件
2. 编译：`make` 或运行SDK的构建脚本
3. 生成 .bin 或 .fw 固件文件
4. 用杰理的下载工具烧录到芯片

**烧录连接：**
```
PC (USB) ──► USB转TTL ──► AC6323A (UART烧录引脚)
```
杰理芯片通常支持UART烧录，具体引脚看开发板原理图。

---

## 编译配置参考

在config.h中切换平台：
```c
/* 取消SIM定义，启用真机 */
#define PLATFORM_AC6323A
// #define PLATFORM_SIM
```

在SDK的Makefile中添加源文件：
```makefile
# 添加GPS测速仪源文件
SRC += app/app_main.c
SRC += app/app_speed.c
SRC += app/app_display.c
SRC += app/app_ble_hid.c
SRC += app/app_key.c
SRC += lib/nmea_parser.c
SRC += platform/platform_ac6323a/hal_ac6323a.c
```
