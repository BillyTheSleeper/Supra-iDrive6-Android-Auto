// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "touch_probe.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

namespace nbt {
namespace {
struct Args { uint16_t port; int seconds; };

void* probeMain(void* p) {
    Args a = *static_cast<Args*>(p);
    delete static_cast<Args*>(p);
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) { printf("[TouchProbe] socket failed: %s\n", strerror(errno)); fflush(stdout); return NULL; }
    int one = 1;
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) != 0)
        printf("[TouchProbe] SO_REUSEADDR failed: %s\n", strerror(errno));
#ifdef SO_REUSEPORT
    if (setsockopt(s, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one)) != 0)
        printf("[TouchProbe] SO_REUSEPORT failed: %s\n", strerror(errno));
#endif
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(a.port);
    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        printf("[TouchProbe] bind UDP %u FAILED: %s (HMI owns it without SO_REUSEADDR?)\n", a.port, strerror(errno));
        fflush(stdout);
        close(s);
        return NULL;
    }
    printf("[TouchProbe] bound UDP %u, listening %d s. Touch the screen now.\n", a.port, a.seconds);
    fflush(stdout);
    struct timeval t0;
    gettimeofday(&t0, NULL);
    int frames = 0;
    for (;;) {
        fd_set rf; FD_ZERO(&rf); FD_SET(s, &rf);
        struct timeval tv; tv.tv_sec = 1; tv.tv_usec = 0;
        int r = select(s + 1, &rf, NULL, NULL, &tv);
        struct timeval now; gettimeofday(&now, NULL);
        if ((long)(now.tv_sec - t0.tv_sec) >= (long)a.seconds) break;
        if (r <= 0) continue;
        unsigned char buf[2048];
        sockaddr_in from; socklen_t fl = sizeof(from);
        ssize_t n = recvfrom(s, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &fl);
        if (n <= 0) continue;
        ++frames;
        if (frames <= 200 || (frames % 100) == 0) {
            char ip[32];
            inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
            printf("[TouchProbe] #%d from %s:%u len=%d:", frames, ip, (unsigned)ntohs(from.sin_port), (int)n);
            for (ssize_t i = 0; i < n && i < 96; ++i) printf(" %02x", buf[i]);
            printf("\n");
            fflush(stdout);
        }
    }
    printf("[TouchProbe] done, %d frames\n", frames);
    fflush(stdout);
    close(s);
    return NULL;
}
}

namespace {
struct FwdArgs { uint16_t port; TouchEventCb cb; void* user; };

double envNum(const char* name, double def) {
    const char* v = getenv(name);
    return (v && *v) ? atof(v) : def;
}

int clampi(double v, int lo, int hi) {
    int i = (int)(v < 0 ? v - 0.5 : v + 0.5);
    return i < lo ? lo : (i > hi ? hi : i);
}

void* forwardMain(void* p) {
    FwdArgs a = *static_cast<FwdArgs*>(p);
    delete static_cast<FwdArgs*>(p);
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) { printf("[TouchFwd] socket failed: %s\n", strerror(errno)); return NULL; }
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#ifdef SO_REUSEPORT
    setsockopt(s, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));
#endif
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(a.port);
    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        printf("[TouchFwd] bind UDP %u FAILED: %s\n", a.port, strerror(errno));
        close(s);
        return NULL;
    }
    const double sx = envNum("NBT_TOUCH_SX", 1.0), sy = envNum("NBT_TOUCH_SY", 1.0);
    const double ox = envNum("NBT_TOUCH_OX", 0.0), oy = envNum("NBT_TOUCH_OY", 0.0);
    const int maxX = (int)envNum("NBT_TOUCH_W", 1280) - 1, maxY = (int)envNum("NBT_TOUCH_H", 720) - 1;
    printf("[TouchFwd] bound UDP %u, forwarding decoded multi-touch (scale %.3f/%.3f offset %.0f/%.0f range 0..%d x 0..%d)\n",
           a.port, sx, sy, ox, oy, maxX, maxY);

    bool active[3] = { false, false, false };
    int cx[3] = { 0, 0, 0 }, cy[3] = { 0, 0, 0 };        // last calibrated position per slot
    int logged = 0, loggedMoves = 0, rawLogged = 0;
    const int idleMs = (int)envNum("NBT_TOUCH_IDLE_MS", 400);   // finger "up" frames can be lost: release after this silence
    struct timeval lastFrame;
    gettimeofday(&lastFrame, NULL);

    for (;;) {
        TouchEventOut evs[10];
        int ne = 0;
        unsigned char buf[2048];

        fd_set rf;
        FD_ZERO(&rf);
        FD_SET(s, &rf);
        struct timeval tv;
        tv.tv_sec = 0; tv.tv_usec = 100000;
        int sr = select(s + 1, &rf, NULL, NULL, &tv);
        if (sr < 0) { if (errno == EINTR) continue; usleep(100000); continue; }

        struct timeval now;
        gettimeofday(&now, NULL);
        if (sr == 0) {
            long gap = (now.tv_sec - lastFrame.tv_sec) * 1000 + (now.tv_usec - lastFrame.tv_usec) / 1000;
            int cnt = 0;
            for (int j = 0; j < 3; ++j) if (active[j]) ++cnt;
            if (cnt && gap >= idleMs) {
                printf("[TouchFwd] no touch frames for %ld ms: releasing %d stuck finger(s)\n", gap, cnt);
                for (int k = 0; k < 3 && ne < 10; ++k) if (active[k]) {
                    int left = 0;
                    for (int j = 0; j < 3; ++j) if (active[j]) ++left;
                    TouchEventOut& e = evs[ne++];
                    e.n = 0; e.index = 0;
                    for (int j = 0; j < 3; ++j) if (active[j]) {
                        if (j == k) e.index = e.n;
                        e.p[e.n].id = j; e.p[e.n].x = cx[j]; e.p[e.n].y = cy[j]; ++e.n;
                    }
                    e.action = (left == 1) ? 1 : 6;
                    active[k] = false;
                }
            }
        } else {
            ssize_t n = recvfrom(s, buf, sizeof(buf), 0, NULL, NULL);
            if (n < 0) { if (errno == EINTR) continue; usleep(100000); continue; }
            if (n < 64) continue;
            if (buf[0] != 0xB0 || buf[1] != 0x04 || buf[2] != 0x80 || buf[3] != 0x02) continue;
            lastFrame = now;

            int state[3] = { 0, 0, 0 }, nx[3] = { 0, 0, 0 }, ny[3] = { 0, 0, 0 };
            int nslots = 0;
            for (int k = 0; k < 3; ++k) {
                int bx = 52 + 16 * k, by = 60 + 16 * k;
                if (by + 3 >= n) break;
                state[k] = (buf[by + 3] >> 4) & 0x0F;
                if (state[k] > 3) state[k] = 0;
                int rawx = (buf[bx + 2] << 8) | buf[bx + 3];
                int rawy = (buf[by] << 8) | buf[by + 1];
                nx[k] = clampi(rawx * sx + ox, 0, maxX);
                ny[k] = clampi(rawy * sy + oy, 0, maxY);
                ++nslots;
            }
            // Raw frame evidence for the multi-finger layout: any data word of entries 6..11 non-zero.
            {
                bool extra = false;
                for (int off = 68; off + 3 < n && off < 112; off += 8)
                    if (buf[off] | buf[off + 1] | buf[off + 2] | buf[off + 3]) extra = true;
                if (extra && rawLogged < 60) {
                    ++rawLogged;
                    printf("[TouchFwd] MULTI frame (%d B):", (int)n);
                    for (ssize_t i = 0; i < n && i < 112; ++i) printf(" %02x", buf[i]);
                    printf("\n");
                }
            }

            // A slot that is active but reported as state 0 has been lifted; a "down" on an active slot means we
            // missed its "up": release it first.
            for (int k = 0; k < nslots && ne < 10; ++k) {
                if (active[k] && (state[k] == 0 || state[k] == 1)) {
                    int cnt = 0;
                    for (int j = 0; j < 3; ++j) if (active[j]) ++cnt;
                    TouchEventOut& e = evs[ne++];
                    e.n = 0; e.index = 0;
                    for (int j = 0; j < 3; ++j) if (active[j]) {
                        if (j == k) e.index = e.n;
                        e.p[e.n].id = j; e.p[e.n].x = cx[j]; e.p[e.n].y = cy[j]; ++e.n;
                    }
                    e.action = (cnt == 1) ? 1 : 6;
                    active[k] = false;
                    printf("[TouchFwd] slot %d: %s -> synthesized release\n", k, state[k] == 0 ? "state 0" : "new down while still active");
                }
            }
            // downs, then moves, then ups
            for (int k = 0; k < nslots && ne < 10; ++k) {
                if (state[k] == 1 || (state[k] == 2 && !active[k])) {
                    int before = 0;
                    for (int j = 0; j < 3; ++j) if (active[j]) ++before;
                    active[k] = true; cx[k] = nx[k]; cy[k] = ny[k];
                    TouchEventOut& e = evs[ne++];
                    e.n = 0; e.index = 0;
                    for (int j = 0; j < 3; ++j) if (active[j]) {
                        if (j == k) e.index = e.n;
                        e.p[e.n].id = j; e.p[e.n].x = cx[j]; e.p[e.n].y = cy[j]; ++e.n;
                    }
                    e.action = before == 0 ? 0 : 5;
                }
            }
            bool moved = false;
            for (int k = 0; k < nslots; ++k)
                if (state[k] == 2 && active[k] && (nx[k] != cx[k] || ny[k] != cy[k])) { cx[k] = nx[k]; cy[k] = ny[k]; moved = true; }
            if (moved && ne < 10) {
                TouchEventOut& e = evs[ne++];
                e.n = 0; e.index = 0; e.action = 2;
                for (int j = 0; j < 3; ++j) if (active[j]) { e.p[e.n].id = j; e.p[e.n].x = cx[j]; e.p[e.n].y = cy[j]; ++e.n; }
            }
            for (int k = 0; k < nslots && ne < 10; ++k) {
                if (state[k] == 3 && active[k]) {
                    int cnt = 0;
                    for (int j = 0; j < 3; ++j) if (active[j]) ++cnt;
                    TouchEventOut& e = evs[ne++];
                    e.n = 0; e.index = 0;
                    for (int j = 0; j < 3; ++j) if (active[j]) {
                        if (j == k) e.index = e.n;
                        e.p[e.n].id = j; e.p[e.n].x = (j == k ? nx[k] : cx[j]); e.p[e.n].y = (j == k ? ny[k] : cy[j]); ++e.n;
                    }
                    e.action = (cnt == 1) ? 1 : 6;
                    active[k] = false;
                }
            }
        }
        for (int i = 0; i < ne; ++i) {
            bool isMove = (evs[i].action == 2);
            if (isMove ? (loggedMoves < 40) : (logged < 600)) {
                if (isMove) ++loggedMoves; else ++logged;
                printf("[TouchFwd] action=%d idx=%d n=%d:", evs[i].action, evs[i].index, evs[i].n);
                for (int j = 0; j < evs[i].n; ++j) printf(" (%d: %d,%d)", evs[i].p[j].id, evs[i].p[j].x, evs[i].p[j].y);
                printf("\n");
            }
            if (a.cb) a.cb(evs[i], a.user);
        }
    }
    return NULL;
}
}

bool startTouchForward(uint16_t port, TouchEventCb cb, void* user) {
    FwdArgs* a = new FwdArgs;
    a->port = port; a->cb = cb; a->user = user;
    pthread_attr_t at; pthread_attr_init(&at);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    pthread_t t;
    bool ok = pthread_create(&t, &at, &forwardMain, a) == 0;
    pthread_attr_destroy(&at);
    if (!ok) delete a;
    return ok;
}

bool startTouchProbe(uint16_t port, int seconds) {
    Args* a = new Args;
    a->port = port; a->seconds = seconds;
    pthread_attr_t at; pthread_attr_init(&at);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    pthread_t t;
    bool ok = pthread_create(&t, &at, &probeMain, a) == 0;
    pthread_attr_destroy(&at);
    if (!ok) delete a;
    return ok;
}
}
