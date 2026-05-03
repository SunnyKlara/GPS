/*
 * drv_tm1638.h - TM1638/CM4718 LED驱动
 *
 * 硬件: CM4718 (TM1638兼容), 只有7个GRID, 14字节RAM
 * 接线: PA0=DIO, PA1=STB, PA2=CLK
 * 协议: 三线串行, LSB first, CLK上升沿采样
 *
 * 已确认的GRID→段映射:
 *   GRID1=a(顶横) GRID2=f(左上竖) GRID3=b(右上竖)
 *   GRID4=g(中间横) GRID5=e(左下竖) GRID6=c(右下竖) GRID7=d(底横)
 *
 * SEG→数码管位置:
 *   SEG1~SEG8 = 低字节bit0~7 (上排5位 + 下排3位)
 *   SEG9      = 高字节bit0   (下排第4位)
 *   SEG10     = 高字节bit1   (冒号/小数点, 待测)
 */
#ifndef _DRV_TM1638_H_
#define _DRV_TM1638_H_

#include "typedef.h"

/* CM4718实际参数 (不是TM1638的8 GRID/16字节!) */
#define TM1638_GRID_COUNT       7
#define TM1638_RAM_SIZE         14  /* 7 GRID × 2 bytes */
#define TM1638_BRIGHTNESS_MAX   7

/* 数码管位数 */
#define DISP_DIGIT_COUNT        9   /* SEG1~SEG9: 9个数码管 */

/* 显示位索引 (物理位置) */
#define DISP_POS_H10    0   /* 上排: 时十位 (SEG1) */
#define DISP_POS_H01    1   /* 上排: 时个位 (SEG2) */
#define DISP_POS_M10    2   /* 上排: 分十位 (SEG3) */
#define DISP_POS_M01    3   /* 上排: 分个位 (SEG4) */
#define DISP_POS_S01    4   /* 上排: 秒个位 (SEG5) */
#define DISP_POS_KM3    5   /* 下排: 千位   (SEG6) */
#define DISP_POS_KM2    6   /* 下排: 百位   (SEG7) */
#define DISP_POS_KM1    7   /* 下排: 十位   (SEG8) */
#define DISP_POS_KM0    8   /* 下排: 个位   (SEG9) */

/**
 * 初始化TM1638驱动
 * 配置GPIO, 清屏, 设置最大亮度
 */
void tm1638_init(void);

/**
 * 写入完整显示RAM (14字节)
 * @param data 14字节显示数据
 */
void tm1638_write_all(const u8 *data);

/**
 * 设置亮度
 * @param brightness 0~7
 * @param on 1=开启显示, 0=关闭
 */
void tm1638_set_brightness(u8 brightness, u8 on);

/**
 * 在指定数码管位置显示一个数字
 * @param buf  14字节显示缓冲区
 * @param pos  数码管位置 (DISP_POS_xxx, 0~8)
 * @param digit 数字 (0~9), 0xFF=空白, 0xFE=横杠'-'
 */
void tm1638_set_digit(u8 *buf, u8 pos, u8 digit);

/**
 * 设置SEG10的某个GRID位 (冒号/小数点控制)
 * @param buf   14字节显示缓冲区
 * @param grid  GRID索引 (0~6)
 * @param on    1=亮, 0=灭
 */
void tm1638_set_seg10(u8 *buf, u8 grid, u8 on);

/* 段码表 (已测试确认) */
extern const u8 tm1638_seg_table[10];

#endif /* _DRV_TM1638_H_ */
