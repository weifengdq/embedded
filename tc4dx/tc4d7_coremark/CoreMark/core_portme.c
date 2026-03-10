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
static volatile ee_u32 __attribute__((section(".lmubss")))
    g_worker_done_generation[MULTITHREAD];
static volatile ee_u32 __attribute__((section(".lmubss")))
    g_worker_seen_generation[MULTITHREAD];
static volatile ee_u32 __attribute__((section(".lmubss")))
    g_worker_boot_stage[MULTITHREAD];
static volatile ee_u32 __attribute__((section(".lmubss")))
    g_worker_boot_coreid[MULTITHREAD];

static const unsigned int g_workerDsprBase[6] = {
    0x70000000U, 0x60000000U, 0x50000000U,
    0x40000000U, 0x30000000U, 0x20000000U
};

static volatile unsigned int *getWorkerDiag(ee_u32 slot)
{
    return (volatile unsigned int *)g_workerDsprBase[slot];
}

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

static void dumpWorkerCpuState(ee_u32 slot, volatile Ifx_CPU *cpu)
{
    /* Read diagnostic data from slave core DSPR via global address.
     * DSPR base addresses: CPU1=0x60000000 CPU2=0x50000000 CPU3=0x40000000
     *                      CPU4=0x30000000 CPU5=0x20000000
     */
    volatile unsigned int *diag = getWorkerDiag(slot);
    unsigned int ssw_step   = diag[0]; /* DIAG_DSPR_SSW_STEP   */
    unsigned int trap_class = diag[1]; /* DIAG_DSPR_TRAP_CLASS */
    unsigned int trap_id    = diag[2]; /* DIAG_DSPR_TRAP_ID    */
    unsigned int trap_addr  = diag[3]; /* DIAG_DSPR_TRAP_ADDR  */
    unsigned int trap_count = diag[4]; /* DIAG_DSPR_TRAP_COUNT */
    unsigned int trap_deadd = diag[5]; /* DIAG_DSPR_TRAP_DEADD */
    unsigned int trap_datr  = diag[6]; /* DIAG_DSPR_TRAP_DATR  */
    /* diag[7] is unused (gap from 0x1C) */
    unsigned int worker_res  = diag[8];  /* DIAG_DSPR_WORKER_RES  at 0x20 */
    unsigned int worker_list = diag[9];  /* DIAG_DSPR_WORKER_LIST at 0x24 */
    unsigned int worker_mb0  = diag[10]; /* DIAG_DSPR_WORKER_MB0  at 0x28 */
    unsigned int worker_mb1  = diag[11]; /* DIAG_DSPR_WORKER_MB1  at 0x2C */
    unsigned int worker_lnext = diag[12]; /* DIAG_DSPR_WORKER_LNEXT at 0x30 */
    unsigned int worker_linfo = diag[13]; /* DIAG_DSPR_WORKER_LINFO at 0x34 */
    unsigned int worker_ninfo = diag[14]; /* DIAG_DSPR_WORKER_NINFO at 0x38 */
    unsigned int worker_seen  = diag[15]; /* DIAG_DSPR_WORKER_SEEN at 0x3C */
    unsigned int worker_done  = diag[16]; /* DIAG_DSPR_WORKER_DONE at 0x40 */
    unsigned int worker_crclist = diag[17]; /* DIAG_DSPR_WORKER_CRCLIST */
    unsigned int worker_crcmat  = diag[18]; /* DIAG_DSPR_WORKER_CRCMAT */
    unsigned int worker_crcstate = diag[19]; /* DIAG_DSPR_WORKER_CRCSTATE */
    unsigned int worker_crcfinal = diag[20]; /* DIAG_DSPR_WORKER_CRCFINAL */

    ee_printf("          ssw_step=0x%02lx trap_class=%lu trap_id=%lu trap_addr=0x%08lx trap_count=%lu\n",
              (long unsigned)ssw_step,
              (long unsigned)trap_class,
              (long unsigned)trap_id,
              (long unsigned)trap_addr,
              (long unsigned)trap_count);
    ee_printf("          trap_deadd=0x%08lx trap_datr=0x%08lx\n",
              (long unsigned)trap_deadd,
              (long unsigned)trap_datr);
    ee_printf("          worker_res=0x%08lx list=0x%08lx mb0=0x%08lx mb1=0x%08lx\n",
              (long unsigned)worker_res,
              (long unsigned)worker_list,
              (long unsigned)worker_mb0,
              (long unsigned)worker_mb1);
    ee_printf("          list_next=0x%08lx list_info=0x%08lx next_info=0x%08lx\n",
              (long unsigned)worker_lnext,
              (long unsigned)worker_linfo,
              (long unsigned)worker_ninfo);
    ee_printf("          dspr_seen=%lu dspr_done=%lu crc=0x%04lx list=0x%04lx mat=0x%04lx state=0x%04lx\n",
              (long unsigned)worker_seen,
              (long unsigned)worker_done,
              (long unsigned)worker_crcfinal,
              (long unsigned)worker_crclist,
              (long unsigned)worker_crcmat,
              (long unsigned)worker_crcstate);
    ee_printf("          hra_pc=0x%08lx start_pc=0x%08lx boot_bhalt=%lu dbg_halt=%lu dbg_de=%lu dbg_susin=%lu dbg_susout=%lu\n",
              (long unsigned)cpu->HRA_PC.U,
              (long unsigned)((uint32)cpu->HRA_PC.B.PC << 1U),
              (long unsigned)cpu->HRA_BOOTCON.B.BHALT,
              (long unsigned)cpu->HRA_DBGSR.B.HALT,
              (long unsigned)cpu->HRA_DBGSR.B.DE,
              (long unsigned)cpu->HRA_DBGSR.B.SUSIN,
              (long unsigned)cpu->HRA_DBGSR.B.SUSOUT);
    (void)slot;
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
        if (getWorkerDiag(slot)[16] != generation)
        {
            return FALSE;
        }
    }

    return TRUE;
}

void coremark_worker_mark_boot(ee_u32 stage)
{
#if (MULTITHREAD > 1)
    sint8 slot = coreIdToSlot(IfxCpu_getCoreId());

    if ((slot >= 1) && ((ee_u32)slot < (ee_u32)MULTITHREAD))
    {
        g_worker_boot_coreid[slot] = (ee_u32)IfxCpu_getCoreId();
        g_worker_boot_stage[slot] = stage;
        __dsync();
    }
#else
    (void)stage;
#endif
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
            g_worker_done_generation[slot] = 0U;
            g_worker_seen_generation[slot] = 0U;
            g_worker_boot_stage[slot] = 0U;
            g_worker_boot_coreid[slot] = 0U;
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

    coremark_worker_mark_boot(2U);
    coremark_worker_mark_boot(3U);

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
        DIAG_DSPR_WORKER_SEEN = seen_generation;
        g_worker_boot_stage[slot] = 4U;

        if ((ee_u32)slot < (ee_u32)MULTITHREAD)
        {
            volatile core_results *wres = g_parallel_results[slot];
            DIAG_DSPR_WORKER_RES  = (unsigned int)wres;
            DIAG_DSPR_WORKER_LIST = (unsigned int)(wres ? wres->list : 0);
            DIAG_DSPR_WORKER_MB0  = (unsigned int)(wres ? wres->memblock[0] : 0);
            DIAG_DSPR_WORKER_MB1  = (unsigned int)(wres ? wres->memblock[1] : 0);

            if (wres != NULL)
            {
                prepareLocalWorkerResults(wres, &localResults, localMem);

                list_head *head = localResults.list;
                list_head *next = (head != NULL) ? head->next : NULL;
                list_data *headInfo = (head != NULL) ? head->info : NULL;
                list_data *nextInfo = (next != NULL) ? next->info : NULL;

                DIAG_DSPR_WORKER_LNEXT = (unsigned int)next;
                DIAG_DSPR_WORKER_LINFO = (unsigned int)headInfo;
                DIAG_DSPR_WORKER_NINFO = (unsigned int)nextInfo;

                if ((head == NULL) || (headInfo == NULL) || (next == NULL) || (nextInfo == NULL))
                {
                    DIAG_DSPR_WORKER_DONE = 0xBAD00000U | (unsigned int)slot;
                    while (1)
                    {
                        __nop();
                    }
                }

                iterate(&localResults);
                DIAG_DSPR_WORKER_CRCLIST = localResults.crclist;
                DIAG_DSPR_WORKER_CRCMAT = localResults.crcmatrix;
                DIAG_DSPR_WORKER_CRCSTATE = localResults.crcstate;
                DIAG_DSPR_WORKER_CRCFINAL = localResults.crc;
                DIAG_DSPR_WORKER_DONE = seen_generation;
            }
            __dsync();
            g_worker_done_generation[slot] = seen_generation;
            g_worker_boot_stage[slot] = 5U;
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
            g_worker_seen_generation[slot] = 0U;
        }

        __dsync();
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
                        volatile unsigned int *diag = getWorkerDiag(slot);
                        ee_printf("  slot %u: seen=%lu done=%lu target=%lu\n",
                                  (unsigned int)slot,
                                  (long unsigned)diag[15],
                                  (long unsigned)diag[16],
                                  (long unsigned)g_parallel_generation);
                        ee_printf("          boot_stage=%lu coreid=%lu\n",
                                  (long unsigned)g_worker_boot_stage[slot],
                                  (long unsigned)g_worker_boot_coreid[slot]);

                        switch (slot)
                        {
                        case 1U:
                            dumpWorkerCpuState(slot, &MODULE_CPU1);
                            break;
                        case 2U:
                            dumpWorkerCpuState(slot, &MODULE_CPU2);
                            break;
                        case 3U:
                            dumpWorkerCpuState(slot, &MODULE_CPU3);
                            break;
                        case 4U:
                            dumpWorkerCpuState(slot, &MODULE_CPU4);
                            break;
                        case 5U:
                            dumpWorkerCpuState(slot, &MODULE_CPU5);
                            break;
                        default:
                            break;
                        }
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
                volatile unsigned int *diag = getWorkerDiag(slot);
                volatile core_results *resSlot = g_parallel_results[slot];

                if (resSlot != NULL)
                {
                    resSlot->crclist = (ee_u16)diag[17];
                    resSlot->crcmatrix = (ee_u16)diag[18];
                    resSlot->crcstate = (ee_u16)diag[19];
                    resSlot->crc = (ee_u16)diag[20];
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
