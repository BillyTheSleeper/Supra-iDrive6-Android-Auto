#ifndef INPUT_MANAGER_HPP
#define INPUT_MANAGER_HPP

#include "dsi_controller.h"
#include <functional>

namespace nbt {

// Android Auto Button Codes according to aasdk::proto::enums::ButtonCode
enum AAButtonCode {
    AA_BUTTON_NONE            = 0,
    AA_BUTTON_MICROPHONE_1    = 1,
    AA_BUTTON_MICROPHONE_2    = 2,
    AA_BUTTON_MENU            = 3,
    AA_BUTTON_DISP            = 4,
    AA_BUTTON_SCAN            = 5,
    AA_BUTTON_SEEK_UP         = 6,
    AA_BUTTON_SEEK_DOWN       = 7,
    AA_BUTTON_TUNE_UP         = 8,
    AA_BUTTON_TUNE_DOWN       = 9,
    AA_BUTTON_HOME            = 10,
    AA_BUTTON_BACK            = 11,
    AA_BUTTON_ENTER           = 12,
    AA_BUTTON_DPAD_UP         = 19,
    AA_BUTTON_DPAD_DOWN       = 20,
    AA_BUTTON_DPAD_LEFT       = 21,
    AA_BUTTON_DPAD_RIGHT      = 22,
    AA_BUTTON_WHEEL_LEFT      = 23,
    AA_BUTTON_WHEEL_RIGHT     = 24,
    AA_BUTTON_CALL            = 25,
    AA_BUTTON_ENDCALL         = 26,
    AA_BUTTON_PLAY            = 126,
    AA_BUTTON_PAUSE           = 127
};

typedef void (*ButtonEventCallback)(AAButtonCode code, bool pressed, void* user_data);
typedef void (*WheelEventCallback)(int steps, void* user_data); // negative = CCW, positive = CW

class InputManager {
public:
    InputManager();
    ~InputManager();

    bool init();
    void start();
    void stop();

    void setButtonCallback(ButtonEventCallback callback, void* user_data = NULL);
    void setWheelCallback(WheelEventCallback callback, void* user_data = NULL);

    // Entry point for DSI dispatcher notifications
    void handleDsiButtonEvent(const zbe_button_event_t& event);
    void handleDsiTouchEvent(const zbe_touch_event_t& event);

private:
    AAButtonCode translateKeycode(uint32_t key_code);

    ButtonEventCallback button_callback_;
    void* button_user_data_;

    WheelEventCallback  wheel_callback_;
    void* wheel_user_data_;

    bool is_running_;
};

} // namespace nbt

#endif // INPUT_MANAGER_HPP
