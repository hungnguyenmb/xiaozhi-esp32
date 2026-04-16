#ifndef OLED_FACE_DISPLAY_H
#define OLED_FACE_DISPLAY_H

#include "oled_display.h"

#include <string>

class OledFaceDisplay : public OledDisplay {
public:
    OledFaceDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width, int height, bool mirror_x, bool mirror_y);
    ~OledFaceDisplay() override;

    void SetupUI() override;
    void SetStatus(const char* status) override;
    void ShowNotification(const char* notification, int duration_ms = 3000) override;
    void SetChatMessage(const char* role, const char* content) override;
    void SetEmotion(const char* emotion) override;
    void SetPowerSaveMode(bool on) override;
    void UpdateStatusBar(bool update_all = false) override;

private:
    enum class EyeStyle {
        OPEN,
        WIDE,
        SMALL,
        HALF,
        SQUINT,
        HAPPY,
        CLOSED,
    };

    enum class MouthStyle {
        FLAT,
        SMALL,
        SMILE,
        OPEN_SMALL,
        OPEN_LARGE,
        KISS,
        SMIRK_LEFT,
        SMIRK_RIGHT,
    };

    enum class ActivityMode {
        IDLE,
        LISTENING,
        SPEAKING,
    };

    struct FacePreset {
        EyeStyle left_eye;
        EyeStyle right_eye;
        MouthStyle mouth;
        bool auto_blink;
        uint16_t blink_period_ticks;
    };

    lv_obj_t* root_ = nullptr;
    lv_obj_t* face_layer_ = nullptr;
    lv_timer_t* anim_timer_ = nullptr;

    std::string current_emotion_ = "neutral";
    ActivityMode activity_mode_ = ActivityMode::IDLE;
    bool power_save_mode_ = false;
    uint32_t animation_tick_ = 0;

    static void AnimationTimerCb(lv_timer_t* timer);
    void OnAnimationTick();
    void ResetAnimation();
    FacePreset GetBasePreset(const std::string& emotion) const;
    FacePreset ResolveAnimatedPreset() const;
    bool IsBlinkFrame(const FacePreset& preset) const;
    void RenderFace();
    void DrawEye(int center_x, int center_y, EyeStyle style);
    void DrawMouth(int center_x, int center_y, MouthStyle style);
    lv_obj_t* CreateFilledEllipse(int x, int y, int width, int height);
    lv_obj_t* CreateOutlineEllipse(int x, int y, int width, int height, int border_width);
};

#endif
