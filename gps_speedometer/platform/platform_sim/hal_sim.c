/*============================================================================
 * PC模拟平台 - HAL实现
 * 用标准C模拟所有硬件接口，让业务逻辑可以在PC上调试
 *============================================================================*/

#include "../hal.h"
#include "../../config/config.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/time.h>
#endif

/*=== 系统时间模拟 ===*/
static uint32_t s_start_ms = 0;

static uint32_t get_real_ms(void)
{
#ifdef _WIN32
    return (uint32_t)GetTickCount();
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
#endif
}

uint32_t hal_get_tick_ms(void)
{
    return get_real_ms() - s_start_ms;
}

void hal_delay_ms(uint32_t ms)
{
#ifdef _WIN32
    Sleep(ms);
#else
    usleep(ms * 1000);
#endif
}

void hal_system_init(void)
{
    s_start_ms = get_real_ms();
    printf("[SIM] 系统初始化 (PC模拟模式)\n");
}

/*=== UART模拟 (GPS数据注入) ===*/
static hal_uart_rx_cb_t s_uart_rx_cb = NULL;

void hal_uart_init(uint32_t baudrate, hal_uart_rx_cb_t rx_callback)
{
    s_uart_rx_cb = rx_callback;
    printf("[SIM] UART初始化: %u bps\n", baudrate);
}

void hal_uart_send(const uint8_t *data, uint16_t len)
{
    /* 模拟：打印发送的数据 */
    printf("[SIM] UART TX: ");
    for (int i = 0; i < len; i++) printf("%c", data[i]);
    printf("\n");
}

/* 供模拟器调用：注入GPS NMEA数据 */
void sim_uart_inject(const uint8_t *data, uint16_t len)
{
    if (s_uart_rx_cb) {
        s_uart_rx_cb((uint8_t *)data, len);
    }
}

/*=== I2C模拟 (LED显示) ===*/
void hal_i2c_init(void)
{
    printf("[SIM] I2C初始化\n");
}

/* GUI模式下的显示缓冲 (sim_gui.c中定义) */
extern uint8_t g_gui_display[];
extern int     g_gui_mode;

/* 弱符号默认值（CLI模式下使用） */
#ifdef __GNUC__
uint8_t __attribute__((weak)) g_gui_display[8] = {0};
int     __attribute__((weak)) g_gui_mode = 0;
#endif

bool hal_i2c_write(uint8_t addr, const uint8_t *data, uint16_t len)
{
    (void)addr;

    /* 同步到GUI显示缓冲 */
    if (len <= 8) {
        memcpy(g_gui_display, data, len);
    }

    /* CLI模式才打印 */
    if (!g_gui_mode) {
        static const char seg_char[] = "0123456789-  ";
        static const uint8_t seg_code[] = {
            0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F, 0x40, 0x00
        };
        printf("[LED] ");
        for (int i = 0; i < len; i++) {
            uint8_t raw = data[i] & 0x7F;
            char ch = '?';
            for (int j = 0; j < 12; j++) {
                if (raw == seg_code[j]) { ch = seg_char[j]; break; }
            }
            printf("%c", ch);
            if (data[i] & 0x80) printf(".");
        }
        printf("\n");
    }
    return true;
}

bool hal_i2c_read(uint8_t addr, uint8_t *data, uint16_t len)
{
    (void)addr;
    memset(data, 0, len);
    return true;
}

/*=== Flash模拟 (文件存储) ===*/
#define SIM_FLASH_FILE  "sim_flash.bin"
#define SIM_FLASH_SIZE  4096

static uint8_t s_flash_data[SIM_FLASH_SIZE];

void hal_flash_init(void)
{
    memset(s_flash_data, 0xFF, SIM_FLASH_SIZE);
    FILE *f = fopen(SIM_FLASH_FILE, "rb");
    if (f) {
        fread(s_flash_data, 1, SIM_FLASH_SIZE, f);
        fclose(f);
        printf("[SIM] Flash数据已加载\n");
    }
}

bool hal_flash_write(uint32_t addr, const uint8_t *data, uint16_t len)
{
    if (addr + len > SIM_FLASH_SIZE) return false;
    memcpy(&s_flash_data[addr], data, len);

    FILE *f = fopen(SIM_FLASH_FILE, "wb");
    if (f) {
        fwrite(s_flash_data, 1, SIM_FLASH_SIZE, f);
        fclose(f);
    }
    return true;
}

bool hal_flash_read(uint32_t addr, uint8_t *data, uint16_t len)
{
    if (addr + len > SIM_FLASH_SIZE) return false;
    memcpy(data, &s_flash_data[addr], len);
    return true;
}

/*=== GPIO模拟 (键盘输入) ===*/
static bool s_key_states[KEY_NUM] = {false};

void hal_gpio_init(void)
{
    printf("[SIM] GPIO初始化 (按键用键盘模拟)\n");
}

bool hal_gpio_read(uint8_t pin)
{
    if (pin >= KEY_NUM) return false;
    return s_key_states[pin];
}

/* 供模拟器调用：设置按键状态 */
void sim_key_set(uint8_t key_id, bool pressed)
{
    if (key_id < KEY_NUM) {
        s_key_states[key_id] = pressed;
    }
}

/*=== BLE模拟 ===*/
static bool s_ble_connected = false;
static hal_ble_connect_cb_t s_ble_cb = NULL;

void hal_ble_init(const char *device_name, hal_ble_connect_cb_t cb)
{
    s_ble_cb = cb;
    printf("[SIM] BLE初始化: \"%s\"\n", device_name);
}

bool hal_ble_is_connected(void)
{
    return s_ble_connected;
}

bool hal_ble_hid_send_key(uint8_t key_code)
{
    if (!s_ble_connected) return false;
    if (!g_gui_mode) printf("[SIM] BLE HID: 0x%02X\n", key_code);
    return true;
}

void hal_ble_process(void)
{
    /* 模拟中无需处理 */
}

/* 供模拟器调用：模拟BLE连接/断开 */
void sim_ble_set_connected(bool connected)
{
    s_ble_connected = connected;
    if (s_ble_cb) {
        s_ble_cb(connected);
    }
}
