#ifndef DUALSENSE_DIAGNOSTICS_H
#define DUALSENSE_DIAGNOSTICS_H

#include <ppu-types.h>

#define DS_LOG_PATH "/dev_hdd0/tmp/dualsense_fix.log"

void ds_diag_init(void);
void ds_diag_shutdown(void);
void ds_diag_info(const char *message, s32 notify);
void ds_diag_error(const char *message, s32 error_code, s32 notify);

#endif
