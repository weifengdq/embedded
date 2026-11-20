/*
******************************************************************************
**  File        : STM32H723XG_FLASH.ld
**  Abstract    : Linker script for STM32H723ZGTx (1024KB FLASH / 564KB RAM)
**
**  内存布局 (和 F407 工程不同, 针对 H7 的 D-Cache + DMA 做了规划):
**    FLASH   0x08000000 1024K  代码/常量
**    DTCMRAM 0x20000000  128K  只放 MSP 栈 (TCM 不经过 cache, 最快;
**                              但 DMA 访问不到 TCM, 所以不放任何 DMA 缓冲)
**    RAM_D1  0x24000000  320K  .data/.bss/heap (AXI SRAM, 走 D-Cache)
**    RAM_D2  0x30000000   32K  .dma_buffer (MPU 配成 Normal non-cacheable,
**                              专门给 SPI1 DMA 用, 不需要 clean/invalidate)
******************************************************************************
*/
ENTRY(Reset_Handler)

MEMORY
{
DTCMRAM (xrw)  : ORIGIN = 0x20000000, LENGTH = 128K
RAM_D1  (xrw)  : ORIGIN = 0x24000000, LENGTH = 320K
RAM_D2  (xrw)  : ORIGIN = 0x30000000, LENGTH = 32K
FLASH   (rx)   : ORIGIN = 0x08000000, LENGTH = 1024K
}

/* 栈顶放在 DTCM 末尾: 128K 全部给栈 (DMA 用不到 TCM, 不浪费) */
_estack = ORIGIN(DTCMRAM) + LENGTH(DTCMRAM);
/* 堆从 .bss 结束开始, 到 RAM_D1 末尾 (sysmem.c 使用 __heap_end) */
__heap_end = ORIGIN(RAM_D1) + LENGTH(RAM_D1);
_Min_Heap_Size  = 0x2000;   /* 8K  (链接期检查用) */
_Min_Stack_Size = 0x1000;   /* 4K  (实际栈是整个 DTCM) */

SECTIONS
{
  /* 中断向量表 */
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } >FLASH

  .text :
  {
    . = ALIGN(4);
    *(.text)
    *(.text*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)

    KEEP (*(.init))
    KEEP (*(.fini))

    . = ALIGN(4);
    _etext = .;
  } >FLASH

  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
  } >FLASH

  /* letter-shell 的命令表 (必须保留, 否则 --gc-sections 会删掉) */
  .shell_command :
  {
    . = ALIGN(4);
    _shell_command_start = .;
    KEEP(*(SORT_BY_NAME(shellCommand*)))
    . = ALIGN(4);
    _shell_command_end = .;
  } >FLASH

  .ARM.extab (READONLY) :
  {
    . = ALIGN(4);
    *(.ARM.extab* .gnu.linkonce.armextab.*)
    . = ALIGN(4);
  } >FLASH

  .ARM (READONLY) :
  {
    . = ALIGN(4);
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
    . = ALIGN(4);
  } >FLASH

  .preinit_array (READONLY) :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array*))
    PROVIDE_HIDDEN (__preinit_array_end = .);
    . = ALIGN(4);
  } >FLASH

  .init_array (READONLY) :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT(.init_array.*)))
    KEEP (*(.init_array*))
    PROVIDE_HIDDEN (__init_array_end = .);
    . = ALIGN(4);
  } >FLASH

  .fini_array (READONLY) :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP (*(SORT(.fini_array.*)))
    KEEP (*(.fini_array*))
    PROVIDE_HIDDEN (__fini_array_end = .);
    . = ALIGN(4);
  } >FLASH

  /* 初始化数据 (LMA 在 FLASH, VMA 在 AXI SRAM) */
  _sidata = LOADADDR(.data);

  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)
    *(.RamFunc)
    *(.RamFunc*)
    . = ALIGN(4);
    _edata = .;
  } >RAM_D1 AT> FLASH

  .bss (NOLOAD) :
  {
    . = ALIGN(4);
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = _ebss;
  } >RAM_D1

  /* 链接期检查: .bss 之后至少还要放得下 8K 堆 */
  ._user_heap_check (NOLOAD) :
  {
    . = ALIGN(8);
    PROVIDE ( end = . );
    PROVIDE ( _end = . );
    . = . + _Min_Heap_Size;
    . = ALIGN(8);
  } >RAM_D1

  /* SPI1 DMA 缓冲: RAM_D2, 32 字节对齐, MPU 里配成 Normal non-cacheable */
  .dma_buffer (NOLOAD) :
  {
    . = ALIGN(32);
    _dma_buffer_start = .;
    *(.dma_buffer)
    *(.dma_buffer*)
    . = ALIGN(32);
    _dma_buffer_end = .;
  } >RAM_D2
}
