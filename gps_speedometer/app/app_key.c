/*============================================================================
 * 按键处理模块（带消抖和长按检测）
 *============================================================================*/

#include "app_key.h"
#include "../platform/hal.h"
#include "../config/config.h"
#include <string.h>

typedef struct {
    uint8_t     pressed_count;      /* 消抖计数 */
    uint8_t     released_count;
    bool        is_pressed;         /* 当前状态 */
    bool        long_triggered;     /* 长按已触发标志 */
    uint32_t    press_start_tick;   /* 按下开始时间 */
} key_state_t;

static key_state_t s_keys[KEY_NUM];
static key_event_cb_t s_callback;

void app_key_init(key_event_cb_t callback)
{
    hal_gpio_init();
    memset(s_keys, 0, sizeof(s_keys));
    s_callback = callback;
}

void app_key_scan(void)
{
    uint32_t now = hal_get_tick_ms();
    uint8_t debounce_cnt = KEY_DEBOUNCE_MS / SYS_TICK_MS;

    for (int i = 0; i < KEY_NUM; i++) {
        bool raw = hal_gpio_read(i);
        key_state_t *k = &s_keys[i];

        if (raw) {
            k->released_count = 0;
            if (!k->is_pressed) {
                k->pressed_count++;
                if (k->pressed_count >= debounce_cnt) {
                    k->is_pressed = true;
                    k->long_triggered = false;
                    k->press_start_tick = now;
                }
            } else if (!k->long_triggered) {
                /* 检测长按 */
                if ((now - k->press_start_tick) >= KEY_LONG_PRESS_MS) {
                    k->long_triggered = true;
                    if (s_callback) {
                        key_event_t evt = { .key_id = i, .event = KEY_EVENT_LONG_PRESS };
                        s_callback(&evt);
                    }
                }
            }
        } else {
            k->pressed_count = 0;
            if (k->is_pressed) {
                k->released_count++;
                if (k->released_count >= debounce_cnt) {
                    if (!k->long_triggered) {
                        /* 短按释放 */
                        if (s_callback) {
                            key_event_t evt = { .key_id = i, .event = KEY_EVENT_SHORT_PRESS };
                            s_callback(&evt);
                        }
                    }
                    k->is_pressed = false;
                }
            }
        }
    }
}
