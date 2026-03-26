/*============================================================================
 * 主应用 - 任务调度和事件处理
 *============================================================================*/

#include "app_main.h"
#include "app_speed.h"
#include "app_display.h"
#include "app_ble_hid.h"
#include "app_key.h"
#include "../lib/nmea_parser.h"
#include "../platform/hal.h"
#include "../config/config.h"
#include <stdio.h>

static uint32_t s_last_display_tick;
static uint32_t s_last_gps_tick;

/* GPS UART接收回调 */
static void gps_uart_rx_callback(uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        if (nmea_parser_feed(data[i])) {
            /* 收到完整的GPS帧，更新速度 */
            const nmea_gps_data_t *gps = nmea_parser_get_data();
            app_speed_update(gps->speed_kmh, gps->is_valid);
        }
    }
}

/* 按键事件回调 */
static void key_event_callback(const key_event_t *event)
{
    if (event->event == KEY_EVENT_SHORT_PRESS) {
        switch (event->key_id) {
        case KEY_PLAY_PAUSE:
            app_ble_hid_send_action(BLE_ACTION_PLAY_PAUSE);
            break;
        case KEY_PREV_TRACK:
            app_ble_hid_send_action(BLE_ACTION_PREV_TRACK);
            break;
        case KEY_NEXT_TRACK:
            app_ble_hid_send_action(BLE_ACTION_NEXT_TRACK);
            break;
        case KEY_VOL_UP:
            app_ble_hid_send_action(BLE_ACTION_VOL_UP);
            break;
        case KEY_VOL_DOWN:
            app_ble_hid_send_action(BLE_ACTION_VOL_DOWN);
            break;
        }
    } else if (event->event == KEY_EVENT_LONG_PRESS) {
        switch (event->key_id) {
        case KEY_PLAY_PAUSE:
            /* 长按播放键: 切换显示模式 */
            app_display_next_mode();
            printf("[APP] 显示模式切换\n");
            break;
        case KEY_PREV_TRACK:
            /* 长按上一曲: 清零里程 */
            app_speed_reset_mileage();
            printf("[APP] 里程已清零\n");
            break;
        case KEY_NEXT_TRACK:
            /* 长按下一曲: 清零最高速度 */
            app_speed_reset_max_speed();
            printf("[APP] 最高速度已清零\n");
            break;
        }
    }
}

void app_main_init(void)
{
    /* 系统初始化 */
    hal_system_init();
    printf("========================================\n");
    printf("  GPS测速仪 v1.0\n");
    printf("  AC6323A + AT6558A + TM5020A\n");
    printf("========================================\n");

    /* 模块初始化 */
    nmea_parser_init();
    app_speed_init();
    app_display_init();
    app_ble_hid_init();
    app_key_init(key_event_callback);

    /* 启动GPS UART */
    hal_uart_init(GPS_UART_BAUDRATE, gps_uart_rx_callback);

    s_last_display_tick = hal_get_tick_ms();
    s_last_gps_tick = s_last_display_tick;

    printf("[APP] 系统初始化完成，等待GPS定位...\n");
}

void app_main_loop(void)
{
    uint32_t now = hal_get_tick_ms();

    /* 按键扫描 (~10ms) */
    app_key_scan();

    /* BLE协议栈处理 */
    app_ble_hid_process();

    /* 显示刷新 */
    if ((now - s_last_display_tick) >= DISPLAY_REFRESH_MS) {
        s_last_display_tick = now;
        app_display_update();
    }
}
