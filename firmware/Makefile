
BOARD=arduino_nano_v3
include config.mk

CROSS_COMPILE=avr-

CC=gcc
LD=gcc
OBJCOPY=objcopy
SIZE=size

# LTO gives a much smaller binary but prevent
# any debugging from beeing used.
LTO=n
DEBUG=0

CPPFLAGS = -MMD				\
	-I.				\

CFLAGS =-Os -Wall			\
	-std=gnu99			\
	-fdata-sections			\
	-ffunction-sections		\
	-fstack-usage			\
	-fstack-check			\

LDFLAGS=-Os				\
	-Wl,-Map=$(@:%.elf=%.map) 	\
	-Wl,--gc-sections		\
	-fwhole-program			\

ifeq ($(LTO),y)
CFLAGS += -flto -fuse-linker-plugin -ffat-lto-objects
LDFLAGS += -flto
else
CFLAGS += -g3
LDFLAGS += -g3
endif

avr-door-controller.elf_DEPS :=		\
	$(MCU_O)			\
	$(BOARD_O)			\
	acl.o				\
	button.o			\
	ctrl-cmd.o			\
	door-controller.o		\
	eeprom.o			\
	eeprom-v1.o			\
	external-irq.o			\
	gpio.o				\
	main.o				\
	sleep.o				\
	timer.o				\
	trigger.o			\
	uart.o				\
	uart-ctrl-transport.o		\
	wiegand-reader.o		\
	work-queue.o			\

avr-door-controller.elf_$(WITH_I2C) +=	\
	i2c.o				\

avr-door-controller.elf_$(WITH_RTC) +=	\
	rtc.o				\

avr-door-controller.elf_$(WITH_RTC_DS3231) +=	\
	rtc-ds3231.o			\

avr-door-controller.flash.ihex_DEPS :=	\
	avr-door-controller.elf		\

all_DEPS :=				\
	avr-door-controller.flash.ihex	\

all: $(all_DEPS)

flash: $(patsubst %.flash.ihex,flash-%,$(all_DEPS))

clean:
	$(call cmd, CLEAN, rm -f *.[oda] mcu/*.[oda] boards/*.[oda] *.elf *.ihex)

# All the flags we support
ALL_FLAGS = CPPFLAGS CFLAGS CXXFLAGS LDFLAGS LIBS FLASH_FLAGS EEPROM_FLAGS

# Pass the MCU and board config
CPPFLAGS+= -include $(MCU_H) -include $(BOARD_H) -DDEBUG=$(DEBUG)
# Set the MCU
CFLAGS+=-mmcu=$(MCU)
LDFLAGS+=-mmcu=$(MCU)

# Flag to use when creating images
FLASH_FLAGS=-R .eeprom
EEPROM_FLAGS=-j .eeprom --change-section-lma .eeprom=0

AVRDUDE = avrdude
AVRDUDE_PROGRAMMER = arduino
AVRDUDE_PORT = /dev/ttyUSB0
AVRDUDE_FLAGS = -c $(AVRDUDE_PROGRAMMER) -P $(AVRDUDE_PORT) -p $(MCU)

%.o: %.c
	$(call compile, CC, $(CPPFLAGS) $(CFLAGS) -o $@ -c $<)

%.o: %.S
	$(call compile, CC, $(CPPFLAGS) $(CFLAGS) -o $@ -c $<)

%.a:
	@rm -f $@
	$(call compile, AR, rs $@ $(filter %.o,$($*.a_DEPS)))

%.eeprom.ihex: %.elf
	$(call compile, OBJCOPY, -O ihex $(EEPROM_FLAGS) $< $@)

%.eeprom.ihex: %.o
	$(call compile, OBJCOPY, -O ihex $(EEPROM_FLAGS) $< $@)

%.flash.ihex: %.elf
	$(call compile, OBJCOPY, -O ihex $(FLASH_FLAGS) $< $@)

%.elf:
	$(call compile, LD, $(LDFLAGS) -o $@ $(filter %.o %.x,$($*.elf_DEPS)) $(LIBS))
	$(call compile, SIZE, --mcu=$(MCU) -C $@)

flash-%: %.flash.ihex
	$(call cmd, FLASH, $(AVRDUDE) $(AVRDUDE_FLAGS) -U flash:w:$<:i)

.PHONY: all clean flash flash-%

.SUFFIXES:

# Run a command and only show a short description unless V=1 is set
# $(1): Action short name
# $(2): Action target (optional)
# $(3): Command to run
ifeq ($(V),1)
cmd = $(or $(3),$(2))
else
cmd = @printf "  %-10s %s\n" "$(strip $(1))" "$(if $(3),$(strip $(2)))" ; $(or $(3),$(2))
endif
compile = $(call cmd,$(1),$@,$(CROSS_COMPILE)$($(strip $(1))) $(2))

# Set a flag for goal
# $(1): goal
# $(2): flag
define goal_flag
$(1): $(2) += $($(1)_$(2))
endef

# Define the rules for a goal and its dependencies
# $(1): goal
define goal
-include $(filter %.d,$($(1)_DEPS:%.o=%.d)))
$(1): $($(1)_DEPS) Makefile
$(foreach flag,$(ALL_FLAGS),$(eval $(call goal_flag,$(1),$(flag))))
$(foreach dep,$($(1)_DEPS),$(eval $(if $($(dep)_DEFINED),,$(call goal,$(dep)))))
$(1)_DEFINED := true
endef

# Generate all dependencies from the 'all' goal
$(foreach dep,$(all_DEPS),$(eval $(call goal,$(dep))))
