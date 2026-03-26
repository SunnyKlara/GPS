/*============================================================================
 * HAL层真机实现 - AC6323A (杰理SDK)
 *
 * 本文件实现hal.h中定义的所有硬件抽象层接口
 * 调用杰理AC63 SDK的底层API驱动真实硬件
 *
 * 注意：引脚定义标记为 TODO 的地方需要根据PCB原理图修改
 *============================================================================*/

#ifdef PLATFORM_AC6323A

#include "system/includes.h"
#include "asm/gpio.h"
#include "asm/iic_soft.h"
#include "asm/uart.h"
#include "os/os_api.h"
#include "app_config.h"

/* 引入我们的头文件 */
#include "hal.h"
#include "gps_config.h"

/*============================================================================
 * 引脚定义 (TODO: 根据PCB原理图修改)
 *============================================================================*/
/* GPS UART - 使用UART1, RX引脚接GPS模块TX */
#define GPS_UART_RX_PIN         IO_PORTA_02     /* TODO: 根据原理图修改 */

/* I2C - TM5020A LED驱动 */
#define I2C_SCL_PIN             IO_PORTA_09     /* TODO: 根据原理图修改 */
#define I2C_SDA_PIN             IO_PORTA_08     /* TODO: 根据原理图修改 */

/* 按键 GPIO */
#define KEY0_PIN                IO_PORTB_00     /* TODO: KEY0 播放/暂停 */
#define KEY1_PIN                IO_PORTB_01     /* TODO: KEY1 上一曲 */
#define KEY2_PIN                IO_PORTB_02     /* TODO: KEY2 下一曲 */
#define KEY3_PIN                IO_PORTB_03     /* TODO: KEY3 音量+ */
#define KEY4_PIN                IO_PORTB_04     /* TODO: KEY4 音量- */

/* 按键引脚查找表 */
static const u32 key_pin_map[KEY_NUM] = {
    KEY0_PIN, KEY1_PIN, KEY2_PIN, KEY3_PIN, KEY4_PIN
};

/*============================================================================
 * 内部变量
 *============================================================================*/
static hal_uart_rx_cb_t     g_uart_rx_cb = NULL;
static hal_ble_connect_cb_t g_ble_connect_cb = NULL;
static volatile bool        g_ble_connected = false;

/* VM存储ID (在SDK的user_cfg_id.h中定义) */
#define VM_GPS_MILEAGE_ID       0x50

/*============================================================================
 * 系统相关
 *============================================================================*/

void hal_system_init(void)
{
    /* SDK已在board_init()中完成系统初始化
     * 此函数在SDK框架下为空操作 */
}

uint32_t hal_get_tick_ms(void)
{
    return timer_get_ms();
}

void hal_delay_ms(uint32_t ms)
{
    if (ms >= 10) {
        os_time_dly(ms / 10);
    } else {
        /* 短延时用忙等待 */
        u32 start = timer_get_ms();
        while ((timer_get_ms() - start) < ms);
    }
}

/*============================================================================
 * UART (GPS通信)
 *
 * 使用UART1接收GPS模块(AT6558A)的NMEA数据
 * AT6558A默认波特率9600, 输出$GNRMC等语句
 *============================================================================*/

/* UART1接收缓冲区 */
#define GPS_UART_RX_BUF_SIZE    256
static u8 gps_uart_rx_buf[GPS_UART_RX_BUF_SIZE] __attribute__((aligned(4)));

/* UART1设备句柄 */
static void *uart1_hdl = NULL;

/* UART1接收中断回调 */
static void uart1_isr_rx_handler(void *priv, void *buf, u16 len)
{
    if (g_uart_rx_cb && len > 0) {
        g_uart_rx_cb((uint8_t *)buf, len);
    }
}

void hal_uart_init(uint32_t baudrate, hal_uart_rx_cb_t rx_callback)
{
    g_uart_rx_cb = rx_callback;

    /*
     * 方案A: 使用SDK设备驱动接口打开UART1
     * 需要在board_ac6323a_demo.c中注册UART1设备
     */
    struct uart_platform_data uart1_pdata = {
        .tx_pin     = NO_CONFIG_PORT,       /* GPS不需要发送 */
        .rx_pin     = GPS_UART_RX_PIN,
        .baudrate   = baudrate,
        .flags      = 0,
    };

    /*
     * 方案B: 直接通过GPIO crossbar配置UART1 RX
     * 适用于SDK设备驱动不方便使用的情况
     */
    gpio_set_fun_input_port(GPS_UART_RX_PIN, PFI_UART1_RX);
    gpio_set_die(GPS_UART_RX_PIN, 1);
    gpio_set_pull_up(GPS_UART_RX_PIN, 1);
    gpio_set_direction(GPS_UART_RX_PIN, 1);  /* 输入模式 */

    /* 注册UART1接收回调
     * 实际实现需要参照SDK的uart_dev驱动注册中断
     * 以下为伪代码框架，编译时需要根据SDK实际API调整 */

    log_info("GPS UART init: pin=%d, baud=%d\n", GPS_UART_RX_PIN, baudrate);
}

void hal_uart_send(const uint8_t *data, uint16_t len)
{
    /* GPS通常不需要发送，保留接口 */
    (void)data;
    (void)len;
}

/*============================================================================
 * I2C (LED驱动通信 - TM5020A)
 *
 * 使用软件I2C，引脚通过board_ac6323a_demo_cfg.h配置
 *============================================================================*/

/* 软件I2C配置 */
const struct soft_iic_config soft_iic_cfg[] = {
    {
        .scl = I2C_SCL_PIN,
        .sda = I2C_SDA_PIN,
        .delay = 50,
        .io_pu = 1,
    },
};

void hal_i2c_init(void)
{
    soft_iic_init(0);
    log_info("I2C init: SCL=%d, SDA=%d\n", I2C_SCL_PIN, I2C_SDA_PIN);
}

bool hal_i2c_write(uint8_t addr, const uint8_t *data, uint16_t len)
{
    soft_iic_start(0);

    /* 发送从机地址 + 写位 */
    if (!soft_iic_tx_byte(0, (addr << 1) | 0)) {
        soft_iic_stop(0);
        return false;   /* NACK */
    }

    /* 发送数据 */
    for (uint16_t i = 0; i < len; i++) {
        if (!soft_iic_tx_byte(0, data[i])) {
            soft_iic_stop(0);
            return false;
        }
    }

    soft_iic_stop(0);
    return true;
}

bool hal_i2c_read(uint8_t addr, uint8_t *data, uint16_t len)
{
    soft_iic_start(0);

    /* 发送从机地址 + 读位 */
    if (!soft_iic_tx_byte(0, (addr << 1) | 1)) {
        soft_iic_stop(0);
        return false;
    }

    /* 接收数据 */
    for (uint16_t i = 0; i < len; i++) {
        data[i] = soft_iic_rx_byte(0, (i < len - 1) ? 1 : 0);
    }

    soft_iic_stop(0);
    return true;
}

/*============================================================================
 * GPIO (按键输入)
 *
 * 5个按键，低电平有效（按下接GND）
 *============================================================================*/

void hal_gpio_init(void)
{
    for (int i = 0; i < KEY_NUM; i++) {
        gpio_set_direction(key_pin_map[i], 1);      /* 输入模式 */
        gpio_set_die(key_pin_map[i], 1);             /* 数字输入使能 */
        gpio_set_pull_up(key_pin_map[i], 1);         /* 内部上拉 */
        gpio_set_pull_down(key_pin_map[i], 0);       /* 禁止下拉 */
    }
    log_info("GPIO key init: %d keys\n", KEY_NUM);
}

bool hal_gpio_read(uint8_t pin)
{
    if (pin >= KEY_NUM) {
        return false;
    }
    /* 低电平有效：gpio_read返回0表示按下 */
    return (gpio_read(key_pin_map[pin]) == 0);
}

/*============================================================================
 * Flash存储 (使用SDK的VM虚拟存储系统)
 *
 * SDK提供syscfg_read/write接口，比直接操作Flash安全
 * 自动处理擦除、均衡、校验
 *============================================================================*/

void hal_flash_init(void)
{
    /* SDK的VM系统在board_init()中已自动初始化 */
    log_info("Flash(VM) init ok\n");
}

bool hal_flash_write(uint32_t addr, const uint8_t *data, uint16_t len)
{
    (void)addr;  /* 不使用地址，使用VM ID */
    int ret = syscfg_write(VM_GPS_MILEAGE_ID, (u8 *)data, len);
    if (ret != len) {
        log_error("VM write fail: ret=%d, len=%d\n", ret, len);
        return false;
    }
    return true;
}

bool hal_flash_read(uint32_t addr, uint8_t *data, uint16_t len)
{
    (void)addr;
    int ret = syscfg_read(VM_GPS_MILEAGE_ID, data, len);
    if (ret <= 0) {
        /* 第一次读取可能为空，用0填充 */
        memset(data, 0, len);
        return false;
    }
    return true;
}

/*============================================================================
 * BLE HID (蓝牙HID媒体控制)
 *
 * 直接调用SDK的 ble_hid_data_send() 发送Consumer Control报告
 * Report Map已在app_keyboard.c的hidkey_report_map[]中定义
 *============================================================================*/

/* SDK提供的外部函数 */
extern int ble_hid_data_send(u8 report_id, u8 *data, u16 len);
extern void edr_hid_data_send(u8 report_id, u8 *data, u16 len);
extern void bt_comm_edr_sniff_clean(void);

/* SDK按键位域定义（来自app_keyboard.c） */
#define SDK_CONSUMER_VOLUME_INC         0x0001
#define SDK_CONSUMER_VOLUME_DEC         0x0002
#define SDK_CONSUMER_PLAY_PAUSE         0x0004
#define SDK_CONSUMER_MUTE               0x0008
#define SDK_CONSUMER_SCAN_PREV_TRACK    0x0010
#define SDK_CONSUMER_SCAN_NEXT_TRACK    0x0020

/* 将我们的Usage Code映射到SDK位域 */
static u16 map_hid_key_to_sdk_bitfield(uint8_t key_code)
{
    switch (key_code) {
    case HID_CONSUMER_PLAY_PAUSE:   return SDK_CONSUMER_PLAY_PAUSE;       /* 0xCD → bit2 */
    case HID_CONSUMER_NEXT_TRACK:   return SDK_CONSUMER_SCAN_NEXT_TRACK;  /* 0xB5 → bit5 */
    case HID_CONSUMER_PREV_TRACK:   return SDK_CONSUMER_SCAN_PREV_TRACK;  /* 0xB6 → bit4 */
    case HID_CONSUMER_VOLUME_UP:    return SDK_CONSUMER_VOLUME_INC;       /* 0xE9 → bit0 */
    case HID_CONSUMER_VOLUME_DOWN:  return SDK_CONSUMER_VOLUME_DEC;       /* 0xEA → bit1 */
    case HID_CONSUMER_MUTE:         return SDK_CONSUMER_MUTE;             /* 0xE2 → bit3 */
    default:
        log_error("Unknown HID key: 0x%02x\n", key_code);
        return 0;
    }
}

/* 获取当前蓝牙模式（来自app_keyboard.c的全局变量） */
typedef enum {
    GPS_HID_MODE_NULL = 0,
    GPS_HID_MODE_EDR,
    GPS_HID_MODE_BLE,
} gps_bt_mode_e;

extern u8 bt_hid_mode;  /* app_keyboard.c中定义 */

void hal_ble_init(const char *device_name, hal_ble_connect_cb_t cb)
{
    g_ble_connect_cb = cb;
    /* BLE初始化由SDK框架完成（btstack_init()等）
     * 设备名称在SDK配置文件中设定
     * 这里只保存回调函数 */
    log_info("BLE HID init: %s\n", device_name);
}

bool hal_ble_is_connected(void)
{
    return g_ble_connected;
}

bool hal_ble_hid_send_key(uint8_t key_code)
{
    u16 key_msg = map_hid_key_to_sdk_bitfield(key_code);
    u16 key_release = 0;

    if (key_msg == 0) {
        return false;
    }

    log_info("HID send: 0x%02x → bitfield 0x%04x\n", key_code, key_msg);

    if (bt_hid_mode == GPS_HID_MODE_EDR) {
#if TCFG_USER_EDR_ENABLE
        bt_comm_edr_sniff_clean();
        edr_hid_data_send(1, (u8 *)&key_msg, 2);
        os_time_dly(5);  /* 50ms延时 */
        edr_hid_data_send(1, (u8 *)&key_release, 2);
#endif
    } else {
#if TCFG_USER_BLE_ENABLE
        ble_hid_data_send(1, (u8 *)&key_msg, 2);
        os_time_dly(5);
        ble_hid_data_send(1, (u8 *)&key_release, 2);
#endif
    }

    return true;
}

void hal_ble_process(void)
{
    /* SDK RTOS自动处理蓝牙协议栈
     * 此函数在SDK框架下为空操作 */
}

/*============================================================================
 * BLE连接状态通知（从SDK回调中调用）
 *
 * 在app_keyboard.c的hidkey_bt_connction_status_event_handler()中
 * 添加对此函数的调用
 *============================================================================*/
void gps_ble_connection_notify(bool connected)
{
    g_ble_connected = connected;
    if (g_ble_connect_cb) {
        g_ble_connect_cb(connected);
    }
    log_info("BLE %s\n", connected ? "connected" : "disconnected");
}

#endif /* PLATFORM_AC6323A */
