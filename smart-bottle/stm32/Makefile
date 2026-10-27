# Smart Bottle — firmware STM32F429I-DISC1, build din linia de comandă (fără CubeIDE).
#
#   python tools/fetch_deps.py      o singură dată: driverele ST (HAL, CMSIS, BSP)
#   make                            compilează build/smart-bottle.elf / .hex / .bin
#   make flash                      scrie pe placă prin ST-LINK-ul de pe placă (OpenOCD)
#   make monitor PORT=COM5          jurnalul de pe portul serial USB (115200)
#
# Variante:
#   make LINK=vcp                   protocolul pe portul serial USB în loc de UART5 (test fără ESP32)
#   make SENSOR=hx711               celula de sarcină în loc de senzorul analogic
#   make FLASHER=pyocd|cubeprog     alt program de scris pe placă

TARGET   := smart-bottle
BUILD    := build
LINK     ?= uart5
SENSOR   ?= analog
FLASHER  ?= openocd
PORT     ?=

ifeq ($(OS),Windows_NT)
  PYTHON ?= python
else
  PYTHON ?= python3
endif

PREFIX  ?= arm-none-eabi-
CC      := $(PREFIX)gcc
AS      := $(PREFIX)gcc -x assembler-with-cpp
OBJCOPY := $(PREFIX)objcopy
SIZE    := $(PREFIX)size

HAL := Drivers/STM32F4xx_HAL_Driver
BSP := Drivers/BSP/STM32F429I-Discovery

# ---- surse ----
APP_SRC := $(wildcard Core/Src/*.c)
HAL_SRC := $(filter-out %_template.c,$(wildcard $(HAL)/Src/stm32f4xx_hal*.c)) $(HAL)/Src/stm32f4xx_ll_fmc.c
BSP_SRC := $(BSP)/stm32f429i_discovery.c $(BSP)/stm32f429i_discovery_lcd.c $(BSP)/stm32f429i_discovery_sdram.c \
           Drivers/BSP/Components/ili9341/ili9341.c
ASM_SRC := startup_stm32f429xx.s

INCLUDES := -ICore/Inc -I$(HAL)/Inc -I$(HAL)/Inc/Legacy -IDrivers/CMSIS/Device/ST/STM32F4xx/Include \
            -IDrivers/CMSIS/Include -I$(BSP) -IDrivers/BSP/Components/ili9341

# ---- opțiuni ----
FW_VERSION := $(shell git describe --always --dirty 2>$(if $(filter Windows_NT,$(OS)),NUL,/dev/null) || echo dev)
DEFS := -DSTM32F429xx -DUSE_HAL_DRIVER -DFW_VERSION=\"$(FW_VERSION)\"
ifeq ($(LINK),vcp)
  DEFS += -DLINK_VCP=1
endif
ifeq ($(SENSOR),hx711)
  DEFS += -DSENSOR_USE_HX711=1
endif

MCU     := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
CFLAGS  := $(MCU) $(DEFS) $(INCLUDES) -Os -g3 -std=gnu11 -ffunction-sections -fdata-sections -MMD -MP
WARN    := -Wall -Wextra -Wno-unused-parameter
LDFLAGS := $(MCU) -TSTM32F429ZITX_FLASH.ld --specs=nano.specs -Wl,--gc-sections -Wl,-Map=$(BUILD)/$(TARGET).map \
           -Wl,--print-memory-usage -Wl,--no-warn-rwx-segments

OBJS := $(addprefix $(BUILD)/,$(APP_SRC:.c=.o) $(HAL_SRC:.c=.o) $(BSP_SRC:.c=.o) $(ASM_SRC:.s=.o))

# ---- reguli ----
.PHONY: all clean flash monitor deps size check-deps

all: check-deps $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).hex $(BUILD)/$(TARGET).bin

check-deps:
	@$(PYTHON) -c "import os,sys; sys.exit(0 if os.path.isdir('$(HAL)') else 'Lipsesc driverele ST. Ruleaza intai: $(PYTHON) tools/fetch_deps.py')"

deps:
	$(PYTHON) tools/fetch_deps.py

# codul aplicației cu avertismente complete; driverele ST fără (nu sunt ale noastre)
$(BUILD)/Core/%.o: Core/%.c
	@$(PYTHON) -c "import os,sys; os.makedirs(os.path.dirname(sys.argv[1]), exist_ok=True)" $@
	$(CC) $(CFLAGS) $(WARN) -c $< -o $@

$(BUILD)/Drivers/%.o: Drivers/%.c
	@$(PYTHON) -c "import os,sys; os.makedirs(os.path.dirname(sys.argv[1]), exist_ok=True)" $@
	$(CC) $(CFLAGS) -w -c $< -o $@

$(BUILD)/%.o: %.s
	@$(PYTHON) -c "import os,sys; os.makedirs(os.path.dirname(sys.argv[1]), exist_ok=True)" $@
	$(AS) $(MCU) -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJS) STM32F429ZITX_FLASH.ld
	$(CC) $(OBJS) $(LDFLAGS) -o $@
	$(SIZE) $@

$(BUILD)/%.hex: $(BUILD)/%.elf
	$(OBJCOPY) -O ihex $< $@

$(BUILD)/%.bin: $(BUILD)/%.elf
	$(OBJCOPY) -O binary -S $< $@

# ---- scriere pe placă ----
ELF := $(BUILD)/$(TARGET).elf
ifeq ($(FLASHER),openocd)
  FLASH_CMD := openocd -f board/stm32f429disc1.cfg -c "program $(ELF) verify reset exit"
else ifeq ($(FLASHER),pyocd)
  FLASH_CMD := pyocd flash -t stm32f429zitx $(ELF)
else ifeq ($(FLASHER),cubeprog)
  FLASH_CMD := STM32_Programmer_CLI -c port=SWD -w $(ELF) -v -rst
endif

flash: all
	$(FLASH_CMD)

monitor:
	$(PYTHON) -m serial.tools.miniterm $(if $(PORT),$(PORT),-) 115200 --eol CRLF

clean:
	$(PYTHON) -c "import shutil; shutil.rmtree('$(BUILD)', ignore_errors=True)"

-include $(OBJS:.o=.d)
