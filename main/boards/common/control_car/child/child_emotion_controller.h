#ifndef CONTROL_CAR_CHILD_EMOTION_CONTROLLER_H
#define CONTROL_CAR_CHILD_EMOTION_CONTROLLER_H

#include "application.h"
#include "board.h"
#include "mcp_server.h"
#include "assets/lang_config.h"
#include "control_car/shared/control_car_role.h"

#include <esp_log.h>

#include <algorithm>
#include <cctype>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>

namespace control_car {

class ChildEmotionController {
private:
    static constexpr const char* TAG = "ChildEmotion";

    struct EmotionPlan {
        std::string_view face;
        const std::string_view* sound;
        std::string_view status;
        std::string_view motion_style;
    };

    struct SoundPresetPlan {
        std::string_view face;
        const std::string_view* sound;
        std::string_view status;
    };

    std::mutex mutex_;
    inline static std::function<std::string(const std::string&)> motion_runner_;

    static std::string Normalize(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return value;
    }

    static EmotionPlan ResolveEmotion(const std::string& raw_emotion) {
        const auto emotion = Normalize(raw_emotion);
        if (emotion == "joy" || emotion == "happy" || emotion == "vui" || emotion == "vui_suong") {
            return {"happy", &Lang::Sounds::OGG_EMOTION_JOY, "child_emotion_joy", "joy"};
        }
        if (emotion == "laugh" || emotion == "laughing" || emotion == "cuoi" || emotion == "cuoi_to") {
            return {"laughing", &Lang::Sounds::OGG_EMOTION_LAUGH, "child_emotion_laugh", "laugh"};
        }
        if (emotion == "cry" || emotion == "crying" || emotion == "khoc") {
            return {"crying", &Lang::Sounds::OGG_EMOTION_CRY, "child_emotion_cry", "cry"};
        }
        if (emotion == "sad" || emotion == "buon") {
            return {"sad", &Lang::Sounds::OGG_EMOTION_CRY, "child_emotion_sad", "cry"};
        }
        if (emotion == "angry" || emotion == "gian") {
            return {"angry", &Lang::Sounds::OGG_EMOTION_ANGRY, "child_emotion_angry", "angry"};
        }
        if (emotion == "hate" || emotion == "ghet" || emotion == "disgust") {
            return {"angry", &Lang::Sounds::OGG_EMOTION_HATE, "child_emotion_hate", "hate"};
        }
        if (emotion == "love" || emotion == "loving" || emotion == "yeu") {
            return {"loving", &Lang::Sounds::OGG_EMOTION_LOVE, "child_emotion_love", "love"};
        }
        if (emotion == "thinking" || emotion == "think" || emotion == "nghi") {
            return {"thinking", nullptr, "child_emotion_thinking", ""};
        }
        if (emotion == "tango") {
            return {"happy", &Lang::Sounds::OGG_TANGO_VOICE, "child_emotion_tango", "tango"};
        }
        return {"confused", nullptr, "unknown_emotion", ""};
    }

    static SoundPresetPlan ResolveSoundPreset(const std::string& raw_action) {
        const auto action = Normalize(raw_action);
        if (action == "speak_hello" || action == "hello" || action == "chao" || action == "xin_chao") {
            return {"happy", &Lang::Sounds::OGG_BIRTH_CHILD_ROBOT_V1, "child_sound_hello"};
        }
        if (action == "speak_yes" || action == "yes" || action == "dong_y" || action == "ok") {
            return {"happy", &Lang::Sounds::OGG_SUCCESS, "child_sound_yes"};
        }
        if (action == "speak_no" || action == "no" || action == "khong") {
            return {"confused", &Lang::Sounds::OGG_EXCLAMATION, "child_sound_no"};
        }
        if (action == "sound_laugh" || action == "laugh_sound" || action == "cuoi_sound") {
            return {"laughing", &Lang::Sounds::OGG_EMOTION_LAUGH, "child_sound_laugh"};
        }
        if (action == "sound_cry" || action == "cry_sound" || action == "khoc_sound" || action == "baby_cry") {
            return {"crying", &Lang::Sounds::OGG_BIRTH_CHILD_ROBOT_BABY_CRY_V1, "child_sound_cry"};
        }
        if (action == "sound_success" || action == "success" || action == "thanh_cong") {
            return {"happy", &Lang::Sounds::OGG_SUCCESS, "child_sound_success"};
        }
        if (action == "sound_surprise" || action == "surprise" || action == "ngac_nhien") {
            return {"surprised", &Lang::Sounds::OGG_EXCLAMATION, "child_sound_surprise"};
        }
        if (action == "sound_popup" || action == "popup") {
            return {"surprised", &Lang::Sounds::OGG_POPUP, "child_sound_popup"};
        }
        if (action == "dance_music" || action == "music_dance" || action == "nhac_nhay") {
            return {"happy", &Lang::Sounds::OGG_DANCE_LONG_V1, "child_sound_dance_music"};
        }
        if (action == "tango_voice" || action == "voice_tango") {
            return {"happy", &Lang::Sounds::OGG_TANGO_VOICE, "child_sound_tango_voice"};
        }
        if (action == "sound_vibration" || action == "vibration") {
            return {"neutral", &Lang::Sounds::OGG_VIBRATION, "child_sound_vibration"};
        }
        return {"confused", nullptr, "unknown_sound_preset"};
    }

    static std::string EmotionDescription() {
        return
            "Cho robot con tự thể hiện cảm xúc local bằng OLED face và âm thanh local.\n"
            "Chỉ dùng tool này trên firmware xe con, khi người dùng yêu cầu robot con vui, cười, khóc, ghét, giận, yêu, suy nghĩ hoặc tango.\n"
            "Các emotion hỗ trợ: joy, happy, laugh, crying, cry, sad, angry, hate, love, thinking, tango.\n"
            "Preset âm thanh LAN hỗ trợ: speak_hello, speak_yes, speak_no, sound_laugh, sound_cry, sound_success, sound_surprise, dance_music, tango_voice.\n"
            "Nếu robot con đang nói và emotion mạnh được đặt, OLED sẽ giữ biểu cảm đó trong lúc nói theo logic display hiện tại.\n"
            "Sau khi tool thành công thì im lặng, không giải thích thêm.";
    }

public:
    static void SetMotionRunner(std::function<std::string(const std::string&)> runner) {
        motion_runner_ = std::move(runner);
    }

    static std::string ApplyEmotion(const std::string& emotion, bool play_sound) {
        const auto plan = ResolveEmotion(emotion);
        auto& app = Application::GetInstance();
        auto* display = Board::GetInstance().GetDisplay();

        if (display != nullptr) {
            display->ForceEmotion(std::string(plan.face).c_str());
        }

        if (play_sound && !plan.motion_style.empty() && motion_runner_) {
            auto motion_result = motion_runner_(std::string(plan.motion_style));
            ESP_LOGI(TAG, "motion_style=%.*s result=%s",
                static_cast<int>(plan.motion_style.size()),
                plan.motion_style.data(),
                motion_result.c_str());
        } else if (play_sound && plan.sound != nullptr) {
            app.PlayPrioritySound(*plan.sound);
        }

        return std::string(plan.status);
    }

    static bool IsSoundPreset(const std::string& action) {
        return ResolveSoundPreset(action).sound != nullptr;
    }

    static std::string ApplySoundPreset(const std::string& action) {
        const auto plan = ResolveSoundPreset(action);
        if (plan.sound == nullptr) {
            return std::string(plan.status);
        }

        auto* display = Board::GetInstance().GetDisplay();
        if (display != nullptr) {
            display->ForceEmotion(std::string(plan.face).c_str());
        }

        Application::GetInstance().PlayPrioritySound(*plan.sound);
        ESP_LOGI(TAG, "sound_preset=%s status=%.*s",
            action.c_str(),
            static_cast<int>(plan.status.size()),
            plan.status.data());
        return std::string(plan.status);
    }

    ChildEmotionController() {
        if (!control_car::IsChild()) {
            return;
        }

        auto& mcp_server = McpServer::GetInstance();
        mcp_server.AddTool(
            "self.child_robot.emotion",
            EmotionDescription(),
            PropertyList({
                Property("emotion", kPropertyTypeString),
                Property("play_sound", kPropertyTypeBoolean, true),
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                std::lock_guard<std::mutex> lock(mutex_);
                return ApplyEmotion(
                    properties["emotion"].value<std::string>(),
                    properties["play_sound"].value<bool>());
            }
        );
    }
};

} // namespace control_car

#endif // CONTROL_CAR_CHILD_EMOTION_CONTROLLER_H
