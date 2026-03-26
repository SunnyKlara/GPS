/*============================================================================
 * PC模拟器入口
 * 模拟GPS数据输入、按键操作和BLE连接
 * ��于在没有硬件的情况下验证核心业务逻辑
 *
 * 使用方法:
 *   编译: make sim
 *   运行: ./build/gps_sim
 *
 * 交互命令:
 *   1-5     模拟按键短按 (1=播放/暂停 2=上一曲 3=下一曲 4=音量+ 5=音量-)
 *   b       切换BLE连接状态
 *   g       发送一帧模拟GPS数据
 *   s       设置模拟速度 (输入km/h值)
 *   q       退出
 *============================================================================*/

#include "../../app/app_main.h"
#include "../../app/app_speed.h"
#include "../../config/config.h"
#include "../../platform/hal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#ifdef _WIN32
#include <conio.h>
#include <windows.h>
#define kbhit _kbhit
#define getch _getch
#else
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
static int kbhit(void) {
    struct termios oldt, newt;
    int ch, oldf;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);
    ch = getchar();
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    fcntl(STDIN_FILENO, F_SETFL, oldf);
    if (ch != EOF) { ungetc(ch, stdin); return 1; }
    return 0;
}
#define getch getchar
#endif

/* 外部声明：模拟器注入函数 */
extern void sim_uart_inject(const uint8_t *data, uint16_t len);
extern void sim_key_set(uint8_t key_id, bool pressed);
extern void sim_ble_set_connected(bool connected);

static bool s_running = true;
static bool s_ble_sim_connected = false;
static float s_sim_speed = 0.0f;

/**
 * 计算NMEA语句的校验和
 */
static uint8_t nmea_checksum(const char *sentence)
{
    uint8_t cs = 0;
    for (int i = 0; sentence[i]; i++) {
        cs ^= (uint8_t)sentence[i];
    }
    return cs;
}

/**
 * 生成模拟的 $GNRMC 语句
 */
static void send_sim_gps(float speed_kmh)
{
    float speed_knots = speed_kmh / 1.852f;

    /* 用当前PC时间生成UTC时间 */
    time_t now = time(NULL);
    struct tm *utc = gmtime(&now);

    char body[128];
    snprintf(body, sizeof(body),
        "GNRMC,%02d%02d%02d.00,A,3939.9000,N,11616.4000,E,%.1f,0.0,%02d%02d%02d,,,A",
        utc->tm_hour, utc->tm_min, utc->tm_sec,
        speed_knots,
        utc->tm_mday, utc->tm_mon + 1, utc->tm_year % 100);

    uint8_t cs = nmea_checksum(body);

    char nmea[160];
    snprintf(nmea, sizeof(nmea), "$%s*%02X\r\n", body, cs);

    sim_uart_inject((const uint8_t *)nmea, (uint16_t)strlen(nmea));
}

static void print_help(void)
{
    printf("\n--- GPS测速仪 PC模拟器 ---\n");
    printf("  1: 播放/暂停   2: 上一曲   3: 下一曲\n");
    printf("  4: 音量+       5: 音量-\n");
    printf("  b: 切换BLE连接  g: 发GPS帧  s: 设速度\n");
    printf("  m: 切换显示模式  r: 清零里程\n");
    printf("  q: 退出\n");
    printf("--------------------------\n");
}

static void handle_input(char ch)
{
    switch (ch) {
    case '1': case '2': case '3': case '4': case '5': {
        int key_id = ch - '1';
        /* 模拟短按：按下->延时->释放 */
        sim_key_set(key_id, true);
        /* 在主循环中会自动扫描并处理 */
        printf("[输入] 按键%d 按下\n", key_id);
        /* 设置一个标记，在下一次循环释放 */
        break;
    }
    case 'b':
        s_ble_sim_connected = !s_ble_sim_connected;
        sim_ble_set_connected(s_ble_sim_connected);
        break;

    case 'g':
        send_sim_gps(s_sim_speed);
        break;

    case 's':
        printf("输入模拟速度(km/h): ");
        scanf("%f", &s_sim_speed);
        printf("[模拟] 速度设为 %.1f km/h\n", s_sim_speed);
        send_sim_gps(s_sim_speed);
        break;

    case 'm':
        printf("[输入] 切换显示模式\n");
        /* 通过长按播放键模拟 */
        break;

    case 'r':
        printf("[输入] 清零里程\n");
        break;

    case 'q':
        s_running = false;
        printf("[模拟] 退出中...\n");
        break;

    case 'h':
    case '?':
        print_help();
        break;
    }
}

int main(void)
{
    printf("=====================================\n");
    printf("  GPS测速仪 - PC模拟器\n");
    printf("  按 'h' 查看帮助\n");
    printf("=====================================\n\n");

    /* 初始化应用 */
    app_main_init();
    print_help();

    /* 自动GPS数据发送计时 */
    uint32_t last_gps_tick = 0;
    uint32_t key_release_tick = 0;
    int pending_key = -1;

    while (s_running) {
        /* 主循环 */
        app_main_loop();

        /* 自动每秒发送GPS数据 */
        uint32_t now = hal_get_tick_ms();
        if ((now - last_gps_tick) >= GPS_UPDATE_INTERVAL_MS) {
            last_gps_tick = now;
            send_sim_gps(s_sim_speed);
        }

        /* 处理按键释放 */
        if (pending_key >= 0 && (now - key_release_tick) >= 50) {
            sim_key_set(pending_key, false);
            pending_key = -1;
        }

        /* 检查键盘输入 */
        if (kbhit()) {
            char ch = (char)getch();
            if (ch >= '1' && ch <= '5') {
                int key_id = ch - '1';
                sim_key_set(key_id, true);
                pending_key = key_id;
                key_release_tick = now;
            }
            handle_input(ch);
        }

        /* 控制主循环频率 ~10ms */
        hal_delay_ms(SYS_TICK_MS);
    }

    /* 保存里程 */
    app_speed_save_mileage();
    printf("[模拟] 里程已保存，程序退出\n");

    return 0;
}
