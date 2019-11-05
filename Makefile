
DEBUG ?= true
DEBUG_FLAGS ?= flashcfg httpclient httpserver ota pingwdt sleep rtc tcpserver device wifi battery system html \
               dnsserver api expr ports sessions webhooks espqtclient virtual \
               adc gpio pwm dht pwdetect rgb fsg dallastemp
DEBUG_IP ?= # 192.168.0.1
DEBUG_PORT ?= 48879

GDB     ?= false
OTA     ?= true
SSL     ?= false
SLEEP   ?= false
VIRTUAL ?= true

PORTS ?= # gpio0 gpio2 gpio4 gpio5 gpio12 gpio13 gpio14 gpio15 adc0 pwm0
EXTRA_PORT_DRIVERS ?=
EXTERNAL_PORT_DRIVERS ?=
EXTERNAL_PORT_DRIVERS_DIR ?=
EXTERNAL_DRIVERS_DIR ?=
PORT_ID_MAPPINGS ?= # gpio0:mygpio adc0:myadc

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
SETUP_MODE_INT ?= 10
SETUP_MODE_RESET_INT ?= 20
SETUP_MODE_LED_PORT ?= null

CONNECTED_LED_PORT ?= null
CONNECTED_LED_LEVEL ?= 0

FLASH_MODE ?= qio
FLASH_FREQ ?= 40

FW_CONFIG_NAME ?= # configuration-name
FW_CONFIG_MODELS ?= # model1|model2|model3

FW_BASE_URL  ?= http://provisioning.qtoggle.io
FW_BASE_PATH ?= /firmware/espqtoggle

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

APP = espqtoggle
SRC_MAIN_DIR = src
SRC_ESPGOODIES_DIR = src/espgoodies
SRC_PORTS_DIR = src/ports
SRC_DRIVERS_DIR = src/drivers
SRC_EXTRA_PORT_DRIVERS_DIR = src/extra
SRC_EXTERNAL_PORT_DRIVERS_DIR = $(EXTERNAL_PORT_DRIVERS_DIR)
SRC_EXTERNAL_DRIVERS_DIR = $(EXTERNAL_DRIVERS_DIR)
GDB_DIR = gdbstub
BUILD_DIR = build

INC = $(SRC_MAIN_DIR) $(SDK_BASE)/include $(GDB_DIR)
LIB = c gcc hal pp phy net80211 lwip wpa crypto upgrade m main
CFLAGS = -Wpointer-arith -Wall -Wl,-EL -fno-inline-functions -nostdlib -mlongcalls -mtext-section-literals \
         -ffunction-sections -fdata-sections -mforce-l32 -Wmissing-prototypes -Werror -fno-builtin-printf \
         -fno-guess-branch-probability -freorder-blocks-and-partition -fno-cse-follow-jumps \
         -D__ets__ -DICACHE_FLASH -DUSE_OPTIMIZE_PRINTF \
         -DFLASH_CONFIG_ADDR=$(FLASH_CONFIG_ADDR)
LDFLAGS	= -nostdlib -Wl,--no-check-sections -u call_user_start -Wl,-static -L$(SDK_BASE)/lib -Wl,--gc-sections

ifeq ($(GDB),true)
    DEBUG = true
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

SRC_MAIN_FILES =                  $(wildcard $(SRC_MAIN_DIR)/*.c)
SRC_ESPGOODIES_FILES =            $(wildcard $(SRC_ESPGOODIES_DIR)/*.c)
SRC_PORT_FILES =                  $(wildcard $(SRC_PORTS_DIR)/*.c)
SRC_DRIVERS_FILES =               $(wildcard $(SRC_DRIVERS_DIR)/*.c)
SRC_EXTERNAL_DRIVERS_FILES =      $(wildcard $(SRC_EXTERNAL_DRIVERS_DIR)/*.c)
SRC_EXTRA_PORT_DRIVERS_FILES =    $(foreach p,$(EXTRA_PORT_DRIVERS),$(SRC_EXTRA_PORT_DRIVERS_DIR)/$(p).c)
SRC_EXTERNAL_PORT_DRIVERS_FILES = $(foreach p,$(EXTERNAL_PORT_DRIVERS),$(SRC_EXTERNAL_PORT_DRIVERS_DIR)/$(p).c)
SRC_GDB_FILES =                   $(wildcard $(GDB_DIR)/*.c)

OBJ_FILES = $(SRC_MAIN_FILES:$(SRC_MAIN_DIR)/%.c=$(BUILD_DIR)/%.o) \
            $(SRC_ESPGOODIES_FILES:$(SRC_MAIN_DIR)/%.c=$(BUILD_DIR)/%.o) \
            $(SRC_PORT_FILES:$(SRC_MAIN_DIR)/%.c=$(BUILD_DIR)/%.o) \
            $(SRC_DRIVERS_FILES:$(SRC_MAIN_DIR)/%.c=$(BUILD_DIR)/%.o) \
            $(SRC_EXTERNAL_DRIVERS_FILES:$(SRC_EXTERNAL_DRIVERS_DIR)/%.c=$(BUILD_DIR)/external/drivers/%.o) \
            $(SRC_EXTRA_PORT_DRIVERS_FILES:$(SRC_MAIN_DIR)/%.c=$(BUILD_DIR)/%.o) \
            $(SRC_EXTERNAL_PORT_DRIVERS_FILES:$(SRC_EXTERNAL_PORT_DRIVERS_DIR)/%.c=$(BUILD_DIR)/external/ports/%.o)
VPATH = $(SRC_MAIN_DIR) $(GDB_DIR)

ifeq ($(GDB),true)
    CFLAGS  += -g -ggdb -Og -D_GDB -fPIE -fPIC
    LDFLAGS += -g -ggdb -Og -fPIE -fPIC
    ASFLAGS += -g -ggdb -Og -fPIE -fPIC -mlongcalls
    OBJ_FILES += $(BUILD_DIR)/gdbstub.o $(BUILD_DIR)/gdbstub-entry.o
else
    CFLAGS  += -Os -O2
    LDLAGS  += -Os -O2
endif

ifneq ($(EXTRA_PORT_DRIVERS),)
    INCLUDE_EXTRA_PORT_DRIVERS = $(foreach p,$(EXTRA_PORT_DRIVERS),-include $(SRC_EXTRA_PORT_DRIVERS_DIR)/$(p).h)
    INIT_EXTRA_PORT_DRIVERS = $(foreach p,$(EXTRA_PORT_DRIVERS),$(p)_init_ports();)
    CFLAGS += -D_INIT_EXTRA_PORT_DRIVERS="$(INIT_EXTRA_PORT_DRIVERS)"
    CFLAGS += $(addprefix -DHAS_,$(shell echo $(EXTRA_PORT_DRIVERS) | tr a-z A-Z))
endif

ifneq ($(EXTERNAL_PORT_DRIVERS),)
    INCLUDE_EXTERNAL_PORT_DRIVERS = $(foreach p,$(EXTERNAL_PORT_DRIVERS),\
    								  -include $(SRC_EXTERNAL_PORT_DRIVERS_DIR)/$(p).h)
    INIT_EXTERNAL_PORT_DRIVERS = $(foreach p,$(EXTERNAL_PORT_DRIVERS),$(p)_init_ports();)
    CFLAGS += -D_INIT_EXTERNAL_PORT_DRIVERS="$(INIT_EXTERNAL_PORT_DRIVERS)"
    CFLAGS += $(addprefix -DHAS_,$(shell echo $(EXTERNAL_PORT_DRIVERS) | tr a-z A-Z))
endif

ifeq ($(DEBUG), true)
    CFLAGS += -D_DEBUG
ifneq ($(DEBUG_IP),)
    CFLAGS += -D_DEBUG_IP=\"$(DEBUG_IP)\"
    CFLAGS += -D_DEBUG_PORT=$(DEBUG_PORT)
endif
endif

# define HAS_<PORT>
CFLAGS += $(addprefix -DHAS_,$(shell echo $(PORTS) | tr a-z A-Z))

# define _DEBUG_<FLAG>
CFLAGS += $(addprefix -D_DEBUG_,$(shell echo $(DEBUG_FLAGS) | tr a-z A-Z))

# define <PORT>_ID="<id>" from mappings
CFLAGS += $(foreach m,$(PORT_ID_MAPPINGS),-D$(shell \
    m=$$(echo $(m) | tr ':' ' '); parts=($${m}); \
    name=$$(echo $${parts[0]} | tr a-z A-Z); id=$${parts[1]}; \
    echo $${name}_ID=\\\"$${id}\\\"; \
))

# define <PORT>_ID="<id>" defaults
CFLAGS += $(foreach p,$(PORTS),$(shell \
    if ! [[ "$(PORT_ID_MAPPINGS)" =~ $(p): ]]; then \
        echo -D$$(echo $(p) | tr a-z A-Z)_ID=\\\"$(p)\\\"; \
    fi \
))

_COMMA := ,
ifneq ($(FW_CONFIG_MODELS),)
	FW_CONFIG_MODELS_PREPARED := $(subst |,\"$(_COMMA)\",$(FW_CONFIG_MODELS))
	FW_CONFIG_MODELS_PREPARED := \"$(FW_CONFIG_MODELS_PREPARED)\",
else
	FW_CONFIG_MODELS_PREPARED := 
endif

CFLAGS += -DFW_CONFIG_NAME=\"$(FW_CONFIG_NAME)\"
CFLAGS += -DFW_CONFIG_MODELS=$(FW_CONFIG_MODELS_PREPARED)
CFLAGS += -DFW_BASE_URL=\"$(FW_BASE_URL)\"
CFLAGS += -DFW_BASE_PATH=\"$(FW_BASE_PATH)\"

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

all: buildinfo dirs $(BUILD_DIR)/full.bin

dirs:
	$(Q) mkdir -p $(BUILD_DIR)

buildinfo:
	$(vecho) "---- configuration summary ----"
	$(vecho) " *" SDK_BASE = $(SDK_BASE)
	$(vecho) " *" VERSION = $(VERSION)
	$(vecho) " *" DEBUG = $(DEBUG)
	$(vecho) " *" DEBUG_FLAGS = $(DEBUG_FLAGS)
	$(vecho) " *" OTA = $(OTA)
	$(vecho) " *" SSL = $(SSL)
	$(vecho) " *" SLEEP = $(SLEEP)
	$(vecho) " *" VIRTUAL = $(VIRTUAL)
	$(vecho) " *" BATTERY = $(BATTERY)
	$(vecho) " *" BATTERY_DIV_FACTOR = $(BATTERY_DIV_FACTOR)
	$(vecho) " *" BATTERY_VOLT = $(BATTERY_VOLT_0) $(BATTERY_VOLT_20) $(BATTERY_VOLT_40) \
								 $(BATTERY_VOLT_60) $(BATTERY_VOLT_80) $(BATTERY_VOLT_100)
	$(vecho) " *" PORTS = $(PORTS)
	$(vecho) " *" EXTRA_PORT_DRIVERS = $(EXTRA_PORT_DRIVERS)
	$(vecho) " *" EXTERNAL_PORT_DRIVERS = $(EXTERNAL_PORT_DRIVERS)
	$(vecho) " *" EXTERNAL_PORT_DRIVERS_DIR = $(EXTERNAL_PORT_DRIVERS_DIR)
	$(vecho) " *" EXTERNAL_DRIVERS_DIR = $(EXTERNAL_DRIVERS_DIR)
	$(vecho) " *" PORT_ID_MAPPINGS = $(PORT_ID_MAPPINGS)
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
	$(vecho) " *" FW_BASE_PATH = "$(FW_BASE_PATH)"
	$(vecho) " *" CFLAGS = $(CFLAGS)
	$(vecho) "-------------------------------"

$(BUILD_DIR)/%.o: %.c
	$(vecho) "CC $<"
	@$(MD) -p $(@D)
	$(Q) $(CC) $(INC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ports.o: src/ports.c
	$(vecho) "CC $<"
	@$(MD) -p $(@D)
	$(Q) $(CC) $(INC) $(CFLAGS) $(INCLUDE_EXTRA_PORT_DRIVERS) $(INCLUDE_EXTERNAL_PORT_DRIVERS) -c $< -o $@

$(BUILD_DIR)/external/ports/%.o: $(SRC_EXTERNAL_PORT_DRIVERS_DIR)/%.c
	$(vecho) "CC $<"
	@$(MD) -p $(@D)
	$(Q) $(CC) $(INC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/external/drivers/%.o: $(SRC_EXTERNAL_DRIVERS_DIR)/%.c
	$(vecho) "CC $<"
	@$(MD) -p $(@D)
	$(Q) $(CC) $(INC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/gdbstub-entry.o: $(GDB_DIR)/gdbstub-entry.S
	$(vecho) "AS $<"
	$(Q) $(CC) $(ASFLAGS) -c -o $@ $<

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

$(BUILD_DIR)/full.bin: $(BUILD_DIR)/user1.bin $(BUILD_DIR)/user2.bin
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
