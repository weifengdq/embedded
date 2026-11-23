/*
 * core_portme.h - CoreMark 移植层配置（hpm5e31_1 / HPM5E31）
 *
 * 基于 EEMBC CoreMark barebones 模板 / HPM SDK v1.13.0
 * (hpm_sdk/samples/coremark/src/port/core_portme.h) 修改：
 *   - 计时类型 CORETIMETYPE 用 64 位，时间源改为 mcycle
 *   - MEM_LOCATION 由 BENCH_DATA_* 决定，输出中体现数据块位置
 *   - HAS_PRINTF=0，ee_printf 由本仓库实现（用于抓取分数做统计）
 */
#ifndef CORE_PORTME_H
#define CORE_PORTME_H

#ifndef HAS_FLOAT
#define HAS_FLOAT 1
#endif
#ifndef HAS_TIME_H
#define HAS_TIME_H 1
#endif
#ifndef USE_CLOCK
#define USE_CLOCK 1
#endif
#ifndef HAS_STDIO
#define HAS_STDIO 1
#endif
#ifndef HAS_PRINTF
#define HAS_PRINTF 0
#endif
#ifndef USE_SNPRINTF
#define USE_SNPRINTF 0
#endif

#ifndef COMPILER_VERSION
#ifdef __GNUC__
#define COMPILER_VERSION "GCC" __VERSION__
#else
#define COMPILER_VERSION "unknown"
#endif
#endif

#ifndef COMPILER_FLAGS
#define COMPILER_FLAGS "-O3"
#endif

#if defined(BENCH_DATA_SRAM)
#define MEM_LOCATION "BSS_AXI_SRAM"
#else
#define MEM_LOCATION "STACK_DLM"
#endif

#ifndef BENCH_OPT_STR
#define BENCH_OPT_STR "-O3"
#endif

typedef signed short       ee_s16;
typedef unsigned short     ee_u16;
typedef signed int         ee_s32;
typedef double             ee_f32;
typedef unsigned char      ee_u8;
typedef unsigned int       ee_u32;
typedef unsigned long long ee_u64;
typedef ee_u32             ee_ptr_int;
typedef ee_u32             ee_size_t;
#define NULL ((void *)0)

#define align_mem(x) (void *)(4 + (((ee_ptr_int)(x)-1) & ~3))

#define CORETIMETYPE ee_u64
typedef ee_u64 CORE_TICKS;

#ifndef SEED_METHOD
#define SEED_METHOD SEED_VOLATILE
#endif
#ifndef MEM_METHOD
#define MEM_METHOD MEM_STACK
#endif
#ifndef MULTITHREAD
#define MULTITHREAD 1
#define USE_PTHREAD 0
#define USE_FORK    0
#define USE_SOCKET  0
#endif
#ifndef MAIN_HAS_NOARGC
#define MAIN_HAS_NOARGC 0
#endif
#ifndef MAIN_HAS_NORETURN
#define MAIN_HAS_NORETURN 0
#endif

extern ee_u32 default_num_contexts;

typedef struct CORE_PORTABLE_S {
    ee_u8 portable_id;
} core_portable;

void portable_init(core_portable *p, int *argc, char *argv[]);
void portable_fini(core_portable *p);

void ee_printf(char *format, ...);

#if !defined(PROFILE_RUN) && !defined(PERFORMANCE_RUN) && !defined(VALIDATION_RUN)
#if (TOTAL_DATA_SIZE == 1200)
#define PROFILE_RUN 1
#define PERFORMANCE_RUN 0
#define VALIDATION_RUN 0
#elif (TOTAL_DATA_SIZE == 2000)
#define PERFORMANCE_RUN 1
#define VALIDATION_RUN 0
#define PROFILE_RUN 0
#else
#define VALIDATION_RUN 1
#define PERFORMANCE_RUN 0
#define PROFILE_RUN 0
#endif
#endif

#ifndef CORE_DEBUG
#define CORE_DEBUG 0
#endif
#ifndef COMPILER_REQUIRES_SORT_RETURN
#define COMPILER_REQUIRES_SORT_RETURN 0
#endif

#endif /* CORE_PORTME_H */
