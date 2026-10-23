// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "audio_manager.hpp"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

namespace nbt {

AudioManager::AudioManager()
    : media_handle_(NULL),
      nav_handle_(NULL),
      mic_handle_(NULL),
      mic_thread_(0),
      is_running_(false),
      mic_callback_(NULL),
      mic_user_data_(NULL) {
}

AudioManager::~AudioManager() {
    stop();
}

bool AudioManager::init() {
    printf("[AudioManager] Initializing A2I audio endpoints...\n");

    // 1. Configure Media Playback (Music: 44.1kHz Stereo)
    a2i_client_config_t media_cfg;
    memset(&media_cfg, 0, sizeof(media_cfg));
    media_cfg.magic             = 0x02000000;
    media_cfg.direction         = A2I_DIR_OUTPUT_DOWNLINK;
    media_cfg.field_08          = 1;
    media_cfg.mode              = 2;
    media_cfg.buffer_size       = A2I_BUFFER_SIZE_STEREO; // 1024 bytes
    media_cfg.channels          = A2I_CHANNELS_STEREO;
    media_cfg.bytes_per_sample  = 2; // 16-bit
    media_cfg.sample_rate       = A2I_SAMPLE_RATE_44K;
    media_cfg.field_20          = 0;
    media_cfg.endpoint_name     = A2I_ENDPOINT_MEDIA_MAIN;
    media_cfg.channel_type      = 1; // Stereo
    media_cfg.field_2c          = 0;
    media_cfg.sample_bits       = A2I_BITS_16;
    media_cfg.field_34          = 0x05555555; // Required 16-bit PCM cookie for downlink (0x05555555)
    media_cfg.timeout_ms        = 2000;

    int res = A2I_Client_Open(&media_handle_, &media_cfg);
    if (res != 0) {
        printf("[AudioManager] Warning: Open '%s' failed (%d), trying fallback...\n",
               A2I_ENDPOINT_MEDIA_MAIN, res);
        media_cfg.endpoint_name = A2I_ENDPOINT_MEDIA_FALLBACK;
        res = A2I_Client_Open(&media_handle_, &media_cfg);
        if (res != 0) {
            printf("[AudioManager] Error: Failed to open media endpoint (%d)\n", res);
            return false;
        }
    }
    printf("[AudioManager] Media endpoint open OK: %s\n", media_cfg.endpoint_name);

    // 2. Configure Navigation Guidance Playback (44.1kHz Stereo)
    a2i_client_config_t nav_cfg;
    memset(&nav_cfg, 0, sizeof(nav_cfg));
    nav_cfg.magic             = 0x02000000;
    nav_cfg.direction         = A2I_DIR_OUTPUT_DOWNLINK;
    nav_cfg.field_08          = 1;
    nav_cfg.mode              = 2;
    nav_cfg.buffer_size       = A2I_BUFFER_SIZE_STEREO;
    nav_cfg.channels          = A2I_CHANNELS_STEREO;
    nav_cfg.bytes_per_sample  = 2;
    nav_cfg.sample_rate       = A2I_SAMPLE_RATE_44K;
    nav_cfg.field_20          = 0;
    nav_cfg.endpoint_name     = A2I_ENDPOINT_NAV_ALT;
    nav_cfg.channel_type      = 1; // Stereo
    nav_cfg.field_2c          = 0;
    nav_cfg.sample_bits       = A2I_BITS_16;
    nav_cfg.field_34          = 0x05555555; // Required 16-bit PCM cookie for downlink (0x05555555)
    nav_cfg.timeout_ms        = 2000;

    res = A2I_Client_Open(&nav_handle_, &nav_cfg);
    if (res != 0) {
        printf("[AudioManager] Warning: Open nav prompt endpoint failed (%d)\n", res);
    } else {
        printf("[AudioManager] Nav prompt endpoint open OK: %s\n", A2I_ENDPOINT_NAV_ALT);
    }

    // 3. Configure Microphone Capture (Echo-Cancelled Uplink: 16kHz Mono)
    a2i_client_config_t mic_cfg;
    memset(&mic_cfg, 0, sizeof(mic_cfg));
    mic_cfg.magic             = 0x02000000;
    mic_cfg.direction         = A2I_DIR_INPUT_UPLINK; // Input from car mic array
    mic_cfg.field_08          = 1;
    mic_cfg.mode              = 2;
    mic_cfg.buffer_size       = A2I_BUFFER_SIZE_MONO; // 512 bytes
    mic_cfg.channels          = A2I_CHANNELS_MONO;
    mic_cfg.bytes_per_sample  = 2;
    mic_cfg.sample_rate       = A2I_SAMPLE_RATE_16K;
    mic_cfg.field_20          = 0;
    mic_cfg.endpoint_name     = A2I_ENDPOINT_MIC_UL;
    mic_cfg.channel_type      = 0; // Mono
    mic_cfg.field_2c          = 0;
    mic_cfg.sample_bits       = A2I_BITS_16;
    mic_cfg.field_34          = 0;
    mic_cfg.timeout_ms        = 2000;

    res = A2I_Client_Open(&mic_handle_, &mic_cfg);
    if (res != 0) {
        printf("[AudioManager] Error: Open microphone endpoint failed (%d)\n", res);
        return false;
    }
    printf("[AudioManager] Echo-cancelled microphone open OK: %s\n", A2I_ENDPOINT_MIC_UL);

    return true;
}

void AudioManager::start() {
    if (is_running_) return;
    is_running_ = true;

    if (media_handle_) A2I_Client_Start(media_handle_);
    if (nav_handle_)   A2I_Client_Start(nav_handle_);
    if (mic_handle_ && mic_callback_) A2I_Client_Start(mic_handle_);

    // Launch microphone capture only when voice upload is wired. Starting the
    // uplink reader without a real consumer floods A2I shared-memory logs and
    // can steal scheduler time from the video pipeline.
    if (mic_handle_ && mic_callback_) {
        pthread_create(&mic_thread_, NULL, &AudioManager::micCaptureThreadStub, this);
    }
    printf("[AudioManager] Audio streams started.\n");
}

void AudioManager::stop() {
    if (!is_running_) return;
    is_running_ = false;

    if (mic_thread_) {
        pthread_join(mic_thread_, NULL);
        mic_thread_ = 0;
    }

    if (media_handle_) {
        A2I_Client_Stop(media_handle_);
        A2I_Client_Close(&media_handle_);
    }
    if (nav_handle_) {
        A2I_Client_Stop(nav_handle_);
        A2I_Client_Close(&nav_handle_);
    }
    if (mic_handle_) {
        A2I_Client_Stop(mic_handle_);
        A2I_Client_Close(&mic_handle_);
    }
    printf("[AudioManager] Audio streams stopped.\n");
}

bool AudioManager::writeMediaAudio(const uint8_t* pcm_data, size_t size) {
    if (!media_handle_ || !is_running_) return false;
    return A2I_Client_Out_Write(media_handle_, pcm_data, size, 0) == 0;
}

bool AudioManager::writeNavPromptAudio(const uint8_t* pcm_data, size_t size) {
    if (!nav_handle_ || !is_running_) return false;
    return A2I_Client_Out_Write(nav_handle_, pcm_data, size, 0) == 0;
}

void AudioManager::setMicCallback(MicDataCallback callback, void* user_data) {
    mic_callback_ = callback;
    mic_user_data_ = user_data;
}

void* AudioManager::micCaptureThreadStub(void* arg) {
    reinterpret_cast<AudioManager*>(arg)->micCaptureLoop();
    return NULL;
}

void AudioManager::micCaptureLoop() {
    printf("[AudioManager] Microphone capture loop active.\n");
    while (is_running_) {
        // Wait for next audio buffer chunk ready from DSP
        if (A2I_Client_GetNotification(mic_handle_) == 0) {
            void* buf_ptr = NULL;
            size_t buf_size = 0;
            if (A2I_Client_GetBuffer(mic_handle_, &buf_ptr, &buf_size) == 0 && buf_ptr && buf_size > 0) {
                if (mic_callback_) {
                    mic_callback_(reinterpret_cast<const uint8_t*>(buf_ptr), buf_size, mic_user_data_);
                }
            }
        }
        usleep(20000); // Prevent A2I shared-memory/log flood on every path
    }
    printf("[AudioManager] Microphone capture loop terminated.\n");
}

} // namespace nbt
