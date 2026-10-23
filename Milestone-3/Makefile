# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
# Makefile for nbt-aad (QNX Neutrino ARMv7LE)
# Target: Cortex-A15 Little Endian (BMW NBT Evo)

QCC ?= qcc
TARGET_ARCH ?= -Vgcc_ntoarmv7le_cpp

CXX = $(QCC) $(TARGET_ARCH)
CC  = $(QCC) -Vgcc_ntoarmv7le

INCLUDES = -I./include -I./src
# Remove -fPIC and -Bdynamic to produce a true QNX executable (ET_EXEC), not a shared lib (ET_DYN)
CXXFLAGS = -O2 -Wall -D_QNX_SOURCE -Wc,-std=gnu++0x -march=armv7-a -mfpu=vfpv3 -mfloat-abi=softfp $(INCLUDES)
# Set RPATH so QNX's dynamic linker automatically finds liba2i_client.so in /opt/mm/lib without copying
LDFLAGS  = -L./lib -la2i_client -lsocket -lssl -lcrypto -lscreen -lang-c++ -Wl,-rpath,/opt/mm/lib:/lib:/usr/lib

SRCS = src/main.cpp \
       src/audio_manager.cpp \
       src/video_manager.cpp \
       src/input_manager.cpp \
       src/tcp_server.cpp \
       src/aa_session.cpp \
       src/aa_cryptor.cpp \
       src/bluetooth_manager.cpp \
       src/wireless_setup.cpp \
       src/touch_probe.cpp \
       src/dsi_client.cpp \
       src/sdr_builder.cpp \
       src/crash_handler.cpp

OBJS = $(SRCS:.cpp=.o)
TARGET = nbt-aad

.PHONY: all clean

all: $(TARGET) test_display

$(TARGET): $(OBJS)
	@echo [LINK] $@
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS) $(LDFLAGS)
	@echo [DONE] Built $(TARGET) for QNX armv7le

test_display: test_display.cpp
	@echo [CXX] test_display.cpp
	$(CXX) $(CXXFLAGS) -o test_display test_display.cpp -L./lib -lscreen -lang-c++
	@echo [DONE] Built test_display for QNX armv7le

%.o: %.cpp
	@echo [CXX] $<
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	@echo [CLEAN] Removing object files and binaries
	rm -f $(OBJS) $(TARGET)
