TARGET = ammonite

# ReverbSc lives in DaisySP-LGPL (no trailing comment on this line: make
# would keep the spaces and the ifeq in the core Makefile would fail)
USE_DAISYSP_LGPL = 1

CPP_SOURCES = synth_main.cpp ../core/synth_core.cpp

C_INCLUDES += -I../core

LIBDAISY_DIR = ../lib/libDaisy
DAISYSP_DIR = ../lib/DaisySP

SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
