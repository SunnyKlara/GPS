#ifndef _HAL_H_
#define _HAL_H_

/*============================================================================
 * 硬件抽象层接口 (HAL)
 * 所有硬件操作通过此接口隔离，方便模拟和真机切换
 *============================================================================*/

#include <stdint.h>
#include <stdbool.h>

/*--- UART (GPS通信) ---*/
typedef void (*hal_uart_rx_cb_t)(uint8_t *data, uint16_t len);

void hal_uart_init(uint32_t baudrate, hal_uart_rx_cb_t rx_callback);
void hal_uart_send(const uint8_t *data, uint16_t len);

/*--- I2C (LED驱动通信) ---*/
void hal_i2c_init(void);
bool hal_i2c_write(uint8_t addr, const uint8_t *data, uint16_t len);
bool hal_i2c_read(uint8_t addr, uint8_t *data, uint16_t len);

/*--- GPIO (按键) ---*/
void hal_gpio_init(void);
bool hal_gpio_read(uint8_t pin);     /* true=按下, false=释放 */

/*--- Flash (掉电存储) ---*/
void hal_flash_init(void);
bool hal_flash_write(uint32_t addr, const uint8_t *data, uint16_t len);
bool hal_flash_read(uint32_t addr, uint8_t *data, uint16_t len);

/*--- BLE ---*/
typedef void (*hal_ble_connect_cb_t)(bool connected);

void hal_ble_init(const char *device_name, hal_ble_connect_cb_t cb);
bool hal_ble_is_connected(void);
bool hal_ble_hid_send_key(uint8_t key_code);
void hal_ble_process(void);     /* BLE协议栈轮询处理 */

/* BLE HID Consumer Control Key Codes (USB HID Usage Table) */
#define HID_CONSUMER_PLAY_PAUSE     0xCD
#define HID_CONSUMER_NEXT_TRACK     0xB5
#define HID_CONSUMER_PREV_TRACK     0xB6
#define HID_CONSUMER_VOLUME_UP      0xE9
#define HID_CONSUMER_VOLUME_DOWN    0xEA
#define HID_CONSUMER_MUTE           0xE2

/*--- 系统 ---*/
uint32_t hal_get_tick_ms(void);     /* 获取系统tick(ms) */
void hal_delay_ms(uint32_t ms);
void hal_system_init(void);

#endif /* _HAL_H_ */
