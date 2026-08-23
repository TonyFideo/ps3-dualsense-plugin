# PS3 PRX Makefile
TARGET		:= dualsense_fix
BUILD		:= build
SOURCE		:= source
INCLUDE		:= include

CFLAGS		:= -O2 -Wall -I$(INCLUDE) -I. -ffunction-sections -fdata-sections
CXXFLAGS	:= $(CFLAGS) -fno-exceptions -fno-rtti
LDFLAGS		:= -L. -Wl,--gc-sections -lusbd_stub -lio_stub -lfs_stub -lsysmodule_stub

OBJS		:= $(BUILD)/main.o $(BUILD)/dualsense_usb.o

# Asegurarse de que el SDK está seteado
ifeq ($(strip $(PSL1GHT)),)
$(error "Please set PSL1GHT in your environment")
endif

include $(PSL1GHT)/ppu_rules

all: $(TARGET).prx

$(TARGET).prx: $(TARGET).elf
	$(PRXGEN) $< $@

$(TARGET).elf: $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@

$(BUILD)/%.o: $(SOURCE)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD) $(TARGET).elf $(TARGET).prx *.sprx
