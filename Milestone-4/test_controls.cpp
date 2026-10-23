// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// test_controls: stand-alone, observe-only tool that shows what the car's iDrive controller sends
// (rotary knob, joystick tilts, hard keys, touch coordinates) over the MOST subsystem, without
// the Android Auto daemon. Like test_display it is a separate binary: nothing here touches AA.
//
//   ./test_controls [-t seconds] [-k E510,E503,...] [-r] > /tmp/c.log
//     -t N      run for N seconds (default 180)
//     -k LIST   FBlock keys (hex, FBlock<<8|Inst) to register as "device controllers"
//               (default: E510,E503,E505,E512,E514,E501,E502,1502)
//     -r        raw: also print the full payload of every DSI event
//
// Protocol (RE of libimp_sys_most_base_nbt): DSI interface MOSTSubSystem.DSYSMOSTServer 4.5
//   1. request 0xF  registerMOSTClientExt(name)         -> response 0x80000000 {clientId, deviceId}
//   2. setNotification: 0xC0000000..3 and information ids 0x80000001..7 (as the HMI does)
//   3. request 0x1  registerDevCtrls(clientId, keys[])
//   4. incoming MOST messages arrive as information 0x80000001 {MessageType}
// Everything is logged; nothing is written to the controller. If your knob produces nothing, the log
// still shows whether registration worked (client id) and every raw event the server sends.

#include "dsi_client.hpp"
#include "most_msg.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <vector>
#include <map>

using namespace nbt;

namespace {

long g_t0 = 0;
bool g_raw = false;
std::vector<uint16_t> g_keys;
uint32_t g_client = 0xffffffffu;
volatile int g_stage = 0;           // 0 init, 1 name sent, 2 devctrls sent
unsigned g_events = 0, g_msgs = 0;
std::map<unsigned, unsigned> g_fktCount;
int g_lastRotary = -1;
unsigned char g_lastPush[2][16];    // [0] = fkt C47, [1] = fkt C4F (same state, different length)
bool g_havePush[2] = { false, false };

long nowMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)(ts.tv_sec * 1000L + ts.tv_nsec / 1000000L);
}

void hex(const char* tag, const uint8_t* p, size_t n, size_t max) {
    printf("%s", tag);
    for (size_t i = 0; p && i < n && i < max; ++i) printf(" %02x", p[i]);
    if (n > max) printf(" ...(%u B)", (unsigned)n);
    printf("\n");
}

const char* opName(unsigned op) {
    switch (op) {
        case 0x0: return "Set"; case 0x1: return "Get"; case 0x2: return "SetGet";
        case 0x3: return "Increment"; case 0x4: return "Decrement"; case 0x5: return "GetInterface";
        case 0x6: return "StartResult"; case 0x7: return "Abort"; case 0x8: return "StartResultAck";
        case 0x9: return "AbortAck"; case 0xA: return "StartAck"; case 0xC: return "Status";
        case 0xD: return "Interface"; case 0xE: return "ProcessingAck"; case 0xF: return "Error";
        default: return "?";
    }
}

void decodeCcs(const MostMsg& m) {
    const std::vector<uint8_t>& d = m.data;
    if (m.fblock != 0xE5) return;
    if (m.fkt == 0xC46 && d.size() >= 3) {
        // CRotaryEncoderState: byte0, then BE16 absolute step counter (bytes 1-2), then small bytes
        int pos = (d[1] << 8) | d[2];
        if (g_lastRotary >= 0 && pos != g_lastRotary) {
            int diff = pos - g_lastRotary;
            printf("   ROTARY pos=%d delta=%d -> %s x%d\n", pos, diff, diff > 0 ? "CLOCKWISE (HMI key 0x0D)" : "COUNTER-CLOCKWISE (HMI key 0x0E)", diff > 0 ? diff : -diff);
        } else {
            printf("   ROTARY pos=%d (%s)\n", pos, g_lastRotary < 0 ? "first value" : "unchanged");
        }
        g_lastRotary = pos;
    } else if ((m.fkt == 0xC47 || m.fkt == 0xC4F) && !d.empty()) {
        // PushButtonsState: one byte per slot, 0 = released, 1 = pressed, 2 = held
        printf("   PUSHBUTTONS(%03X) slots:", m.fkt);
        for (size_t i = 0; i < d.size(); ++i) printf(" [%u]=%u", (unsigned)i, d[i]);
        const int w = (m.fkt == 0xC4F) ? 1 : 0;
        if (g_havePush[w]) {
            for (size_t i = 0; i < d.size() && i < sizeof(g_lastPush[w]); ++i)
                if (d[i] != g_lastPush[w][i]) printf("  <- slot %u changed %u->%u", (unsigned)i, g_lastPush[w][i], d[i]);
        }
        printf("\n");
        memset(g_lastPush[w], 0, sizeof(g_lastPush[w]));
        for (size_t i = 0; i < d.size() && i < sizeof(g_lastPush[w]); ++i) g_lastPush[w][i] = d[i];
        g_havePush[w] = true;
    } else if (m.fkt == 0xC44 && d.size() >= 3) {
        // AbsoluteCoordinates: 4-bit state, then two 12-bit coordinates (big endian packing)
        unsigned st = d[0] >> 4;
        unsigned x = ((d[0] & 0x0F) << 8) | d[1];
        unsigned y = (d.size() > 2) ? (unsigned)(d[2] << 4) | (d.size() > 3 ? d[3] >> 4 : 0) : 0;
        printf("   TOUCHPAD? state=%u x12=%u y12=%u (layout guess)\n", st, x, y);
    }
}

void onMessage(const uint8_t* pl, uint32_t len) {
    MostMsg m;
    if (!parseMostMessage(pl, len, m)) {
        printf("[%6ld] MOST message: could not parse (%u B)\n", nowMs() - g_t0, len);
        hex("   raw:", pl, len, 96);
        return;
    }
    ++g_msgs;
    g_fktCount[(unsigned)((m.fblock << 16) | m.fkt)]++;
    printf("[%6ld] MOST tx=%u var=%u/%u dst=%04X src=%04X FB=%02X inst=%02X fkt=%03X op=%s(%X) hdrTag=%u len=%u:",
           nowMs() - g_t0, m.txType, m.varTag, m.varVal, m.dst, m.src, m.fblock, m.inst, m.fkt,
           opName(m.opType), m.opType, m.hdrTag, (unsigned)m.data.size());
    for (size_t i = 0; i < m.data.size() && i < 24; ++i) printf(" %02x", m.data[i]);
    printf("\n");
    decodeCcs(m);
}

void onAttached(DsiClient* c, void*) {
    printf("[%6ld] attached to MOSTSubSystem.DSYSMOSTServer: registering client\n", nowMs() - g_t0);
    std::vector<uint8_t> p;
    DsiClient::putString(p, "NBTAAD_CTL");
    g_stage = 1;
    c->sendRequest(0xF, p);
    // Same subscriptions the HMI's MOST client makes in CHBMOSTClientImpl::componentConnected:
    // notifyOn(0xC0000002), notifyOn(0xC0000003), notifyOn(0xC0000001 = registry), and listeners for the
    // information ids 0x80000001..7 (informationProcessMessage, ChangeInstID, Connect, ...).
    // Without these the server never sends us the MOST messages (first run: only the 0xC0000000 state arrived).
    static const uint32_t kAttrs[] = { 0xC0000002u, 0xC0000003u, 0xC0000001u, 0x80000001u, 0x80000002u,
                                       0x80000003u, 0x80000004u, 0x80000005u, 0x80000006u, 0x80000007u };
    for (size_t i = 0; i < sizeof(kAttrs) / sizeof(kAttrs[0]); ++i) c->setNotification(kAttrs[i]);
}

void onData(DsiClient* c, uint32_t type, uint32_t id, uint32_t seq, const uint8_t* pl, uint32_t len, void*) {
    ++g_events;
    if (g_raw) {
        char tag[96];
        snprintf(tag, sizeof(tag), "[%6ld] DSI type=0x%x id=0x%x seq=%u len=%u raw:", nowMs() - g_t0, type, id, seq, len);
        hex(tag, pl, len, 256);
    }
    if (id == 0x80000000u && len >= 6) {
        g_client = mostRdU32(pl);
        unsigned dev = mostRdU16(pl + 4);
        printf("[%6ld] registered: clientId=%u deviceId=0x%04X%s\n", nowMs() - g_t0, g_client, dev,
               g_client == 0xffffffffu ? "  (REGISTRATION REFUSED)" : "");
        if (g_client != 0xffffffffu && g_stage < 2) {
            c->setNotification(0xC0000000u);
            std::vector<uint8_t> p = buildRegisterDevCtrls(g_client, g_keys);
            printf("[%6ld] registerDevCtrls with %u keys:", nowMs() - g_t0, (unsigned)g_keys.size());
            for (size_t i = 0; i < g_keys.size(); ++i) printf(" %04X", g_keys[i]);
            printf("\n");
            g_stage = 2;
            c->sendRequest(1, p);
        }
    } else if (id == 0x80000001u) {
        onMessage(pl, len);
    } else if (!g_raw) {
        char tag[96];
        snprintf(tag, sizeof(tag), "[%6ld] DSI type=0x%x id=0x%x seq=%u len=%u:", nowMs() - g_t0, type, id, seq, len);
        hex(tag, pl, len, 96);
    }
}

void onSig(int) {
    printf("\n[interrupted]\n");
    fflush(stdout);
    _exit(0);
}

} // namespace

int main(int argc, char** argv) {
    int secs = 180;
    const char* keyArg = "E510,E503,E505,E512,E514,E501,E502,1502";
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-t") && i + 1 < argc) secs = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-k") && i + 1 < argc) keyArg = argv[++i];
        else if (!strcmp(argv[i], "-r")) g_raw = true;
        else { printf("usage: %s [-t seconds] [-k HEX,HEX,...] [-r]\n", argv[0]); return 1; }
    }
    char buf[256];
    strncpy(buf, keyArg, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    for (char* tok = strtok(buf, ","); tok; tok = strtok(NULL, ","))
        g_keys.push_back((uint16_t)strtoul(tok, NULL, 16));

    setvbuf(stdout, NULL, _IOLBF, 0);
    signal(SIGINT, onSig);
    signal(SIGTERM, onSig);
    g_t0 = nowMs();
    printf("test_controls: observe-only MOST controller monitor, %d s. Turn the knob, push it, tilt it, press hard keys.\n", secs);

    DsiClient cli;
    cli.setCallbacks(&onAttached, &onData, NULL);
    if (!cli.open("DSYSMOSTServer", "MOSTSubSystem", 4, 5)) {
        printf("DSI open FAILED\n");
        return 2;
    }
    for (int s = 0; s < secs; ++s) {
        sleep(1);
        if (s == 10 && g_stage == 0) printf("[%6ld] WARNING: still not attached after 10 s (is the most process running?)\n", nowMs() - g_t0);
        if (s == 15 && g_stage == 1) printf("[%6ld] WARNING: no registration response after 15 s\n", nowMs() - g_t0);
    }
    printf("\nsummary: %u DSI events, %u MOST messages, client id %d\n", g_events, g_msgs, (int)g_client);
    for (std::map<unsigned, unsigned>::iterator it = g_fktCount.begin(); it != g_fktCount.end(); ++it)
        printf("   FBlock %02X fkt %03X : %u messages\n", it->first >> 16, it->first & 0xFFFF, it->second);
    fflush(stdout);
    _exit(0);   // the DSI connection thread is detached and blocks in the library
}
