#ifndef _GPS_UART_H_
#define _GPS_UART_H_

#include "typedef.h"

void gps_uart_init(void);
void gps_uart_loop(void);
void gps_uart_show_next(void);  /* 按键显示下一组原始数据 */

#endif
