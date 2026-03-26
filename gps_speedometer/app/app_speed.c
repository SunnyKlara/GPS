/*============================================================================
 * 测速与里程计算模块
 *============================================================================*/

#include "app_speed.h"
#include "../config/config.h"
#include "../platform/hal.h"
#include <string.h>

static app_speed_data_t s_data;
static uint32_t s_last_update_tick;
static uint32_t s_last_save_tick;

/* 从Flash加载里程 */
static void load_mileage(void)
{
    float saved_mileage = 0;
    uint32_t magic = 0;

    hal_flash_read(MILEAGE_FLASH_ADDR, (uint8_t *)&magic, sizeof(magic));
    if (magic == 0xA5A5A5A5) {
        hal_flash_read(MILEAGE_FLASH_ADDR + 4, (uint8_t *)&saved_mileage, sizeof(saved_mileage));
        s_data.mileage_km = saved_mileage;
    }
}

void app_speed_init(void)
{
    memset(&s_data, 0, sizeof(s_data));
    s_last_update_tick = hal_get_tick_ms();
    s_last_save_tick = s_last_update_tick;
    load_mileage();
}

void app_speed_update(float raw_speed_kmh, bool gps_valid)
{
    uint32_t now = hal_get_tick_ms();
    uint32_t dt_ms = now - s_last_update_tick;
    s_last_update_tick = now;

    if (!gps_valid) {
        s_data.speed_raw_kmh = 0;
        s_data.speed_kmh = 0;
        s_data.is_moving = false;
        return;
    }

    /* 限幅 */
    if (raw_speed_kmh > SPEED_MAX_KMH) {
        raw_speed_kmh = SPEED_MAX_KMH;
    }

    s_data.speed_raw_kmh = raw_speed_kmh;

    /* 低通滤波: y = alpha * x + (1-alpha) * y_prev */
    s_data.speed_kmh = SPEED_FILTER_ALPHA * raw_speed_kmh +
                       (1.0f - SPEED_FILTER_ALPHA) * s_data.speed_kmh;

    /* 低速判为静止 */
    if (s_data.speed_kmh < SPEED_MIN_VALID_KMH) {
        s_data.speed_kmh = 0;
        s_data.is_moving = false;
    } else {
        s_data.is_moving = true;

        /* 更新最高速度 */
        if (s_data.speed_kmh > s_data.max_speed_kmh) {
            s_data.max_speed_kmh = s_data.speed_kmh;
        }

        /* 累计里程: distance = speed * time */
        float dt_h = (float)dt_ms / 3600000.0f;  /* ms -> hours */
        s_data.mileage_km += s_data.speed_kmh * dt_h;
    }

    /* 定时保存里程到Flash */
    if ((now - s_last_save_tick) >= (MILEAGE_SAVE_INTERVAL_S * 1000)) {
        app_speed_save_mileage();
        s_last_save_tick = now;
    }
}

void app_speed_save_mileage(void)
{
    uint32_t magic = 0xA5A5A5A5;
    hal_flash_write(MILEAGE_FLASH_ADDR, (const uint8_t *)&magic, sizeof(magic));
    hal_flash_write(MILEAGE_FLASH_ADDR + 4, (const uint8_t *)&s_data.mileage_km, sizeof(s_data.mileage_km));
}

void app_speed_reset_mileage(void)
{
    s_data.mileage_km = 0;
    app_speed_save_mileage();
}

void app_speed_reset_max_speed(void)
{
    s_data.max_speed_kmh = 0;
}

const app_speed_data_t *app_speed_get_data(void)
{
    return &s_data;
}
