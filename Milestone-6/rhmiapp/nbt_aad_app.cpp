// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// libNbtAad.so - NBT-AAD as a native iDrive "PluginApp" (same framework as BMW's SecurityMenu / ActiveSeating).
//
// HmiMain3D's CAppManager loads every <ro|rw>/Plugins/<Name>/<Name>.manifest at boot (no coding whitelist; the
// only gate is CSpecialAppsHelper = coding A4A / A4A_REMOTE_APPS / REMOTE_HMI / TOUCH_COMMAND). The manifest names
// this library, the UI description (NbtAad.xml) and the text/image DBs. The XML declares one entry button; its
// placement comes from the XML (applicationType="Custom" + insertStateID/insertAfterFocusID, like ActiveSeating),
// so iDrive itself creates and places the menu row. Pressing it opens our own menu (hmiState 10, pure XML hmiAction):
//   Start Android Auto  (raAction 21) - launches /opt/aa/nbt-aad, or brings a running one to the front ("show");
//                                       the row reads "Connecting..." / "Open Android Auto" with the daemon state
//   Force Stop          (raAction 22) - "quit", then SIGTERM after 3 s, SIGKILL after 6 s
//   Restart             (raAction 23) - Force Stop, then Start
// When the phone connects after Start, the app sends iDrive back to the main menu (focusEvent 50 -> NativeStateID 1,
// the way BMW's ActiveSeating leaves its own screen), so stray knob presses under the Android Auto overlay land
// there and not on Force Stop / Restart. Those two rows are also ignored while the overlay is up.
// The old one-press entry (raAction 2 -> start) is still handled, for NbtAad_Direct.xml.
//
// ABI (from libSecurityMenu.so / libAppFramework.A.so, see plugin/plugin_abi.hpp notes):
//   pluginAppInfo = { u16 1, u16 0, name, vendor, u32 rhmiVersion 1, create, destroy }
//   create(IDispatcher&, const char* name, const Bundle& params, const Bundle& config) -> IPluginApp*
//   bmw::PluginApp(IDispatcher&, BasicString<char>, 1, 0, RHMIInterfaceVersion 1, params, config)
//   PluginApp vtable (34 slots, no pure virtuals): 21 onError, 22 onInit, 23 onFinalize, 25 onCheckAppAvailable
//   PluginApp::addHandler(actionId, Delegate<bool, const Parcel&>{obj, stub})
// Our app object is a bmw::PluginApp built with the framework's own constructor; it gets its own copy of the
// PluginApp vtable with onInit/onError/onFinalize replaced (what a C++ subclass would do - we have no BMW headers).
//
// Logs: /dev/shmem/nbt-aad-app.log (+ slog major 0x4e41). Daemon output -> /dev/shmem/nbt-aad.log.
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
#include <netinet/in.h>
#include <arpa/inet.h>
#ifdef __QNX__
#include <sys/slog.h>
#include <sys/slogcodes.h>
#endif

#define NBT_AAD_APP_VERSION "rhmiapp-6 (" __DATE__ " " __TIME__ ")"

extern char** environ;

// ---- minimal declarations of BMW's RHMI app framework (libAppFramework.A.so / libCoreFramework.A.so) ----------
// Only names and parameter types matter (they produce the exported, mangled symbols); nothing here is defined.
namespace bmw {
class IDispatcher;
class Bundle;
class Runnable;
class Parcel {
public:
    explicit Parcel(bool value);             // _ZN3bmw6ParcelC1Eb (libCoreFramework), 0x14 bytes, polymorphic
    ~Parcel();                               // _ZN3bmw6ParcelD1Ev
    void readBool(bool& value) const;        // _ZNK3bmw6Parcel8readBoolERb (SecurityMenu_Base::onFocus)
private:
    unsigned char m_storage[32];             // >= sizeof(bmw::Parcel) (0x14)
};
enum RHMIInterfaceVersion { RHMI_INTERFACE_VERSION_1 = 1 };

template <typename T> class BasicString {
public:
    BasicString(const T* s);                 // _ZN3bmw11BasicStringIcEC1EPKc
    BasicString(const BasicString& other);   // _ZN3bmw11BasicStringIcEC1ERKS1_
    ~BasicString();                          // _ZN3bmw11BasicStringIcED1Ev
    const T* raw() const { return m_data; }  // data pointer is the only member (see SecurityMenu)
private:
    T* m_data;
};

// Text-DB reference (libCoreFramework: ID::ID(id, 0x28) -> { u32 id, u32 type }, trivially destructible).
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
typedef Delegate<bool, const BasicString<unsigned short>&, void, void, void> CdsDelegate;

const size_t kPluginAppStorage = 0x200;      // SecurityMenu's own members start at 0x160

class PluginApp {
public:
    PluginApp(IDispatcher& dispatcher, BasicString<char> name, unsigned major, unsigned minor,
              RHMIInterfaceVersion rhmi, const Bundle& params, const Bundle& config);
    void addHandler(unsigned actionId, const ActionDelegate& handler);
    // Notification handler for one of our hmiStates (type 1 = focus; the Parcel carries one bool "focused"):
    // SecurityMenu_Kommunikation::onInit registers addHandler(95 /*its hmiState*/, 1, ...).
    void addHandler(unsigned id, unsigned type, const ActionDelegate& handler);   // _ZN3bmw9PluginApp10addHandlerEjjRK...
    // Fires one of our XML <events>; a focusEvent whose targetModel is a NativeStateID raIntModel makes HmiMain3D
    // request that native state (ActiveSeating::onExitToSeatMenu: triggerEvent(23) -> NativeStateID 1789).
    void triggerEvent(unsigned eventId);                                          // _ZN3bmw9PluginApp12triggerEventEj
    void post(Runnable* runnable, unsigned delayMs);   // runs runnable->run() on the app thread after delayMs
    // Re-points a model at another text of our text DB. BMW's ActiveSeating sets its entry button text this way
    // (onInit: TextID(0x54) -> updateModel(2, ...)); we use it for the entry's "Connecting..." state.
    void updateModel(unsigned modelId, const TextID& text);   // _ZN3bmw9PluginApp11updateModelEjRKNS_6TextIDE
    void updateModel(unsigned modelId, const ImageID& image); // _ZN3bmw9PluginApp11updateModelEjRKNS_7ImageIDE
    // Component property: 1 enabled, 2 selectable, 3 visible, 8 LABEL_WAITINGANIMATION (iDrive's busy ring while any
    // component of the state has it set; HmiMain3D component -> state -> activity waitingAnimation).
    void setProperty(unsigned componentId, unsigned propertyId, const Parcel& value);   // _ZN3bmw9PluginApp11setPropertyEjjRKNS_6ParcelE
    // Car data (same call SecurityMenu uses for "specialvehicle.menucontrol"); value arrives as UTF-16 JSON text.
    void cdsAddPropertyChangedEventHandler(BasicString<char>& property, unsigned id, const CdsDelegate& handler);
    bool onInit();                           // base implementations, called from our overrides
    void onFinalize();
private:
    unsigned char m_storage[kPluginAppStorage];
};
} // namespace bmw

namespace {

const char* const kDaemon     = "/opt/aa/nbt-aad";
const char* const kDisable    = "/opt/aa/app.disable";
const char* const kDaemonEnv  = "/opt/aa/nbt-aad.env";
const char* const kDaemonLog  = "/dev/shmem/nbt-aad.log";
const char* const kAppLog     = "/dev/shmem/nbt-aad-app.log";
const unsigned    kEntryAction = 2;          // <entryButton action="2"> / <raAction id="2"/> in NbtAad.xml
const char* const kGearProperty = "driving.gear";   // CDS property (libhmiremoting.so property table)
const char* const kGearFile    = "/dev/shmem/nbt-aad-gear";
const uint16_t    kDaemonCtlPort = 52790;    // nbt-aad control port (UDP 127.0.0.1)
const char* const kStateFile   = "/dev/shmem/nbt-aad-state";   // written by nbt-aad: "<idle|connecting|connected> <pid>"
const char* const kStatusOff   = "/opt/aa/status.disable";      // keep the entry text static
const unsigned    kEntryTextModel = 3;       // <entryButton model="3"> = textIdModel 3 in NbtAad.xml
const unsigned    kTextDefault    = 1;       // "Android Auto"           (NbtAad_ALL_Texts.zip, make_db.py)
const unsigned    kTextConnecting = 2;       // "Android Auto: Connecting..."
const unsigned    kStatusPollMs   = 1000;
// Android Auto menu (NbtAad.xml): hmiState 10 with three rows.
const unsigned    kMenuState      = 10;      // <hmiState id="10">
const unsigned    kActStart       = 21;      // <raAction id="21"/> row "Start Android Auto"
const unsigned    kActStop        = 22;      // <raAction id="22"/> row "Force Stop"
const unsigned    kActRestart     = 23;      // <raAction id="23"/> row "Restart"
const unsigned    kStartRowModel  = 31;      // textIdModel of the Start row
const unsigned    kTextStart      = 10;      // "Start Android Auto"
const unsigned    kTextRowConnecting = 13;   // "Connecting..."
const unsigned    kTextRowOpen    = 14;      // "Open Android Auto"
const unsigned    kEvGoHome       = 50;      // <focusEvent id="50" targetModel="40"/>: NativeStateID 1 = iDrive main menu
const unsigned    kEvGoMediaList  = 51;      // <focusEvent id="51" targetModel="41"/>: NativeStateID 20 = Media/Radio list
const int         kHomeWindowS    = 180;     // after Start, go home on "connected" only within this time
// "Connecting" animation: the Media/Radio entry row has no busy property (entry buttons only take 3/23/53), so its icon
// (imageIdModel 4) cycles through 8 spinner frames (NbtAad_ALL_Images.zip 55010..55017); our menu's Start row uses the
// HMI's own busy ring (property 8 on button 61).
const unsigned    kEntryImageModel = 4;
const unsigned    kImgEntryIcon    = 55001;
const unsigned    kImgSpinFirst    = 55010;
const unsigned    kSpinFrames      = 8;
const unsigned    kSpinMs          = 200;
const unsigned    kStartButton     = 61;      // <button id="61"> (Start row)
const unsigned    kPropWaiting     = 8;       // LABEL_WAITINGANIMATION

// PluginApp vtable slots (index from the address point)
enum { kSlotOnError = 21, kSlotOnInit = 22, kSlotOnFinalize = 23, kVtblSlots = 34 };

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
    int fd = open(kAppLog, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd < 0) fd = open("/tmp/nbt-aad-app.log", O_WRONLY | O_CREAT | O_APPEND, 0666);   // host tests
    if (fd >= 0) {
        ssize_t w = write(fd, line, (size_t)n);
        (void)w;
        close(fd);
    }
#ifdef __QNX__
    slogf(_SLOG_SETCODE(0x4e41, 1), _SLOG_INFO, "[NbtAadApp] %s", msg);
#endif
}

bool fileExists(const char* p) {
    struct stat st;
    return stat(p, &st) == 0;
}

// ---- launching the daemon (same as plugin/nbt_aad_plugin.cpp) ---------------------------------------------------
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
    else alog("launch: started %s pid %d (%d env overrides, output -> %s)", kDaemon, (int)pid, nb, kDaemonLog);
}

// ---- car gear -> daemon ---------------------------------------------------------------------------------------
// The value is published as text (JSON-like, e.g. {"gear":2}); the first integer is the gear code. Every change is
// logged with the raw text so the code for reverse can be confirmed on the car, written to /dev/shmem/nbt-aad-gear
// (the daemon reads it at start) and sent to a running daemon as "gear <n>".
int g_lastGear = -1000;

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

bool onGearChanged(void* /*app*/, const bmw::BasicString<unsigned short>& value) {
    char text[160];
    size_t n = 0;
    const unsigned short* p = value.raw();
    while (p && p[n] && n < sizeof(text) - 1) { text[n] = (p[n] < 0x80) ? (char)p[n] : '?'; ++n; }
    text[n] = 0;
    int gear = -1;
    for (size_t i = 0; i < n; ++i) {
        if (text[i] >= '0' && text[i] <= '9') { gear = atoi(text + i); break; }
    }
    if (gear != g_lastGear) {
        alog("CDS %s = '%s' -> gear %d", kGearProperty, text, gear);
        g_lastGear = gear;
        FILE* f = fopen(kGearFile, "w");
        if (f) { fprintf(f, "gear %d\n", gear); fclose(f); }
        char msg[32];
        snprintf(msg, sizeof(msg), "gear %d", gear);
        sendToDaemon(msg);
    }
    return true;
}

// bmw::Runnable stand-in (vtable: D1, D0, run): posted once to subscribe on the app thread, like SecurityMenu does
// ~1 s after onInit. Static object, so the destructor slots are no-ops.
struct GearRunnable { void** vptr; };
GearRunnable g_gearRunnable;
void* g_runVtbl[5];
bmw::PluginApp* g_runApp = NULL;

void runnableNoop(void*) {}

void runnableRun(void* /*self*/) {
    if (!g_runApp) return;
    bmw::BasicString<char> prop(kGearProperty);
    bmw::CdsDelegate d;
    d.object = g_runApp;
    d.stub = &onGearChanged;
    g_runApp->cdsAddPropertyChangedEventHandler(prop, 0, d);
    alog("CDS handler for %s registered", kGearProperty);
}

void subscribeGear(bmw::PluginApp* app) {
    void** base = reinterpret_cast<void**>(dlsym(RTLD_DEFAULT, "_ZTVN3bmw8RunnableE"));
    if (base) memcpy(g_runVtbl, base, sizeof(g_runVtbl));   // keep offset-to-top + typeinfo of bmw::Runnable
    else { g_runVtbl[0] = NULL; g_runVtbl[1] = NULL; }
    g_runVtbl[2] = reinterpret_cast<void*>(&runnableNoop);
    g_runVtbl[3] = reinterpret_cast<void*>(&runnableNoop);
    g_runVtbl[4] = reinterpret_cast<void*>(&runnableRun);
    g_gearRunnable.vptr = g_runVtbl + 2;
    g_runApp = app;
    app->post(reinterpret_cast<bmw::Runnable*>(&g_gearRunnable), 1500);
    alog("gear subscription posted (Runnable vtable %s)", base ? "from libCoreFramework" : "built locally");
}

// ---- daemon state -> entry text, menu rows, stop / restart, return to the main menu ---------------------------------
// Polled on the app thread (a self-reposting Runnable, like the gear subscription). nbt-aad writes
// "<idle|connecting|connected> <pid> [overlay]" to /dev/shmem/nbt-aad-state; a file left behind by a daemon that is no
// longer running counts as idle.
GearRunnable g_statusRunnable;
void* g_statusVtbl[5];
unsigned g_shownText = kTextDefault;
unsigned g_shownRow = kTextStart;

struct DaemonState {
    char state[24];     // idle | connecting | connected
    int pid;            // > 0 and alive, else 0
    int overlay;        // 1 = Android Auto on screen, knob goes to the phone
};

bool pidAlive(int pid) {
    if (pid <= 0) return false;
    if (kill((pid_t)pid, 0) == 0) return true;
    return errno == EPERM;                               // exists, just not ours to signal
}

// Independent of the state file: a running nbt-aad owns UDP 127.0.0.1:52790 (single-instance control port).
bool controlPortBusy() {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return false;
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(kDaemonCtlPort);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bool busy = bind(fd, (struct sockaddr*)&a, sizeof(a)) != 0 && errno == EADDRINUSE;
    close(fd);
    return busy;
}

// pid > 0: running with a known pid. pid == -1: running (control port taken) but no usable state file.
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
        static bool s_logged = false;
        if (!s_logged) { alog("nbt-aad is running but %s is missing or stale (old daemon build?)", kStateFile); s_logged = true; }
        strcpy(d.state, "running");
        d.pid = -1;
    }
    return d;
}

bool g_focusKnown = false;     // a focus notification for our menu arrived at least once
bool g_menuFocused = false;
bool g_wantHome = false;       // Start was pressed: return to the main menu once the phone is connected
time_t g_startTime = 0;
char g_prevState[24] = "idle";
// Stop / restart in progress.
pid_t g_stopPid = 0;
time_t g_stopTime = 0;
int g_stopStage = 0;           // 0 quit sent, 1 SIGTERM sent, 2 SIGKILL sent
bool g_restartAfterStop = false;

// Where to go once connected: NBT_MENU_RETURN in /opt/aa/nbt-aad.env = home (default) | media | off.
unsigned menuReturnEvent() {
    FILE* f = fopen(kDaemonEnv, "r");
    if (!f) return kEvGoHome;
    char line[160];
    unsigned ev = kEvGoHome;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "NBT_MENU_RETURN=", 16) != 0) continue;
        const char* v = line + 16;
        if (!strncmp(v, "media", 5)) ev = kEvGoMediaList;
        else if (!strncmp(v, "off", 3) || v[0] == '0') ev = 0;
        else ev = kEvGoHome;
    }
    fclose(f);
    return ev;
}

void startDaemonOrShow() {
    DaemonState d = readDaemonState();
    g_wantHome = true;
    g_startTime = time(NULL);
    if (d.pid != 0) {
        alog("start: nbt-aad already running (pid %d, %s): asking it to come to the front", d.pid, d.state);
        sendToDaemon("show");
        if (!strcmp(d.state, "connected")) {            // already projecting: leave our menu right away
            g_wantHome = false;
            unsigned ev = menuReturnEvent();
            if (ev && g_runApp) { g_runApp->triggerEvent(ev); alog("start: already connected -> event %u", ev); }
        }
    } else {
        launchDaemon();
    }
}

void beginStop(bool restart) {
    DaemonState d = readDaemonState();
    g_restartAfterStop = restart;
    if (d.pid == 0) {
        alog("%s: nbt-aad is not running", restart ? "restart" : "force stop");
        if (restart) { g_lastLaunch = 0; startDaemonOrShow(); }
        return;
    }
    g_stopPid = (pid_t)d.pid;                            // -1: pid unknown, watch the control port instead
    g_stopTime = time(NULL);
    g_stopStage = 0;
    g_wantHome = false;
    sendToDaemon("quit");
    alog("%s: sent 'quit' to nbt-aad pid %d", restart ? "restart" : "force stop", d.pid);
}

// Stop / restart progress, called from the status poll (1 s).
void serviceStop() {
    if (!g_stopPid) return;
    bool gone = g_stopPid > 0 ? !pidAlive((int)g_stopPid) : !controlPortBusy();
    if (g_stopPid < 0 && !gone && time(NULL) - g_stopTime >= 3) {
        DaemonState d = readDaemonState();               // a state file may have appeared: escalate by pid then
        if (d.pid > 0) g_stopPid = (pid_t)d.pid;
    }
    if (gone) {
        alog("nbt-aad (pid %d) has exited", (int)g_stopPid);
        g_stopPid = 0;
        unlink(kStateFile);
        if (g_restartAfterStop) {
            g_restartAfterStop = false;
            g_lastLaunch = 0;
            alog("restart: starting nbt-aad again");
            startDaemonOrShow();
        }
        return;
    }
    long waited = (long)(time(NULL) - g_stopTime);
    if (g_stopPid < 0) {                                 // no pid: 'quit' is all we can do
        if (waited >= 10) { alog("nbt-aad (pid unknown) still running 10 s after 'quit': giving up"); g_stopPid = 0; g_restartAfterStop = false; }
        return;
    }
    if (g_stopStage == 0 && waited >= 3) {
        alog("nbt-aad pid %d still running 3 s after 'quit': SIGTERM", (int)g_stopPid);
        kill(g_stopPid, SIGTERM);
        g_stopStage = 1;
    } else if (g_stopStage == 1 && waited >= 6) {
        alog("nbt-aad pid %d still running 6 s after 'quit': SIGKILL", (int)g_stopPid);
        kill(g_stopPid, SIGKILL);
        g_stopStage = 2;
    } else if (g_stopStage == 2 && waited >= 12) {
        alog("nbt-aad pid %d survived SIGKILL; giving up", (int)g_stopPid);
        g_stopPid = 0;
        g_restartAfterStop = false;
    }
}

// ---- connecting animation ------------------------------------------------------------------------------------------
GearRunnable g_spinRunnable;
void* g_spinVtbl[5];
bool g_spinWanted = false;     // set by the status poll while nbt-aad is connecting
bool g_spinRunning = false;    // the Runnable is posted
unsigned g_spinFrame = 0;

void spinRun(void* /*self*/) {
    bmw::PluginApp* app = g_runApp;
    if (!app) { g_spinRunning = false; return; }
    if (!g_spinWanted) {                                 // done: back to the normal icon
        bmw::ImageID icon(kImgEntryIcon);
        app->updateModel(kEntryImageModel, icon);
        g_spinRunning = false;
        return;
    }
    bmw::ImageID frame(kImgSpinFirst + (g_spinFrame++ % kSpinFrames));
    app->updateModel(kEntryImageModel, frame);
    app->post(reinterpret_cast<bmw::Runnable*>(&g_spinRunnable), kSpinMs);
}

void setConnectingAnimation(bmw::PluginApp* app, bool on) {
    if (on == g_spinWanted) return;
    g_spinWanted = on;
    bmw::Parcel busy(on);
    app->setProperty(kStartButton, kPropWaiting, busy);  // busy ring on our menu (when it is on screen)
    alog("connecting animation %s (entry icon frames, Start row busy ring)", on ? "on" : "off");
    if (on && !g_spinRunning) {
        void** base = reinterpret_cast<void**>(dlsym(RTLD_DEFAULT, "_ZTVN3bmw8RunnableE"));
        if (base) memcpy(g_spinVtbl, base, sizeof(g_spinVtbl));
        else { g_spinVtbl[0] = NULL; g_spinVtbl[1] = NULL; }
        g_spinVtbl[2] = reinterpret_cast<void*>(&runnableNoop);
        g_spinVtbl[3] = reinterpret_cast<void*>(&runnableNoop);
        g_spinVtbl[4] = reinterpret_cast<void*>(&spinRun);
        g_spinRunnable.vptr = g_spinVtbl + 2;
        g_spinRunning = true;
        app->post(reinterpret_cast<bmw::Runnable*>(&g_spinRunnable), kSpinMs);
    }
}

void statusRun(void* /*self*/) {
    bmw::PluginApp* app = g_runApp;
    if (!app) return;                                   // finalized: stop polling
    serviceStop();
    DaemonState d = readDaemonState();
    bool connecting = !strcmp(d.state, "connecting"), connected = !strcmp(d.state, "connected");

    unsigned want = connecting ? kTextConnecting : kTextDefault;          // the Media/Radio entry row
    if (want != g_shownText) {
        bmw::TextID text(want);
        app->updateModel(kEntryTextModel, text);
        alog("entry text -> %u (%s)", want, want == kTextConnecting ? "connecting" : "default");
        g_shownText = want;
    }
    if (!fileExists("/opt/aa/spinner.disable")) setConnectingAnimation(app, connecting);
    unsigned row = connected ? kTextRowOpen : connecting ? kTextRowConnecting : kTextStart;   // our menu's Start row
    if (row != g_shownRow) {
        bmw::TextID text(row);
        app->updateModel(kStartRowModel, text);
        alog("start row text -> %u (%s)", row, d.state);
        g_shownRow = row;
    }
    if (connected && strcmp(g_prevState, "connected") != 0 && g_wantHome) {
        g_wantHome = false;
        long since = (long)(time(NULL) - g_startTime);
        unsigned ev = menuReturnEvent();
        bool onTop = g_focusKnown ? g_menuFocused : since <= kHomeWindowS;
        if (!ev) alog("connected: return to menu off (NBT_MENU_RETURN=off)");
        else if (!onTop) alog("connected %ld s after Start, our menu not on top (focus %s): no navigation", since,
                              g_focusKnown ? "reported off" : "unknown");
        else {
            app->triggerEvent(ev);
            alog("connected %ld s after Start: event %u -> %s", since, ev, ev == kEvGoHome ? "iDrive main menu" : "Media/Radio list");
        }
    }
    strncpy(g_prevState, d.state, sizeof(g_prevState) - 1);
    app->post(reinterpret_cast<bmw::Runnable*>(&g_statusRunnable), kStatusPollMs);
}

void startStatusPolling(bmw::PluginApp* app) {
    if (fileExists(kStatusOff)) { alog("status polling disabled by %s (no live texts, no return to menu, no stop timeouts)", kStatusOff); return; }
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

// ---- the app object -----------------------------------------------------------------------------------------------
void* g_vtbl[2 + kVtblSlots];   // [offset-to-top, typeinfo, slots...] copied from bmw::PluginApp's vtable
void* g_baseOnInit = NULL;
bmw::PluginApp* g_app = NULL;

bool onEntryAction(void* /*app*/, const bmw::Parcel& /*args*/) {
    alog("entry button pressed (raAction %u, direct-start XML)", kEntryAction);
    startDaemonOrShow();
    return true;
}

// Force Stop / Restart are refused while Android Auto is on screen: then the iDrive menu under the overlay only gets
// knob input meant for the phone. Exit to iDrive first (phone's car icon / BACK hold), then use the menu.
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

bool onMenuFocus(void*, const bmw::Parcel& p) {
    bool f = false;
    p.readBool(f);
    g_focusKnown = true;
    g_menuFocused = f;
    alog("menu state %u focus -> %d", kMenuState, (int)f);
    return true;
}

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
    {
        bmw::ActionDelegate d;
        d.object = self;
        d.stub = &onMenuFocus;
        self->addHandler(kMenuState, 1, d);
    }
    bool ok = self->onInit();
    alog("onInit: raActions %u/%u/%u/%u + menu focus handler registered, base onInit -> %d",
         kEntryAction, kActStart, kActStop, kActRestart, (int)ok);
    if (!fileExists("/opt/aa/gear.disable")) subscribeGear(self);
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
    bmw::PluginApp* app = new (mem) bmw::PluginApp(dispatcher, bmw::BasicString<char>(name ? name : "NbtAad"), 1, 0,
                                                   bmw::RHMI_INTERFACE_VERSION_1, params, config);

    // Own vtable: identical to bmw::PluginApp's, with our onInit/onError/onFinalize.
    void** base = *reinterpret_cast<void***>(app);
    memcpy(g_vtbl, base - 2, sizeof(g_vtbl));
    g_baseOnInit = g_vtbl[2 + kSlotOnInit];
    g_vtbl[2 + kSlotOnInit]     = reinterpret_cast<void*>(&appOnInit);
    g_vtbl[2 + kSlotOnError]    = reinterpret_cast<void*>(&appOnError);
    g_vtbl[2 + kSlotOnFinalize] = reinterpret_cast<void*>(&appOnFinalize);
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

// ---- entry point looked up by ManagedPluginApp::init (dlsym "pluginAppInfo") -----------------------------------
extern "C" {
struct NbtPluginAppInfo {
    uint16_t versionMajor;
    uint16_t versionMinor;
    const char* name;
    const char* vendor;
    uint32_t rhmiInterfaceVersion;
    bmw::PluginApp* (*create)(bmw::IDispatcher&, const char*, const bmw::Bundle&, const bmw::Bundle&);
    void (*destroy)(void*);
};
__attribute__((visibility("default"))) NbtPluginAppInfo pluginAppInfo = {
    1, 0, "Android Auto", "NBT-AAD", 1, &createApp, &destroyApp
};
}
