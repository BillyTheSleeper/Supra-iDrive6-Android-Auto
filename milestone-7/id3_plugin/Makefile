# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Build libNbtAad.so - the id3 (Intel NBT / iDrive 4/5, QNX x86) Android Auto menu-entry plugin.
#
# The id3 framework is the OLDER "hmi2012" App Framework (not ID5/ID6 RHMI). Its ABI differs from ID5/ID6:
#   * bmw::PluginApp constructor is 4-arg (no major/minor/rhmiInterfaceVersion)
#   * pluginAppInfo is 20 bytes (no rhmiInterfaceVersion field)
# lib/ holds copies of the id3's own libAppFramework.A.so + libCoreFramework.A.so (pulled from
# /fs/sda0/opt/hmi/lib/), used only for linking; the symbols resolve at load time on the car.
#
# Toolchain (QNX 6.5.0 x86):
#   export QNX_HOST=/opt/qnx650/host/linux/x86
#   export QNX_TARGET=/opt/qnx650/target/qnx6
#   export PATH=/opt/qnx650/host/linux/x86/usr/bin:$PATH
#   make ARCH=x86

QCC ?= qcc
ARCH ?= x86

ifeq ($(ARCH),x86)
GPP_TARGET := -Vgcc_ntox86_gpp
ARCH_FLAGS :=
else
GPP_TARGET := -Vgcc_ntoarmv7le_gpp
ARCH_FLAGS := -march=armv7-a -mfpu=vfpv3 -mfloat-abi=softfp
endif

PLUGIN_CXX      = $(QCC) $(GPP_TARGET)
PLUGIN_CXXFLAGS = -O2 -Wall -D_QNX_SOURCE $(ARCH_FLAGS) -fPIC -I.
RHMI_LIBDIR ?= ./lib
RHMI_LIBS = $(wildcard $(RHMI_LIBDIR)/libAppFramework.A.so $(RHMI_LIBDIR)/libCoreFramework.A.so)

TARGET = libNbtAad.so

.PHONY: all clean

all: $(TARGET)

$(TARGET): nbt_aad_app.cpp
	@echo [BUILD] id3 PluginApp $@
	$(PLUGIN_CXX) $(PLUGIN_CXXFLAGS) -shared -Wl,-soname,libNbtAad.so -o $@ nbt_aad_app.cpp $(RHMI_LIBS) -Wl,--allow-shlib-undefined -lang-c++
	@echo [DONE] Built $@ - install with NbtAad.manifest, NbtAad.xml, NbtAad_ALL_*.zip into /fs/sda0/opt/hmi/data/ro/Plugins/NbtAad

clean:
	rm -f $(TARGET)
