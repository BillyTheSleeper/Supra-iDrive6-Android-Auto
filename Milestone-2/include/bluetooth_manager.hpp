#ifndef BLUETOOTH_MANAGER_HPP
#define BLUETOOTH_MANAGER_HPP

#include <stdint.h>
#include <stddef.h>

namespace nbt {

// Google Android Auto Wireless Bluetooth SDP UUID:
// 4DE17A00-52CB-11E6-BEF8-0002A5D5C51B
static const uint32_t AA_BT_UUID_WORDS[4] = {
    0x4DE17A00,
    0x52CB11E6,
    0xBEF80002,
    0xA5D5C51B
};

// DSI Subsystem identifiers for BMW NBT Evo WICOME Bluetooth stack
static const char* const DSI_SERVICE_BLUETOOTH_GAP  = "SGBluetoothGap";
static const uint32_t   DSI_INSTANCE_BLUETOOTH_GAP = 0;
static const uint32_t   DSI_METHOD_SPP_UUID_REGISTER = 35; // 0x23

class BluetoothManager {
public:
    BluetoothManager();
    ~BluetoothManager();

    bool init();
    bool registerAndroidAutoSdp();
    void stop();

private:
    bool is_registered_;
};

} // namespace nbt

#endif // BLUETOOTH_MANAGER_HPP
