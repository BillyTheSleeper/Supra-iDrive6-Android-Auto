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

void onGapAttached(DsiClient* c, void*) {
    const char* uuidCon = getenv("NBT_SPP_UUID_CON");
    if (!uuidCon || !*uuidCon) uuidCon = AA_BT_UUID;   // connect UUID: same as service UUID (INFERRED)
    std::vector<uint8_t> p;
    DsiClient::putString(p, "AndroidAuto");
    DsiClient::putString(p, AA_BT_UUID);
    DsiClient::putString(p, uuidCon);
    DsiClient::putU32(p, 0);   // type 0 = DevA (inferred)
    printf("[BluetoothManager] SppUuidRegister(name='AndroidAuto', uuid=%s, con=%s, type=0)\n",
           AA_BT_UUID, uuidCon);
    c->sendRequest(DSI_REQ_SPP_UUID_REGISTER, p);
}

void onGapData(DsiClient*, uint32_t type, uint32_t id, uint32_t, const uint8_t* pl, uint32_t len, void*) {
    if (id == DSI_RSP_SPP_UUID_REGISTER) {
        uint32_t err = (pl && len >= 4) ? (uint32_t)(pl[0] | (pl[1] << 8) | (pl[2] << 16) | ((uint32_t)pl[3] << 24)) : 0xffffffffu;
        printf("[BluetoothManager] SppUuidRegister response: GapErrorCode=%u (%s)\n", err, err == 0 ? "OK" : "FAILED");
    } else if (type == 0x204 || type == 0x202) {
        printf("[BluetoothManager] request rejected by stub/server (type=0x%x id=0x%x)\n", type, id);
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
