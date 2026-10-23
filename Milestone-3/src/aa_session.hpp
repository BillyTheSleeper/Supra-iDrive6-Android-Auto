// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef AA_SESSION_HPP
#define AA_SESSION_HPP

#include <stdint.h>
#include <stddef.h>
#include <vector>
#include <map>
#include <pthread.h>
#include "aa_cryptor.hpp"
#include "touch_types.hpp"

namespace nbt {

typedef void (*SessionEndedCallback)(void* user_data);
typedef void (*VideoFrameCallback)(uint32_t frame_index, const uint8_t* nal_data, size_t nal_len, uint64_t pts, void* user_data);
typedef void (*CodecConfigCallback)(const uint8_t* config_data, size_t config_len, void* user_data);

class AASession {
public:
    AASession();
    ~AASession();

    bool start(int client_fd);
    void stop();
    void setDisconnectCallback(SessionEndedCallback cb, void* user_data = NULL);
    void setVideoFrameCallback(VideoFrameCallback cb, void* user_data = NULL);
    void setCodecConfigCallback(CodecConfigCallback cb, void* user_data = NULL);
    bool isActive() const { return isRunning(); }   // true from start() until the session ended or stop()
    // Touch forwarding (default on, NBT_TOUCH_FWD=0 disables): thread-safe, called from the touch thread.
    // Events are queued and sent by the session thread.
    void postTouchEvent(const TouchEventOut& ev);

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
    static uint32_t rxTimeoutMs();
    bool isRunning() const;
    void setRunning(bool v);
    bool exchangeRunningOff();   // clear the flag, return the previous value
    void wakeSession();      // shutdown(fd) so a blocked recv() returns
    void releaseSocket();    // session thread only: take + close the fd
    void finishSession();
    void drainTouch();                       // session thread only
    bool sendTouchEvent(const TouchEventOut& ev);  // session thread only
    bool sendBindingResponse(uint8_t channel);
    // Sensor channel (ch 1): answer SensorStartRequest and publish night mode / driving status.
    bool sendSensorStartResponse(uint8_t channel, bool ok);
    bool sendSensorEvent(uint32_t sensor_type);
    std::vector<TouchEventOut> touch_queue_;
    pthread_mutex_t touch_mutex_;
    bool input_ready_;                       // guarded by touch_mutex_
    bool touch_fwd_;
    int wake_pipe_[2];    // common tail of sessionLoop (fd release + end-of-session callback)

    // Socket ownership: only the session thread closes the fd (at the end of sessionLoop).
    // stop()/start() merely shutdown() it to wake the thread and then join. fd_mutex_ keeps
    // "shutdown(fd)" from racing with the thread releasing that fd number.
    int client_fd_;
    int running_;                      // guarded by fd_mutex_; use isRunning()/setRunning()
    pthread_t thread_;
    pthread_mutex_t lifecycle_mutex_;   // serialises start()/stop() (accept thread vs main thread)
    pthread_mutex_t fd_mutex_;
    AACryptor cryptor_;
    SessionEndedCallback disconnect_cb_;
    void* disconnect_user_data_;
    VideoFrameCallback video_cb_;
    void* video_user_data_;
    CodecConfigCallback config_cb_;
    void* config_user_data_;

    std::map<uint8_t, std::vector<uint8_t> > reassembly_;
    uint32_t media_frames_;
    std::vector<uint8_t> sps_pps_cache_;
};

} // namespace nbt

#endif // AA_SESSION_HPP
