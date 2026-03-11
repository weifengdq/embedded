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
#include <stdint.h>

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

#if (MEM_METHOD == MEM_MALLOC) && (MULTITHREAD > 1)
#define COREMARK_LMU_POOL_SIZE (256U * 1024U)
static uint8 __attribute__((section(".coremark_lmu"), aligned(64)))
    g_coremarkLmuPool[COREMARK_LMU_POOL_SIZE];
static ee_size_t __attribute__((section(".lmubss"))) g_coremarkLmuPoolOffset;
#endif

static volatile core_results * __attribute__((section(".lmubss")))
    g_parallel_results[MULTITHREAD];
static volatile ee_u32 __attribute__((section(".lmubss")))
    g_parallel_submit_count;
static volatile ee_u32 __attribute__((section(".lmubss")))
    g_parallel_collect_count;
static volatile ee_u32 __attribute__((section(".lmubss")))
    g_parallel_generation;

#if (MULTITHREAD > 1)
#define COREMARK_WORKER_MAILBOX_SEEN      0U
#define COREMARK_WORKER_MAILBOX_DONE      1U
#define COREMARK_WORKER_MAILBOX_CRCLIST   2U
#define COREMARK_WORKER_MAILBOX_CRCMATRIX 3U
#define COREMARK_WORKER_MAILBOX_CRCSTATE  4U
#define COREMARK_WORKER_MAILBOX_CRCFINAL  5U

static const unsigned int g_workerDsprBase[6] = {
    0x70000000U, 0x60000000U, 0x50000000U,
    0x40000000U, 0x30000000U, 0x20000000U
};

static volatile ee_u32 *getWorkerMailbox(ee_u32 slot)
{
    return (volatile ee_u32 *)g_workerDsprBase[slot];
}

static volatile ee_u32 *getLocalWorkerMailbox(void)
{
    return (volatile ee_u32 *)0xD0000000U;
}

static void clearWorkerMailbox(ee_u32 slot)
{
    volatile ee_u32 *mailbox = getWorkerMailbox(slot);

    mailbox[COREMARK_WORKER_MAILBOX_SEEN] = 0U;
    mailbox[COREMARK_WORKER_MAILBOX_DONE] = 0U;
    mailbox[COREMARK_WORKER_MAILBOX_CRCLIST] = 0U;
    mailbox[COREMARK_WORKER_MAILBOX_CRCMATRIX] = 0U;
    mailbox[COREMARK_WORKER_MAILBOX_CRCSTATE] = 0U;
    mailbox[COREMARK_WORKER_MAILBOX_CRCFINAL] = 0U;
}
#endif

#if (MULTITHREAD > 1)
static void prepareLocalWorkerResults(const volatile core_results *shared,
                                      core_results *local,
                                      ee_u8 *localMem)
{
    ee_u32 i;
    ee_u32 j = 0U;

    *local = *shared;
    local->list = NULL;
    local->mat.A = NULL;
    local->mat.B = NULL;
    local->mat.C = NULL;
    local->crc = 0U;
    local->crclist = 0U;
    local->crcmatrix = 0U;
    local->crcstate = 0U;
    local->err = 0;

    local->memblock[0] = localMem;

    for (i = 0U; i < NUM_ALGORITHMS; ++i)
    {
        if (((ee_u32)1U << i) & local->execs)
        {
            local->memblock[i + 1U] = (void *)(localMem + (local->size * j));
            ++j;
        }
        else
        {
            local->memblock[i + 1U] = NULL;
        }
    }

    if (local->execs & ID_LIST)
    {
        local->list = core_list_init(local->size,
                                     (list_head *)local->memblock[1],
                                     local->seed1);
    }

    if (local->execs & ID_MATRIX)
    {
        core_init_matrix(local->size,
                         local->memblock[2],
                         (ee_s32)local->seed1 | (((ee_s32)local->seed2) << 16),
                         &local->mat);
    }

    if (local->execs & ID_STATE)
    {
        core_init_state(local->size,
                        local->seed1,
                        (ee_u8 *)local->memblock[3]);
    }
}

static void runLocalTemplate(const volatile core_results *shared,
                             core_results *resultSink)
{
    ee_u8 localMem[TOTAL_DATA_SIZE] __attribute__((aligned(64)));
    core_results localResults;

    prepareLocalWorkerResults(shared, &localResults, localMem);
    iterate(&localResults);

    if (resultSink != NULL)
    {
        resultSink->crc = localResults.crc;
        resultSink->crclist = localResults.crclist;
        resultSink->crcmatrix = localResults.crcmatrix;
        resultSink->crcstate = localResults.crcstate;
        resultSink->err = localResults.err;
    }
}

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
        if (getWorkerMailbox(slot)[COREMARK_WORKER_MAILBOX_DONE] != generation)
        {
            return FALSE;
        }
    }

    return TRUE;
}
#endif

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
#if (MEM_METHOD == MEM_MALLOC) && (MULTITHREAD > 1)
    ee_size_t alignedSize = (size + 63U) & ~(ee_size_t)63U;
    ee_size_t current = g_coremarkLmuPoolOffset;

    if ((current + alignedSize) > (ee_size_t)COREMARK_LMU_POOL_SIZE)
    {
        return NULL;
    }

    g_coremarkLmuPoolOffset = current + alignedSize;
    return (void *)&g_coremarkLmuPool[current];
#else
    return malloc(size);
#endif
}

void portable_free(void *p)
{
#if (MEM_METHOD == MEM_MALLOC) && (MULTITHREAD > 1)
    (void)p;
#else
    free(p);
#endif
}

ee_u32 default_num_contexts = MULTITHREAD;

void portable_init(core_portable *p, int *argc, char *argv[])
{
    ee_u32 slot;

    (void)argc;
    (void)argv;

    initTime();

#if (MEM_METHOD == MEM_MALLOC) && (MULTITHREAD > 1)
    g_coremarkLmuPoolOffset = 0U;
#endif

    if ((uint32)IfxCpu_getCoreId() == 0U)
    {
        for (slot = 0U; slot < (ee_u32)MULTITHREAD; ++slot)
        {
            g_parallel_results[slot] = NULL;
#if (MULTITHREAD > 1)
            if (slot > 0U)
            {
                clearWorkerMailbox(slot);
            }
#endif
        }

        g_parallel_submit_count = 0U;
        g_parallel_collect_count = 0U;
        g_parallel_generation = 0U;

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
    ee_u32 seen_generation = 0U;
    sint8 slot = coreIdToSlot(IfxCpu_getCoreId());
    volatile ee_u32 *localMailbox = getLocalWorkerMailbox();

    if (slot < 1)
    {
        while (1)
        {
            __nop();
        }
    }

    while (1)
    {
        ee_u8 localMem[TOTAL_DATA_SIZE] __attribute__((aligned(64)));
        core_results localResults;

        while (g_parallel_generation == seen_generation)
        {
            __nop();
        }

        __dsync();
        seen_generation = g_parallel_generation;
        localMailbox[COREMARK_WORKER_MAILBOX_SEEN] = seen_generation;

        if ((ee_u32)slot < (ee_u32)MULTITHREAD)
        {
            volatile core_results *wres = g_parallel_results[slot];

            if (wres != NULL)
            {
                prepareLocalWorkerResults(wres, &localResults, localMem);

                iterate(&localResults);
                localMailbox[COREMARK_WORKER_MAILBOX_CRCLIST] = localResults.crclist;
                localMailbox[COREMARK_WORKER_MAILBOX_CRCMATRIX] = localResults.crcmatrix;
                localMailbox[COREMARK_WORKER_MAILBOX_CRCSTATE] = localResults.crcstate;
                localMailbox[COREMARK_WORKER_MAILBOX_CRCFINAL] = localResults.crc;
            }
            __dsync();
            localMailbox[COREMARK_WORKER_MAILBOX_DONE] = seen_generation;
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
            clearWorkerMailbox(slot);
        }

        __dsync();
        g_parallel_generation = generation;

        runLocalTemplate(g_parallel_results[0], (core_results *)g_parallel_results[0]);
    }

    return 0;
}

ee_u8 core_stop_parallel(core_results *res)
{
    (void)res;

    if (g_parallel_collect_count == 0U)
    {
        Ifx_TickTime start = now();
        Ifx_TickTime timeout = 120 * TimeConst_1s;

        while (!areAllWorkersDone(g_parallel_generation))
        {
            if ((now() - start) > timeout)
            {
                ee_printf("ERROR: multicore synchronization timeout\n");
                {
                    ee_u32 slot;

                    for (slot = 1U; slot < (ee_u32)MULTITHREAD; ++slot)
                    {
                        volatile ee_u32 *mailbox = getWorkerMailbox(slot);

                        ee_printf("  slot %u: seen=%lu done=%lu target=%lu\n",
                                  (unsigned int)slot,
                                  (long unsigned)mailbox[COREMARK_WORKER_MAILBOX_SEEN],
                                  (long unsigned)mailbox[COREMARK_WORKER_MAILBOX_DONE],
                                  (long unsigned)g_parallel_generation);
                    }
                }
                break;
            }
        }

        if (areAllWorkersDone(g_parallel_generation))
        {
            ee_u32 slot;

            for (slot = 1U; slot < (ee_u32)MULTITHREAD; ++slot)
            {
                volatile ee_u32 *mailbox = getWorkerMailbox(slot);
                volatile core_results *resSlot = g_parallel_results[slot];

                if (resSlot != NULL)
                {
                    resSlot->crclist = (ee_u16)mailbox[COREMARK_WORKER_MAILBOX_CRCLIST];
                    resSlot->crcmatrix = (ee_u16)mailbox[COREMARK_WORKER_MAILBOX_CRCMATRIX];
                    resSlot->crcstate = (ee_u16)mailbox[COREMARK_WORKER_MAILBOX_CRCSTATE];
                    resSlot->crc = (ee_u16)mailbox[COREMARK_WORKER_MAILBOX_CRCFINAL];
                }
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
