#ifndef DUALSENSE_DIAGNOSTICS_H
#define DUALSENSE_DIAGNOSTICS_H

#include "ds_types.h"

#define DS_LOG_PATH "/dev_hdd0/tmp/dualsense_fix.log"

void ds_diag_init(void);
void ds_diag_shutdown(void);
void ds_diag_trace(const char *message);
void ds_diag_trace_value(const char *message, s32 value);
void ds_diag_info(const char *message, s32 notify);
void ds_diag_error(const char *message, s32 error_code, s32 notify);
void ds_diag_usb_layout(u8 configuration, u8 interface_count,
                        u8 interface_number, u8 alternate_setting,
                        u8 input_address, u16 input_packet_size,
                        u8 input_interval, s32 output_address,
                        u16 output_packet_size, u8 output_interval,
                        s32 speed_result, u8 speed);
void ds_diag_ldd_registration(s32 register_result, s32 register_handle,
                              s32 insert_mode_result, u8 advanced_used,
                              u8 fallback_used);
void ds_diag_counters(const char *reason, u32 completed_reports,
                      u32 valid_reports, u32 successful_inserts,
                      u32 usb_errors, u32 insert_errors,
                      u64 elapsed_nanoseconds);

#endif
