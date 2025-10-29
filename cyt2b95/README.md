# CYT2B95

[TOC]



## 板子图片

![image-20251028165143647](README.assets/image-20251028165143647.png)

资源如下:

- CYT2B95CAC00AZSGS
  - Cortex-M4F(160M max) + Cortex-M0+(100M max), TRAVEO™ T2G Family
  - 2112 KB (1984 KB + 128 KB) Code Flash, 128 KB (96 KB + 32 KB) Work Flash, 256 KB SRAM
  - 100-LQFP, ASIL-B, eSHE – on, HSM – on, RSA - 2K, Third revision, S-grade (–40 °C to 105 °C)
- 8路 CANFD 通过 TCAN1044VDRBRQ1 引出到 3.81 间距的绿端子, 板载120Ω终端电阻, 支持常用的 500K, 1M, 2M, 5M等速率
- SCB0 UART(P0.0 P0.1) 调试串口通过 CH343 引出到 USB TypeC
- 外部16MHz无源晶振, 复位按键, 用户LED, 3.3V电源指示灯, SWD调试口, IO引出等

详细原理图参见板子附送的资料包. 硬件设计也可参考官方文档: [Hardware design guide for the TRAVEO™ T2G family](https://www.infineon.com/row/public/documents/10/42/infineon-an220270-hardware-design-guide-for-the-traveo-t2g-family-applicationnotes-en.pdf)

## 开发环境

ModusToolbox Tools Package 3.6:

- Project Creator 2.50, 图标NEW, 基于BSPs创建 Eclipse VSCode Keil 等工程, 省去很多Startup的工作
- BSP Assistant 1.50, 图标BSP, 管理Bsp配置, 这里可以直接配置固件版本, 宏, 启动Device Configurator等
- Device Configurator 5.50, 图标DEV, 类似STM32CubeMX, 配置外设, 时钟树, 引脚等, 保存自动生成配置代码

Keil MDK + [T2G-B-E_DFP pack](https://www.keil.arm.com/packs/t2g-b-e_dfp-infineon/versions/)

所有文档与代码仅用于学习交流.

## 新建工程 LED Blink

### Project Creator 创建工程

打开 Project Creator, 选择 Creat from MPN...(需要联网, 从主仓库拉最新版, 如果不能联网可以尝试另一个按钮从本地BSP创建)

![image-20251028175118162](README.assets/image-20251028175118162.png)

弹出 BSP Assistant 界面, 选择 TRAVEO MCU 下面的 CYT2B95CAS(S是标准温度105℃, E是扩展温度125℃), 新的BSP命名为 TARGET_CYT2B95:

![image-20251028173844423](README.assets/image-20251028173844423.png)

联网卡顿一会, 先不管配置, 点击Close回来

![image-20251028174034402](README.assets/image-20251028174034402.png)

可以看到 Import 处有了 CYT2B95, 点击Next

![image-20251028174134836](README.assets/image-20251028174134836.png)

选择工程路径和IDE, 模版应用选择  Peripherals 下面的 `Toggle LED with Systick Timer`, 重命名为 `cyt2b95_led_blink_systick`, 点击 Create:

![image-20251028175555413](README.assets/image-20251028175555413.png)

接下来会联网下载一些库, 稍微等待一会即可

![image-20251028175939543](README.assets/image-20251028175939543.png)

接下来使用 BSP Assistant  配置IO和时钟树, 就像 STM32CubeMX 那样.

### BSP Assistant 配置IO和时钟树

打开 BSP Assistant, 点击 Open BSP

![image-20251028175036983](README.assets/image-20251028175036983.png)

上面工作空间是`C:\github\cyt2b95\`, 工程目录是`C:\github\cyt2b95\cyt2b95_led_blink_systick` , 此处BSP就打开 `C:\github\cyt2b95\cyt2b95_led_blink_systick\bsps\TARGET_CYT2B95` 文件夹.

此处无需使用 HAL, 下拉选N/A, 上面默认CM4启动, 让CM0+核进入deep sleep:

![image-20251028180412831](README.assets/image-20251028180412831.png)

点击Edit Configuration 进入 Device Configurator 

![image-20251028180833654](README.assets/image-20251028180833654.png)

LED是 P18.5, 低电平点亮, Pins选项卡, 设置 P18.5 为 Strong Drive, 初始高电平熄灭, 其它默认, Code Preview 可以预览代码

![image-20251028181244889](README.assets/image-20251028181244889.png)

接下来配置时钟树, 如果不改默认是使用内部8MHz, CM4跑在100MHz, 板子上有16MHz外部晶振, 此处虽然用不到, 但是可以配一下, 双击ECO, 配置晶振 [**X322516MLB4SI**](https://item.szlcsc.com/14393.html?fromZone=s_s__%2216MHz%22&spm=sc.gbn.xh1.zy.n___sc.hm.hd.ss&lcsc_vid=R1YKVVACRVRYAQBTQwdfBQcHQQMIAlFfFFVdBQZRFVcxVlNTR1VfUlZTTlReVTsOAxUeFF5JWBYZEEoEHg8JSQcJGk4%3D) 的参数 16MHz, ±10ppm, 9pF负载电容

![image-20251028181856142](README.assets/image-20251028181856142.png)

接下来双击PLL, 可以看到只有8MHz内部晶振的选项并不能用到外部的16MHz, 期望频率改到160MHz

![image-20251028182153910](README.assets/image-20251028182153910.png)

点击CLK_HF0, 改到 PATH1, 这样CM4就能跑到160MHz, 如果CM0+跑的话是80MHz

![image-20251028182336476](README.assets/image-20251028182336476.png)

外设这里使用不到, 暂不配置, 按下 Ctrl+S, 已经在工程目录下 `C:\github\cyt2b95\cyt2b95_led_blink_systick\bsps\TARGET_CYT2B95\config\GeneratedSource` 自动生成了代码

### 打开工程编译下载

双击打开工程

![image-20251028182543027](README.assets/image-20251028182543027.png)

修改 main.c

```c
#include "cy_pdl.h"
#include "cybsp.h"

#define SYSTICK_RELOAD_VAL   (16000000UL)
#define SYSTICK_PERIOD_TIMES (10u)

// 必须加 volatile
volatile uint32_t counterTick = 0;

void toggle_led_on_systick_handler(void)
{
    counterTick++;
}

int main(void)
{
    cy_rslt_t result = cybsp_init() ;
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    __enable_irq();

    /*Initialize the User LED*/
    Cy_GPIO_Pin_FastInit(P18_5_PORT, P18_5_NUM, CY_GPIO_DM_STRONG, 1u, P18_5_GPIO);

    /*Initialize the systick*/
    Cy_SysTick_Init(CY_SYSTICK_CLOCK_SOURCE_CLK_CPU, SYSTICK_RELOAD_VAL);

    /*Set Systick interrupt callback*/
    Cy_SysTick_SetCallback(0, toggle_led_on_systick_handler);

    /*Enable Systick and the Systick interrupt*/
    Cy_SysTick_Enable();


    for (;;)
    {
        /*toggle user led*/
        if(counterTick > SYSTICK_PERIOD_TIMES)
        {
            counterTick = 0;
            Cy_GPIO_Inv(P18_5_PORT, P18_5_NUM);
        }
    }
}
```

编译无error, 暂时忽略warning.

调试器用的 JLink SWD 接口, 接了3根线(GND DIO CLK)

![image-20251028183139084](README.assets/image-20251028183139084.png)

 魔术棒打开调试器, JLink SW

![image-20251028184020479](README.assets/image-20251028184020479.png)

这里勾选 Reset and Run 并没有效果, 实际还是下载后按了复位键

![image-20251028184140193](README.assets/image-20251028184140193.png)

有时候第一次下载会失败, 再下载一次即可, 按下复位后可以看到  LED 以 2s 周期闪烁.

### HEX 烧录

![image-20251028184747421](README.assets/image-20251028184747421.png)

重新编译, 生成的HEX为 `"C:\github\cyt2b95\cyt2b95_led_blink_systick\cyt2b95_led_blink_systick_Objects\cyt2b95_led_blink_systick.hex"`

打开 JFlash, 新建工程选择 CCYT2B95CAS_M4, SWD

![image-20251028185042885](README.assets/image-20251028185042885.png)

加载 hex 文件, F7 编程后复位即可.

## UART print

SCB0 UART(P0.0 P0.1) 调试串口通过 CH343 引出到 USB TypeC, 时钟树配置参考上小节, 下面是 BSP Assistant 外设里面勾选 SCB0, UART, 115200-8-N-1, 过采样8(这样主时钟需要设置到大约921600Hz)

![image-20251028185651479](README.assets/image-20251028185651479.png)

先配置外设时钟, 选择 `24.5 bit Divider 0`, 搭配整数86, 分数26, 得到 921.5kHz, 接近需要的 921600Hz, 外设选择 SCB0

![image-20251028190053957](README.assets/image-20251028190053957.png)

回到 SCB0 设置界面, 发现时钟已经过来了, 再选好IO映射即可:

![image-20251028190150203](README.assets/image-20251028190150203.png)

这里只是简单的 print 和 echo, 下面的 API Mode 选择了 Low Level, 图略. 

保存后生成代码, 直接编译报错, 原因是新生成的 `cycfg_peripherals.c` 外设源码文件没有被包含到 Keil 工程, 添加即可

![image-20251028190655526](README.assets/image-20251028190655526.png)

修改main.c代码, 此处实现了print, 无需keil勾选微库

```c
#include "cy_pdl.h"
#include "cy_scb_uart.h"
#include "cybsp.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

cy_stc_scb_uart_context_t uart0;
uint8_t g_uart_out_data[256];

void uart0_callback(uint32_t event) {}

void print(void *fmt, ...) {
  va_list arg;
  va_start(arg, fmt);
  vsprintf((char *)&g_uart_out_data[0], (char *)fmt, arg);
  while (Cy_SCB_UART_IsTxComplete(SCB0) != true) {
  };
  Cy_SCB_UART_PutArray(SCB0, g_uart_out_data, strlen((char *)g_uart_out_data));
  va_end(arg);
}

int main(void) {
  cy_rslt_t result = cybsp_init();
  if (result != CY_RSLT_SUCCESS) {
    CY_ASSERT(0);
  }

  Cy_SCB_UART_DeInit(SCB0);
  Cy_SCB_UART_Init(SCB0, &scb_0_config, &uart0);
  Cy_SCB_UART_Enable(SCB0);

  __enable_irq();

  print("CYT2B95 UART Echo Example\r\n");
  // 测试整数浮点数uint64_t打印
  print("int: %d, float: %.2f, uint64_t: %llu\r\n", -12345, 3.14159,
        12345678901234ULL);

  while (1) {
    // Check if there is at least one character in the RX FIFO
    if (Cy_SCB_UART_GetNumInRxFifo(SCB0) > 0) {
      // Read the received character
      uint8_t rx_data = Cy_SCB_UART_Get(SCB0);

      // Send the same character back to the terminal (echo)
      Cy_SCB_UART_Put(SCB0, rx_data);
    }
  }

  return 0;
}
```

下载复位后测试效果:

![image-20251028190524225](README.assets/image-20251028190524225.png)































































