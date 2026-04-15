# GPS测速仪固件项目

## 芯片组
- AC6323A (杰理) - 主控 + BLE
- AT6558A (中科微) - GPS/BDS定位
- TM5020A (天马微电子) - LED驱动

## 项目结构
```
gps_speedometer/
├── app/                    # 应用层（业务逻辑）
│   ├── app_main.c/h       # 主应用入口和任务调度
│   ├── app_speed.c/h      # 测速与里程计算
│   ├── app_display.c/h    # 显示管理
│   ├── app_ble_hid.c/h    # BLE HID音乐控制
│   └── app_key.c/h        # 按键处理
├── driver/                 # 驱动层（硬件抽象HAL）
│   ├── drv_gps.c/h        # GPS UART驱动 + NMEA解析
│   ├── drv_led.c/h        # TM5020A LED驱动
│   ├── drv_ble.c/h        # BLE HID驱动抽象
│   ├── drv_key.c/h        # GPIO按键驱动
│   └── drv_flash.c/h      # Flash存储驱动
├── lib/                    # 通用库
│   └── nmea_parser.c/h    # NMEA协议解析器
├── platform/               # 平台层
│   ├── hal.h               # 硬件抽象层接口定义
│   ├── platform_ac6323a/   # 杰理真机平台（后续接入SDK）
│   └── platform_sim/       # PC模拟平台（开发调试用）
│       ├── hal_sim.c       # 模拟HAL实现
│       └── sim_main.c      # PC模拟入口
├── config/
│   └── config.h            # 全局配置
└── Makefile                # 构建脚本
```

## 开发策略
1. **Phase 1**: PC模拟环境搭建，跑通NMEA解析+速度计算+里程累计
2. **Phase 2**: 实现BLE HID协议栈模拟
3. **Phase 3**: 获取杰理SDK后，替换HAL层适配真机
4. **Phase 4**: 硬件联调

## 编译运行（PC模拟）
```bash
make sim
./build/gps_sim
```
