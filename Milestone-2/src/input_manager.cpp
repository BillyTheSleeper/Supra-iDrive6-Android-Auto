// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "input_manager.hpp"
#include <stdio.h>

namespace nbt {

InputManager::InputManager()
    : button_callback_(NULL),
      button_user_data_(NULL),
      wheel_callback_(NULL),
      wheel_user_data_(NULL),
      is_running_(false) {
}

InputManager::~InputManager() {
    stop();
}

bool InputManager::init() {
    printf("[InputManager] Initializing DSI controller bridge (Service 0x%04X, Instance 0x%02X)...\n",
           DSI_SERVICE_ID_CENTRAL_CONTROL_35UP, DSI_INSTANCE_ID_NBT_EVO_TC_HU);

    // In a full build, this hooks DSIConnection_Init and DSIConnection_AddClient
    // to /proc/mount/srv/servicebroker in Mode 8 (IPC).
    return true;
}

void InputManager::start() {
    is_running_ = true;
    printf("[InputManager] Listening for ZBE controller events.\n");
}

void InputManager::stop() {
    is_running_ = false;
    printf("[InputManager] ZBE controller listener stopped.\n");
}

void InputManager::setButtonCallback(ButtonEventCallback callback, void* user_data) {
    button_callback_ = callback;
    button_user_data_ = user_data;
}

void InputManager::setWheelCallback(WheelEventCallback callback, void* user_data) {
    wheel_callback_ = callback;
    wheel_user_data_ = user_data;
}

AAButtonCode InputManager::translateKeycode(uint32_t key_code) {
    switch (key_code) {
        case ZBE_KEY_CENTER_PUSH: return AA_BUTTON_ENTER;
        case ZBE_KEY_TILT_UP:     return AA_BUTTON_DPAD_UP;
        case ZBE_KEY_TILT_DOWN:   return AA_BUTTON_DPAD_DOWN;
        case ZBE_KEY_TILT_LEFT:   return AA_BUTTON_DPAD_LEFT;
        case ZBE_KEY_TILT_RIGHT:  return AA_BUTTON_DPAD_RIGHT;
        case ZBE_KEY_MENU_HOME:   return AA_BUTTON_HOME;
        case ZBE_KEY_BACK:        return AA_BUTTON_BACK;
        case ZBE_KEY_TEL:         return AA_BUTTON_CALL;
        case ZBE_KEY_MEDIA:       return AA_BUTTON_PLAY;
        default:                  return AA_BUTTON_NONE;
    }
}

void InputManager::handleDsiButtonEvent(const zbe_button_event_t& event) {
    if (!is_running_) return;

    // 1. Handle Rotary Turns (Keycodes 0x0D and 0x0E)
    if (event.key_code == ZBE_KEY_ROTATE_CW) {
        int steps = event.repeat_count > 0 ? event.repeat_count : 1;
        printf("[InputManager] Rotary CW -> Steps: %d\n", steps);
        if (wheel_callback_) wheel_callback_(steps, wheel_user_data_);
        return;
    }
    if (event.key_code == ZBE_KEY_ROTATE_CCW) {
        int steps = event.repeat_count > 0 ? -event.repeat_count : -1;
        printf("[InputManager] Rotary CCW -> Steps: %d\n", steps);
        if (wheel_callback_) wheel_callback_(steps, wheel_user_data_);
        return;
    }

    // 2. Handle Button Clicks & Tilts
    AAButtonCode aa_code = translateKeycode(event.key_code);
    if (aa_code != AA_BUTTON_NONE) {
        printf("[InputManager] Button Event -> Key: 0x%02X, AA: %u, Pressed: %d\n",
               event.key_code, (unsigned int)aa_code, event.pressed ? 1 : 0);
        if (button_callback_) {
            button_callback_(aa_code, event.pressed, button_user_data_);
        }
    }
}

void InputManager::handleDsiTouchEvent(const zbe_touch_event_t& event) {
    if (!is_running_) return;
    // Normalized 0..4095 touch coordinates
    // Used for touchpad pan/scroll gestures inside Android Auto
}

} // namespace nbt
