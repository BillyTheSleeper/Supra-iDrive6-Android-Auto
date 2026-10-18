#include "screen_video.h"
#include "audio_manager.hpp"
#include "video_manager.hpp"
#include "input_manager.hpp"
#include "tcp_server.hpp"
#include "aa_session.hpp"
#include "bluetooth_manager.hpp"

#include <stdio.h>
#include <signal.h>
#include <unistd.h>

static volatile sig_atomic_t g_running = 1;
static nbt::AASession g_session;

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

static void onSessionEnded(void* user_data) {
    nbt::TcpServer* server = reinterpret_cast<nbt::TcpServer*>(user_data);
    printf("[Main] Android Auto session ended. Resuming auto-connector probing...\n");
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
    printf("====================================================\n");
    printf("  NBT-AAD: Native Wireless Android Auto Daemon      \n");
    printf("  Platform: BMW NBT Evo ID6 / Toyota GR Supra (J29) \n");
    printf("  OS: QNX Neutrino RTOS (ARMv7LE)                   \n");
    printf("  Build: v1.3.29 (%s %s)                            \n", __DATE__, __TIME__);
    printf("====================================================\n");
    printf("[Main] REMINDER: Disable packet filter on hu-omap if blocked:\n");
    printf("       Run 'pfctl -d' to allow all incoming Wi-Fi traffic on uap0.\n");
    printf("----------------------------------------------------\n");

    signal(SIGINT,  signalHandler);
    signal(SIGTERM, signalHandler);

    // 1. Initialize Video Pipeline (libscreen.so surface: 1440x540 @ 60fps)
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
