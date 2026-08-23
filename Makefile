TARGET      := dualsense_fix
BUILD       := build
SOURCES     := source
INCLUDES    := include

ifeq ($(strip $(PS3DEV)),)
$(error PS3DEV is not defined)
endif
ifeq ($(strip $(PSL1GHT)),)
$(error PSL1GHT is not defined)
endif

CC          := $(PS3DEV)/ppu/bin/ppu-gcc
STRIP       := $(PS3DEV)/ppu/bin/ppu-strip
SPRX_LINKER := $(PS3DEV)/bin/sprxlinker
PRXGEN      := tools/prxgen.py
PREPARE_LD  := tools/prepare_linker_script.py
VALIDATE_PRX:= tools/validate_prx.py
VALIDATE_SELF:=tools/validate_self.py
MAKE_SPRX   ?= $(PS3DEV)/bin/make_sprx
CRT_SPRX    := $(PS3DEV)/ppu/powerpc64-ps3-elf/lib/lv2-sprx.o
LV2_LDSCRIPT:= $(PS3DEV)/ppu/powerpc64-ps3-elf/lib/lv2.ld
HOST_CC     ?= cc
HOST_TEST   := $(BUILD)/test_usb_descriptor_parser

CFILES      := $(wildcard $(SOURCES)/*.c)
OFILES      := $(patsubst $(SOURCES)/%.c,$(BUILD)/%.o,$(CFILES))

CFLAGS      := -O2 -Wall -Wextra -Werror -std=gnu11 -ffreestanding \
               -fno-builtin -fno-stack-protector -ffunction-sections \
               -fdata-sections -I$(INCLUDES) -I$(PSL1GHT)/ppu/include
LDFLAGS     := -shared -nostartfiles -nodefaultlibs -Wl,--gc-sections \
               -Wl,-Bsymbolic -Wl,--undefined=g_module_info \
               -specs=tools/prx.specs \
               -Wl,-e,0 -Wl,-Map,$(BUILD)/$(TARGET).map
LIBDIRS     := -L$(PSL1GHT)/ppu/lib
LIBS        := -Wl,--start-group -lusb -lio -lsysmodule -llv2 \
               -lgcc -Wl,--end-group

.PHONY: all clean check-env test-host
all: $(TARGET).sprx $(TARGET).prx

test-host: $(HOST_TEST)
	$(HOST_TEST)

check-env:
	@test -x "$(CC)"
	@test -x "$(SPRX_LINKER)"
	@test -x "$(MAKE_SPRX)"
	@test -x "$(PRXGEN)"
	@test -x "$(PREPARE_LD)"
	@test -x "$(VALIDATE_PRX)"
	@test -x "$(VALIDATE_SELF)"
	@test -f "$(CRT_SPRX)"
	@test -f "$(LV2_LDSCRIPT)"

$(BUILD):
	@mkdir -p $@

$(BUILD)/%.o: $(SOURCES)/%.c | $(BUILD) check-env
	$(CC) $(CFLAGS) -c $< -o $@

$(HOST_TEST): tests/test_usb_descriptor_parser.c \
              source/usb_descriptor_parser.c | $(BUILD)
	$(HOST_CC) -O2 -Wall -Wextra -Werror -std=c11 -I$(INCLUDES) $^ -o $@

$(BUILD)/lv2-prx.ld: $(LV2_LDSCRIPT) $(PREPARE_LD) | $(BUILD)
	$(PREPARE_LD) $< $@

$(BUILD)/$(TARGET).elf: $(OFILES) $(BUILD)/lv2-prx.ld | $(BUILD) check-env
	$(CC) $(LDFLAGS) $(CRT_SPRX) $(OFILES) $(LIBDIRS) $(LIBS) -o $@

$(TARGET).prx: $(BUILD)/$(TARGET).elf
	cp $< $(BUILD)/$(TARGET).linked.elf
	$(SPRX_LINKER) $(BUILD)/$(TARGET).linked.elf
	$(PRXGEN) $(BUILD)/$(TARGET).linked.elf $@
	$(VALIDATE_PRX) $@

$(TARGET).sprx: $(TARGET).prx
	$(MAKE_SPRX) $< $@
	$(VALIDATE_SELF) $@

clean:
	rm -rf $(BUILD) $(TARGET).prx $(TARGET).sprx
