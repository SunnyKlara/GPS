/*============================================================================
 * BLE HID 音乐控制模块
 * 使用BLE HID Consumer Control协议模拟媒体按键
 * 手机端不需要安装APP，系统原生支持
 *============================================================================*/

#include "app_ble_hid.h"
#include "../platform/hal.h"
#include "../config/config.h"
#include <stdio.h>

static bool s_connected = false;

static void ble_connect_callback(bool connected)
{
    s_connected = connected;
    if (connected) {
        printf("[BLE] ���机已连接\n");
    } else {
        printf("[BLE] 手机已断开\n");
    }
}

void app_ble_hid_init(void)
{
    hal_ble_init(BLE_DEVICE_NAME, ble_connect_callback);
    s_connected = false;
}

void app_ble_hid_process(void)
{
    hal_ble_process();
}

bool app_ble_hid_send_action(ble_action_t action)
{
    if (!s_connected) {
        printf("[BLE] 未连接，无法发送\n");
        return false;
    }

    uint8_t key_code = 0;
    const char *action_name = "";

    switch (action) {
    case BLE_ACTION_PLAY_PAUSE:
        key_code = HID_CONSUMER_PLAY_PAUSE;
        action_name = "播放/暂停";
        break;
    case BLE_ACTION_NEXT_TRACK:
        key_code = HID_CONSUMER_NEXT_TRACK;
        action_name = "下一曲";
        break;
    case BLE_ACTION_PREV_TRACK:
        key_code = HID_CONSUMER_PREV_TRACK;
        action_name = "上一曲";
        break;
    case BLE_ACTION_VOL_UP:
        key_code = HID_CONSUMER_VOLUME_UP;
        action_name = "音量+";
        break;
    case BLE_ACTION_VOL_DOWN:
        key_code = HID_CONSUMER_VOLUME_DOWN;
        action_name = "音量-";
        break;
    }

    bool ok = hal_ble_hid_send_key(key_code);
    printf("[BLE] 发送: %s (0x%02X) %s\n", action_name, key_code, ok ? "成功" : "失败");
    return ok;
}

bool app_ble_hid_is_connected(void)
{
    return s_connected;
}
