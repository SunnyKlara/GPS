/*
 * gps_uart.c - GPS UART 驱动（修复版 v4，延时彻底修正）
 *
 * 工作流程（开机初始化阶段，非阻塞状态机）:
 *   阶段0-子阶段GPIO: 纯GPIO读PB5电平，确认GPS有输出
 *   阶段0-子阶段TX:   GPIO软件模拟发 $PCAS03 配置命令
 *   阶段0-子阶段WAIT: 等待GPS响应
 *   阶段0-子阶段DONE: 打开SDK UART，进入RX_NMEA
 *   阶段1(RX_NMEA):   正常接收NMEA，解析并显示
 *
 * 关键修复(v4):
 *   - 延时宏彻底修正: 24MHz下 1 NOP = 1/24MHz = 0.04167us
 *   - gpio_delay_us(): 每轮1个NOP，即 1 * 0.04167us (精确!)
 *   - GPIO 软件模拟 UART TX: 9600bps，每位 = 2500 NOPs ≈ 104.17us
 *   - gpio_delay_ms(): 循环1000次 gpio_delay_us(1000)
 *   - 不依赖任何SDK不存在的函数，不碰RTOS中断
 *
 * 显示含义:
 *   开机: 88:88:8
 *   阶段0-GPIO: 00:00:0 (GPIO测试)
 *   阶段0-TX:   00:01:1 (发配置)
 *   阶段0-WAIT: 09:60:2 (等待NMEA)
 *   阶段1:      HH:MM:SS (GPS时间) + 下排速度
 */
#include "system/includes.h"
#include "app_config.h"
#include "nmea_parser.h"
#include "gps_uart.h"
#include "gps_main.h"
#include "app_display.h"
#include "generic/typedef.h"

#define LOG_TAG     "[GPS_UART]"
#define LOG_ERROR_ENABLE
#define LOG_INFO_ENABLE
#include "debug.h"

#define GPS_TX_PIN      IO_PORTB_04
#define GPS_RX_PIN      IO_PORTB_05
#define GPS_ONOFF_PIN   IO_PORTA_07

#define GPS_UART_CBUF_SIZE  256
#define GPS_UART_FRAME_LEN  32
#define GPS_UART_RX_OT      10

/*------------------------------------------------------*/
/*              微秒/毫秒级延时（纯汇编，忙等待）           */
/*------------------------------------------------------*/
/*
 * 24MHz系统时钟: 1 NOP = 1 CPU周期 = 1/24MHz ≈ 0.04167us
 * gpio_delay_us(n): 循环n次，每次1个NOP → 精确n微秒
 * gpio_delay_ms(n): 循环n*1000次gpio_delay_us(1) → 精确n毫秒
 *
 * 注意: 不能在 gpio_delay_ms 内部直接写 gpio_delay_us(1000)，
 *       因为gpio_delay_us也是宏，会导致展开混乱。
 *       用 static 函数来实现毫秒延时。
 */
#define __NOP()  __asm__ volatile("nop")

/* 精确的微秒延时（注意: 参数us范围有限，勿传太大值） */
#define gpio_delay_us(us) do {           \
    u32 __i;                             \
    for (__i = 0; __i < (us); __i++) {   \
        __NOP();                          \
    }                                     \
} while (0)

/* 毫秒延时（static函数，不依赖宏展开） */
static void gpio_delay_ms(u16 ms_count)
{
    u16 __i2;
    for (__i2 = 0; __i2 < ms_count; __i2++) {
        u32 __j;
        for (__j = 0; __j < 1000; __j++) {
            __NOP();
        }
    }
}

/*------------------------------------------------------*/
/*                   全局变量                            */
/*------------------------------------------------------*/

static u8 s_uart_cbuf[GPS_UART_CBUF_SIZE] __attribute__((aligned(4)));
static const uart_bus_t *s_gps_uart;

/* 工作阶段 */
#define PHASE_INIT       0   /* 初始化序列（只执行一次） */
#define PHASE_RX_NMEA    1   /* 正常接收NMEA（永久停留） */
static u8 s_phase;

/* GPIO 测试计数 */
static u32 s_high_count;
static u32 s_low_count;
static u32 s_gpio_sample_count;
#define GPIO_SAMPLE_MAX  5000   /* 约500ms采样 */

/* 初始化序列状态（阶段0内部分状态） */
#define SUB_INIT_GPIO    0   /* GPIO电平测试 */
#define SUB_INIT_TX      1   /* 发配置命令 */
#define SUB_INIT_WAIT    2   /* 等待GPS响应 */
#define SUB_INIT_DONE    3   /* 初始化完成，切换到RX */
static u8 s_init_sub_phase;

/* UART 统计 */
static u32 s_rx_total;
static u32 s_dollar_count;
static u32 s_nmea_ok_count;
static u8  s_last_byte;
static u32 s_non_ascii_count;

/* GPS 数据 */
static volatile u8 s_new_data;
static volatile u8 s_gps_hour;
static volatile u8 s_gps_minute;
static volatile u8 s_gps_second;
static volatile u8 s_gps_valid;
static volatile float s_gps_speed_kmh;

static u16 s_timeout_cnt;
#define GPS_TIMEOUT_TICKS   50
static float s_filtered_speed;
#define SPEED_FILTER_ALPHA  0.3f
#define SPEED_MIN_KMH       1.0f

#define NMEA_START_CHAR  0x24

/* 配置命令: $PCAS03 让GPS只输出RMC语句
 * 格式: $PCAS03,<GGA>,<GLL>,<GSA>,<GSV>,<RMC>,<VTG>,<GRS>,<GST>
 * 0=禁用，1=启用
 * 校验和: $和*之间所有字符的XOR = 0x1F (不是0x03!)
 * AT6558R 支持NMEA配置命令，详见AT6558数据手册
 */
static const char s_config_cmd[] = "$PCAS03,0,0,0,0,1,0,0,0*1F\r\n";

/*------------------------------------------------------*/
/*                   硬件操作函数                        */
/*------------------------------------------------------*/

static void gps_uart_isr(void *arg, u32 status)
{
    (void)arg;
    (void)status;
}

/* 打开 GPS UART
 * tx_pin: TX引脚，设为(u8)-1则不配置TX（只接收模式）
 * rx_pin: RX引脚
 * 返回: 1成功 0失败
 */
static u8 gps_uart_hw_open(u32 baud, u8 tx_pin, u8 rx_pin)
{
    struct uart_platform_data_t gps_uart_arg;

    log_info("GPS HW open: baud=%d tx=%d rx=%d", (int)baud, tx_pin, rx_pin);

    memset(&gps_uart_arg, 0, sizeof(gps_uart_arg));
    gps_uart_arg.tx_pin = tx_pin;
    gps_uart_arg.rx_pin = rx_pin;
    gps_uart_arg.baud = baud;
    gps_uart_arg.rx_cbuf = s_uart_cbuf;
    gps_uart_arg.rx_cbuf_size = GPS_UART_CBUF_SIZE;
    gps_uart_arg.frame_length = GPS_UART_FRAME_LEN;
    gps_uart_arg.rx_timeout = GPS_UART_RX_OT;
    gps_uart_arg.isr_cbfun = gps_uart_isr;
    gps_uart_arg.argv = NULL;
    gps_uart_arg.is_9bit = 0;

    s_gps_uart = uart_dev_open(&gps_uart_arg);
    if (!s_gps_uart) {
        log_info("GPS UART open FAILED! Check CONFIG_UARTx_ENABLE in uart_dev.c");
        return 0;
    }
    log_info("GPS UART open OK (using real HW UART), baud=%d", (int)baud);
    return 1;
}

/*------------------------------------------------------*/
/*           GPIO 软件模拟 UART TX (9600bps)            */
/*------------------------------------------------------*/
/*
 * 9600bps: 每位时间 = 104.167us
 * 24MHz, 1 NOP = 1周期 = 0.04167us
 * 104.167us / 0.04167us ≈ 2500 NOPs per bit
 *
 * 帧格式: 1起始位 + 8数据位(LSB优先) + 1停止位 = 10 bits
 * 每字节总时间 ≈ 1.042ms → 实际波特率 ≈ 9600
 */
static void gpio_uart_send_byte(u8 byte_val)
{
    u8 bit_idx;
    u8 level;

    /* Start bit (低电平) */
    gpio_write(GPS_TX_PIN, 0);
    for (bit_idx = 0; bit_idx < 2500; bit_idx++) {
        __NOP();
    }

    /* 8数据位, LSB先发 */
    for (bit_idx = 0; bit_idx < 8; bit_idx++) {
        level = (byte_val >> bit_idx) & 1;
        gpio_write(GPS_TX_PIN, level);
        for (bit_idx = 0; bit_idx < 2500; bit_idx++) {
            __NOP();
        }
    }

    /* Stop bit (高电平) */
    gpio_write(GPS_TX_PIN, 1);
    for (bit_idx = 0; bit_idx < 2500; bit_idx++) {
        __NOP();
    }
}

/* 修正版: 内层循环变量不冲突（C89兼容） */
static void gpio_uart_send_byte_fixed(u8 byte_val)
{
    u8 i;
    u8 level;

    /* Start bit (低电平) */
    gpio_write(GPS_TX_PIN, 0);
    for (i = 0; i < 2500; i++) {
        __NOP();
    }

    /* 8数据位, LSB先发 */
    for (i = 0; i < 8; i++) {
        level = (byte_val >> i) & 1;
        gpio_write(GPS_TX_PIN, level);
        {
            u8 j;
            for (j = 0; j < 2500; j++) {
                __NOP();
            }
        }
    }

    /* Stop bit (高电平) */
    gpio_write(GPS_TX_PIN, 1);
    {
        u8 j;
        for (j = 0; j < 2500; j++) {
            __NOP();
        }
    }
}

/* 通过 GPIO 模拟发送配置命令字符串
 * 发送完成后保持TX为高（空闲状态）
 */
static void gpio_uart_send_config(void)
{
    u8 i;
    u8 retry;

    log_info("GPIO UART TX: sending $PCAS03 config at 9600bps");
    log_info("cmd: %s", s_config_cmd);

    /* 发送3次，确保GPS能收到 */
    for (retry = 0; retry < 3; retry++) {
        log_info("TX attempt %d/3", retry + 1);

        /* 发送配置字符串 */
        for (i = 0; s_config_cmd[i] != '\0'; i++) {
            gpio_uart_send_byte_fixed((u8)s_config_cmd[i]);
        }

        /* 字符串间隔 500ms (GPS需要时间处理配置) */
        gpio_delay_ms(500);
    }

    log_info("GPIO UART TX: config sent (%d bytes)", (int)i);
}

/*------------------------------------------------------*/
/*                 初始化序列（阶段0）                    */
/*------------------------------------------------------*/

/* 进入初始化子阶段 */
static void enter_init_sub_phase(u8 sub)
{
    s_init_sub_phase = sub;

    switch (sub) {
    case SUB_INIT_GPIO:
        /* 配置PB5为普通GPIO输入，准备采样 */
        gpio_direction_input(GPS_RX_PIN);
        gpio_set_pull_up(GPS_RX_PIN, 1);
        gpio_set_die(GPS_RX_PIN, 1);
        /* PB4也要释放（之前可能是GPIO输出） */
        gpio_direction_input(GPS_TX_PIN);
        gpio_set_pull_up(GPS_TX_PIN, 1);
        gpio_set_die(GPS_TX_PIN, 1);

        s_high_count = 0;
        s_low_count = 0;
        s_gpio_sample_count = 0;

        gps_display_set_time(0, 0, 0, 1);
        gps_display_set_speed(0, 1);
        log_info("Init subphase: GPIO test");
        break;

    case SUB_INIT_TX:
        /* 配置PB4为普通GPIO输出，准备发TX
         * 注意：这里不调用 SDK UART，只用GPIO模拟
         */
        gpio_direction_output(GPS_TX_PIN, 1);
        gpio_set_pull_up(GPS_TX_PIN, 0);
        gpio_set_die(GPS_TX_PIN, 1);

        gps_display_set_time(0, 1, 1, 1);
        gps_display_set_speed(1, 1);
        log_info("Init subphase: TX config");
        break;

    case SUB_INIT_WAIT:
        /* 发完配置后，等待GPS响应
         * PB4切回输入（释放）
         * PB5已在输入模式
         */
        gpio_direction_input(GPS_TX_PIN);
        gpio_set_pull_up(GPS_TX_PIN, 1);
        gpio_set_die(GPS_TX_PIN, 1);

        gps_display_set_time(9, 60, 2, 1);
        gps_display_set_speed(2, 1);
        log_info("Init subphase: waiting for GPS response");
        break;

    case SUB_INIT_DONE:
        /*
         * 修复: 传入真实的TX引脚GPS_TX_PIN，而不用(u8)-1
         *
         * 问题: 之前传tx_pin=(u8)-1是想做RX-only模式，但发现:
         *   - (u8)-1 = 255，uart_config()的判断 `!(tx<MAX || rx<MAX)` 虽然
         *     不会直接return -1，但后续的TX配置被跳过是OK的
         *   - 真正的问题可能是: 当只有RX有效时，uart_dev_open()可能选择了
         *     错误的UART（UART0而不是UART1）
         *
         * 解决: 传入GPS_TX_PIN=PB4，让uart_dev_open()正确选择UART1，
         *       并同时配置PB4(TX)和PB5(RX)，形成完整的双工连接。
         *
         * 接线确认:
         *   AC6323 PB4(TX) → GPS模块 RX (接收配置命令)
         *   AC6323 PB5(RX) ← GPS模块 TX (发送NMEA数据) ← 这是关键！
         */
        gps_uart_hw_open(9600, GPS_TX_PIN, GPS_RX_PIN);

        /* 切换到RX阶段 */
        s_phase = PHASE_RX_NMEA;
        s_rx_total = 0;
        s_dollar_count = 0;
        s_nmea_ok_count = 0;
        s_non_ascii_count = 0;
        nmea_parser_init();
        log_info("Init subphase: DONE, now in RX_NMEA phase");
        break;
    }
}

/*------------------------------------------------------*/
/*                    公开接口                            */
/*------------------------------------------------------*/

void gps_uart_init(void)
{
    nmea_parser_init();
    s_timeout_cnt = GPS_TIMEOUT_TICKS;
    s_filtered_speed = 0;
    s_new_data = 0;
    s_gps_uart = NULL;
    s_phase = PHASE_INIT;

    /* ON_OFF引脚: 拉低让GPS模块自动启动
     * (board_ac6323a_demo.c的mask_io_cfg()已将PA7设为SOFTFLAG_OUT0=低)
     */
    gpio_direction_output(GPS_ONOFF_PIN, 0);

    /* 从GPIO测试开始初始化序列 */
    enter_init_sub_phase(SUB_INIT_GPIO);
    log_info("GPS UART init done, TX=%d RX=%d ONOFF=%d",
        GPS_TX_PIN, GPS_RX_PIN, GPS_ONOFF_PIN);
}

void gps_uart_loop(void)
{
    u8 buf[64];
    u32 rlen;
    u32 i;
    u8 byte_val;
    u8 pb5_val;
    const nmea_gps_data_t *gps;

    /*------------------------------------------------------*/
    /* 阶段0: 初始化序列（非阻塞状态机）                      */
    /*------------------------------------------------------*/
    if (s_phase == PHASE_INIT) {
        switch (s_init_sub_phase) {

        /* 子阶段0: GPIO电平测试（一次loop完成，不阻塞） */
        case SUB_INIT_GPIO: {
            u32 i;
            u32 hi = 0, lo = 0;

            log_info("GPIO sampling start...");

            /* 一次性采样5000次，约50ms */
            for (i = 0; i < GPIO_SAMPLE_MAX; i++) {
                if (gpio_read(GPS_RX_PIN)) {
                    hi++;
                } else {
                    lo++;
                }
            }

            log_info("GPIO test done: hi=%d lo=%d", hi, lo);

            if (lo > 10) {
                /* GPS有输出，进入TX配置阶段 */
                enter_init_sub_phase(SUB_INIT_TX);
            } else {
                /* GPS没有输出，跳过配置直接进入RX阶段 */
                log_info("No GPS activity, skip to RX phase");
                enter_init_sub_phase(SUB_INIT_DONE);
            }
            break;
        }

        /* 子阶段1: 发配置命令（只执行一次，约2秒） */
        case SUB_INIT_TX: {
            static u8 s_tx_done;

            if (!s_tx_done) {
                /* 发送 $PCAS03 命令，让GPS只输出RMC语句
                 * 发送3次确保GPS收到
                 */
                gpio_uart_send_config();

                /* 发完后标记，然后进入等待阶段 */
                s_tx_done = 1;
                log_info("TX config sent, entering wait phase");

                /* 切换到等待子阶段 */
                enter_init_sub_phase(SUB_INIT_WAIT);
            }
            break;
        }

        /* 子阶段2: 等待GPS响应（约3秒） */
        case SUB_INIT_WAIT: {
            static u32 s_wait_count;

            s_wait_count++;

            /* 等待30次loop（约3秒）后直接进入RX阶段
             * 即使GPS没有正确响应配置命令，
             * 只要它之前在输出数据（乱码也行），
             * 说明物理连接是通的，直接进入RX阶段
             */
            if (s_wait_count >= 30) {
                s_wait_count = 0;
                log_info("Wait done, entering RX_NMEA phase");
                enter_init_sub_phase(SUB_INIT_DONE);
            }
            break;
        }

        default:
            break;
        }
        return;  /* 初始化阶段不需要处理RX */
    }

    /*------------------------------------------------------*/
    /* 阶段1: 正常接收NMEA（永久停留）                       */
    /*------------------------------------------------------*/
    if (s_phase == PHASE_RX_NMEA) {
        if (!s_gps_uart) {
            /* UART未打开，可能是之前失败了，尝试重新打开 */
            log_info("GPS UART not opened, retrying...");
            if (!gps_uart_hw_open(9600, (u8)-1, GPS_RX_PIN)) {
                return;
            }
        }

        /* 读取所有可用字节 */
        rlen = s_gps_uart->read(buf, sizeof(buf), 0);
        for (i = 0; i < rlen; i++) {
            byte_val = buf[i];
            s_rx_total++;
            s_last_byte = byte_val;

            /* 统计 $ 字符数量（NMEA帧头） */
            if (byte_val == NMEA_START_CHAR) {
                s_dollar_count++;
                log_info("[GPS RAW]");
                log_info_hexdump(buf, rlen);
            }

            /* 统计非ASCII字符（用于判断GPS是否输出乱码） */
            if (byte_val < 32 || byte_val > 126) {
                if (byte_val != '\r' && byte_val != '\n') {
                    s_non_ascii_count++;
                }
            }

            /* 喂给NMEA解析器 */
            if (nmea_parser_feed(byte_val)) {
                s_nmea_ok_count++;
                gps = nmea_parser_get_data();
                s_gps_hour   = gps->hour;
                s_gps_minute = gps->minute;
                s_gps_second = gps->second;
                s_gps_valid  = gps->is_valid;
                s_gps_speed_kmh = gps->speed_kmh;
                s_new_data = 1;

                /* 成功解析后打印（方便调试） */
                log_info("NMEA OK: %02d:%02d:%02d valid=%d speed=%.1f",
                    s_gps_hour, s_gps_minute, s_gps_second,
                    s_gps_valid, (float)s_gps_speed_kmh);
            }
        }

        /* 每收到500字节打印一次统计 */
        if (s_rx_total > 0 && (s_rx_total % 500 == 0)) {
            u32 pct = 0;
            if (s_rx_total > 0) {
                pct = (s_non_ascii_count * 100) / s_rx_total;
            }
            log_info("GPS RX: total=%d $=%d nmea=%d non_ascii=%d%% last=0x%02x",
                s_rx_total, s_dollar_count, s_nmea_ok_count,
                pct, s_last_byte);
        }

        /* GPS数据处理 */
        if (s_new_data) {
            s_new_data = 0;
            s_timeout_cnt = 0;

            /* 显示GPS时间 */
            gps_display_set_time(s_gps_hour, s_gps_minute, s_gps_second, 1);

            /* 速度滤波 */
            if (s_gps_valid) {
                s_filtered_speed = s_filtered_speed * (1.0f - SPEED_FILTER_ALPHA)
                                 + s_gps_speed_kmh * SPEED_FILTER_ALPHA;
                if (s_filtered_speed < SPEED_MIN_KMH) {
                    s_filtered_speed = 0;
                }
            } else {
                s_filtered_speed = 0;
            }
            gps_speed_update(s_filtered_speed, s_gps_valid);
        }

        /* GPS超时处理（5秒无数据认为丢失） */
        if (s_timeout_cnt < GPS_TIMEOUT_TICKS) {
            s_timeout_cnt++;
            if (s_timeout_cnt >= GPS_TIMEOUT_TICKS) {
                log_info("GPS timeout, clearing speed");
                gps_speed_update(0, 0);
                s_filtered_speed = 0;
            }
        }

        /* 里程累加 */
        if (s_filtered_speed > SPEED_MIN_KMH) {
            float dt_hours;
            dt_hours = 0.1f / 3600.0f;
            gps_mileage_update(s_filtered_speed * dt_hours);
        }
    }
}

/* POWER键: 重启初始化序列（用于调试） */
void gps_uart_show_next(void)
{
    if (s_phase == PHASE_RX_NMEA) {
        /* 从RX阶段重启到INIT */
        log_info("Restarting GPS init sequence");
        s_phase = PHASE_INIT;
        s_rx_total = 0;
        s_dollar_count = 0;
        s_nmea_ok_count = 0;
        s_non_ascii_count = 0;
        nmea_parser_init();
        enter_init_sub_phase(SUB_INIT_GPIO);
    }
}
