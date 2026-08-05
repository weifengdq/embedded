/**
 * \file Ifx_Gnuc_CppInit.c
 * \brief TriCore GCC 裸机构建所需的 C++ 静态构造/析构入口桩函数。
 *
 * 背景：
 *   Libraries/Infra/Ssw/TC3xx/Tricore/Ifx_Ssw_Infra.c 中的 Ifx_Ssw_doCppInit()
 *   在 __GNUC__ 分支会调用 _init()（C++ 全局构造函数初始化入口）。
 *   _init / _fini 通常由 crti.o / crtn.o 提供，但裸机链接使用 -nostartfiles
 *   将其排除，导致链接期报 "undefined reference to `_init'"。
 *
 *   本工程为纯 C 工程，不存在 C++ 全局构造函数，因此这里提供空实现即可。
 *   同时调用 __libc_init_array 会遍历 .init_array，保证将来若引入带构造函数的
 *   代码也能正常初始化（链接脚本中 .init_array 为空时该循环不执行）。
 *
 * 说明：
 *   仅在 GCC（且非 HighTec）下编译，TASKING 构建时整个文件为空，
 *   避免与 TASKING 运行时的 _main() 机制冲突。
 */

#if defined(__GNUC__) && !defined(__HIGHTEC__) && !defined(__TASKING__)

/* 由链接脚本 Lcf_Gnuc_Tricore_Tc.lsl 中的 .init_array / .fini_array 段提供 */
extern void (*__init_array_start[])(void) __attribute__((weak));
extern void (*__init_array_end[])(void) __attribute__((weak));
extern void (*__fini_array_start[])(void) __attribute__((weak));
extern void (*__fini_array_end[])(void) __attribute__((weak));

/** \brief C++ 全局构造函数初始化入口（替代 crti.o 中的 _init） */
void _init(void)
{
    if ((__init_array_start != 0) && (__init_array_end != 0))
    {
        unsigned int count = (unsigned int)(__init_array_end - __init_array_start);
        unsigned int i;

        for (i = 0U; i < count; i++)
        {
            if (__init_array_start[i] != 0)
            {
                __init_array_start[i]();
            }
        }
    }
}

/** \brief C++ 全局析构函数入口（替代 crtn.o 中的 _fini），裸机下通常不会被调用 */
void _fini(void)
{
    if ((__fini_array_start != 0) && (__fini_array_end != 0))
    {
        unsigned int count = (unsigned int)(__fini_array_end - __fini_array_start);
        unsigned int i;

        for (i = count; i > 0U; i--)
        {
            if (__fini_array_start[i - 1U] != 0)
            {
                __fini_array_start[i - 1U]();
            }
        }
    }
}

#endif /* __GNUC__ && !__HIGHTEC__ && !__TASKING__ */
