// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "bluetooth_manager.hpp"
#include "dsi_client.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

namespace nbt {

namespace {
DsiClient g_gap;

// GapErrorCode values (libbssservice.so enum order): 0 NONE, 1 TIMEOUT, 2 BUSY,
// 3 STATE, 4 REFUSED, 5 DEFAULT, 6 INVALID_PARAM. DEFAULT is what the stack
// returns for "service name/uuid already exists", e.g. a registration left
// behind by an earlier nbt-aad run (we never deregister at shutdown).
const char* gapErrorName(uint32_t e) {
    switch (e) {
    case 0: return "OK";
    case 1: return "TIMEOUT";
    case 2: return "BUSY";
    case 3: return "STATE";
    case 4: return "REFUSED";
    case 5: return "DEFAULT (name/uuid already registered?)";
    case 6: return "INVALID_PARAM";
    default: return "FAILED";
    }
}

enum SppState { SPP_IDLE, SPP_REG_SENT, SPP_DEREG_SENT, SPP_REREG_SENT, SPP_DONE };
SppState g_spp = SPP_IDLE;

void sendRegister(DsiClient* c) {
    const char* uuidCon = getenv("NBT_SPP_UUID_CON");
    if (!uuidCon || !*uuidCon) uuidCon = AA_BT_UUID;   // connect UUID: same as service UUID (INFERRED)
    std::vector<uint8_t> p;
    DsiClient::putString(p, AA_SPP_NAME);
    DsiClient::putString(p, AA_BT_UUID);
    DsiClient::putString(p, uuidCon);
    DsiClient::putU32(p, 0);   // type 0 = DevA (inferred)
    printf("[BluetoothManager] SppUuidRegister(name='%s', uuid=%s, con=%s, type=0)\n",
           AA_SPP_NAME, AA_BT_UUID, uuidCon);
    c->sendRequest(DSI_REQ_SPP_UUID_REGISTER, p);
}

void sendDeregister(DsiClient* c) {
    std::vector<uint8_t> p;
    DsiClient::putString(p, AA_SPP_NAME);   // libdsiservice: deregister takes the service name only
    printf("[BluetoothManager] SppUuidDeregister(name='%s'): clearing a stale registration, then retrying\n",
           AA_SPP_NAME);
    c->sendRequest(DSI_REQ_SPP_UUID_DEREGISTER, p);
}

uint32_t readErr(const uint8_t* pl, uint32_t len) {
    return (pl && len >= 4) ? (uint32_t)(pl[0] | (pl[1] << 8) | (pl[2] << 16) | ((uint32_t)pl[3] << 24)) : 0xffffffffu;
}

void onGapAttached(DsiClient* c, void*) {
    g_spp = SPP_REG_SENT;
    sendRegister(c);
}

void onGapData(DsiClient* c, uint32_t type, uint32_t id, uint32_t, const uint8_t* pl, uint32_t len, void*) {
    if (id == DSI_RSP_SPP_UUID_REGISTER) {
        uint32_t err = readErr(pl, len);
        printf("[BluetoothManager] SppUuidRegister response: GapErrorCode=%u (%s)%s\n", err, gapErrorName(err),
               g_spp == SPP_REREG_SENT ? " [retry]" : "");
        if (err != 0 && g_spp == SPP_REG_SENT) {
            g_spp = SPP_DEREG_SENT;
            sendDeregister(c);
            return;
        }
        if (err != 0)
            printf("[BluetoothManager] SPP registration still failing: the phone will not find Android Auto over Bluetooth "
                   "(reboot the head unit and try again)\n");
        g_spp = SPP_DONE;
    } else if (g_spp == SPP_DEREG_SENT && type == 0x200) {
        // Deregister response id is not confirmed; take the first plain response after the request.
        uint32_t err = readErr(pl, len);
        printf("[BluetoothManager] SppUuidDeregister response (id=0x%x): GapErrorCode=%u (%s); registering again\n",
               id, err, gapErrorName(err));
        g_spp = SPP_REREG_SENT;
        sendRegister(c);
    } else if (type == 0x204 || type == 0x202) {
        printf("[BluetoothManager] request rejected by stub/server (type=0x%x id=0x%x)\n", type, id);
        if (g_spp == SPP_DEREG_SENT) {   // no deregister support: still try once more
            g_spp = SPP_REREG_SENT;
            sendRegister(c);
        }
    }
}
} // namespace

BluetoothManager::BluetoothManager() : is_registered_(false) {}

BluetoothManager::~BluetoothManager() { stop(); }

bool BluetoothManager::init() {
    printf("[BluetoothManager] Initializing (DSI client to BSS_GAP.SGBluetoothGap %d.%d)\n",
           DSI_GAP_MAJOR, DSI_GAP_MINOR);
    return true;
}

bool BluetoothManager::registerAndroidAutoSdp() {
    if (is_registered_) return true;
    g_gap.setCallbacks(&onGapAttached, &onGapData, NULL);
    if (!g_gap.open(DSI_SERVICE_BLUETOOTH_GAP, DSI_ROLE_BLUETOOTH_GAP, DSI_GAP_MAJOR, DSI_GAP_MINOR)) {
        printf("[BluetoothManager] DSI client could not be created; no SDP registration\n");
        return false;
    }
    is_registered_ = true;   // request goes out asynchronously when the server attaches
    return true;
}

void BluetoothManager::stop() {
    if (!is_registered_) return;
    // Deregistration (request 0x2f) is intentionally not sent at shutdown: the DSI
    // connection thread is detached and sending off-thread is unsafe.
    is_registered_ = false;
}

} // namespace nbt
