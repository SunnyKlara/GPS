# GPS UART 调试 - 新对话提示词

## 项目背景
基于杰理AC6323A蓝牙芯片 + AT6558R GPS模块 + CM4718 LED驱动的GPS测速仪。
代码在 `fw-AC63_BT_SDK/apps/hid/examples/keyboard/` 目录下。
SDK是杰理AC63系列BT SDK，编译用Code::Blocks，芯片是bd19平台。
**C89兼容！不能用C99语法（块内变量声明会导致芯片不启动）**

## 已完成且正常工作的功能
- TM1638数码管显示驱动（PA0=DIO, PA1=STB, PA2=CLK）
- ADC 8按键识别（PA8，已标定并修正映射）
- 蓝牙EDR模式 HID控制手机音乐（音量加减、上下曲、播放暂停）
- HFP电话控制（MODE键：来电接听/通话挂断/无电话暂停音乐，双击回拨来电号码）
- 蓝牙连接指示灯（小数点LED闪烁/常亮）
- 里程VM保存

## 当前问题：GPS UART 收不到有效 NMEA 数据

### 已确认的事实
1. **GPS芯片在工作** — GPIO电平检测确认PB5有数据输出（high/low都在变化）
2. **UART硬件能收到字节** — rx_total持续增长，说明uart_dev_open成功，数据在进来
3. **9600波特率下收到了少量'$'字符** — dollar_count=2（最后一次测试），说明波特率可能是对的
4. **但ASCII可打印字符占比极低（约0%）** — 绝大部分字节不在32-126范围
5. **NMEA解析器从未成功解析出一条完整语句** — nmea_ok_count始终为0

### 最可能的原因
AT6558R可能同时输出NMEA文本和CASIC二进制协议。二进制数据淹没了NMEA文本，导致：
- 偶尔能收到'$'（NMEA帧头）
- 但紧接着的二进制数据破坏了NMEA帧，解析器无法完成解析

### 需要做的事
1. **发送AT命令配置AT6558R只输出NMEA** — 通过PB4(TX)发送CASIC配置命令，关闭二进制输出
2. **或者修改NMEA解析器** — 增强容错能力，能在二进制数据中找到并提取NMEA语句
3. **确认波特率** — 虽然9600下收到了'$'，但需要进一步确认

### AT6558R配置命令参考
AT6558R使用CASIC协议配置，帧格式：
```
0xBA 0xCE [class] [id] [payload_len_lo] [payload_len_hi] [payload...] [ckA] [ckB]
```
关闭二进制输出、只保留NMEA的配置命令需要查AT6558R数据手册。

## 关键文件
- `fw-AC63_BT_SDK/apps/hid/examples/keyboard/gps/gps_uart.c` — GPS UART接收（当前是诊断模式）
- `fw-AC63_BT_SDK/apps/hid/examples/keyboard/gps/nmea_parser.c` — NMEA解析器
- `fw-AC63_BT_SDK/apps/hid/examples/keyboard/gps/gps_main.c` — 主模块，按键处理，HID发送
- `fw-AC63_BT_SDK/apps/hid/examples/keyboard/app_keyboard.c` — SDK集成入口
- `fw-AC63_BT_SDK/apps/hid/board/bd19/board_ac6323a_demo_cfg.h` — 板级配置
- `docs/11_原理图分析.md` — 硬件引脚分配

## GPS硬件连接
- AC6323A PB4(TX) → 220R → AT6558R RX（可以发配置命令）
- AC6323A PB5(RX) → 220R → AT6558R TX（接收NMEA数据）
- AC6323A PB6(TX1) → 220R → AT6558R RX1（辅助串口）
- AC6323A PB7(RX1) → 220R → AT6558R TX1（辅助串口）
- AC6323A PA7 → AT6558R ON_OFF（高电平=正常工作）
- AT6558R有32.768K RTC晶振（之前不起振，现已修复）

## gps_uart.c 当前状态
- `gps_uart_init()`: 打开UART 9600 RX=PB5, TX设为无效值(-1)
- `gps_uart_loop()`: 读取UART数据，统计rx_total/dollar_count/ascii_count，喂给nmea_parser
- `gps_uart_hw_open()`: 调用SDK的uart_dev_open，TX pin设为(u8)-1避免占用引脚
- 注意：uart_dev_close后重新uart_dev_open会失败，不能动态切换波特率

## 编译烧录流程
1. Code::Blocks打开 `fw-AC63_BT_SDK/apps/hid/board/bd19/AC632N_hid.cbp`
2. Build → Rebuild
3. 运行 `fw-AC63_BT_SDK/cpu/bd19/tools/download.bat` 烧录

## 重要注意事项
- C89兼容！所有变量必须在函数/块的开头声明
- **绝对不能用os_time_dly()** — 会阻塞app任务导致按键失效
- Report Map修改后必须在手机上删除配对重新配对
- 修改代码后必须在Code::Blocks里Rebuild
- 当前蓝牙模式是EDR（不是BLE），因为HFP电话控制需要EDR

## ADC按键映射（实测确认）
| key_value | 物理按键 |
|-----------|---------|
| 0 | NEXT |
| 1 | PREV |
| 2 | MODE |
| 3 | 电话 |
| 4 | PLAY（注意：物理VOL+和PLAY的value互换了）|
| 5 | VOL- |
| 6 | VOL+（注意：实际返回6不是4）|
| 7 | POWER |

## 请求
1. 先读取 gps_uart.c 和 nmea_parser.c 的当前代码
2. 分析为什么9600波特率下能收到少量'$'但NMEA解析失败
3. 提出解决方案（配置AT6558R只输出NMEA / 增强解析器容错 / 其他）
4. 实施修改，每次只改一个点，确认后再改下一个
5. **不要动 app_keyboard.c 和 gps_main.c** — 按键和电话功能已经正常，不要碰
