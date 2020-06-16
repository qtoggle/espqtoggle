
DEBUG ?= true
DEBUG_FLAGS ?= battery dnsserver flashcfg gpio html httpclient \
               httpserver ota rtc sleep system tcpserver wifi \
               peripherals api core device espqtclient expr ports events sessions virtual webhooks \
               adc pwm uart hspi \
               gpiop dallastemp dht v9821 bl0940 hlw8012 sht kr102
DEBUG_IP ?= # 192.168.0.1
DEBUG_PORT ?= 48879
DEBUG_UART_NO ?= 0

OTA     ?= true
SSL     ?= false
SLEEP   ?= false
VIRTUAL ?= true

BATTERY ?= false
BATTERY_DIV_FACTOR ?= 0.166 # 200k / (1000k + 200k)
BATTERY_VOLT_0   ?= 3000
BATTERY_VOLT_20  ?= 3650
BATTERY_VOLT_40  ?= 3710
BATTERY_VOLT_60  ?= 3760
BATTERY_VOLT_80  ?= 3880
BATTERY_VOLT_100 ?= 4250

SETUP_MODE_PORT ?= null
SETUP_MODE_LEVEL ?= 0
SETUP_MODE_INT ?= 5
SETUP_MODE_RESET_INT ?= 20
SETUP_MODE_LED_PORT ?= null

CONNECTED_LED_PORT ?= null
CONNECTED_LED_LEVEL ?= 1

FLASH_MODE ?= qio
FLASH_FREQ ?= 40

FW_CONFIG_NAME ?= # configuration-name
FW_CONFIG_MODELS ?= # model1|model2|model3

FW_BASE_URL  ?= http://provisioning.qtoggle.io
FW_BASE_OTA_PATH ?= /firmware/espqtoggle
FW_BASE_CFG_PATH ?= /config

# SSID to connect to when unconfigured
DEFAULT_SSID ?= qToggleSetup

# ---- configurable stuff ends here ---- #

SHELL = /bin/bash  # other shells will probably fail
VERSION = $(shell cat src/ver.h | grep FW_VERSION | head -n 1 | tr -s ' ' | cut -d ' ' -f 3 | tr -d '"')

ifeq ($(FLASH_MODE),qio)
	FLASH_MODE_INT = 0
else ifeq ($(FLASH_MODE),qout)
	FLASH_MODE_INT = 1
else ifeq ($(FLASH_MODE),dio)
	FLASH_MODE_INT = 2
else ifeq ($(FLASH_MODE),dout)
	FLASH_MODE_INT = 3
endif

ifeq ($(FLASH_FREQ),20)
	FLASH_CLK_DIV = 2
	FLASH_SIZE_FREQ_HEX = 22
else ifeq ($(FLASH_FREQ),26)
	FLASH_CLK_DIV = 1
	FLASH_SIZE_FREQ_HEX = 21
else ifeq ($(FLASH_FREQ),40)
	FLASH_CLK_DIV = 0
	FLASH_SIZE_FREQ_HEX = 20
else ifeq ($(FLASH_FREQ),80)
	FLASH_CLK_DIV = 15
	FLASH_SIZE_FREQ_HEX = 2F
endif

FLASH_SIZE_MAP = 2  # 1024 (512 + 512)
FLASH_SIZE = 1024

FLASH_BOOT_ADDR=0x00000
FLASH_USR1_ADDR=0x01000
FLASH_CONFIG_ADDR = 0x7C000
FLASH_USR2_ADDR=0x81000
FLASH_INIT_DATA_ADDR=0xFC000
FLASH_SYS_PARAM_ADDR=0xFE000

CC = xtensa-lx106-elf-gcc
AR = xtensa-lx106-elf-ar
LD = xtensa-lx106-elf-gcc
OC = xtensa-lx106-elf-objcopy
MD = mkdir -p
RM = rm -rf
GZ = gzip -c
DD = dd status=none

ESPTOOL ?= esptool
APPGEN ?= $(PWD)/gen_appbin.py

# ensure we have a toolchain
ifeq ($(shell which $(CC) 2>/dev/null),)
    $(error "No $(CC) found in $(PATH). Please install the ESP8266 toolchain")
endif

# ensure we have an SDK
ifeq ($(SDK_BASE),)
    $(error "Please set SDK_BASE to the installation dir of your ESP8266 non-OS SDK")
endif

# ensure the SDK has been fixed
ifeq ($(wildcard $(SDK_BASE)/.fixed),)
    $(error "Please run 'builder/fix-sdk.sh $(SDK_BASE)' to apply required fixes")
endif

APP = espqtoggle
SRC_MAIN_DIR = src
SRC_ESPGOODIES_DIR = src/espgoodies
SRC_DRIVERS_DIR = src/drivers
SRC_PERIPHERALS_DIR = src/peripherals
BUILD_DIR = build
INIT_DATA_FILES = esp_init_data_default.bin

INC = $(SRC_MAIN_DIR) $(SDK_BASE)/include
LIB = c gcc hal pp phy net80211 lwip wpa crypto upgrade m main
CFLAGS += -Wpointer-arith -Wall -Wl,-EL -fno-inline-functions -nostdlib -mlongcalls -mtext-section-literals \
          -ffunction-sections -fdata-sections -mforce-l32 -Wmissing-prototypes -fno-builtin-printf \
          -fno-guess-branch-probability -freorder-blocks-and-partition -fno-cse-follow-jumps \
          -D__ets__ -DICACHE_FLASH -DUSE_OPTIMIZE_PRINTF -DFLASH_CONFIG_ADDR=$(FLASH_CONFIG_ADDR) \
          -Os -O2 -std=c99
LDFLAGS	= -nostdlib -Wl,--no-check-sections -u call_user_start -Wl,-static -L$(SDK_BASE)/lib -Wl,--gc-sections -Os -O2

ifneq ($(DEBUG),true)
    CFLAGS += -Werror
endif

ifeq ($(OTA), true)
    CFLAGS += -D_OTA
endif

ifeq ($(SSL), true)
    LIB += ssl
    CFLAGS += -D_SSL
endif

ifeq ($(SLEEP), true)
    CFLAGS += -D_SLEEP
endif

ifeq ($(VIRTUAL), true)
    CFLAGS += -DHAS_VIRTUAL
endif

ifeq ($(BATTERY), true)
    CFLAGS += -D_BATTERY
endif

ifneq ($(BATTERY_DIV_FACTOR),)
    CFLAGS += -DBATTERY_DIV_FACTOR=$(BATTERY_DIV_FACTOR)
endif

ifneq ($(BATTERY_VOLT_0)),)
    CFLAGS += -DBATTERY_VOLT_0=$(BATTERY_VOLT_0)
    CFLAGS += -DBATTERY_VOLT_20=$(BATTERY_VOLT_20)
    CFLAGS += -DBATTERY_VOLT_40=$(BATTERY_VOLT_40)
    CFLAGS += -DBATTERY_VOLT_60=$(BATTERY_VOLT_60)
    CFLAGS += -DBATTERY_VOLT_80=$(BATTERY_VOLT_80)
    CFLAGS += -DBATTERY_VOLT_100=$(BATTERY_VOLT_100)
endif

ifneq ($(SETUP_MODE_PORT), null)
    CFLAGS += -DSETUP_MODE_PORT=$(SETUP_MODE_PORT)
    CFLAGS += -DSETUP_MODE_LEVEL=$(SETUP_MODE_LEVEL)
    CFLAGS += -DSETUP_MODE_INT=$(SETUP_MODE_INT)
    CFLAGS += -DSETUP_MODE_RESET_INT=$(SETUP_MODE_RESET_INT)
endif

ifneq ($(SETUP_MODE_LED_PORT), null)
    CFLAGS += -DSETUP_MODE_LED_PORT=$(SETUP_MODE_LED_PORT)
endif

ifneq ($(CONNECTED_LED_PORT), null)
    CFLAGS += -DCONNECTED_LED_PORT=$(CONNECTED_LED_PORT)
endif

ifneq ($(CONNECTED_LED_LEVEL), null)
    CFLAGS += -DCONNECTED_LED_LEVEL=$(CONNECTED_LED_LEVEL)
endif

INC := $(addprefix -I,$(INC))
LIB := $(addprefix -l,$(LIB))

SRC_MAIN_FILES =        $(wildcard $(SRC_MAIN_DIR)/*.c)
SRC_ESPGOODIES_FILES =  $(wildcard $(SRC_ESPGOODIES_DIR)/*.c)
SRC_DRIVERS_FILES =     $(wildcard $(SRC_DRIVERS_DIR)/*.c)
SRC_PERIPHERALS_FILES = $(wildcard $(SRC_PERIPHERALS_DIR)/*.c)

OBJ_FILES = $(SRC_MAIN_FILES:$(SRC_MAIN_DIR)/%.c=$(BUILD_DIR)/%.o) \
            $(SRC_ESPGOODIES_FILES:$(SRC_MAIN_DIR)/%.c=$(BUILD_DIR)/%.o) \
            $(SRC_DRIVERS_FILES:$(SRC_MAIN_DIR)/%.c=$(BUILD_DIR)/%.o) \
            $(SRC_PERIPHERALS_FILES:$(SRC_MAIN_DIR)/%.c=$(BUILD_DIR)/%.o)
            
INIT_DATA_FILES := $(foreach f,$(INIT_DATA_FILES),$(SDK_BASE)/bin/$(f))

VPATH = $(SRC_MAIN_DIR)

ifeq ($(DEBUG), true)
    CFLAGS += -D_DEBUG
    CFLAGS += -D_DEBUG_UART_NO=$(DEBUG_UART_NO)
ifneq ($(DEBUG_IP),)
    CFLAGS += -D_DEBUG_IP=\"$(DEBUG_IP)\"
    CFLAGS += -D_DEBUG_PORT=$(DEBUG_PORT)
endif
endif

# define _DEBUG_<FLAG>
CFLAGS += $(addprefix -D_DEBUG_,$(shell echo $(DEBUG_FLAGS) | tr a-z A-Z))

# define init data hex content
INIT_DATA_HEX_DEF = $(foreach f,$(INIT_DATA_FILES),$(shell \
    def_name=_$$(basename $(f) | sed 's/.bin/_hex/' | tr a-z A-Z); \
    echo -D$${def_name}="\{$$(hexdump -ve '"0x%08X,"' $(f))\}"; \
))

_COMMA := ,
ifneq ($(FW_CONFIG_MODELS),)
	FW_CONFIG_MODELS_PREPARED := $(subst |,\"$(_COMMA)\",$(FW_CONFIG_MODELS))
	FW_CONFIG_MODELS_PREPARED := \"$(FW_CONFIG_MODELS_PREPARED)\"
else
	FW_CONFIG_MODELS_PREPARED := \"default\"
endif

CFLAGS += -DFW_CONFIG_NAME=\"$(FW_CONFIG_NAME)\"
CFLAGS += -DFW_CONFIG_MODELS=$(FW_CONFIG_MODELS_PREPARED)
CFLAGS += -DFW_BASE_URL=\"$(FW_BASE_URL)\"
CFLAGS += -DFW_BASE_OTA_PATH=\"$(FW_BASE_OTA_PATH)\"
CFLAGS += -DFW_BASE_CFG_PATH=\"$(FW_BASE_CFG_PATH)\"
CFLAGS += -DDEFAULT_SSID=\"$(DEFAULT_SSID)\"

LDSCRIPT = $(SDK_BASE)/ld/eagle.app.v6.new.$(FLASH_SIZE).app$(1).ld

V ?= $(VERBOSE)
ifeq ("$(V)","1")
    Q :=
    vecho := @true
else
    Q := @
    vecho := @echo
endif

.PHONY: dirs
.NOTPARALLEL: buildinfo
.PRECIOUS: $(BUILD_DIR)/%.html.gz build/$(APP)%.out

all: buildinfo dirs $(BUILD_DIR)/firmware.bin

dirs:
	$(Q) mkdir -p $(BUILD_DIR)

buildinfo:
	$(vecho) "---- configuration summary ----"
	$(vecho) " *" SDK_BASE = $(SDK_BASE)
	$(vecho) " *" VERSION = $(VERSION)
	$(vecho) " *" DEBUG = $(DEBUG)
	$(vecho) " *" DEBUG_UART_NO = $(DEBUG_UART_NO)
	$(vecho) " *" DEBUG_IP = $(DEBUG_IP):$(DEBUG_PORT)
	$(vecho) " *" DEBUG_FLAGS = $(DEBUG_FLAGS)
	$(vecho) " *" OTA = $(OTA)
	$(vecho) " *" SSL = $(SSL)
	$(vecho) " *" SLEEP = $(SLEEP)
	$(vecho) " *" VIRTUAL = $(VIRTUAL)
	$(vecho) " *" BATTERY = $(BATTERY)
	$(vecho) " *" BATTERY_DIV_FACTOR = $(BATTERY_DIV_FACTOR)
	$(vecho) " *" BATTERY_VOLT = $(BATTERY_VOLT_0) $(BATTERY_VOLT_20) $(BATTERY_VOLT_40) \
								 $(BATTERY_VOLT_60) $(BATTERY_VOLT_80) $(BATTERY_VOLT_100)
	$(vecho) " *" SETUP_MODE_PORT = $(SETUP_MODE_PORT)
	$(vecho) " *" SETUP_MODE_LEVEL = $(SETUP_MODE_LEVEL)
	$(vecho) " *" SETUP_MODE_LED_PORT = $(SETUP_MODE_LED_PORT)
	$(vecho) " *" CONNECTED_LED_PORT = $(CONNECTED_LED_PORT)
	$(vecho) " *" CONNECTED_LED_LEVEL = $(CONNECTED_LED_LEVEL)
	$(vecho) " *" FLASH_MODE = $(FLASH_MODE)
	$(vecho) " *" FLASH_FREQ = $(FLASH_FREQ)
	$(vecho) " *" CONFIG_NAME = $(FW_CONFIG_NAME)
	$(vecho) " *" CONFIG_MODELS = "$(FW_CONFIG_MODELS)"
	$(vecho) " *" FW_BASE_URL = "$(FW_BASE_URL)"
	$(vecho) " *" FW_BASE_OTA_PATH = "$(FW_BASE_OTA_PATH)"
	$(vecho) " *" FW_BASE_CFG_PATH = "$(FW_BASE_CFG_PATH)"
	$(vecho) " *" DEFAULT_SSID = "$(DEFAULT_SSID)"
	$(vecho) " *" CFLAGS = $(CFLAGS)
	$(vecho) "-------------------------------"

$(BUILD_DIR)/%.o: %.c
	$(vecho) "CC $<"
	@$(MD) -p $(@D)
	$(Q) $(CC) $(INC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/espgoodies/initdata.o: espgoodies/initdata.c
	$(vecho) "CC $<"
	$(Q) $(CC) $(INC) $(CFLAGS) $(INIT_DATA_HEX_DEF) -c $< -o $@

$(BUILD_DIR)/$(APP).a: $(OBJ_FILES)
	$(vecho) "AR $@"
	$(Q) $(AR) cru $@ $^

$(BUILD_DIR)/$(APP)%.out:$(BUILD_DIR)/$(APP).a
	$(vecho) "LD $@"
	$(Q) $(LD) $(LDFLAGS) -T$(call LDSCRIPT,$*) -Wl,--start-group $(LIB) $^ -Wl,--end-group -o $@

$(BUILD_DIR)/%.html.gz: html/%.html
	$(vecho) "GZ $@"
	$(Q) sed 's/{{VERSION}}/$(VERSION)/g' $^ > $(BUILD_DIR)/$$(basename $^)
	$(Q) $(GZ) $(BUILD_DIR)/$$(basename $^) > $@

$(BUILD_DIR)/user%.bin: $(BUILD_DIR)/$(APP)%.out $(BUILD_DIR)/index.html.gz
	@echo $(FW_CONFIG_NAME) > $(BUILD_DIR)/.config_name
	$(vecho) "FW $@"
	$(Q) $(OC) --only-section .text -O binary $< $(BUILD_DIR)/eagle.app.v6.text.bin
	$(Q) $(OC) --only-section .data -O binary $< $(BUILD_DIR)/eagle.app.v6.data.bin
	$(Q) $(OC) --only-section .rodata -O binary $< $(BUILD_DIR)/eagle.app.v6.rodata.bin
	$(Q) $(OC) --only-section .irom0.text -O binary $< $(BUILD_DIR)/eagle.app.v6.irom0text.bin
	$(Q) cd $(BUILD_DIR) && \
	     $(APPGEN) $(APP)$*.out 2 $(FLASH_MODE_INT) $(FLASH_CLK_DIV) $(FLASH_SIZE_MAP) $* index.html.gz
	$(Q) mv $(BUILD_DIR)/eagle.app.flash.bin $@

$(BUILD_DIR)/firmware.bin: $(BUILD_DIR)/user1.bin $(BUILD_DIR)/user2.bin
	$(vecho) "FW $@"
	$(Q) $(DD) if=/dev/zero of=$@ bs=1k count=$(FLASH_SIZE)
	$(Q) $(DD) if=$(SDK_BASE)/bin/boot.bin of=$@ bs=1k \
	           seek=$$(($(FLASH_BOOT_ADDR) / 1024)) conv=notrunc
	$(Q) $(DD) if=$(BUILD_DIR)/user1.bin of=$@ bs=1k \
	           seek=$$(($(FLASH_USR1_ADDR) / 1024)) conv=notrunc
	$(Q) $(DD) if=$(BUILD_DIR)/user2.bin of=$@ bs=1k \
	           seek=$$(($(FLASH_USR2_ADDR) / 1024)) conv=notrunc
	$(Q) $(DD) if=$(SDK_BASE)/bin/esp_init_data_default.bin of=$@ bs=1k \
	           seek=$$(($(FLASH_INIT_DATA_ADDR) / 1024)) conv=notrunc
	$(Q) $(DD) if=$(SDK_BASE)/bin/blank.bin of=$@ bs=1k \
	           seek=$$(($(FLASH_SYS_PARAM_ADDR) / 1024)) conv=notrunc
	$(Q) echo -en "\x$(FLASH_MODE_INT)" | $(DD) of=$@ bs=1 seek=2 conv=notrunc
	$(Q) echo -en "\x$(FLASH_SIZE_FREQ_HEX)" | $(DD) of=$@ bs=1 seek=3 conv=notrunc

clean:
	$(Q) $(RM) $(BUILD_DIR)
