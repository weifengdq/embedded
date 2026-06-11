/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <stdlib.h>

static unsigned long fatal_probe_read_csr(const char *csr_name)
{
    unsigned long value = 0;

    if (csr_name[0] == 'm' && csr_name[1] == 'c') {
        __asm volatile("csrr %0, mcause" : "=r"(value));
    } else if (csr_name[0] == 'm' && csr_name[1] == 'e') {
        __asm volatile("csrr %0, mepc" : "=r"(value));
    } else if (csr_name[0] == 'm' && csr_name[1] == 't') {
        __asm volatile("csrr %0, mtval" : "=r"(value));
    } else if (csr_name[0] == 'm' && csr_name[1] == 's') {
        __asm volatile("csrr %0, mstatus" : "=r"(value));
    }

    return value;
}

__attribute__((noreturn))
static void fatal_probe_halt(void)
{
    for (;;) {
    }
}

void _exit(int status)
{
    printf("fatal: _exit(%d)\n", status);
    fatal_probe_halt();
}

void abort(void)
{
    printf("fatal: abort()\n");
    fatal_probe_halt();
}

void __stack_chk_fail(void)
{
    printf("fatal: __stack_chk_fail()\n");
    fatal_probe_halt();
}

void __assert_func(const char *file, int line, const char *func, const char *expr)
{
    printf("fatal: assert file=%s line=%d func=%s expr=%s\n",
           file != NULL ? file : "?",
           line,
           func != NULL ? func : "?",
           expr != NULL ? expr : "?");
    fatal_probe_halt();
}

long exception_handler(long cause, long epc)
{
    printf("fatal: exception cause=0x%08lx epc=0x%08lx mtval=0x%08lx mstatus=0x%08lx\n",
           (unsigned long) cause,
           (unsigned long) epc,
           fatal_probe_read_csr("mtval"),
           fatal_probe_read_csr("mstatus"));
    fatal_probe_halt();
    return epc;
}

long exception_s_handler(long cause, long epc)
{
    printf("fatal: s_exception cause=0x%08lx epc=0x%08lx mtval=0x%08lx mstatus=0x%08lx\n",
           (unsigned long) cause,
           (unsigned long) epc,
           fatal_probe_read_csr("mtval"),
           fatal_probe_read_csr("mstatus"));
    fatal_probe_halt();
    return epc;
}
