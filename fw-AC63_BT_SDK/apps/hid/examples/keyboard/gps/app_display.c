/*
 * app_display.c - 显示管理模块实现
 *
 * 布局:
 *   上排: SEG1(时十) SEG2(时个) [冒号] SEG3(分十) SEG4(分个) [冒号] SEG5(秒个)
 *   下排: SEG6(千)   SEG7(百)   [冒号] SEG8(十)   SEG9(个)   [小数点]
 *
 * 注意: 杰理编译器只支持C89, 变量必须在函数开头声明!
 */
#include "system/includes.h"
#include "drv_tm1638.h"
#include "app_display.h"

#define LOG_TAG     "[DISP]"
#define LOG_INFO_ENABLE
#include "debug.h"

/* 显示状态 */
static display_mode_t s_mode = DISPLAY_MODE_SPEED;
static u8 s_ble_connected = 0;
static u8 s_colon_toggle = 0;      /* 冒号闪烁状态 */
static u8 s_ble_led_toggle = 0;    /* 蓝牙LED闪烁状态 */
static u8 s_tick_100ms = 0;        /* 100ms计数器 */

/* GPS数据缓存 */
static u8 s_hour = 0, s_minute = 0, s_second = 0;
static u8 s_time_valid = 0;
static float s_speed_kmh = 0;
static u8 s_speed_valid = 0;
static float s_mileage_km = 0;
static float s_max_speed_kmh = 0;

/* 显示缓冲区 */
static u8 s_disp_buf[TM1638_RAM_SIZE];

/*--- 数据设置接口 ---*/

void gps_display_set_time(u8 hour, u8 minute, u8 second, u8 valid)
{
    s_hour = hour;
    s_minute = minute;
    s_second = second;
    s_time_valid = valid;
}

void gps_display_set_speed(float speed_kmh, u8 valid)
{
    s_speed_kmh = speed_kmh;
    s_speed_valid = valid;
}

void gps_display_set_mileage(float mileage_km)
{
    s_mileage_km = mileage_km;
}

void gps_display_set_max_speed(float max_speed_kmh)
{
    s_max_speed_kmh = max_speed_kmh;
}

void gps_display_set_ble_status(u8 connected)
{
    s_ble_connected = connected;
}

/*--- 内部: 填充上排时间 ---*/
static void fill_time_row(void)
{
    u8 bj_hour;

    if (!s_time_valid) {
        /* GPS无效: 显示 --:--:- */
        tm1638_set_digit(s_disp_buf, DISP_POS_H10, 0xFE);
        tm1638_set_digit(s_disp_buf, DISP_POS_H01, 0xFE);
        tm1638_set_digit(s_disp_buf, DISP_POS_M10, 0xFE);
        tm1638_set_digit(s_disp_buf, DISP_POS_M01, 0xFE);
        tm1638_set_digit(s_disp_buf, DISP_POS_S01, 0xFE);
        return;
    }

    /* UTC+8 北京时间 */
    bj_hour = (s_hour + 8) % 24;

    tm1638_set_digit(s_disp_buf, DISP_POS_H10, bj_hour / 10);
    tm1638_set_digit(s_disp_buf, DISP_POS_H01, bj_hour % 10);
    tm1638_set_digit(s_disp_buf, DISP_POS_M10, s_minute / 10);
    tm1638_set_digit(s_disp_buf, DISP_POS_M01, s_minute % 10);
    tm1638_set_digit(s_disp_buf, DISP_POS_S01, s_second % 10);
}

/*--- 内部: 填充下排4位数值 ---*/
/* 显示整数值 (0~9999), 前导零消隐 */
static void fill_value_4digit(int value)
{
    u8 d3, d2, d1, d0;

    if (value < 0) value = 0;
    if (value > 9999) value = 9999;

    d3 = value / 1000;
    d2 = (value / 100) % 10;
    d1 = (value / 10) % 10;
    d0 = value % 10;

    /* 前导零消隐 */
    tm1638_set_digit(s_disp_buf, DISP_POS_KM3, d3 ? d3 : 0xFF);
    tm1638_set_digit(s_disp_buf, DISP_POS_KM2, (d3 || d2) ? d2 : 0xFF);
    tm1638_set_digit(s_disp_buf, DISP_POS_KM1, (d3 || d2 || d1) ? d1 : 0xFF);
    tm1638_set_digit(s_disp_buf, DISP_POS_KM0, d0);
}

/* 显示带1位小数的值 (xxx.x), 范围0.0~999.9 */
static void fill_value_1decimal(float value)
{
    int int_val;
    u8 d2, d1, d0, df;

    if (value < 0) value = 0;
    if (value > 999.9f) value = 999.9f;

    int_val = (int)(value * 10 + 0.5f);  /* 四舍五入 */
    if (int_val > 9999) int_val = 9999;

    d2 = int_val / 1000;
    d1 = (int_val / 100) % 10;
    d0 = (int_val / 10) % 10;
    df = int_val % 10;

    /* 千位(SEG6)=百位, 百位(SEG7)=十位, 十位(SEG8)=个位, 个位(SEG9)=小数 */
    tm1638_set_digit(s_disp_buf, DISP_POS_KM3, d2 ? d2 : 0xFF);
    tm1638_set_digit(s_disp_buf, DISP_POS_KM2, (d2 || d1) ? d1 : 0xFF);
    tm1638_set_digit(s_disp_buf, DISP_POS_KM1, d0);
    tm1638_set_digit(s_disp_buf, DISP_POS_KM0, df);
    /* 小数点在SEG8和SEG9之间, 后面通过SEG10控制 */
}

/* 下排显示 ---- (无效) */
static void fill_value_dash(void)
{
    tm1638_set_digit(s_disp_buf, DISP_POS_KM3, 0xFE);
    tm1638_set_digit(s_disp_buf, DISP_POS_KM2, 0xFE);
    tm1638_set_digit(s_disp_buf, DISP_POS_KM1, 0xFE);
    tm1638_set_digit(s_disp_buf, DISP_POS_KM0, 0xFE);
}

/*--- 内部: 填充下排 (根据模式) ---*/
static void fill_bottom_row(void)
{
    switch (s_mode) {
    case DISPLAY_MODE_SPEED:
        /* GPS未定位时也显示0.0，而不是--:--，用户体验更好 */
        fill_value_1decimal(s_speed_kmh);
        break;

    case DISPLAY_MODE_MILEAGE:
        /* 里程: 显示到0.1km */
        fill_value_1decimal(s_mileage_km);
        break;

    case DISPLAY_MODE_MAX_SPEED:
        if (s_speed_valid) {
            fill_value_1decimal(s_max_speed_kmh);
        } else {
            fill_value_dash();
        }
        break;

    default:
        fill_value_dash();
        break;
    }
}

/*--- 公开接口 ---*/

void gps_display_init(void)
{
    int i;
    tm1638_init();
    s_mode = DISPLAY_MODE_SPEED;
    s_tick_100ms = 0;
    for (i = 0; i < TM1638_RAM_SIZE; i++) {
        s_disp_buf[i] = 0;
    }
    log_info("GPS display init OK");
}

void gps_display_update(void)
{
    int i;

    s_tick_100ms++;

    /* 每500ms切换冒号闪烁 */
    if (s_tick_100ms % 5 == 0) {
        s_colon_toggle = !s_colon_toggle;
    }

    /* 蓝牙LED: 未连接时每500ms闪烁 */
    if (!s_ble_connected && (s_tick_100ms % 5 == 0)) {
        s_ble_led_toggle = !s_ble_led_toggle;
    }

    /* 清空缓冲区 */
    for (i = 0; i < TM1638_RAM_SIZE; i++) {
        s_disp_buf[i] = 0;
    }

    /* 填充上排时间 */
    fill_time_row();

    /* 填充下排数据 */
    fill_bottom_row();

    /*
     * 冒号/小数点控制 (SEG10 = 高字节bit1)
     * 已测试确认的GRID映射:
     *   GRID1=冒号1上点  GRID2=冒号1下点
     *   GRID3=冒号2上点  GRID4=冒号2下点
     *   GRID5=冒号3上点  GRID6=冒号3下点
     *   GRID7=小数点
     */

    /* 冒号1 (时:分之间): GRID1上 + GRID2下, 跟随闪烁 */
    if (s_colon_toggle) {
        tm1638_set_seg10(s_disp_buf, 0, 1);  /* GRID1=冒号1上 */
        tm1638_set_seg10(s_disp_buf, 1, 1);  /* GRID2=冒号1下 */
    }

    /* 冒号2 (分:秒之间): GRID3上 + GRID4下, 跟随闪烁 */
    if (s_colon_toggle) {
        tm1638_set_seg10(s_disp_buf, 2, 1);  /* GRID3=冒号2上 */
        tm1638_set_seg10(s_disp_buf, 3, 1);  /* GRID4=冒号2下 */
    }

    /* 冒号3 (下排百:十之间): GRID5上 + GRID6下, 常亮 */
    tm1638_set_seg10(s_disp_buf, 4, 1);  /* GRID5=冒号3上 */
    tm1638_set_seg10(s_disp_buf, 5, 1);  /* GRID6=冒号3下 */

    /* 小数点 (GRID7): 复用为蓝牙连接指示灯 */
    /* 连接=常亮, 断开=闪烁 */
    if (s_ble_connected) {
        tm1638_set_seg10(s_disp_buf, 6, 1);  /* 常亮 */
    } else {
        tm1638_set_seg10(s_disp_buf, 6, s_ble_led_toggle);  /* 闪烁 */
    }

    /* 写入硬件 */
    tm1638_write_all(s_disp_buf);
}

void gps_display_next_mode(void)
{
    s_mode = (display_mode_t)((s_mode + 1) % DISPLAY_MODE_COUNT);
    log_info("Display mode: %d", s_mode);
}

display_mode_t gps_display_get_mode(void)
{
    return s_mode;
}
