// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef TOUCH_FRAME_HPP
#define TOUCH_FRAME_HPP

// Pure parser for the HMI touch PDU (UDP 160.48.199.115:30501 -> 30567, 112 bytes, ident B0 04 80 02).
// Layout (finger 0 verified 2026-10, two fingers verified with the w9 pinch capture 2026-10-23):
//   16-byte header, then 12 records of 8 bytes: [00 c? 40 seq][4 data bytes].
//   The data bytes of records 4..11 form one contiguous stream (record headers are NOT part of it):
//     stream[0]   flags / rolling counter (top 2 bits count 00,40,80,c0)
//     stream[1]   number of contacts N
//     then N contacts of 6 bytes: x_hi x_lo y_hi y_lo size (state << 4 | id)
//   state: 1 down, 2 move, 3 up. id: stable finger id (0..). x 0..1279, y 0..479.
//   A second finger therefore straddles record boundaries (e.g. finger 1 = records 6 and 7).
// Plain C++98, no allocation, so it is host-testable (tests/test_touch_frame.cpp).

#include <stddef.h>
#include <stdint.h>

namespace nbt {

enum { kTouchMaxContacts = 5 };   // 32 stream bytes - 2 header bytes = 5 contacts of 6 bytes

struct TouchContact {
    int id;
    int state;
    int x;
    int y;
    int size;
};

// Byte offset in the UDP frame of stream byte i.
inline size_t touchStreamOffset(size_t i) { return 52 + 8 * (i / 4) + (i % 4); }

// Returns the number of contacts written to out (0..max), or -1 if the frame is not a touch PDU.
// *declared receives the contact count the frame claims (may exceed what fits).
inline int parseTouchFrame(const unsigned char* buf, size_t n, TouchContact* out, int max, int* declared) {
    if (declared) *declared = 0;
    if (!buf || n < 64) return -1;
    if (buf[0] != 0xB0 || buf[1] != 0x04 || buf[2] != 0x80 || buf[3] != 0x02) return -1;
    if (touchStreamOffset(1) >= n) return -1;
    int count = buf[touchStreamOffset(1)];
    if (declared) *declared = count;
    if (count > kTouchMaxContacts) count = kTouchMaxContacts;
    int k = 0;
    for (int c = 0; c < count && k < max; ++c) {
        const size_t i0 = 2 + 6 * (size_t)c;
        if (touchStreamOffset(i0 + 5) >= n) break;
        TouchContact& t = out[k];
        t.x = (buf[touchStreamOffset(i0)] << 8) | buf[touchStreamOffset(i0 + 1)];
        t.y = (buf[touchStreamOffset(i0 + 2)] << 8) | buf[touchStreamOffset(i0 + 3)];
        t.size = buf[touchStreamOffset(i0 + 4)];
        const unsigned char st = buf[touchStreamOffset(i0 + 5)];
        t.state = (st >> 4) & 0x0F;
        t.id = st & 0x0F;
        if (t.state < 1 || t.state > 3) continue;     // unknown state: ignore this contact
        if (t.id >= kTouchMaxContacts) continue;      // id outside our slot table
        ++k;
    }
    return k;
}

// Test/helper: writes a touch PDU (112 bytes) for the given contacts. counter = stream[0].
inline void buildTouchFrame(unsigned char* f, const TouchContact* c, int nc, unsigned char counter) {
    for (int i = 0; i < 112; ++i) f[i] = 0;
    f[0] = 0xB0; f[1] = 0x04; f[2] = 0x80; f[3] = 0x02; f[7] = 0x68;
    for (int r = 0; r < 12; ++r) { f[16 + 8 * r + 1] = (unsigned char)(r == 0 ? 0xc5 : (r == 11 ? 0xc3 : 0xc1));
                                   f[16 + 8 * r + 2] = 0x40; f[16 + 8 * r + 3] = (unsigned char)r; }
    f[touchStreamOffset(0)] = counter;
    f[touchStreamOffset(1)] = (unsigned char)nc;
    for (int k = 0; k < nc && k < kTouchMaxContacts; ++k) {
        const size_t i0 = 2 + 6 * (size_t)k;
        f[touchStreamOffset(i0)]     = (unsigned char)(c[k].x >> 8);
        f[touchStreamOffset(i0 + 1)] = (unsigned char)(c[k].x & 255);
        f[touchStreamOffset(i0 + 2)] = (unsigned char)(c[k].y >> 8);
        f[touchStreamOffset(i0 + 3)] = (unsigned char)(c[k].y & 255);
        f[touchStreamOffset(i0 + 4)] = (unsigned char)c[k].size;
        f[touchStreamOffset(i0 + 5)] = (unsigned char)((c[k].state << 4) | (c[k].id & 0x0F));
    }
}

} // namespace nbt

#endif // TOUCH_FRAME_HPP
