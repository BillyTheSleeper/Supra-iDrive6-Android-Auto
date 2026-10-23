// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef WIRELESS_SETUP_HPP
#define WIRELESS_SETUP_HPP

#include <stdint.h>
#include <stddef.h>
#include <string>
#include <vector>

namespace nbt {

// Access-point description sent to the phone (WifiInfoResponse).
struct ApInfo {
    std::string ssid;
    std::string key;
    std::string bssid;   // "AA:BB:CC:DD:EE:FF" (uppercase, colon separated)
    std::string ip;      // head unit IP the phone must connect to
    int security;        // 8 = WPA2_PERSONAL
    int apType;          // 1 = DYNAMIC
    ApInfo() : security(8), apType(1) {}
};

// Wireless Android Auto Wi-Fi Projection Protocol (WPP) over RFCOMM.
// Frame: [u16 payload_len BE][u16 msg_id BE][protobuf payload]
namespace wpp {

enum MsgId {
    kWifiStartRequest  = 1,  // HU -> phone  {ip=1 string, port=2 int32}
    kWifiInfoRequest   = 2,  // phone -> HU  (empty)
    kWifiInfoResponse  = 3,  // HU -> phone  {ssid=1,key=2,bssid=3,security=4,ap_type=5}
    kWifiVersionRequest  = 4, // HU -> phone (openauto sends it empty)
    kWifiVersionResponse = 5, // phone -> HU
    kWifiConnectStatus = 6,  // phone -> HU  {status=1}
    kWifiStartResponse = 7   // phone -> HU  {ip=2,port=3,status=4}
};

// Protobuf helpers (proto2 wire format, only what WPP needs)
void putVarint(std::vector<uint8_t>& out, uint64_t v);
void putString(std::vector<uint8_t>& out, int field, const std::string& s);
void putInt(std::vector<uint8_t>& out, int field, uint64_t v);
bool getVarint(const uint8_t* p, size_t n, size_t& pos, uint64_t& v);
// Finds a varint field (returns false if absent). Skips other fields.
bool findVarintField(const uint8_t* p, size_t n, int field, uint64_t& v);

std::vector<uint8_t> frame(uint16_t id, const std::vector<uint8_t>& payload);
std::vector<uint8_t> buildStartRequest(const std::string& ip, uint16_t port);
std::vector<uint8_t> buildInfoResponse(const ApInfo& ap);

} // namespace wpp

// Reads the [WiFi] section (SSID=, Passphrase=, BSSID=, Channel=) of the CarPlay
// iAP2 config (e.g. /fs/sda1/opt/conn/carplay/iap2_Wifi.cfg). Lines starting
// with ';' or '#' are ignored. Returns true if ssid and passphrase were found.
bool loadCarPlayApConfig(const char* path, ApInfo& out);

// Fills ip / bssid from `ifconfig <ifname>` output when still empty.
// Env overrides: NBT_AP_IP, NBT_AP_BSSID.
void fillApNetworkInfo(const char* ifname, ApInfo& ap);

// Blocking handshake on an already-open RFCOMM stream (fd). Returns true when the
// phone reports WifiConnectStatus == 0 (SUCCESS). timeout_ms bounds each read wait.
//   mode 0 = legacy (WifiStartRequest, then answer WifiInfoRequest)           -- what WAAD does
//   mode 1 = version-first (empty WifiVersionRequest, then WifiStartRequest)  -- what openauto does
//   mode 2 = proactive (WifiStartRequest, then WifiInfoResponse without waiting for the request)
bool runWirelessHandshake(int fd, const ApInfo& ap, uint16_t port, int timeout_ms, int mode = 0);

// After a successful handshake keep the RFCOMM link open and serviced: answers WifiPingRequest, repeats
// the AP info if the phone asks again, logs everything else. Waits up to timeout_ms for data.
// Returns false once the link is closed by the phone / broken.
bool wppService(int fd, int timeout_ms, const ApInfo& ap, uint16_t port);

} // namespace nbt

#endif // WIRELESS_SETUP_HPP
