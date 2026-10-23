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

#include "screen_video.h"
#include "audio_manager.hpp"
#include "video_manager.hpp"
#include "input_manager.hpp"
#include "license_text.hpp"
#include <string.h>
#include "tcp_server.hpp"
#include "aa_session.hpp"
#include "bluetooth_manager.hpp"

#include <stdio.h>
#include <signal.h>
#include <unistd.h>

static volatile sig_atomic_t g_running = 1;
static nbt::AASession g_session;
static nbt::AudioManager* g_audio = NULL;

static void signalHandler(int sig) {
    printf("\n[Main] Received signal %d, shutting down nbt-aad...\n", sig);
    g_running = 0;
    g_session.stop();
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

static void onAudioFrame(uint8_t channel, uint32_t frame_index, const uint8_t* pcm_data, size_t pcm_len, uint64_t pts, void* user_data) {
    (void)frame_index;
    (void)pts;
    nbt::AudioManager* audio = reinterpret_cast<nbt::AudioManager*>(user_data);
    if (audio) audio->onAaAudio(channel, pcm_data, pcm_len);
}

static void onAudioState(uint8_t channel, bool started, void* user_data) {
    nbt::AudioManager* audio = reinterpret_cast<nbt::AudioManager*>(user_data);
    if (audio) audio->onAaStreamState(channel, started);
}

static void onSessionEnded(void* user_data) {
    nbt::TcpServer* server = reinterpret_cast<nbt::TcpServer*>(user_data);
    printf("[Main] Android Auto session ended. Resuming auto-connector probing...\n");
    if (g_audio) g_audio->onSessionEnded();
    if (server) {
        server->notifySessionEnded();
    }
}

static void onClientConnected(int client_fd, void* user_data) {
    (void)user_data;
    printf("[Main] Android Auto client connected on fd %d. Initializing aasdk session...\n", client_fd);
    g_session.start(client_fd);
}

int main(int argc, char* argv[]) {
    if (argc > 1 && (!strcmp(argv[1], "--license") || !strcmp(argv[1], "--licence"))) {
        nbt::printNbtAadLicense();
        return 0;
    }
    printf("====================================================\n");
    printf("  NBT-AAD: Native Wireless Android Auto Daemon      \n");
    printf("  Platform: BMW NBT Evo ID6 / Toyota GR Supra (J29) \n");
    printf("  OS: QNX Neutrino RTOS (ARMv7LE)                   \n");
    printf("  Build: v1.4.3-open-watch (%s %s)                            \n", __DATE__, __TIME__);
    nbt::printNbtAadBannerNotice();
    printf("====================================================\n");
    printf("[Main] REMINDER: Disable packet filter on hu-omap if blocked:\n");
    printf("       Run 'pfctl -d' to allow all incoming Wi-Fi traffic on uap0.\n");
    printf("----------------------------------------------------\n");

    signal(SIGINT,  signalHandler);
    signal(SIGTERM, signalHandler);

    // 1. Initialize Video Pipeline (libscreen.so surface: 1280x480 @ 60fps)
    nbt::VideoManager video;
    if (!video.init(NBT_SCREEN_WIDTH_SUPRA_WIDESCREEN,
                    NBT_SCREEN_HEIGHT_SUPRA_WIDESCREEN,
                    NBT_SCREEN_MAX_FPS)) {
        printf("[Main] Warning: Video initialization encountered an issue.\n");
    }

    // 2. Initialize Audio Pipeline (A2I Media + Nav + Echo-Cancelled Mic)
    nbt::AudioManager audio;
    if (!audio.init()) {
        printf("[Main] Warning: Audio initialization encountered an issue.\n");
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
    audio.start();
    input.start();

    // 4. Start Wireless TCP Server on standard Android Auto Port 5277
    nbt::TcpServer tcp_server(5277);
    tcp_server.setConnectionCallback(&onClientConnected, NULL);
    g_session.setDisconnectCallback(&onSessionEnded, &tcp_server);
    g_session.setVideoFrameCallback(&onVideoFrame, &video);
    g_session.setCodecConfigCallback(&onCodecConfig, &video);
    g_session.setAudioFrameCallback(&onAudioFrame, &audio);
    g_session.setAudioStateCallback(&onAudioState, &audio);

    if (!tcp_server.start()) {
        printf("[Main] Fatal: Failed to start TCP server on port 5277\n");
        return 1;
    }

    // 5. Initialize Bluetooth SDP Subsystem (Advertise Wireless AA UUID)
    nbt::BluetoothManager bluetooth;
    if (bluetooth.init()) {
        bluetooth.registerAndroidAutoSdp();
    }

    printf("[Main] Daemon running. Press Ctrl+C to terminate.\n");

    // Daemon main loop
    while (g_running) {
        sleep(1);
    }

    // Cleanup & graceful teardown
    printf("[Main] Stopping subsystems...\n");
    tcp_server.stop();
    input.stop();
    audio.stop();
    video.stop();

    printf("[Main] nbt-aad shutdown complete.\n");
    return 0;
}
