#include "oled_face_display.h"

#include "assets/lang_config.h"
#include "lvgl_theme.h"

#include <algorithm>

#include <esp_log.h>

#define TAG "OledFaceDisplay"

namespace {

constexpr int kAnimPeriodMs = 120;
constexpr int kLeftEyeCenterX = 38;
constexpr int kRightEyeCenterX = 90;
constexpr int kEyesCenterY = 24;
constexpr int kMouthCenterX = 64;
constexpr int kMouthCenterY = 47;

bool IsDeferredEmotion(std::string_view emotion) {
    return !emotion.empty() && emotion != "neutral" && emotion != "thinking";
}

}  // namespace

OledFaceDisplay::OledFaceDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
    int width, int height, bool mirror_x, bool mirror_y)
    : OledDisplay(panel_io, panel, width, height, mirror_x, mirror_y) {
}

OledFaceDisplay::~OledFaceDisplay() {
    if (anim_timer_ != nullptr) {
        lv_timer_del(anim_timer_);
        anim_timer_ = nullptr;
    }
    if (root_ != nullptr) {
        lv_obj_del(root_);
        root_ = nullptr;
        face_layer_ = nullptr;
    }
}

void OledFaceDisplay::SetupUI() {
    if (setup_ui_called_) {
        ESP_LOGW(TAG, "SetupUI() called multiple times, skipping duplicate call");
        return;
    }

    Display::SetupUI();
    DisplayLockGuard lock(this);

    auto screen = lv_screen_active();
    lv_obj_clean(screen);
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    root_ = lv_obj_create(screen);
    lv_obj_set_size(root_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_opa(root_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(root_, 0, 0);
    lv_obj_set_style_pad_all(root_, 0, 0);
    lv_obj_set_scrollbar_mode(root_, LV_SCROLLBAR_MODE_OFF);

    face_layer_ = lv_obj_create(root_);
    lv_obj_set_size(face_layer_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_opa(face_layer_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(face_layer_, 0, 0);
    lv_obj_set_style_pad_all(face_layer_, 0, 0);
    lv_obj_set_scrollbar_mode(face_layer_, LV_SCROLLBAR_MODE_OFF);

    anim_timer_ = lv_timer_create(AnimationTimerCb, kAnimPeriodMs, this);
    if (anim_timer_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create animation timer");
    }

    RenderFace();
}

void OledFaceDisplay::SetStatus(const char* status) {
    DisplayLockGuard lock(this);
    auto previous_mode = activity_mode_;
    if (status != nullptr && strcmp(status, Lang::Strings::LISTENING) == 0) {
        activity_mode_ = ActivityMode::LISTENING;
        defer_emotion_until_speaking_ = false;
        pending_emotion_.clear();
    } else if (status != nullptr && strcmp(status, Lang::Strings::SPEAKING) == 0) {
        activity_mode_ = ActivityMode::SPEAKING;
        if (defer_emotion_until_speaking_) {
            defer_emotion_until_speaking_ = false;
            ApplyEmotionNow(pending_emotion_.empty() ? "neutral" : pending_emotion_);
            pending_emotion_.clear();
        }
    } else {
        if (previous_mode == ActivityMode::LISTENING) {
            activity_mode_ = ActivityMode::THINKING;
            defer_emotion_until_speaking_ = true;
            pending_emotion_.clear();
        } else {
            activity_mode_ = ActivityMode::IDLE;
            defer_emotion_until_speaking_ = false;
            pending_emotion_.clear();
        }
    }
    ResetAnimation();
    RenderFace();
}

void OledFaceDisplay::ShowNotification(const char* notification, int duration_ms) {
    (void)notification;
    (void)duration_ms;
}

void OledFaceDisplay::SetChatMessage(const char* role, const char* content) {
    (void)role;
    (void)content;
}

void OledFaceDisplay::SetEmotion(const char* emotion) {
    DisplayLockGuard lock(this);
    std::string next_emotion = (emotion != nullptr && emotion[0] != '\0') ? emotion : "neutral";

    if (defer_emotion_until_speaking_) {
        if (IsDeferredEmotion(next_emotion)) {
            pending_emotion_ = std::move(next_emotion);
        }
        return;
    }

    ApplyEmotionNow(std::move(next_emotion));
    RenderFace();
}

void OledFaceDisplay::SetPowerSaveMode(bool on) {
    DisplayLockGuard lock(this);
    power_save_mode_ = on;
    if (on) {
        activity_mode_ = ActivityMode::IDLE;
    }
    ResetAnimation();
    RenderFace();
}

void OledFaceDisplay::UpdateStatusBar(bool update_all) {
    (void)update_all;
}

void OledFaceDisplay::AnimationTimerCb(lv_timer_t* timer) {
    auto* self = static_cast<OledFaceDisplay*>(lv_timer_get_user_data(timer));
    if (self != nullptr) {
        self->OnAnimationTick();
    }
}

void OledFaceDisplay::OnAnimationTick() {
    animation_tick_++;
    RenderFace();
}

void OledFaceDisplay::ResetAnimation() {
    animation_tick_ = 0;
}

void OledFaceDisplay::ApplyEmotionNow(std::string emotion) {
    current_emotion_ = emotion.empty() ? "neutral" : std::move(emotion);
    ResetAnimation();
}

OledFaceDisplay::FacePreset OledFaceDisplay::GetBasePreset(const std::string& emotion) const {
    if (emotion == "happy") {
        return {EyeStyle::OPEN, EyeStyle::OPEN, MouthStyle::SMILE, true, 26};
    }
    if (emotion == "laughing") {
        return {EyeStyle::HAPPY, EyeStyle::HAPPY, MouthStyle::OPEN_LARGE, false, 0};
    }
    if (emotion == "funny") {
        return {EyeStyle::OPEN, EyeStyle::SMALL, MouthStyle::SMILE, true, 18};
    }
    if (emotion == "sad") {
        return {EyeStyle::HALF, EyeStyle::HALF, MouthStyle::FROWN, true, 28};
    }
    if (emotion == "angry") {
        return {EyeStyle::SQUINT, EyeStyle::SQUINT, MouthStyle::FLAT, false, 0};
    }
    if (emotion == "crying") {
        return {EyeStyle::HALF, EyeStyle::HALF, MouthStyle::OPEN_SMALL, true, 30};
    }
    if (emotion == "loving") {
        return {EyeStyle::WIDE, EyeStyle::WIDE, MouthStyle::KISS, true, 24};
    }
    if (emotion == "embarrassed") {
        return {EyeStyle::SMALL, EyeStyle::SMALL, MouthStyle::SMALL, true, 18};
    }
    if (emotion == "surprised") {
        return {EyeStyle::WIDE, EyeStyle::WIDE, MouthStyle::OPEN_SMALL, false, 0};
    }
    if (emotion == "shocked") {
        return {EyeStyle::WIDE, EyeStyle::WIDE, MouthStyle::OPEN_LARGE, false, 0};
    }
    if (emotion == "thinking") {
        return {EyeStyle::SMALL, EyeStyle::OPEN, MouthStyle::SMIRK_RIGHT, true, 24};
    }
    if (emotion == "winking") {
        return {EyeStyle::CLOSED, EyeStyle::OPEN, MouthStyle::SMILE, false, 0};
    }
    if (emotion == "cool") {
        return {EyeStyle::HALF, EyeStyle::HALF, MouthStyle::SMIRK_RIGHT, false, 0};
    }
    if (emotion == "relaxed") {
        return {EyeStyle::HAPPY, EyeStyle::HAPPY, MouthStyle::SMALL, true, 30};
    }
    if (emotion == "delicious") {
        return {EyeStyle::HAPPY, EyeStyle::HAPPY, MouthStyle::OPEN_SMALL, true, 20};
    }
    if (emotion == "kissy") {
        return {EyeStyle::HAPPY, EyeStyle::CLOSED, MouthStyle::KISS, false, 0};
    }
    if (emotion == "confident") {
        return {EyeStyle::HALF, EyeStyle::HALF, MouthStyle::SMIRK_LEFT, false, 0};
    }
    if (emotion == "sleepy") {
        return {EyeStyle::CLOSED, EyeStyle::CLOSED, MouthStyle::SMALL, false, 0};
    }
    if (emotion == "silly") {
        return {EyeStyle::WIDE, EyeStyle::SMALL, MouthStyle::OPEN_SMALL, true, 14};
    }
    if (emotion == "confused") {
        return {EyeStyle::OPEN, EyeStyle::HALF, MouthStyle::FLAT, true, 16};
    }
    return {EyeStyle::OPEN, EyeStyle::OPEN, MouthStyle::SMALL, true, 24};
}

OledFaceDisplay::FacePreset OledFaceDisplay::ResolveSequence(const EmotionStep* steps, size_t count) const {
    if (steps == nullptr || count == 0) {
        return GetBasePreset("neutral");
    }

    uint32_t total_ticks = 0;
    for (size_t i = 0; i < count; ++i) {
        total_ticks += std::max<uint16_t>(1, steps[i].duration_ticks);
    }

    if (total_ticks == 0) {
        return GetBasePreset("neutral");
    }

    uint32_t phase_tick = animation_tick_ % total_ticks;
    for (size_t i = 0; i < count; ++i) {
        uint32_t duration = std::max<uint16_t>(1, steps[i].duration_ticks);
        if (phase_tick < duration) {
            return {
                steps[i].left_eye,
                steps[i].right_eye,
                steps[i].mouth,
                steps[i].auto_blink,
                steps[i].blink_period_ticks,
            };
        }
        phase_tick -= duration;
    }

    const auto& last = steps[count - 1];
    return {last.left_eye, last.right_eye, last.mouth, last.auto_blink, last.blink_period_ticks};
}

OledFaceDisplay::FacePreset OledFaceDisplay::ResolveHappyPreset() const {
    // "Duoc khen" nen vui theo nhip: mo ra -> no dan -> giu -> chop nhe -> quay ve cuoi am.
    static const EmotionStep kHappySteps[] = {
        {EyeStyle::WIDE,   EyeStyle::WIDE,   MouthStyle::SMALL, 1, false, 0},
        {EyeStyle::OPEN,   EyeStyle::OPEN,   MouthStyle::SMALL, 1, false, 0},
        {EyeStyle::OPEN,   EyeStyle::OPEN,   MouthStyle::SMILE, 3, false, 0},
        {EyeStyle::HAPPY,  EyeStyle::HAPPY,  MouthStyle::SMILE, 1, false, 0},
        {EyeStyle::CLOSED, EyeStyle::CLOSED, MouthStyle::SMILE, 1, false, 0},
        {EyeStyle::OPEN,   EyeStyle::OPEN,   MouthStyle::SMILE, 2, false, 0},
    };

    return ResolveSequence(kHappySteps, sizeof(kHappySteps) / sizeof(kHappySteps[0]));
}

OledFaceDisplay::FacePreset OledFaceDisplay::ResolveLaughingPreset() const {
    // "Cuoi to" phai co nhip bat 3 lan: lay da -> bung cuoi -> ha xuong nhe -> cuoi tiep -> hoi phuc.
    static const EmotionStep kLaughingSteps[] = {
        {EyeStyle::WIDE,  EyeStyle::WIDE,  MouthStyle::SMALL,      1, false, 0},
        {EyeStyle::HAPPY, EyeStyle::HAPPY, MouthStyle::OPEN_SMALL, 1, false, 0},
        {EyeStyle::HAPPY, EyeStyle::HAPPY, MouthStyle::OPEN_LARGE, 1, false, 0},
        {EyeStyle::HAPPY, EyeStyle::HAPPY, MouthStyle::OPEN_SMALL, 1, false, 0},
        {EyeStyle::HAPPY, EyeStyle::HAPPY, MouthStyle::OPEN_LARGE, 1, false, 0},
        {EyeStyle::HAPPY, EyeStyle::HAPPY, MouthStyle::OPEN_SMALL, 1, false, 0},
        {EyeStyle::OPEN,  EyeStyle::OPEN,  MouthStyle::SMILE,      1, false, 0},
        {EyeStyle::OPEN,  EyeStyle::OPEN,  MouthStyle::SMILE,      1, false, 0},
    };

    return ResolveSequence(kLaughingSteps, sizeof(kLaughingSteps) / sizeof(kLaughingSteps[0]));
}

OledFaceDisplay::FacePreset OledFaceDisplay::ResolveThinkingPreset() const {
    // "Dang suy nghi" can khac neutral ro rang: lech mat, mieng lech, co nhip dao y rat nhe.
    static const EmotionStep kThinkingSteps[] = {
        {EyeStyle::SMALL, EyeStyle::OPEN, MouthStyle::FLAT,        1, false, 0},
        {EyeStyle::HALF,  EyeStyle::OPEN, MouthStyle::SMIRK_RIGHT, 2, false, 0},
        {EyeStyle::OPEN,  EyeStyle::SMALL, MouthStyle::SMALL,      1, false, 0},
        {EyeStyle::OPEN,  EyeStyle::HALF, MouthStyle::SMIRK_LEFT,  2, false, 0},
        {EyeStyle::HALF,  EyeStyle::HALF, MouthStyle::SMALL,       1, false, 0},
        {EyeStyle::SMALL, EyeStyle::OPEN, MouthStyle::SMIRK_RIGHT, 2, false, 0},
    };

    return ResolveSequence(kThinkingSteps, sizeof(kThinkingSteps) / sizeof(kThinkingSteps[0]));
}

OledFaceDisplay::FacePreset OledFaceDisplay::ResolveSadPreset() const {
    // "Buon" can co khoang dung, rut xuong, giu lai va chop mat cham o cuoi.
    static const EmotionStep kSadSteps[] = {
        {EyeStyle::OPEN,   EyeStyle::OPEN,  MouthStyle::SMALL, 1, false, 0},
        {EyeStyle::OPEN,   EyeStyle::HALF,  MouthStyle::SMALL, 1, false, 0},
        {EyeStyle::HALF,   EyeStyle::HALF,  MouthStyle::FLAT,  1, false, 0},
        {EyeStyle::HALF,   EyeStyle::HALF,  MouthStyle::FROWN, 3, false, 0},
        {EyeStyle::CLOSED, EyeStyle::CLOSED, MouthStyle::FROWN, 1, false, 0},
        {EyeStyle::HALF,   EyeStyle::HALF,  MouthStyle::FROWN, 2, false, 0},
    };

    return ResolveSequence(kSadSteps, sizeof(kSadSteps) / sizeof(kSadSteps[0]));
}

OledFaceDisplay::FacePreset OledFaceDisplay::ResolveCryingPreset() const {
    // "Khoc" phai di qua buon roi moi roi le, tranh hien nuoc mat qua som.
    static const EmotionStep kCryingSteps[] = {
        {EyeStyle::OPEN,   EyeStyle::OPEN,  MouthStyle::SMALL,      1, false, 0},
        {EyeStyle::OPEN,   EyeStyle::HALF,  MouthStyle::FLAT,       1, false, 0},
        {EyeStyle::HALF,   EyeStyle::HALF,  MouthStyle::FROWN,      2, false, 0},
        {EyeStyle::HALF,   EyeStyle::HALF,  MouthStyle::FROWN,      1, false, 0},
        {EyeStyle::CLOSED, EyeStyle::HALF,  MouthStyle::FROWN,      1, false, 0},
        {EyeStyle::HALF,   EyeStyle::HALF,  MouthStyle::OPEN_SMALL, 2, false, 0},
    };

    return ResolveSequence(kCryingSteps, sizeof(kCryingSteps) / sizeof(kCryingSteps[0]));
}

bool OledFaceDisplay::IsBlinkFrame(const FacePreset& preset) const {
    if (!preset.auto_blink || preset.blink_period_ticks == 0) {
        return false;
    }
    uint32_t phase = animation_tick_ % preset.blink_period_ticks;
    return phase == 0 || phase == 1;
}

OledFaceDisplay::FacePreset OledFaceDisplay::ResolveAnimatedPreset() const {
    std::string emotion = power_save_mode_
        ? "sleepy"
        : (activity_mode_ == ActivityMode::THINKING ? "thinking" : current_emotion_);
    FacePreset preset;
    if (emotion == "happy") {
        preset = ResolveHappyPreset();
    } else if (emotion == "laughing") {
        preset = ResolveLaughingPreset();
    } else if (emotion == "thinking") {
        preset = ResolveThinkingPreset();
    } else if (emotion == "sad") {
        preset = ResolveSadPreset();
    } else if (emotion == "crying") {
        preset = ResolveCryingPreset();
    } else {
        preset = GetBasePreset(emotion);
    }

    if (emotion == "winking") {
        preset.left_eye = (animation_tick_ / 6) % 2 == 0 ? EyeStyle::CLOSED : EyeStyle::OPEN;
    } else if (emotion == "kissy") {
        preset.right_eye = (animation_tick_ / 6) % 2 == 0 ? EyeStyle::CLOSED : EyeStyle::OPEN;
    } else if (emotion == "shocked" || emotion == "surprised") {
        preset.mouth = (animation_tick_ / 2) % 2 == 0 ? MouthStyle::OPEN_SMALL : MouthStyle::OPEN_LARGE;
    }

    if (activity_mode_ == ActivityMode::LISTENING) {
        if ((animation_tick_ / 2) % 2 == 0) {
            if (preset.left_eye == EyeStyle::OPEN) preset.left_eye = EyeStyle::WIDE;
            if (preset.right_eye == EyeStyle::OPEN) preset.right_eye = EyeStyle::WIDE;
            if (preset.left_eye == EyeStyle::SMALL) preset.left_eye = EyeStyle::OPEN;
            if (preset.right_eye == EyeStyle::SMALL) preset.right_eye = EyeStyle::OPEN;
        }
        if (preset.mouth == MouthStyle::OPEN_LARGE) {
            preset.mouth = MouthStyle::OPEN_SMALL;
        }
    } else if (activity_mode_ == ActivityMode::THINKING) {
        preset = ResolveThinkingPreset();
    } else if (activity_mode_ == ActivityMode::SPEAKING) {
        if (!ShouldUseFullEmotionWhileSpeaking(emotion)) {
            uint32_t phase = animation_tick_ % 4;
            if (phase == 0 || phase == 2) {
                preset.mouth = MouthStyle::OPEN_SMALL;
            } else {
                preset.mouth = MouthStyle::OPEN_LARGE;
            }
        }
    } else if (IsBlinkFrame(preset)) {
        preset.left_eye = EyeStyle::CLOSED;
        preset.right_eye = EyeStyle::CLOSED;
    }

    return preset;
}

bool OledFaceDisplay::ShouldUseFullEmotionWhileSpeaking(std::string_view emotion) const {
    return emotion == "laughing" || emotion == "happy" || emotion == "sad" || emotion == "crying";
}

bool OledFaceDisplay::ShouldDrawLeftTear() const {
    if (power_save_mode_ || current_emotion_ != "crying") {
        return false;
    }

    uint32_t phase = animation_tick_ % 8;
    return phase >= 4;
}

bool OledFaceDisplay::ShouldDrawRightTear() const {
    if (power_save_mode_ || current_emotion_ != "crying") {
        return false;
    }

    uint32_t phase = animation_tick_ % 8;
    return phase >= 5;
}

lv_obj_t* OledFaceDisplay::CreateFilledEllipse(int x, int y, int width, int height) {
    lv_obj_t* obj = lv_obj_create(face_layer_);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, width, height);
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(obj, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
    return obj;
}

lv_obj_t* OledFaceDisplay::CreateOutlineEllipse(int x, int y, int width, int height, int border_width) {
    lv_obj_t* obj = lv_obj_create(face_layer_);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, width, height);
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, border_width, 0);
    lv_obj_set_style_border_color(obj, lv_color_black(), 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
    return obj;
}

void OledFaceDisplay::DrawEye(int center_x, int center_y, EyeStyle style) {
    int width = 24;
    int height = 18;
    int y_offset = 0;

    switch (style) {
    case EyeStyle::OPEN:
        width = 24;
        height = 18;
        break;
    case EyeStyle::WIDE:
        width = 28;
        height = 21;
        break;
    case EyeStyle::SMALL:
        width = 18;
        height = 12;
        break;
    case EyeStyle::HALF:
        width = 24;
        height = 10;
        y_offset = 3;
        break;
    case EyeStyle::SQUINT:
        width = 24;
        height = 6;
        y_offset = 5;
        break;
    case EyeStyle::HAPPY:
        width = 24;
        height = 8;
        y_offset = 4;
        break;
    case EyeStyle::CLOSED:
        width = 24;
        height = 4;
        y_offset = 6;
        break;
    }

    CreateFilledEllipse(center_x - (width / 2), center_y - (height / 2) + y_offset, width, height);
}

void OledFaceDisplay::DrawMouth(int center_x, int center_y, MouthStyle style) {
    int width = 16;
    int height = 4;
    int x_offset = 0;
    bool outline = false;
    int border_width = 2;

    switch (style) {
    case MouthStyle::FLAT:
        width = 16;
        height = 3;
        break;
    case MouthStyle::SMALL:
        width = 10;
        height = 4;
        break;
    case MouthStyle::SMILE:
        width = 22;
        height = 5;
        break;
    case MouthStyle::FROWN:
        width = 20;
        height = 6;
        break;
    case MouthStyle::OPEN_SMALL:
        width = 10;
        height = 8;
        outline = true;
        break;
    case MouthStyle::OPEN_LARGE:
        width = 14;
        height = 12;
        outline = true;
        break;
    case MouthStyle::KISS:
        width = 8;
        height = 8;
        outline = true;
        border_width = 2;
        break;
    case MouthStyle::SMIRK_LEFT:
        width = 14;
        height = 4;
        x_offset = -5;
        break;
    case MouthStyle::SMIRK_RIGHT:
        width = 14;
        height = 4;
        x_offset = 5;
        break;
    }

    int x = center_x - (width / 2) + x_offset;
    int y = center_y - (height / 2);
    if (style == MouthStyle::FROWN) {
        CreateFilledEllipse(x, y + 2, 6, 3);
        CreateFilledEllipse(center_x - 3, y + 4, 6, 3);
        CreateFilledEllipse(x + 14, y + 2, 6, 3);
    } else if (outline) {
        CreateOutlineEllipse(x, y, width, height, border_width);
    } else {
        CreateFilledEllipse(x, y, width, height);
    }
}

void OledFaceDisplay::DrawTear(int center_x, int top_y, bool long_drop) {
    CreateFilledEllipse(center_x - 2, top_y, 4, 5);
    CreateFilledEllipse(center_x - 1, top_y + 4, 2, 4);
    if (long_drop) {
        CreateFilledEllipse(center_x - 1, top_y + 8, 2, 5);
    }
}

void OledFaceDisplay::RenderFace() {
    if (face_layer_ == nullptr) {
        return;
    }

    lv_obj_clean(face_layer_);

    auto preset = ResolveAnimatedPreset();
    DrawEye(kLeftEyeCenterX, kEyesCenterY, preset.left_eye);
    DrawEye(kRightEyeCenterX, kEyesCenterY, preset.right_eye);
    DrawMouth(kMouthCenterX, kMouthCenterY, preset.mouth);
    if (ShouldDrawLeftTear()) {
        DrawTear(kLeftEyeCenterX - 6, kEyesCenterY + 10, animation_tick_ % 2 == 0);
    }
    if (ShouldDrawRightTear()) {
        DrawTear(kRightEyeCenterX + 6, kEyesCenterY + 10, animation_tick_ % 2 == 1);
    }
}
