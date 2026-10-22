#ifndef AUDIO_MANAGER_HPP
#define AUDIO_MANAGER_HPP

#include "a2i_client.h"
#include <pthread.h>
#include <signal.h>
#include <stddef.h>

namespace nbt {

// C++03/GCC 4.4 compatible callback
typedef void (*MicDataCallback)(const uint8_t* pcm_data, size_t size, void* user_data);

class AudioManager {
public:
    AudioManager();
    ~AudioManager();

    bool init();
    void start();
    void stop();

    // Media music audio write (44.1kHz Stereo)
    bool writeMediaAudio(const uint8_t* pcm_data, size_t size);

    // Nav prompt audio write (44.1kHz Stereo)
    bool writeNavPromptAudio(const uint8_t* pcm_data, size_t size);

    // Set callback for receiving echo-cancelled microphone audio
    void setMicCallback(MicDataCallback callback, void* user_data = NULL);

private:
    static void* micCaptureThreadStub(void* arg);
    void micCaptureLoop();

    a2i_client_handle_t media_handle_;
    a2i_client_handle_t nav_handle_;
    a2i_client_handle_t mic_handle_;

    pthread_t mic_thread_;
    volatile bool is_running_;

    MicDataCallback mic_callback_;
    void* mic_user_data_;
};

} // namespace nbt

#endif // AUDIO_MANAGER_HPP
