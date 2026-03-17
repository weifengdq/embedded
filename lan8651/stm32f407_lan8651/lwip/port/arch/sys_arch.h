/**
 * @file  sys_arch.h
 * @brief lwIP system architecture — NO_SYS=1 (bare-metal) stubs
 */
#ifndef LWIP_ARCH_SYS_ARCH_H
#define LWIP_ARCH_SYS_ARCH_H

/* With NO_SYS=1, lwIP does not need a real sys_arch.
 * We only need to provide sys_now() for timeouts. */

#define SYS_MBOX_NULL   NULL
#define SYS_SEM_NULL    NULL

typedef int sys_sem_t;
typedef int sys_mbox_t;
typedef int sys_thread_t;
typedef int sys_prot_t;

#endif /* LWIP_ARCH_SYS_ARCH_H */
