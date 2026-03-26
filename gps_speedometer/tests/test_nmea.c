/*============================================================================
 * NMEA解析器单元测试
 *============================================================================*/

#include "../lib/nmea_parser.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static int s_pass = 0;
static int s_fail = 0;

#define ASSERT(cond, msg) do { \
    if (cond) { s_pass++; } \
    else { s_fail++; printf("  FAIL: %s (line %d)\n", msg, __LINE__); } \
} while(0)

#define ASSERT_FLOAT(a, b, eps, msg) ASSERT(fabs((a)-(b)) < (eps), msg)

static bool feed_string(const char *str)
{
    bool result = false;
    for (int i = 0; str[i]; i++) {
        if (nmea_parser_feed((uint8_t)str[i])) {
            result = true;
        }
    }
    return result;
}

void test_nmea_valid_gnrmc(void)
{
    printf("[TEST] NMEA解析 - 有效GNRMC语句\n");
    nmea_parser_init();

    /* 标准GNRMC语句 (校验和手动计算) */
    /* 位置: 北纬39°39.9' 东经116°16.4' 速度:30节 */
    const char *nmea = "$GNRMC,083000.00,A,3939.9000,N,11616.4000,E,30.0,45.5,260326,,,A*43\r\n";

    bool parsed = feed_string(nmea);
    ASSERT(parsed, "应成功解析");

    const nmea_gps_data_t *data = nmea_parser_get_data();
    ASSERT(data->is_valid == true, "状态应为有效");
    ASSERT(data->hour == 8, "小时=8");
    ASSERT(data->minute == 30, "分钟=30");
    ASSERT(data->second == 0, "秒=0");
    ASSERT(data->day == 26, "日=26");
    ASSERT(data->month == 3, "月=3");
    ASSERT(data->year == 2026, "年=2026");
    ASSERT_FLOAT(data->speed_knots, 30.0f, 0.1f, "速度=30节");
    ASSERT_FLOAT(data->speed_kmh, 55.56f, 0.1f, "速度≈55.56km/h");
    ASSERT_FLOAT(data->latitude, 39.665, 0.001, "纬度≈39.665");
    ASSERT_FLOAT(data->longitude, 116.2733, 0.001, "经度≈116.273");
    ASSERT(data->lat_dir == 'N', "北纬");
    ASSERT(data->lon_dir == 'E', "东经");
}

void test_nmea_invalid_status(void)
{
    printf("[TEST] NMEA解析 - 无效定位状态\n");
    nmea_parser_init();

    /* V=无效定位 */
    const char *nmea = "$GNRMC,120000.00,V,,,,,,,260326,,,N*63\r\n";

    bool parsed = feed_string(nmea);
    ASSERT(parsed, "应成功解析");

    const nmea_gps_data_t *data = nmea_parser_get_data();
    ASSERT(data->is_valid == false, "状态应为无效");
}

void test_nmea_checksum_error(void)
{
    printf("[TEST] NMEA解析 - 校验和错误\n");
    nmea_parser_init();

    /* 故意写错校验和 FF */
    const char *nmea = "$GNRMC,083000.00,A,3939.9000,N,11616.4000,E,30.0,45.5,260326,,,A*FF\r\n";

    bool parsed = feed_string(nmea);
    ASSERT(!parsed, "校验和错误应拒绝");
}

void test_nmea_gprmc_compat(void)
{
    printf("[TEST] NMEA解析 - GPRMC兼容\n");
    nmea_parser_init();

    /* GPS-only前缀 GPRMC */
    const char *nmea = "$GPRMC,100000.00,A,3939.9000,N,11616.4000,E,10.0,0.0,260326,,,A*61\r\n";

    bool parsed = feed_string(nmea);
    ASSERT(parsed, "GPRMC应能解析");

    const nmea_gps_data_t *data = nmea_parser_get_data();
    ASSERT_FLOAT(data->speed_knots, 10.0f, 0.1f, "速度=10节");
}

int test_nmea_run(void)
{
    s_pass = 0;
    s_fail = 0;

    printf("\n=== NMEA解析器测试 ===\n");
    test_nmea_valid_gnrmc();
    test_nmea_invalid_status();
    test_nmea_checksum_error();
    test_nmea_gprmc_compat();

    printf("\n结果: %d 通过, %d 失败\n", s_pass, s_fail);
    return s_fail;
}
