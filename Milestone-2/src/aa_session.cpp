#include "aa_session.hpp"
#include "dhu_sdp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <vector>

namespace nbt {

AASession::AASession()
    : client_fd_(-1),
      is_running_(false),
      thread_(0),
      disconnect_cb_(NULL),
      disconnect_user_data_(NULL),
      video_cb_(NULL),
      video_user_data_(NULL),
      config_cb_(NULL),
      config_user_data_(NULL),
      audio_cb_(NULL),
      audio_user_data_(NULL),
      audio_state_cb_(NULL),
      audio_state_user_data_(NULL),
      media_frames_(0),
      audio_observe_frames_(0) {
}

AASession::~AASession() {
    stop();
}

void AASession::setDisconnectCallback(SessionEndedCallback cb, void* user_data) {
    disconnect_cb_ = cb;
    disconnect_user_data_ = user_data;
}

void AASession::setVideoFrameCallback(VideoFrameCallback cb, void* user_data) {
    video_cb_ = cb;
    video_user_data_ = user_data;
}

void AASession::setCodecConfigCallback(CodecConfigCallback cb, void* user_data) {
    config_cb_ = cb;
    config_user_data_ = user_data;
}

void AASession::setAudioStateCallback(AudioStateCallback cb, void* user_data) {
    audio_state_cb_ = cb;
    audio_state_user_data_ = user_data;
}

void AASession::setAudioFrameCallback(AudioFrameCallback cb, void* user_data) {
    audio_cb_ = cb;
    audio_user_data_ = user_data;
}

bool AASession::start(int client_fd) {
    if (is_running_) stop();

    client_fd_ = client_fd;
    is_running_ = true;
    reassembly_.clear();
    sps_pps_cache_.clear();
    media_frames_ = 0;
    audio_observe_frames_ = 0;

    if (pthread_create(&thread_, NULL, &AASession::sessionThreadStub, this) != 0) {
        perror("[AASession] Failed to spawn session thread");
        is_running_ = false;
        close(client_fd_);
        client_fd_ = -1;
        return false;
    }

    return true;
}

void AASession::stop() {
    if (!is_running_) return;
    is_running_ = false;

    if (client_fd_ >= 0) {
        shutdown(client_fd_, SHUT_RDWR);
        close(client_fd_);
        client_fd_ = -1;
    }

    if (thread_) {
        pthread_join(thread_, NULL);
        thread_ = 0;
    }
}

void* AASession::sessionThreadStub(void* arg) {
    reinterpret_cast<AASession*>(arg)->sessionLoop();
    return NULL;
}

bool AASession::readExact(uint8_t* buffer, size_t length) {
    size_t total = 0;
    while (total < length && is_running_) {
        ssize_t n = recv(client_fd_, buffer + total, length - total, 0);
        if (n <= 0) {
            return false;
        }
        total += n;
    }
    return total == length;
}

bool AASession::sendAll(const uint8_t* data, size_t length) {
    size_t total = 0;
    while (total < length && is_running_) {
        ssize_t n = send(client_fd_, data + total, length - total, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) return false;
        total += static_cast<size_t>(n);
    }
    return total == length;
}

bool AASession::sendFrame(uint8_t channel, uint8_t flags, const uint8_t* payload, size_t length) {
    if (length > 0xFFFFU) return false;
    std::vector<uint8_t> frame;
    frame.reserve(length + 4);
    frame.push_back(channel);
    frame.push_back(flags);
    frame.push_back(static_cast<uint8_t>((length >> 8) & 0xFF));
    frame.push_back(static_cast<uint8_t>(length & 0xFF));
    if (length > 0) {
        if (!payload) return false;
        frame.insert(frame.end(), payload, payload + length);
    }
    return sendAll(&frame[0], frame.size());
}

bool AASession::sendSslHandshake(const std::vector<uint8_t>& tls_bytes) {
    if (tls_bytes.empty()) return true;

    // Control Message ID 0x0003 (MESSAGE_SSL_HANDSHAKE)
    std::vector<uint8_t> payload;
    payload.reserve(tls_bytes.size() + 2);
    payload.push_back(0x00);
    payload.push_back(0x03);
    payload.insert(payload.end(), tls_bytes.begin(), tls_bytes.end());

    return sendFrame(0x00, 0x03, &payload[0], payload.size());
}

bool AASession::sendAuthComplete() {
    // Control Message ID 0x0004 (MESSAGE_AUTH_COMPLETE)
    // Per canonical aasdk specification (ControlServiceChannel.cpp:65):
    // AUTH_COMPLETE is transmitted as messenger::EncryptionType::PLAIN (Flags = 0x03)!
    // Protobuf payload: 08 00 (field 1 = 0, status SUCCESS)
    uint8_t auth_payload[4] = { 0x00, 0x04, 0x08, 0x00 };
    printf("[AASession] Sent AuthCompleteNotification (Flags=0x03, 4 bytes).\n");
    return sendFrame(0x00, 0x03, auth_payload, sizeof(auth_payload));
}

bool AASession::sendServiceDiscoveryResponse() {
    // Control Message ID 0x0006 (MESSAGE_SERVICE_DISCOVERY_RESPONSE)
    // Uses Google's canonical reference Desktop Head Unit (DHU) capability manifest
    // including all 9 mandatory channels: Video, Media Audio, Speech, System Audio,
    // Input (touch/rotary), Sensors (Driving Status, Night Mode), Mic, and Nav.
    std::vector<uint8_t> plain_sdr;
    plain_sdr.push_back(0x00);
    plain_sdr.push_back(0x06); // Message ID: 6 (ServiceDiscoveryResponse)
    plain_sdr.insert(plain_sdr.end(), kDhuSdpProtobuf, kDhuSdpProtobuf + kDhuSdpProtobufLen);

    std::vector<uint8_t> encrypted_payload;
    if (cryptor_.encrypt(&plain_sdr[0], plain_sdr.size(), encrypted_payload)) {
        printf("[AASession] >>> Sent Google DHU ServiceDiscoveryResponse (%u bytes: All Channels, Audio, Video, Sensors, Input)!\n",
               static_cast<unsigned int>(encrypted_payload.size()));
        return sendFrame(0x00, 0x0B, &encrypted_payload[0], encrypted_payload.size());
    }
    return false;
}

bool AASession::sendChannelOpenRequest(uint8_t channel_id) {
    uint8_t plain[4] = { 0x00, 0x07, 0x10, channel_id };
    std::vector<uint8_t> enc;
    if (cryptor_.encrypt(plain, sizeof(plain), enc)) {
        printf("[AASession] >>> Sent ChannelOpenRequest for Channel %u!\n", channel_id);
        return sendFrame(0x00, 0x0B, &enc[0], enc.size());
    }
    return false;
}

bool AASession::sendAvSetupResponse(uint8_t channel) {
    // AVChannelSetupResponse: media_status=OK(2), max_unacked=10, configs[0]=0
    uint8_t plain[8] = { 0x80, 0x03, 0x08, 0x02, 0x10, 0x0A, 0x18, 0x00 };
    std::vector<uint8_t> enc;
    if (cryptor_.encrypt(plain, sizeof(plain), enc)) {
        printf("[AASession] >>> Sent AV SETUP_RESPONSE on Channel %u (max_unacked=10)!\n", channel);
        return sendFrame(channel, 0x0B, &enc[0], enc.size());
    }
    return false;
}

bool AASession::sendVideoFocusIndication(uint8_t channel) {
    // VideoFocusIndication: focus_mode=FOCUSED(1), unrequested=false(0)
    uint8_t plain[6] = { 0x80, 0x08, 0x08, 0x01, 0x10, 0x00 };
    std::vector<uint8_t> enc;
    if (cryptor_.encrypt(plain, sizeof(plain), enc)) {
        printf("[AASession] >>> Sent VideoFocusIndication (FOCUSED) on Channel %u!\n", channel);
        return sendFrame(channel, 0x0B, &enc[0], enc.size());
    }
    return false;
}

bool AASession::sendAvMediaAck(uint8_t channel, uint32_t count) {
    (void)count;
    // Canonical openauto / aasdk AVMediaAckIndication (session=0, value=1)
    uint8_t plain[6] = { 0x80, 0x04, 0x08, 0x00, 0x10, 0x01 };
    std::vector<uint8_t> enc;
    if (cryptor_.encrypt(plain, sizeof(plain), enc)) {
        return sendFrame(channel, 0x0B, &enc[0], enc.size());
    }
    return false;
}

bool AASession::sendAudioFocusResponse(uint8_t focus_state) {
    uint8_t plain[4] = { 0x00, 0x13, 0x08, focus_state };
    std::vector<uint8_t> enc;
    if (cryptor_.encrypt(plain, sizeof(plain), enc)) {
        printf("[AASession] >>> Sent AudioFocusResponse (state=%u)!\n", focus_state);
        return sendFrame(0x00, 0x0B, &enc[0], enc.size());
    }
    return false;
}

void AASession::sessionLoop() {
    printf("[AASession] Protocol thread active on fd %d.\n", client_fd_);

    // Step 1: Send Android Auto VersionRequest (Major 1, Minor 6, Status 0)
    // Frame: Channel 0 (Control), Flags 0x03 (First | Last, Unencrypted)
    uint8_t version_req[8] = {
        0x00, 0x01, // Message ID: 1 (VersionRequest)
        0x00, 0x01, // Major version: 1
        0x00, 0x06, // Minor version: 6
        0x00, 0x00  // Status: 0
    };

    printf("[AASession] Sending VersionRequest (v1.6)...\n");
    if (!sendFrame(0x00, 0x03, version_req, sizeof(version_req))) {
        printf("[AASession] Error: Failed to send VersionRequest.\n");
        return;
    }

    // Step 2: Read incoming frames from the phone
    while (is_running_) {
        uint8_t header[4];
        if (!readExact(header, 4)) {
            if (is_running_) {
                printf("[AASession] Phone closed socket or read error.\n");
            }
            break;
        }

        uint8_t channel = header[0];
        uint8_t flags   = header[1];
        uint16_t length = (static_cast<uint16_t>(header[2]) << 8) | header[3];

        uint32_t total_size = 0;
        // Check if frame has FrameType::FIRST (bit 0 set, bit 1 clear: flags & 0x03 == 1)
        // Per canonical aasdk specification (FrameHeader.cpp:33, FrameSize.cpp:52):
        // FrameType::FIRST carries an extended 6-byte size header (2 bytes length + 4 bytes totalSize).
        if ((flags & 0x03) == 0x01) {
            uint8_t total_buf[4];
            if (!readExact(total_buf, 4)) {
                printf("[AASession] Failed to read extended total_size header.\n");
                break;
            }
            total_size = (static_cast<uint32_t>(total_buf[0]) << 24) |
                         (static_cast<uint32_t>(total_buf[1]) << 16) |
                         (static_cast<uint32_t>(total_buf[2]) << 8)  |
                          static_cast<uint32_t>(total_buf[3]);
            printf("[AASession] Frame RX (EXTENDED FIRST): Ch=%u, Flags=0x%02X, Len=%u, Total=%u\n",
                   channel, flags, length, total_size);
        } else {
            printf("[AASession] Frame RX: Ch=%u, Flags=0x%02X, Len=%u\n",
                   channel, flags, length);
        }

        if (length > 0) {
            std::vector<uint8_t> payload(length);
            if (!readExact(&payload[0], length)) {
                printf("[AASession] Failed to read payload bytes (%u bytes).\n", length);
                break;
            }

            // Decrypt payload if frame has encryption flag (0x08)
            std::vector<uint8_t> decrypted_chunk;
            if (flags & 0x08) {
                if (!cryptor_.decrypt(&payload[0], payload.size(), decrypted_chunk)) {
                    printf("[AASession] Failed to decrypt encrypted frame (%u bytes).\n", length);
                    continue;
                }
            } else {
                decrypted_chunk = payload;
            }

            // Reassemble fragmented frames per channel
            uint8_t frame_type = flags & 0x03; // 0=MIDDLE, 1=FIRST, 2=LAST, 3=BULK
            std::vector<uint8_t> frame_data;

            if (frame_type == 0x01) { // FIRST
                reassembly_[channel].clear();
                reassembly_[channel].insert(reassembly_[channel].end(),
                                             decrypted_chunk.begin(), decrypted_chunk.end());
                continue; // Wait for remaining fragments
            } else if (frame_type == 0x00) { // MIDDLE
                reassembly_[channel].insert(reassembly_[channel].end(),
                                             decrypted_chunk.begin(), decrypted_chunk.end());
                continue; // Wait for remaining fragments
            } else if (frame_type == 0x02) { // LAST
                reassembly_[channel].insert(reassembly_[channel].end(),
                                             decrypted_chunk.begin(), decrypted_chunk.end());
                frame_data = reassembly_[channel];
                reassembly_[channel].clear();
            } else { // 0x03 = BULK (Single unfragmented message)
                frame_data = decrypted_chunk;
            }

            // Dump first bytes in hex
            printf("            Data: ");
            for (size_t i = 0; i < frame_data.size() && i < 16; ++i) {
                printf("%02X ", frame_data[i]);
            }
            if (frame_data.size() > 16) printf("...");
            printf("\n");

            // Universal Message Dispatcher based on protocol wire captures
            uint16_t msg_id = frame_data.size() >= 2 ? ((static_cast<uint16_t>(frame_data[0]) << 8) | frame_data[1]) : 0;

            if (msg_id == 7) { // ChannelOpenRequest (sent by phone on the channel being opened!)
                printf("[AASession] >>> Received ChannelOpenRequest on Channel %u! Sending ChannelOpenResponse (SUCCESS)...\n",
                       channel);
                uint8_t open_resp[4] = { 0x00, 0x08, 0x08, 0x00 }; // Msg ID 8 (ChannelOpenResponse), Status 0 (OK)
                std::vector<uint8_t> enc_resp;
                if (cryptor_.encrypt(open_resp, sizeof(open_resp), enc_resp)) {
                    sendFrame(channel, 0x0F, &enc_resp[0], enc_resp.size()); // Specific channel flag 0x0F
                }
            } else if (msg_id == 0x8000) { // AV SETUP_REQUEST (Phone requests AV configuration on Video/Audio channel)
                // Only focus/open the main dashboard display. Advertising/focusing all
                // DHU display channels makes the phone encode several H.264 streams; we
                // only ingest Channel 6, so keep Channels 2/4 unfocused to reduce load.
                printf("[AASession] >>> Received AV SETUP_REQUEST on Channel %u! Sending SETUP_RESPONSE%s...\n",
                       channel, (channel == 6) ? " and VIDEO_FOCUS" : "");
                sendAvSetupResponse(channel);
                if (channel == 6) {
                    sendVideoFocusIndication(channel);
                }
            } else if (msg_id == 0x8001) { // AV START_INDICATION
                printf("[AASession] >>> Received AV START_INDICATION on Channel %u! Projection ACTIVE!\n", channel);
                if ((channel == 8 || channel == 9) && audio_state_cb_) {
                    audio_state_cb_(channel, true, audio_state_user_data_);
                }
            } else if (msg_id == 0x8002) { // AV STOP_INDICATION
                printf("[AASession] >>> Received AV STOP_INDICATION on Channel %u.\n", channel);
                if ((channel == 8 || channel == 9) && audio_state_cb_) {
                    audio_state_cb_(channel, false, audio_state_user_data_);
                }
            } else if (msg_id == 0x8008) { // VIDEO_FOCUS_INDICATION
                printf("[AASession] >>> Received VideoFocusIndication on Channel %u.\n", channel);
            } else if (msg_id == 0x0000 || msg_id == 0x0001) { // AV Media Data (Live H.264 video frames)
                media_frames_++;
                if ((media_frames_ % 30) == 1) {
                    printf("[AASession] >>> Streaming Media Frame %u on Channel %u (%u bytes)\n",
                           media_frames_, channel, static_cast<unsigned int>(frame_data.size()));
                }
                if ((channel == 9 || channel == 8) && msg_id == 0x0000 && frame_data.size() > 10) {
                    // Observe AA PCM and optionally pass a bounded probe chunk to A2I.
                    ++audio_observe_frames_;
                    size_t pcm_len = frame_data.size() - 10;
                    uint64_t pts = (static_cast<uint64_t>(frame_data[2]) << 56) |
                                  (static_cast<uint64_t>(frame_data[3]) << 48) |
                                  (static_cast<uint64_t>(frame_data[4]) << 40) |
                                  (static_cast<uint64_t>(frame_data[5]) << 32) |
                                  (static_cast<uint64_t>(frame_data[6]) << 24) |
                                  (static_cast<uint64_t>(frame_data[7]) << 16) |
                                  (static_cast<uint64_t>(frame_data[8]) << 8)  |
                                   static_cast<uint64_t>(frame_data[9]);
                    if (audio_observe_frames_ <= 5 || (audio_observe_frames_ % 50) == 0) {
                        printf("[AASession] >>> Observed PCM audio frame #%u on Channel %u (%u bytes, pts=%llu; direct A2I probe active)\n",
                               audio_observe_frames_, channel,
                               static_cast<unsigned int>(pcm_len),
                               static_cast<unsigned long long>(pts));
                    }
                    if (audio_cb_) {
                        const uint8_t* pcm_ptr = &frame_data[10];
                        audio_cb_(channel, audio_observe_frames_, pcm_ptr, pcm_len, pts, audio_user_data_);
                    }
                } else if (channel == 6) { // Channel 6 is EXCLUSIVELY the Main Dashboard Screen (display_id=0, display_type=MAIN)
                    // Use protocol boundaries, never scan the message ID or timestamp:
                    // 0x0001 = 2-byte ID + codec data; 0x0000 = ID + 8-byte PTS + video.
                    // Timestamps can contain 00 00 01 and look like Annex-B start codes.
                    const uint8_t* p = frame_data.empty() ? NULL : &frame_data[0];
                    size_t sz = frame_data.size();
                    const size_t offset = (msg_id == 0x0001) ? 2 : 10;
                    if (sz <= offset) {
                        printf("[AASession] Dropping truncated/empty video message 0x%04X (%u bytes).\n",
                               msg_id, static_cast<unsigned int>(sz));
                        continue;
                    }
                    const uint8_t* nal_ptr = p + offset;
                    size_t nal_len = sz - offset;

                    if (msg_id == 0x0001) { // AV_MEDIA_CONFIG (SPS / PPS)
                        sps_pps_cache_.assign(nal_ptr, nal_ptr + nal_len);
                        printf("[AASession] >>> Received H.264 SPS/PPS codec config (%u bytes)!\n",
                               static_cast<unsigned int>(nal_len));
                        if (config_cb_) {
                            config_cb_(nal_ptr, nal_len, config_user_data_);
                        }
                    } else { // 0x0000 = AV_MEDIA_DATA (Video Slices with 8-byte PTS)
                        uint64_t pts = 0;
                        if (sz >= 10) {
                            pts = (static_cast<uint64_t>(p[2]) << 56) |
                                  (static_cast<uint64_t>(p[3]) << 48) |
                                  (static_cast<uint64_t>(p[4]) << 40) |
                                  (static_cast<uint64_t>(p[5]) << 32) |
                                  (static_cast<uint64_t>(p[6]) << 24) |
                                  (static_cast<uint64_t>(p[7]) << 16) |
                                  (static_cast<uint64_t>(p[8]) << 8)  |
                                   static_cast<uint64_t>(p[9]);
                        }

                        // VideoManager owns the hardware decoder feed and performs any
                        // in-band SPS/PPS prepend next to the mdev Message 3 send. Keep
                        // AASession limited to protocol parsing and fixed-offset slicing.
                        if (video_cb_) {
                            video_cb_(media_frames_, nal_ptr, nal_len, pts, video_user_data_);
                        }
                    }
                }
                sendAvMediaAck(channel, media_frames_);
            } else if (channel == 0) {
                if (msg_id == 2) { // VersionResponse
                    if (payload.size() >= 8) {
                        uint16_t major  = (payload[2] << 8) | payload[3];
                        uint16_t minor  = (payload[4] << 8) | payload[5];
                        uint16_t status = (payload[6] << 8) | payload[7];
                        printf("[AASession] >>> VersionResponse: Major=%u, Minor=%u, Status=%u (SUCCESS)\n",
                               major, minor, status);

                        // Step 2: Initialize Cryptor with mTLS cert and send TLS ClientHello
                        if (cryptor_.init()) {
                            std::vector<uint8_t> tls_client_hello;
                            if (cryptor_.startHandshake(tls_client_hello)) {
                                sendSslHandshake(tls_client_hello);
                                printf("[AASession] Sent TLS ClientHello (with mTLS client cert) to phone.\n");
                            }
                        }
                    }
                } else if (msg_id == 3) { // SSL Handshake
                    printf("[AASession] >>> Received SSL Handshake from phone (%u bytes)\n",
                           static_cast<unsigned int>(payload.size() - 2));

                    std::vector<uint8_t> tls_out;
                    cryptor_.processHandshakeData(&payload[2], payload.size() - 2, tls_out);

                    if (!tls_out.empty()) {
                        sendSslHandshake(tls_out);
                        printf("[AASession] Sent TLS Handshake response (%u bytes).\n",
                               static_cast<unsigned int>(tls_out.size()));
                    }

                    if (cryptor_.isHandshakeComplete()) {
                        printf("[AASession] >>> TLS Handshake Complete! Sending AuthCompleteNotification...\n");
                        sendAuthComplete();
                        printf("[AASession] >>> Awaiting ServiceDiscoveryRequest from phone...\n");
                    }
                } else if (msg_id == 5) { // ServiceDiscoveryRequest
                    printf("[AASession] >>> Received ServiceDiscoveryRequest from phone! Sending ServiceDiscoveryResponse...\n");
                    sendServiceDiscoveryResponse();
                } else if (msg_id == 0x0012) { // AudioFocusRequest
                    uint8_t requested_focus = (frame_data.size() >= 4) ? frame_data[3] : 0;
                    // Request value 4 is RELEASE; respond with NONE. Otherwise grant focus.
                    uint8_t response_focus = (requested_focus == 4) ? 0 : 1;
                    printf("[AASession] >>> Received AudioFocusRequest on Channel 0 (request=%u)! Sending AudioFocusResponse(state=%u)...\n",
                           requested_focus, response_focus);
                    sendAudioFocusResponse(response_focus);
                } else if (msg_id == 11) { // PingRequest
                    printf("[AASession] >>> Received PingRequest. Replying with PingResponse...\n");
                    uint8_t ping_resp[2] = { 0x00, 0x0C }; // Msg ID 12 (PingResponse)
                    sendFrame(0x00, 0x03, ping_resp, sizeof(ping_resp)); // Pings are unencrypted
                } else {
                    printf("[AASession] Control Message RX: ID=0x%04X, Len=%u\n",
                           msg_id, static_cast<unsigned int>(frame_data.size()));
                }
            } else {
                printf("[AASession] Channel %u Message: ID=0x%04X, Len=%u\n",
                       channel, msg_id, static_cast<unsigned int>(frame_data.size()));
            }
        }
    }

    printf("[AASession] Session loop terminated.\n");
    if (is_running_) {
        is_running_ = false;
        if (disconnect_cb_) {
            disconnect_cb_(disconnect_user_data_);
        }
    }
}

} // namespace nbt
