#ifndef _APP_KEY_H_
#define _APP_KEY_H_

#include <stdint.h>

/* 按键事件类型 */
typedef enum {
    KEY_EVENT_NONE,
    KEY_EVENT_SHORT_PRESS,
    KEY_EVENT_LONG_PRESS,
} key_event_type_t;

typedef struct {
    uint8_t             key_id;
    key_event_type_t    event;
} key_event_t;

typedef void (*key_event_cb_t)(const key_event_t *event);

void app_key_init(key_event_cb_t callback);
void app_key_scan(void);   /* 周期调用，建议10-20ms */

#endif
