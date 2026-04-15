# 05 - BLE HID 音乐控制开发指南

## 这是整个项目最难的部分

BLE HID（蓝牙低功耗人机接口设备）是让芯片"假装"成蓝牙键盘/遥控器的技术。
手机连接后，芯片发送的"按键"信号会被系统当作真实的媒体按键处理。

**为什么这里最难：**
1. BLE HID协议栈比较复杂，涉及GATT Service、Report Map、HID Descriptor等概念
2. 杰理SDK的BLE API是私有的，和标准Nordic/ESP32的写法完全不同
3. 不同手机（iOS/Android）对HID的兼容性有差异

---

## BLE HID的工作原理（通俗版）

```
手机视角:
  "哦，有个蓝牙设备叫GPS-Speedometer，它说自己是个遥控器"
  "它注册了这些按键：播放、暂停、上一曲、下一曲、音量+、音量-"
  "它按了'下一曲'这个键 → 我要通知正在播放音乐的APP切歌"

芯片视角:
  1. 广播: "我叫GPS-Speedometer，我是HID设备"
  2. 手机连接上来，读取我的HID Report Descriptor（按键描述表）
  3. 用户按了物理按键
  4. 我发送一个HID Report（按键报告）给手机
  5. 手机操作系统处理这个按键

这就像你在遥控器上按了一个键，电视就切台了。
只不过这里的"遥控器"是我们的芯片，"电视"是手机。
```

---

## HID Report Descriptor（按键描述表）

这是告诉手机"我有哪些按键"的数据结构。
不管用什么SDK，这个描述符的内容都是一样的（USB HID标准）：

```c
/* Consumer Control HID Report Descriptor */
/* 这段数据定义了一个"多媒体遥控器"设备 */
static const uint8_t hid_report_descriptor[] = {
    0x05, 0x0C,        // Usage Page (Consumer Devices)
    0x09, 0x01,        // Usage (Consumer Control)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1 bit)
    0x95, 0x06,        //   Report Count (6 keys)
    
    0x09, 0xCD,        //   Usage (Play/Pause)
    0x09, 0xB5,        //   Usage (Next Track)
    0x09, 0xB6,        //   Usage (Previous Track)
    0x09, 0xE9,        //   Usage (Volume Up)
    0x09, 0xEA,        //   Usage (Volume Down)
    0x09, 0xE2,        //   Usage (Mute)
    
    0x81, 0x02,        //   Input (Data, Variable, Absolute)
    0x75, 0x02,        //   Report Size (2 bits) - 填充对齐到1字节
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x01,        //   Input (Constant) - padding
    0xC0               // End Collection
};
```

**解释：** 这段描述符告诉手机：
- 我是一个Consumer Control设备（多媒体遥控器）
- 我有6个按键，每个按键用1个bit表示（按下=1，释放=0）
- 6个bit + 2个bit填充 = 刚好1个字节

**发送按键的数据格式：**
```
Report ID: 0x01
Data: 1 byte (bit0=Play/Pause, bit1=Next, bit2=Prev, bit3=Vol+, bit4=Vol-, bit5=Mute)

例如: 按下 Play/Pause → 发送 [0x01, 0x01]  (bit0=1)
      释放 Play/Pause → 发送 [0x01, 0x00]  (全部释放)
      按下 Next Track → 发送 [0x01, 0x02]  (bit1=1)
```

---

## 在杰理SDK上的实现方案

### 方案A：基于SDK自带HID示例（推荐）

杰理SDK如果有 `apps/hid/` 目录，通常已经实现了BLE HID的底层。
你需要做的是：

1. **替换HID Report Descriptor** 为上面的Consumer Control描述符
2. **修改发送函数** 来发送我们的按键数据

```c
/* 杰理SDK HID发送函数的典型用法（伪代码） */

// SDK通常提供类似这样的函数:
// int ble_hid_send_report(uint8_t report_id, uint8_t *data, uint8_t len);

bool hal_ble_hid_send_key(uint8_t key_code)
{
    uint8_t report = 0;
    
    /* 把HID键码映射到report的bit位 */
    switch (key_code) {
    case HID_CONSUMER_PLAY_PAUSE:  report = 0x01; break;  // bit0
    case HID_CONSUMER_NEXT_TRACK:  report = 0x02; break;  // bit1
    case HID_CONSUMER_PREV_TRACK:  report = 0x04; break;  // bit2
    case HID_CONSUMER_VOLUME_UP:   report = 0x08; break;  // bit3
    case HID_CONSUMER_VOLUME_DOWN: report = 0x10; break;  // bit4
    case HID_CONSUMER_MUTE:        report = 0x20; break;  // bit5
    default: return false;
    }
    
    /* 发送按键按下 */
    ble_hid_send_report(0x01, &report, 1);
    
    /* 延时一小段时间 */
    hal_delay_ms(50);
    
    /* 发送按键释放（全0） */
    report = 0x00;
    ble_hid_send_report(0x01, &report, 1);
    
    return true;
}
```

### 方案B：从零配置BLE GATT（如果SDK没有HID示例）

需要手动注册BLE GATT服务：

```
BLE GATT结构:
├── GAP Service (自动)
│   ├── Device Name: "GPS-Speedometer"
│   └── Appearance: 0x03C0 (HID Generic)
├── HID Service (UUID: 0x1812)    ← 需要手动注册
│   ├── HID Information (UUID: 0x2A4A)
│   │   └── Value: [0x01, 0x01, 0x00, 0x02]  (HID 1.1, 非可启动, 远程唤醒)
│   ├── Report Map (UUID: 0x2A4B)
│   │   └── Value: 上面的 hid_report_descriptor
│   ├── Report (UUID: 0x2A4D)
│   │   └── Value: [0x00] (当前按键状态)
│   │   └── Descriptor: Report Reference [0x01, 0x01] (ID=1, Input)
│   │   └── Descriptor: CCC [0x00, 0x00] (Client Config, 手机写入启用通知)
│   └── HID Control Point (UUID: 0x2A4C)
│       └── Value: writable
├── Battery Service (UUID: 0x180F)   ← 可选但推荐
│   └── Battery Level (UUID: 0x2A19)
│       └── Value: [100] (电量百分比)
└── Device Information Service (UUID: 0x180A)  ← 可选
    ├── Manufacturer Name: "GPS-Speedometer"
    └── PnP ID
```

这个方案比较复杂，建议优先找SDK的HID示例。

---

## BLE广播配置

```c
/* BLE广播数据 */
static const uint8_t adv_data[] = {
    /* Flags */
    0x02, 0x01, 0x06,   // LE General Discoverable + BR/EDR Not Supported
    
    /* Complete Local Name */
    0x11, 0x09, 'G','P','S','-','S','p','e','e','d','o','m','e','t','e','r',0,
    
    /* Appearance: HID Gamepad */
    0x03, 0x19, 0xC0, 0x03,
    
    /* Service UUIDs: HID Service */
    0x03, 0x03, 0x12, 0x18,
};
```

---

## 兼容性注意事项

### iOS (iPhone/iPad)
- 完全支持BLE HID Consumer Control
- 连接后会在"设置-蓝牙"中显示为"已连接"
- 按键直接控制正在播放的音乐APP
- **注意：** iOS要求HID设备必须配对（加密连接），不能是Just Works

### Android
- Android 5.0+ 支持BLE HID
- 大部分手机表现良好
- 部分国产ROM可能有兼容问题，需要测试
- **注意：** 一些Android版本要求在设置中手动信任设备

### 测试方法
1. 先让芯片进入广播模式
2. 手机蓝牙搜索到 "GPS-Speedometer"
3. 点击配对/连接
4. 打开网易云/QQ音乐/酷狗，播放一首歌
5. 按下按钮，观察是否能控制播放
6. 测试所有5个按键功能

---

## 调试技巧

### 如果手机搜不到设备
- 检查广播数据是否正确
- 检查BLE是否已启动广播
- 用nRF Connect APP（免费）扫描，确认广播包内容

### 如果连接上但按键不响应
- 用nRF Connect查看GATT服务列表，确认HID Service (0x1812)存在
- 检查Report Map是否被手机正确读取
- 确认CCC Descriptor已被手机写入0x0001（启用通知）
- 检查Report发送是否成功（Notify方式）

### 推荐调试工具
- **nRF Connect** (手机APP)：查看BLE广播和GATT服务
- **Wireshark + nRF Sniffer**：抓取BLE空中包
- **杰理SDK的串口日志**：查看内部状态
