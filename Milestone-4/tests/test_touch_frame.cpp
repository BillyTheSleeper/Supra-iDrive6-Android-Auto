// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: decodes the real two-finger pinch frames captured on the car (w9.log, 2026-10-23).
// g++ -std=gnu++98 -Wall -Wextra -Iinclude -Isrc -o /tmp/test_touch_frame tests/test_touch_frame.cpp
// /tmp/test_touch_frame tests/fixtures/touch_multi_2026-10-23_pinch.txt
#include "touch_frame.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
using namespace nbt;

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "tests/fixtures/touch_multi_2026-10-23_pinch.txt";
    FILE* f = fopen(path, "r");
    if (!f) { printf("cannot open %s\n", path); return 1; }
    char line[1024];
    int frames = 0, firstX0 = -1, firstX1 = -1, lastX0 = -1, lastX1 = -1;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        unsigned char buf[128];
        size_t n = 0;
        const char* p = line;
        unsigned b; int k;
        while (n < sizeof(buf) && sscanf(p, " %2x%n", &b, &k) == 1) { buf[n++] = (unsigned char)b; p += k; }
        assert(n == 112);
        TouchContact c[kTouchMaxContacts];
        int declared = 0;
        int nc = parseTouchFrame(buf, n, c, kTouchMaxContacts, &declared);
        assert(nc == 2 && declared == 2);
        assert(c[0].id == 0 && c[1].id == 1);
        for (int i = 0; i < 2; ++i) {
            assert(c[i].state >= 1 && c[i].state <= 3);
            assert(c[i].x >= 0 && c[i].x < 1280 && c[i].y >= 0 && c[i].y < 480);
        }
        if (frames == 0) {
            assert(c[0].state == 1 && c[1].state == 1);           // both fingers land in the first frame
            firstX0 = c[0].x; firstX1 = c[1].x;
        }
        lastX0 = c[0].x; lastX1 = c[1].x;
        ++frames;
    }
    fclose(f);
    printf("frames=%d finger0 x %d->%d finger1 x %d->%d\n", frames, firstX0, lastX0, firstX1, lastX1);
    assert(frames == 60);
    assert(firstX0 == 959 && firstX1 == 683);
    assert((lastX0 - lastX1) < (firstX0 - firstX1));             // pinch: fingers moved together
    // round trip through the builder
    TouchContact in[3] = { {0, 1, 100, 200, 50}, {1, 2, 1279, 479, 60}, {2, 3, 640, 0, 70} };
    unsigned char fr[112];
    buildTouchFrame(fr, in, 3, 0x80);
    TouchContact out[kTouchMaxContacts];
    int d = 0;
    assert(parseTouchFrame(fr, sizeof(fr), out, kTouchMaxContacts, &d) == 3 && d == 3);
    for (int i = 0; i < 3; ++i)
        assert(out[i].id == in[i].id && out[i].state == in[i].state && out[i].x == in[i].x && out[i].y == in[i].y && out[i].size == in[i].size);
    // wrong ident -> -1
    fr[0] = 0; assert(parseTouchFrame(fr, sizeof(fr), out, kTouchMaxContacts, &d) == -1);
    printf("test_touch_frame OK\n");
    return 0;
}
