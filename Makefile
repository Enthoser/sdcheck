#---------------------------------------------------------------------------------
# Nintendo Switch Homebrew (NRO) - Minimal robust Makefile (no switch_rules)
#---------------------------------------------------------------------------------

TARGET      := sdcheck
BUILD       := build
SOURCES     := source

APP_TITLE   := SD Check
APP_AUTHOR  := Enthoser
APP_VERSION := 0.21

OUTPUT      := $(CURDIR)/$(TARGET)

ifeq ($(strip $(DEVKITPRO)),)
$(error DEVKITPRO is not set. Use the devkitPro MSYS2 shell.)
endif
ifeq ($(strip $(DEVKITA64)),)
$(error DEVKITA64 is not set. Use the devkitPro MSYS2 shell.)
endif

DKP_TOOLS   := $(DEVKITPRO)/tools/bin
LIBNX_DIR   := $(DEVKITPRO)/libnx
PORTLIBS    := $(DEVKITPRO)/portlibs/switch

export PATH := $(DKP_TOOLS):$(DEVKITA64)/bin:$(PATH)

CC          := aarch64-none-elf-gcc

ARCH        := -march=armv8-a+crc -mtune=cortex-a57 -mtp=soft -fPIE

CFLAGS      := -g -O2 -Wall -Wextra -ffunction-sections -fdata-sections $(ARCH) -D__SWITCH__
CFLAGS      += -I$(LIBNX_DIR)/include
CFLAGS      += -DSDCHECK_VERSION=\"$(APP_VERSION)\"

LDFLAGS     := -specs=$(LIBNX_DIR)/switch.specs -g $(ARCH) -Wl,--gc-sections -Wl,-Map,$(notdir $@).map
LIBDIRS     := -L$(LIBNX_DIR)/lib -L$(PORTLIBS)/lib
LIBS        := -lnx

CFILES      := $(wildcard $(SOURCES)/*.c)
OFILES      := $(patsubst $(SOURCES)/%.c,$(BUILD)/%.o,$(CFILES))

.PHONY: all clean
all: $(OUTPUT).nro

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/%.o: $(SOURCES)/%.c | $(BUILD)
	@echo compiling $<
	@$(CC) $(CFLAGS) -c $< -o $@

$(OUTPUT).elf: $(OFILES)
	@echo linking $(notdir $@)
	@$(CC) $(OFILES) $(LDFLAGS) $(LIBDIRS) $(LIBS) -o $@

$(OUTPUT).nacp: Makefile
	@echo generating $(notdir $@)
	@$(DKP_TOOLS)/nacptool --create "$(APP_TITLE)" "$(APP_AUTHOR)" "$(APP_VERSION)" $@

$(OUTPUT).nro: $(OUTPUT).elf $(OUTPUT).nacp
	@echo building $(notdir $@)
	@$(DKP_TOOLS)/elf2nro $(OUTPUT).elf $@ --nacp=$(OUTPUT).nacp --icon=icon.jpg
clean:
	@echo Cleaning...
	@rm -rf $(BUILD) $(OUTPUT).elf $(OUTPUT).nro $(OUTPUT).nacp *.map
