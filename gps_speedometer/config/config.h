#ifndef _CONFIG_H_
#define _CONFIG_H_

/*============================================================================
 * GPS测速仪 - 全局配置
 *============================================================================*/

/* 系统时钟 */
#define SYS_TICK_MS             10      /* 系统tick间隔(ms) */

/* GPS配置 */
#define GPS_UART_BAUDRATE       9600
#define GPS_UPDATE_INTERVAL_MS  1000    /* GPS数据更新间隔 */
#define GPS_NMEA_BUF_SIZE       256     /* NMEA语句缓冲区大小 */
#define GPS_TIMEZONE_OFFSET     8       /* 时区偏移：UTC+8(北京时间) */

/* 速度配置 */
#define SPEED_MAX_KMH           999.9f  /* 最大显示速度 */
#define SPEED_FILTER_ALPHA      0.3f    /* 低通滤波系数(0~1, 越小越平滑) */
#define SPEED_MIN_VALID_KMH     1.0f    /* 低于此速度认为静止 */

/* 里程配置 */
#define MILEAGE_SAVE_INTERVAL_S 30      /* 里程自动保存间隔(秒) */
#define MILEAGE_FLASH_ADDR      0x1000  /* Flash存储地址(具体看芯片手册) */

/* 显示配置 */
#define DISPLAY_DIGITS          6       /* 显示位数 */
#define DISPLAY_REFRESH_MS      100     /* 显示刷新间隔 */

/* BLE HID配置 */
#define BLE_DEVICE_NAME         "GPS-Speedometer"
#define BLE_HID_REPORT_ID       1

/* 按键配置 */
#define KEY_NUM                 5
#define KEY_DEBOUNCE_MS         20      /* 消抖时间 */
#define KEY_LONG_PRESS_MS       1500    /* 长按时间 */

/* 按键功能映射 */
#define KEY_PLAY_PAUSE          0
#define KEY_PREV_TRACK          1
#define KEY_NEXT_TRACK          2
#define KEY_VOL_UP              3
#define KEY_VOL_DOWN            4

/* 编译目标 */
/* #define PLATFORM_AC6323A */     /* 真机平台 - 拿到SDK后取消注释 */
#ifndef PLATFORM_SIM
#define PLATFORM_SIM               /* PC模拟平台 */
#endif

#endif /* _CONFIG_H_ */
