# 实施计划：GPS测速仪固件SDK集成

## 概述

将GPS测速仪业务逻辑以"插件"方式嫁接到杰理AC6323A蓝牙SDK框架。按照自底向上的顺序实现：先完成底层驱动（TM1638），再构建显示模块，然后适配HAL层和SDK板级配置，最后完成BLE HID扩展和SDK集成入口。所有GPS模块代码放置在 `fw-AC63_BT_SDK/apps/hid/gps/` 目录下。

## 任务

- [ ] 1. 创建项目目录结构和核心接口定义
  - 在 `fw-AC63_BT_SDK/apps/hid/gps/` 下创建目录结构
  - 创建 `drv_tm1638.h` 头文件，定义TM1638命令宏、错误码和所有函数原型
  - 创建 `app_display.h` 头文件，定义 `display_mode_t` 枚举和显示管理接口
  - 创建 `app_ble_hid.h` 头文件，定义 `ble_action_t` 枚举（含 BLE_ACTION_PHONE）和接口
  - 创建 `hal.h` 头文件，移除I2C/GPIO按键接口，新增TM1638和BLE电话键接口
  - 创建 `config.h`，设置 KEY_NUM=8, DISPLAY_DIGITS=9，定义8个按键ID宏
  - _需求: 1.1, 2.3, 3.1, 8.2, 9.1~9.9, 11.1_

- [ ] 2. 实现TM1638 LED驱动层
  - [ ] 2.1 实现 `drv_tm1638.c` 核心函数
    - 实现GPIO引脚初始化（PA0=DIO, PA1=STB, PA2=CLK 配置为输出）
    - 实现 `tm1638_shift_out`：LSB优先移出一个字节，CLK上升沿采样DIO
    - 实现 `tm1638_send_command`：STB↓ → shiftOut(cmd) → STB↑
    - 实现 `tm1638_write_display`：自动递增模式写入16字节（0x40→0xC0+data）
    - 实现 `tm1638_write_grid`：固定地址模式写入单个GRID（0x44→0xC0+addr+data）
    - 实现 `tm1638_set_brightness`：亮度0~7映射到0x88~0x8F，关闭为0x80
    - 实现 `tm1638_init`：发送初始化序列 0x40→0xC0+全零→0x8F
    - _需求: 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7, 1.8_

  - [ ]* 2.2 编写属性测试：shiftOut LSB优先位序
    - **Property 1: shiftOut LSB优先位序正确性**
    - Mock GPIO操作，验证对任意字节值(0x00~0xFF)输出的DIO电平序列严格按LSB优先排列
    - **验证需求: 1.2**

  - [ ]* 2.3 编写属性测试：TM1638协议写入正确性
    - **Property 2: TM1638协议写入正确性**
    - Mock GPIO操作，验证自动递增和固定地址模式产生正确的STB/数据序列
    - **验证需求: 1.4, 1.5**

  - [ ]* 2.4 编写属性测试：亮度命令映射
    - **Property 3: 亮度命令映射正确性**
    - 验证亮度0~7映射到0x88+brightness，关闭时为0x80
    - **验证需求: 1.6**

  - [ ]* 2.5 编写单元测试：TM1638初始化序列
    - 验证 `tm1638_init` 依次发送 0x40、0xC0+16字节零、0x8F
    - _需求: 1.3_

- [ ] 3. 检查点 - TM1638驱动完成
  - 确保所有测试通过，如有疑问请询问用户。

- [ ] 4. 重写显示管理模块
  - [ ] 4.1 实现 `app_display.c` 显示缓冲区和段码映射
    - 定义16字节 `display_buffer_t` 结构体和 `SEGMENT_TABLE[10]` 段码表
    - 定义 `grid_map_t` 映射表（GRID→物理数码管位置，含冒号和小数点）
    - 实现 `fill_time_display`：将时间(HH:MM:S)填入上排GRID1~GRID5，含冒号控制
    - 实现 `fill_value_display`：将数值填入下排GRID6~GRID8+，含小数点控制
    - 实现 `fill_time_invalid` 和 `fill_dash_display`：GPS无效时显示"--:--:-"和"----"
    - _需求: 2.1, 2.2, 2.3, 2.4, 2.5, 2.6_

  - [ ] 4.2 实现 `app_display.c` 显示模式和更新逻辑
    - 实现 `app_display_init`：调用 `tm1638_init`，初始化缓冲区和默认模式
    - 实现 `app_display_update`：根据当前模式格式化上排(时间)+下排(速度/里程/最高速度)，一次写入16字节
    - 实现 `app_display_set_mode` / `app_display_get_mode` / `app_display_next_mode`：模式循环切换
    - 实现 `app_display_set_colon_blink`：冒号闪烁控制（每秒切换）
    - _需求: 3.1, 3.2, 3.3, 3.4, 3.5, 3.6, 7.1, 7.2, 7.3_

  - [ ]* 4.3 编写属性测试：显示缓冲区内容正确性
    - **Property 4: 显示缓冲区时间与里程内容正确性**
    - 验证任意有效时间和里程值在缓冲区中的段码和冒号/小数点控制位正确
    - **验证需求: 2.1, 2.2, 2.4**

  - [ ]* 4.4 编写属性测试：显示模式循环切换
    - **Property 5: 显示模式循环切换**
    - 验证连续调用4次 `app_display_next_mode` 后回到原始模式
    - **验证需求: 3.2**

  - [ ]* 4.5 编写属性测试：显示模式布局正确性
    - **Property 6: 显示模式布局正确性**
    - 验证各模式下上排始终显示北京时间，下排根据模式显示对应数据
    - **验证需求: 3.3, 3.4, 3.5, 3.6**

  - [ ]* 4.6 编写属性测试：UTC到北京时间转换
    - **Property 17: UTC到北京时间转换**
    - 验证任意UTC时间转换后小时=(utc_hour+8)%24，分秒不变
    - **验证需求: 7.1**

- [ ] 5. 检查点 - 显示模块完成
  - 确保所有测试通过，如有疑问请询问用户。

- [ ] 6. NMEA解析器和测速模块属性测试
  - [ ]* 6.1 编写属性测试：GNRMC解析往返一致性
    - **Property 7: NMEA GNRMC解析往返一致性**
    - 生成有效 `nmea_gps_data_t`，格式化为$GNRMC再解析回，验证时间/速度/定位状态等价
    - **验证需求: 4.3, 15.1, 15.5**

  - [ ]* 6.2 编写属性测试：速度单位转换
    - **Property 8: 速度单位转换正确性**
    - 验证任意非负节速度转换后 speed_kmh = speed_knots × 1.852（浮点精度内）
    - **验证需求: 4.4**

  - [ ]* 6.3 编写属性测试：校验和错误拒绝
    - **Property 9: NMEA校验和错误拒绝**
    - 篡改有效GNRMC语句的校验和，验证解析器不更新GPS数据
    - **验证需求: 4.5**

  - [ ]* 6.4 编写属性测试：空字段容错
    - **Property 10: NMEA空字段容错**
    - 随机清空GNRMC语句中的某些字段，验证解析器不崩溃且非空字段正确解析
    - **验证需求: 15.4**

  - [ ]* 6.5 编写属性测试：低通滤波器输出有界性
    - **Property 11: 低通滤波器输出有界性**
    - 验证任意非负速度输入序列，滤波输出始终在历史最小值和最大值之间
    - **验证需求: 16.1, 16.3**

  - [ ]* 6.6 编写属性测试：低通滤波器恒定输入收敛
    - **Property 12: 低通滤波器恒定输入收敛性**
    - 验证恒定速度输入20次后，滤波输出收敛到该值（误差<0.01）
    - **验证需求: 16.2**

  - [ ]* 6.7 编写属性测试：低速阈值截断
    - **Property 13: 低速阈值截断**
    - 验证滤波后速度<1.0 km/h时，speed_kmh=0，is_moving=false
    - **验证需求: 5.2**

  - [ ]* 6.8 编写属性测试：最高速度跟踪不变量
    - **Property 14: 最高速度跟踪不变量**
    - 验证任意速度更新序列中 max_speed_kmh 始终等于所有滤波后速度的最大值
    - **验证需求: 5.3**

  - [ ]* 6.9 编写属性测试：里程积分正确性
    - **Property 15: 里程积分正确性**
    - 验证(速度,时间间隔)序列的累计里程等于 Σ(speed_i × dt_i / 3600000)
    - **验证需求: 6.1**

  - [ ]* 6.10 编写属性测试：里程保存/恢复往返一致性
    - **Property 16: 里程保存/恢复往返一致性**
    - Mock VM接口，验证写入后读取的里程值与原始值相等
    - **验证需求: 6.3**

- [ ] 7. 重写HAL层适配
  - [ ] 7.1 重写 `hal.h` 接口定义
    - 移除 `hal_i2c_init/write/read` 和 `hal_gpio_init/read` 接口
    - 移除 `hal_ble_process` 接口（SDK RTOS自动处理）
    - 新增 `hal_ble_hid_send_consumer_key(uint16_t key_bitfield)` 直接发送位域
    - 新增 `hal_ble_hid_send_phone_key(bool press)` 发送电话HID报告
    - 保留 UART、Flash、BLE连接状态、系统接口
    - _需求: 13.1, 13.4_

  - [ ] 7.2 重写 `hal_ac6323a.c` 实现
    - 移除I2C和GPIO按键相关代码
    - 移除 `hal_ble_process` 空实现
    - 实现 `hal_ble_hid_send_consumer_key`：直接调用SDK的 `ble_hid_data_send(1, &key_bitfield, 2)`
    - 实现 `hal_ble_hid_send_phone_key`：调用 `ble_hid_data_send(2, &phone_report, 1)` 发送Telephony报告
    - 修改UART初始化：使用UART1 PB5接收，注册 `uart1_rx_handler` 回调
    - 保留VM存储桥接和BLE连接状态通知
    - _需求: 4.1, 4.2, 10.1, 10.3, 10.4, 11.1, 13.5_

- [ ] 8. 更新配置文件 `config.h`
  - 修改 KEY_NUM 从5改为8
  - 修改 DISPLAY_DIGITS 从6改为9
  - 新增8个按键ID宏定义（KEY_ID_POWER~KEY_ID_NEXT）
  - 移除旧的5键映射宏（KEY_PLAY_PAUSE~KEY_VOL_DOWN）
  - 移除 `PLATFORM_SIM` 宏，启用 `PLATFORM_AC6323A`
  - _需求: 8.2, 14.3_

- [ ] 9. 修改BLE HID模块
  - [ ] 9.1 修改 `app_ble_hid.c/h`
    - 在 `ble_action_t` 枚举中新增 `BLE_ACTION_PHONE`
    - 实现 `app_ble_hid_send_action` 对 `BLE_ACTION_PHONE` 的处理：调用 `hal_ble_hid_send_phone_key`
    - 移除 `app_ble_hid_process` 函数（SDK自动处理）
    - 新增 `gps_ble_connection_notify(bool)` 回调函数
    - _需求: 11.1, 11.2, 11.3, 11.4, 10.5_

  - [ ]* 9.2 编写属性测试：HID键码映射正确性
    - **Property 18: HID键码映射正确性**
    - 验证每个 `ble_action_t` 枚举值映射到正确的Consumer Control位域
    - **验证需求: 10.3**

- [ ] 10. 简化主应用 `app_main.c`
  - 移除 `#include "app_key.h"` 和 `app_key_init/app_key_scan` 调用
  - 移除 `app_ble_hid_process` 调用
  - 移除按键事件回调函数（按键由SDK app_keyboard.c分发）
  - 保留 `app_main_init`：初始化 nmea_parser、app_speed、app_display、app_ble_hid、UART
  - 保留 `app_main_loop`：仅包含显示刷新逻辑（100ms间隔调用 `app_display_update`）
  - _需求: 13.1, 13.2_

- [ ] 11. 删除 `app_key.c/h`
  - 删除 `gps_speedometer/app/app_key.c` 和 `gps_speedometer/app/app_key.h`
  - 按键扫描和事件处理完全由SDK ADC按键框架替代
  - _需求: 8.1, 8.4, 8.5_

- [ ] 12. 检查点 - GPS模块代码完成
  - 确保所有测试通过，如有疑问请询问用户。

- [ ] 13. 修改SDK板级配置 `board_ac6323a_demo_cfg.h`
  - 修改 `TCFG_UART0_TX_PORT` 为 `NO_CONFIG_PORT`（释放PA0给TM1638 DIO）
  - 修改 `TCFG_ADKEY_PORT` 为 `IO_PORTA_08`
  - 修改 `TCFG_ADKEY_AD_CHANNEL` 为 `AD_CH_PA8`
  - 修改 `KEY_NUM` 为 8
  - 保持 `R_UP` 为 220（22K外部上拉）
  - 根据8键分压电阻值重新计算 `TCFG_ADKEY_ADx` 阈值
  - 新增 UART1 配置：`TCFG_UART1_ENABLE`, `TCFG_UART1_RX_PORT=IO_PORTB_05`, `TCFG_UART1_BAUDRATE=9600`
  - _需求: 14.1, 14.2, 14.3, 14.4, 14.5_

- [ ] 14. 修改SDK板级初始化 `board_ac6323a_demo.c`
  - [ ] 14.1 新增UART1平台数据结构体
    - 新增 `UART1_PLATFORM_DATA_BEGIN(uart1_data)` 结构体，配置TX=NO_CONFIG_PORT, RX=PB5, 9600bps
    - 在 `board_devices_init` 或设备注册表中注册UART1设备
    - _需求: 4.1, 14.5_

  - [ ]* 14.2 编写属性测试：ADC按键阈值计算正确性
    - **Property 20: ADC按键阈值计算正确性**
    - 验证任意分压电阻R和R_UP=22K，ADC阈值=1023×R/(R+R_UP)，相邻按键中点为判定边界
    - **验证需求: 8.3**

- [ ] 15. 修改SDK `app_keyboard.c` 集成入口
  - [ ] 15.1 挂载GPS初始化和定时回调
    - 在 `hidkey_app_start()` 末尾添加 `#include "gps/app_main.h"`
    - 调用 `app_main_init()` 初始化GPS模块
    - 调用 `sys_timer_add(NULL, gps_timer_callback, 100)` 注册100ms定时回调
    - 定时回调中调用 `app_main_loop()`
    - _需求: 13.1, 13.2_

  - [ ] 15.2 重写按键分发逻辑
    - 替换 `hidkey_app_key_deal_test` 中的按键映射表为8键版本
    - 短按MODE → `app_display_next_mode()`
    - 短按PHONE → `hal_ble_hid_send_phone_key(true)` + `hal_ble_hid_send_phone_key(false)`
    - 短按PLAY/PREV/NEXT/VOL± → 通过 `KEY_CLICK_TO_HID[]` 查表发送Consumer Control
    - 长按PREV → `app_speed_reset_mileage()`
    - 长按NEXT → `app_speed_reset_max_speed()`
    - 长按POWER → `hidkey_power_event_to_user(POWER_EVENT_POWER_SOFTOFF)`
    - 持续按住VOL± → 通过 `KEY_HOLD_TO_HID[]` 查表持续发送音量键码
    - _需求: 9.1~9.9, 12.1_

  - [ ] 15.3 添加BLE状态通知
    - 在 `hidkey_bt_connction_status_event_handler` 的连接成功分支调用 `gps_ble_connection_notify(true)`
    - 在断开分支调用 `gps_ble_connection_notify(false)`
    - _需求: 13.4_

  - [ ] 15.4 扩展HID Report Map
    - 在 `hidkey_report_map[]` 末尾追加 Telephony Usage Page (0x0B) 的 Report ID 2 集合
    - 包含 Hook Switch (0x20) 和 Phone Mute (0x2F) 用法，2 bit数据 + 6 bit填充
    - 同步更新 `hidkey_edr_config` 和 `hidkey_ble_config` 中的 `report_map_size`
    - _需求: 11.2, 11.3_

  - [ ]* 15.5 编写属性测试：按键事件分发正确性
    - **Property 19: 按键事件分发正确性**
    - 验证任意key_value(0~7)和event_type的分发逻辑正确
    - **验证需求: 13.3**

- [ ] 16. 检查点 - SDK集成完成
  - 确保所有测试通过，如有疑问请询问用户。

- [ ] 17. 最终集成与连线验证
  - [ ] 17.1 将GPS模块文件从 `gps_speedometer/` 复制到 `fw-AC63_BT_SDK/apps/hid/gps/`
    - 复制不需修改的文件：`nmea_parser.c/h`、`app_speed.c/h`
    - 复制已修改的文件：`drv_tm1638.c/h`、`app_display.c/h`、`app_ble_hid.c/h`、`app_main.c/h`、`hal.h`、`hal_ac6323a.c`、`config.h`
    - 确保所有 `#include` 路径正确适配新目录结构
    - _需求: 13.1_

  - [ ]* 17.2 编写集成测试：模块间数据流验证
    - 测试 UART接收 → NMEA解析 → 速度更新 → 显示刷新 的完整数据流
    - 测试 按键事件 → HID发送 的完整流程
    - 测试 BLE连接/断开 → 状态通知 的回调链
    - _需求: 13.1, 13.2, 13.3, 13.4_

- [ ] 18. 最终检查点 - 全部完成
  - 确保所有测试通过，如有疑问请询问用户。

## 备注

- 标记 `*` 的任务为可选任务，可跳过以加快MVP进度
- 每个任务引用了具体的需求编号，确保可追溯性
- 属性测试使用 [theft](https://github.com/silentbicycle/theft) C语言属性测试库，每个属性至少运行100次迭代
- 单元测试使用 Unity 或 CMocka 框架，在PC平台编译运行
- GRID→物理数码管的映射为推测值，需在实际硬件上逐段点亮测试后更新 `grid_map_t`
- ADC按键阈值需烧测试固件实测后填入
