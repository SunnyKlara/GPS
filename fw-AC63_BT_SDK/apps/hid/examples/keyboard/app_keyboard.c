/*********************************************************************************************
    *   Filename        : app_keyboard.c

    *   Description     :

    *   Author          :

    *   Email           :

    *   Last modifiled  : 2019-07-05 10:09

    *   Copyright:(c)JIELI  2011-2019  @ , All Rights Reserved.
*********************************************************************************************/
#include "system/app_core.h"
#include "system/includes.h"
#include "server/server_core.h"
#include "app_config.h"
#include "app_action.h"
#include "os/os_api.h"
#include "btcontroller_config.h"
#include "btctrler/btctrler_task.h"
#include "config/config_transport.h"
#include "btstack/avctp_user.h"
#include "btstack/btstack_task.h"
#include "bt_common.h"
#include "edr_hid_user.h"
/* #include "code_switch.h" */
/* #include "omsensor/OMSensor_manage.h" */
#include "le_common.h"
#include <stdlib.h>
#include "standard_hid.h"
#include "rcsp_bluetooth.h"
#include "app_charge.h"
#include "app_power_manage.h"
#include "app_chargestore.h"
#include "app_comm_bt.h"

#if(CONFIG_APP_KEYBOARD)
#define LOG_TAG_CONST       HID_KEY
#define LOG_TAG             "[HID_KEY]"
#define LOG_ERROR_ENABLE
#define LOG_DEBUG_ENABLE
#define LOG_INFO_ENABLE
/* #define LOG_DUMP_ENABLE */
#define LOG_CLI_ENABLE
#include "debug.h"

/*============================================================================
 * GPS测速仪集成
 * 正式驱动在 gps/ 子目录, 这里只保留SDK集成入口
 *============================================================================*/
#include "gps/gps_main.h"
#include "gps/app_display.h"

/*
 * ADC按键标定模式开关
 * 设为1: 按键只打印ADC原始值到串口, 不执行任何功能
 * 设为0: 正常按键功能
 */
#define ADC_KEY_CALIBRATION_MODE    0
#define COLON_MAP_TEST_MODE         0   /* 测试完毕,确认是硬件焊接问题 */

#if ADC_KEY_CALIBRATION_MODE
/*
 * ADC标定: 在数码管上显示按键的ADC原始值
 * 上排显示 "KEY X" (X=按键编号), 下排显示ADC值
 */
extern u32 adc_get_value(u32 ch);
extern u32 adc_add_sample_ch(u32 ch);

static void adc_calibration_display(u8 key_value, u32 adc_val)
{
    u8 buf[14];
    int i;
    int grid;
    u8 segments;

    /* 已确认的段码表 */
    static const u8 seg_tbl[10] = {
        0x77, 0x24, 0x5D, 0x6D, 0x2E,
        0x6B, 0x7B, 0x25, 0x7F, 0x6F,
    };

    for (i = 0; i < 14; i++) buf[i] = 0;

    /* 上排: 显示按键编号 (SEG5=秒位 显示key_value) */
    if (key_value <= 9) {
        segments = seg_tbl[key_value];
        for (grid = 0; grid < 7; grid++) {
            if (segments & (1 << grid)) {
                buf[grid * 2] |= (1 << 4);  /* SEG5 = bit4 */
            }
        }
    }

    /* 下排: 显示ADC值 (4位, SEG6~SEG9) */
    {
        u8 d3, d2, d1, d0;
        d3 = (adc_val / 1000) % 10;
        d2 = (adc_val / 100) % 10;
        d1 = (adc_val / 10) % 10;
        d0 = adc_val % 10;

        /* SEG6=bit5, SEG7=bit6, SEG8=bit7, SEG9=高字节bit0 */
        segments = seg_tbl[d3];
        for (grid = 0; grid < 7; grid++) {
            if (segments & (1 << grid)) buf[grid * 2] |= (1 << 5);
        }
        segments = seg_tbl[d2];
        for (grid = 0; grid < 7; grid++) {
            if (segments & (1 << grid)) buf[grid * 2] |= (1 << 6);
        }
        segments = seg_tbl[d1];
        for (grid = 0; grid < 7; grid++) {
            if (segments & (1 << grid)) buf[grid * 2] |= (1 << 7);
        }
        segments = seg_tbl[d0];
        for (grid = 0; grid < 7; grid++) {
            if (segments & (1 << grid)) buf[grid * 2 + 1] |= 0x01;
        }
    }

    /* 写入TM1638 */
    {
        /* 内联写14字节 */
        gpio_direction_output(IO_PORTA_00, 1);
        gpio_direction_output(IO_PORTA_01, 1);
        gpio_direction_output(IO_PORTA_02, 1);

        /* 自动递增 */
        gpio_set_output_value(IO_PORTA_01, 0);
        for (i = 0; i < 8; i++) {
            gpio_set_output_value(IO_PORTA_02, 0);
            gpio_set_output_value(IO_PORTA_00, (0x40 >> i) & 1);
            { volatile int _d = 10; while(_d--); }
            gpio_set_output_value(IO_PORTA_02, 1);
            { volatile int _d = 10; while(_d--); }
        }
        gpio_set_output_value(IO_PORTA_01, 1);

        /* 地址+数据 */
        gpio_set_output_value(IO_PORTA_01, 0);
        for (i = 0; i < 8; i++) {
            gpio_set_output_value(IO_PORTA_02, 0);
            gpio_set_output_value(IO_PORTA_00, (0xC0 >> i) & 1);
            { volatile int _d = 10; while(_d--); }
            gpio_set_output_value(IO_PORTA_02, 1);
            { volatile int _d = 10; while(_d--); }
        }
        for (i = 0; i < 14; i++) {
            int b;
            for (b = 0; b < 8; b++) {
                gpio_set_output_value(IO_PORTA_02, 0);
                gpio_set_output_value(IO_PORTA_00, (buf[i] >> b) & 1);
                { volatile int _d = 10; while(_d--); }
                gpio_set_output_value(IO_PORTA_02, 1);
                { volatile int _d = 10; while(_d--); }
            }
        }
        gpio_set_output_value(IO_PORTA_01, 1);

        /* 显示ON最大亮度 */
        gpio_set_output_value(IO_PORTA_01, 0);
        for (i = 0; i < 8; i++) {
            gpio_set_output_value(IO_PORTA_02, 0);
            gpio_set_output_value(IO_PORTA_00, (0x8F >> i) & 1);
            { volatile int _d = 10; while(_d--); }
            gpio_set_output_value(IO_PORTA_02, 1);
            { volatile int _d = 10; while(_d--); }
        }
        gpio_set_output_value(IO_PORTA_01, 1);
    }
}

/* ADC标定: 定时读取ADC原始值并直接显示到数码管 */
static u8 adc_cal_last_key = 0xFF;
static u32 adc_cal_last_val = 0;

static void adc_calibration_timer_cb(void *param)
{
    u32 adc_val;
    adc_val = adc_get_value(AD_CH_PA8);

    /* 每次都刷新数码管显示 (按住按键时实时看到ADC值) */
    adc_calibration_display(0xFF, adc_val);

    /* 值变化时打印到串口 */
    if (adc_val != adc_cal_last_val) {
        adc_cal_last_val = adc_val;
        log_info("ADC_RAW: %d (0x%03x)", adc_val, adc_val);
    }
}
#endif /* ADC_KEY_CALIBRATION_MODE */

/*==========================================================
 * 冒号/小数点 GRID映射测试
 * SEG10 = 高字节bit1, 共7个GRID
 * 每次只亮1个GRID的SEG10, 按任意键切换到下一个
 * 同时SEG1~SEG9显示1~9作为位置参考
 *==========================================================*/
#if COLON_MAP_TEST_MODE

static u8 colon_test_grid = 0;  /* 当前测试的GRID (0~6) */

static const u8 ct_seg_tbl[10] = {
    0x77, 0x24, 0x5D, 0x6D, 0x2E,
    0x6B, 0x7B, 0x25, 0x7F, 0x6F,
};

static void ct_shift_out(u8 byte)
{
    int i;
    for (i = 0; i < 8; i++) {
        gpio_set_output_value(IO_PORTA_02, 0);
        gpio_set_output_value(IO_PORTA_00, (byte >> i) & 1);
        { volatile int _d = 10; while(_d--); }
        gpio_set_output_value(IO_PORTA_02, 1);
        { volatile int _d = 10; while(_d--); }
    }
}

static void ct_send_cmd(u8 cmd)
{
    gpio_set_output_value(IO_PORTA_01, 0);
    ct_shift_out(cmd);
    gpio_set_output_value(IO_PORTA_01, 1);
}

static void ct_write_14(const u8 *data)
{
    int i;
    ct_send_cmd(0x40);
    gpio_set_output_value(IO_PORTA_01, 0);
    ct_shift_out(0xC0);
    for (i = 0; i < 14; i++) {
        ct_shift_out(data[i]);
    }
    gpio_set_output_value(IO_PORTA_01, 1);
    ct_send_cmd(0x8F);
}

static void colon_test_display(void)
{
    u8 buf[14];
    int i;
    int grid;
    u8 segments;

    for (i = 0; i < 14; i++) buf[i] = 0;

    /* SEG1~SEG8 显示 1~8 */
    for (i = 0; i < 8; i++) {
        segments = ct_seg_tbl[i + 1];
        for (grid = 0; grid < 7; grid++) {
            if (segments & (1 << grid)) {
                buf[grid * 2] |= (1 << i);
            }
        }
    }

    /* SEG9 显示 9 */
    segments = ct_seg_tbl[9];
    for (grid = 0; grid < 7; grid++) {
        if (segments & (1 << grid)) {
            buf[grid * 2 + 1] |= 0x01;
        }
    }

    /* SEG10: 只亮 colon_test_grid 指定的那个GRID */
    buf[colon_test_grid * 2 + 1] |= 0x02;

    ct_write_14(buf);
    log_info("COLON TEST: GRID%d SEG10 ON", colon_test_grid + 1);
}

static void colon_test_init(void)
{
    gpio_direction_output(IO_PORTA_00, 1);
    gpio_direction_output(IO_PORTA_01, 1);
    gpio_direction_output(IO_PORTA_02, 1);
    colon_test_grid = 0;
    colon_test_display();
}

static void colon_test_next(void)
{
    colon_test_grid++;
    if (colon_test_grid >= 7) {
        colon_test_grid = 0;
    }
    colon_test_display();
}

#endif /* COLON_MAP_TEST_MODE */

#if TCFG_AUDIO_ENABLE
#include "tone_player.h"
#include "media/includes.h"
#include "key_event_deal.h"
extern void midi_paly_test(u32 key);
#endif/*TCFG_AUDIO_ENABLE*/

//测试每个interval上行发一包数据
#define  HID_TEST_KEEP_SEND_EN        0//disabled for TM1638 segment test

#define trace_run_debug_val(x)   //log_info("\n## %s: %d,  0x%04x ##\n",__FUNCTION__,__LINE__,x)

//---------------------------------------------------------------------
#if SNIFF_MODE_RESET_ANCHOR
#define SNIFF_MODE_TYPE               SNIFF_MODE_ANCHOR
#define SNIFF_CNT_TIME                1/////<空闲?S之后进入sniff模式

#define SNIFF_MAX_INTERVALSLOT        16
#define SNIFF_MIN_INTERVALSLOT        16
#define SNIFF_ATTEMPT_SLOT            2
#define SNIFF_TIMEOUT_SLOT            1
#define SNIFF_CHECK_TIMER_PERIOD      200
#else

#define SNIFF_MODE_TYPE               SNIFF_MODE_DEF
#define SNIFF_CNT_TIME                5/////<空闲?S之后进入sniff模式

#define SNIFF_MAX_INTERVALSLOT        800
#define SNIFF_MIN_INTERVALSLOT        100
#define SNIFF_ATTEMPT_SLOT            4
#define SNIFF_TIMEOUT_SLOT            1
#define SNIFF_CHECK_TIMER_PERIOD      1000
#endif

//默认配置
static const edr_sniff_par_t hidkey_sniff_param = {
    .sniff_mode = SNIFF_MODE_TYPE,
    .cnt_time = SNIFF_CNT_TIME,
    .max_interval_slots = SNIFF_MAX_INTERVALSLOT,
    .min_interval_slots = SNIFF_MIN_INTERVALSLOT,
    .attempt_slots = SNIFF_ATTEMPT_SLOT,
    .timeout_slots = SNIFF_TIMEOUT_SLOT,
    .check_timer_period = SNIFF_CHECK_TIMER_PERIOD,
};

typedef enum {
    HID_MODE_NULL = 0,
    HID_MODE_EDR,
    HID_MODE_BLE,
    HID_MODE_INIT = 0xff
} bt_mode_e;


static bt_mode_e bt_hid_mode;
static volatile u8 is_hidkey_active = 0;//1-临界点,系统不允许进入低功耗，0-系统可以进入低功耗
static u16 g_auto_shutdown_timer = 0;
static void hidkey_app_select_btmode(u8 mode);
void hidkey_power_event_to_user(u8 event);

/* GPS模块需要访问当前蓝牙模式 */
u8 gps_get_bt_hid_mode(void)
{
    return (u8)bt_hid_mode;
}
//----------------------------------
static const u8 hidkey_report_map[] = {
    /* === Consumer Control (Report ID 1) === */
    0x05, 0x0C,        // Usage Page (Consumer)
    0x09, 0x01,        // Usage (Consumer Control)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1)
    0x09, 0xE9,        //   Usage (Volume Increment)
    0x09, 0xEA,        //   Usage (Volume Decrement)
    0x09, 0xCD,        //   Usage (Play/Pause)
    0x09, 0xE2,        //   Usage (Mute)
    0x09, 0xB6,        //   Usage (Scan Previous Track)
    0x09, 0xB5,        //   Usage (Scan Next Track)
    0x09, 0xB3,        //   Usage (Fast Forward)
    0x09, 0xB4,        //   Usage (Rewind)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x10,        //   Report Count (16)
    0x81, 0x02,        //   Input (Data,Var,Abs)
    0xC0,              // End Collection
    /*
     * BLE HID限制: Telephony Hook Switch在大多数Android手机上不工作
     * 挂断电话需要HFP协议, 纯BLE HID无法实现
     * MODE键功能: 接听来电(OK) / 暂停音乐(OK) / 挂断电话(不支持)
     */
    /* 注意: 去掉了Telephony Report, 电话接听/挂断统一用Play/Pause */
    /* 这和SDK keyfob例程的做法一致, Android/iOS兼容性最好 */
};

// consumer key
#define CONSUMER_VOLUME_INC             0x0001
#define CONSUMER_VOLUME_DEC             0x0002
#define CONSUMER_PLAY_PAUSE             0x0004
#define CONSUMER_MUTE                   0x0008
#define CONSUMER_SCAN_PREV_TRACK        0x0010
#define CONSUMER_SCAN_NEXT_TRACK        0x0020
#define CONSUMER_SCAN_FRAME_FORWARD     0x0040
#define CONSUMER_SCAN_FRAME_BACK        0x0080

//----------------------------------
static const u16 hid_key_click_table[8] = {
    CONSUMER_PLAY_PAUSE,
    CONSUMER_SCAN_PREV_TRACK,
    CONSUMER_VOLUME_DEC,
    CONSUMER_SCAN_NEXT_TRACK,
    CONSUMER_VOLUME_INC,
    CONSUMER_MUTE,
    0,
    0,
};

static const u16 hid_key_hold_table[8] = {
    0,
    CONSUMER_SCAN_FRAME_BACK,
    CONSUMER_VOLUME_DEC,
    CONSUMER_SCAN_FRAME_FORWARD,
    CONSUMER_VOLUME_INC,
    0,
    0,
    0,
};

//----------------------------------
static const edr_init_cfg_t hidkey_edr_config = {
    .page_timeout = 8000,
    .super_timeout = 8000,
    .io_capabilities = 3,
    .passkey_enable = 0,
    .authentication_req = 2,
    .oob_data = 0,
    .sniff_param = &hidkey_sniff_param,
    .class_type = BD_CLASS_KEYBOARD,
    .report_map = hidkey_report_map,
    .report_map_size = sizeof(hidkey_report_map),
};

//----------------------------------
static const ble_init_cfg_t hidkey_ble_config = {
    .same_address = 0,
    .appearance = BLE_APPEARANCE_HID_KEYBOARD,
    .report_map = hidkey_report_map,
    .report_map_size = sizeof(hidkey_report_map),
};

extern void p33_soft_reset(void);
extern void ble_hid_key_deal_test(u16 key_msg);
extern void ble_module_enable(u8 en);
static void hidkey_set_soft_poweroff(void);

/*************************************************************************************************/
/*!
 *  \brief      删除auto关机
 *
 *  \param      [in]
 *
 *  \return
 *
 *  \note
 */
/*************************************************************************************************/
static void hidkey_auto_shutdown_disable(void)
{
    log_info("----%s", __FUNCTION__);
    if (g_auto_shutdown_timer) {
        sys_timeout_del(g_auto_shutdown_timer);
    }
}

/*************************************************************************************************/
/*!
 *  \brief      测试一直发空键
 *
 *  \param      [in]
 *
 *  \return
 *
 *  \note
 */
/*************************************************************************************************/
#if HID_TEST_KEEP_SEND_EN
static u8 test_keep_send_start = 0;
extern int ble_hid_timer_handle;
extern int edr_hid_timer_handle;
extern int ble_hid_data_send(u8 report_id, u8 *data, u16 len);
void hidkey_test_keep_send_data(void)
{
    static const u8 test_data_000[8] = {0, 0, 0, 0};
    void (*hid_data_send_pt)(u8 report_id, u8 * data, u16 len) = NULL;

    if (!test_keep_send_start) {
        return;
    }

    if (bt_hid_mode == HID_MODE_EDR) {
#if TCFG_USER_EDR_ENABLE
        hid_data_send_pt = edr_hid_data_send;
        bt_comm_edr_sniff_clean();
#endif
    } else {
#if TCFG_USER_BLE_ENABLE
        hid_data_send_pt = ble_hid_data_send;
#endif
    }
    hid_data_send_pt(1, test_data_000, sizeof(test_data_000));
}

/*************************************************************************************************/
/*!
 *  \brief      初始化测试发送
 *
 *  \param      [in]
 *
 *  \return
 *
 *  \note
 */
/*************************************************************************************************/
void hidkey_test_keep_send_init(void)
{
    if (bt_hid_mode == HID_MODE_BLE) {
#if TCFG_USER_BLE_ENABLE
        log_info("###keep test ble\n");
        ble_hid_timer_handle = sys_s_hi_timer_add((void *)0, hidkey_test_keep_send_data, 10);
#endif
    } else {
#if TCFG_USER_EDR_ENABLE
        log_info("###keep test edr\n");
        edr_hid_timer_handle = sys_s_hi_timer_add((void *)0, hidkey_test_keep_send_data, 10);
#endif
    }
}
#endif


/*************************************************************************************************/
/*!
 *  \brief      按键处理
 *
 *  \param      [in]
 *
 *  \return
 *
 *  \note
 */
/*************************************************************************************************/
static void hidkey_app_key_deal_test(u8 key_type, u8 key_value)
{
    u16 key_msg = 0;
    u16 key_msg_up = 0;

#if ADC_KEY_CALIBRATION_MODE
    /*=== ADC标定模式: 只打印ADC值和按键编号 ===*/
    {
        u32 adc_val;
        adc_val = adc_get_value(AD_CH_PA8);
        log_info("KEY event: type=%d val=%d ADC_RAW=%d (0x%03x)",
                 key_type, key_value, adc_val, adc_val);

        if (key_type == KEY_EVENT_CLICK || key_type == KEY_EVENT_LONG) {
            adc_cal_last_key = key_value;
            adc_calibration_display(key_value, adc_val);
        }
    }
    return;
#elif COLON_MAP_TEST_MODE == 2
    /* 全8测试: 不需要按键处理 */
    return;
#elif COLON_MAP_TEST_MODE == 1
    /*=== 冒号映射测试: 任意键单击切换到下一个GRID ===*/
    if (key_type == KEY_EVENT_CLICK) {
        colon_test_next();
    }
    return;
#else
    /*=== 正常模式: 转发给GPS模块处理 ===*/
    gps_key_event(key_type, key_value);

    /* 保留SDK原有的三击关机 */
    if (key_type == KEY_EVENT_TRIPLE_CLICK
        && (key_value == TCFG_ADKEY_VALUE0)) {
        hidkey_power_event_to_user(POWER_EVENT_POWER_SOFTOFF);
        return;
    }

    /* 保留SDK原有的双击切换BLE/EDR模式 (用POWER键) */
    if (key_type == KEY_EVENT_DOUBLE_CLICK && key_value == TCFG_ADKEY_VALUE0) {
#if (TCFG_USER_EDR_ENABLE && TCFG_USER_BLE_ENABLE)
        is_hidkey_active = 1;
        if (HID_MODE_BLE == bt_hid_mode) {
            hidkey_app_select_btmode(HID_MODE_EDR);
        } else {
            hidkey_app_select_btmode(HID_MODE_BLE);
        }
        os_time_dly(WAIT_DISCONN_TIME_MS / 10);
        p33_soft_reset();
        while (1);
#endif
    }
#endif /* ADC_KEY_CALIBRATION_MODE */
}

/*************************************************************************************************/
/*!
 *  \brief      绑定信息 VM读写操作
 *
 *  \param      [in]rw_flag: 0-read vm,1--write
 *
 *  \return
 *
 *  \note
 */
/*************************************************************************************************/
typedef struct {
    u16 head_tag;
    u8  mode;
} hid_vm_cfg_t;

#define	HID_VM_HEAD_TAG (0x3AA3)
static void hidkey_vm_deal(u8 rw_flag)
{
    hid_vm_cfg_t info;
    int ret;
    int vm_len = sizeof(hid_vm_cfg_t);

    log_info("-hid_info vm_do:%d\n", rw_flag);
    memset(&info, 0, vm_len);

    if (rw_flag == 0) {
        bt_hid_mode = HID_MODE_NULL;
        ret = syscfg_read(CFG_AAP_MODE_INFO, (u8 *)&info, vm_len);
        if (!ret) {
            log_info("-null--\n");
        } else {
            if (HID_VM_HEAD_TAG == info.head_tag) {
                log_info("-exist--\n");
                log_info_hexdump((u8 *)&info, vm_len);
                bt_hid_mode = info.mode;
            }
        }

        if (HID_MODE_NULL == bt_hid_mode) {
#if TCFG_USER_EDR_ENABLE
            bt_hid_mode = HID_MODE_EDR;  /* 默认EDR模式, 支持HFP电话控制 */
#else
            bt_hid_mode = HID_MODE_BLE;
#endif
        } else {
            /* 强制使用EDR模式 (HFP电话控制需要EDR) */
#if TCFG_USER_EDR_ENABLE
            bt_hid_mode = HID_MODE_EDR;
#endif

            if (bt_hid_mode != info.mode) {
                log_info("-write00--\n");
                info.mode = bt_hid_mode;
                syscfg_write(CFG_AAP_MODE_INFO, (u8 *)&info, vm_len);
            }
        }
    } else {
        info.mode = bt_hid_mode;
        info.head_tag = HID_VM_HEAD_TAG;
        syscfg_write(CFG_AAP_MODE_INFO, (u8 *)&info, vm_len);
        log_info("-write11--\n");
        log_info_hexdump((u8 *)&info, vm_len);
    }
}

/*************************************************************************************************/
/*!
 *  \brief      软关机消息处理
 *
 *  \param      [in]
 *
 *  \return
 *
 *  \note
 */
/*************************************************************************************************/
void hidkey_power_event_to_user(u8 event)
{
    struct sys_event e;
    e.type = SYS_DEVICE_EVENT;
    e.arg  = (void *)DEVICE_EVENT_FROM_POWER;
    e.u.dev.event = event;
    e.u.dev.value = 0;
    sys_event_notify(&e);
}

/*************************************************************************************************/
/*!
 *  \brief      进入软关机
 *
 *  \param      [in]
 *
 *  \return
 *
 *  \note
 */
/*************************************************************************************************/
static void hidkey_set_soft_poweroff(void)
{
    log_info("hidkey_set_soft_poweroff\n");
    is_hidkey_active = 1;

#if TCFG_USER_BLE_ENABLE
    btstack_ble_exit(0);
#endif

#if TCFG_USER_EDR_ENABLE
    btstack_edr_exit(0);
#endif

#if (TCFG_USER_EDR_ENABLE || TCFG_USER_BLE_ENABLE)
    //延时300ms，确保BT退出链路断开
    sys_timeout_add(NULL, power_set_soft_poweroff, WAIT_DISCONN_TIME_MS);
#else
    power_set_soft_poweroff();
#endif
}

static void hidkey_timer_handle_test(void)
{
    log_info("not_bt");
    //mem_stats();//see memory
}

/*************************************************************************************************/
/*!
 *  \brief      app 入口
 *
 *  \param      [in]
 *
 *  \return
 *
 *  \note
 */
/*************************************************************************************************/
extern void bt_pll_para(u32 osc, u32 sys, u8 low_power, u8 xosc);
static void hidkey_app_start()
{
    log_info("=======================================");
    log_info("-------------HID DEMO-----------------");
    log_info("=======================================");
    log_info("app_file: %s", __FILE__);

    clk_set("sys", BT_NORMAL_HZ);

    //有蓝牙
#if (TCFG_USER_EDR_ENABLE || TCFG_USER_BLE_ENABLE)
    u32 sys_clk =  clk_get("sys");
    bt_pll_para(TCFG_CLOCK_OSC_HZ, sys_clk, 0, 0);

#if TCFG_USER_EDR_ENABLE
    btstack_edr_start_before_init(&hidkey_edr_config, 0);
#endif

#if TCFG_USER_BLE_ENABLE
    btstack_ble_start_before_init(&hidkey_ble_config, 0);
    /* le_hogp_set_reconnect_adv_cfg(ADV_IND, 5000); */
    le_hogp_set_reconnect_adv_cfg(ADV_DIRECT_IND_LOW, 5000);
#endif

    btstack_init();

#else
    //no bt,to for test
    sys_timer_add(NULL, hidkey_timer_handle_test, 1000);
#endif

    /* 按键消息使能 */
    sys_key_event_enable();
#if TCFG_SOFTOFF_WAKEUP_KEY_DRIVER_ENABLE
    set_key_wakeup_send_flag(1);
#endif

#if (TCFG_HID_AUTO_SHUTDOWN_TIME)
    //无操作定时软关机
    g_auto_shutdown_timer = sys_timeout_add((void *)POWER_EVENT_POWER_SOFTOFF, hidkey_power_event_to_user, TCFG_HID_AUTO_SHUTDOWN_TIME * 1000);
#endif

    /*=== GPS测速仪模块初始化 ===*/
#if ADC_KEY_CALIBRATION_MODE
    log_info("=== ADC KEY CALIBRATION MODE ===");
    log_info("=== Press each key and note the ADC value ===");
    /* 初始化TM1638显示 (用于显示ADC值) */
    gpio_direction_output(IO_PORTA_00, 1);
    gpio_direction_output(IO_PORTA_01, 1);
    gpio_direction_output(IO_PORTA_02, 1);
    /* 定时读取ADC原始值 */
    adc_add_sample_ch(AD_CH_PA8);
    sys_timer_add(NULL, adc_calibration_timer_cb, 100);  /* 100ms刷新ADC值 */
    /* 初始显示: 全0 */
    adc_calibration_display(0xFF, 0);
#elif COLON_MAP_TEST_MODE == 2
    /* 全8测试: 9个数码管全部显示8, 检查每个段是否都亮 */
    log_info("=== ALL-8 TEST: every segment should be ON ===");
    {
        u8 buf8[14];
        int i8;
        int grid8;
        /* 数字8的段码 = 0x7F (全段亮) */
        for (i8 = 0; i8 < 14; i8++) buf8[i8] = 0;
        /* SEG1~SEG8 全显示8 */
        for (i8 = 0; i8 < 8; i8++) {
            for (grid8 = 0; grid8 < 7; grid8++) {
                buf8[grid8 * 2] |= (1 << i8);
            }
        }
        /* SEG9 也显示8 */
        for (grid8 = 0; grid8 < 7; grid8++) {
            buf8[grid8 * 2 + 1] |= 0x01;
        }
        /* 冒号和小数点全亮 */
        for (grid8 = 0; grid8 < 7; grid8++) {
            buf8[grid8 * 2 + 1] |= 0x02;
        }
        /* 初始化GPIO */
        gpio_direction_output(IO_PORTA_00, 1);
        gpio_direction_output(IO_PORTA_01, 1);
        gpio_direction_output(IO_PORTA_02, 1);
        ct_send_cmd(0x40);
        gpio_set_output_value(IO_PORTA_01, 0);
        ct_shift_out(0xC0);
        for (i8 = 0; i8 < 14; i8++) ct_shift_out(buf8[i8]);
        gpio_set_output_value(IO_PORTA_01, 1);
        ct_send_cmd(0x8F);
    }
#elif COLON_MAP_TEST_MODE == 1
#else
    log_info("=== GPS Speedometer Start ===");
    gps_module_init();
    sys_timer_add(NULL, gps_module_loop, 100);  /* 100ms定时驱动 */
#endif
}


/*************************************************************************************************/
/*!
 *  \brief      app  状态处理
 *
 *  \param      [in]
 *
 *  \return
 *
 *  \note
 */
/*************************************************************************************************/
static int hidkey_state_machine(struct application *app, enum app_state state, struct intent *it)
{
    switch (state) {
    case APP_STA_CREATE:
        break;
    case APP_STA_START:
        if (!it) {
            break;
        }
        switch (it->action) {
        case ACTION_HID_MAIN:
            hidkey_app_start();
            break;
        }
        break;
    case APP_STA_PAUSE:
        break;
    case APP_STA_RESUME:
        break;
    case APP_STA_STOP:
        break;
    case APP_STA_DESTROY:
        log_info("APP_STA_DESTROY\n");
        break;
    }

    return 0;
}

/*************************************************************************************************/
/*!
 *  \brief      蓝牙HCI事件消息处理
 *
 *  \param      [in]
 *
 *  \return
 *
 *  \note
 */
/*************************************************************************************************/
static int hidkey_bt_hci_event_handler(struct bt_event *bt)
{
    //对应原来的蓝牙连接上断开处理函数  ,bt->value=reason
    log_info("----%s reason %x %x", __FUNCTION__, bt->event, bt->value);

#if TCFG_USER_EDR_ENABLE
    bt_comm_edr_hci_event_handler(bt);
#endif

#if TCFG_USER_BLE_ENABLE
    bt_comm_ble_hci_event_handler(bt);
#endif

    return 0;
}

/*************************************************************************************************/
/*!
 *  \brief      蓝牙连接状态事件消息处理
 *
 *  \param      [in]
 *
 *  \return
 *
 *  \note
 */
/*************************************************************************************************/
static int hidkey_bt_connction_status_event_handler(struct bt_event *bt)
{
    log_info("----%s %d", __FUNCTION__, bt->event);


    switch (bt->event) {
    case BT_STATUS_INIT_OK:
        /*
         * 蓝牙初始化完成
         */
        log_info("BT_STATUS_INIT_OK\n");

#if TCFG_NORMAL_SET_DUT_MODE
#if TCFG_USER_EDR_ENABLE
        log_info("set edr dut mode\n");
        bredr_set_dut_enble(1, 1);
#else
        log_info("set ble dut mode\n");
        ble_standard_dut_test_init();
#endif
        break;
#endif

        hidkey_vm_deal(0);//bt_hid_mode read for VM

        //根据模式执行对应蓝牙的初始化
        if (bt_hid_mode == HID_MODE_BLE) {
#if TCFG_USER_BLE_ENABLE
            btstack_ble_start_after_init(0);
#endif
        } else {
#if TCFG_USER_EDR_ENABLE
            btstack_edr_start_after_init(0);
#endif
        }

        hidkey_app_select_btmode(HID_MODE_INIT);//

#if HID_TEST_KEEP_SEND_EN
        hidkey_test_keep_send_init();
#endif
        break;

    default:
#if TCFG_USER_EDR_ENABLE
        bt_comm_edr_status_event_handler(bt);
#endif

#if TCFG_USER_BLE_ENABLE
        bt_comm_ble_status_event_handler(bt);
#endif

        /* GPS模块: BLE连接状态通知 */
#if !ADC_KEY_CALIBRATION_MODE
        if (bt->event == BT_STATUS_FIRST_CONNECTED) {
            gps_ble_status_notify(1);
        } else if (bt->event == BT_STATUS_FIRST_DISCONNECT) {
            gps_ble_status_notify(0);
        }
        /* GPS模块: 电话状态通知 */
        if (bt->event == BT_STATUS_PHONE_INCOME) {
            gps_phone_status_notify(1);  /* 来电 */
        } else if (bt->event == BT_STATUS_PHONE_OUT) {
            gps_phone_status_notify(2);  /* 拨出(回拨), 视为通话中 */
        } else if (bt->event == BT_STATUS_PHONE_ACTIVE) {
            gps_phone_status_notify(2);  /* 接通 */
        } else if (bt->event == BT_STATUS_PHONE_HANGUP) {
            gps_phone_status_notify(0);  /* 挂断 */
        } else if (bt->event == BT_STATUS_PHONE_NUMBER) {
            /* 来电号码: value可能是指向号码字符串的指针 */
            {
                u8 *num_ptr;
                u8 num_len;
                num_ptr = (u8 *)bt->value;
                if (num_ptr) {
                    /* 计算号码长度(找到非数字字符或最大20) */
                    num_len = 0;
                    while (num_len < 20 && num_ptr[num_len] >= '0' && num_ptr[num_len] <= '9') {
                        num_len++;
                    }
                    /* 也接受+号开头的国际号码 */
                    if (num_len == 0 && num_ptr[0] == '+') {
                        num_len = 1;
                        while (num_len < 20 && num_ptr[num_len] >= '0' && num_ptr[num_len] <= '9') {
                            num_len++;
                        }
                    }
                    gps_phone_number_notify(num_ptr, num_len);
                }
            }
        }
#endif
        break;
    }
    return 0;
}

/*************************************************************************************************/
/*!
 *  \brief      蓝牙公共消息处理
 *
 *  \param      [in]
 *
 *  \return
 *
 *  \note
 */
/*************************************************************************************************/
static int hidkey_bt_common_event_handler(struct bt_event *bt)
{
    log_info("----%s reason %x %x", __FUNCTION__, bt->event, bt->value);

    switch (bt->event) {
    case COMMON_EVENT_EDR_REMOTE_TYPE:
        log_info(" COMMON_EVENT_EDR_REMOTE_TYPE,%d \n", bt->value);
        break;

    case COMMON_EVENT_BLE_REMOTE_TYPE:
        log_info(" COMMON_EVENT_BLE_REMOTE_TYPE,%d \n", bt->value);
        break;

    case COMMON_EVENT_SHUTDOWN_DISABLE:
        hidkey_auto_shutdown_disable();
        break;

    default:
        break;

    }
    return 0;
}

/*************************************************************************************************/
/*!
 *  \brief      按键事件处理
 *
 *  \param      [in]
 *
 *  \return
 *
 *  \note
 */
/*************************************************************************************************/
static void hidkey_key_event_handler(struct sys_event *event)
{
    /* u16 cpi = 0; */
    u8 event_type = 0;
    u8 key_value = 0;

    if (event->arg == (void *)DEVICE_EVENT_FROM_KEY) {
        event_type = event->u.key.event;
        key_value = event->u.key.value;
        log_info("app_key_evnet: %d,%d\n", event_type, key_value);
        hidkey_app_key_deal_test(event_type, key_value);
    }
}

/*************************************************************************************************/
/*!
 *  \brief      app 线程事件处理
 *
 *  \param      [in]
 *
 *  \return
 *
 *  \note
 */
/*************************************************************************************************/
static int hidkey_event_handler(struct application *app, struct sys_event *event)
{
#if (TCFG_HID_AUTO_SHUTDOWN_TIME)
    //重置无操作定时计数
    if (event->type != SYS_DEVICE_EVENT || DEVICE_EVENT_FROM_POWER != event->arg) { //过滤电源消息
        sys_timer_modify(g_auto_shutdown_timer, TCFG_HID_AUTO_SHUTDOWN_TIME * 1000);
    }
#endif

#if TCFG_USER_EDR_ENABLE
    bt_comm_edr_sniff_clean();
#endif

    switch (event->type) {
    case SYS_KEY_EVENT:
        hidkey_key_event_handler(event);
        return 0;

    case SYS_BT_EVENT:
#if (TCFG_USER_EDR_ENABLE || TCFG_USER_BLE_ENABLE)
        if ((u32)event->arg == SYS_BT_EVENT_TYPE_CON_STATUS) {
            hidkey_bt_connction_status_event_handler(&event->u.bt);
        } else if ((u32)event->arg == SYS_BT_EVENT_TYPE_HCI_STATUS) {
            hidkey_bt_hci_event_handler(&event->u.bt);
        } else if ((u32)event->arg == SYS_BT_EVENT_FORM_COMMON) {
            return hidkey_bt_common_event_handler(&event->u.dev);
        }
#endif
        return 0;

    case SYS_DEVICE_EVENT:
        if ((u32)event->arg == DEVICE_EVENT_FROM_POWER) {
            return app_power_event_handler(&event->u.dev, hidkey_set_soft_poweroff);
        }
#if TCFG_CHARGE_ENABLE
        else if ((u32)event->arg == DEVICE_EVENT_FROM_CHARGE) {
            app_charge_event_handler(&event->u.dev);
        }
#endif
        return 0;

    default:
        return 0;
    }

    return 0;
}

/*************************************************************************************************/
/*!
 *  \brief      切换蓝牙模式
 *
 *  \param      [in]
 *
 *  \return
 *
 *  \note      切换模式,重启
 */
/*************************************************************************************************/
static void hidkey_app_select_btmode(u8 mode)
{
    if (mode != HID_MODE_INIT) {
        if (bt_hid_mode == mode) {
            return;
        }
        bt_hid_mode = mode;
    } else {
        //init start
    }

    log_info("###### %s: %d,%d\n", __FUNCTION__, mode, bt_hid_mode);

    if (bt_hid_mode == HID_MODE_BLE) {
        //ble
        log_info("---------app select ble--------\n");
        if (!STACK_MODULES_IS_SUPPORT(BT_BTSTACK_LE) || !BT_MODULES_IS_SUPPORT(BT_MODULE_LE)) {
            log_info("not surpport ble,make sure config !!!\n");
            ASSERT(0);
        }

#if TCFG_USER_EDR_ENABLE
        //close edr
        bt_comm_edr_mode_enable(0);
#endif

#if TCFG_USER_BLE_ENABLE
        if (mode == HID_MODE_INIT) {
            ble_module_enable(1);
        }
#endif

    } else {
        //edr
        log_info("---------app select edr--------\n");
        if (!STACK_MODULES_IS_SUPPORT(BT_BTSTACK_CLASSIC) || !BT_MODULES_IS_SUPPORT(BT_MODULE_CLASSIC)) {
            log_info("not surpport edr,make sure config !!!\n");
            ASSERT(0);
        }

#if TCFG_USER_BLE_ENABLE
        //close ble
        ble_module_enable(0);
#endif

#if TCFG_USER_EDR_ENABLE
        if (mode == HID_MODE_INIT) {
            if (!bt_connect_phone_back_start()) {
                bt_wait_phone_connect_control(1);
            }
        }
#endif

    }

    hidkey_vm_deal(1);
}

/*************************************************************************************************/
/*!
 *  \brief      注册控制是否进入sleep
 *
 *  \param      [in]
 *
 *  \return
 *
 *  \note
 */
/*************************************************************************************************/
//-----------------------
//system check go sleep is ok
static u8 hidkey_app_idle_query(void)
{
    return !is_hidkey_active;
}

REGISTER_LP_TARGET(app_hidkey_lp_target) = {
    .name = "app_hidkey_deal",
    .is_idle = hidkey_app_idle_query,
};


static const struct application_operation app_hidkey_ops = {
    .state_machine  = hidkey_state_machine,
    .event_handler 	= hidkey_event_handler,
};

/*
 * 注册模式
 */
REGISTER_APPLICATION(app_hidkey) = {
    .name 	= "hid_key",
    .action	= ACTION_HID_MAIN,
    .ops 	= &app_hidkey_ops,
    .state  = APP_STA_DESTROY,
};


#endif

