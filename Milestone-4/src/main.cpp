// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
//
// NBT-AAD: native wireless Android Auto daemon for the BMW NBT Evo ID6 head unit (Toyota GR Supra J29).
// Official source: https://github.com/Sq00ky/Supra-iDrive6-Android-Auto
//
// This file (and the whole of NBT-AAD) is licensed under the following terms:
//
// # PolyForm Noncommercial License 1.0.0
//
// <https://polyformproject.org/licenses/noncommercial/1.0.0>
//
// ## Acceptance
//
// In order to get any license under these terms, you must agree to them as both strict obligations and conditions to all your licenses.
//
// ## Copyright License
//
// The licensor grants you a copyright license for the software to do everything you might do with the software that would otherwise infringe the licensor's copyright in it for any permitted purpose.  However, you may only distribute the software according to [Distribution License](#distribution-license) and make changes or new works based on the software according to [Changes and New Works License](#changes-and-new-works-license).
//
// ## Distribution License
//
// The licensor grants you an additional copyright license to distribute copies of the software.  Your license to distribute covers distributing the software with changes and new works permitted by [Changes and New Works License](#changes-and-new-works-license).
//
// ## Notices
//
// You must ensure that anyone who gets a copy of any part of the software from you also gets a copy of these terms or the URL for them above, as well as copies of any plain-text lines beginning with `Required Notice:` that the licensor provided with the software.  For example:
//
// > Required Notice: Copyright Yoyodyne, Inc. (http://example.com)
//
// ## Changes and New Works License
//
// The licensor grants you an additional copyright license to make changes and new works based on the software for any permitted purpose.
//
// ## Patent License
//
// The licensor grants you a patent license for the software that covers patent claims the licensor can license, or becomes able to license, that you would infringe by using the software.
//
// ## Noncommercial Purposes
//
// Any noncommercial purpose is a permitted purpose.
//
// ## Personal Uses
//
// Personal use for research, experiment, and testing for the benefit of public knowledge, personal study, private entertainment, hobby projects, amateur pursuits, or religious observance, without any anticipated commercial application, is use for a permitted purpose.
//
// ## Noncommercial Organizations
//
// Use by any charitable organization, educational institution, public research organization, public safety or health organization, environmental protection organization, or government institution is use for a permitted purpose regardless of the source of funding or obligations resulting from the funding.
//
// ## Fair Use
//
// You may have "fair use" rights for the software under the law. These terms do not limit them.
//
// ## No Other Rights
//
// These terms do not allow you to sublicense or transfer any of your licenses to anyone else, or prevent the licensor from granting licenses to anyone else.  These terms do not imply any other licenses.
//
// ## Patent Defense
//
// If you make any written claim that the software infringes or contributes to infringement of any patent, your patent license for the software granted under these terms ends immediately. If your company makes such a claim, your patent license ends immediately for work on behalf of your company.
//
// ## Violations
//
// The first time you are notified in writing that you have violated any of these terms, or done anything with the software not covered by your licenses, your licenses can nonetheless continue if you come into full compliance with these terms, and take practical steps to correct past violations, within 32 days of receiving notice.  Otherwise, all your licenses end immediately.
//
// ## No Liability
//
// ***As far as the law allows, the software comes as is, without any warranty or condition, and the licensor will not be liable to you for any damages arising out of these terms or the use or nature of the software, under any kind of legal claim.***
//
// ## Definitions
//
// The **licensor** is the individual or entity offering these terms, and the **software** is the software the licensor makes available under these terms.
//
// **You** refers to the individual or entity agreeing to these terms.
//
// **Your company** is any legal entity, sole proprietorship, or other kind of organization that you work for, plus all organizations that have control over, are under the control of, or are under common control with that organization.  **Control** means ownership of substantially all the assets of an entity, or the power to direct its management and policies by vote, contract, or otherwise.  Control can be direct or indirect.
//
// **Your licenses** are all the licenses granted to you for the software under these terms.
//
// **Use** means anything you do with the software requiring one of your licenses.

#include "nbt_env.hpp"
#include "screen_video.h"
#include "audio_manager.hpp"
#include "video_manager.hpp"
#include "input_manager.hpp"
#include "license_text.hpp"
#include "tcp_server.hpp"
#include "aa_session.hpp"
#include "bluetooth_manager.hpp"
#include "wireless_setup.hpp"
#include "touch_probe.hpp"
#include "ccs_input.hpp"
#include "crash_handler.hpp"
#include <termios.h>

#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>

static volatile sig_atomic_t g_running = 1;
static nbt::AASession g_session;


// ---- Native wireless bring-up (default on; NBT_WIRELESS=0 disables) ---------------------------------
// Replaces "Start Head Unit Server" on the phone: the phone connects to us over
// Bluetooth RFCOMM (/dev/btspp<N>, served by Connectivity), we send the CarPlay AP
// credentials and our ip:port, and the phone then opens the TCP session itself.
static uint16_t g_aaPort = 5277;
static volatile bool g_sessionActive = false;   // AA session running (set by connect/end callbacks)
static nbt::VideoManager* g_video = NULL;       // for the session callbacks (valid between init and teardown)
static nbt::AudioManager* g_audio = NULL;       // same lifetime rule as g_video
// NBT_AUDIO_BT=1: AA audio stays on the phone's Bluetooth A2DP link; we only select the car's BT source.
static bool g_btAudio = false;
static nbt::AudioRoute g_btRoute;
static uint32_t g_btSrc = 0x3a;                 // eSRC_A2DP (audioconn -l: ConnId 0xc7 AuxInBTAudio)
static uint32_t g_btSink = 0x1;                 // eSNK_SPEAKERS

// Prints the Wi-Fi clients the head unit currently knows on the AP subnet (ARP table), so a log
// shows whether the phone actually joined the CarPlay network.
static void logStations(const char* tag) {
    FILE* p = popen("arp -an", "r");
    if (!p) return;
    char line[256];
    int n = 0;
    while (fgets(line, sizeof(line), p)) {
        if (strstr(line, "172.16.224.")) {
            size_t l = strlen(line);
            while (l && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = 0;
            printf("[Wireless] AP client (%s): %s\n", tag, line);
            ++n;
        }
    }
    pclose(p);
    if (!n) printf("[Wireless] AP clients (%s): none in ARP table\n", tag);
}

static void* wirelessThread(void*) {
    const char* dev = getenv("NBT_BTSPP_DEV");
    if (!dev || !*dev) dev = "/dev/btspp0";
    const char* cfg = getenv("NBT_AP_CFG");
    if (!cfg || !*cfg) cfg = "/fs/sda1/opt/conn/carplay/iap2_Wifi.cfg";
    const char* ifn = getenv("NBT_AP_IF");
    if (!ifn || !*ifn) ifn = "uap0";

    while (g_running) {
        // Nothing to do while a projection session is running; re-arm after it ends.
        if (g_sessionActive) { sleep(1); continue; }

        // The SPP node only exists while the phone's Bluetooth link is up: poll for it first and
        // only then read the AP config (keeps the log quiet while idle).
        int fd = open(dev, O_RDWR);
        if (fd < 0) {
            static int quiet = 0;
            if ((quiet++ % 30) == 0)
                printf("[Wireless] waiting for %s (%s)\n", dev, strerror(errno));
            usleep(500000);
            continue;
        }
        nbt::ApInfo ap;
        if (!nbt::loadCarPlayApConfig(cfg, ap)) {
            printf("[Wireless] AP config unreadable, retry in 10 s\n");
            close(fd);
            sleep(10);
            continue;
        }
        nbt::fillApNetworkInfo(ifn, ap);
        if (ap.ip.empty()) { printf("[Wireless] no IP on %s, retry\n", ifn); close(fd); sleep(5); continue; }
        // Make the serial device a completely transparent 8-bit stream (no line editing, no signal
        // characters, no flow control, no CR/NL or parity/break mangling). Log what the driver says.
        struct termios tio;
        if (tcgetattr(fd, &tio) == 0) {
            printf("[Wireless] termios before: iflag=0x%x oflag=0x%x cflag=0x%x lflag=0x%x\n",
                   (unsigned)tio.c_iflag, (unsigned)tio.c_oflag, (unsigned)tio.c_cflag, (unsigned)tio.c_lflag);
            tio.c_iflag = 0;
            tio.c_oflag = 0;
            tio.c_lflag = 0;
            tio.c_cflag &= ~(CSIZE | PARENB);
            tio.c_cflag |= CS8 | CREAD | CLOCAL;
            for (int i = 0; i < NCCS; ++i) tio.c_cc[i] = 0;
            tio.c_cc[VMIN] = 1;
            tio.c_cc[VTIME] = 0;
            int rc = tcsetattr(fd, TCSANOW, &tio);
            struct termios chk;
            if (tcgetattr(fd, &chk) == 0)
                printf("[Wireless] termios set rc=%d now: iflag=0x%x oflag=0x%x cflag=0x%x lflag=0x%x\n", rc,
                       (unsigned)chk.c_iflag, (unsigned)chk.c_oflag, (unsigned)chk.c_cflag, (unsigned)chk.c_lflag);
        } else {
            printf("[Wireless] tcgetattr failed: %s (device left in its default mode!)\n", strerror(errno));
        }
        printf("[Wireless] opened %s (fd %d), starting handshake\n", dev, fd);
        // Handshake style: the proactive one (AP info sent without waiting to be asked) is what worked
        // on the car (w5) and does not depend on reading the phone's WifiInfoRequest; on consecutive
        // failures rotate through the others. NBT_WPP_MODE=0/1/2 pins one.
        static int s_fails = 0;
        static const int kOrder[3] = { 2, 0, 1 };
        int mode = kOrder[s_fails % 3];
        const char* me = getenv("NBT_WPP_MODE");
        if (me && *me) mode = atoi(me);
        logStations("before handshake");
        bool ok = nbt::runWirelessHandshake(fd, ap, g_aaPort, 10000, mode);
        printf("[Wireless] handshake %s (mode %d)\n", ok ? "SUCCESS: waiting for phone TCP connect" : "FAILED", mode);
        if (ok) {
            s_fails = 0;
            // Keep the Bluetooth link open and serviced (ping replies, repeated AP info requests) for
            // as long as the phone wants it: closing it early makes the phone abort the wireless setup.
            bool everActive = false;
            int tick = 0;
            while (g_running) {
                bool alive = nbt::wppService(fd, 1000, ap, g_aaPort);
                ++tick;
                if (g_sessionActive) everActive = true;
                if (!alive) { printf("[Wireless] Bluetooth link closed by the phone\n"); break; }
                if (!g_sessionActive && (tick % 10) == 5) logStations("waiting for phone TCP connect");
                if (!everActive && tick > 90) { printf("[Wireless] phone did not connect within 90 s\n"); break; }
                if (everActive && !g_sessionActive) { printf("[Wireless] session ended, releasing the Bluetooth link\n"); break; }
            }
            close(fd);
        } else {
            close(fd);
            ++s_fails;
            sleep(2);
        }
    }
    return NULL;
}

static void signalHandler(int sig) {
    (void)sig;       // printf is not async-signal-safe; main() reports the shutdown
    g_running = 0;   // session is stopped from main() (joining a thread in a signal handler can hang)
}

// C++03 / GCC 4.4 static callbacks
static void onWheelEvent(int steps, void* user_data) {
    (void)user_data;
    printf("[Main -> aasdk] Knob rotation: %d detents\n", steps);
    // Inject into aasdk wheel channel
}

static void onButtonEvent(nbt::AAButtonCode code, bool pressed, void* user_data) {
    nbt::VideoManager* video = reinterpret_cast<nbt::VideoManager*>(user_data);
    printf("[Main -> aasdk] Button code: %u, Pressed: %d\n",
           (unsigned int)code, pressed ? 1 : 0);

    // Track 4 Feature: Pressing HOME / MENU toggles overlay visibility
    if (code == nbt::AA_BUTTON_HOME && pressed && video) {
        bool next_vis = !video->isVisible();
        video->setVisible(next_vis);
        printf("[Main] Toggled video overlay visibility to: %s\n",
               next_vis ? "VISIBLE" : "HIDDEN (iDrive Native Active)");
    }
}

static void onMicData(const uint8_t* pcm_data, size_t size, void* user_data) {
    (void)pcm_data;
    (void)size;
    (void)user_data;
    // Stream clean, echo-cancelled mic chunk into aasdk VoiceInput channel
}

static void onVideoFrame(uint32_t frame_index, const uint8_t* nal_data, size_t nal_len, uint64_t pts, void* user_data) {
    nbt::VideoManager* video = reinterpret_cast<nbt::VideoManager*>(user_data);
    if (video) {
        // Feed raw H.264 slice straight into the car's hardware video decoder (/dev/name/local/mdev)
        video->postVideoFrame(nal_data, nal_len, pts);
        video->onVideoFrameReceived(frame_index, nal_len);
    }
}

static void onCodecConfig(const uint8_t* config_data, size_t config_len, void* user_data) {
    nbt::VideoManager* video = reinterpret_cast<nbt::VideoManager*>(user_data);
    if (video) {
        video->postCodecConfig(config_data, config_len);
    }
}

static void onAudioFrame(uint8_t channel, const uint8_t* pcm, size_t pcm_len, uint64_t pts, void* user_data) {
    (void)pts;
    (void)user_data;
    if (g_audio) g_audio->onAaAudio(channel, pcm, pcm_len);
}

static void onAudioState(uint8_t channel, bool started, void* user_data) {
    (void)user_data;
    if (g_audio) g_audio->onAaStreamState(channel, started);
}

static void onSessionEnded(void* user_data) {
    g_sessionActive = false;
    nbt::TcpServer* server = reinterpret_cast<nbt::TcpServer*>(user_data);
    printf("[Main] Android Auto session ended. Resuming auto-connector probing...\n");
    if (server) {
        server->notifySessionEnded();
    }
    // Runs on the session thread (AASession::start() waits for it): drop stale frames and the
    // cached SPS/PPS. The overlay itself is handled by the main loop.
    if (g_video) g_video->endSession();
    if (g_audio) g_audio->onSessionEnded();
    if (g_btAudio && getenv("NBT_BT_RELEASE") && strcmp(getenv("NBT_BT_RELEASE"), "1") == 0) {
        printf("[BtAudio] session ended: releasing source 0x%x -> sink 0x%x\n", g_btSrc, g_btSink);
        g_btRoute.removeFocus(g_btSrc, g_btSink);
    }
}

static void onClientConnected(int client_fd, void* user_data) {
    nbt::TcpServer* server = reinterpret_cast<nbt::TcpServer*>(user_data);
    printf("[Main] Android Auto client connected on fd %d. Initializing aasdk session...\n", client_fd);
    g_sessionActive = true;
    if (!g_session.start(client_fd)) {
        // start() closed the fd; make sure the server does not wait for a session that never began
        g_sessionActive = false;
        if (server) server->notifySessionEnded();
        return;
    }
    // start() joined any previous session thread, so that thread's onSessionEnded() has finished.
    // Re-assert "active" now: a stale end-of-session callback may have cleared the flags between
    // the assignments above and the join (phone reconnecting while the old session was dying).
    g_sessionActive = true;
    if (server) server->markSessionActive();
    if (g_btAudio) {
        printf("[BtAudio] session started: requesting car Bluetooth audio source 0x%x -> sink 0x%x\n",
               g_btSrc, g_btSink);
        if (!g_btRoute.buildFocus(g_btSrc, g_btSink)) printf("[BtAudio] could not queue audioconn\n");
    }
}

// One-shot diagnostics for "frames reach the decoder but nothing is visible": dump the LayerManager
// state and the QNX Screen window properties (order / above / below / visible) of our window and of
// the HMI's windows. Runs in its own thread so a slow cat can never stall the main loop.
static void* diagThread(void*) {
    char cmd[320];
    int me = (int)getpid();
    printf("[Diag] --- LayerManager ---\n");
    fflush(stdout);
    system("/opt/sys/bin/LayerManagerControl get surface 1280");
    system("/opt/sys/bin/LayerManagerControl get surface 256");
    system("/opt/sys/bin/LayerManagerControl get layer 0x80000000");
    system("/opt/sys/bin/LayerManagerControl get screen 0");
    printf("[Diag] --- Screen windows of this daemon (pid %d) ---\n", me);
    fflush(stdout);
    snprintf(cmd, sizeof(cmd), "ls -l /dev/screen/%d", me);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "cat /dev/screen/%d/*/*", me);
    system(cmd);
    int hmi = 0;
    FILE* f = popen("pidin arg", "r");
    if (f) {
        char line[512];
        while (fgets(line, sizeof(line), f)) {
            if (strstr(line, "HmiMain3D")) { hmi = atoi(line); if (hmi > 0) break; }
        }
        pclose(f);
    }
    if (hmi > 0) {
        printf("[Diag] --- Screen windows of HmiMain3D (pid %d) ---\n", hmi);
        fflush(stdout);
        snprintf(cmd, sizeof(cmd), "ls -l /dev/screen/%d", hmi);
        system(cmd);
        snprintf(cmd, sizeof(cmd), "cat /dev/screen/%d/*/*", hmi);
        system(cmd);
    } else {
        printf("[Diag] HmiMain3D pid not found\n");
    }
    printf("[Diag] --- end ---\n");
    fflush(stdout);
    return NULL;
}

static void onTouchEvent(const nbt::TouchEventOut& ev, void* user) {
    static_cast<nbt::AASession*>(user)->postTouchEvent(ev);
}

// iDrive knob / tilt / hard keys (default on, NBT_KNOB=0 disables) -> phone (MOST DSI connection thread).
static void onKnobEvent(const nbt::CcsEvent& ev, void* user) {
    nbt::AASession* s = static_cast<nbt::AASession*>(user);
    if (ev.kind == 2) s->postRelativeEvent(ev.keycode, ev.delta);
    else s->postKeyEvent(ev.keycode, ev.down != 0);
}

int main(int argc, char* argv[]) {
    if (argc > 1 && (!strcmp(argv[1], "--license") || !strcmp(argv[1], "--licence"))) {
        nbt::printNbtAadLicense();
        return 0;
    }
    nbt::installProcessGuards();   // first: line-buffered log, SIGPIPE ignored, fatal-signal report (setvbuf must precede any output)
    printf("====================================================\n");
    printf("  NBT-AAD: Native Wireless Android Auto Daemon      \n");
    printf("  Platform: BMW NBT Evo ID6 / Toyota GR Supra (J29) \n");
    printf("  OS: QNX Neutrino RTOS (ARMv7LE)                   \n");
    printf("  Build: v1.5.2-audio (%s %s)                                \n", __DATE__, __TIME__);
    nbt::printNbtAadBannerNotice();
    printf("====================================================\n");
    printf("----------------------------------------------------\n");

    // Drop the packet filter so the phone can reach the stock AA port (5277) on uap0.
    // Set NBT_KEEP_PF=1 to leave the firewall alone.
    if (getenv("NBT_KEEP_PF")) {
        printf("[Main] NBT_KEEP_PF set: leaving packet filter untouched.\n");
    } else {
        int rc = system("pfctl -d");
        printf("[Main] 'pfctl -d' (disable packet filter) returned %d\n", rc);
    }

    signal(SIGINT,  signalHandler);
    signal(SIGTERM, signalHandler);

    // 1. Initialize Video Pipeline (libscreen.so surface: 1440x540 @ 60fps)
    nbt::VideoManager video;
    if (!video.init(NBT_SCREEN_WIDTH_SUPRA_WIDESCREEN,
                    NBT_SCREEN_HEIGHT_SUPRA_WIDESCREEN,
                    NBT_SCREEN_MAX_FPS)) {
        printf("[Main] Warning: Video initialization encountered an issue.\n");
    }
    g_video = &video;

    // 2. Initialize Audio Pipeline (A2I Media + Nav + Echo-Cancelled Mic)
    nbt::AudioManager audio;
    g_btAudio = nbt::btAudioEnabled();
    if (g_btAudio) {
        g_btSrc = (uint32_t)nbt::envHex("NBT_BT_SRC", 0x3a);
        g_btSink = (uint32_t)nbt::envHex("NBT_BT_SINK", 0x1);
        printf("[BtAudio] NBT_AUDIO_BT on: AA audio sinks + mic left out of the ServiceDiscovery (NBT_SDR_DROP=%s),\n"
               "[BtAudio] phone audio stays on Bluetooth A2DP; A2I pipeline not opened. Source 0x%x -> sink 0x%x.\n",
               getenv("NBT_SDR_DROP") ? getenv("NBT_SDR_DROP") : "8,9,14", g_btSrc, g_btSink);
        if (!g_btRoute.configure()) printf("[BtAudio] audioconn not found: select Bluetooth audio in iDrive manually.\n");
        g_audio = NULL;
    } else {
        if (!audio.init()) {
            printf("[Main] Warning: Audio initialization encountered an issue.\n");
        }
        g_audio = &audio;
    }

    // 3. Initialize Input Pipeline (DSI CPxyCentralControlSystem35up)
    nbt::InputManager input;
    if (!input.init()) {
        printf("[Main] Warning: Input initialization encountered an issue.\n");
    }

    // Bind input callbacks (compatible with GCC 4.4 function pointers)
    input.setWheelCallback(&onWheelEvent, NULL);
    input.setButtonCallback(&onButtonEvent, &video);

    // Microphone upload is not wired into AA yet; leave callback unset so the
    // A2I uplink reader does not flood slog while debugging video bring-up.

    // Start all subsystems
    video.start();
    if (!g_btAudio) audio.start();
    input.start();

    // 4. Start Wireless TCP Server on the stock Android Auto port 5277 (override: NBT_AA_PORT)
    {
        const char* ep = getenv("NBT_AA_PORT");
        if (ep && *ep) g_aaPort = (uint16_t)atoi(ep);
    }
    nbt::TcpServer tcp_server(g_aaPort);
    tcp_server.setConnectionCallback(&onClientConnected, &tcp_server);
    g_session.setDisconnectCallback(&onSessionEnded, &tcp_server);
    g_session.setVideoFrameCallback(&onVideoFrame, &video);
    g_session.setCodecConfigCallback(&onCodecConfig, &video);
    g_session.setAudioFrameCallback(&onAudioFrame, NULL);
    g_session.setAudioStateCallback(&onAudioState, NULL);

    if (!tcp_server.start()) {
        printf("[Main] Fatal: Failed to start TCP server on port %u\n", (unsigned)g_aaPort);
        return 1;
    }

    // 5. Initialize Bluetooth SDP Subsystem (Advertise Wireless AA UUID)
    nbt::BluetoothManager bluetooth;
    if (nbt::wirelessEnabled() && bluetooth.init()) {
        bluetooth.registerAndroidAutoSdp();
    }

    const char* tfwd = getenv("NBT_TOUCH_FWD");
    if (!(tfwd && tfwd[0] == '0') && !getenv("NBT_TOUCH_PROBE")) {   // default: forward touches to the phone
        const char* tp = getenv("NBT_TOUCH_PORT");
        uint16_t port = (tp && *tp) ? (uint16_t)atoi(tp) : 30567;
        if (!nbt::startTouchForward(port, &onTouchEvent, &g_session))
            printf("[Main] Touch forwarder failed to start.\n");
    } else if (getenv("NBT_TOUCH_PROBE")) {
        const char* tp = getenv("NBT_TOUCH_PORT");
        const char* ts = getenv("NBT_TOUCH_PROBE_SECS");
        nbt::startTouchProbe((uint16_t)(tp ? atoi(tp) : 30567), ts ? atoi(ts) : 60);
    }

    if (nbt::knobEnabled()) {
        if (!nbt::startCcsInput(&onKnobEvent, &g_session))
            printf("[Main] iDrive knob input failed to start.\n");
    } else {
        printf("[Main] iDrive knob input off (NBT_KNOB=0).\n");
    }

    if (nbt::wirelessEnabled()) {
        pthread_t wt;
        if (pthread_create(&wt, NULL, &wirelessThread, NULL) == 0) {
            pthread_detach(wt);
            printf("[Main] Wireless bring-up thread started (port %u)\n", (unsigned)g_aaPort);
        }
    } else {
        printf("[Main] Wireless off (NBT_WIRELESS=0): outbound phone scanner mode.\n");
    }

    printf("[Main] Daemon running. Press Ctrl+C to terminate.\n");

    // Daemon main loop. The overlay follows the session: up while a phone session is running, down
    // (iDrive visible) otherwise. This is the only place that calls setVisible() while the daemon runs,
    // so the 3-5 system() calls of two transitions cannot interleave, stop() cannot race them, and a
    // session that dies right after it started cannot leave the overlay on top of iDrive.
    // Default (known-good on the car): the overlay stays up for the whole daemon run.
    // NBT_HIDE_IDLE=1 gives iDrive back while no phone is connected (hide/show cycle is UNTESTED).
    const bool hide_idle = (getenv("NBT_HIDE_IDLE") != NULL);
    bool overlay_on = true;               // video.start() showed it
    bool was_active = false;
    int active_ticks = 0;
    bool diag_done = false;
    while (g_running) {
        usleep(250000);
        bool act = g_session.isActive();
        bool want = hide_idle ? act : true;
        if (want != overlay_on) {
            video.setVisible(want);
            overlay_on = want;
        }
        if (act) {
            if (!was_active) {
                // The HMI / LayerManager may have rearranged layers and window order since the daemon
                // started (minutes ago, with iDrive in use): put our window back on top for the session.
                active_ticks = 0;
                printf("[Main] Session active: re-asserting overlay (window z-order + LayerManager).\n");
                video.setVisible(true);
                overlay_on = true;
            }
            ++active_ticks;
            if (active_ticks == 20) {   // ~5 s in: again, in case the HMI reacted to the new source
                printf("[Main] Re-asserting overlay (5 s into the session).\n");
                video.setVisible(true);
            }
            if (active_ticks == 32 && !diag_done) {   // ~8 s in
                diag_done = true;
                pthread_t dt;
                if (pthread_create(&dt, NULL, &diagThread, NULL) == 0) pthread_detach(dt);
            }
        } else {
            active_ticks = 0;
        }
        was_active = act;
    }

    // Cleanup & graceful teardown
    printf("[Main] Shutdown requested, stopping subsystems...\n");
    video.setVisible(false);   // give iDrive back first: any of the stop() calls below may block
    tcp_server.stop();         // no more onClientConnected -> no session can start behind our back
    g_session.stop();
    g_video = NULL;
    g_audio = NULL;
    input.stop();
    if (!g_btAudio) audio.stop();
    video.stop();

    printf("[Main] nbt-aad shutdown complete.\n");
    return 0;
}
