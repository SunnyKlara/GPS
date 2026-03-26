#ifndef _APP_BLE_HID_H_
#define _APP_BLE_HID_H_

#include <stdbool.h>

/* BLE HID音乐控制动作 */
typedef enum {
    BLE_ACTION_PLAY_PAUSE,
    BLE_ACTION_NEXT_TRACK,
    BLE_ACTION_PREV_TRACK,
    BLE_ACTION_VOL_UP,
    BLE_ACTION_VOL_DOWN,
} ble_action_t;

void app_ble_hid_init(void);
void app_ble_hid_process(void);
bool app_ble_hid_send_action(ble_action_t action);
bool app_ble_hid_is_connected(void);

#endif
