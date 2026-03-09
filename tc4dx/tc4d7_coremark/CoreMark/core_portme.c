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

#include <stdio.h>
#include <stdlib.h>

#include "IfxCpu.h"
#include "coremark.h"
#include "serialio.h"

#define COREMARK_UART_BAUDRATE 115200

#if VALIDATION_RUN
volatile ee_s32 seed1_volatile = 0x3415;
volatile ee_s32 seed2_volatile = 0x3415;
volatile ee_s32 seed3_volatile = 0x66;
#endif
#if PERFORMANCE_RUN
volatile ee_s32 seed1_volatile = 0x0;
volatile ee_s32 seed2_volatile = 0x0;
volatile ee_s32 seed3_volatile = 0x66;
#endif
#if PROFILE_RUN
volatile ee_s32 seed1_volatile = 0x8;
volatile ee_s32 seed2_volatile = 0x8;
volatile ee_s32 seed3_volatile = 0x8;
#endif
volatile ee_s32 seed4_volatile = ITERATIONS;
volatile ee_s32 seed5_volatile = 0;

static CORE_TICKS start_time_val;
static CORE_TICKS stop_time_val;

static volatile core_results *g_parallel_results[MULTITHREAD];
static volatile ee_u32 g_parallel_submit_count;
static volatile ee_u32 g_parallel_collect_count;
static volatile ee_u32 g_parallel_generation;
static volatile ee_u32 g_worker_done_generation[MULTITHREAD];

static sint8 coreIdToSlot(IfxCpu_Id coreId)
{
    uint32 rawCoreId = (uint32)coreId;

    if (rawCoreId == 0U)
    {
        return 0;
    }

#if (MULTITHREAD > 1)
    if (rawCoreId == 1U)
    {
        return 1;
    }
#endif
#if (MULTITHREAD > 2)
    if (rawCoreId == 2U)
    {
        return 2;
    }
#endif
#if (MULTITHREAD > 3)
    if (rawCoreId == 3U)
    {
        return 3;
    }
#endif
#if (MULTITHREAD > 4)
    if (rawCoreId == 4U)
    {
        return 4;
    }
#endif
#if (MULTITHREAD > 5)
    if ((rawCoreId == 5U) || (rawCoreId == 6U))
    {
        return 5;
    }
#endif

    return -1;
}

static boolean areAllWorkersDone(ee_u32 generation)
{
    ee_u32 slot;

    for (slot = 1U; slot < (ee_u32)MULTITHREAD; ++slot)
    {
        if (g_worker_done_generation[slot] != generation)
        {
            return FALSE;
        }
    }

    return TRUE;
}

void start_time(void)
{
    start_time_val = now();
}

void stop_time(void)
{
    stop_time_val = now();
}

CORE_TICKS get_time(void)
{
    return (CORE_TICKS)(stop_time_val - start_time_val);
}

secs_ret time_in_secs(CORE_TICKS ticks)
{
    return ((secs_ret)ticks) / (secs_ret)TimeConst_1s;
}

void *portable_malloc(ee_size_t size)
{
    return malloc(size);
}

void portable_free(void *p)
{
    free(p);
}

ee_u32 default_num_contexts = MULTITHREAD;

void portable_init(core_portable *p, int *argc, char *argv[])
{
    (void)argc;
    (void)argv;

    initTime();

    if ((uint32)IfxCpu_getCoreId() == 0U)
    {
        SERIALIO_Init(COREMARK_UART_BAUDRATE);
        printf("\r\n=== TC4D7 CoreMark (%u thread) ===\r\n", (unsigned int)MULTITHREAD);
    }

    if (sizeof(ee_ptr_int) != sizeof(ee_u8 *))
    {
        ee_printf("ERROR! Please define ee_ptr_int to hold a pointer!\n");
    }

    if (sizeof(ee_u32) != 4)
    {
        ee_printf("ERROR! Please define ee_u32 as a 32-bit unsigned type!\n");
    }

    p->portable_id = 1;
}

void portable_fini(core_portable *p)
{
    p->portable_id = 0;
}

void coremark_worker_entry(void)
{
#if (MULTITHREAD > 1)
    ee_u32 seen_generation = g_parallel_generation;
    sint8 slot = coreIdToSlot(IfxCpu_getCoreId());

    if (slot < 1)
    {
        while (1)
        {
            __nop();
        }
    }

    while (1)
    {
        while (g_parallel_generation == seen_generation)
        {
            __nop();
        }

        seen_generation = g_parallel_generation;

        if ((ee_u32)slot < (ee_u32)MULTITHREAD)
        {
            iterate((void *)g_parallel_results[slot]);
            g_worker_done_generation[slot] = seen_generation;
        }
    }
#else
    while (1)
    {
        __nop();
    }
#endif
}

#if (MULTITHREAD > 1)
ee_u8 core_start_parallel(core_results *res)
{
    ee_u32 submit_slot = g_parallel_submit_count;

    if (submit_slot >= (ee_u32)MULTITHREAD)
    {
        return 1;
    }

    g_parallel_results[submit_slot] = res;
    g_parallel_submit_count = submit_slot + 1U;

    if (g_parallel_submit_count == (ee_u32)MULTITHREAD)
    {
        ee_u32 slot;
        ee_u32 generation = g_parallel_generation + 1U;

        for (slot = 1U; slot < (ee_u32)MULTITHREAD; ++slot)
        {
            g_worker_done_generation[slot] = 0U;
        }

        g_parallel_generation = generation;

        iterate((void *)g_parallel_results[0]);
    }

    return 0;
}

ee_u8 core_stop_parallel(core_results *res)
{
    (void)res;

    if (g_parallel_collect_count == 0U)
    {
        ee_u32 start = now();

        while (!areAllWorkersDone(g_parallel_generation))
        {
            if ((now() - start) > (20U * TimeConst_1s))
            {
                ee_printf("ERROR: multicore synchronization timeout\n");
                break;
            }
        }
    }

    g_parallel_collect_count++;

    if (g_parallel_collect_count == (ee_u32)MULTITHREAD)
    {
        g_parallel_collect_count = 0U;
        g_parallel_submit_count = 0U;
    }

    return 0;
}
#endif
