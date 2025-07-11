# S32K3



## S32K312 评估板概览

NXP S32K312 评估板:

- [**S32K312NHT0VPAST**](https://item.szlcsc.com/5890293.html?fromZone=s_s__%22S32K312%22&spm=sc.gbn.xh2.zy.n___sc.hm.hd.ss&lcsc_vid=R1YKVVACRVRYAQBTQwdfBQcHQQMIAlFfFFVdBQZRFVcxVlNTRVBfXlJXR1hbUDtW),  1 x M7 core, 120M, 2MB P-Flash, 192KB SRAM, -40 °C to 105 °C, MaxQFP-100 封装, 外部 16M 晶振
- [USB转高速串口芯片 CH343](https://www.wch.cn/products/CH343.html), 最高可支持到 6Mbps
- 6路CANFD接口, 收发器型号 [**TCAN1044VDRBRQ1**](https://item.szlcsc.com/5911591.html?fromZone=s_s__%22C5215851%22&spm=sc.gbn.xh1.zy.n___sc.hm.hd.ss&lcsc_vid=QlkKU1VeFFcMV1MFFlAIXgdWTlVZBVxXTwALXlAAFQUxVlNTRVBfXl1UQVBeUzsOAxUeFF5JWBIBSRccGwIdBEoFGAxBAAgJFQACSQwSGg0%3D), AEC-Q100, 支持到 8Mbps, 通过 [**KF2EDGR-3.81-12P**](https://item.szlcsc.com/441153.html?fromZone=s_s__%223.81-12P%22&spm=sc.gbn.xh2.zy.n&lcsc_vid=QlkKU1VeFFcMV1MFFlAIXgdWTlVZBVxXTwALXlAAFQUxVlNTRVBcUlVfQlRXUzsOAxUeFF5JWBIBSRccGwIdBEoFGAxBAAgJFQACSQwSGg0%3D) 绿色插座引出, 配套插头为 [**KF2EDGK-3.81-12P**](https://item.szlcsc.com/440829.html?fromZone=s_s__%223.81-12P%22&spm=sc.gbn.xh3.zy.n&lcsc_vid=QlkKU1VeFFcMV1MFFlAIXgdWTlVZBVxXTwALXlAAFQUxVlNTRVBcUlVfQlRXUzsOAxUeFF5JWBIBSRccGwIdBEoFGAxBAAgJFQACSQwSGg0%3D)
- 引出所有的 100PIN 引脚
- 其它: RST按键, 用户LED, SWD接口, 1.27_2*5P JTAG接口, TypeC 或 排针5V 供电

![image_editor_1752119468389](README.assets/image_editor_1752119468389.jpg)

## 硬件设计

[S32K312](https://www.nxp.com.cn/products/S32K3) 的硬件资源:

![image-20250710133501618](README.assets/image-20250710133501618.png)

硬件设计参考资料:

-  `S32K3 MCUs for General Purpose – Hardware Design Package` 中的 `S32K3xx - Hardware Design Guidelines -- Rev_E2.pdf` 文档
- [S32K31XEVB-Q100 Evaluation Board | NXP 半导体](https://www.nxp.com.cn/design/design-center/development-boards-and-designs/S32K31XEVB-Q100), 官方的评估板设计文件
- S32DS 的 引脚配置工具

### 电源

有以下电源类型:

- VDD_HV_A, 主 I/O 和模拟电源电压, 3.3V 或 5V
- VREFH, ADC 高参考电压
- V11, 内部生成的核心逻辑电压供应 (+1.1 V), 外接电容即可
- V25, 内部产生的闪存电源 (+2.5 V), 外接电容即可

![image-20250710134609434](README.assets/image-20250710134609434.png)

![image-20250710134656659](README.assets/image-20250710134656659.png)

对于 S32K312 100PIN, 单 3.3V 供电即可.

### 时钟

S32K3xx MCU有以下时钟源:

- 8 - 40 MHz Fast External Oscillator (FXOSC), 外部晶振
- 48 MHz Fast Internal RC oscillator (FIRC)
- 32 kHz Low Power Oscillator (SIRC)
- 32 kHz Slow External Oscillator (SXOSC)
- Up to 320 MHz System Phased Lock Loop (SPLL)

选 16MHz 无源晶振即可, 因为官方评估板就是这个, S32DS默认新工程也是这个, 立创基础库也有这个, 一般外部搭配 12~20pF 电容使用

![image-20250710142621031](README.assets/image-20250710142621031.png)

下面是 S32DS 里面 S32K312 默认新工程的时钟树:

![image-20250710140704358](README.assets/image-20250710140704358.png)

### 复位

对于所有复位源，RESET_B 引脚(PTA5)均由 MCU 驱动至低电平至少 128 个总线时钟周期，直至闪存初始化完成。闪存初始化完成后，RESET_B 引脚释放，内部芯片复位。保持 RESET_B 引脚外部有效可延迟内部芯片复位的取消。有内部弱上拉。继续使用 10K + 100nF或1uF 的经典组合即可。

![image-20250710141033114](README.assets/image-20250710141033114.png)

### 调试

SWD 或 JTAG

![image-20250710141256995](README.assets/image-20250710141256995.png)

一般 SWD 接口即可:

- SWCLK, PTC4
- SWDIO, PTA4
- 一般会加上 RESET_B PTA5 引脚方便直接复位运行

![image-20250710142726338](README.assets/image-20250710142726338.png)

JTAG 接口有常见的 `2.54_2*10P 20PIN` 和 `1.27_2*5P 10PIN` 两种接口, 后者 7 9 有的调试器拿来做调试串口, 有的直接接地, 这里为了避免冲突, 直接悬空, 如下图:

![image-20250710141349969](README.assets/image-20250710141349969.png)

因为 100PIN 都引出来了, 所以 JTAG 用杜邦线连接的也可以直接插到引出的 2.54 排针上.

### LED

板载两个 LED:

- PWR, 3.3V 电源指示灯
- USR, 用户LED, 低电平点亮, 接到了 PTB10

![image-20250710143822176](README.assets/image-20250710143822176.png)

### 串口

调试串口:

- LPUART0_RX, PTC2
- LPUART0_TX, PTC3

![image-20250710142823805](README.assets/image-20250710142823805.png)

### CAN

6路CANFD的引脚映射:

| CANx | 引脚TX | 引脚RX |
| ---- | ------ | ------ |
| 0    | A7     | A6     |
| 1    | C8     | C9     |
| 2    | C15    | C14    |
| 3    | C0     | C1     |
| 4    | B3     | B2     |
| 5    | C10    | C11    |

其中 CAN5 的原理图:

![image-20250710143401099](README.assets/image-20250710143401099.png)

这里 120Ω 最终还是全贴上了, 方便调试, 毕竟调试时多了问题不大, 忘了没有就问题很大.

实际项目也可选择贴共模电感和把120Ω拆成两个60Ω, 如下图

![image-20250710143703697](README.assets/image-20250710143703697.png)

## 开发环境搭建

需要先到 [恩智浦半导体官方网站 | NXP 半导体](https://www.nxp.com.cn/) 官网注册帐号. 

大部分选用官方的 S32DS, 之前 S32K1 时也可以用 Keil MDK, 方便使用 DAP_Link 等, 但还是推荐 NXP 官方的 IDE. S32DS 新建工程默认支持 PEMicro, JTAG 等调试器. 如果不调试只下载, 那么 DAP Link, HSLinkPro/MicroLink, 硬汉的安富莱 等都是可以下载程序的.

[Real-Time Drivers (RTD)](https://www.nxp.com.cn/design/design-center/software/automotive-software-and-tools/real-time-drivers-rtd:AUTOMOTIVE-RTD#downloads) SDK 支持AUTOSAR®(配合EB)和非AUTOSAR应用上的实时软件, 大概是大家常说的 RTD MCAL 和 RTD LLD 两种开发方式.

这里为方便快速验证硬件, 开发环境为非AUTOSAR的 [S32K3 Standard Software](https://nxp.flexnetoperations.com/control/frse/product?entitlementId=719331357&lineNum=1&authContactId=136826597&authPartyId=151684767):

- IDE: S32DS 3.6.2
- SDK: RTD 6.0.0

### S32DS

[Design : Product Information : Automotive SW - S32K3 - S32 Design Studio](https://nxp.flexnetoperations.com/control/frse/product?child_plneID=831967):

![image-20250710145617898](README.assets/image-20250710145617898.png)

同意许可

![image-20250710145839225](README.assets/image-20250710145839225.png)

下载完后双击, 一步步安装即可, 现在也不需要 License Key 了, 可能需要注意的是 同意许可需要鼠标拉到最下面才能勾选, 还有安装路径的修改:

![image-20250708153715451](README.assets/image-20250708153715451.png)

![image-20250708153808123](README.assets/image-20250708153808123.png)

装完占用 11.7GB 空间, 

常用的开发组件都可以在 S32DS Extensions and  Updates 里面安装或更新, 如 GCC, Configuration Tools, S32K3xx development package 等:

![image-20250710145730227](README.assets/image-20250710145730227.png)

### RTD

[Design : Product Information : Automotive SW - S32K3/S32M27x - Real-Time Drivers for Cortex-M](https://nxp.flexnetoperations.com/control/frse/product?child_plneID=830617)

![image-20250710145936890](README.assets/image-20250710145936890.png)

同意许可, 下图中的 exe 用于 AutoSAR EB 方式, 此处不用, 只需 updatesite.zip 即可

![image-20250710150015107](README.assets/image-20250710150015107.png)

下载完后无需解压, 打开 S32DS:

至于上面 updatesite.zip 这类的安装包:
![image-20250708181707434](README.assets/image-20250708181707434.png)

![image-20250708182430369](README.assets/image-20250708182430369.png)

![image-20250708182454109](README.assets/image-20250708182454109.png)

Next, accept, 中间还会弹出信任组件:

![image-20250710150330377](README.assets/image-20250710150330377.png)

装完以后, 才能 S32DS 新建工程时找到 SDKs

![image-20250710150354414](README.assets/image-20250710150354414.png)

### JTAG 设置

之前买的 V11 仿真器在 J-Link 7.9x 版本下使用正常, 但在 S32K3 默认的驱动版本 8.28 不匹配

![image-20250710150703428](README.assets/image-20250710150703428.png)

S32DS 安装的路径下 `D:\NXP\S32DS.3.6.2\Drivers\Segger` 的四个文件替换成 V7.9x 的即可, 可以用 Everything 辅助查找

![image-20250710150936340](README.assets/image-20250710150936340.png)

这样 IDE 里面就能正常调试了

## 新建工程 LED Blink

### 新建工程

打开 S32DS, 多个 S32DS 可以同时打开, 但需要选择(Choose)不同的工作目录

![image-20250710152052576](README.assets/image-20250710152052576.png)

File -> New -> S32DS Application Project 新建新工程, 下面也有库工程和从官方例子中导入工程

![image-20250710152154594](README.assets/image-20250710152154594.png)

找到芯片, 输入工程名

![image-20250710154910125](README.assets/image-20250710154910125.png)

调试器 Jlink, SDK 选 6.0.0

![image-20250710155055421](README.assets/image-20250710155055421.png)

### 引脚配置

双击 .mex 文件打开配置工具

![image-20250710155352472](README.assets/image-20250710155352472.png)

更换封装

![image-20250710160110565](README.assets/image-20250710160110565.png)

引脚列取消默认的 PTA0 引脚配置, 然后找到 PTB10, 选择 `SIUL2:gpio,42`, 方向输出 Output

![image-20250710155606317](README.assets/image-20250710155606317.png)

标签和标识符取名 LED1

![image-20250710155921506](README.assets/image-20250710155921506.png)

默认输出电平 LOW, 可以不改或改成 HIGH

![image-20250710160325920](README.assets/image-20250710160325920.png)

### 时钟配置

**时钟配置**可以不用修改, 默认外部晶振 16MHz, 系统时钟 120MHz

![image-20250710161549992](README.assets/image-20250710161549992.png)

### 外设配置

点开外设配置, 加入DIO组件

![image-20250710160532974](README.assets/image-20250710160532974.png)

Port组件可以默认不修改

![image-20250710160642795](README.assets/image-20250710160642795.png)

DIO 组件没有什么配置的, 也保持默认. 点击更新源代码

![image-20250710160807700](README.assets/image-20250710160807700.png)

### 代码修改

回到代码编辑界面, 添加 Blink 需要的简易代码, main.c 文件中

```c
#include "Siul2_Dio_Ip.h"
#include "Siul2_Port_Ip.h"

Siul2_Port_Ip_Init(
      NUM_OF_CONFIGURED_PINS_PortContainer_0_BOARD_InitPeripherals,
      g_pin_mux_InitConfigArr_PortContainer_0_BOARD_InitPeripherals);

Siul2_Dio_Ip_WritePin(LED1_PORT, LED1_PIN, 0);
for(volatile int i = 0; i < 1000000; i++);
Siul2_Dio_Ip_WritePin(LED1_PORT, LED1_PIN, 1);
for(volatile int i = 0; i < 1000000; i++);
```

如图:

![image-20250710170911209](README.assets/image-20250710170911209.png)

### 工程编译

工程右键 Build Project

![image-20250710170947471](README.assets/image-20250710170947471.png)

### 代码调试

![image-20250710162116391](README.assets/image-20250710162116391.png)

展开 GDB SEGGER J-Link Debugging (如果没有可以右键新建配置), 可以看到默认 SWD 接口

![image-20250710162440770](README.assets/image-20250710162440770.png)

点击 Debug, 切换视图

![image-20250710162618700](README.assets/image-20250710162618700.png)

双击打断点, Step Over 单步执行, 右边可以查看变量等

![image-20250710162813073](README.assets/image-20250710162813073.png)

如果想一直执行, 可以取消断点, 鼠标点到return行, 然后 run to line 运行

![image-20250710163229067](README.assets/image-20250710163229067.png)

就可以看到 LED 在一直闪烁

## HEX 文件生成

S32DS 工程右键 Properties

![image-20250710151135845](README.assets/image-20250710151135845.png)

勾选完后, 再随便点击最左侧某个设置项比如Logging转移一下IDE的注意力, 然后再点回来Settings页面

![image-20250710151223880](README.assets/image-20250710151223880.png)

一般发布的 HEX 文件先把工程切换到 Release FLASH, 再编译生成 HEX 文件.

![image-20250710164200614](README.assets/image-20250710164200614.png)

## 烧录

使用 SWD 接口连接, 外加 3.3V 接 Vref, RESET_B 接 JTAG 15PIN SRST

### JFlash

![image-20250711091312277](README.assets/image-20250711091312277.png)

File -> Creat Project, 选择 S32K312, SWD 接口

![image-20250710151343263](README.assets/image-20250710151343263.png)

File -> Open data file 载入HEX 文件.

然后 F7 直接烧录, F9 直接执行(可能需要连接RESET引脚) 即可:

![image-20250710151649744](README.assets/image-20250710151649744.png)

### DAP-Link Utility

![image-20250711091412392](README.assets/image-20250711091412392.png)

![Snipaste_2025-07-10_21-27-36](README.assets/Snipaste_2025-07-10_21-27-36.png)

## 导入工程

File -> Import, 导入本地已有工程

![image-20250710152426005](README.assets/image-20250710152426005.png)

选择已有的工程目录

![image-20250710152546420](README.assets/image-20250710152546420.png)

## 重命名工程

很多时候需要改个名, 工程右键 -> Rename

![image-20250710152808627](README.assets/image-20250710152808627.png)

输入新名称即可

![image-20250710152838071](README.assets/image-20250710152838071.png)

## UART

引脚配置:

- LPUART0_RX, PTC2
- LPUART0_TX, PTC3

这里配置成 2Mbps, 实现一个串口 echo 的硬件测试程序

![image-20250710172449976](README.assets/image-20250710172449976.png)

![image-20250710172548972](README.assets/image-20250710172548972.png)

时钟配置 LPUART0 的时钟默认是 60MHz

![image-20250710172757794](README.assets/image-20250710172757794.png)

外设配置加入 Lpuart 和 中断控制IntCtrl

自定义 `60M / 2 / 15 = 2M` 波特率

![image-20250711133909407](README.assets/image-20250711133909407.png)

设置中断的回调函数

![image-20250711134004242](README.assets/image-20250711134004242.png)

打开中断

![image-20250710173443608](README.assets/image-20250710173443608.png)

这里的 Handler 是和 `s32k3\s32k312_uart\RTD\include\Lpuart_Uart_Ip_Irq.h` 中的名称对应上的

点击 `更新源代码`, 然后修改 main.c

```c
#include "Clock_Ip.h"
#include "IntCtrl_Ip.h"
#include "Lpuart_Uart_Ip.h"
#include "Lpuart_Uart_Ip_Irq.h"
#include "Mcal.h"
#include "Siul2_Port_Ip.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

const uint8 UART_INSTANCE = 0; // LPUART0
// 环形缓冲区相关定义
#define UART_RX_BUFFER_SIZE 256
uint8_t g_uart0_rx_buffer[UART_RX_BUFFER_SIZE] = {0};
uint8_t g_uart0_cmd_buf[UART_RX_BUFFER_SIZE] = {0};
volatile uint16_t g_uart0_rx_head = 0;
volatile uint16_t g_uart0_rx_tail = 0;
volatile uint8_t g_uart0_cmd_ready = 0;
volatile uint8_t g_uart0_cmd_len = 0;

void UART0_CALLBACK(const uint8 HwInstance,
                    const Lpuart_Uart_Ip_EventType Event,
                    const void *UserData) {
  (void)UserData;
  if (Event == LPUART_UART_IP_EVENT_RX_FULL) {
    // 写入数据到环形缓冲区
    uint8_t data = g_uart0_rx_buffer[g_uart0_rx_head]; // 当前收到的数据
    g_uart0_rx_head = (g_uart0_rx_head + 1) % UART_RX_BUFFER_SIZE;
    // 检查是否收到换行符
    if (data == '\n') {
      // 取出一条命令（从 tail 到 head，遇到 \n 截止）
      while (g_uart0_rx_tail != g_uart0_rx_head) {
        uint8_t ch = g_uart0_rx_buffer[g_uart0_rx_tail];
        g_uart0_rx_tail = (g_uart0_rx_tail + 1) % UART_RX_BUFFER_SIZE;
        g_uart0_cmd_buf[g_uart0_cmd_len++] = ch;
        if (ch == '\n') {
          break;
        }
      }
      g_uart0_cmd_ready = 1;
    }
    // 继续接收下一个字节
    Lpuart_Uart_Ip_SetRxBuffer(HwInstance, &g_uart0_rx_buffer[g_uart0_rx_head],
                               1);
  }
}

int main(void) {
  Clock_Ip_Init(&Clock_Ip_aClockConfig[0]);
  Siul2_Port_Ip_Init(
      NUM_OF_CONFIGURED_PINS_PortContainer_0_BOARD_InitPeripherals,
      g_pin_mux_InitConfigArr_PortContainer_0_BOARD_InitPeripherals);
  IntCtrl_Ip_Init(&IntCtrlConfig_0);
  Lpuart_Uart_Ip_Init(UART_INSTANCE, &Lpuart_Uart_Ip_xHwConfigPB_0);

  const char *message =
      "UART Ping-Pong Buffer Example\r\nType something and press enter...\r\n";
  Lpuart_Uart_Ip_SyncSend(UART_INSTANCE, (const uint8_t *)message,
                          strlen(message), 0xFFFF);
  memset(g_uart0_rx_buffer, 0, sizeof(g_uart0_rx_buffer));
  Lpuart_Uart_Ip_AsyncReceive(UART_INSTANCE, g_uart0_rx_buffer, 1);

  while (1) {
    // 检查是否收到完整命令（遇到换行符）
    if (g_uart0_cmd_ready) {
      // 处理命令, 这里原封不动发回
      if (LPUART_UART_IP_STATUS_BUSY !=
          Lpuart_Uart_Ip_GetTransmitStatus(UART_INSTANCE, NULL)) {
        Lpuart_Uart_Ip_AsyncSend(UART_INSTANCE, g_uart0_cmd_buf,
                                 g_uart0_cmd_len);
      }
      g_uart0_cmd_len = 0; // 重置命令长度
      g_uart0_cmd_ready = 0;
    }
  }

  return 0;
}

```

代码解释:

- 初始化部分, 配置时钟、端口、中断、UART 等外设。发送欢迎信息，并启动异步 1字节 接收。
- UART 回调函数 `UART0_CALLBACK`, 存放收到的字节到环形缓冲区, 遇到 `\n` 结尾取出命令, 设置标志
- 主循环, 检查标志, 处理串口命令, 此处是直接异步回传

测试, 打开串口调试助手, 2M-8-N-1, 收到欢迎语后清空, 然后每 1ms 定时发送 100字节(含回车换行), 可以看到收发字节相同:

![image-20250711135330922](README.assets/image-20250711135330922.png)















































