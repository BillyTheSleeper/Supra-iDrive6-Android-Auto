// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: replays the E5 controller messages of the 2026-10-22 car run through CcsDecoder.
// g++ -std=gnu++98 -Wall -Wextra -Iinclude -Isrc -o /tmp/test_ccs tests/test_ccs_decoder.cpp && /tmp/test_ccs tests/fixtures/controls_2026-10-22_e5.txt
#include "ccs_decoder.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <string>
#include <vector>
using namespace nbt;

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "tests/fixtures/controls_2026-10-22_e5.txt";
    FILE* f = fopen(path, "r");
    if (!f) { printf("cannot open %s\n", path); return 1; }
    CcsDecoder dec;
    std::string seq;
    int cw = 0, ccw = 0;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        unsigned inst, fkt, op;
        int off = 0;
        if (sscanf(line, "%x %x %x%n", &inst, &fkt, &op, &off) != 3) continue;
        std::vector<uint8_t> d;
        const char* p = line + off;
        unsigned b; int k;
        while (sscanf(p, " %2x%n", &b, &k) == 1) { d.push_back((uint8_t)b); p += k; }
        CcsEvent ev[16];
        int n = dec.feed(0xE5, (uint8_t)inst, (uint16_t)fkt, (uint8_t)op, d.empty() ? NULL : &d[0], d.size(), ev, 16);
        for (int i = 0; i < n; ++i) {
            if (ev[i].kind == 2) {
                assert(ev[i].keycode == AAK_ROTARY_CONTROLLER);
                if (ev[i].delta > 0) cw += ev[i].delta; else ccw -= ev[i].delta;
                char c = ev[i].delta > 0 ? '+' : '-';
                if (seq.empty() || seq[seq.size() - 1] != c) seq += c;
            } else if (ev[i].down) {
                if (!seq.empty()) seq += ' ';
                seq += ev[i].what;
                char kc[16]; snprintf(kc, sizeof(kc), "=%u", ev[i].keycode); seq += kc;
            }
        }
    }
    fclose(f);
    printf("rotary: %d clockwise, %d counter-clockwise\nsequence: %s\n", cw, ccw, seq.c_str());
    // Car run: 7 detents CW, 12 CCW, tilts up/down/right/left (with a few stray detents while tilting),
    // then media, com, menu, map, nav, back, options.
    assert(seq.find("+-") == 0);
    const char* order[] = { "TILT_UP=19", "TILT_DOWN=20", "TILT_RIGHT=22", "TILT_LEFT=21",
                            "MEDIA=65537", "COM=5", "MENU=3", "MAP=65538", "NAV=65538", "BACK=4", "OPTIONS=0" };
    size_t at = 0;
    for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); ++i) {
        size_t f2 = seq.find(order[i], at);
        if (f2 == std::string::npos) { printf("MISSING in order: %s\n", order[i]); return 1; }
        at = f2 + strlen(order[i]);
    }
    // wrap-around and inversion
    CcsDecoder w; CcsEvent e[4];
    const uint8_t a[8] = { 1, 0xff, 0xff, 0, 0, 0, 0, 0xff }, c[8] = { 2, 0x00, 0x01, 0, 0, 0, 0, 0xff };
    assert(w.feed(0xE5, 3, 0xC46, 0xC, a, 8, e, 4) == 0);
    assert(w.feed(0xE5, 3, 0xC46, 0xC, c, 8, e, 4) == 1 && e[0].delta == 2);
    assert(w.feed(0xE5, 5, 0xC46, 0xC, a, 8, e, 4) == 0);   // other instance ignored
    w.setInvert(true);
    assert(w.feed(0xE5, 3, 0xC46, 0xC, a, 8, e, 4) == 1 && e[0].delta == 2);
    printf("test_ccs_decoder OK\n");
    return 0;
}
