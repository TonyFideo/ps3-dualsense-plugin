CELL_SDK ?= C:/usr/local/cell
CELL_MK_DIR ?= $(CELL_SDK)/samples/mk

include $(CELL_MK_DIR)/sdk.makedef.mk

PPU_SRCS = source/main.c \
           source/diagnostics.c \
           source/dualsense_usb.c
PPU_PRX_TARGET = dualsense_fix.prx

PPU_OPTIMIZE_LV = -O2
PPU_CSTDFLAGS = -std=gnu99 -ffreestanding -fno-builtin \
                 -fno-stack-protector -ffunction-sections -fdata-sections
PPU_CWARNFLAGS = -Wall -Wextra -Werror
PPU_INCDIRS = -Iinclude

PPU_PRX_LDFLAGS += -zgc-sections -nodefaultlibs
PPU_PRX_LDLIBS = -Wl,--start-group \
                  -lusbd_stub -lio_stub -lfs_stub \
                  -llv2_stub -lgcc \
                  -Wl,--end-group

include $(CELL_MK_DIR)/sdk.target.mk
