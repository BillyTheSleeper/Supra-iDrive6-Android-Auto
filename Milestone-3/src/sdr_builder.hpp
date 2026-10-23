// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef SDR_BUILDER_HPP
#define SDR_BUILDER_HPP

#include <stdint.h>
#include <stddef.h>
#include <string>
#include <vector>

namespace nbt {

struct SdrIdentity {
    std::string carMake, carModel, carYear, vehicleId;
    std::string huMake, huModel, swBuild, swVersion;
    std::string btAdapterAddress;   // empty = no BluetoothChannel descriptor
    SdrIdentity();                  // Toyota / GR Supra defaults
    void loadEnv();                 // NBT_CAR_MAKE, NBT_CAR_MODEL, NBT_CAR_YEAR, NBT_VEHICLE_ID,
                                    // NBT_HU_MAKE, NBT_HU_MODEL, NBT_SW_VERSION, NBT_BT_ADDR
};

// Takes the canned Google DHU ServiceDiscoveryResponse (dhu_sdp.h), keeps every
// channel descriptor untouched, replaces the identity strings (fields 2,3,4,5,7,8,9,10
// and the copies inside fields 16/17) and optionally appends a BluetoothChannel
// descriptor (ChannelDescriptor.bluetooth_channel = 6, adapter_address = 1).
std::vector<uint8_t> buildServiceDiscovery(const uint8_t* base, size_t baseLen, const SdrIdentity& id);

// Experiment hook for the main display's VideoConfig (channel 6). Environment (all optional):
//   NBT_VID_RES  = VideoResolution enum (1 = 800x480, 2 = 1280x720, 3 = 1920x1080)
//   NBT_VID_MW / NBT_VID_MH = margin width / height (VideoConfig fields 3 / 4)
//   NBT_VID_INSETS = "top,bottom,left,right"  -> AdditionalVideoConfig.insets (VideoConfig field 11)
// Returns true if anything was changed (prints what it did).
bool patchMainVideo(std::vector<uint8_t>& sdr);

// Declares the touchscreen (channel 7) as 1280 x NBT_TOUCH_CFG_H (default 720).
bool patchTouchConfig(std::vector<uint8_t>& sdr);

} // namespace nbt

#endif
