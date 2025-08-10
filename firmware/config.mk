
# Check that $(BOARD).h exists
ifeq ($(wildcard boards/$(BOARD).h),)
$(error $(BOARD) is not a supported board)
endif

# Get various config from BOARD_H
BOARD_H := boards/$(BOARD).h
BOARD_O := $(patsubst %.c,%.o,$(wildcard boards/$(BOARD).c))

# Macros to read variables and conditions from $(BOARD_H)
CPP_EXPAND = $(shell ($(2)) | $(CPP) -E -P -imacros $(1) -)
CPP_COND_CMD = echo '\#if $(1)' ; echo DEPS ; echo '\#endif'

CPP_VAR = $(call CPP_EXPAND,$(1),echo $(2))
CPP_COND = $(call CPP_EXPAND,$(1),$(call CPP_COND_CMD,$(2)))

# Get the MCU and the optional modules
MCU := $(call CPP_VAR,$(BOARD_H),MCU)

# Add the MCU support
MCU_H := mcu/$(MCU).h
MCU_O := $(patsubst %.c,%.o,$(wildcard mcu/$(MCU).c))

# Check that the MCU is supported
ifeq ($(wildcard $(MCU_H)),)
$(error $(MCU) is not a supported MCU)
endif

WITH_I2C := $(call CPP_COND,$(MCU_H),HAS_I2C)

WITH_RTC := $(call CPP_COND,$(BOARD_H),HAS_RTC)

WITH_RTC_DS3231 := $(call CPP_COND,$(BOARD_H),HAS_RTC && DS3231_ADDR)

WITH_OTP := $(call CPP_COND,$(BOARD_H),WITH_OTP)
