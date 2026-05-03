/*
 * nmea_parser.c - NMEA解析器 (C89兼容)
 *
 * 解析 $GNRMC/$GPRMC/$BDRMC 提取时间和速度
 * 不用atof/double, 手动解析浮点数
 */
#include "system/includes.h"
#include "nmea_parser.h"
#include <string.h>

#define NMEA_MAX_LEN    128
#define KNOTS_TO_KMH_X1000  1852  /* 1.852 × 1000 */

/* 状态机 */
#define ST_IDLE     0
#define ST_RECV     1
#define ST_CHK1     2
#define ST_CHK2     3

static u8 s_state;
static char s_buf[NMEA_MAX_LEN];
static u16 s_idx;
static u8 s_checksum;
static nmea_gps_data_t s_data;

/* 手动解析浮点: "123.45" → 12345, 小数位数=2 */
static long parse_decimal(const char *s, int *dec_places)
{
    long val = 0;
    int dp = 0;
    int after_dot = 0;

    while (*s) {
        if (*s == '.') {
            after_dot = 1;
        } else if (*s >= '0' && *s <= '9') {
            val = val * 10 + (*s - '0');
            if (after_dot) dp++;
        } else {
            break;
        }
        s++;
    }
    *dec_places = dp;
    return val;
}

static u8 hex_char(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;
}

/* 提取第N个逗号分隔字段 */
static int get_field(const char *s, int idx, char *out, int maxlen)
{
    int cur = 0;
    int i = 0;
    int o = 0;

    /* 跳过语句头到第一个逗号 */
    while (s[i] && s[i] != ',') i++;
    if (s[i] == ',') { i++; cur = 1; }

    while (s[i]) {
        if (cur == idx) {
            while (s[i] && s[i] != ',' && o < maxlen - 1) {
                out[o++] = s[i++];
            }
            out[o] = '\0';
            return o;
        }
        if (s[i] == ',') cur++;
        i++;
    }
    out[0] = '\0';
    return 0;
}

/* 解析GNRMC */
static u8 parse_rmc(const char *buf)
{
    char f[20];
    long val;
    int dp;

    /* 字段1: 时间 hhmmss.ss */
    if (get_field(buf, 1, f, sizeof(f)) >= 6) {
        s_data.hour   = (f[0] - '0') * 10 + (f[1] - '0');
        s_data.minute = (f[2] - '0') * 10 + (f[3] - '0');
        s_data.second = (f[4] - '0') * 10 + (f[5] - '0');
    }

    /* 字段2: 状态 A/V */
    if (get_field(buf, 2, f, sizeof(f)) > 0) {
        s_data.is_valid = (f[0] == 'A') ? 1 : 0;
    }

    /* 字段7: 速度(节) */
    if (get_field(buf, 7, f, sizeof(f)) > 0) {
        val = parse_decimal(f, &dp);
        /* val是整数部分×10^dp, 转成float */
        s_data.speed_knots = (float)val;
        while (dp > 0) { s_data.speed_knots /= 10.0f; dp--; }
        /* 节 → km/h */
        s_data.speed_kmh = s_data.speed_knots * 1.852f;
    } else {
        s_data.speed_knots = 0;
        s_data.speed_kmh = 0;
    }

    /* 字段9: 日期 ddmmyy */
    if (get_field(buf, 9, f, sizeof(f)) >= 6) {
        s_data.day   = (f[0] - '0') * 10 + (f[1] - '0');
        s_data.month = (f[2] - '0') * 10 + (f[3] - '0');
        s_data.year  = 2000 + (f[4] - '0') * 10 + (f[5] - '0');
    }

    return 1;
}

/*--- 公开接口 ---*/

void nmea_parser_init(void)
{
    memset(&s_data, 0, sizeof(s_data));
    s_state = ST_IDLE;
    s_idx = 0;
    s_checksum = 0;
}

u8 nmea_parser_feed(u8 byte)
{
    switch (s_state) {
    case ST_IDLE:
        if (byte == 0x24) {  /* '$' = ASCII 0x24 */
            s_idx = 0;
            s_checksum = 0;
            s_state = ST_RECV;
        }
        break;

    case ST_RECV:
        if (byte == '*') {
            s_buf[s_idx] = '\0';
            s_state = ST_CHK1;
        } else if (byte == '\r' || byte == '\n') {
            s_state = ST_IDLE;
        } else {
            s_checksum ^= byte;
            if (s_idx < NMEA_MAX_LEN - 1) {
                s_buf[s_idx++] = (char)byte;
            } else {
                s_state = ST_IDLE;
            }
        }
        break;

    case ST_CHK1:
        s_buf[s_idx] = (char)byte;  /* 暂存 */
        s_state = ST_CHK2;
        break;

    case ST_CHK2:
        {
            u8 rx_chk;
            rx_chk = (hex_char(s_buf[s_idx]) << 4) | hex_char((char)byte);
            s_state = ST_IDLE;

            /* 暂时跳过校验和验证, 直接解析 */
            s_buf[s_idx] = '\0';
            if (strncmp(s_buf, "GNRMC", 5) == 0 ||
                strncmp(s_buf, "GPRMC", 5) == 0 ||
                strncmp(s_buf, "BDRMC", 5) == 0) {
                return parse_rmc(s_buf);
            }
        }
        break;
    }
    return 0;
}

const nmea_gps_data_t *nmea_parser_get_data(void)
{
    return &s_data;
}
