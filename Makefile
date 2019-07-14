
DEBUG ?= true
DEBUG_FLAGS ?= flashcfg httpclient httpserver ota pingwdt sleep rtc tcpserver device wifi battery system \
               api expr ports sessions webhooks espqtclient virtual adc gpio pwm dht pwdetect rgb fsg dallastemp
DEBUG_IP ?= # 192.168.0.1
DEBUG_PORT ?= 48879

USR   ?= 1

GDB     ?= false
OTA     ?= true
SSL     ?= false
SLEEP   ?= false
VIRTUAL ?= true

PORTS ?= # gpio0 gpio2 gpio4 gpio5 gpio12 gpio13 gpio14 gpio15 adc0 pwm0
EXTRA_DRIVERS ?=
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

FW_CONFIG_NAME ?= # configuration-name


# ---- configurable stuff ends here ---- #

SHELL = /bin/bash  # other shells will probably fail
VERSION = $(shell cat src/ver.h | grep FW_VERSION | head -n 1 | tr -s ' ' | cut -d ' ' -f 3 | tr -d '"')

FLASH_MODE = 0     	# QIO
FLASH_CLK_DIV = 15 	# 80 MHz
FLASH_SIZE_MAP = 2  # 1024 (512 + 512)
FLASH_SIZE = 1024
FLASH_CONFIG_FACTORY_ADDR_BASE = 0x78000
FLASH_CONFIG_ADDR_BASE = 0x7C000
FLASH_EXTRA_ADDR_BASE = 0x100000

CC = xtensa-lx106-elf-gcc
AR = xtensa-lx106-elf-ar
LD = xtensa-lx106-elf-gcc
OC = xtensa-lx106-elf-objcopy
MD = mkdir -p
RM = rm -rf

ESPTOOL ?= esptool
APPGEN = $(SDK_BASE)/tools/gen_appbin.py

APP = espqtoggle
SRC_MAIN_DIR = src
SRC_ESPGOODIES_DIR = src/espgoodies
SRC_PORTS_DIR = src/ports
SRC_DRIVERS_DIR = src/drivers
SRC_EXTRA_DRIVERS_DIR = src/extra
GDB_DIR = gdbstub
BUILD_DIR = build

INC = $(SRC_MAIN_DIR) $(SDK_BASE)/include $(GDB_DIR)
LIB = c gcc hal pp phy net80211 lwip wpa crypto upgrade m main
CFLAGS = -Wpointer-arith -Wall -Wl,-EL -fno-inline-functions -nostdlib -mlongcalls -mtext-section-literals \
         -ffunction-sections -fdata-sections -mforce-l32 -Wmissing-prototypes \
         -D__ets__ -DICACHE_FLASH -DUSE_OPTIMIZE_PRINTF \
         -DFLASH_CONFIG_ADDR_BASE=$(FLASH_CONFIG_ADDR_BASE) \
         -DFLASH_CONFIG_FACTORY_ADDR_BASE=$(FLASH_CONFIG_FACTORY_ADDR_BASE) \
         -DFLASH_EXTRA_ADDR_BASE=$(FLASH_EXTRA_ADDR_BASE)
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

SRC_MAIN_FILES =          $(wildcard $(SRC_MAIN_DIR)/*.c)
SRC_ESPGOODIES_FILES =    $(wildcard $(SRC_ESPGOODIES_DIR)/*.c)
SRC_PORT_FILES =          $(wildcard $(SRC_PORTS_DIR)/*.c)
SRC_DRIVERS_FILES =       $(wildcard $(SRC_DRIVERS_DIR)/*.c)
SRC_EXTRA_DRIVERS_FILES = $(foreach p,$(EXTRA_DRIVERS),$(SRC_EXTRA_DRIVERS_DIR)/$(p).c)
SRC_GDB_FILES =           $(wildcard $(GDB_DIR)/*.c)
OBJ_FILES = $(SRC_MAIN_FILES:$(SRC_MAIN_DIR)/%.c=$(BUILD_DIR)/%.o) \
            $(SRC_ESPGOODIES_FILES:$(SRC_MAIN_DIR)/%.c=$(BUILD_DIR)/%.o) \
            $(SRC_PORT_FILES:$(SRC_MAIN_DIR)/%.c=$(BUILD_DIR)/%.o) \
            $(SRC_DRIVERS_FILES:$(SRC_MAIN_DIR)/%.c=$(BUILD_DIR)/%.o) \
            $(SRC_EXTRA_DRIVERS_FILES:$(SRC_MAIN_DIR)/%.c=$(BUILD_DIR)/%.o)	
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

ifneq ($(EXTRA_DRIVERS),)
    INCLUDE_EXTRA_DRIVERS = $(foreach p,$(EXTRA_DRIVERS),-include $(SRC_EXTRA_DRIVERS_DIR)/$(p).h)
    INIT_EXTRA_DRIVERS = $(foreach p,$(EXTRA_DRIVERS),$(p)_init_ports();)
    CFLAGS += -D_INIT_EXTRA_DRIVERS="$(INIT_EXTRA_DRIVERS)" $(INCLUDE_EXTRA_DRIVERS)
    CFLAGS += $(addprefix -DHAS_,$(shell echo $(EXTRA_DRIVERS) | tr a-z A-Z))
endif

APP_AR  = $(BUILD_DIR)/$(APP).a
APP_OUT = $(BUILD_DIR)/$(APP).out
FW      = $(BUILD_DIR)/user$(USR).bin

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

LDSCRIPT = eagle.app.v6.new.$(FLASH_SIZE).app$(USR).ld
LDSCRIPT := $(SDK_BASE)/ld/$(LDSCRIPT)

# compute the firmware config identifier (nulls and 0s are reserved)
ifeq ($(FW_CONFIG_ID),)
    FW_CONFIG_TEXT = $(SETUP_MODE_PORT) $(SETUP_MODE_LED_PORT) $(CONNECTED_LED_PORT) \
                     null null null null null null null null null null null null null
    FW_CONFIG_NUMB = $(FLASH_SIZE) \
                     $(SETUP_MODE_LEVEL) $(BATTERY_DIV_FACTOR) \
                     $(BATTERY_VOLT_0) $(BATTERY_VOLT_20) $(BATTERY_VOLT_40) \
                     $(BATTERY_VOLT_60) $(BATTERY_VOLT_80) $(BATTERY_VOLT_100) \
                     $(CONNECTED_LED_LEVEL) 0 0 0 0 0 0
    FW_CONFIG_FLAG = $(OTA) $(SSL) $(SLEEP) $(BATTERY) $(VIRTUAL) $(DEBUG) \
                     false false false false false false false false false false
    FW_CONFIG_PORT = $(sort $(PORTS)) $(sort $(EXTRA_DRIVERS))
    
    FW_CONFIG_ID := $(FW_CONFIG_TEXT) $(FW_CONFIG_NUMB) $(FW_CONFIG_FLAG) $(FW_CONFIG_PORT)
    FW_CONFIG_ID := $(shell echo -n $(FW_CONFIG_ID) | tr -s ' ' | tr ' ' ':' | sha1sum | cut -b 1-8)
endif

CFLAGS += -DFW_CONFIG_ID=\"$(FW_CONFIG_ID)\"
CFLAGS += -DFW_CONFIG_NAME=\"$(FW_CONFIG_NAME)\"

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

all: buildinfo dirs $(FW) 

dirs:
	$(Q) mkdir -p $(BUILD_DIR)

buildinfo:
	$(vecho) "---- configuration summary ----"
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
	$(vecho) " *" EXTRA_DRIVERS = $(EXTRA_DRIVERS)
	$(vecho) " *" PORT_ID_MAPPINGS = $(PORT_ID_MAPPINGS)
	$(vecho) " *" SETUP_MODE_PORT = $(SETUP_MODE_PORT)
	$(vecho) " *" SETUP_MODE_LEVEL = $(SETUP_MODE_LEVEL)
	$(vecho) " *" SETUP_MODE_LED_PORT = $(SETUP_MODE_LED_PORT)
	$(vecho) " *" CONNECTED_LED_PORT = $(CONNECTED_LED_PORT)
	$(vecho) " *" CONNECTED_LED_LEVEL = $(CONNECTED_LED_LEVEL)
	$(vecho) " *" FW_CONFIG_NAME = $(FW_CONFIG_NAME)
	$(vecho) " *" FW_CONFIG_ID = $(FW_CONFIG_ID)
	$(vecho) " *" USR = $(USR)
	$(vecho) " *" CFLAGS = $(CFLAGS)
	$(vecho) "-------------------------------"

$(BUILD_DIR)/%.o: %.c
	$(vecho) "CC $<"
	@$(MD) -p $(@D)
	$(Q) $(CC) $(INC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/gdbstub-entry.o: $(GDB_DIR)/gdbstub-entry.S
	$(vecho) "AS $<"
	$(Q) $(CC) $(ASFLAGS) -c -o $@ $<

$(APP_AR): $(OBJ_FILES)
	$(vecho) "AR $@"
	$(Q) $(AR) cru $@ $^

$(APP_OUT): $(APP_AR)
	$(vecho) "LD $@"
	$(Q) $(LD) $(LDFLAGS) -T$(LDSCRIPT) -Wl,--start-group $(LIB) $^ -Wl,--end-group -o $@

$(BUILD_DIR)/user%.bin: $(APP_OUT)
	@echo $(FW_CONFIG_ID) > $(BUILD_DIR)/.config_id
	$(vecho) "FW $@"
	$(Q) $(OC) --only-section .text -O binary $< $(BUILD_DIR)/eagle.app.v6.text.bin
	$(Q) $(OC) --only-section .data -O binary $< $(BUILD_DIR)/eagle.app.v6.data.bin
	$(Q) $(OC) --only-section .rodata -O binary $< $(BUILD_DIR)/eagle.app.v6.rodata.bin
	$(Q) $(OC) --only-section .irom0.text -O binary $< $(BUILD_DIR)/eagle.app.v6.irom0text.bin
	$(Q) cd $(BUILD_DIR); \
	     COMPILE=gcc python2 $(APPGEN) *.out 2 $(FLASH_MODE) $(FLASH_CLK_DIV) $(FLASH_SIZE_MAP) $(USR) > /dev/null
	$(Q) mv $(BUILD_DIR)/eagle.app.flash.bin $@
	$(vecho) "-------------------------------"
	$(vecho) " *" $(notdir $@): $$(stat -L -c %s $@) bytes
	$(vecho)

clean:
	$(Q) $(RM) $(BUILD_DIR)
