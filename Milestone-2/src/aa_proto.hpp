#ifndef AA_PROTO_HPP
#define AA_PROTO_HPP

// Minimal Android Auto wire-protocol helpers (C++03 / GCC 4.4 compatible).
//
// Channel IDs and audio formats below come from the embedded Google DHU
// service-discovery manifest (include/dhu_sdp.h). Message IDs follow the
// aasdk / openauto reference implementation.

#include <stdint.h>
#include <stddef.h>
#include <vector>

namespace nbt {

// ---- Channel IDs declared in kDhuSdpProtobuf --------------------------------
enum AAChannel {
    AA_CH_CONTROL      = 0,
    AA_CH_SENSOR       = 1,
    AA_CH_INPUT_ROTARY = 3,   // controller: center(23) + rotary(65536)
    AA_CH_VIDEO_MAIN   = 6,   // 1280x480 main display
    AA_CH_INPUT_TOUCH  = 7,   // touchscreen + key list
    AA_CH_AUDIO_SPEECH = 8,   // guidance / assistant: 16 kHz mono (config 0)
    AA_CH_AUDIO_MEDIA  = 9,   // media: 48 kHz stereo 16-bit
    AA_CH_NAV_STATUS   = 10,
    AA_CH_MIC          = 14   // microphone: 16 kHz mono 16-bit
};

// ---- Message IDs -------------------------------------------------------------
enum AAMessage {
    AA_MSG_AV_DATA            = 0x0000,
    AA_MSG_AV_CODEC_CONFIG    = 0x0001,
    AA_MSG_CHANNEL_OPEN_REQ   = 0x0007,
    AA_MSG_AV_SETUP_REQUEST   = 0x8000,
    AA_MSG_AV_START           = 0x8001,
    AA_MSG_AV_STOP            = 0x8002,
    AA_MSG_AV_SETUP_RESPONSE  = 0x8003,
    AA_MSG_AV_ACK             = 0x8004,
    AA_MSG_MIC_REQUEST        = 0x8005,
    AA_MSG_MIC_RESPONSE       = 0x8006,
    // Input channels reuse the 0x80xx space with different meanings.
    AA_MSG_INPUT_REPORT       = 0x8001,
    AA_MSG_INPUT_BIND_REQUEST = 0x8002,
    AA_MSG_INPUT_BIND_RESP    = 0x8003
};

// ---- Android key codes accepted by the DHU manifest -------------------------
enum AAKeycode {
    AA_KEYCODE_NONE         = 0,
    AA_KEYCODE_HOME         = 3,
    AA_KEYCODE_BACK         = 4,
    AA_KEYCODE_CALL         = 5,
    AA_KEYCODE_ENDCALL      = 6,
    AA_KEYCODE_DPAD_UP      = 19,
    AA_KEYCODE_DPAD_DOWN    = 20,
    AA_KEYCODE_DPAD_LEFT    = 21,
    AA_KEYCODE_DPAD_RIGHT   = 22,
    AA_KEYCODE_DPAD_CENTER  = 23,
    AA_KEYCODE_SEARCH       = 84,
    AA_KEYCODE_PLAY_PAUSE   = 85,
    AA_KEYCODE_NEXT         = 87,
    AA_KEYCODE_PREVIOUS     = 88,
    AA_KEYCODE_PLAY         = 126,
    AA_KEYCODE_PAUSE        = 127,
    AA_KEYCODE_ROTARY       = 65536   // relative event (delta = detents)
};

namespace pb {

inline void putVarint(std::vector<uint8_t>& out, uint64_t v) {
    while (v >= 0x80) {
        out.push_back(static_cast<uint8_t>(v | 0x80));
        v >>= 7;
    }
    out.push_back(static_cast<uint8_t>(v));
}

inline void putTag(std::vector<uint8_t>& out, uint32_t field, uint32_t wire) {
    putVarint(out, (static_cast<uint64_t>(field) << 3) | wire);
}

inline void putVarintField(std::vector<uint8_t>& out, uint32_t field, uint64_t v) {
    putTag(out, field, 0);
    putVarint(out, v);
}

// protobuf int32 negative values are sign-extended to 64 bits (10-byte varint)
inline void putInt32Field(std::vector<uint8_t>& out, uint32_t field, int32_t v) {
    putTag(out, field, 0);
    putVarint(out, static_cast<uint64_t>(static_cast<int64_t>(v)));
}

inline void putBytesField(std::vector<uint8_t>& out, uint32_t field,
                          const std::vector<uint8_t>& sub) {
    putTag(out, field, 2);
    putVarint(out, sub.size());
    out.insert(out.end(), sub.begin(), sub.end());
}

} // namespace pb

// InputReport { timestamp=1, key_event=4 { keys=1 { keycode=1, down=2, metastate=3, longpress=4 } } }
inline std::vector<uint8_t> buildKeyInputReport(uint64_t timestamp_ns, uint32_t keycode, bool down) {
    std::vector<uint8_t> key;
    pb::putVarintField(key, 1, keycode);
    pb::putVarintField(key, 2, down ? 1 : 0);
    pb::putVarintField(key, 3, 0);
    pb::putVarintField(key, 4, 0);

    std::vector<uint8_t> key_event;
    pb::putBytesField(key_event, 1, key);

    std::vector<uint8_t> report;
    pb::putVarintField(report, 1, timestamp_ns);
    pb::putBytesField(report, 4, key_event);

    std::vector<uint8_t> msg;
    msg.push_back(0x80);
    msg.push_back(0x01);
    msg.insert(msg.end(), report.begin(), report.end());
    return msg;
}

// InputReport { timestamp=1, relative_event=6 { data=1 { keycode=1, delta=2 } } }
inline std::vector<uint8_t> buildRelativeInputReport(uint64_t timestamp_ns, uint32_t keycode, int32_t delta) {
    std::vector<uint8_t> rel;
    pb::putVarintField(rel, 1, keycode);
    pb::putInt32Field(rel, 2, delta);

    std::vector<uint8_t> rel_event;
    pb::putBytesField(rel_event, 1, rel);

    std::vector<uint8_t> report;
    pb::putVarintField(report, 1, timestamp_ns);
    pb::putBytesField(report, 6, rel_event);

    std::vector<uint8_t> msg;
    msg.push_back(0x80);
    msg.push_back(0x01);
    msg.insert(msg.end(), report.begin(), report.end());
    return msg;
}

// Microphone data: 0x0000 + 8-byte big-endian timestamp (us) + PCM
inline std::vector<uint8_t> buildMicDataMessage(uint64_t timestamp_us, const uint8_t* pcm, size_t len) {
    std::vector<uint8_t> msg;
    msg.reserve(len + 10);
    msg.push_back(0x00);
    msg.push_back(0x00);
    for (int shift = 56; shift >= 0; shift -= 8) {
        msg.push_back(static_cast<uint8_t>((timestamp_us >> shift) & 0xFF));
    }
    if (len > 0 && pcm) msg.insert(msg.end(), pcm, pcm + len);
    return msg;
}

} // namespace nbt

#endif // AA_PROTO_HPP
