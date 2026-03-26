#ifndef _APP_DISPLAY_H_
#define _APP_DISPLAY_H_

#include <stdint.h>
#include <stdbool.h>

/* 显示模式 */
typedef enum {
    DISPLAY_MODE_SPEED,     /* 显示速度 */
    DISPLAY_MODE_MILEAGE,   /* 显示里程 */
    DISPLAY_MODE_TIME,      /* 显示时间 */
    DISPLAY_MODE_MAX_SPEED, /* 显示最高速度 */
    DISPLAY_MODE_COUNT
} display_mode_t;

void app_display_init(void);
void app_display_update(void);
void app_display_set_mode(display_mode_t mode);
display_mode_t app_display_get_mode(void);
void app_display_next_mode(void);

#endif
