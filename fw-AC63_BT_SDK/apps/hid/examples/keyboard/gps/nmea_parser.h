/*
 * nmea_parser.h - NMEA解析器 (C89兼容, 杰理SDK适配版)
 * 解析 $GNRMC/$GPRMC/$BDRMC 语句
 */
#ifndef _GPS_NMEA_PARSER_H_
#define _GPS_NMEA_PARSER_H_

#include "typedef.h"

/* GPS数据结构 */
typedef struct {
    u8  hour;           /* UTC时间 */
    u8  minute;
    u8  second;
    u8  day;            /* 日期 */
    u8  month;
    u16 year;
    u8  is_valid;       /* 1=定位有效(A), 0=无效(V) */
    float speed_knots;  /* 速度(节) */
    float speed_kmh;    /* 速度(km/h) */
} nmea_gps_data_t;

/* 初始化 */
void nmea_parser_init(void);

/* 逐字节喂入, 返回1=解析到完整帧 */
u8 nmea_parser_feed(u8 byte);

/* 获取最新数据 */
const nmea_gps_data_t *nmea_parser_get_data(void);

#endif
