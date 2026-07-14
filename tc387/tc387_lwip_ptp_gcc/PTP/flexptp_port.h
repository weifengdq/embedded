#ifndef FLEXPTP_PORT_H
#define FLEXPTP_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "flexptp/event.h"
#include "flexptp/timeutils.h"

typedef int (*flexptp_cli_callback_t)(const char **ppArgs, uint8_t argc);

int  flexptp_printf(const char *format, ...);
int  flexptp_cli_register_command(const char *cmd_hintline,
                                  uint8_t n_cmd,
                                  uint8_t n_min_arg,
                                  flexptp_cli_callback_t callback);
void flexptp_cli_remove_command(int id);

void     ptphw_pps_init_gpio(void);
void     ptphw_pps_enable(int enable);
void     ptphw_pps_toggle(void);
void     ptphw_pps_poll_fast(void);
void     ptphw_pps_sw_init(void);
void     ptphw_pps_sw_poll(void);
int      ptphw_pps_is_enabled(void);
void     ptphw_pps_set_freq_duty(uint32_t freq_hz, uint32_t duty_pct);
uint32_t ptphw_pps_get_freq(void);
uint32_t ptphw_pps_get_duty(void);

void flexptp_osless_lock(bool lock);

void ptphw_init(uint32_t increment, uint32_t addend);
void ptphw_gettime(TimestampU *time_value);
void flexptp_ptp_set_clock(int64_t seconds, int32_t nanoseconds);
void flexptp_ptp_set_addend(uint32_t addend);

void flexptp_user_event_cb(PtpUserEventCode event_code);

#ifdef __cplusplus
}
#endif

#endif
