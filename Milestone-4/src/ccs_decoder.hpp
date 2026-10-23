// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef CCS_DECODER_HPP
#define CCS_DECODER_HPP

// Pure decoder: iDrive controller (CCS35up, MOST FBlock 0xE5) status messages -> Android Auto input events.
// Car-verified layout (test_controls run 2026-10-22, controller instance 0x03, op Status 0xC):
//   fkt 0xC46 RotaryEncoderState, 8 bytes:
//       [0] message counter  [1..2] BE16 absolute detent position (+1 per clockwise detent, wraps)
//       [3] centre push  [4] joystick direction  [5..6] 0  [7] 0xff
//       direction is a 12-step clock: 0 = centred, 1 = up, 4 = right, 7 = down, 10 = left
//       (seen: 1,7,4,10 for the tilt sequence up, down, right, left)
//       Centre push = byte 3 (1 pressed, 0 released; car-verified 2026-10-23, no separate 'held' value,
//       bytes 5/6 stay 0). Tilt right = 4, left = 10 also verified.
//   fkt 0xC47 PushButtonsState (7 bytes) and 0xC4F PushButtonsState2 (8 bytes, [7] = 7):
//       one byte per hard key, 0 released / 1 pressed (a 1.5 s MEDIA hold stayed 1: no 'held' value). Both carry the same state, so
//       state-diffing de-duplicates them. Slots (from the media,com,menu,map,nav,back,options sequence):
//       0 MENU  1 OPTIONS  2 BACK  3 COM  4 MEDIA  5 NAV  6 MAP
// Everything here is plain C++98 so the host test (tests/test_ccs_decoder.cpp) can replay car logs.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace nbt {

// Android key codes / Android Auto extended key codes (all declared on input channel 7 in the SDR).
enum {
    AAK_HOME = 3, AAK_BACK = 4, AAK_CALL = 5,
    AAK_DPAD_UP = 19, AAK_DPAD_DOWN = 20, AAK_DPAD_LEFT = 21, AAK_DPAD_RIGHT = 22, AAK_DPAD_CENTER = 23,
    AAK_SEARCH = 84,
    AAK_ROTARY_CONTROLLER = 65536, AAK_MEDIA = 65537, AAK_NAVIGATION = 65538, AAK_TEL = 65540
};

struct CcsEvent {
    int kind;            // 1 = key (keycode, down), 2 = relative (keycode, delta)
    uint32_t keycode;
    int down;
    int delta;
    const char* what;    // human readable source ("MEDIA", "ROTARY", "TILT_UP", ...)
};

enum { CCS_SLOTS = 7 };

class CcsDecoder {
public:
    CcsDecoder() : inst_(0x03), invert_(false), lastPos_(-1), tiltKey_(0), pushDown_(false) {
        memset(slot_, 0, sizeof(slot_));
        static const char* const names[CCS_SLOTS] = { "MENU", "OPTIONS", "BACK", "COM", "MEDIA", "NAV", "MAP" };
        static const uint32_t keys[CCS_SLOTS] = { AAK_HOME, 0, AAK_BACK, AAK_CALL, AAK_MEDIA, AAK_NAVIGATION, AAK_NAVIGATION };
        for (int i = 0; i < CCS_SLOTS; ++i) { slotName_[i] = names[i]; slotKey_[i] = keys[i]; }
    }

    void setInstance(uint8_t inst) { inst_ = inst; }
    void setInvert(bool inv) { invert_ = inv; }
    void setSlotKey(int slot, uint32_t key) { if (slot >= 0 && slot < CCS_SLOTS) slotKey_[slot] = key; }
    uint32_t slotKey(int slot) const { return (slot >= 0 && slot < CCS_SLOTS) ? slotKey_[slot] : 0; }
    static const char* slotName(int slot) {
        static const char* const names[CCS_SLOTS] = { "MENU", "OPTIONS", "BACK", "COM", "MEDIA", "NAV", "MAP" };
        return (slot >= 0 && slot < CCS_SLOTS) ? names[slot] : "?";
    }
    // Index of a slot by name (case-sensitive upper case), -1 if unknown.
    static int slotByName(const char* n) {
        for (int i = 0; i < CCS_SLOTS; ++i) if (n && !strcmp(n, slotName(i))) return i;
        return -1;
    }

    // 12-step clock direction -> DPAD key (0 = centred / unknown).
    static uint32_t tiltKey(unsigned dir) {
        if (dir == 0 || dir > 12) return 0;
        if (dir == 12 || dir <= 2) return AAK_DPAD_UP;
        if (dir <= 5) return AAK_DPAD_RIGHT;
        if (dir <= 8) return AAK_DPAD_DOWN;
        return AAK_DPAD_LEFT;
    }
    static const char* tiltName(uint32_t key) {
        switch (key) {
            case AAK_DPAD_UP: return "TILT_UP"; case AAK_DPAD_DOWN: return "TILT_DOWN";
            case AAK_DPAD_LEFT: return "TILT_LEFT"; case AAK_DPAD_RIGHT: return "TILT_RIGHT";
            default: return "TILT_?";
        }
    }

    // Feed one MOST message. Returns the number of events written to out (at most max).
    int feed(uint8_t fblock, uint8_t inst, uint16_t fkt, uint8_t op, const uint8_t* d, size_t n,
             CcsEvent* out, int max) {
        int k = 0;
        if (fblock != 0xE5 || inst != inst_ || op != 0xC || !d) return 0;
        if (fkt == 0xC46 && n >= 5) {
            int pos = (d[1] << 8) | d[2];
            if (lastPos_ >= 0 && pos != lastPos_) {
                int delta = (int)(int16_t)(uint16_t)(pos - lastPos_);   // wrap-safe
                if (invert_) delta = -delta;
                if (delta != 0) add(out, max, k, 2, AAK_ROTARY_CONTROLLER, 0, delta, "ROTARY");
            }
            lastPos_ = pos;
            uint32_t tk = tiltKey(d[4]);
            if (tk != tiltKey_) {
                if (tiltKey_) add(out, max, k, 1, tiltKey_, 0, 0, tiltName(tiltKey_));
                if (tk) add(out, max, k, 1, tk, 1, 0, tiltName(tk));
                tiltKey_ = tk;
            }
            bool push = d[3] != 0;
            if (push != pushDown_) {
                add(out, max, k, 1, AAK_DPAD_CENTER, push ? 1 : 0, 0, "PUSH");
                pushDown_ = push;
            }
        } else if ((fkt == 0xC47 || fkt == 0xC4F) && n >= 1) {
            for (size_t i = 0; i < n && i < (size_t)CCS_SLOTS; ++i) {
                bool p = d[i] != 0;
                if (p != slot_[i]) {
                    slot_[i] = p;
                    if (slotKey_[i]) add(out, max, k, 1, slotKey_[i], p ? 1 : 0, 0, slotName_[i]);
                    else add(out, max, k, 1, 0, p ? 1 : 0, 0, slotName_[i]);   // unmapped: keycode 0, caller logs only
                }
            }
        }
        return k;
    }

private:
    static void add(CcsEvent* out, int max, int& k, int kind, uint32_t key, int down, int delta, const char* what) {
        if (!out || k >= max) return;
        out[k].kind = kind; out[k].keycode = key; out[k].down = down; out[k].delta = delta; out[k].what = what;
        ++k;
    }

    uint8_t inst_;
    bool invert_;
    int lastPos_;
    uint32_t tiltKey_;
    bool pushDown_;
    bool slot_[CCS_SLOTS];
    uint32_t slotKey_[CCS_SLOTS];
    const char* slotName_[CCS_SLOTS];
};

} // namespace nbt

#endif
