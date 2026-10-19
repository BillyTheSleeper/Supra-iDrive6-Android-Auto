#ifndef AA_SESSION_HPP
#define AA_SESSION_HPP

#include <stdint.h>
#include <stddef.h>
#include <vector>
#include <map>
#include <pthread.h>
#include "aa_cryptor.hpp"

namespace nbt {

typedef void (*SessionEndedCallback)(void* user_data);
typedef void (*VideoFrameCallback)(uint32_t frame_index, const uint8_t* nal_data, size_t nal_len, uint64_t pts, void* user_data);
typedef void (*CodecConfigCallback)(const uint8_t* config_data, size_t config_len, void* user_data);
typedef void (*AudioFrameCallback)(uint8_t channel, uint32_t frame_index, const uint8_t* pcm_data, size_t pcm_len, uint64_t pts, void* user_data);

typedef void (*AudioStateCallback)(uint8_t channel, bool started, void* user_data);

class AASession {
public:
    AASession();
    ~AASession();

    bool start(int client_fd);
    void stop();
    void setDisconnectCallback(SessionEndedCallback cb, void* user_data = NULL);
    void setVideoFrameCallback(VideoFrameCallback cb, void* user_data = NULL);
    void setCodecConfigCallback(CodecConfigCallback cb, void* user_data = NULL);
    void setAudioFrameCallback(AudioFrameCallback cb, void* user_data = NULL);
    void setAudioStateCallback(AudioStateCallback cb, void* user_data = NULL);

private:
    static void* sessionThreadStub(void* arg);
    void sessionLoop();

    bool sendFrame(uint8_t channel, uint8_t flags, const uint8_t* payload, size_t length);
    bool sendAll(const uint8_t* data, size_t length);
    bool sendSslHandshake(const std::vector<uint8_t>& tls_bytes);
    bool sendAuthComplete();
    bool sendServiceDiscoveryResponse();
    bool sendChannelOpenRequest(uint8_t channel_id);
    bool sendAvSetupResponse(uint8_t channel);
    bool sendVideoFocusIndication(uint8_t channel);
    bool sendAvMediaAck(uint8_t channel, uint32_t count);
    bool sendAudioFocusResponse(uint8_t focus_state);
    bool readExact(uint8_t* buffer, size_t length);

    int client_fd_;
    bool is_running_;
    pthread_t thread_;
    AACryptor cryptor_;
    SessionEndedCallback disconnect_cb_;
    void* disconnect_user_data_;
    VideoFrameCallback video_cb_;
    void* video_user_data_;
    CodecConfigCallback config_cb_;
    void* config_user_data_;
    AudioFrameCallback audio_cb_;
    void* audio_user_data_;
    AudioStateCallback audio_state_cb_;
    void* audio_state_user_data_;

    std::map<uint8_t, std::vector<uint8_t> > reassembly_;
    uint32_t media_frames_;
    uint32_t audio_observe_frames_;
    std::vector<uint8_t> sps_pps_cache_;
};

} // namespace nbt

#endif // AA_SESSION_HPP
