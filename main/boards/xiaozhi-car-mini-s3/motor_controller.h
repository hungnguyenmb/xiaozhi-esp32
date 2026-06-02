#ifndef _XIAOZHI_CAR_MINI_S3_MOTOR_CONTROLLER_H_
#define _XIAOZHI_CAR_MINI_S3_MOTOR_CONTROLLER_H_

#include "mcp_server.h"

#include <driver/gpio.h>
#include <driver/ledc.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <mutex>
#include <string>

#include "board.h"
#include "application.h"
#include "audio_service.h"
#include "assets/lang_config.h"

class MotorController {
public:
    MotorController(gpio_num_t left_forward, gpio_num_t left_backward,
                    gpio_num_t right_forward, gpio_num_t right_backward)
        : left_forward_(left_forward),
          left_backward_(left_backward),
          right_forward_(right_forward),
          right_backward_(right_backward) {
        InitializePwm();
        Stop();
        RegisterTools();
    }

    void StopMotorsOnly() {
        SetDuty(kLeftForwardChannel, 0);
        SetDuty(kLeftBackwardChannel, 0);
        SetDuty(kRightForwardChannel, 0);
        SetDuty(kRightBackwardChannel, 0);
    }

    void Stop() {
        if (dance_task_handle_ != nullptr) {
            TaskHandle_t temp = dance_task_handle_;
            dance_task_handle_ = nullptr;
            vTaskDelete(temp);
        }
        StopMotorsOnly();
    }

    void Forward(int speed_percent) {
        Drive(speed_percent, 0, speed_percent, 0);
    }

    void Backward(int speed_percent) {
        Drive(0, speed_percent, 0, speed_percent);
    }

    void TurnLeft(int speed_percent) {
        Drive(0, speed_percent, speed_percent, 0);
    }

    void TurnRight(int speed_percent) {
        Drive(speed_percent, 0, 0, speed_percent);
    }

    std::string Dance(const std::string& style) {
        return RunDance(style);
    }

private:
    TaskHandle_t dance_task_handle_ = nullptr;
    int current_dance_style_ = 0;
    static constexpr const char* TAG = "MotorController";
    static constexpr ledc_timer_t kMotorTimer = LEDC_TIMER_2;
    static constexpr ledc_channel_t kLeftForwardChannel = LEDC_CHANNEL_2;
    static constexpr ledc_channel_t kLeftBackwardChannel = LEDC_CHANNEL_3;
    static constexpr ledc_channel_t kRightForwardChannel = LEDC_CHANNEL_4;
    static constexpr ledc_channel_t kRightBackwardChannel = LEDC_CHANNEL_5;
    static constexpr int kDefaultSpeedPercent = 75;
    static constexpr int kDefaultDriveMs = 450;
    static constexpr int kDefaultTurnMs = 320;
    static constexpr int kNudgeDriveMs = 160;
    static constexpr int kNudgeTurnMs = 130;
    static constexpr int kMinDurationMs = 60;
    static constexpr int kMaxDurationMs = 1500;

    gpio_num_t left_forward_;
    gpio_num_t left_backward_;
    gpio_num_t right_forward_;
    gpio_num_t right_backward_;
    std::mutex mutex_;

    int ClampDuration(int duration_ms) {
        return std::clamp(duration_ms, kMinDurationMs, kMaxDurationMs);
    }

    int ClampSpeed(int speed_percent) {
        return std::clamp(speed_percent, 0, 100);
    }

    void InitializePwm() {
        ledc_timer_config_t timer_config = {};
        timer_config.speed_mode = LEDC_LOW_SPEED_MODE;
        timer_config.duty_resolution = LEDC_TIMER_10_BIT;
        timer_config.timer_num = kMotorTimer;
        timer_config.freq_hz = 5000;
        timer_config.clk_cfg = LEDC_AUTO_CLK;
        ESP_ERROR_CHECK(ledc_timer_config(&timer_config));

        ConfigureChannel(kLeftForwardChannel, left_forward_);
        ConfigureChannel(kLeftBackwardChannel, left_backward_);
        ConfigureChannel(kRightForwardChannel, right_forward_);
        ConfigureChannel(kRightBackwardChannel, right_backward_);
    }

    void ConfigureChannel(ledc_channel_t channel, gpio_num_t gpio) {
        ledc_channel_config_t channel_config = {};
        channel_config.gpio_num = gpio;
        channel_config.speed_mode = LEDC_LOW_SPEED_MODE;
        channel_config.channel = channel;
        channel_config.timer_sel = kMotorTimer;
        channel_config.duty = 0;
        channel_config.hpoint = 0;
        ESP_ERROR_CHECK(ledc_channel_config(&channel_config));
    }

    void SetDuty(ledc_channel_t channel, int speed_percent) {
        uint32_t duty = static_cast<uint32_t>(ClampSpeed(speed_percent) * 1023 / 100);
        ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, duty));
        ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, channel));
    }

    void Drive(int left_forward, int left_backward, int right_forward, int right_backward) {
        SetDuty(kLeftForwardChannel, left_forward);
        SetDuty(kLeftBackwardChannel, left_backward);
        SetDuty(kRightForwardChannel, right_forward);
        SetDuty(kRightBackwardChannel, right_backward);
    }

    std::string RunTimedMotion(const std::string& motion, int duration_ms, int speed_percent) {
        std::lock_guard<std::mutex> lock(mutex_);
        duration_ms = ClampDuration(duration_ms);
        speed_percent = ClampSpeed(speed_percent);

        ESP_LOGI(TAG, "motion=%s duration_ms=%d speed=%d", motion.c_str(), duration_ms, speed_percent);

        // Stop any audio playback before running motor to prevent brownout
        auto& audio_service = Application::GetInstance().GetAudioService();
        audio_service.ResetDecoder();
        vTaskDelay(pdMS_TO_TICKS(50));

        if (motion == "forward") {
            Forward(speed_percent);
        } else if (motion == "backward") {
            Backward(speed_percent);
        } else if (motion == "turn_left") {
            TurnLeft(speed_percent);
        } else if (motion == "turn_right") {
            TurnRight(speed_percent);
        } else {
            Stop();
            return "unknown motion";
        }

        vTaskDelay(pdMS_TO_TICKS(duration_ms));
        Stop();
        return "ok";
    }

    static void DanceTask(void* pvParameters) {
        MotorController* self = static_cast<MotorController*>(pvParameters);
        self->ExecuteDance();
        self->dance_task_handle_ = nullptr;
        vTaskDelete(NULL);
    }

    // Generic sound task — plays whichever sound is stored in current_emotion_sound_
    const std::string_view* current_emotion_sound_ = nullptr;

    static void PlayEmotionSoundTask(void* pvParameters) {
        MotorController* self = static_cast<MotorController*>(pvParameters);
        ESP_LOGI(TAG, "PlayEmotionSoundTask started!");
        auto& app = Application::GetInstance();
        auto& audio_service = app.GetAudioService();
        audio_service.ResetDecoder();
        if (self->current_emotion_sound_ != nullptr) {
            audio_service.PlaySound(*self->current_emotion_sound_);
        }
        ESP_LOGI(TAG, "PlayEmotionSoundTask finished!");
        vTaskDelete(NULL);
    }

    static void PlaySoundTask(void* pvParameters) {
        ESP_LOGI(TAG, "PlaySoundTask started!");
        auto& app = Application::GetInstance();
        auto& audio_service = app.GetAudioService();
        audio_service.ResetDecoder();
        audio_service.PlaySound(Lang::Sounds::OGG_TANGO_VOICE);
        ESP_LOGI(TAG, "PlaySoundTask finished pushing audio!");
        vTaskDelete(NULL);
    }

    // ========== Emotion Movement Routines ==========

    // JOY: Energetic bouncing, spinning, fast wiggles — like an excited child
    // Total duration: ~4.5s
    void ExecuteJoy() {
        ESP_LOGI(TAG, "Emotion: JOY — energetic bouncing and spinning!");

        // Phase 1: Excited bouncing (rapid forward-backward)
        for (int i = 0; i < 4; ++i) {
            Forward(90); vTaskDelay(pdMS_TO_TICKS(120));
            StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(30));
            Backward(90); vTaskDelay(pdMS_TO_TICKS(120));
            StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(30));
        }
        StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(100));

        // Phase 2: Spin 360° (full rotation celebration)
        TurnLeft(95); vTaskDelay(pdMS_TO_TICKS(800));
        StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(150));

        // Phase 3: Fast excited wiggles
        for (int i = 0; i < 6; ++i) {
            TurnLeft(85); vTaskDelay(pdMS_TO_TICKS(100));
            StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(20));
            TurnRight(85); vTaskDelay(pdMS_TO_TICKS(100));
            StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(20));
        }
        StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(100));

        // Phase 4: Victory jump (forward burst + backward settle)
        Forward(100); vTaskDelay(pdMS_TO_TICKS(200));
        StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(80));
        Backward(50); vTaskDelay(pdMS_TO_TICKS(150));
        StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(100));

        // Phase 5: Final happy spin
        TurnRight(90); vTaskDelay(pdMS_TO_TICKS(600));
        StopMotorsOnly();
    }

    // ANGRY: Aggressive charges, sharp turns, agitated shaking — like a bull
    // Total duration: ~5.0s
    void ExecuteAngry() {
        ESP_LOGI(TAG, "Emotion: ANGRY — aggressive charges and shaking!");

        // Phase 1: Agitated shaking (short sharp turns)
        for (int i = 0; i < 5; ++i) {
            TurnLeft(100); vTaskDelay(pdMS_TO_TICKS(80));
            StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(20));
            TurnRight(100); vTaskDelay(pdMS_TO_TICKS(80));
            StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(20));
        }
        StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(200));

        // Phase 2: Charge forward aggressively
        Forward(100); vTaskDelay(pdMS_TO_TICKS(400));
        StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(150));

        // Phase 3: Slam backward (recoil from impact)
        Backward(100); vTaskDelay(pdMS_TO_TICKS(250));
        StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(200));

        // Phase 4: Furious spin (losing temper)
        TurnLeft(100); vTaskDelay(pdMS_TO_TICKS(500));
        StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(100));

        // Phase 5: Second charge + stomp
        Forward(100); vTaskDelay(pdMS_TO_TICKS(350));
        StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(80));
        Backward(80); vTaskDelay(pdMS_TO_TICKS(150));
        StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(100));

        // Phase 6: Final agitated shaking (still angry)
        for (int i = 0; i < 4; ++i) {
            TurnLeft(95); vTaskDelay(pdMS_TO_TICKS(70));
            StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(20));
            TurnRight(95); vTaskDelay(pdMS_TO_TICKS(70));
            StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(20));
        }
        StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(150));

        // Phase 7: Turn away sharply (dismissive)
        TurnRight(90); vTaskDelay(pdMS_TO_TICKS(400));
        StopMotorsOnly();
    }

    // LOVE: Gentle approach, soft nuzzling, slow orbit — tender and warm
    // Total duration: ~5.5s
    void ExecuteLove() {
        ESP_LOGI(TAG, "Emotion: LOVE — gentle approach and nuzzling!");

        // Phase 1: Gentle approach (slowly moving toward)
        Forward(40); vTaskDelay(pdMS_TO_TICKS(600));
        StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(200));

        // Phase 2: Soft nuzzle (gentle side-to-side, like rubbing cheek)
        for (int i = 0; i < 4; ++i) {
            TurnLeft(35); vTaskDelay(pdMS_TO_TICKS(250));
            TurnRight(35); vTaskDelay(pdMS_TO_TICKS(250));
        }
        StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(200));

        // Phase 3: Slow orbit (circling around lovingly)
        // Left wheel forward, right wheel slightly faster = gentle curve
        Drive(30, 0, 45, 0); vTaskDelay(pdMS_TO_TICKS(1000));
        StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(200));

        // Phase 4: Lean in (tiny forward nudge, like a kiss)
        Forward(30); vTaskDelay(pdMS_TO_TICKS(200));
        StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(300));

        // Phase 5: Happy little wiggle (contentment)
        for (int i = 0; i < 3; ++i) {
            TurnLeft(30); vTaskDelay(pdMS_TO_TICKS(150));
            TurnRight(30); vTaskDelay(pdMS_TO_TICKS(150));
        }
        StopMotorsOnly();
    }

    // HATE: Recoil, turn away, shudder with disgust — repulsed
    // Total duration: ~4.5s
    void ExecuteHate() {
        ESP_LOGI(TAG, "Emotion: HATE — recoil and turn away in disgust!");

        // Phase 1: Sudden recoil (jump backward in disgust)
        Backward(100); vTaskDelay(pdMS_TO_TICKS(350));
        StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(200));

        // Phase 2: Shudder (rapid tiny shakes — disgust tremor)
        for (int i = 0; i < 6; ++i) {
            TurnLeft(60); vTaskDelay(pdMS_TO_TICKS(50));
            StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(20));
            TurnRight(60); vTaskDelay(pdMS_TO_TICKS(50));
            StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(20));
        }
        StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(200));

        // Phase 3: Turn away sharply (don't want to look)
        TurnRight(85); vTaskDelay(pdMS_TO_TICKS(500));
        StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(200));

        // Phase 4: Back away more (creating distance)
        Backward(70); vTaskDelay(pdMS_TO_TICKS(400));
        StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(150));

        // Phase 5: Another shudder (still disgusted)
        for (int i = 0; i < 4; ++i) {
            TurnLeft(50); vTaskDelay(pdMS_TO_TICKS(60));
            StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(20));
            TurnRight(50); vTaskDelay(pdMS_TO_TICKS(60));
            StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(20));
        }
        StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(200));

        // Phase 6: Final turn away + retreat
        TurnLeft(80); vTaskDelay(pdMS_TO_TICKS(400));
        StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(100));
        Backward(50); vTaskDelay(pdMS_TO_TICKS(300));
        StopMotorsOnly();
    }

    // CRY: Trembling in place, slow retreat — sad and helpless
    // Total duration: ~5.0s
    // NOTE: Motor speeds kept very low and direction changes slow to avoid brownout
    void ExecuteCry() {
        ESP_LOGI(TAG, "Emotion: CRY — trembling and retreating sadly!");

        // Phase 1: Trembling in place (sobbing) — very gentle, slow direction changes
        for (int i = 0; i < 6; ++i) {
            TurnLeft(20); vTaskDelay(pdMS_TO_TICKS(100));
            StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(30));
            TurnRight(20); vTaskDelay(pdMS_TO_TICKS(100));
            StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(30));
        }
        StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(300));

        // Phase 2: Slow sad retreat
        Backward(25); vTaskDelay(pdMS_TO_TICKS(400));
        StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(200));

        // Phase 3: More trembling (still crying)
        for (int i = 0; i < 4; ++i) {
            TurnLeft(18); vTaskDelay(pdMS_TO_TICKS(120));
            StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(30));
            TurnRight(18); vTaskDelay(pdMS_TO_TICKS(120));
            StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(30));
        }
        StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(400));

        // Phase 4: One last sad backward nudge
        Backward(20); vTaskDelay(pdMS_TO_TICKS(300));
        StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(200));

        // Phase 5: Final weak tremble (exhausted from crying)
        for (int i = 0; i < 3; ++i) {
            TurnLeft(15); vTaskDelay(pdMS_TO_TICKS(130));
            StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(40));
            TurnRight(15); vTaskDelay(pdMS_TO_TICKS(130));
            StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(40));
        }
        StopMotorsOnly();
    }

    // LAUGH: Bouncy wiggles, playful shaking — giggling
    // Total duration: ~4.0s
    void ExecuteLaugh() {
        ESP_LOGI(TAG, "Emotion: LAUGH — bouncy giggling!");

        // Phase 1: Quick bouncy forward-backward (giggle bounces)
        for (int i = 0; i < 3; ++i) {
            Forward(70); vTaskDelay(pdMS_TO_TICKS(100));
            StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(30));
            Backward(70); vTaskDelay(pdMS_TO_TICKS(100));
            StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(30));
        }
        StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(150));

        // Phase 2: Playful side-to-side shaking (laughing hard)
        for (int i = 0; i < 5; ++i) {
            TurnLeft(75); vTaskDelay(pdMS_TO_TICKS(90));
            StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(20));
            TurnRight(75); vTaskDelay(pdMS_TO_TICKS(90));
            StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(20));
        }
        StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(150));

        // Phase 3: Another round of bounces (can't stop laughing)
        for (int i = 0; i < 4; ++i) {
            Forward(60); vTaskDelay(pdMS_TO_TICKS(80));
            StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(20));
            Backward(60); vTaskDelay(pdMS_TO_TICKS(80));
            StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(20));
        }
        StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(100));

        // Phase 4: Final happy wiggle
        for (int i = 0; i < 3; ++i) {
            TurnLeft(50); vTaskDelay(pdMS_TO_TICKS(100));
            StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(20));
            TurnRight(50); vTaskDelay(pdMS_TO_TICKS(100));
            StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(20));
        }
        StopMotorsOnly();
    }

    void ExecuteDance() {
        if (current_dance_style_ == 0) { // wiggle
            for (int i = 0; i < 4; ++i) {
                TurnLeft(kDefaultSpeedPercent);
                vTaskDelay(pdMS_TO_TICKS(150));
                TurnRight(kDefaultSpeedPercent);
                vTaskDelay(pdMS_TO_TICKS(150));
            }
        } else if (current_dance_style_ == 1) { // step
            for (int i = 0; i < 3; ++i) {
                Forward(kDefaultSpeedPercent);
                vTaskDelay(pdMS_TO_TICKS(150));
                Backward(kDefaultSpeedPercent);
                vTaskDelay(pdMS_TO_TICKS(150));
            }
            TurnLeft(kDefaultSpeedPercent);
            vTaskDelay(pdMS_TO_TICKS(1000));
        } else if (current_dance_style_ == 2) { // tango
            ESP_LOGI(TAG, "Starting tango dance routine!");
            
            // Create audio task at priority 3 (above OpusCodecTask=2, below audio_output=4)
            // so it can push packets into decode queue without being starved,
            // while DanceTask (priority 5) still preempts during motor commands.
            xTaskCreate(PlaySoundTask, "tango_audio", 4096, nullptr, 3, nullptr);
            
            vTaskDelay(pdMS_TO_TICKS(180));
            ESP_LOGI(TAG, "DanceTask starting first movement!");
            
            for (int i = 0; i < 4; ++i) {
                Forward(kDefaultSpeedPercent); vTaskDelay(pdMS_TO_TICKS(300));
                StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(80));
                Backward(kDefaultSpeedPercent); vTaskDelay(pdMS_TO_TICKS(400));
                StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(i < 3 ? 80 : 120));
            }
            
            for (int i = 0; i < 4; ++i) {
                TurnLeft(kDefaultSpeedPercent); vTaskDelay(pdMS_TO_TICKS(440));
                StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(70));
                TurnRight(kDefaultSpeedPercent); vTaskDelay(pdMS_TO_TICKS(440));
                StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(i < 3 ? 70 : 120));
            }
            
            for (int i = 0; i < 3; ++i) {
                TurnLeft(kDefaultSpeedPercent); vTaskDelay(pdMS_TO_TICKS(320));
                StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(80));
                TurnRight(kDefaultSpeedPercent); vTaskDelay(pdMS_TO_TICKS(320));
                StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(i < 2 ? 80 : 180));
            }
            
            for (int i = 0; i < 2; ++i) {
                Forward(kDefaultSpeedPercent); vTaskDelay(pdMS_TO_TICKS(220));
                StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(70));
                Backward(kDefaultSpeedPercent); vTaskDelay(pdMS_TO_TICKS(280));
                StopMotorsOnly(); vTaskDelay(pdMS_TO_TICKS(i == 0 ? 70 : 120));
            }
        } else if (current_dance_style_ == 3) { // joy
            PlayEmotionSound();
            vTaskDelay(pdMS_TO_TICKS(100));
            ExecuteJoy();
            WaitForAudioAndSilence();
        } else if (current_dance_style_ == 4) { // angry
            PlayEmotionSound();
            vTaskDelay(pdMS_TO_TICKS(100));
            ExecuteAngry();
            WaitForAudioAndSilence();
        } else if (current_dance_style_ == 5) { // love
            PlayEmotionSound();
            vTaskDelay(pdMS_TO_TICKS(100));
            ExecuteLove();
            WaitForAudioAndSilence();
        } else if (current_dance_style_ == 6) { // hate
            PlayEmotionSound();
            vTaskDelay(pdMS_TO_TICKS(100));
            ExecuteHate();
            WaitForAudioAndSilence();
        } else if (current_dance_style_ == 7) { // cry
            PlayEmotionSound();
            vTaskDelay(pdMS_TO_TICKS(100));
            ExecuteCry();
            WaitForAudioAndSilence();
        } else if (current_dance_style_ == 8) { // laugh
            PlayEmotionSound();
            vTaskDelay(pdMS_TO_TICKS(100));
            ExecuteLaugh();
            WaitForAudioAndSilence();
        }
        StopMotorsOnly();
    }

    void PlayEmotionSound() {
        if (current_emotion_sound_ != nullptr) {
            xTaskCreate(PlayEmotionSoundTask, "emo_audio", 4096, this, 3, nullptr);
        }
    }

    // Wait for audio to finish after motor completes, then silence AI
    void WaitForAudioAndSilence() {
        auto& audio_service = Application::GetInstance().GetAudioService();
        audio_service.WaitForPlaybackQueueEmpty();
        // Extra delay to let I2S buffer drain completely
        vTaskDelay(pdMS_TO_TICKS(500));
        SilenceAfterEmotion();
    }

    // Play emotion sound AFTER motor finishes, wait for it to complete, then silence AI
    void PlayEmotionSoundAndWait() {
        if (current_emotion_sound_ != nullptr) {
            // Play sound synchronously on this task (motor already stopped)
            auto& app = Application::GetInstance();
            auto& audio_service = app.GetAudioService();
            audio_service.ResetDecoder();
            audio_service.PlaySound(*current_emotion_sound_);
            // Wait for audio to finish playing
            audio_service.WaitForPlaybackQueueEmpty();
            // Extra delay to let I2S buffer drain completely through speaker
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        // Silence AI — don't let it speak after emotion
        SilenceAfterEmotion();
    }

    // Wait for audio playback to finish before allowing AI to speak
    void WaitForAudioFinish() {
        auto& audio_service = Application::GetInstance().GetAudioService();
        audio_service.WaitForPlaybackQueueEmpty();
    }

    // Silence the AI after emotion — abort any pending speech
    void SilenceAfterEmotion() {
        auto& app = Application::GetInstance();
        app.Schedule([&app]() {
            app.AbortSpeaking(kAbortReasonNone);
        });
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    std::string RunDance(const std::string& style) {
        std::lock_guard<std::mutex> lock(mutex_);
        ESP_LOGI(TAG, "dance=%s", style.c_str());

        // Stop any audio playback before motor dance to prevent brownout
        auto& audio_service = Application::GetInstance().GetAudioService();
        audio_service.ResetDecoder();

        Stop();
        current_emotion_sound_ = nullptr;

        if (style == "wiggle") {
            current_dance_style_ = 0;
        } else if (style == "step") {
            current_dance_style_ = 1;
        } else if (style == "tango") {
            current_dance_style_ = 2;
        } else if (style == "joy") {
            current_dance_style_ = 3;
            current_emotion_sound_ = &Lang::Sounds::OGG_EMOTION_JOY;
        } else if (style == "angry") {
            current_dance_style_ = 4;
            current_emotion_sound_ = &Lang::Sounds::OGG_EMOTION_ANGRY;
        } else if (style == "love") {
            current_dance_style_ = 5;
            current_emotion_sound_ = &Lang::Sounds::OGG_EMOTION_LOVE;
        } else if (style == "hate") {
            current_dance_style_ = 6;
            current_emotion_sound_ = &Lang::Sounds::OGG_EMOTION_HATE;
        } else if (style == "cry") {
            current_dance_style_ = 7;
            current_emotion_sound_ = &Lang::Sounds::OGG_EMOTION_CRY;
        } else if (style == "laugh") {
            current_dance_style_ = 8;
            current_emotion_sound_ = &Lang::Sounds::OGG_EMOTION_LAUGH;
        } else {
            return "unknown dance style";
        }

        xTaskCreate(DanceTask, "dance_task", 4096, this, 5, &dance_task_handle_);
        return "ok";
    }

    void AddTimedTool(const std::string& name, const std::string& description,
                      const std::string& motion, int default_duration_ms) {
        auto& mcp_server = McpServer::GetInstance();
        mcp_server.AddTool(
            name,
            description,
            PropertyList({
                Property("duration_ms", kPropertyTypeInteger, default_duration_ms, kMinDurationMs, kMaxDurationMs),
                Property("speed_percent", kPropertyTypeInteger, kDefaultSpeedPercent, 30, 100),
            }),
            [this, motion](const PropertyList& properties) -> ReturnValue {
                return RunTimedMotion(motion,
                    properties["duration_ms"].value<int>(),
                    properties["speed_percent"].value<int>());
            });
    }

    void RegisterTools() {
        auto& mcp_server = McpServer::GetInstance();
        mcp_server.AddTool(
            "self.car.stop",
            "Stop the car immediately. Use this whenever the user asks the car to stop.",
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                std::lock_guard<std::mutex> lock(mutex_);
                Stop();
                return "ok";
            });

        AddTimedTool("self.car.forward",
            "Move the car forward for a bounded short duration, then stop.",
            "forward", kDefaultDriveMs);
        AddTimedTool("self.car.backward",
            "Move the car backward for a bounded short duration, then stop.",
            "backward", kDefaultDriveMs);
        AddTimedTool("self.car.turn_left",
            "Turn the car left for a bounded short duration, then stop.",
            "turn_left", kDefaultTurnMs);
        AddTimedTool("self.car.turn_right",
            "Turn the car right for a bounded short duration, then stop.",
            "turn_right", kDefaultTurnMs);

        AddTimedTool("self.car.nudge_forward",
            "Move the car forward only a tiny amount for safe tabletop testing.",
            "forward", kNudgeDriveMs);
        AddTimedTool("self.car.nudge_backward",
            "Move the car backward only a tiny amount for safe tabletop testing.",
            "backward", kNudgeDriveMs);
        AddTimedTool("self.car.nudge_left",
            "Turn the car left only a tiny amount for safe tabletop testing.",
            "turn_left", kNudgeTurnMs);
        AddTimedTool("self.car.nudge_right",
            "Turn the car right only a tiny amount for safe tabletop testing.",
            "turn_right", kNudgeTurnMs);

        AddTimedTool("self.car.rotate",
            "Rotate the car around continuously. Use this when the user asks to spin or rotate.",
            "turn_left", 1200);

        mcp_server.AddTool(
            "self.car.dance",
            "Perform a dance or express an emotion through movement. "
            "Available styles: 'wiggle', 'step', 'tango' (dances), "
            "'joy' (happy/excited), 'angry' (furious/mad), 'love' (affectionate/tender), 'hate' (disgusted/repulsed), "
            "'cry' (sad/crying), 'laugh' (giggling/funny). "
            "Use emotion styles when the robot needs to express feelings.",
            PropertyList({
                Property("style", kPropertyTypeString, "wiggle"),
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                return RunDance(properties["style"].value<std::string>());
            });
    }
};

#endif // _XIAOZHI_CAR_MINI_S3_MOTOR_CONTROLLER_H_
