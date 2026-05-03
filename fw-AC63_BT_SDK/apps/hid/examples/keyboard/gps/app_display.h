/*
 * app_display.h - 显示管理模块
 *
 * 上排: HH:MM:S (时间, 5位数码管 + 2组冒号)
 * 下排: XXX.X   (速度/里程, 4位数码管 + 冒号 + 小数点)
 */
#ifndef _GPS_APP_DISPLAY_H_
#define _GPS_APP_DISPLAY_H_

#include "typedef.h"

/* 显示模式 */
typedef enum {
    DISPLAY_MODE_SPEED = 0,     /* 上排:时间  下排:速度 km/h */
    DISPLAY_MODE_MILEAGE,       /* 上排:时间  下排:里程 km   */
    DISPLAY_MODE_MAX_SPEED,     /* 上排:时间  下排:最高速度  */
    DISPLAY_MODE_COUNT
} display_mode_t;

/**
 * 初始化显示模块 (调用tm1638_init)
 */
void gps_display_init(void);

/**
 * 100ms定时刷新 (由主循环调用)
 */
void gps_display_update(void);

/**
 * 切换到下一个显示模式 (循环)
 */
void gps_display_next_mode(void);

/**
 * 获取当前显示模式
 */
display_mode_t gps_display_get_mode(void);

/**
 * 设置蓝牙连接状态 (控制小数点LED闪烁)
 * @param connected 1=已连接(常亮), 0=未连接(闪烁)
 */
void gps_display_set_ble_status(u8 connected);

/**
 * 设置GPS数据 (由NMEA解析回调调用)
 */
void gps_display_set_time(u8 hour, u8 minute, u8 second, u8 valid);
void gps_display_set_speed(float speed_kmh, u8 valid);
void gps_display_set_mileage(float mileage_km);
void gps_display_set_max_speed(float max_speed_kmh);

#endif /* _GPS_APP_DISPLAY_H_ */
