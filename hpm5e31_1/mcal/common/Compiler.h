/*
 * Compiler.h —— AUTOSAR 编译器抽象（GCC for RISC-V 实现）
 *
 * 依据：AUTOSAR_CP_SWS_BSWGeneral §7.1（Compiler abstraction）。
 * 说明：本工程不做内存保护（无 OS-Application 分区），因此所有 memclass/ptrclass
 * 参数都是"占位"，宏展开为空。若将来接入 MPU/OS，只需改这里。
 */
#ifndef COMPILER_H
#define COMPILER_H

#define COMPILER_AR_RELEASE_MAJOR_VERSION    (4u)
#define COMPILER_AR_RELEASE_MINOR_VERSION    (4u)
#define COMPILER_AR_RELEASE_REVISION_VERSION (0u)
#define COMPILER_SW_MAJOR_VERSION            (1u)
#define COMPILER_SW_MINOR_VERSION            (0u)
#define COMPILER_SW_PATCH_VERSION            (0u)

/* 存储类 */
#define AUTOMATIC
#define TYPEDEF     typedef
#define STATIC      static
#define CONST       const

/* 函数/指针/变量抽象 */
#define FUNC(rettype, memclass)                     rettype
#define FUNC_P2CONST(rettype, ptrclass, memclass)   const rettype *
#define FUNC_P2VAR(rettype, ptrclass, memclass)     rettype *

#define P2VAR(ptrtype, memclass, ptrclass)          ptrtype *
#define P2CONST(ptrtype, memclass, ptrclass)        const ptrtype *
#define CONSTP2VAR(ptrtype, memclass, ptrclass)     ptrtype * const
#define CONSTP2CONST(ptrtype, memclass, ptrclass)   const ptrtype * const
#define P2FUNC(rettype, ptrclass, fctname)          rettype (*fctname)
#define CONSTP2FUNC(rettype, ptrclass, fctname)     rettype (* const fctname)

#define VAR(vartype, memclass)                      vartype

/* 内联 */
#define INLINE          static inline
#define LOCAL_INLINE    static inline

/* 未使用参数（只在调试/裁剪代码里用） */
#define MCAL_UNUSED(x)  ((void)(x))

/* 段/对齐（后续需要时集中在这里改） */
#define MCAL_SECTION(x) __attribute__((section(x)))
#define MCAL_ALIGN(x)   __attribute__((aligned(x)))
#define MCAL_WEAK       __attribute__((weak))

#endif /* COMPILER_H */
