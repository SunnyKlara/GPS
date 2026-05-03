/*
 * drv_tm1638.c - TM1638/CM4718 LED驱动实现
 *
 * 关键: CM4718只有7个GRID, 只写14字节! 写16字节会地址回绕覆盖GRID1!
 */
#include "system/includes.h"
#include "drv_tm1638.h"

/* GPIO引脚 */
#define TM_DIO_PIN  IO_PORTA_00
#define TM_STB_PIN  IO_PORTA_01
#define TM_CLK_PIN  IO_PORTA_02

/* 微延时 - 24MHz下，增加延时确保TM1638时序稳定 */
/* 24MHz: 1 NOP ≈ 0.04167us
 * TM1638要求CLK高/低电平 ≥ 400ns
 * 原来: 10 NOP ≈ 0.417us (勉强够)
 * 现在: 30 NOP ≈ 1.25us (更安全)
 */
#define TM_DELAY_SHORT()  do { volatile int _d = 10; while(_d--); } while(0)
#define TM_DELAY_LONG()   do { volatile int _d = 30; while(_d--); } while(0)
#define TM_DELAY()        TM_DELAY_LONG()

/*
 * 已确认的段码表:
 * bit0=GRID1=a, bit1=GRID2=f, bit2=GRID3=b,
 * bit3=GRID4=g, bit4=GRID5=e, bit5=GRID6=c, bit6=GRID7=d
 */
const u8 tm1638_seg_table[10] = {
    0x77,  /* 0: abcdef  */
    0x24,  /* 1: bc      */
    0x5D,  /* 2: abdeg   */
    0x6D,  /* 3: abcdg   */
    0x2E,  /* 4: bcfg    */
    0x6B,  /* 5: acdfg   */
    0x7B,  /* 6: acdefg  */
    0x25,  /* 7: abc     */
    0x7F,  /* 8: abcdefg */
    0x6F,  /* 9: abcdfg  */
};

#define SEG_BLANK   0x00
#define SEG_DASH    0x08  /* 只亮g段 = bit3=GRID4=g */

/* LSB优先移出一个字节 */
static void shift_out(u8 byte)
{
    int i;
    for (i = 0; i < 8; i++) {
        gpio_set_output_value(TM_CLK_PIN, 0);
        gpio_set_output_value(TM_DIO_PIN, (byte >> i) & 1);
        TM_DELAY();
        gpio_set_output_value(TM_CLK_PIN, 1);
        TM_DELAY();
    }
}

/* 发送单个命令 */
static void send_cmd(u8 cmd)
{
    gpio_set_output_value(TM_STB_PIN, 0);
    TM_DELAY();
    shift_out(cmd);
    TM_DELAY();
    gpio_set_output_value(TM_STB_PIN, 1);
    TM_DELAY();
}

void tm1638_init(void)
{
    u8 blank[TM1638_RAM_SIZE];
    int i;

    /* 配置GPIO为输出 */
    gpio_direction_output(TM_DIO_PIN, 1);
    gpio_direction_output(TM_STB_PIN, 1);
    gpio_direction_output(TM_CLK_PIN, 1);

    /* 清屏 */
    for (i = 0; i < TM1638_RAM_SIZE; i++) {
        blank[i] = 0;
    }
    tm1638_write_all(blank);

    /* 最大亮度 */
    tm1638_set_brightness(TM1638_BRIGHTNESS_MAX, 1);
}

void tm1638_write_all(const u8 *data)
{
    int i;

    /* 自动递增模式 */
    send_cmd(0x40);
    TM_DELAY();

    /* 起始地址 + 14字节数据 */
    gpio_set_output_value(TM_STB_PIN, 0);
    TM_DELAY();
    shift_out(0xC0);
    for (i = 0; i < TM1638_RAM_SIZE; i++) {
        shift_out(data[i]);
    }
    gpio_set_output_value(TM_STB_PIN, 1);
    TM_DELAY();
}

void tm1638_set_brightness(u8 brightness, u8 on)
{
    if (brightness > TM1638_BRIGHTNESS_MAX) {
        brightness = TM1638_BRIGHTNESS_MAX;
    }
    if (on) {
        send_cmd(0x88 | brightness);
    } else {
        send_cmd(0x80);
    }
}

void tm1638_set_digit(u8 *buf, u8 pos, u8 digit)
{
    u8 segments;
    int grid;

    if (pos > 8) return;

    /* 确定段码 */
    if (digit <= 9) {
        segments = tm1638_seg_table[digit];
    } else if (digit == 0xFE) {
        segments = SEG_DASH;
    } else {
        segments = SEG_BLANK;
    }

    /*
     * 在buf中设置: 对于每个GRID, 设置对应SEG位
     * pos 0~7 → 低字节的 bit0~bit7
     * pos 8   → 高字节的 bit0
     */
    for (grid = 0; grid < TM1638_GRID_COUNT; grid++) {
        if (segments & (1 << grid)) {
            /* 该段要亮 */
            if (pos < 8) {
                buf[grid * 2] |= (1 << pos);
            } else {
                buf[grid * 2 + 1] |= (1 << (pos - 8));
            }
        } else {
            /* 该段要灭 */
            if (pos < 8) {
                buf[grid * 2] &= ~(1 << pos);
            } else {
                buf[grid * 2 + 1] &= ~(1 << (pos - 8));
            }
        }
    }
}

void tm1638_set_seg10(u8 *buf, u8 grid, u8 on)
{
    if (grid >= TM1638_GRID_COUNT) return;

    if (on) {
        buf[grid * 2 + 1] |= 0x02;  /* SEG10 = 高字节bit1 */
    } else {
        buf[grid * 2 + 1] &= ~0x02;
    }
}
