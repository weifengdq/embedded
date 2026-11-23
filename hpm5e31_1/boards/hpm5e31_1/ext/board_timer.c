/*
 * tp-timer 域板级支撑: 只有版本号, 同时保证 boards/hpm5e31_1/ext/ 目录
 * 出现在所有例程的头文件搜索路径里(CMakeLists 的 file(GLOB ext 目录下的 .c) 分支)。
 */
#include "board_timer.h"

const char *board_timer_ext_version(void)
{
    return BOARD_TIMER_EXT_VERSION;
}