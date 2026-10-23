// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "sdr_builder.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace nbt {

SdrIdentity::SdrIdentity()
    : carMake("Toyota"), carModel("GR Supra"), carYear("2020"),
      vehicleId("6f1c2a40-5a1e-4c7e-9e21-0a90bd51e0a7"),
      huMake("Toyota"), huModel("NBT Evo ID6"), swBuild("nbt-aad"), swVersion("1.3.33") {}

static void envStr(const char* name, std::string& out) {
    const char* v = getenv(name);
    if (v && *v) out = v;
}

void SdrIdentity::loadEnv() {
    envStr("NBT_CAR_MAKE", carMake);   envStr("NBT_CAR_MODEL", carModel);
    envStr("NBT_CAR_YEAR", carYear);   envStr("NBT_VEHICLE_ID", vehicleId);
    envStr("NBT_HU_MAKE", huMake);     envStr("NBT_HU_MODEL", huModel);
    envStr("NBT_SW_VERSION", swVersion);
    envStr("NBT_BT_ADDR", btAdapterAddress);
}

namespace {

void putVarint(std::vector<uint8_t>& o, uint64_t v) {
    while (v >= 0x80) { o.push_back((uint8_t)(v | 0x80)); v >>= 7; }
    o.push_back((uint8_t)v);
}
bool getVarint(const uint8_t* p, size_t n, size_t& pos, uint64_t& v) {
    v = 0; int sh = 0;
    while (pos < n && sh < 64) {
        uint8_t b = p[pos++];
        v |= (uint64_t)(b & 0x7f) << sh;
        if (!(b & 0x80)) return true;
        sh += 7;
    }
    return false;
}
void putLen(std::vector<uint8_t>& o, int field, const uint8_t* d, size_t n) {
    putVarint(o, ((uint64_t)field << 3) | 2);
    putVarint(o, n);
    o.insert(o.end(), d, d + n);
}
void putStr(std::vector<uint8_t>& o, int field, const std::string& s) {
    putLen(o, field, reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

// Rewrites string fields of a message: 'repl' maps field number -> new string.
// Unlisted fields are copied byte for byte.
struct Repl { int field; const std::string* value; };

bool rewrite(const uint8_t* p, size_t n, const Repl* repl, size_t nrepl, std::vector<uint8_t>& out) {
    size_t pos = 0;
    while (pos < n) {
        size_t start = pos;
        uint64_t key;
        if (!getVarint(p, n, pos, key)) return false;
        int f = (int)(key >> 3), wt = (int)(key & 7);
        size_t valStart = pos;
        if (wt == 0) { uint64_t x; if (!getVarint(p, n, pos, x)) return false; }
        else if (wt == 2) { uint64_t l; if (!getVarint(p, n, pos, l)) return false; if (pos + l > n) return false; pos += (size_t)l; }
        else if (wt == 5) pos += 4;
        else if (wt == 1) pos += 8;
        else return false;
        if (pos > n) return false;
        bool replaced = false;
        if (wt == 2) {
            for (size_t i = 0; i < nrepl; ++i) {
                if (repl[i].field == f) { putStr(out, f, *repl[i].value); replaced = true; break; }
            }
        }
        (void)valStart;
        if (!replaced) out.insert(out.end(), p + start, p + pos);
    }
    return true;
}

} // namespace

std::vector<uint8_t> buildServiceDiscovery(const uint8_t* base, size_t baseLen, const SdrIdentity& id) {
    std::vector<uint8_t> out;
    // Fields 16/17 (headunit info) carry copies of the identity strings:
    // 1 car make, 2 car model, 3 year, 4 vehicle id, 5 hu make, 6 hu model, 7 build, 8 version
    Repl top[] = {
        {2, &id.carMake}, {3, &id.carModel}, {4, &id.carYear}, {5, &id.vehicleId},
        {7, &id.huMake},  {8, &id.huModel},  {9, &id.swBuild}, {10, &id.swVersion}
    };
    Repl info[] = {
        {1, &id.carMake}, {2, &id.carModel}, {3, &id.carYear}, {4, &id.vehicleId},
        {5, &id.huMake},  {6, &id.huModel},  {7, &id.swBuild}, {8, &id.swVersion}
    };
    size_t pos = 0;
    while (pos < baseLen) {
        size_t start = pos;
        uint64_t key;
        if (!getVarint(base, baseLen, pos, key)) return std::vector<uint8_t>(base, base + baseLen);
        int f = (int)(key >> 3), wt = (int)(key & 7);
        if (wt == 2) {
            uint64_t l;
            if (!getVarint(base, baseLen, pos, l) || pos + l > baseLen)
                return std::vector<uint8_t>(base, base + baseLen);
            const uint8_t* body = base + pos;
            pos += (size_t)l;
            if (f == 17) {
                std::vector<uint8_t> sub;
                if (!rewrite(body, (size_t)l, info, sizeof(info) / sizeof(info[0]), sub))
                    return std::vector<uint8_t>(base, base + baseLen);
                putLen(out, 17, sub.empty() ? NULL : &sub[0], sub.size());
            } else {
                bool done = false;
                for (size_t i = 0; i < sizeof(top) / sizeof(top[0]); ++i) {
                    if (top[i].field == f) { putStr(out, f, *top[i].value); done = true; break; }
                }
                if (!done) out.insert(out.end(), base + start, base + pos);
            }
        } else {
            // varint / fixed fields copied verbatim
            if (wt == 0) { uint64_t x; if (!getVarint(base, baseLen, pos, x)) break; }
            else if (wt == 5) pos += 4;
            else if (wt == 1) pos += 8;
            else break;
            out.insert(out.end(), base + start, base + pos);
        }
    }
    if (!id.btAdapterAddress.empty()) {
        // ChannelDescriptor{ channel_id=15, bluetooth_channel(6){ adapter_address(1) } }
        std::vector<uint8_t> bt, cd;
        putStr(bt, 1, id.btAdapterAddress);
        putVarint(cd, (1u << 3) | 0); putVarint(cd, 15);
        putLen(cd, 6, &bt[0], bt.size());
        putLen(out, 1, &cd[0], cd.size());
    }
    return out;
}


// ---- main-display VideoConfig experiments ----------------------------------------------------------
namespace {

struct VidOv { bool res, mw, mh, ins, dpi; int vres, vmw, vmh, it, ib, il, ir, vdpi; };

bool envInt(const char* n, int& v) {
    const char* e = getenv(n);
    if (!e || !*e) return false;
    v = atoi(e);
    return true;
}

// Rewrites a message, calling 'sub' for length-delimited field 'field' (if non-zero) and replacing
// varint fields listed in vv[] (field -> value). Everything else is copied.
struct VarRepl { int field; bool on; int value; };

bool patchVideoConfig(const uint8_t* p, size_t n, const VidOv& o, std::vector<uint8_t>& out) {
    size_t pos = 0;
    const uint8_t* f11p = NULL;
    size_t f11n = 0;
    VarRepl vr[5] = { {1, o.res, o.vres}, {3, o.mw, o.vmw}, {4, o.mh, o.vmh}, {5, o.dpi, o.vdpi}, {9, o.dpi, o.vdpi} };
    while (pos < n) {
        size_t start = pos;
        uint64_t key;
        if (!getVarint(p, n, pos, key)) return false;
        int f = (int)(key >> 3), wt = (int)(key & 7);
        if (wt == 0) {
            uint64_t x;
            if (!getVarint(p, n, pos, x)) return false;
            bool done = false;
            for (int i = 0; i < 5; ++i)
                if (vr[i].field == f && vr[i].on) { putVarint(out, key); putVarint(out, (uint64_t)vr[i].value); done = true; }
            if (!done) out.insert(out.end(), p + start, p + pos);
        } else if (wt == 2) {
            uint64_t l;
            if (!getVarint(p, n, pos, l) || pos + l > n) return false;
            if (f == 11 && o.ins) { f11p = p + pos; f11n = (size_t)l; pos += (size_t)l; }   // rebuilt below
            else { pos += (size_t)l; out.insert(out.end(), p + start, p + pos); }
        } else if (wt == 5) { pos += 4; out.insert(out.end(), p + start, p + pos); }
        else if (wt == 1) { pos += 8; out.insert(out.end(), p + start, p + pos); }
        else return false;
    }
    if (o.ins) {
        std::vector<uint8_t> vi, avc;
        putVarint(vi, (1u << 3) | 0); putVarint(vi, (uint64_t)o.it);
        putVarint(vi, (2u << 3) | 0); putVarint(vi, (uint64_t)o.ib);
        putVarint(vi, (3u << 3) | 0); putVarint(vi, (uint64_t)o.il);
        putVarint(vi, (4u << 3) | 0); putVarint(vi, (uint64_t)o.ir);
        putLen(avc, 1, &vi[0], vi.size());
        // keep the other AdditionalVideoConfig fields (empty insets 2/3, theme, ...) of the canned manifest
        size_t q = 0;
        while (f11p && q < f11n) {
            size_t st = q;
            uint64_t k;
            if (!getVarint(f11p, f11n, q, k)) break;
            int ff = (int)(k >> 3), w = (int)(k & 7);
            if (w == 0) { uint64_t x; if (!getVarint(f11p, f11n, q, x)) break; }
            else if (w == 2) { uint64_t ll; if (!getVarint(f11p, f11n, q, ll) || q + ll > f11n) break; q += (size_t)ll; }
            else if (w == 5) q += 4; else if (w == 1) q += 8; else break;
            if (ff != 1) avc.insert(avc.end(), f11p + st, f11p + q);
        }
        putLen(out, 11, &avc[0], avc.size());
    }
    return true;
}

// Generic walker: field path [1(channel, id==6) -> 3 (media sink) -> 4 (video config)].
bool patchMediaSink(const uint8_t* p, size_t n, const VidOv& o, std::vector<uint8_t>& out) {
    size_t pos = 0;
    while (pos < n) {
        size_t start = pos;
        uint64_t key;
        if (!getVarint(p, n, pos, key)) return false;
        int f = (int)(key >> 3), wt = (int)(key & 7);
        if (wt == 0) { uint64_t x; if (!getVarint(p, n, pos, x)) return false; out.insert(out.end(), p + start, p + pos); }
        else if (wt == 2) {
            uint64_t l;
            if (!getVarint(p, n, pos, l) || pos + l > n) return false;
            const uint8_t* body = p + pos; pos += (size_t)l;
            if (f == 4) {
                std::vector<uint8_t> sub;
                if (!patchVideoConfig(body, (size_t)l, o, sub)) return false;
                putLen(out, 4, sub.empty() ? NULL : &sub[0], sub.size());
            } else out.insert(out.end(), p + start, p + pos);
        }
        else if (wt == 5) { pos += 4; out.insert(out.end(), p + start, p + pos); }
        else if (wt == 1) { pos += 8; out.insert(out.end(), p + start, p + pos); }
        else return false;
    }
    return true;
}

bool patchChannel(const uint8_t* p, size_t n, const VidOv& o, std::vector<uint8_t>& out, bool& isMain) {
    isMain = false;
    // peek channel id (field 1 varint)
    size_t pos = 0;
    while (pos < n) {
        uint64_t key;
        if (!getVarint(p, n, pos, key)) break;
        int f = (int)(key >> 3), wt = (int)(key & 7);
        if (wt == 0) { uint64_t x; if (!getVarint(p, n, pos, x)) break; if (f == 1 && x == 6) isMain = true; }
        else if (wt == 2) { uint64_t l; if (!getVarint(p, n, pos, l)) break; pos += (size_t)l; }
        else if (wt == 5) pos += 4; else if (wt == 1) pos += 8; else break;
    }
    if (!isMain) return false;
    pos = 0;
    while (pos < n) {
        size_t start = pos;
        uint64_t key;
        if (!getVarint(p, n, pos, key)) return false;
        int f = (int)(key >> 3), wt = (int)(key & 7);
        if (wt == 0) { uint64_t x; if (!getVarint(p, n, pos, x)) return false; out.insert(out.end(), p + start, p + pos); }
        else if (wt == 2) {
            uint64_t l;
            if (!getVarint(p, n, pos, l) || pos + l > n) return false;
            const uint8_t* body = p + pos; pos += (size_t)l;
            if (f == 3) {
                std::vector<uint8_t> sub;
                if (!patchMediaSink(body, (size_t)l, o, sub)) return false;
                putLen(out, 3, sub.empty() ? NULL : &sub[0], sub.size());
            } else out.insert(out.end(), p + start, p + pos);
        }
        else if (wt == 5) { pos += 4; out.insert(out.end(), p + start, p + pos); }
        else if (wt == 1) { pos += 8; out.insert(out.end(), p + start, p + pos); }
        else return false;
    }
    return true;
}

} // namespace

bool patchMainVideo(std::vector<uint8_t>& sdr) {
    VidOv o;
    memset(&o, 0, sizeof(o));
    o.res = envInt("NBT_VID_RES", o.vres);
    o.mw = envInt("NBT_VID_MW", o.vmw);
    o.mh = envInt("NBT_VID_MH", o.vmh);
    o.dpi = envInt("NBT_VID_DPI", o.vdpi);
    // Default (car-verified in w7): the phone keeps its UI inside the top 480 rows of its 1280x720 canvas
    // (bottom inset 240) and we show those rows 1:1. NBT_VID_INSETS=off restores the plain manifest.
    const char* ins = getenv("NBT_VID_INSETS");
    if (!ins || !*ins) ins = "0,240,0,0";
    if (strcmp(ins, "off") != 0) {
        int t = 0, b = 0, l = 0, r = 0;
        if (sscanf(ins, "%d,%d,%d,%d", &t, &b, &l, &r) >= 2) { o.ins = true; o.it = t; o.ib = b; o.il = l; o.ir = r; }
    }
    if (!o.res && !o.mw && !o.mh && !o.ins && !o.dpi) return false;
    std::vector<uint8_t> out;
    size_t n = sdr.size(), pos = 0;
    bool changed = false;
    while (pos < n) {
        size_t start = pos;
        uint64_t key;
        if (!getVarint(&sdr[0], n, pos, key)) return false;
        int f = (int)(key >> 3), wt = (int)(key & 7);
        if (wt == 2) {
            uint64_t l;
            if (!getVarint(&sdr[0], n, pos, l) || pos + l > n) return false;
            const uint8_t* body = &sdr[0] + pos; pos += (size_t)l;
            bool isMain = false;
            std::vector<uint8_t> sub;
            if (f == 1 && patchChannel(body, (size_t)l, o, sub, isMain) && isMain) {
                putLen(out, 1, sub.empty() ? NULL : &sub[0], sub.size());
                changed = true;
            } else out.insert(out.end(), &sdr[0] + start, &sdr[0] + pos);
        } else {
            if (wt == 0) { uint64_t x; if (!getVarint(&sdr[0], n, pos, x)) return false; }
            else if (wt == 5) pos += 4; else if (wt == 1) pos += 8; else return false;
            out.insert(out.end(), &sdr[0] + start, &sdr[0] + pos);
        }
    }
    if (changed) {
        printf("[SDR] main VideoConfig override: res=%s%d mw=%s%d mh=%s%d dpi=%s%d insets=%s(%d,%d,%d,%d)\n",
               o.res ? "" : "(keep)", o.vres, o.mw ? "" : "(keep)", o.vmw, o.mh ? "" : "(keep)", o.vmh,
               o.dpi ? "" : "(keep)", o.vdpi, o.ins ? "" : "(keep)", o.it, o.ib, o.il, o.ir);
        sdr.swap(out);
    }
    return changed;
}


// Touchscreen geometry the phone is told about (channel 7 -> input source (4) -> touchscreen (2) -> height (2)).
// The phone renders a 1280x720 canvas that we squash into the 480-row panel, so by default the touch space is
// declared 1280x720 and the forwarder scales y by 1.5 (NBT_TOUCH_SY). NBT_TOUCH_CFG_H overrides the height.
namespace {
bool patchTouchscreen(const uint8_t* p, size_t n, int h, std::vector<uint8_t>& out) {
    size_t pos = 0;
    while (pos < n) {
        size_t start = pos;
        uint64_t key;
        if (!getVarint(p, n, pos, key)) return false;
        int f = (int)(key >> 3), wt = (int)(key & 7);
        if (wt == 0) {
            uint64_t x;
            if (!getVarint(p, n, pos, x)) return false;
            if (f == 2) { putVarint(out, key); putVarint(out, (uint64_t)h); }
            else out.insert(out.end(), p + start, p + pos);
        } else if (wt == 2) {
            uint64_t l; if (!getVarint(p, n, pos, l) || pos + l > n) return false; pos += (size_t)l;
            out.insert(out.end(), p + start, p + pos);
        } else if (wt == 5) { pos += 4; out.insert(out.end(), p + start, p + pos); }
        else if (wt == 1) { pos += 8; out.insert(out.end(), p + start, p + pos); }
        else return false;
    }
    return true;
}
bool patchInputSource(const uint8_t* p, size_t n, int h, std::vector<uint8_t>& out) {
    size_t pos = 0;
    while (pos < n) {
        size_t start = pos;
        uint64_t key;
        if (!getVarint(p, n, pos, key)) return false;
        int f = (int)(key >> 3), wt = (int)(key & 7);
        if (wt == 0) { uint64_t x; if (!getVarint(p, n, pos, x)) return false; out.insert(out.end(), p + start, p + pos); }
        else if (wt == 2) {
            uint64_t l; if (!getVarint(p, n, pos, l) || pos + l > n) return false;
            const uint8_t* body = p + pos; pos += (size_t)l;
            if (f == 2) {
                std::vector<uint8_t> sub;
                if (!patchTouchscreen(body, (size_t)l, h, sub)) return false;
                putLen(out, 2, sub.empty() ? NULL : &sub[0], sub.size());
            } else out.insert(out.end(), p + start, p + pos);
        }
        else if (wt == 5) { pos += 4; out.insert(out.end(), p + start, p + pos); }
        else if (wt == 1) { pos += 8; out.insert(out.end(), p + start, p + pos); }
        else return false;
    }
    return true;
}
bool patchInputChannel(const uint8_t* p, size_t n, int h, std::vector<uint8_t>& out, bool& is7) {
    is7 = false;
    size_t pos = 0;
    while (pos < n) {
        uint64_t key;
        if (!getVarint(p, n, pos, key)) break;
        int f = (int)(key >> 3), wt = (int)(key & 7);
        if (wt == 0) { uint64_t x; if (!getVarint(p, n, pos, x)) break; if (f == 1 && x == 7) is7 = true; }
        else if (wt == 2) { uint64_t l; if (!getVarint(p, n, pos, l)) break; pos += (size_t)l; }
        else if (wt == 5) pos += 4; else if (wt == 1) pos += 8; else break;
    }
    if (!is7) return false;
    pos = 0;
    while (pos < n) {
        size_t start = pos;
        uint64_t key;
        if (!getVarint(p, n, pos, key)) return false;
        int f = (int)(key >> 3), wt = (int)(key & 7);
        if (wt == 0) { uint64_t x; if (!getVarint(p, n, pos, x)) return false; out.insert(out.end(), p + start, p + pos); }
        else if (wt == 2) {
            uint64_t l; if (!getVarint(p, n, pos, l) || pos + l > n) return false;
            const uint8_t* body = p + pos; pos += (size_t)l;
            if (f == 4) {
                std::vector<uint8_t> sub;
                if (!patchInputSource(body, (size_t)l, h, sub)) return false;
                putLen(out, 4, sub.empty() ? NULL : &sub[0], sub.size());
            } else out.insert(out.end(), p + start, p + pos);
        }
        else if (wt == 5) { pos += 4; out.insert(out.end(), p + start, p + pos); }
        else if (wt == 1) { pos += 8; out.insert(out.end(), p + start, p + pos); }
        else return false;
    }
    return true;
}
} // namespace

bool patchTouchConfig(std::vector<uint8_t>& sdr) {
    int h = 720;
    envInt("NBT_TOUCH_CFG_H", h);
    if (h == 480) return false;
    std::vector<uint8_t> out;
    size_t n = sdr.size(), pos = 0;
    bool changed = false;
    while (pos < n) {
        size_t start = pos;
        uint64_t key;
        if (!getVarint(&sdr[0], n, pos, key)) return false;
        int f = (int)(key >> 3), wt = (int)(key & 7);
        if (wt == 2) {
            uint64_t l;
            if (!getVarint(&sdr[0], n, pos, l) || pos + l > n) return false;
            const uint8_t* body = &sdr[0] + pos; pos += (size_t)l;
            bool is7 = false;
            std::vector<uint8_t> sub;
            if (f == 1 && patchInputChannel(body, (size_t)l, h, sub, is7) && is7) {
                putLen(out, 1, sub.empty() ? NULL : &sub[0], sub.size());
                changed = true;
            } else out.insert(out.end(), &sdr[0] + start, &sdr[0] + pos);
        } else {
            if (wt == 0) { uint64_t x; if (!getVarint(&sdr[0], n, pos, x)) return false; }
            else if (wt == 5) pos += 4; else if (wt == 1) pos += 8; else return false;
            out.insert(out.end(), &sdr[0] + start, &sdr[0] + pos);
        }
    }
    if (changed) {
        printf("[SDR] touchscreen declared 1280x%d\n", h);
        sdr.swap(out);
    }
    return changed;
}

} // namespace nbt
