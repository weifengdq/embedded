# HPM5321 MCAN0 ECHO 测试实现

## 概述
基于官方 mcan_demo.c 例子，在 main.c 中实现了 MCAN0 的配置和 ECHO 测试功能。

## 实现的功能

### 1. MCAN0 配置
- **仲裁阶段**: 1Mbps
- **数据阶段**: 5Mbps  
- **TDC**: 开启传输延迟补偿 (Transmitter Delay Compensation)
- **滤波器**: 0:0 掩码方式，接收所有消息
- **模式**: 正常模式，支持 CANFD

### 2. 测试的报文类型
- **CAN2.0 标准帧数据帧** (11位ID)
- **CAN2.0 扩展帧数据帧** (29位ID)
- **CANFD 标准帧数据帧** (64字节数据)
- **CANFD+BRS 扩展帧数据帧** (64字节数据，位速率切换)

### 3. 主要功能
- 循环发送不同类型的CAN报文
- 接收并显示回环的报文
- LED状态指示
- 错误处理和检测
- 详细的调试信息输出

## 文件结构
```
hpm5321_mcan0/app/
├── src/
│   └── main.c                 # 主程序文件
├── CMakeLists.txt            # 构建配置
├── app.yaml                  # SDK依赖配置
└── win.ps1                   # 构建脚本
```

## 关键配置参数

### 时序配置
```c
// 仲裁阶段: 1Mbps
can_config.can_timing.prescaler = 10;
can_config.can_timing.num_seg1 = 15;
can_config.can_timing.num_seg2 = 4;
can_config.can_timing.num_sjw = 2;

// 数据阶段: 5Mbps  
can_config.canfd_timing.prescaler = 5;
can_config.canfd_timing.num_seg1 = 6;
can_config.canfd_timing.num_seg2 = 1;
can_config.canfd_timing.num_sjw = 1;
```

### TDC配置
```c
can_config.canfd_timing.enable_tdc = true;
can_config.enable_tdc = true;
can_config.tdc_config.ssp_offset = can_config.canfd_timing.num_seg1 + 1;
can_config.tdc_config.filter_window_length = can_config.tdc_config.ssp_offset;
```

### 滤波器配置
```c
// 禁用特定滤波器，接收所有消息
// TODO: Fix
can_config.all_filters_config.std_id_filter_list.mcan_filter_elem_count = 0;
can_config.all_filters_config.ext_id_filter_list.mcan_filter_elem_count = 0;
```

## 使用方法

1. **构建项目**:
   ```powershell
   .\win.ps1
   ```

2. **连接硬件**:
   - 连接 MCAN0 引脚 (PA00=TXD, PA01=RXD)
   - 连接到 CAN 收发器
   - 连接到 CAN 总线或回环测试器

3. **运行程序**:
   - 下载固件到 HPM5321EVK
   - 通过串口查看测试输出
   - 观察 LED 状态指示

## 测试输出示例
```
*****************************************************
*                                                   *
*          HPM5321 MCAN0 ECHO Test Demo             *
*                                                   *
*  Features:                                        *
*  - MCAN0: 1Mbps + 5Mbps with TDC                 *
*  - Filter: 0:0 mask (accept all messages)        *
*  - Test: CAN/CANFD/CANFD+BRS frames               *
*  - Test: Standard/Extended ID                     *
*  - Test: Data/Remote frames                       *
*                                                   *
*****************************************************

MCAN0 initialized successfully
  Arbitration phase: 1Mbps
  Data phase: 5Mbps
  TDC: Enabled
  Filter: Accept all messages (0:0 mask)

Test 1: Sending CAN2.0 Standard Data Frame (ID=0x123)
CAN message sent successfully
Received message: StdID=0x123 DLC=8 CAN2.0 Data: 00 11 22 33 44 55 66 77
```

## 注意事项

1. **时钟配置**: 假设系统时钟为 200MHz
2. **引脚配置**: 已在 pinmux.c 中配置 MCAN0 引脚
3. **回环测试**: 需要外部 CAN 收发器支持
4. **错误处理**: 包含基本的错误检测和恢复机制

## 技术特点

- 基于官方 SDK 的标准实现
- 支持完整的 CANFD 功能
- 开启 TDC 提高高速数据传输可靠性
- 灵活的滤波器配置
- 详细的调试信息输出
- 循环测试多种报文类型
