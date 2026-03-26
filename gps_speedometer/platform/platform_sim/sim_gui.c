/*============================================================================
 * GPS测速仪 - Windows GUI模拟器
 * Win32 API: 7段数码管 + 按钮面板 + GPS/BLE状态
 *============================================================================*/
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <math.h>

#include "../../app/app_main.h"
#include "../../app/app_speed.h"
#include "../../app/app_display.h"
#include "../../app/app_ble_hid.h"
#include "../../app/app_key.h"
#include "../../lib/nmea_parser.h"
#include "../../config/config.h"
#include "../../platform/hal.h"

/* hal_sim.c 模拟注入接口 */
extern void sim_uart_inject(const uint8_t *data, uint16_t len);
extern void sim_key_set(uint8_t key_id, bool pressed);
extern void sim_ble_set_connected(bool connected);

/* GUI捕获显示数据的全局缓冲 */
uint8_t g_gui_display[8] = {0};
int     g_gui_mode = 1;  /* 1=GUI模式, hal_sim检测此变量 */

#define WIN_W 720
#define WIN_H 520
#define IDT_TIMER_MAIN  1
#define IDT_TIMER_GPS   2

/*--- 全局状态 ---*/
static HWND  s_hwnd;
static bool  s_ble_connected = false;
static float s_sim_speed = 0.0f;
static bool  s_gps_valid = false;

/* 颜色 */
#define C_SEG_ON    RGB(0,255,80)
#define C_SEG_OFF   RGB(20,40,20)
#define C_DISP_BG   RGB(10,15,10)
#define C_PANEL     RGB(30,30,35)
#define C_TEXT      RGB(200,200,200)
#define C_BTN       RGB(50,55,65)
#define C_BTN_BD    RGB(100,105,120)
#define C_BLE_ON    RGB(30,120,255)
#define C_BLE_OFF   RGB(100,100,100)
#define C_GPS_ON    RGB(0,200,80)
#define C_GPS_OFF   RGB(150,60,60)

/*==========================================================================
 * 7段数码管绘制
 *    --a--       bit0=a bit1=b bit2=c bit3=d bit4=e bit5=f bit6=g bit7=dp
 *   f     b
 *    --g--
 *   e     c
 *    --d--  .dp
 *==========================================================================*/
static void seg_h(HDC dc, int x, int y, int w, int t, COLORREF c)
{
    HBRUSH br = CreateSolidBrush(c);
    POINT p[6] = {
        {x+t/2, y}, {x+w-t/2, y}, {x+w, y+t/2},
        {x+w-t/2, y+t}, {x+t/2, y+t}, {x, y+t/2}
    };
    SelectObject(dc, br); SelectObject(dc, GetStockObject(NULL_PEN));
    Polygon(dc, p, 6); DeleteObject(br);
}

static void seg_v(HDC dc, int x, int y, int h, int t, COLORREF c)
{
    HBRUSH br = CreateSolidBrush(c);
    POINT p[6] = {
        {x+t/2, y}, {x+t, y+t/2}, {x+t, y+h-t/2},
        {x+t/2, y+h}, {x, y+h-t/2}, {x, y+t/2}
    };
    SelectObject(dc, br); SelectObject(dc, GetStockObject(NULL_PEN));
    Polygon(dc, p, 6); DeleteObject(br);
}

static void draw_digit(HDC dc, int x, int y, uint8_t seg, int w, int h, int t)
{
    int hh = (h - t) / 2;
    COLORREF on = C_SEG_ON, off = C_SEG_OFF;
    seg_h(dc, x+t,   y,          w-2*t, t, (seg&0x01)?on:off); /* a */
    seg_v(dc, x+w-t, y+t,        hh,    t, (seg&0x02)?on:off); /* b */
    seg_v(dc, x+w-t, y+t+hh,     hh,    t, (seg&0x04)?on:off); /* c */
    seg_h(dc, x+t,   y+h-t,      w-2*t, t, (seg&0x08)?on:off); /* d */
    seg_v(dc, x,     y+t+hh,     hh,    t, (seg&0x10)?on:off); /* e */
    seg_v(dc, x,     y+t,        hh,    t, (seg&0x20)?on:off); /* f */
    seg_h(dc, x+t,   y+hh,       w-2*t, t, (seg&0x40)?on:off); /* g */
    if (seg & 0x80) { /* dp */
        HBRUSH br = CreateSolidBrush(on);
        SelectObject(dc, br);
        Ellipse(dc, x+w+2, y+h-t-2, x+w+t+4, y+h+4);
        DeleteObject(br);
    }
}

/*==========================================================================
 * GPS模拟数据发送
 *==========================================================================*/
static void send_gps(float kmh)
{
    float knots = kmh / 1.852f;
    time_t now = time(NULL);
    struct tm *u = gmtime(&now);
    char body[128];
    snprintf(body, sizeof(body),
        "GNRMC,%02d%02d%02d.00,A,3939.9000,N,11616.4000,E,%.1f,0.0,%02d%02d%02d,,,A",
        u->tm_hour, u->tm_min, u->tm_sec, knots,
        u->tm_mday, u->tm_mon+1, u->tm_year%100);
    uint8_t cs = 0;
    for (int i = 0; body[i]; i++) cs ^= (uint8_t)body[i];
    char nmea[180];
    snprintf(nmea, sizeof(nmea), "$%s*%02X\r\n", body, cs);
    sim_uart_inject((const uint8_t*)nmea, (uint16_t)strlen(nmea));
    s_gps_valid = true;
}

/*==========================================================================
 * 按钮定义
 *==========================================================================*/
typedef struct { RECT r; wchar_t txt[24]; int key; int cmd; } btn_t;
#define CMD_BLE   100
#define CMD_SPD_U 101
#define CMD_SPD_D 102
#define CMD_MODE  103
#define CMD_RST   104

static btn_t s_btns[] = {
    {{30, 280,155,318}, L"Play/Pause",  0, -1},
    {{165,280,280,318}, L"|<< Prev",    1, -1},
    {{290,280,405,318}, L"Next >>|",    2, -1},
    {{415,280,510,318}, L"Vol +",       3, -1},
    {{520,280,615,318}, L"Vol -",       4, -1},
    {{30, 340,155,378}, L"BLE Toggle", -1, CMD_BLE},
    {{165,340,280,378}, L"Speed +10",  -1, CMD_SPD_U},
    {{290,340,405,378}, L"Speed -10",  -1, CMD_SPD_D},
    {{415,340,510,378}, L"Mode",       -1, CMD_MODE},
    {{520,340,615,378}, L"Reset Mile", -1, CMD_RST},
};
#define BTN_N (sizeof(s_btns)/sizeof(s_btns[0]))

static void do_btn_action(btn_t *b)
{
    if (b->key >= 0) {
        sim_key_set((uint8_t)b->key, true);
        Sleep(40);
        app_key_scan(); app_key_scan(); app_key_scan();
        sim_key_set((uint8_t)b->key, false);
        app_key_scan(); app_key_scan(); app_key_scan();
        return;
    }
    switch (b->cmd) {
    case CMD_BLE:
        s_ble_connected = !s_ble_connected;
        sim_ble_set_connected(s_ble_connected);
        break;
    case CMD_SPD_U:
        s_sim_speed += 10; if (s_sim_speed > 300) s_sim_speed = 300;
        send_gps(s_sim_speed);
        break;
    case CMD_SPD_D:
        s_sim_speed -= 10; if (s_sim_speed < 0) s_sim_speed = 0;
        send_gps(s_sim_speed);
        break;
    case CMD_MODE:
        app_display_next_mode();
        break;
    case CMD_RST:
        app_speed_reset_mileage();
        break;
    }
}

/*==========================================================================
 * 绘制整个界面
 *==========================================================================*/
static void paint(HDC dc, int w, int h)
{
    /* 背景 */
    HBRUSH bg = CreateSolidBrush(C_PANEL);
    RECT all = {0,0,w,h}; FillRect(dc, &all, bg); DeleteObject(bg);

    /* 数码管区域背景 */
    HBRUSH dbg = CreateSolidBrush(C_DISP_BG);
    RECT da = {20, 15, w-20, 155}; FillRect(dc, &da, dbg); DeleteObject(dbg);

    /* 标题 */
    SetBkMode(dc, TRANSPARENT);
    HFONT ft = CreateFontW(14,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,0,0,
        CLEARTYPE_QUALITY,0,L"Consolas");
    SelectObject(dc, ft);
    SetTextColor(dc, RGB(80,80,90));
    RECT tr = {25,18,300,34};
    DrawTextW(dc, L"GPS SPEEDOMETER", -1, &tr, DT_LEFT);

    /* 6位数码管 */
    for (int i = 0; i < 6; i++) {
        draw_digit(dc, 80 + i*66, 35, g_gui_display[i], 52, 80, 8);
    }

    /* 显示模式 */
    const wchar_t *mnames[] = {L"SPEED (km/h)", L"MILEAGE (km)", L"TIME (HH.MM.SS)", L"MAX SPEED (km/h)"};
    display_mode_t m = app_display_get_mode();
    HFONT ft2 = CreateFontW(14,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,
        CLEARTYPE_QUALITY,0,L"Consolas");
    SelectObject(dc, ft2);
    SetTextColor(dc, RGB(0,200,100));
    RECT mr = {80,125,500,142};
    DrawTextW(dc, mnames[m], -1, &mr, DT_LEFT);

    /* 状态指示灯 */
    HFONT ft3 = CreateFontW(14,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,
        CLEARTYPE_QUALITY,0,L"Segoe UI");
    SelectObject(dc, ft3);

    /* GPS */
    HBRUSH gb = CreateSolidBrush(s_gps_valid ? C_GPS_ON : C_GPS_OFF);
    RECT gd = {30,170,44,184}; FillRect(dc, &gd, gb); DeleteObject(gb);
    SetTextColor(dc, C_TEXT);
    wchar_t gt[32]; wsprintfW(gt, L"GPS: %s", s_gps_valid?L"Fixed":L"No Fix");
    RECT gr = {50,168,200,186}; DrawTextW(dc, gt, -1, &gr, DT_LEFT);

    /* BLE */
    HBRUSH bb = CreateSolidBrush(s_ble_connected ? C_BLE_ON : C_BLE_OFF);
    RECT bd = {220,170,234,184}; FillRect(dc, &bd, bb); DeleteObject(bb);
    wchar_t bt[32]; wsprintfW(bt, L"BLE: %s", s_ble_connected?L"Connected":L"Off");
    RECT br2 = {240,168,400,186}; DrawTextW(dc, bt, -1, &br2, DT_LEFT);

    /* 数值信息行 */
    const app_speed_data_t *sp = app_speed_get_data();
    const nmea_gps_data_t *gps = nmea_parser_get_data();
    int lh = (gps->hour + GPS_TIMEZONE_OFFSET) % 24;
    wchar_t inf[200];
    swprintf(inf, 200,
        L"Speed: %.1f km/h  |  Mile: %.2f km  |  Max: %.1f km/h  |  Sim: %.0f km/h",
        (double)sp->speed_kmh, (double)sp->mileage_km,
        (double)sp->max_speed_kmh, (double)s_sim_speed);
    RECT ir = {30,200,w-30,218}; DrawTextW(dc, inf, -1, &ir, DT_LEFT);

    wchar_t tm2[80];
    if (gps->is_valid)
        swprintf(tm2, 80, L"Time: %02d:%02d:%02d (UTC+8)   Date: %04d-%02d-%02d   Sat: %d",
            lh, gps->minute, gps->second, gps->year, gps->month, gps->day, gps->satellites);
    else
        swprintf(tm2, 80, L"Time: -- : -- : --   Waiting for GPS...");
    RECT tr2 = {30,220,w-30,238}; DrawTextW(dc, tm2, -1, &tr2, DT_LEFT);

    /* 分割线 */
    HPEN lp = CreatePen(PS_SOLID, 1, RGB(60,60,70));
    SelectObject(dc, lp);
    MoveToEx(dc, 30, 250, NULL); LineTo(dc, w-30, 250);
    MoveToEx(dc, 30, 265, NULL); LineTo(dc, w-30, 265);
    DeleteObject(lp);

    /* 区域标签 */
    SetTextColor(dc, RGB(120,120,140));
    RECT lbl1 = {30,252,200,264}; DrawTextW(dc, L"MUSIC CONTROL", -1, &lbl1, DT_LEFT);
    RECT lbl2 = {30,322,200,334}; DrawTextW(dc, L"FUNCTIONS", -1, &lbl2, DT_LEFT);

    /* 按钮 */
    for (int i = 0; i < (int)BTN_N; i++) {
        btn_t *b = &s_btns[i];
        HBRUSH bbr = CreateSolidBrush(C_BTN);
        FillRect(dc, &b->r, bbr); DeleteObject(bbr);
        HPEN bp = CreatePen(PS_SOLID, 1, C_BTN_BD);
        SelectObject(dc, bp); SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, b->r.left, b->r.top, b->r.right, b->r.bottom);
        DeleteObject(bp);
        SetTextColor(dc, C_TEXT);
        DrawTextW(dc, b->txt, -1, &b->r, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    }

    /* 速度滑块区域 */
    SetTextColor(dc, RGB(120,120,140));
    RECT slbl = {30,400,200,415}; DrawTextW(dc, L"SPEED SLIDER", -1, &slbl, DT_LEFT);

    /* 滑块背景 */
    HBRUSH sbg = CreateSolidBrush(RGB(40,42,48));
    RECT sbar = {30,420,w-30,440}; FillRect(dc, &sbar, sbg); DeleteObject(sbg);

    /* 滑块填充 */
    int fill_w = (int)((s_sim_speed / 300.0f) * (w - 60));
    if (fill_w > 0) {
        HBRUSH sfill = CreateSolidBrush(RGB(0,180,80));
        RECT sf = {30,420,30+fill_w,440}; FillRect(dc, &sf, sfill); DeleteObject(sfill);
    }
    /* 滑块文字 */
    SetTextColor(dc, RGB(255,255,255));
    wchar_t st[32]; swprintf(st, 32, L"%.0f km/h", (double)s_sim_speed);
    DrawTextW(dc, st, -1, &sbar, DT_CENTER|DT_VCENTER|DT_SINGLELINE);

    /* 底部键盘提示 */
    HFONT ft4 = CreateFontW(12,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,
        CLEARTYPE_QUALITY,0,L"Segoe UI");
    SelectObject(dc, ft4);
    SetTextColor(dc, RGB(90,90,100));
    RECT hrc = {30,h-25,w-30,h-5};
    DrawTextW(dc, L"Keys: 1-5 Music | B=BLE | Up/Down=Speed | M=Mode | R=Reset | Q=Quit", -1, &hrc, DT_CENTER);

    DeleteObject(ft); DeleteObject(ft2); DeleteObject(ft3); DeleteObject(ft4);
}

/*==========================================================================
 * 滑块拖拽
 *==========================================================================*/
static bool s_dragging = false;

static void handle_slider(int mx)
{
    if (mx < 30) mx = 30;
    if (mx > WIN_W - 30) mx = WIN_W - 30;
    s_sim_speed = (float)(mx - 30) / (float)(WIN_W - 60) * 300.0f;
    send_gps(s_sim_speed);
}

/*==========================================================================
 * 窗口过程
 *==========================================================================*/
static LRESULT CALLBACK WndProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        SetTimer(hw, IDT_TIMER_MAIN, 20, NULL);   /* 50fps 主循环 */
        SetTimer(hw, IDT_TIMER_GPS, 1000, NULL);   /* 1Hz GPS */
        return 0;

    case WM_TIMER:
        if (wp == IDT_TIMER_MAIN) {
            app_main_loop();
            InvalidateRect(hw, NULL, FALSE);
        } else if (wp == IDT_TIMER_GPS) {
            send_gps(s_sim_speed);
        }
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hw, &ps);
        /* 双缓冲 */
        RECT cr; GetClientRect(hw, &cr);
        HDC mdc = CreateCompatibleDC(dc);
        HBITMAP mbm = CreateCompatibleBitmap(dc, cr.right, cr.bottom);
        SelectObject(mdc, mbm);
        paint(mdc, cr.right, cr.bottom);
        BitBlt(dc, 0, 0, cr.right, cr.bottom, mdc, 0, 0, SRCCOPY);
        DeleteObject(mbm);
        DeleteDC(mdc);
        EndPaint(hw, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        int mx = LOWORD(lp), my = HIWORD(lp);
        /* 检查滑块 */
        if (my >= 420 && my <= 440) {
            s_dragging = true; SetCapture(hw);
            handle_slider(mx);
            return 0;
        }
        /* 检查按钮 */
        POINT pt = {mx, my};
        for (int i = 0; i < (int)BTN_N; i++) {
            if (PtInRect(&s_btns[i].r, pt)) {
                do_btn_action(&s_btns[i]);
                InvalidateRect(hw, NULL, FALSE);
                return 0;
            }
        }
        return 0;
    }

    case WM_MOUSEMOVE:
        if (s_dragging) {
            handle_slider(LOWORD(lp));
            InvalidateRect(hw, NULL, FALSE);
        }
        return 0;

    case WM_LBUTTONUP:
        if (s_dragging) { s_dragging = false; ReleaseCapture(); }
        return 0;

    case WM_KEYDOWN:
        switch (wp) {
        case '1': case '2': case '3': case '4': case '5': {
            int k = (int)(wp - '1');
            sim_key_set((uint8_t)k, true);
            break;
        }
        case 'B':
            s_ble_connected = !s_ble_connected;
            sim_ble_set_connected(s_ble_connected);
            break;
        case VK_UP:
            s_sim_speed += 5; if (s_sim_speed > 300) s_sim_speed = 300;
            send_gps(s_sim_speed);
            break;
        case VK_DOWN:
            s_sim_speed -= 5; if (s_sim_speed < 0) s_sim_speed = 0;
            send_gps(s_sim_speed);
            break;
        case 'M':
            app_display_next_mode();
            break;
        case 'R':
            app_speed_reset_mileage();
            break;
        case 'Q':
            PostQuitMessage(0);
            break;
        }
        return 0;

    case WM_KEYUP:
        if (wp >= '1' && wp <= '5') {
            sim_key_set((uint8_t)(wp - '1'), false);
        }
        return 0;

    case WM_DESTROY:
        KillTimer(hw, IDT_TIMER_MAIN);
        KillTimer(hw, IDT_TIMER_GPS);
        app_speed_save_mileage();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

/*==========================================================================
 * WinMain 入口
 *==========================================================================*/
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmdLine, int nShow)
{
    (void)hPrev; (void)cmdLine;

    /* 初始化固件逻辑 */
    app_main_init();

    /* 注册窗口类 */
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = L"GPS_SIM";
    RegisterClassExW(&wc);

    /* 计算窗口大小（含边框） */
    RECT wr = {0, 0, WIN_W, WIN_H};
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME, FALSE);

    s_hwnd = CreateWindowExW(0, L"GPS_SIM",
        L"GPS Speedometer Simulator - AC6323A+AT6558A+TM5020A",
        (WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX) | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT,
        wr.right - wr.left, wr.bottom - wr.top,
        NULL, NULL, hInst, NULL);

    ShowWindow(s_hwnd, nShow);
    UpdateWindow(s_hwnd);

    /* 消息循环 */
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return (int)msg.wParam;
}
