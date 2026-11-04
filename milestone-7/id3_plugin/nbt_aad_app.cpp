// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// libNbtAad.so - NBT-AAD "Android Auto" menu entry for the id3 (Intel NBT, iDrive 4/5, QNX x86).
//
// The id3's HMI is NOT the ID5/ID6 RHMI framework. It is the older "hmi2012" App Framework living in
// /fs/sda0/opt/hmi/lib/libAppFramework.A.so + libCoreFramework.A.so, loaded by /fs/sda0/opt/hmi/bin/zipped/HmiMain3D.
// Plugins are plain shared objects (the one real example is SecurityMenu, an "App" plugin with a
// {pluginDir}/libSecurityMenu.so). The manifest is <Name>.manifest; HmiMain3D's CAppManager scans every
// <ro|rw>/Plugins/<Name>/<Name>.manifest at boot, dlopen()s the <file> and dlsym()s "pluginAppInfo".
//
// This file mirrors the daemon-control logic of rhmiapp/nbt_aad_app.cpp (the ID5/ID6 app) and plugin/nbt_aad_plugin.cpp,
// reduced to the essential surface, and adapted to the id3 framework's older ABI:
//
//   * pluginAppInfo is 20 bytes { u16 major, u16 minor, name, vendor, create, destroy }   (id3 / SecurityMenu layout)
//     - the ID5/ID6 layout adds a u32 rhmiInterfaceVersion between vendor and create (24 bytes); the id3 does NOT.
//   * bmw::PluginApp's constructor is 4-arg: (IDispatcher&, BasicString<char>, const Bundle&, const Bundle&)
//     - the ID5/ID6 constructor is 6-arg (adds major, minor, RHMIInterfaceVersion); the id3 does NOT.
//   * the PluginApp vtable slots are identical on both: onError = 21, onInit = 22, onFinalize = 23 (34 slots total).
//
// Daemon control protocol (see milestone-7/src/main.cpp):
//   * single-instance claim on UDP 127.0.0.1:52790 (NBT_CTL_PORT overrides)
//   * commands (plain text datagrams): "ping" -> "pong", "show" (overlay to front), "quit" (clean stop),
//     "hide" (step aside), "gear <n>", "pdc <n>", "daynight <n>", "drivemode <prop> <n> [prev]"
//   * UI-state file /dev/shmem/nbt-aad-state, written by the daemon as "%-11.11s %10d %d\n":
//     state = idle | connecting | connected, pid, overlay (1 = on screen, 2 = BACK held, 3 = menu key went to phone)
//   * start = spawn /opt/aa/nbt-aad (or "show" a running instance), stop = "quit" (+SIGTERM 3 s, SIGKILL 10 s),
//     restart = stop then start. A stale state file with a dead pid counts as "idle" (pid liveness via kill(pid,0)).
//
// Off switch: touch /opt/aa/app.disable, then reboot.
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <new>
#include <sys/stat.h>
#include <spawn.h>
#include <dlfcn.h>
#ifndef RTLD_DEFAULT
#define RTLD_DEFAULT ((void*)-2)   // QNX value
#endif
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#ifdef __QNX__
#include <sys/slog.h>
#include <sys/slogcodes.h>
#endif

#define NBT_AAD_APP_VERSION "id3-plugin-1 (" __DATE__ " " __TIME__ ")"

extern char** environ;

// ---- minimal declarations of BMW's App Framework (libAppFramework.A.so / libCoreFramework.A.so) ------------------
// Names and parameter types only; nothing is defined here. They produce the exported, mangled symbols that the
// dynamic linker resolves against the framework libs. Layouts/ABI taken from libSecurityMenu.so (id3) and
// rhmiapp/nbt_aad_app.cpp (ID5/ID6); the id3 differences are noted above.
namespace bmw {
class IDispatcher;
class Bundle;
class Runnable;

class Parcel {
public:
    explicit Parcel(bool value);             // _ZN3bmw6ParcelC1Eb (libCoreFramework), 0x14 bytes, polymorphic
    ~Parcel();                               // _ZN3bmw6ParcelD1Ev
    void readBool(bool& value) const;        // _ZNK3bmw6Parcel8readBoolERb
private:
    unsigned char m_storage[32];             // >= sizeof(bmw::Parcel) (0x14)
};

template <typename T> class BasicString {
public:
    BasicString(const T* s);                 // _ZN3bmw11BasicStringIcEC1EPKc
    BasicString(const BasicString& other);   // _ZN3bmw11BasicStringIcEC1ERKS1_
    ~BasicString();                          // _ZN3bmw11BasicStringIcED1Ev
    const T* raw() const { return m_data; }  // data pointer is the only member
private:
    T* m_data;
};

// Text-DB reference ({u32 id, u32 type}); updateModel(textIdModel, TextID(n)) re-points a text.
class TextID {
public:
    explicit TextID(unsigned id);            // _ZN3bmw6TextIDC1Ej
private:
    unsigned m_id;
    unsigned m_type;
};

// Image-DB reference ({u32 id, u32 type 0x2b}); updateModel(imageIdModel, ImageID(n)) re-points an icon.
class ImageID {
public:
    explicit ImageID(unsigned id);           // _ZN3bmw7ImageIDC1Ej
private:
    unsigned m_id;
    unsigned m_type;
};

template <typename R, typename A1, typename A2, typename A3, typename A4> struct Delegate {
    void* object;
    R (*stub)(void* object, A1 a1);
};
typedef Delegate<bool, const Parcel&, void, void, void> ActionDelegate;

// The id3's PluginApp: 4-arg constructor, no RHMI interface version. vtable slots 21/22/23 = onError/onInit/onFinalize.
const size_t kPluginAppStorage = 0x400;      // generously sized; the ID5/6 PluginApp is 0x200, the id3 one is <= that
enum { kSlotOnError = 21, kSlotOnInit = 22, kSlotOnFinalize = 23, kVtblSlots = 34 };

class PluginApp {
public:
    PluginApp(IDispatcher& dispatcher, BasicString<char> name, const Bundle& params, const Bundle& config);
    void addHandler(unsigned actionId, const ActionDelegate& handler);   // _ZN3bmw9PluginApp10addHandlerEjRK...
    void addHandler(unsigned id, unsigned type, const ActionDelegate& handler);
    void triggerEvent(unsigned eventId);                                 // _ZN3bmw9PluginApp12triggerEventEj
    void post(Runnable* runnable, unsigned delayMs);                     // _ZN3bmw9PluginApp4postEPNS_8RunnableEj
    void updateModel(unsigned modelId, const TextID& text);              // _ZN3bmw9PluginApp11updateModelEjRKNS_6TextIDE
    void updateModel(unsigned modelId, const ImageID& image);            // _ZN3bmw9PluginApp11updateModelEjRKNS_7ImageIDE
    bool onInit();                                                       // base implementations, called from our overrides
    void onFinalize();
};
} // namespace bmw

namespace {

const char* const kDaemon     = "/opt/aa/nbt-aad";      // where the ported daemon is installed (see README)
const char* const kDisable    = "/opt/aa/app.disable";
const char* const kDaemonEnv  = "/opt/aa/nbt-aad.env";
const char* const kDaemonLog  = "/dev/shmem/nbt-aad.log";
const uint16_t    kDaemonCtlPort = 52790;               // nbt-aad control port (UDP 127.0.0.1)
const char* const kStateFile  = "/dev/shmem/nbt-aad-state";

// NbtAad.xml ids (mirrors rhmiapp/NbtAad.xml, adapted to the id3 vocabulary).
const unsigned kEntryAction   = 2;    // <entryButton action="2"> -> start/show on entry
const unsigned kActStart      = 21;   // "Start Android Auto"
const unsigned kActStop       = 22;   // "Force Stop"
const unsigned kActRestart    = 23;   // "Restart"
const unsigned kMenuState     = 10;   // our menu hmiState
const unsigned kEntryTextModel = 3;   // <entryButton model="3"> = textIdModel 3
const unsigned kStartRowModel = 31;   // textIdModel of the Start row
const unsigned kTextDefault    = 1;   // "Android Auto"
const unsigned kTextConnecting = 2;   // "Android Auto: Connecting..."
const unsigned kTextStart      = 10;  // "Start Android Auto"
const unsigned kTextRowConnecting = 13; // "Connecting..."
const unsigned kTextRowOpen    = 14;  // "Open Android Auto"
const unsigned kStatusPollMs   = 1000;

// ---- logging (slog + /dev/shmem) ------------------------------------------------------------------------------
void alog(const char* fmt, ...) {
    char msg[384];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    char line[448];
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    int n = snprintf(line, sizeof(line), "[%5ld.%03ld] [NbtAadApp] %s\n", (long)ts.tv_sec, (long)(ts.tv_nsec / 1000000), msg);
    if (n < 0) return;
    if (n >= (int)sizeof(line)) n = (int)sizeof(line) - 1;
    int fd = open("/dev/shmem/nbt-aad-app.log", O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd >= 0) { ssize_t w = write(fd, line, (size_t)n); (void)w; close(fd); }
#ifdef __QNX__
    slogf(_SLOG_SETCODE(0x4e41, 1), _SLOG_INFO, "[NbtAadApp] %s", msg);
#endif
}

bool fileExists(const char* p) {
    struct stat st;
    return stat(p, &st) == 0;
}

// ---- launching the daemon (same as plugin/nbt_aad_plugin.cpp) -------------------------------------------------
time_t g_lastLaunch = 0;

void launchDaemon() {
    time_t now = time(NULL);
    if (g_lastLaunch && now - g_lastLaunch < 3) { alog("launch ignored (pressed again within 3 s)"); return; }
    g_lastLaunch = now;
    if (!fileExists(kDaemon)) { alog("launch: %s not found", kDaemon); return; }

    static char envbuf[16][160];
    char* envp[16 + 64];
    int ne = 0;
    strcpy(envbuf[0], "PATH=/bin:/usr/bin:/opt/os/bin:/opt/sys/bin:/opt/core/bin:/opt/car/bin:/fs/sda0/opt/mm/bin");
    envp[ne++] = envbuf[0];
    int nb = 1;
    FILE* f = fopen(kDaemonEnv, "r");
    if (f) {
        char line[160];
        while (nb < 16 && fgets(line, sizeof(line), f)) {
            size_t l = strlen(line);
            while (l && (line[l - 1] == '\n' || line[l - 1] == '\r' || line[l - 1] == ' ')) line[--l] = 0;
            if (!l || line[0] == '#' || !strchr(line, '=')) continue;
            strcpy(envbuf[nb], line);
            envp[ne++] = envbuf[nb++];
        }
        fclose(f);
    } else {
        strcpy(envbuf[1], "NBT_AUDIO_BT=1");
        envp[ne++] = envbuf[1];
        nb = 2;
    }
    for (char** e = environ; e && *e && ne < (int)(sizeof(envp) / sizeof(envp[0])) - 1; ++e) {
        if (strncmp(*e, "PATH=", 5) == 0) continue;
        envp[ne++] = *e;
    }
    envp[ne] = NULL;

    char* argv[] = { const_cast<char*>(kDaemon), NULL };
    int in  = open("/dev/null", O_RDONLY);
    int out = open(kDaemonLog, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (in < 0 || out < 0) {
        alog("launch: cannot open /dev/null or %s (errno %d)", kDaemonLog, errno);
        if (in >= 0) close(in);
        if (out >= 0) close(out);
        return;
    }
#ifdef __QNX__
    int fdmap[3] = { in, out, out };
    struct inheritance inh;
    memset(&inh, 0, sizeof(inh));
    inh.flags = SPAWN_SETGROUP | SPAWN_SETSIGDEF | SPAWN_SETSIGMASK | SPAWN_NOZOMBIE;
    inh.pgroup = SPAWN_NEWPGROUP;
    sigfillset(&inh.sigdefault);
    sigemptyset(&inh.sigmask);
    pid_t pid = spawn(kDaemon, 3, fdmap, &inh, argv, envp);
#else
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, in, 0);
    posix_spawn_file_actions_adddup2(&fa, out, 1);
    posix_spawn_file_actions_adddup2(&fa, out, 2);
    pid_t pid = -1;
    if (posix_spawn(&pid, kDaemon, &fa, NULL, argv, envp) != 0) pid = -1;
    posix_spawn_file_actions_destroy(&fa);
#endif
    int e = errno;
    close(in);
    close(out);
    if (pid < 0) alog("launch: spawn %s failed (errno %d)", kDaemon, e);
    else alog("launch: started %s pid %d (output -> %s)", kDaemon, (int)pid, kDaemonLog);
}

// ---- daemon control (UDP 127.0.0.1:52790) ---------------------------------------------------------------------
void sendToDaemon(const char* msg) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return;
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(kDaemonCtlPort);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sendto(fd, msg, strlen(msg), 0, (struct sockaddr*)&a, sizeof(a));
    close(fd);
}

struct DaemonState {
    char state[24];     // idle | connecting | connected
    int pid;            // > 0 and alive, else 0
    int overlay;        // 1 = on screen, 2 = BACK held, 3 = menu key went to the phone
};

bool pidAlive(int pid) {
    if (pid <= 0) return false;
    if (kill((pid_t)pid, 0) == 0) return true;
    return errno == EPERM;
}

bool controlPortBusy() {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return false;
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(kDaemonCtlPort);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const char ping[] = "ping";
    bool busy = false;
    if (sendto(fd, ping, 4, 0, (struct sockaddr*)&a, sizeof(a)) == 4) {
        fd_set rf;
        FD_ZERO(&rf);
        FD_SET(fd, &rf);
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 200000;
        if (select(fd + 1, &rf, NULL, NULL, &tv) > 0) {
            char buf[8];
            ssize_t n = recv(fd, buf, sizeof(buf), 0);
            busy = (n >= 4 && memcmp(buf, "pong", 4) == 0);
        }
    }
    close(fd);
    return busy;
}

DaemonState readDaemonState() {
    DaemonState d;
    strcpy(d.state, "idle");
    d.pid = 0;
    d.overlay = 0;
    FILE* f = fopen(kStateFile, "r");
    if (f) {
        char st[24] = "";
        int pid = 0, ov = 0;
        int n = fscanf(f, "%23s %d %d", st, &pid, &ov);
        fclose(f);
        if (n >= 2 && pidAlive(pid)) {
            strcpy(d.state, st);
            d.pid = pid;
            d.overlay = (n >= 3) ? ov : 0;
            return d;
        }
    }
    if (controlPortBusy()) {
        strcpy(d.state, "running");
        d.pid = -1;
    }
    return d;
}

// ---- start / stop / restart -----------------------------------------------------------------------------------
void startDaemonOrShow() {
    DaemonState d = readDaemonState();
    if (d.pid != 0) {
        alog("start: nbt-aad already running (pid %d, %s): asking it to come to the front", d.pid, d.state);
        sendToDaemon("show");
    } else {
        launchDaemon();
    }
}

pid_t g_stopPid = 0;
time_t g_stopTime = 0;
int g_stopStage = 0;                 // 0 quit sent, 1 SIGTERM sent, 2 SIGKILL sent
bool g_restartAfterStop = false;

void beginStop(bool restart) {
    DaemonState d = readDaemonState();
    g_restartAfterStop = restart;
    if (d.pid == 0) {
        alog("%s: nbt-aad is not running", restart ? "restart" : "force stop");
        if (restart) { g_lastLaunch = 0; startDaemonOrShow(); }
        return;
    }
    g_stopPid = (pid_t)d.pid;
    g_stopTime = time(NULL);
    g_stopStage = 0;
    sendToDaemon("quit");
    alog("%s: sent 'quit' to nbt-aad pid %d", restart ? "restart" : "force stop", d.pid);
}

void serviceStop() {
    if (!g_stopPid) return;
    bool gone = g_stopPid > 0 ? !pidAlive((int)g_stopPid) : !controlPortBusy();
    if (gone) {
        alog("nbt-aad (pid %d) has exited", (int)g_stopPid);
        g_stopPid = 0;
        if (g_restartAfterStop) {
            g_restartAfterStop = false;
            g_lastLaunch = 0;
            alog("restart: starting nbt-aad again");
            startDaemonOrShow();
        }
        return;
    }
    long waited = (long)(time(NULL) - g_stopTime);
    if (g_stopPid < 0) {
        if (waited >= 10) { alog("nbt-aad (pid unknown) still running 10 s after 'quit': giving up"); g_stopPid = 0; g_restartAfterStop = false; }
        return;
    }
    if (g_stopStage == 0 && waited >= 3) {
        alog("nbt-aad pid %d still running 3 s after 'quit': SIGTERM", (int)g_stopPid);
        kill(g_stopPid, SIGTERM);
        g_stopStage = 1;
    } else if (g_stopStage == 1 && waited >= 10) {
        alog("nbt-aad pid %d still running 10 s after 'quit': SIGKILL", (int)g_stopPid);
        kill(g_stopPid, SIGKILL);
        g_stopStage = 2;
    } else if (g_stopStage == 2 && waited >= 12) {
        alog("nbt-aad pid %d survived SIGKILL; giving up", (int)g_stopPid);
        g_stopPid = 0;
        g_restartAfterStop = false;
    }
}

// ---- action handlers ------------------------------------------------------------------------------------------
bool onEntryAction(void*, const bmw::Parcel&) {
    alog("entry button pressed (raAction %u): start / show", kEntryAction);
    startDaemonOrShow();
    return true;
}

bool guardedByOverlay(const char* what) {
    DaemonState d = readDaemonState();
    if (d.pid > 0 && d.overlay) {
        alog("%s ignored: Android Auto is on screen (stray knob input); exit to iDrive first", what);
        return true;
    }
    return false;
}

bool onStartRow(void*, const bmw::Parcel&) {
    alog("menu: Start (raAction %u)", kActStart);
    if (g_stopPid) { alog("start ignored: a stop/restart is in progress"); return true; }
    startDaemonOrShow();
    return true;
}

bool onStopRow(void*, const bmw::Parcel&) {
    alog("menu: Force Stop (raAction %u)", kActStop);
    if (g_stopPid || guardedByOverlay("force stop")) return true;
    beginStop(false);
    return true;
}

bool onRestartRow(void*, const bmw::Parcel&) {
    alog("menu: Restart (raAction %u)", kActRestart);
    if (g_stopPid || guardedByOverlay("restart")) return true;
    beginStop(true);
    return true;
}

// ---- status polling (self-reposting bmw::Runnable, runs on the app thread) -----------------------------------
// bmw::Runnable stand-in (vtable: D1, D0, run). Static object, destructor slots are no-ops.
struct GearRunnable { void** vptr; };
GearRunnable g_statusRunnable;
void* g_statusVtbl[5];
bmw::PluginApp* g_runApp = NULL;
unsigned g_shownText = kTextDefault;
unsigned g_shownRow = kTextStart;

void runnableNoop(void*) {}

void statusRun(void*) {
    bmw::PluginApp* app = g_runApp;
    if (!app) return;
    serviceStop();
    DaemonState d = readDaemonState();
    bool connecting = !strcmp(d.state, "connecting"), connected = !strcmp(d.state, "connected");

    unsigned want = connecting ? kTextConnecting : kTextDefault;
    if (want != g_shownText) {
        bmw::TextID text(want);
        app->updateModel(kEntryTextModel, text);
        alog("entry text -> %u (%s)", want, want == kTextConnecting ? "connecting" : "default");
        g_shownText = want;
    }
    unsigned row = connected ? kTextRowOpen : connecting ? kTextRowConnecting : kTextStart;
    if (row != g_shownRow) {
        bmw::TextID text(row);
        app->updateModel(kStartRowModel, text);
        alog("start row text -> %u (%s)", row, d.state);
        g_shownRow = row;
    }
    app->post(reinterpret_cast<bmw::Runnable*>(&g_statusRunnable), kStatusPollMs);
}

void startStatusPolling(bmw::PluginApp* app) {
    void** base = reinterpret_cast<void**>(dlsym(RTLD_DEFAULT, "_ZTVN3bmw8RunnableE"));
    if (base) memcpy(g_statusVtbl, base, sizeof(g_statusVtbl));
    else { g_statusVtbl[0] = NULL; g_statusVtbl[1] = NULL; }
    g_statusVtbl[2] = reinterpret_cast<void*>(&runnableNoop);
    g_statusVtbl[3] = reinterpret_cast<void*>(&runnableNoop);
    g_statusVtbl[4] = reinterpret_cast<void*>(&statusRun);
    g_statusRunnable.vptr = g_statusVtbl + 2;
    g_runApp = app;
    app->post(reinterpret_cast<bmw::Runnable*>(&g_statusRunnable), 2000);
    alog("status polling posted (%s every %u ms)", kStateFile, kStatusPollMs);
}

// ---- the app object -------------------------------------------------------------------------------------------
void* g_vtbl[2 + bmw::kVtblSlots];   // [offset-to-top, typeinfo, slots...] copied from bmw::PluginApp's vtable
bmw::PluginApp* g_app = NULL;

void addAction(bmw::PluginApp* self, unsigned id, bool (*stub)(void*, const bmw::Parcel&)) {
    bmw::ActionDelegate d;
    d.object = self;
    d.stub = stub;
    self->addHandler(id, d);
}

bool appOnInit(bmw::PluginApp* self) {
    addAction(self, kEntryAction, &onEntryAction);
    addAction(self, kActStart, &onStartRow);
    addAction(self, kActStop, &onStopRow);
    addAction(self, kActRestart, &onRestartRow);
    bool ok = self->onInit();
    alog("onInit: raActions %u/%u/%u/%u registered, base onInit -> %d", kEntryAction, kActStart, kActStop, kActRestart, (int)ok);
    startStatusPolling(self);
    return ok;
}

void appOnError(bmw::PluginApp* /*self*/, unsigned code, const bmw::BasicString<char>& text) {
    const char* s = text.raw();
    alog("onError %u: %s", code, s ? s : "(null)");
}

void appOnFinalize(bmw::PluginApp* self) {
    alog("onFinalize");
    g_runApp = NULL;
    self->onFinalize();
}

bmw::PluginApp* createApp(bmw::IDispatcher& dispatcher, const char* name, const bmw::Bundle& params, const bmw::Bundle& config) {
    alog("create(name=%s) %s", name ? name : "(null)", NBT_AAD_APP_VERSION);
    if (fileExists(kDisable)) { alog("disabled by %s", kDisable); return NULL; }
    if (g_app) { alog("create: already created"); return NULL; }

    void* mem = ::operator new(bmw::kPluginAppStorage);
    memset(mem, 0, bmw::kPluginAppStorage);
    // id3: 4-arg constructor (no major/minor/rhmiVersion).
    bmw::PluginApp* app = new (mem) bmw::PluginApp(dispatcher, bmw::BasicString<char>(name ? name : "NbtAad"), params, config);

    // Own vtable: identical to bmw::PluginApp's, with our onInit/onError/onFinalize.
    void** base = *reinterpret_cast<void***>(app);
    memcpy(g_vtbl, base - 2, sizeof(g_vtbl));
    g_vtbl[2 + bmw::kSlotOnInit]     = reinterpret_cast<void*>(&appOnInit);
    g_vtbl[2 + bmw::kSlotOnError]    = reinterpret_cast<void*>(&appOnError);
    g_vtbl[2 + bmw::kSlotOnFinalize] = reinterpret_cast<void*>(&appOnFinalize);
    *reinterpret_cast<void***>(app) = g_vtbl + 2;

    g_app = app;
    alog("create: app %p (PluginApp vtable %p)", (void*)app, (void*)base);
    return app;
}

void destroyApp(void* app) {
    alog("destroy(%p)", app);
    if (!app) return;
    void** vt = *reinterpret_cast<void***>(app);
    reinterpret_cast<void (*)(void*)>(vt[1])(app);   // deleting destructor (PluginApp::~PluginApp + operator delete)
    if (app == g_app) g_app = NULL;
}

__attribute__((constructor)) void libraryLoaded() {
    alog("library loaded into pid %d, %s", (int)getpid(), NBT_AAD_APP_VERSION);
}

} // namespace

// ---- entry point looked up by the plugin manager (dlsym "pluginAppInfo") --------------------------------------
// The id3 layout is 20 bytes (SecurityMenu): { u16 major, u16 minor, name, vendor, create, destroy }.
// No rhmiInterfaceVersion field (that is the ID5/ID6 RHMI layout, 24 bytes).
extern "C" {
struct NbtPluginAppInfo {
    uint16_t versionMajor;
    uint16_t versionMinor;
    const char* name;
    const char* vendor;
    bmw::PluginApp* (*create)(bmw::IDispatcher&, const char*, const bmw::Bundle&, const bmw::Bundle&);
    void (*destroy)(void*);
};
__attribute__((visibility("default"))) NbtPluginAppInfo pluginAppInfo = {
    1, 0, "Android Auto", "NBT-AAD", &createApp, &destroyApp
};
}
