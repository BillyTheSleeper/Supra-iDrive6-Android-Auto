// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "touch_probe.hpp"
#include "touch_frame.hpp"
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

// e = every active finger; e.index = position of finger 'which' (or 0).
void fillActive(TouchEventOut& e, const bool* active, const int* cx, const int* cy, int ns, int which) {
    e.n = 0; e.index = 0; e.action = 2;
    for (int j = 0; j < ns && e.n < kTouchMaxContacts; ++j) if (active[j]) {
        if (j == which) e.index = e.n;
        e.p[e.n].id = j; e.p[e.n].x = cx[j]; e.p[e.n].y = cy[j]; ++e.n;
    }
}

// RELEASE (last finger) or POINTER_UP (others stay) for finger k at (x,y); clears active[k].
void emitUp(TouchEventOut& e, bool* active, int* cx, int* cy, int ns, int k, int x, int y) {
    cx[k] = x; cy[k] = y;
    int cnt = 0;
    for (int j = 0; j < ns; ++j) if (active[j]) ++cnt;
    fillActive(e, active, cx, cy, ns, k);
    e.action = (cnt <= 1) ? 1 : 6;
    active[k] = false;
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

    // Slots are indexed by the HMI's finger id (0..kTouchMaxContacts-1); the id is also the AA pointer_id.
    enum { NS = kTouchMaxContacts, MAXEV = 16 };
    bool active[NS];
    int cx[NS], cy[NS];        // last calibrated position per finger id
    for (int j = 0; j < NS; ++j) { active[j] = false; cx[j] = cy[j] = 0; }
    int logged = 0, loggedMoves = 0, rawLogged = 0, multiFrames = 0;
    const int idleMs = (int)envNum("NBT_TOUCH_IDLE_MS", 400);   // finger "up" frames can be lost: release after this silence
    struct timeval lastFrame;
    gettimeofday(&lastFrame, NULL);

    for (;;) {
        TouchEventOut evs[MAXEV];
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
            for (int j = 0; j < NS; ++j) if (active[j]) ++cnt;
            if (cnt && gap >= idleMs) {
                printf("[TouchFwd] no touch frames for %ld ms: releasing %d stuck finger(s)\n", gap, cnt);
                for (int k = 0; k < NS && ne < MAXEV; ++k)
                    if (active[k]) emitUp(evs[ne++], active, cx, cy, NS, k, cx[k], cy[k]);
            }
        } else {
            ssize_t n = recvfrom(s, buf, sizeof(buf), 0, NULL, NULL);
            if (n < 0) { if (errno == EINTR) continue; usleep(100000); continue; }
            TouchContact c[kTouchMaxContacts];
            int declared = 0;
            const int nc = parseTouchFrame(buf, (size_t)n, c, kTouchMaxContacts, &declared);
            if (nc < 0) continue;
            lastFrame = now;

            if (declared >= 2) {
                ++multiFrames;
                if (rawLogged < 20) {
                    ++rawLogged;
                    printf("[TouchFwd] MULTI frame (%d contacts):", declared);
                    for (int k = 0; k < nc; ++k) printf(" [id %d st %d %d,%d sz %d]", c[k].id, c[k].state, c[k].x, c[k].y, c[k].size);
                    printf("\n");
                } else if ((multiFrames % 200) == 0) {
                    printf("[TouchFwd] %d multi-finger frames so far\n", multiFrames);
                }
            }

            int nx[NS], ny[NS], st[NS];
            bool seen[NS];
            for (int j = 0; j < NS; ++j) { st[j] = 0; seen[j] = false; nx[j] = cx[j]; ny[j] = cy[j]; }
            for (int k = 0; k < nc; ++k) {
                const int id = c[k].id;
                seen[id] = true;
                st[id] = c[k].state;
                nx[id] = clampi(c[k].x * sx + ox, 0, maxX);
                ny[id] = clampi(c[k].y * sy + oy, 0, maxY);
            }

            // A finger that is active but no longer listed has been lifted; a "down" on an active finger means we
            // missed its "up": release it first.
            for (int k = 0; k < NS && ne < MAXEV; ++k) {
                if (active[k] && (!seen[k] || st[k] == 1)) {
                    emitUp(evs[ne++], active, cx, cy, NS, k, cx[k], cy[k]);
                    printf("[TouchFwd] finger %d: %s -> synthesized release\n", k, !seen[k] ? "gone from frame" : "new down while still active");
                }
            }
            // downs, then one move for all fingers, then ups
            for (int k = 0; k < NS && ne < MAXEV; ++k) {
                if (seen[k] && (st[k] == 1 || (st[k] == 2 && !active[k]))) {
                    int before = 0;
                    for (int j = 0; j < NS; ++j) if (active[j]) ++before;
                    active[k] = true; cx[k] = nx[k]; cy[k] = ny[k];
                    TouchEventOut& e = evs[ne++];
                    fillActive(e, active, cx, cy, NS, k);
                    e.action = before == 0 ? 0 : 5;     // PRESS / POINTER_DOWN
                }
            }
            bool moved = false;
            for (int k = 0; k < NS; ++k)
                if (seen[k] && st[k] == 2 && active[k] && (nx[k] != cx[k] || ny[k] != cy[k])) { cx[k] = nx[k]; cy[k] = ny[k]; moved = true; }
            if (moved && ne < MAXEV) {
                TouchEventOut& e = evs[ne++];
                fillActive(e, active, cx, cy, NS, -1);
                e.action = 2;                            // DRAG (all pointers)
            }
            for (int k = 0; k < NS && ne < MAXEV; ++k)
                if (seen[k] && st[k] == 3 && active[k]) emitUp(evs[ne++], active, cx, cy, NS, k, nx[k], ny[k]);
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
