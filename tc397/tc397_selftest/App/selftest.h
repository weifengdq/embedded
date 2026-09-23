#ifndef SELFTEST_H
#define SELFTEST_H

/* Combined peripheral self-test framework for tc397_selftest.
 *
 * `selftest`       - automatic factory check over every on-board peripheral,
 *                    prints one line per item plus a PASS/FAIL/SKIP summary
 *                    and the list of failed peripherals.
 * `selftest <x>`   - run a single group (core|uart|adc|can|fr|tlf|sd|t1s|geth|net).
 * `selftest all`   - same as `selftest` but also the group detail lines.
 * `bench`          - throughput measurements (UART / CAN / FlexRay / SD).
 * `stat`           - live peripheral status summary without running tests.
 *
 * Network throughput (iperf) needs the PC side and is therefore reported as
 * SKIP by the automatic run; see README.md for the PC commands and the
 * measured numbers.
 */

#include "shell.h"
#include "Ifx_Types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** \brief Result of a single test item */
typedef enum
{
    Selftest_Pass = 0,
    Selftest_Fail,
    Selftest_Skip
} Selftest_Result;

/** \brief Automatic factory self-test (`selftest`). */
void Selftest_Run(Shell *sh, int deep);

/** \brief Show the peripheral status summary without running anything (`stat`). */
void Selftest_Status(Shell *sh);

/** \brief Throughput bench (`bench`). */
void Selftest_Bench(Shell *sh, int argc, char *argv[]);

/** \brief UDP statistics service on port 5002 (GETH diag) - called at boot. */
void Selftest_DiagUdpInit(void);

#ifdef __cplusplus
}
#endif

#endif /* SELFTEST_H */
