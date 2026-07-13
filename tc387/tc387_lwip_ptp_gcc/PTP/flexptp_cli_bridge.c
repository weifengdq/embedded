#include "flexptp_app.h"
#include "flexptp_port.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FLEXPTP_CLI_MAX_COMMANDS 48U

typedef struct
{
    const char *hint;
    uint8_t n_cmd;
    uint8_t n_min_arg;
    uint8_t active;
    flexptp_cli_callback_t callback;
} flexptp_cli_command_t;

static flexptp_cli_command_t gFlexPtpCliCommands[FLEXPTP_CLI_MAX_COMMANDS];
static long gTaiUtcOffsetSec = 37L;

static const char *flexptp_cli_hint_end(const char *hint)
{
    const char *cursor = hint;

    while ((*cursor != '\0') && (*cursor != '\t'))
    {
        cursor++;
    }

    return cursor;
}

static int flexptp_cli_hints_equal(const char *left, const char *right)
{
    const char *leftEnd = flexptp_cli_hint_end(left);
    const char *rightEnd = flexptp_cli_hint_end(right);
    size_t leftLen = (size_t)(leftEnd - left);
    size_t rightLen = (size_t)(rightEnd - right);

    return (leftLen == rightLen) && (strncmp(left, right, leftLen) == 0);
}

static void flexptp_cli_print_usage(const flexptp_cli_command_t *command)
{
    const char *usageEnd = flexptp_cli_hint_end(command->hint);

    flexptp_printf("%.*s\n", (int)(usageEnd - command->hint), command->hint);
}

static int flexptp_cli_match(const flexptp_cli_command_t *command, int argc, char *argv[])
{
    const char *cursor = command->hint;
    uint8_t tokenIndex;

    if ((command == NULL) || (argc < (int)command->n_cmd))
    {
        return 0;
    }

    for (tokenIndex = 0U; tokenIndex < command->n_cmd; ++tokenIndex)
    {
        const char *tokenStart;
        size_t tokenLen;

        while (*cursor == ' ')
        {
            cursor++;
        }

        if ((*cursor == '\0') || (*cursor == '\t'))
        {
            return 0;
        }

        tokenStart = cursor;
        while ((*cursor != '\0') && (*cursor != ' ') && (*cursor != '\t'))
        {
            cursor++;
        }

        tokenLen = (size_t)(cursor - tokenStart);
        if ((strlen(argv[tokenIndex]) != tokenLen) ||
            (strncmp(tokenStart, argv[tokenIndex], tokenLen) != 0))
        {
            return 0;
        }
    }

    return 1;
}

static void flexptp_cli_print_wrapper_help(void)
{
    flexptp_printf("ptp on|gptp|off|status|help|-h\n");
    flexptp_printf("  ptp on          start default PTP (E2E over L2, SLAVE_ONLY)\n");
    flexptp_printf("  ptp gptp        start gPTP (802.1AS P2P over L2, SLAVE_ONLY)\n");
    flexptp_printf("  ptp off         stop PTP/gPTP\n");
    flexptp_printf("  ptp status      show PTP mode and profile\n");
    flexptp_printf("  ptp info        show own and master clock identity\n");
    flexptp_printf("  ptp help        show this help\n");
    flexptp_printf("  ptp -h          same as ptp help\n");
    FlexPTP_PrintCommands("ptp ");
    flexptp_printf("time [ns|utc|local|+-H|offset N|-h]\n");
    flexptp_printf("  time            PTP time in s.ns (TAI)\n");
    flexptp_printf("  time ns         PTP time as 'sec ns'\n");
    flexptp_printf("  time utc        UTC datetime YYYY-MM-DD HH:MM:SS.ns\n");
    flexptp_printf("  time local      local UTC+8 datetime\n");
    flexptp_printf("  time +H|-H      datetime at given offset\n");
    flexptp_printf("  time offset N   set TAI-UTC offset (default 37)\n");
    flexptp_printf("  time -h         show this help\n");
    flexptp_printf("ptp pps [on|off|freq N|duty N|-h]\n");
    flexptp_printf("  ptp pps on/off  enable/disable P14.4 PPS output\n");
    flexptp_printf("  ptp pps freq N  set frequency Hz (default 1)\n");
    flexptp_printf("  ptp pps duty N  set duty cycle %% (default 50)\n");
    FlexPTP_PrintCommands("time");
}

int flexptp_cli_register_command(const char *cmd_hintline,
                                 uint8_t n_cmd,
                                 uint8_t n_min_arg,
                                 flexptp_cli_callback_t callback)
{
    uint32_t index;

    if ((cmd_hintline == NULL) || (callback == NULL) || (n_cmd == 0U))
    {
        return -1;
    }

    for (index = 0U; index < FLEXPTP_CLI_MAX_COMMANDS; ++index)
    {
        if (gFlexPtpCliCommands[index].active &&
            (gFlexPtpCliCommands[index].n_cmd == n_cmd) &&
            (gFlexPtpCliCommands[index].callback == callback) &&
            flexptp_cli_hints_equal(gFlexPtpCliCommands[index].hint, cmd_hintline))
        {
            return (int)(index + 1U);
        }
    }

    for (index = 0U; index < FLEXPTP_CLI_MAX_COMMANDS; ++index)
    {
        if (!gFlexPtpCliCommands[index].active)
        {
            gFlexPtpCliCommands[index].hint = cmd_hintline;
            gFlexPtpCliCommands[index].n_cmd = n_cmd;
            gFlexPtpCliCommands[index].n_min_arg = n_min_arg;
            gFlexPtpCliCommands[index].callback = callback;
            gFlexPtpCliCommands[index].active = 1U;
            return (int)(index + 1U);
        }
    }

    flexptp_printf("flexPTP CLI registry full, cannot add: %s\n", cmd_hintline);
    return -1;
}

void flexptp_cli_remove_command(int id)
{
    if ((id > 0) && (id <= (int)FLEXPTP_CLI_MAX_COMMANDS))
    {
        memset(&gFlexPtpCliCommands[id - 1], 0, sizeof(gFlexPtpCliCommands[0]));
    }
}

void FlexPTP_PrintCommands(const char *prefix)
{
    uint32_t index;
    size_t prefixLen = (prefix != NULL) ? strlen(prefix) : 0U;

    for (index = 0U; index < FLEXPTP_CLI_MAX_COMMANDS; ++index)
    {
        if (!gFlexPtpCliCommands[index].active)
        {
            continue;
        }

        if ((prefixLen == 0U) || (strncmp(gFlexPtpCliCommands[index].hint, prefix, prefixLen) == 0))
        {
            flexptp_cli_print_usage(&gFlexPtpCliCommands[index]);
        }
    }
}

int FlexPTP_DispatchCommand(int argc, char *argv[])
{
    uint32_t index;

    for (index = 0U; index < FLEXPTP_CLI_MAX_COMMANDS; ++index)
    {
        int result;
        flexptp_cli_command_t *command = &gFlexPtpCliCommands[index];

        if (!command->active || !flexptp_cli_match(command, argc, argv))
        {
            continue;
        }

        if ((argc - (int)command->n_cmd) < (int)command->n_min_arg)
        {
            flexptp_cli_print_usage(command);
            return -1;
        }

        result = command->callback((const char **)&argv[command->n_cmd], (uint8_t)(argc - (int)command->n_cmd));
        if (result < 0)
        {
            flexptp_cli_print_usage(command);
        }
        return result;
    }

    flexptp_printf("Unknown flexPTP command\n");
    return -1;
}

static int time_parse_offset(const char *arg, long *offset_sec)
{
    char *end = NULL;
    long val;

    if ((arg == NULL) || (offset_sec == NULL) || (*arg == '\0'))
    {
        return -1;
    }

    val = strtol(arg, &end, 10);
    if ((end == NULL) || (*end != '\0'))
    {
        return -1;
    }

    *offset_sec = val * 3600L;
    return 0;
}

static void time_fmt_datetime(uint32_t unix_sec,
                              uint32_t nanosec,
                              long tz_offset_sec,
                              char *buf,
                              size_t buf_size)
{
    static const uint8_t days_in_month[12] =
    {
        31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U
    };
    uint32_t day_sec;
    uint32_t sec_in_day;
    uint32_t year;
    uint32_t month;
    uint32_t day;
    uint32_t hour;
    uint32_t minute;
    uint32_t second;
    uint8_t is_leap;
    uint32_t days_per_year;
    uint32_t total_sec;
    int32_t signed_total;

    if ((buf == NULL) || (buf_size == 0U))
    {
        return;
    }

    signed_total = (int32_t)unix_sec + (int32_t)tz_offset_sec;
    if (signed_total < 0)
    {
        signed_total = 0;
    }

    total_sec = (uint32_t)signed_total;
    day_sec = total_sec / 86400UL;
    sec_in_day = total_sec - (day_sec * 86400UL);
    second = sec_in_day % 60U;
    sec_in_day /= 60U;
    minute = sec_in_day % 60U;
    hour = sec_in_day / 60U;

    year = 1970U;
    while (1U)
    {
        is_leap = ((year % 4U) == 0U && ((year % 100U) != 0U || (year % 400U) == 0U)) ? 1U : 0U;
        days_per_year = 365U + (uint32_t)is_leap;
        if (day_sec < days_per_year)
        {
            break;
        }
        day_sec -= days_per_year;
        year++;
    }

    month = 0U;
    while (month < 12U)
    {
        uint32_t dim = days_in_month[month];
        if ((month == 1U) && (is_leap != 0U))
        {
            dim = 29U;
        }
        if (day_sec < dim)
        {
            break;
        }
        day_sec -= dim;
        month++;
    }

    day = day_sec + 1U;
    month++;

    (void)snprintf(buf, buf_size,
                   "%04lu-%02lu-%02lu %02lu:%02lu:%02lu.%09lu",
                   (unsigned long)year,
                   (unsigned long)month,
                   (unsigned long)day,
                   (unsigned long)hour,
                   (unsigned long)minute,
                   (unsigned long)second,
                   (unsigned long)nanosec);
}

int FlexPTP_ShellPtp(int argc, char *argv[])
{
    if ((argc == 1) || (strcmp(argv[1], "status") == 0))
    {
        FlexPTP_PrintStatus();
        return 0;
    }

    if (strcmp(argv[1], "on") == 0)
    {
        return FlexPTP_Start();
    }

    if (strcmp(argv[1], "gptp") == 0)
    {
        return FlexPTP_StartGptp();
    }

    if (strcmp(argv[1], "off") == 0)
    {
        return FlexPTP_Stop();
    }

    if ((strcmp(argv[1], "help") == 0) ||
        (strcmp(argv[1], "-h") == 0) ||
        (strcmp(argv[1], "--help") == 0))
    {
        flexptp_cli_print_wrapper_help();
        return 0;
    }

    if (!FlexPTP_IsRunning())
    {
        flexptp_printf("flexPTP is stopped, use 'ptp on' or 'ptp gptp' first\n");
        return -1;
    }

    if (strcmp(argv[1], "pps") == 0)
    {
        if (argc < 3)
        {
            flexptp_printf("PPS=%s freq=%luHz duty=%lu%%\n",
                           ptphw_pps_is_enabled() ? "on" : "off",
                           (unsigned long)ptphw_pps_get_freq(),
                           (unsigned long)ptphw_pps_get_duty());
            return 0;
        }

        if (strcmp(argv[2], "on") == 0)
        {
            ptphw_pps_enable(1);
            return 0;
        }

        if (strcmp(argv[2], "off") == 0)
        {
            ptphw_pps_enable(0);
            return 0;
        }

        if ((strcmp(argv[2], "-h") == 0) || (strcmp(argv[2], "help") == 0))
        {
            flexptp_printf("ptp pps [on|off|freq N|duty N|-h]\n");
            flexptp_printf("  ptp pps         show PPS status\n");
            flexptp_printf("  ptp pps on/off  enable/disable P14.4 PPS output\n");
            flexptp_printf("  ptp pps freq N  set frequency 1-10000 Hz\n");
            flexptp_printf("  ptp pps duty N  set duty cycle 1-99 %%\n");
            return 0;
        }

        if ((strcmp(argv[2], "freq") == 0) && (argc >= 4))
        {
            uint32_t freq = (uint32_t)strtoul(argv[3], NULL, 10);
            ptphw_pps_set_freq_duty(freq, ptphw_pps_get_duty());
            if (ptphw_pps_is_enabled())
            {
                ptphw_pps_enable(1);
            }
            flexptp_printf("PPS freq=%luHz\n", (unsigned long)ptphw_pps_get_freq());
            return 0;
        }

        if ((strcmp(argv[2], "duty") == 0) && (argc >= 4))
        {
            uint32_t duty = (uint32_t)strtoul(argv[3], NULL, 10);
            ptphw_pps_set_freq_duty(ptphw_pps_get_freq(), duty);
            if (ptphw_pps_is_enabled())
            {
                ptphw_pps_enable(1);
            }
            flexptp_printf("PPS duty=%lu%%\n", (unsigned long)ptphw_pps_get_duty());
            return 0;
        }

        flexptp_printf("usage: ptp pps [on|off|freq N|duty N|-h]\n");
        return 0;
    }

    return FlexPTP_DispatchCommand(argc, argv);
}

int FlexPTP_ShellTime(int argc, char *argv[])
{
    TimestampU timestamp;
    long tz_offset;
    int32_t unix_sec;
    char buf[64];

    if (!FlexPTP_IsRunning())
    {
        flexptp_printf("flexPTP is stopped, use 'ptp on' or 'ptp gptp' first\n");
        return -1;
    }

    if ((argc >= 2) && ((strcmp(argv[1], "-h") == 0) ||
                        (strcmp(argv[1], "--help") == 0) ||
                        (strcmp(argv[1], "help") == 0)))
    {
        flexptp_cli_print_wrapper_help();
        return 0;
    }

    if (argc == 1)
    {
        ptphw_gettime(&timestamp);
        flexptp_printf("%lu.%09lu s (TAI-UTC=%ld s)\n",
                       (unsigned long)timestamp.sec,
                       (unsigned long)timestamp.nanosec,
                       gTaiUtcOffsetSec);
        return 0;
    }

    if ((argc == 2) && (strcmp(argv[1], "ns") == 0))
    {
        ptphw_gettime(&timestamp);
        flexptp_printf("%lu %lu\n",
                       (unsigned long)timestamp.sec,
                       (unsigned long)timestamp.nanosec);
        return 0;
    }

    if ((argc >= 3) && (strcmp(argv[1], "offset") == 0))
    {
        long val = strtol(argv[2], NULL, 10);
        gTaiUtcOffsetSec = val;
        flexptp_printf("TAI-UTC offset set to %ld s\n", gTaiUtcOffsetSec);
        return 0;
    }

    if ((argc == 2) && (strcmp(argv[1], "offset") == 0))
    {
        flexptp_printf("TAI-UTC offset = %ld s\n", gTaiUtcOffsetSec);
        return 0;
    }

    ptphw_gettime(&timestamp);

    if ((argc == 2) && (strcmp(argv[1], "utc") == 0))
    {
        tz_offset = 0L;
    }
    else if ((argc == 2) && (strcmp(argv[1], "local") == 0))
    {
        tz_offset = 8L * 3600L;
    }
    else if ((argc == 2) && (time_parse_offset(argv[1], &tz_offset) == 0))
    {
    }
    else
    {
        return FlexPTP_DispatchCommand(argc, argv);
    }

    unix_sec = (int32_t)timestamp.sec - (int32_t)gTaiUtcOffsetSec;
    time_fmt_datetime((uint32_t)unix_sec, timestamp.nanosec, tz_offset, buf, sizeof(buf));
    flexptp_printf("%s\n", buf);
    return 0;
}
