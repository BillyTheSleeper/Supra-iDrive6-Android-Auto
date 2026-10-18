#ifndef VIDEO_MANAGER_HPP
#define VIDEO_MANAGER_HPP

#include "screen_video.h"
#include <stdint.h>
#include <stddef.h>
#include <pthread.h>
#include <vector>

namespace nbt {

class VideoManager {
public:
    VideoManager();
    ~VideoManager();

    bool init(uint32_t width = NBT_SCREEN_WIDTH_SUPRA_WIDESCREEN,
              uint32_t height = NBT_SCREEN_HEIGHT_SUPRA_WIDESCREEN,
              uint32_t fps = NBT_SCREEN_MAX_FPS);
    void start();
    void stop();

    // Toggle overlay visibility (bring forward or hide back to native iDrive 6)
    void setVisible(bool visible);
    bool isVisible() const;

    // Push raw H.264 NAL units from aasdk
    bool postVideoFrame(const uint8_t* data, size_t length, uint64_t pts = 0);
    bool postCodecConfig(const uint8_t* data, size_t length);
    void onVideoFrameReceived(uint32_t frame_index, size_t byte_len);

private:
    struct VideoQueueItem {
        std::vector<uint8_t> data;
        uint64_t pts;
        bool is_config;
        bool has_inline_config;
        size_t raw_size;
        size_t inline_config_size;
    };

    static void* workerThreadStub(void* arg);
    void workerLoop();

    bool sendMdevSync();
    bool sendMdevFormat(uint32_t width, uint32_t height, uint32_t fps);
    bool sendMdevConfig(const uint8_t* data, size_t length);

    screen_video_sink_t sink_;
    bool is_initialized_;
    bool mdev_initialized_;
    int mdev_fd_;

    void* mmr_lib_;
    void* mmr_cnx_;
    void* mmr_ctxt_;
    int mmr_output_id_;
    int mmr_input_id_;

    pthread_t worker_thread_;
    pthread_mutex_t queue_mutex_;
    pthread_cond_t queue_cond_;
    bool worker_running_;
    std::vector<VideoQueueItem> frame_queue_;
    std::vector<uint8_t> cached_codec_config_;
};

} // namespace nbt

#endif // VIDEO_MANAGER_HPP
