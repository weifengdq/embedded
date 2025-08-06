# HPM5321 USB CDC ACM 虚拟串口 Echo 测试

本项目实现了基于HPM5321微控制器的USB CDC ACM虚拟串口功能，支持echo回显测试。

## 功能特性

- USB CDC ACM虚拟串口设备
- 自动echo回显接收到的数据
- DTR状态检测
- LED状态指示
- 串口调试输出

## 硬件连接

### USB连接
- PA24: USB0_DM (USB D-)
- PA25: USB0_DP (USB D+)

### LED指示
- PA10: 状态LED (闪烁表示系统运行)

### 调试串口
- PY00: UART0_TXD (调试输出)
- PY01: UART0_RXD (调试输入)

## 使用方法

### 1. 编译和下载
```bash
# 进入项目目录
cd hpm5321_usb_cdc_acm_0

# 编译项目
.\win.ps1 -a build

# 下载到开发板
.\win.ps1 -a flash
```

### 2. 测试步骤

1. **连接硬件**
   - 使用USB线连接开发板USB口到PC
   - 连接调试串口到PC (波特率115200)

2. **观察启动过程**
   - 打开串口调试工具，观察启动信息
   - LED应该开始闪烁

3. **虚拟串口测试**
   - 在PC设备管理器中找到新的COM端口
   - 使用串口工具连接该COM端口
   - 发送任意数据，应该收到相同的回显数据

### 3. 测试示例

在虚拟COM端口中发送：
```
Hello HPM5321!
```

应该收到回显：
```
HPM5321 USB CDC ACM Echo Test Ready!
Hello HPM5321!
```

## 代码结构

### 主要文件

- `src/main.c` - 主程序，初始化系统和USB
- `src/cdc_acm.c` - CDC ACM实现，处理USB通信和echo功能
- `src/cdc_acm.h` - CDC ACM头文件
- `src/usb_config.h` - USB配置文件
- `hpm4canfd/pinmux.c` - 引脚配置，包含USB引脚
- `hpm4canfd/board.c` - 板级支持包，包含USB初始化

### 关键功能

1. **USB初始化**
   ```c
   board_init_usb((USB_Type *)CONFIG_HPM_USBD_BASE);
   cdc_acm_init(0, CONFIG_HPM_USBD_BASE);
   ```

2. **Echo处理**
   ```c
   void usbd_cdc_acm_bulk_out(uint8_t busid, uint8_t ep, uint32_t nbytes)
   {
       // 自动将接收到的数据发送回去
       usbd_ep_start_write(busid, CDC_IN_EP, &read_buffer[index][0], nbytes);
   }
   ```

3. **DTR状态检测**
   ```c
   bool cdc_acm_is_dtr_enabled(void)
   {
       return dtr_enable;
   }
   ```

## 调试信息

通过调试串口可以看到以下信息：

- 系统启动信息
- USB CDC ACM初始化状态
- DTR连接/断开状态
- 数据传输日志

## 故障排除

### 常见问题

1. **PC无法识别USB设备**
   - 检查USB线连接
   - 确认PA24/PA25引脚配置正确
   - 检查USB时钟是否正确配置

2. **虚拟串口无数据**
   - 确认DTR状态已启用
   - 检查串口工具的DTR设置
   - 确认波特率设置正确

3. **echo数据异常**
   - 检查USB buffer大小设置
   - 确认数据对齐配置
   - 查看调试串口的错误信息

### 调试技巧

1. 通过调试串口观察详细日志
2. 使用USB分析仪检查USB通信
3. 检查Windows设备管理器中的设备状态

## 参考资料

- HPM5321 用户手册
- CherryUSB 文档
- HPM SDK 示例代码
