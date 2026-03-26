#ifndef _APP_SPEED_H_
#define _APP_SPEED_H_

#include <stdint.h>
#include <stdbool.h>

/* 测速与里程模块 */

typedef struct {
    float   speed_kmh;          /* 当前速度 km/h (滤波后) */
    float   speed_raw_kmh;      /* 原始速度 km/h */
    float   mileage_km;         /* 累计里程 km */
    float   max_speed_kmh;      /* 最���速度 km/h */
    bool    is_moving;          /* 是否在移动 */
} app_speed_data_t;

void app_speed_init(void);
void app_speed_update(float raw_speed_kmh, bool gps_valid);
void app_speed_save_mileage(void);
void app_speed_reset_mileage(void);
void app_speed_reset_max_speed(void);
const app_speed_data_t *app_speed_get_data(void);

#endif
