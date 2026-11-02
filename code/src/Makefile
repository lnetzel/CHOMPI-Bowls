TARGET = CHOMPI_BOWLS_v1.0

DEBUG = 0
OPT = -O3

APP_TYPE = BOOT_SRAM
LDSCRIPT = chompi_sram.lds

CPP_SOURCES = chompi_main.cpp

# SD-card state (BOWLS/state.json): FatFS + coreJSON (vendored)
USE_FATFS = 1
C_SOURCES = ../libs/coreJSON/source/core_json.c
C_INCLUDES += -I../libs/coreJSON/source/include

LIBDAISY_DIR = ../libs/libDaisy
DAISYSP_DIR = ../libs/DaisySP

SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core

ifdef CPU_METER
C_DEFS += -DCPU_METER
endif

include $(SYSTEM_FILES_DIR)/Makefile

.PHONY: check-toolchain
check-toolchain:
	@$(CXX) --version | head -n 1
	@$(CXX) --version | head -n 1 | grep -q "10.3.1" \
		&& echo "Toolchain OK: GCC 10.3.1" \
		|| (echo "ERROR: expected GCC 10.3.1 (Arm GNU 10.3-2021.10)"; exit 1)