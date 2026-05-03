/*
 * gps_main.h - GPS测速仪主模块
 */
#ifndef _GPS_MAIN_H_
#define _GPS_MAIN_H_

#include "typedef.h"

/* GPS模块初始化 */
void gps_module_init(void);

/* 100ms定时回调 */
void gps_module_loop(void);

/* 按键事件处理 */
void gps_key_event(u8 key_type, u8 key_value);

/* BLE连接状态通知 */
void gps_ble_status_notify(u8 connected);

/* 电话状态通知: 0=无电话, 1=来电, 2=通话中, 3=挂断 */
void gps_phone_status_notify(u8 status);

/* 来电号码通知 (用于回拨) */
void gps_phone_number_notify(u8 *args, u8 len);

/* GPS数据更新 (供GPS UART模块调用) */
void gps_speed_update(float speed_kmh, u8 valid);
void gps_mileage_update(float delta_km);

#endif /* _GPS_MAIN_H_ */
