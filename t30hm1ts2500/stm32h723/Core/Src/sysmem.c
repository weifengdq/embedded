/**
 * @file  sysmem.c
 * @brief newlib _sbrk (栈在 DTCM 顶部, 堆在 RAM_D1 的 .bss 之后)
 *
 * 与 CubeMX 默认版本的区别: 堆上限用链接脚本给出的 __heap_end,
 * 不再用 "栈顶 - _Min_Stack_Size" 推算 (本工程栈在 DTCM, 堆在 AXI SRAM,
 * 两者不在同一块内存里, 用 _estack 推算会算出错误的上限)。
 */
#include <errno.h>
#include <stdint.h>
#include <stddef.h>

static uint8_t *__sbrk_heap_end = NULL;

void *_sbrk(ptrdiff_t incr)
{
  extern uint8_t _end;        /* 链接脚本: .bss 结束 */
  extern uint8_t __heap_end;  /* 链接脚本: RAM_D1 末尾 */
  const uint8_t *max_heap = (const uint8_t *)&__heap_end;
  uint8_t *prev_heap_end;

  if (NULL == __sbrk_heap_end)
  {
    __sbrk_heap_end = &_end;
  }

  if (__sbrk_heap_end + incr > max_heap)
  {
    errno = ENOMEM;
    return (void *)-1;
  }

  prev_heap_end = __sbrk_heap_end;
  __sbrk_heap_end += incr;

  return (void *)prev_heap_end;
}

#if defined(__PICOLIBC__)
  __strong_reference(_sbrk, sbrk);
#endif
