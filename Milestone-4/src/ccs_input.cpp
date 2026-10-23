// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "ccs_input.hpp"
#include "dsi_client.hpp"
#include "most_msg.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vector>

namespace nbt {

namespace {

DsiClient g_most;
CcsDecoder g_dec;
CcsEventCb g_cb = NULL;
void* g_user = NULL;
uint32_t g_client = 0xffffffffu;
int g_stage = 0;           // 0 init, 1 name sent, 2 devctrls sent (connection thread only)
unsigned g_msgs = 0, g_ccs = 0;

void onAttached(DsiClient* c, void*) {
    printf("[Knob] attached to MOSTSubSystem.DSYSMOSTServer: registering MOST client\n");
    std::vector<uint8_t> p;
    DsiClient::putString(p, "NBTAAD_KNOB");
    g_stage = 1;
    c->sendRequest(0xF, p);
    // Same subscriptions as the HMI's CHBMOSTClientImpl::componentConnected (car-verified in test_controls v2).
    static const uint32_t kAttrs[] = { 0xC0000002u, 0xC0000003u, 0xC0000001u, 0x80000001u, 0x80000002u,
                                       0x80000003u, 0x80000004u, 0x80000005u, 0x80000006u, 0x80000007u };
    for (size_t i = 0; i < sizeof(kAttrs) / sizeof(kAttrs[0]); ++i) c->setNotification(kAttrs[i]);
}

void onData(DsiClient* c, uint32_t, uint32_t id, uint32_t, const uint8_t* pl, uint32_t len, void*) {
    if (id == 0x80000000u && len >= 6) {
        g_client = mostRdU32(pl);
        unsigned dev = mostRdU16(pl + 4);
        printf("[Knob] registered: clientId=%u deviceId=0x%04X%s\n", g_client, dev,
               g_client == 0xffffffffu ? "  (REGISTRATION REFUSED)" : "");
        if (g_client != 0xffffffffu && g_stage < 2) {
            c->setNotification(0xC0000000u);
            static const uint16_t kKeys[] = { 0xE510, 0xE503, 0xE505, 0xE512, 0xE514, 0xE501, 0xE502, 0x1502 };
            std::vector<uint16_t> keys(kKeys, kKeys + sizeof(kKeys) / sizeof(kKeys[0]));
            g_stage = 2;
            c->sendRequest(1, buildRegisterDevCtrls(g_client, keys));
            printf("[Knob] registerDevCtrls sent (8 keys): waiting for controller events\n");
        }
        return;
    }
    if (id != 0x80000001u) return;
    MostMsg m;
    if (!parseMostMessage(pl, len, m)) return;
    ++g_msgs;
    if (m.fblock != 0xE5) return;
    if (m.fkt == 0xC46 || m.fkt == 0xC47 || m.fkt == 0xC4F) {
        ++g_ccs;
        if (g_ccs <= 3 || (m.fkt == 0xC46 && m.data.size() >= 7 && (m.data[3] || m.data[5] || m.data[6]))) {
            printf("[Knob] E5/%02X fkt=%03X op=%X len=%u:", m.inst, m.fkt, m.opType, (unsigned)m.data.size());
            for (size_t i = 0; i < m.data.size() && i < 16; ++i) printf(" %02x", m.data[i]);
            printf("\n");
        }
    }
    CcsEvent ev[16];
    int n = g_dec.feed(m.fblock, m.inst, m.fkt, m.opType, m.data.empty() ? NULL : &m.data[0], m.data.size(), ev, 16);
    for (int i = 0; i < n; ++i) {
        if (ev[i].kind == 2)
            printf("[Knob] %s delta=%d -> AA relative key %u\n", ev[i].what, ev[i].delta, ev[i].keycode);
        else
            printf("[Knob] %s %s -> AA key %u%s\n", ev[i].what, ev[i].down ? "down" : "up", ev[i].keycode,
                   ev[i].keycode ? "" : " (unmapped, not sent)");
        if (g_cb && (ev[i].kind == 2 || ev[i].keycode)) g_cb(ev[i], g_user);
    }
}

} // namespace

bool startCcsInput(CcsEventCb cb, void* user) {
    g_cb = cb;
    g_user = user;
    const char* e = getenv("NBT_KNOB_INST");
    if (e && *e) g_dec.setInstance((uint8_t)strtoul(e, NULL, 16));
    if (getenv("NBT_KNOB_INVERT")) g_dec.setInvert(true);
    for (int s = 0; s < CCS_SLOTS; ++s) {
        char name[32];
        snprintf(name, sizeof(name), "NBT_KEY_%s", CcsDecoder::slotName(s));
        const char* v = getenv(name);
        if (v && *v) g_dec.setSlotKey(s, (uint32_t)strtoul(v, NULL, 0));
    }
    printf("[Knob] iDrive controller input: instance %s, rotary%s, keys:",
           (e && *e) ? e : "03", getenv("NBT_KNOB_INVERT") ? " inverted" : "");
    for (int s = 0; s < CCS_SLOTS; ++s) printf(" %s=%u", CcsDecoder::slotName(s), g_dec.slotKey(s));
    printf("\n");
    g_most.setQuiet(getenv("NBT_KNOB_RAW") == NULL);
    g_most.setCallbacks(&onAttached, &onData, NULL);
    if (!g_most.open("DSYSMOSTServer", "MOSTSubSystem", 4, 5)) {
        printf("[Knob] DSI open FAILED: knob/keys disabled\n");
        return false;
    }
    return true;
}

} // namespace nbt
