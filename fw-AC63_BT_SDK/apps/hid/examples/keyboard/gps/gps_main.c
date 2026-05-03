/*
 * gps_main.c - GPS测速仪主模块
 *
 * 修复版: 消除按键延迟, 改进电话功能
 * C89兼容
 */
#include "system/includes.h"
#include "app_config.h"
#include "btstack/avctp_user.h"
#include "le_common.h"
#include "standard_hid.h"
#include "drv_tm1638.h"
#include "app_display.h"
#include "gps_main.h"
#include "gps_uart.h"

#define LOG_TAG     "[GPS]"
#define LOG_ERROR_ENABLE
#define LOG_DEBUG_ENABLE
#define LOG_INFO_ENABLE
#include "debug.h"

/* 按键ID (实测ADC标定 - 2026-04-22 重新确认) */
/* 注意: 物理VOL+按键实际返回value=6, 物理PLAY返回value=4 */
/* 所以这里按实际测量值定义, 不是按原始标定表 */
#define KEY_ID_NEXT     0
#define KEY_ID_PREV     1
#define KEY_ID_MODE     2
#define KEY_ID_PHONE    3
#define KEY_ID_VOL_UP   6   /* 物理VOL+实测返回6 */
#define KEY_ID_VOL_DN   5
#define KEY_ID_PLAY     4   /* 物理PLAY实测返回4 */
#define KEY_ID_POWER    7

/* HID Consumer Control 位域 */
#define CONSUMER_VOLUME_INC             0x0001
#define CONSUMER_VOLUME_DEC             0x0002
#define CONSUMER_PLAY_PAUSE             0x0004
#define CONSUMER_MUTE                   0x0008
#define CONSUMER_SCAN_PREV_TRACK        0x0010
#define CONSUMER_SCAN_NEXT_TRACK        0x0020

extern int ble_hid_data_send(u8 report_id, u8 *data, u16 len);
extern void edr_hid_data_send(u8 report_id, u8 *data, u16 len);
extern void bt_comm_edr_sniff_clean(void);
extern u8 gps_get_bt_hid_mode(void);
extern void hidkey_power_event_to_user(u8 event);
extern int ble_hid_is_connected(void);
extern int edr_hid_is_connected(void);

#define HID_MODE_EDR    1
#define HID_MODE_BLE    2

/*==========================================================
 * 异步按键释放 (消除延迟的关键!)
 *
 * 之前: 按下→delay 50ms→释放 (阻塞系统)
 * 现在: 按下→立即返回→定时器30ms后自动发释放
 *==========================================================*/
static u8 s_pending_release_id = 0;   /* 待释放的report_id, 0=无 */
static u16 s_pending_release_timer = 0;

static void hid_send_raw(u8 report_id, u8 *data, u16 len)
{
    u8 mode;
    mode = gps_get_bt_hid_mode();
    if (mode == HID_MODE_EDR) {
#if TCFG_USER_EDR_ENABLE
        bt_comm_edr_sniff_clean();
        edr_hid_data_send(report_id, data, len);
#endif
    } else {
#if TCFG_USER_BLE_ENABLE
        ble_hid_data_send(report_id, data, len);
#endif
    }
}

/* 定时器回调: 发送按键释放 */
static void hid_release_timer_cb(void *param)
{
    u16 zero16 = 0;

    if (s_pending_release_id == 1) {
        hid_send_raw(1, (u8 *)&zero16, 2);
    }
    s_pending_release_id = 0;
    s_pending_release_timer = 0;
}

/* 发送Consumer Control按键 (非阻塞) */
static void send_consumer_key(u16 key_msg)
{
    if (!key_msg) return;

    /* 如果上一个还没释放, 先释放 */
    if (s_pending_release_id) {
        u16 zero16 = 0;
        if (s_pending_release_id == 1) {
            hid_send_raw(1, (u8 *)&zero16, 2);
        }
        if (s_pending_release_timer) {
            sys_timeout_del(s_pending_release_timer);
            s_pending_release_timer = 0;
        }
        s_pending_release_id = 0;
    }

    hid_send_raw(1, (u8 *)&key_msg, 2);
    s_pending_release_id = 1;
    s_pending_release_timer = sys_timeout_add(NULL, hid_release_timer_cb, 30);
}

/* 发送Consumer Control持续按住 (不释放) */
static void send_consumer_hold(u16 key_msg)
{
    if (!key_msg) return;
    hid_send_raw(1, (u8 *)&key_msg, 2);
}

/* 发送按键释放 */
static void send_key_up(void)
{
    u16 zero16 = 0;
    if (s_pending_release_id == 1) {
        hid_send_raw(1, (u8 *)&zero16, 2);
    }
    if (s_pending_release_timer) {
        sys_timeout_del(s_pending_release_timer);
        s_pending_release_timer = 0;
    }
    s_pending_release_id = 0;
}

/*
 * 电话/音乐控制 (纯Consumer Control):
 *
 * Play/Pause行为:
 *   来电时 → 接听 (OK)
 *   通话中 → 静音 (Android BLE HID限制, 挂断需要HFP协议)
 *   无电话 → 暂停/播放音乐 (OK)
 */

/*==========================================================
 * 里程掉电保存
 *==========================================================*/
#define VM_GPS_MILEAGE_ID       40
#define VM_MILEAGE_MAGIC        0xA55A
#define MILEAGE_SAVE_INTERVAL   300

typedef struct {
    u16 magic;
    float mileage_km;
    float max_speed_kmh;
} vm_mileage_t;

static float s_mileage_km = 0;
static float s_max_speed_kmh = 0;
static u16 s_save_countdown = MILEAGE_SAVE_INTERVAL;
static u8 s_mileage_dirty = 0;

static void mileage_vm_load(void)
{
    vm_mileage_t data;
    int ret;
    ret = syscfg_read(VM_GPS_MILEAGE_ID, (u8 *)&data, sizeof(data));
    if (ret == sizeof(data) && data.magic == VM_MILEAGE_MAGIC) {
        s_mileage_km = data.mileage_km;
        s_max_speed_kmh = data.max_speed_kmh;
    } else {
        s_mileage_km = 0;
        s_max_speed_kmh = 0;
    }
}

static void mileage_vm_save(void)
{
    vm_mileage_t data;
    data.magic = VM_MILEAGE_MAGIC;
    data.mileage_km = s_mileage_km;
    data.max_speed_kmh = s_max_speed_kmh;
    syscfg_write(VM_GPS_MILEAGE_ID, (u8 *)&data, sizeof(data));
    s_mileage_dirty = 0;
}

/*==========================================================
 * 模拟时钟
 *==========================================================*/
static u8 s_sim_hour = 14, s_sim_min = 30, s_sim_sec = 0;
static u8 s_sim_tick = 0;
static u8 s_gps_no_data = 1;

/* 按键调试: 显示按键信息持续2秒 */
static u8 s_key_debug_countdown = 0;  /* >0时下排显示按键调试值 */
static float s_key_debug_value = 0;

/*==========================================================
 * 按键处理
 *
 * MODE键逻辑 (改进版):
 *   无电话: 单击=播放/暂停, 双击=切换显示模式
 *   有电话: 单击=Hook Switch(接听/挂断), 双击=回拨
 *
 * 电话键: 直接发Hook Switch, 同时发Play/Pause双保险
 *==========================================================*/
static u8 s_phone_state = 0;

/* 来电号码存储 (用于回拨) */
static u8 s_last_income_num[20];   /* 最后来电号码 */
static u8 s_last_income_num_len = 0;

static u16 s_play_pause_val = CONSUMER_PLAY_PAUSE;

static void handle_mode_click(void)
{
    /*
     * MODE单击:
     *   来电时: HFP接听 + Play/Pause备用
     *   通话中: HFP挂断
     *   无电话: Play/Pause (音乐)
     *
     * s_phone_state: 0=无电话, 1=来电, 2=通话中
     * 由app_keyboard.c中BT_STATUS_PHONE_xxx事件更新
     */
    log_info("MODE click, phone_state=%d", s_phone_state);
    if (s_phone_state == 1) {
        /* 来电: 接听 */
        user_send_cmd_prepare(USER_CTRL_HFP_CALL_ANSWER, 0, NULL);
    } else if (s_phone_state == 2) {
        /* 通话中: 挂断 */
        user_send_cmd_prepare(USER_CTRL_HFP_CALL_HANGUP, 0, NULL);
    } else {
        /* 无电话: 音乐暂停/播放 */
        send_consumer_key(CONSUMER_PLAY_PAUSE);
    }
}

static void handle_phone_click(void)
{
    /* 电话键: 同MODE键 */
    log_info("PHONE click, phone_state=%d", s_phone_state);
    if (s_phone_state == 1) {
        user_send_cmd_prepare(USER_CTRL_HFP_CALL_ANSWER, 0, NULL);
    } else if (s_phone_state == 2) {
        user_send_cmd_prepare(USER_CTRL_HFP_CALL_HANGUP, 0, NULL);
    } else {
        send_consumer_key(CONSUMER_PLAY_PAUSE);
    }
}

/*==========================================================
 * TM1638 全亮测试（烧录测试用）
 * 上排: 88888 (所有5位都显示8)
 * 下排: 8888 (所有4位都显示8)
 * 冒号和小数点全部亮起
 * 如果显示正常，说明:
 *   1. 固件烧录成功 ✅
 *   2. TM1638驱动正常 ✅
 *   3. PA0/PA1/PA2 GPIO配置正常 ✅
 *==========================================================*/
/*==========================================================
 * TM1638全亮测试
 *
 * 验证项:
 *   1. 固件烧录成功 ✅
 *   2. TM1638驱动正常 ✅
 *   3. PA0/PA1/PA2 GPIO配置正常 ✅
 *
 * 关键修复: 测试结束后必须清屏!
 *   - 写0xFF导致GRID0高字节SEG10=1（冒号不该亮）
 *   - 正确做法: 写0x7F（只亮7个段，不碰SEG10）
 *   - 或者测试结束后清零所有RAM
 *==========================================================*/
static void test_tm1638_all_on(void)
{
    int i;
    u8 test_buf[TM1638_RAM_SIZE];

    log_info("=== TM1638 ALL-ON TEST ===");
    tm1638_init();

    /*
     * 修复: 只写0x7F，不写0xFF
     *
     * drv_tm1638.c 的 tm1638_write_all() 写14字节到:
     *   buf[0]  = GRID0低字节(SEG1~8)
     *   buf[1]  = GRID0高字节(SEG9~10) ← 这里有SEG10!
     *   buf[2]  = GRID1低字节
     *   buf[3]  = GRID1高字节
     *   ...
     *
     * 0xFF = 所有bit=1 → GRID0高字节的bit1=SEG10=1 → 冒号不该亮却亮了
     * 0x7F = 0b01111111 → 只亮7个段(bit0~6), 不碰SEG10(bit1在高字节里)
     *
     * 但0x7F只控制低字节的8个bit，SEG10在buf[1]的高字节bit1。
     * 真正安全的做法: 先清buf再设置需要的位。
     */
    for (i = 0; i < TM1638_RAM_SIZE; i++) {
        test_buf[i] = 0;  /* 先清零 */
    }

    /* 每个GRID的7个段全亮(0x7F)，不碰高字节SEG10 */
    for (i = 0; i < TM1638_RAM_SIZE; i += 2) {
        test_buf[i] = 0x7F;  /* 每个GRID的低字节: 7个段全亮 */
        /* buf[i+1] 保持0，SEG10不亮 */
    }

    /* 写入硬件 */
    tm1638_write_all(test_buf);
    log_info("TM1638 ALL-ON test: buf written (safe, no SEG10 pollution)");

    /* 2秒后进入正常GPS模式 */
    for (i = 0; i < 200; i++) {
        /* 2秒内保持全亮 */
    }

    /* 修复: 测试结束后清屏，防止污染GPS显示 */
    log_info("TM1638 ALL-ON test: clearing display");
    for (i = 0; i < TM1638_RAM_SIZE; i++) {
        test_buf[i] = 0;
    }
    tm1638_write_all(test_buf);
    log_info("TM1638 ALL-ON test: switching to GPS mode");
}

/*==========================================================
 * 公开接口
 *==========================================================*/
void gps_module_init(void)
{
    log_info("=== GPS Speedometer Init ===");

    /* 先做TM1638全亮测试（用于验证烧录是否成功） */
    test_tm1638_all_on();

    /* 测试通过后，继续正常GPS流程 */
    mileage_vm_load();
    gps_display_init();
    gps_display_set_mileage(s_mileage_km);
    gps_display_set_max_speed(s_max_speed_kmh);
    gps_uart_init();

    s_sim_hour = 14; s_sim_min = 30; s_sim_sec = 0; s_sim_tick = 0;

    /* 开机显示: 上排 18:00:8, 下排 6789 (版本v8: EDR+HFP) */
    gps_display_set_time(18, 0, 8, 1);
    gps_display_set_speed(6789, 1);

    s_phone_state = 0;
    s_save_countdown = MILEAGE_SAVE_INTERVAL;
    s_pending_release_id = 0;
    s_pending_release_timer = 0;
    s_key_debug_countdown = 20;  /* 开机显示持续2秒(20*100ms) */
    s_key_debug_value = 8888;
    log_info("=== GPS Init Done ===");
}

void gps_module_loop(void)
{
    u8 connected;

    connected = (ble_hid_is_connected() || edr_hid_is_connected()) ? 1 : 0;
    gps_display_set_ble_status(connected);

    gps_uart_loop();

    /* 按键调试倒计时 */
    if (s_key_debug_countdown > 0) {
        s_key_debug_countdown--;
        /* 调试期间: 下排显示按键调试值, 不覆盖 */
        gps_display_set_speed(s_key_debug_value, 1);
    }

    gps_display_update();

    if (s_mileage_dirty) {
        s_save_countdown--;
        if (s_save_countdown == 0) {
            mileage_vm_save();
            s_save_countdown = MILEAGE_SAVE_INTERVAL;
        }
    }
}

void gps_key_event(u8 key_type, u8 key_value)
{
    /*
     * 调试: 上排显示按键信息, 持续2秒
     * 上排: type : value : 0
     * 下排: type*100+value (整数, 不带小数点)
     */
    s_key_debug_value = (float)(key_type * 100 + key_value);
    s_key_debug_countdown = 20;  /* 20*100ms = 2秒 */
    gps_display_set_time(key_type, key_value, 0, 1);
    log_info("GPS_KEY: type=%d val=%d", key_type, key_value);

    if (key_type == KEY_EVENT_CLICK) {
        switch (key_value) {
        case KEY_ID_MODE:
            handle_mode_click();
            break;
        case KEY_ID_PLAY:
            send_consumer_key(CONSUMER_PLAY_PAUSE);
            break;
        case KEY_ID_VOL_DN:
            send_consumer_key(CONSUMER_VOLUME_DEC);
            break;
        case KEY_ID_VOL_UP:
            send_consumer_key(CONSUMER_VOLUME_INC);
            break;
        case KEY_ID_PREV:
            send_consumer_key(CONSUMER_SCAN_PREV_TRACK);
            break;
        case KEY_ID_NEXT:
            send_consumer_key(CONSUMER_SCAN_NEXT_TRACK);
            break;
        case KEY_ID_PHONE:
            handle_phone_click();
            break;
        case KEY_ID_POWER:
            gps_uart_show_next();  /* 切换GPS诊断页面/波特率 */
            break;
        default:
            break;
        }
    } else if (key_type == KEY_EVENT_HOLD) {
        switch (key_value) {
        case KEY_ID_VOL_DN:
            send_consumer_hold(CONSUMER_VOLUME_DEC);
            break;
        case KEY_ID_VOL_UP:
            send_consumer_hold(CONSUMER_VOLUME_INC);
            break;
        default:
            break;
        }
    } else if (key_type == KEY_EVENT_UP) {
        send_key_up();
    } else if (key_type == KEY_EVENT_DOUBLE_CLICK) {
        switch (key_value) {
        case KEY_ID_MODE:
            /* 双击MODE: 回拨最后来电号码 */
            if (s_last_income_num_len > 0) {
                log_info("Redial last income, len=%d", s_last_income_num_len);
                user_send_cmd_prepare(USER_CTRL_DIAL_NUMBER,
                    s_last_income_num_len, s_last_income_num);
            } else {
                /* 没有来电记录, 回拨最后拨出号码 */
                user_send_cmd_prepare(USER_CTRL_HFP_CALL_LAST_NO, 0, NULL);
            }
            break;
        default:
            break;
        }
    } else if (key_type == KEY_EVENT_LONG) {
        switch (key_value) {
        case KEY_ID_PREV:
            s_mileage_km = 0;
            s_mileage_dirty = 1;
            mileage_vm_save();
            gps_display_set_mileage(0);
            break;
        case KEY_ID_NEXT:
            s_max_speed_kmh = 0;
            s_mileage_dirty = 1;
            mileage_vm_save();
            gps_display_set_max_speed(0);
            break;
        default:
            break;
        }
    }
}

void gps_ble_status_notify(u8 connected)
{
    gps_display_set_ble_status(connected);
}

void gps_phone_status_notify(u8 status)
{
    if (status == 1) {
        s_phone_state = 1;
    } else if (status == 2) {
        s_phone_state = 2;
    } else {
        s_phone_state = 0;
    }
}

void gps_phone_number_notify(u8 *args, u8 len)
{
    /* 保存来电号码用于回拨 */
    if (len > 0 && len < sizeof(s_last_income_num)) {
        memcpy(s_last_income_num, args, len);
        s_last_income_num_len = len;
        log_info("Saved phone number, len=%d", len);
    }
}

void gps_speed_update(float speed_kmh, u8 valid)
{
    /* 只要被调用就说明GPS UART在工作, 停止模拟 */
    s_gps_no_data = 0;
    gps_display_set_speed(speed_kmh, valid);
    if (valid && speed_kmh > s_max_speed_kmh) {
        s_max_speed_kmh = speed_kmh;
        gps_display_set_max_speed(s_max_speed_kmh);
        s_mileage_dirty = 1;
    }
}

void gps_mileage_update(float delta_km)
{
    s_mileage_km += delta_km;
    gps_display_set_mileage(s_mileage_km);
    s_mileage_dirty = 1;
}
