#ifndef _NMEA_PARSER_H_
#define _NMEA_PARSER_H_

/*============================================================================
 * NMEA协议解析器
 * 解析GPS模��输出的标准NMEA-0183语句
 *============================================================================*/

#include <stdint.h>
#include <stdbool.h>

/* GPS定位数据结构 */
typedef struct {
    /* 时间 (UTC) */
    uint8_t     hour;
    uint8_t     minute;
    uint8_t     second;

    /* 日期 */
    uint8_t     day;
    uint8_t     month;
    uint16_t    year;

    /* 定位状态 */
    bool        is_valid;       /* A=有效, V=无效 */

    /* 位置 */
    double      latitude;       /* 纬度 (度) */
    double      longitude;      /* 经度 (度) */
    char        lat_dir;        /* N/S */
    char        lon_dir;        /* E/W */

    /* 速度 */
    float       speed_knots;    /* 速度(节) */
    float       speed_kmh;      /* 速度(km/h) */

    /* 方向 */
    float       course;         /* 航向角(度) */

    /* 卫星 */
    uint8_t     satellites;     /* 使用中的卫星数 */
} nmea_gps_data_t;

/**
 * 初始化NMEA解析器
 */
void nmea_parser_init(void);

/**
 * 逐字节喂入数据
 * @param byte  接收到的字节
 * @return true = 解析到完整的一帧数据
 */
bool nmea_parser_feed(uint8_t byte);

/**
 * 获取最新的GPS数据
 * @return 指向GPS数据结构的指针
 */
const nmea_gps_data_t *nmea_parser_get_data(void);

#endif /* _NMEA_PARSER_H_ */
