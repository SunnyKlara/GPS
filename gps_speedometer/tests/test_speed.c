/*============================================================================
 * 速度与里程计算单元测试
 *============================================================================*/

#include "../app/app_speed.h"
#include "../platform/hal.h"
#include <stdio.h>
#include <math.h>

static int s_pass = 0;
static int s_fail = 0;

#define ASSERT(cond, msg) do { \
    if (cond) { s_pass++; } \
    else { s_fail++; printf("  FAIL: %s (line %d)\n", msg, __LINE__); } \
} while(0)

#define ASSERT_FLOAT(a, b, eps, msg) ASSERT(fabs((double)(a)-(double)(b)) < (eps), msg)

void test_speed_basic(void)
{
    printf("[TEST] 速度计算 - 基本功能\n");
    app_speed_init();

    /* 静止状态 */
    const app_speed_data_t *data = app_speed_get_data();
    ASSERT_FLOAT(data->speed_kmh, 0, 0.01, "初始速度=0");
    ASSERT_FLOAT(data->mileage_km, 0, 0.01, "初始里程=0");
    ASSERT(data->is_moving == false, "初始状态静止");
}

void test_speed_update(void)
{
    printf("[TEST] 速度计算 - 更新速度\n");
    app_speed_init();

    /* 模拟GPS上报60km/h */
    app_speed_update(60.0f, true);
    hal_delay_ms(100);

    const app_speed_data_t *data = app_speed_get_data();
    /* 第一次更新：滤波后 = 0.3*60 + 0.7*0 = 18 */
    ASSERT(data->speed_kmh > 0, "速度应>0");
    ASSERT(data->is_moving == true, "应标记为移动中");

    /* 多次更新让滤波收敛 */
    for (int i = 0; i < 20; i++) {
        hal_delay_ms(100);
        app_speed_update(60.0f, true);
    }
    ASSERT_FLOAT(data->speed_kmh, 60.0, 2.0, "稳定后速度≈60km/h");
}

void test_speed_filter(void)
{
    printf("[TEST] 速度计算 - 低通滤波\n");
    app_speed_init();

    /* 先稳定在60 */
    for (int i = 0; i < 30; i++) {
        hal_delay_ms(100);
        app_speed_update(60.0f, true);
    }

    /* 突然跳变到100，滤波应平滑过渡 */
    hal_delay_ms(100);
    app_speed_update(100.0f, true);
    const app_speed_data_t *data = app_speed_get_data();
    ASSERT(data->speed_kmh < 90.0f, "滤波应抑制突变");
    ASSERT(data->speed_kmh > 60.0f, "滤波后应上升");
}

void test_speed_gps_invalid(void)
{
    printf("[TEST] 速度计算 - GPS无效\n");
    app_speed_init();

    /* 先有速度 */
    for (int i = 0; i < 10; i++) {
        hal_delay_ms(100);
        app_speed_update(80.0f, true);
    }

    /* GPS失效 */
    app_speed_update(0, false);
    const app_speed_data_t *data = app_speed_get_data();
    ASSERT_FLOAT(data->speed_kmh, 0, 0.01, "GPS无效时速度清零");
    ASSERT(data->is_moving == false, "GPS无效时标记静止");
}

void test_speed_max(void)
{
    printf("[TEST] 速度计算 - 最高速度\n");
    app_speed_init();

    for (int i = 0; i < 30; i++) {
        hal_delay_ms(100);
        app_speed_update(120.0f, true);
    }
    for (int i = 0; i < 30; i++) {
        hal_delay_ms(100);
        app_speed_update(60.0f, true);
    }

    const app_speed_data_t *data = app_speed_get_data();
    ASSERT(data->max_speed_kmh > 100.0f, "最高速度应>100");

    app_speed_reset_max_speed();
    ASSERT_FLOAT(data->max_speed_kmh, 0, 0.01, "清零后最高速度=0");
}

int test_speed_run(void)
{
    s_pass = 0;
    s_fail = 0;

    printf("\n=== 速度与里程测试 ===\n");
    test_speed_basic();
    test_speed_update();
    test_speed_filter();
    test_speed_gps_invalid();
    test_speed_max();

    printf("\n结果: %d 通过, %d 失败\n", s_pass, s_fail);
    return s_fail;
}
