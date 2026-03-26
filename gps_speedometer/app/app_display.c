/*============================================================================
 * 显示管理模块
 * 根据显示模式，将速度/里程/时间格式化后输出到LED驱动
 *============================================================================*/

#include "app_display.h"
#include "app_speed.h"
#include "../lib/nmea_parser.h"
#include "../config/config.h"
#include "../platform/hal.h"
#include <stdio.h>
#include <string.h>

static display_mode_t s_mode = DISPLAY_MODE_SPEED;

/* LED显示缓冲区: 每个元素代表一个数码管的段码 */
static uint8_t s_display_buf[DISPLAY_DIGITS];

/* 7段数码管段码表 (共阴, 对应 TM5020A) */
/* 段位: dp-g-f-e-d-c-b-a */
static const uint8_t DIGIT_MAP[] = {
    0x3F, /* 0 */  0x06, /* 1 */  0x5B, /* 2 */  0x4F, /* 3 */
    0x66, /* 4 */  0x6D, /* 5 */  0x7D, /* 6 */  0x07, /* 7 */
    0x7F, /* 8 */  0x6F, /* 9 */
};
#define SEG_DASH    0x40    /* 显示 '-' */
#define SEG_BLANK   0x00    /* 空白 */
#define SEG_DP      0x80    /* 小数点 */

/* 将浮点数显示到数码管 (如 123.4) */
static void display_float(float val, int decimal_places)
{
    char str[16];
    snprintf(str, sizeof(str), "%*.*f", DISPLAY_DIGITS, decimal_places, val);

    int j = 0;
    for (int i = 0; str[i] && j < DISPLAY_DIGITS; i++) {
        if (str[i] == '.') {
            if (j > 0) s_display_buf[j - 1] |= SEG_DP;
        } else if (str[i] >= '0' && str[i] <= '9') {
            s_display_buf[j++] = DIGIT_MAP[str[i] - '0'];
        } else if (str[i] == ' ') {
            s_display_buf[j++] = SEG_BLANK;
        }
    }
}

/* 显示时间 HH.MM.SS */
static void display_time(uint8_t hh, uint8_t mm, uint8_t ss)
{
    s_display_buf[0] = DIGIT_MAP[hh / 10];
    s_display_buf[1] = DIGIT_MAP[hh % 10] | SEG_DP;  /* 用小数点代替冒号 */
    s_display_buf[2] = DIGIT_MAP[mm / 10];
    s_display_buf[3] = DIGIT_MAP[mm % 10] | SEG_DP;
    s_display_buf[4] = DIGIT_MAP[ss / 10];
    s_display_buf[5] = DIGIT_MAP[ss % 10];
}

/* 显示无效状态 "---" */
static void display_invalid(void)
{
    for (int i = 0; i < DISPLAY_DIGITS; i++) {
        s_display_buf[i] = SEG_DASH;
    }
}

void app_display_init(void)
{
    hal_i2c_init();
    memset(s_display_buf, 0, sizeof(s_display_buf));
    s_mode = DISPLAY_MODE_SPEED;
}

void app_display_update(void)
{
    const app_speed_data_t *speed = app_speed_get_data();
    const nmea_gps_data_t *gps = nmea_parser_get_data();

    memset(s_display_buf, SEG_BLANK, sizeof(s_display_buf));

    switch (s_mode) {
    case DISPLAY_MODE_SPEED:
        if (gps->is_valid) {
            display_float(speed->speed_kmh, 1);
        } else {
            display_invalid();
        }
        break;

    case DISPLAY_MODE_MILEAGE:
        display_float(speed->mileage_km, 2);
        break;

    case DISPLAY_MODE_TIME:
        if (gps->is_valid) {
            /* UTC转北京时间 */
            uint8_t local_hour = (gps->hour + GPS_TIMEZONE_OFFSET) % 24;
            display_time(local_hour, gps->minute, gps->second);
        } else {
            display_invalid();
        }
        break;

    case DISPLAY_MODE_MAX_SPEED:
        display_float(speed->max_speed_kmh, 1);
        break;

    default:
        break;
    }

    /* 将显示缓冲区写入LED驱动芯片 */
    hal_i2c_write(0x48, s_display_buf, DISPLAY_DIGITS);  /* TM5020A默认地址 */
}

void app_display_set_mode(display_mode_t mode)
{
    if (mode < DISPLAY_MODE_COUNT) {
        s_mode = mode;
    }
}

display_mode_t app_display_get_mode(void)
{
    return s_mode;
}

void app_display_next_mode(void)
{
    s_mode = (display_mode_t)((s_mode + 1) % DISPLAY_MODE_COUNT);
}
