// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// libhmiaad.so - NBT-AAD plugin for HmiMain3D (stage 1: load test only).
//
// Loaded by HmiMain3D from hmiplugins/NbtAadPlugin.xml at boot. Stage 1 does nothing except log that the
// framework created and started the bundle and the plugin, so we can prove the ABI on the car with no side effects.
// Log: /tmp/nbt-aad-plugin.log (append) and the system log (sloginfo, major code 0x4e41 'NA').
#include "plugin_abi.hpp"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#ifdef __QNX__
#include <sys/slog.h>
#include <sys/slogcodes.h>
#endif

#define NBT_AAD_PLUGIN_VERSION "plugin-s1 (" __DATE__ " " __TIME__ ")"

namespace {

void plog(const char* fmt, ...) {
    char msg[384];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    char line[448];
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    int n = snprintf(line, sizeof(line), "[%5ld.%03ld] [NbtAadPlugin] %s\n", (long)ts.tv_sec, (long)(ts.tv_nsec / 1000000), msg);
    if (n < 0) return;
    if (n >= (int)sizeof(line)) n = (int)sizeof(line) - 1;
    int fd = open("/tmp/nbt-aad-plugin.log", O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd >= 0) {
        ssize_t w = write(fd, line, (size_t)n);
        (void)w;
        close(fd);
    }
#ifdef __QNX__
    slogf(_SLOG_SETCODE(0x4e41, 0), _SLOG_INFO, "[NbtAadPlugin] %s", msg);
#endif
}

class NbtAadBundle : public nbt::IHmiPluginLike {
public:
    NbtAadBundle() { plog("bundle constructed (%p), %s", (void*)this, NBT_AAD_PLUGIN_VERSION); }
    virtual ~NbtAadBundle() { plog("bundle destroyed (%p)", (void*)this); }
    virtual int start(void* resolver) {
        plog("bundle start(resolver %p) pid %d - stage 1: nothing to do", resolver, (int)getpid());
        return 0;
    }
    virtual int stop(void* resolver) {
        plog("bundle stop(resolver %p)", resolver);
        return 0;
    }
};

class NbtAadGate : public nbt::IHmiPluginLike {
public:
    NbtAadGate() { plog("gate plugin constructed (%p)", (void*)this); }
    virtual ~NbtAadGate() { plog("gate plugin destroyed (%p)", (void*)this); }
    virtual int start(void* resolver) {
        plog("gate plugin start(resolver %p) - stage 1: input gate not implemented, iDrive untouched", resolver);
        return 0;
    }
    virtual int stop(void* resolver) {
        plog("gate plugin stop(resolver %p)", resolver);
        return 0;
    }
};

void createBundle(void** iface, void** plugin, void** object) {
    NbtAadBundle* b = new NbtAadBundle();
    nbt::IHmiPluginLike* p = b;
    if (iface) *iface = p;
    if (plugin) *plugin = p;
    if (object) *object = p;
    plog("createFunctionNbtAadBundle -> %p", (void*)p);
}

void destroyBundle(void* object) {
    plog("destroyFunctionNbtAadBundle(%p)", object);
    delete static_cast<nbt::IHmiPluginLike*>(object);
}

void createGate(void** iface, void** plugin, void** object) {
    NbtAadGate* g = new NbtAadGate();
    nbt::IHmiPluginLike* p = g;
    if (iface) *iface = p;
    if (plugin) *plugin = p;
    if (object) *object = p;
    plog("createFunctionCNbtAadGate -> %p", (void*)p);
}

void destroyGate(void* object) {
    plog("destroyFunctionCNbtAadGate(%p)", object);
    delete static_cast<nbt::IHmiPluginLike*>(object);
}

} // namespace

// Looked up by name (exportedSymbolName + "Struct") from NbtAadPlugin.xml.
extern "C" {
__attribute__((visibility("default"))) nbt::PluginStruct NbtAadBundleStruct = { &createBundle, &destroyBundle, NBT_AAD_PLUGIN_COMPILER };
__attribute__((visibility("default"))) nbt::PluginStruct CNbtAadGateStruct  = { &createGate,   &destroyGate,   NBT_AAD_PLUGIN_COMPILER };
}
