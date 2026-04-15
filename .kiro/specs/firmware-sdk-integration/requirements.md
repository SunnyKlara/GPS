# 需求文档：GPS测速仪固件SDK集成

## 简介

本项目将已有的GPS测速仪业务逻辑代码（NMEA解析、测速计算、显示逻辑、BLE HID）集成到杰理AC6323A蓝牙SDK框架中。集成过程需要根据实际硬件原理图重写LED驱动（从I2C改为TM1638三线串行协议）、适配SDK的ADC按键框架（8键）、对接SDK的UART驱动接收GPS数据、桥接BLE HID发送到SDK蓝牙协议栈，并新增电话接听/挂断功能。

## 术语表

- **SDK**: 杰理AC63系列蓝牙SDK（fw-AC63_BT_SDK），提供RTOS调度、蓝牙协议栈、按键驱动、UART驱动等基础框架
- **GPS_Module**: AT6558R-5N32 GPS/BDS双模定位芯片，通过UART输出NMEA-0183协议数据
- **LED_Driver**: CM4718（TM1638兼容）LED驱动芯片，通过三线串行接口（DIO/CLK/STB）驱动70颗LED
- **TM1638_Protocol**: 三线串行通信协议，使用DIO（数据）、CLK（时钟）、STB（片选）三根信号线，CLK上升沿采样DIO，LSB先发
- **Display_Manager**: 显示管理模块，负责将速度、里程、时间等数据格式化并写入LED_Driver的显示RAM
- **NMEA_Parser**: NMEA-0183协议解析器，从GPS串口数据中提取时间、速度、定位状态等信息
- **Speed_Calculator**: 测速与里程计算模块，对GPS原始速度进行低通滤波并通过速度积分计算累计里程
- **BLE_HID_Controller**: BLE HID控制模块，通过Consumer Control协议向手机发送媒体控制和电话控制键码
- **ADC_Key_Driver**: SDK内置的ADC按键驱动，通过单根ADC引脚（PA8）的电阻分压值识别8个不同按键
- **Display_Buffer**: 16字节显示RAM缓冲区，每个GRID占2字节（低8位对应SEG1~SEG8，高2位对应SEG9~SEG10）
- **Consumer_Control**: USB HID Consumer Control Usage Page（0x0C），定义媒体播放、音量等控制键码
- **Telephony_HID**: USB HID Telephony Usage Page（0x0B），定义电话接听/挂断等控制键码
- **VM_System**: SDK内置的虚拟内存存储系统（syscfg_read/syscfg_write），用于掉电数据保存
- **Segment_Map**: 7段数码管的段码映射表，定义每个数字（0~9）对应的LED段（a~g）点亮组合
- **GRID**: LED_Driver的位选信号线（GRID1~GRID7），每条GRID对应一位数码管
- **SEG**: LED_Driver的段选信号线（SEG0~SEG10），每条SEG对应数码管的一个LED段

## 需求

### 需求1：TM1638 LED驱动层实现

**用户故事：** 作为固件开发者，我希望实现CM4718/TM1638兼容芯片的三线串行驱动，以便通过GPIO位操作控制70颗LED的显示。

#### 验收标准

1. THE LED_Driver SHALL通过三线串行接口（PA0=DIO, PA1=STB, PA2=CLK）与CM4718芯片通信
2. THE LED_Driver SHALL在CLK上升沿采样DIO数据，且数据以LSB优先顺序发送
3. WHEN LED_Driver初始化时，THE LED_Driver SHALL依次发送数据命令（0x40）、地址命令（0xC0）和显示控制命令（0x8F）以开启显示
4. THE LED_Driver SHALL支持自动递增地址模式（命令0x40），一次写入16字节数据覆盖全部显示RAM
5. THE LED_Driver SHALL支持固定地址模式（命令0x44），单独更新指定GRID的显示数据
6. THE LED_Driver SHALL支持8级亮度调节（命令0x88~0x8F）
7. WHEN STB信号拉低时，THE LED_Driver SHALL开始一次通信事务；WHEN STB信号拉高时，THE LED_Driver SHALL结束该通信事务
8. IF DIO、CLK或STB引脚初始化失败，THEN THE LED_Driver SHALL返回错误码且不执行后续通信操作

### 需求2：LED显示布局与段码映射

**用户故事：** 作为用户，我希望LED显示屏同时显示时间和里程信息，以便在骑行中一目了然地获取关键数据。

#### 验收标准

1. THE Display_Manager SHALL驱动上排5位数码管（GRID1~GRID5）显示时间，格式为HH:MM:S（时十位、时个位、冒号、分十位、分个位、冒号、秒个位）
2. THE Display_Manager SHALL驱动下排4位数码管显示里程，格式为XX:XX.（千位、百位、冒号、十位、个位、小数点）
3. THE Display_Manager SHALL维护一张Segment_Map，将数字0~9映射为对应的7段LED点亮模式
4. THE Display_Manager SHALL控制3个冒号指示灯（上排2个、下排1个）和1个小数点指示灯的独立开关
5. WHEN Display_Manager刷新显示时，THE Display_Manager SHALL将完整的16字节Display_Buffer一次性写入LED_Driver的显示RAM
6. THE Display_Manager SHALL在每次刷新时同时更新上排和下排的显示内容，刷新间隔为100ms

### 需求3：显示模式切换

**用户故事：** 作为用户，我希望通过MODE按键在不同显示内容之间切换，以便查看速度、里程、时间或最高速度。

#### 验收标准

1. THE Display_Manager SHALL支持4种显示模式：速度模式、里程模式、时间模式、最高速度模式
2. WHEN 用户短按MODE键时，THE Display_Manager SHALL按照"速度→里程→时间→最高速度→速度"的顺序循环切换显示模式
3. WHILE 处于速度模式时，THE Display_Manager SHALL在上排显示当前时间（HH:MM:S），在下排显示当前速度（km/h，精度0.1）
4. WHILE 处于里程模式时，THE Display_Manager SHALL在上排显示当前时间（HH:MM:S），在下排显示累计里程（km，精度0.01）
5. WHILE 处于时间模式时，THE Display_Manager SHALL在上排显示当前时间（HH:MM:S），在下排显示当前日期或速度
6. WHILE 处于最高速度模式时，THE Display_Manager SHALL在上排显示当前时间（HH:MM:S），在下排显示本次最高速度（km/h，精度0.1）

### 需求4：GPS UART数据接收与NMEA解析

**用户故事：** 作为固件开发者，我希望通过SDK的UART驱动接收GPS模块的NMEA数据，以便解析出定位、速度和时间信息。

#### 验收标准

1. THE SDK SHALL配置UART1以9600bps波特率、8N1格式在PB5（RX）引脚接收GPS_Module输出的NMEA数据
2. WHEN UART1接收到字节数据时，THE SDK SHALL通过中断回调将每个字节逐一传递给NMEA_Parser的feed接口
3. THE NMEA_Parser SHALL解析$GNRMC语句，提取UTC时间（时、分、秒）、定位状态（有效/无效）、速度（节）和日期
4. THE NMEA_Parser SHALL将速度从节（knots）转换为千米每小时（km/h），转换系数为1.852
5. IF NMEA_Parser接收到校验和错误的语句，THEN THE NMEA_Parser SHALL丢弃该语句且不更新GPS数据
6. IF GPS_Module连续5秒未输出有效定位数据，THEN THE NMEA_Parser SHALL将定位状态标记为无效

### 需求5：GPS测速与低通滤波

**用户故事：** 作为用户，我希望看到平滑稳定的速度显示，而不是GPS原始数据的跳动值。

#### 验收标准

1. WHEN NMEA_Parser输出新的有效速度值时，THE Speed_Calculator SHALL使用一阶低通滤波器处理原始速度，滤波系数为0.3
2. WHILE GPS定位有效且滤波后速度低于1.0 km/h时，THE Speed_Calculator SHALL将显示速度设为0，判定为静止状态
3. THE Speed_Calculator SHALL记录本次运行的最高速度值
4. WHEN 用户长按NEXT键时，THE Speed_Calculator SHALL将最高速度值重置为0
5. IF GPS定位无效，THEN THE Display_Manager SHALL在速度显示区域显示"---"表示无数据

### 需求6：里程累计与掉电保存

**用户故事：** 作为用户，我希望累计里程在断电后不丢失，并且可以手动清零重新计算。

#### 验收标准

1. WHILE GPS定位有效且速度大于1.0 km/h时，THE Speed_Calculator SHALL通过速度对时间的积分累加里程值，精度为0.01 km
2. THE Speed_Calculator SHALL每30秒通过VM_System将当前累计里程写入Flash存储
3. WHEN 系统启动时，THE Speed_Calculator SHALL从VM_System读取上次保存的累计里程值并恢复
4. WHEN 用户长按PREV键时，THE Speed_Calculator SHALL将累计里程重置为0并立即写入VM_System
5. IF VM_System读取失败（首次使用或数据损坏），THEN THE Speed_Calculator SHALL将累计里程初始化为0

### 需求7：GPS时间转换为北京时间

**用户故事：** 作为中国用户，我希望显示屏直接显示北京时间，而不是UTC时间。

#### 验收标准

1. WHEN NMEA_Parser解析出有效的UTC时间时，THE Display_Manager SHALL将UTC时间加8小时转换为北京时间（UTC+8）
2. WHEN UTC时间加8小时后超过24:00时，THE Display_Manager SHALL正确处理日期进位（小时取模24）
3. IF GPS定位无效，THEN THE Display_Manager SHALL在时间显示区域显示上一次有效的时间值或"--:--:-"

### 需求8：ADC按键输入适配

**用户故事：** 作为固件开发者，我希望利用SDK的ADC按键框架驱动8个物理按键，以便实现各种控制功能。

#### 验收标准

1. THE ADC_Key_Driver SHALL配置PA8引脚为ADC输入，使用AD_CH_PA8通道，外部22K上拉电阻
2. THE ADC_Key_Driver SHALL识别8个按键：POWER、PLAY、VOL-、VOL+、电话、MODE、PREV、NEXT
3. THE ADC_Key_Driver SHALL为每个按键配置正确的ADC电压阈值，阈值基于各按键分压电阻与22K上拉电阻的分压比计算
4. THE ADC_Key_Driver SHALL支持短按（KEY_EVENT_CLICK）、长按（KEY_EVENT_LONG）、持续按住（KEY_EVENT_HOLD）和松开（KEY_EVENT_UP）四种事件类型
5. THE SDK SHALL将按键事件通过SYS_KEY_EVENT消息分发到应用层的按键处理函数

### 需求9：按键功能映射

**用户故事：** 作为用户，我希望每个按键都有明确的短按和长按功能，以便方便地控制音乐播放和测速仪功能。

#### 验收标准

1. WHEN 用户短按PLAY键时，THE BLE_HID_Controller SHALL发送Consumer Control播放/暂停键码（0xCD）
2. WHEN 用户短按PREV键时，THE BLE_HID_Controller SHALL发送Consumer Control上一曲键码（0xB6）
3. WHEN 用户短按NEXT键时，THE BLE_HID_Controller SHALL发送Consumer Control下一曲键码（0xB5）
4. WHEN 用户短按VOL+键时，THE BLE_HID_Controller SHALL发送Consumer Control音量增加键码（0xE9）
5. WHEN 用户短按VOL-键时，THE BLE_HID_Controller SHALL发送Consumer Control音量减少键码（0xEA）
6. WHEN 用户持续按住VOL+或VOL-键时，THE BLE_HID_Controller SHALL以SDK默认的HOLD重复间隔持续发送对应的音量键码
7. WHEN 用户短按MODE键时，THE Display_Manager SHALL切换到下一个显示模式
8. WHEN 用户长按PREV键时，THE Speed_Calculator SHALL清零累计里程
9. WHEN 用户长按NEXT键时，THE Speed_Calculator SHALL清零最高速度

### 需求10：BLE HID媒体控制

**用户故事：** 作为用户，我希望通过蓝牙连接手机后，按键可以控制手机的音乐播放，无需安装任何APP。

#### 验收标准

1. THE BLE_HID_Controller SHALL使用SDK蓝牙协议栈的BLE HOGP（HID Over GATT Profile）发送HID报告
2. THE BLE_HID_Controller SHALL使用Consumer Control Report Map，包含音量增减、播放/暂停、上一曲、下一曲、静音、快进、快退共8个Usage
3. WHEN 用户按下HID功能按键时，THE BLE_HID_Controller SHALL通过ble_hid_data_send接口发送对应的2字节Consumer Control报告
4. WHEN 用户松开HID功能按键时，THE BLE_HID_Controller SHALL发送全零的2字节报告表示按键释放
5. IF BLE未连接手机，THEN THE BLE_HID_Controller SHALL丢弃按键事件且不尝试发送HID报告

### 需求11：BLE HID电话接听/挂断

**用户故事：** 作为用户，我希望在来电时通过按键接听或挂断电话，方便骑行中的通话操作。

#### 验收标准

1. WHEN 用户短按电话键时，THE BLE_HID_Controller SHALL发送电话接听/挂断的HID键码
2. THE BLE_HID_Controller SHALL在HID Report Map中包含Telephony Usage Page（0x0B）的Hook Switch用法，或使用Consumer Control的等效键码
3. THE BLE_HID_Controller SHALL确保电话控制功能同时兼容iOS和Android系统
4. IF BLE未连接手机，THEN THE BLE_HID_Controller SHALL忽略电话键按下事件

### 需求12：电源管理

**用户故事：** 作为用户，我希望通过POWER键控制设备开关机，以便节省电量。

#### 验收标准

1. WHEN 用户长按POWER键时，THE SDK SHALL触发软关机流程，依次断开蓝牙连接、关闭GPS_Module、关闭LED_Driver、进入低功耗模式
2. WHEN 设备处于关机状态且用户按下POWER键时，THE SDK SHALL唤醒系统并执行完整的初始化流程
3. WHEN 系统启动时，THE SDK SHALL依次初始化蓝牙协议栈、GPS UART接收、LED_Driver和ADC按键驱动

### 需求13：SDK集成架构

**用户故事：** 作为固件开发者，我希望GPS测速仪代码以插件方式挂载到SDK框架上，以便保持SDK主体代码的最小改动。

#### 验收标准

1. THE SDK SHALL在app_keyboard.c的hidkey_app_start函数末尾调用GPS模块的初始化函数（app_main_init）
2. THE SDK SHALL通过sys_timer_add注册一个100ms周期的定时回调，在回调中调用GPS模块的主循环函数（app_main_loop）
3. THE SDK SHALL在按键事件处理函数中，根据按键值和事件类型分发到GPS功能处理或BLE HID发送
4. THE SDK SHALL在BLE连接状态变化时（BLE_ST_CONNECT/BLE_ST_DISCONN），通知GPS模块更新BLE连接状态
5. THE SDK SHALL禁用UART0的PA0发送引脚配置（改为NO_CONFIG_PORT），避免与LED_Driver的DIO信号冲突

### 需求14：SDK板级配置适配

**用户故事：** 作为固件开发者，我希望board_ac6323a_demo_cfg.h中的硬件配置与实际原理图一致，以便所有外设正常工作。

#### 验收标准

1. THE SDK SHALL将TCFG_ADKEY_PORT配置为IO_PORTA_08，TCFG_ADKEY_AD_CHANNEL配置为AD_CH_PA8
2. THE SDK SHALL将R_UP配置为220（代表22K外部上拉电阻）
3. THE SDK SHALL将KEY_NUM配置为8，支持8个ADC按键
4. THE SDK SHALL将TCFG_UART0_TX_PORT配置为NO_CONFIG_PORT，释放PA0给LED_Driver使用
5. THE SDK SHALL新增UART1配置：TCFG_UART1_ENABLE为ENABLE，RX引脚为IO_PORTB_05，波特率为9600

### 需求15：NMEA解析器正确性

**用户故事：** 作为固件开发者，我希望NMEA解析器能正确处理各种格式的NMEA语句，以便GPS数据的可靠提取。

#### 验收标准

1. THE NMEA_Parser SHALL解析符合NMEA-0183标准的$GNRMC语句，提取所有字段（时间、定位状态、纬度、经度、速度、航向、日期）
2. THE NMEA_Parser SHALL正确计算并验证NMEA语句的XOR校验和（'$'与'*'之间所有字符的异或值）
3. IF NMEA语句缺少起始符'$'或结束符'*'，THEN THE NMEA_Parser SHALL丢弃该语句
4. THE NMEA_Parser SHALL处理空字段（连续逗号），将对应数据字段保持为上一次有效值或默认值
5. FOR ALL 有效的nmea_gps_data_t结构体，将其格式化为$GNRMC语句再解析回nmea_gps_data_t后，时间、速度和定位状态字段SHALL与原始值等价（往返一致性）

### 需求16：低通滤波器正确性

**用户故事：** 作为固件开发者，我希望低通滤波器的数学特性可验证，以便确保速度显示的平滑性和准确性。

#### 验收标准

1. THE Speed_Calculator的低通滤波器SHALL满足：对于任意输入序列，滤波后的输出值始终介于历史最小输入值和历史最大输入值之间
2. THE Speed_Calculator的低通滤波器SHALL满足幂等性：对已经稳定的恒定速度输入，连续多次滤波后输出值SHALL收敛到该恒定值
3. THE Speed_Calculator SHALL确保滤波后速度值始终为非负数

