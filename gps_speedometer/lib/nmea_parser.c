/*============================================================================
 * NMEA协议解析器实现
 * 主要解析 $GNRMC 语句（推荐最小定位信息）
 *
 * $GNRMC格式:
 * $GNRMC,hhmmss.ss,A,llll.ll,a,yyyyy.yy,a,x.x,x.x,ddmmyy,x.x,a*hh
 *   字段: 时间,状态,纬度,N/S,经度,E/W,速度(节),航向,日期,磁偏角,模式*校验
 *============================================================================*/

#include "nmea_parser.h"
#include <string.h>
#include <stdlib.h>

#define NMEA_MAX_LEN    128
#define KNOTS_TO_KMH    1.852f

/* 解析器状态 */
typedef enum {
    NMEA_STATE_IDLE,        /* 等待'$' */
    NMEA_STATE_RECEIVING,   /* 接收数据 */
    NMEA_STATE_CHECKSUM1,   /* 接收校验和第1字节 */
    NMEA_STATE_CHECKSUM2,   /* 接收校验和第2字节 */
} nmea_state_t;

static nmea_state_t s_state;
static char s_buf[NMEA_MAX_LEN];
static uint16_t s_buf_idx;
static uint8_t s_calc_checksum;
static nmea_gps_data_t s_gps_data;

/* 前向声明 */
static bool parse_gnrmc(const char *sentence);
static int  get_field(const char *sentence, int field_idx, char *out, int out_size);
static uint8_t hex_to_byte(char c);

void nmea_parser_init(void)
{
    memset(&s_gps_data, 0, sizeof(s_gps_data));
    s_state = NMEA_STATE_IDLE;
    s_buf_idx = 0;
    s_calc_checksum = 0;
}

bool nmea_parser_feed(uint8_t byte)
{
    switch (s_state) {
    case NMEA_STATE_IDLE:
        if (byte == '$') {
            s_buf_idx = 0;
            s_calc_checksum = 0;
            s_state = NMEA_STATE_RECEIVING;
        }
        break;

    case NMEA_STATE_RECEIVING:
        if (byte == '*') {
            s_buf[s_buf_idx] = '\0';
            s_state = NMEA_STATE_CHECKSUM1;
        } else if (byte == '\r' || byte == '\n') {
            /* 异常结束，重置 */
            s_state = NMEA_STATE_IDLE;
        } else {
            s_calc_checksum ^= byte;
            if (s_buf_idx < NMEA_MAX_LEN - 1) {
                s_buf[s_buf_idx++] = (char)byte;
            } else {
                s_state = NMEA_STATE_IDLE;  /* 溢出 */
            }
        }
        break;

    case NMEA_STATE_CHECKSUM1:
        s_state = NMEA_STATE_CHECKSUM2;
        /* 暂存第一个校验字符 */
        s_buf[s_buf_idx++] = (char)byte;
        break;

    case NMEA_STATE_CHECKSUM2: {
        uint8_t rx_checksum = (hex_to_byte(s_buf[s_buf_idx - 1]) << 4) | hex_to_byte((char)byte);
        s_state = NMEA_STATE_IDLE;
        s_buf[s_buf_idx - 1] = '\0';  /* 移除校验字符 */

        if (rx_checksum == s_calc_checksum) {
            /* 校验通过，判断语句类型 */
            if (strncmp(s_buf, "GNRMC", 5) == 0 ||
                strncmp(s_buf, "GPRMC", 5) == 0 ||
                strncmp(s_buf, "BDRMC", 5) == 0) {
                return parse_gnrmc(s_buf);
            }
        }
        break;
    }
    }

    return false;
}

const nmea_gps_data_t *nmea_parser_get_data(void)
{
    return &s_gps_data;
}


/*--- 内部实现 ---*/

/**
 * 从NMEA语句中提取第N个字段（逗号分隔）
 */
static int get_field(const char *sentence, int field_idx, char *out, int out_size)
{
    int cur_field = 0;
    int i = 0;
    int out_idx = 0;

    /* 跳过语句头(如"GNRMC") 到第一个逗号 */
    while (sentence[i] && sentence[i] != ',') i++;
    if (sentence[i] == ',') { i++; cur_field = 1; }

    while (sentence[i]) {
        if (cur_field == field_idx) {
            while (sentence[i] && sentence[i] != ',' && out_idx < out_size - 1) {
                out[out_idx++] = sentence[i++];
            }
            out[out_idx] = '\0';
            return out_idx;
        }
        if (sentence[i] == ',') {
            cur_field++;
        }
        i++;
    }
    out[0] = '\0';
    return 0;
}

static uint8_t hex_to_byte(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;
}

/**
 * 解析 $GNRMC 语句
 * 格式: GNRMC,hhmmss.ss,A,llll.ll,N,yyyyy.yy,E,speed,course,ddmmyy,...
 * 字段:  0     1         2 3      4 5        6 7     8      9
 */
static bool parse_gnrmc(const char *sentence)
{
    char field[32];

    /* 字段1: 时间 hhmmss.ss */
    if (get_field(sentence, 1, field, sizeof(field)) >= 6) {
        s_gps_data.hour   = (field[0] - '0') * 10 + (field[1] - '0');
        s_gps_data.minute = (field[2] - '0') * 10 + (field[3] - '0');
        s_gps_data.second = (field[4] - '0') * 10 + (field[5] - '0');
    }

    /* 字段2: 状态 A=有效 V=无效 */
    if (get_field(sentence, 2, field, sizeof(field)) > 0) {
        s_gps_data.is_valid = (field[0] == 'A');
    }

    /* 字段3+4: 纬度 + 方向 */
    if (get_field(sentence, 3, field, sizeof(field)) > 0) {
        /* 格式: ddmm.mmmm */
        double raw = atof(field);
        int degrees = (int)(raw / 100);
        double minutes = raw - degrees * 100;
        s_gps_data.latitude = degrees + minutes / 60.0;
    }
    if (get_field(sentence, 4, field, sizeof(field)) > 0) {
        s_gps_data.lat_dir = field[0];
        if (field[0] == 'S') s_gps_data.latitude = -s_gps_data.latitude;
    }

    /* 字段5+6: 经度 + 方向 */
    if (get_field(sentence, 5, field, sizeof(field)) > 0) {
        double raw = atof(field);
        int degrees = (int)(raw / 100);
        double minutes = raw - degrees * 100;
        s_gps_data.longitude = degrees + minutes / 60.0;
    }
    if (get_field(sentence, 6, field, sizeof(field)) > 0) {
        s_gps_data.lon_dir = field[0];
        if (field[0] == 'W') s_gps_data.longitude = -s_gps_data.longitude;
    }

    /* 字段7: 速度(节) */
    if (get_field(sentence, 7, field, sizeof(field)) > 0) {
        s_gps_data.speed_knots = (float)atof(field);
        s_gps_data.speed_kmh = s_gps_data.speed_knots * KNOTS_TO_KMH;
    } else {
        s_gps_data.speed_knots = 0;
        s_gps_data.speed_kmh = 0;
    }

    /* 字段8: 航向(度) */
    if (get_field(sentence, 8, field, sizeof(field)) > 0) {
        s_gps_data.course = (float)atof(field);
    }

    /* 字段9: 日期 ddmmyy */
    if (get_field(sentence, 9, field, sizeof(field)) >= 6) {
        s_gps_data.day   = (field[0] - '0') * 10 + (field[1] - '0');
        s_gps_data.month = (field[2] - '0') * 10 + (field[3] - '0');
        s_gps_data.year  = 2000 + (field[4] - '0') * 10 + (field[5] - '0');
    }

    return true;
}
