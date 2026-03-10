/*
Copyright 2018 Embedded Microprocessor Benchmark Consortium (EEMBC)

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

Original Author: Shay Gal-on
*/

#ifndef CORE_PORTME_H
#define CORE_PORTME_H

#include <stddef.h>
#include "Ifx_Types.h"
#include "Bsp.h"

#ifndef HAS_FLOAT
#define HAS_FLOAT 1
#endif

#ifndef HAS_TIME_H
#define HAS_TIME_H 0
#endif

#ifndef USE_CLOCK
#define USE_CLOCK 0
#endif

#ifndef HAS_STDIO
#define HAS_STDIO 1
#endif

#ifndef HAS_PRINTF
#define HAS_PRINTF 1
#endif

#ifndef COMPILER_VERSION
#ifdef __GNUC__
#define COMPILER_VERSION "GCC" __VERSION__
#else
#define COMPILER_VERSION "TriCore GCC"
#endif
#endif

#ifndef COMPILER_FLAGS
#ifdef COREMARK_COMPILER_FLAGS
#define COMPILER_FLAGS COREMARK_COMPILER_FLAGS
#else
#define COMPILER_FLAGS "(unspecified)"
#endif
#endif

#ifndef MEM_LOCATION
#ifdef COREMARK_MEM_LOCATION
#define MEM_LOCATION COREMARK_MEM_LOCATION
#else
#define MEM_LOCATION "STACK"
#endif
#endif

typedef signed short ee_s16;
typedef unsigned short ee_u16;
typedef signed int ee_s32;
typedef double ee_f32;
typedef unsigned char ee_u8;
typedef unsigned int ee_u32;
typedef ee_u32 ee_ptr_int;
typedef size_t ee_size_t;

#define align_mem(x) (void *)(4 + (((ee_ptr_int)(x)-1) & ~3))

typedef Ifx_TickTime CORE_TICKS;

#ifndef SEED_METHOD
#define SEED_METHOD SEED_VOLATILE
#endif

#ifndef MEM_METHOD
#if defined(COREMARK_MEM_METHOD_MALLOC)
#define MEM_METHOD MEM_MALLOC
#elif defined(COREMARK_MEM_METHOD_STATIC)
#define MEM_METHOD MEM_STATIC
#else
#define MEM_METHOD MEM_STACK
#endif
#endif

#ifndef MULTITHREAD
#define MULTITHREAD 1
#define USE_PTHREAD 0
#define USE_FORK 0
#define USE_SOCKET 0
#endif

#if (MULTITHREAD > 1)
#ifndef PARALLEL_METHOD
#define PARALLEL_METHOD "AURIX_TC4DX_CORES"
#endif
#endif

#ifndef MAIN_HAS_NOARGC
#define MAIN_HAS_NOARGC 1
#endif

#ifndef MAIN_HAS_NORETURN
#define MAIN_HAS_NORETURN 0
#endif

extern ee_u32 default_num_contexts;

typedef struct CORE_PORTABLE_S
{
    ee_u8 portable_id;
} core_portable;

void portable_init(core_portable *p, int *argc, char *argv[]);
void portable_fini(core_portable *p);

void coremark_worker_mark_boot(ee_u32 stage);
void coremark_worker_entry(void);

#if !defined(PROFILE_RUN) && !defined(PERFORMANCE_RUN) && !defined(VALIDATION_RUN)
#if (TOTAL_DATA_SIZE == 1200)
#define PROFILE_RUN 1
#elif (TOTAL_DATA_SIZE == 2000)
#define PERFORMANCE_RUN 1
#else
#define VALIDATION_RUN 1
#endif
#endif

#endif
