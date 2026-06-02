#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/oled_display.h"
#include "display/oled_face_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "motor_controller.h"
#include "led/single_led.h"
#include "mcp_server.h"
#include "control_car/child/child_command_server.h"
#include "control_car/child/child_emotion_controller.h"

#include <esp_log.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <nvs_flash.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#ifdef SH1106
#include <esp_lcd_panel_sh1106.h>
#endif

#define TAG "XiaozhiCarMiniS3"

class XiaozhiCarMiniS3Board : public WifiBoard {
private:
    i2c_master_bus_handle_t display_i2c_bus_;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    Display* display_ = nullptr;
    Button boot_button_;
    control_car::ChildCommandServer child_command_server_;
    bool boot_tone_scheduled_ = false;

    static void PlayTone(AudioCodec* codec, int frequency_hz, int duration_ms, int volume_percent) {
        if (codec == nullptr) {
            return;
        }

        frequency_hz = std::clamp(frequency_hz, 120, 3000);
        duration_ms = std::clamp(duration_ms, 120, 2000);
        volume_percent = std::clamp(volume_percent, 1, 100);

        int previous_volume = codec->output_volume();
        bool was_output_enabled = codec->output_enabled();

        if (previous_volume != volume_percent) {
            codec->SetOutputVolume(volume_percent);
        }
        codec->EnableOutput(true);

        const int sample_rate = codec->output_sample_rate();
        const int sample_count = sample_rate * duration_ms / 1000;
        const int fade_samples = std::min(sample_rate / 80, sample_count / 2);
        constexpr double kPi = 3.14159265358979323846;
        std::vector<int16_t> samples(sample_count);

        for (int i = 0; i < sample_count; ++i) {
            double gain = 1.0;
            if (fade_samples > 0 && i < fade_samples) {
                gain = static_cast<double>(i) / fade_samples;
            } else if (fade_samples > 0 && i >= sample_count - fade_samples) {
                gain = static_cast<double>(sample_count - i - 1) / fade_samples;
            }
            double phase = 2.0 * kPi * frequency_hz * i / sample_rate;
            samples[i] = static_cast<int16_t>(std::sin(phase) * 12000.0 * gain);
        }

        codec->OutputData(samples);

        if (!was_output_enabled) {
            codec->EnableOutput(false);
        }
        if (previous_volume != volume_percent) {
            codec->SetOutputVolume(previous_volume);
        }
    }

    static void BootToneTask(void* arg) {
        auto* codec = static_cast<AudioCodec*>(arg);
        vTaskDelay(pdMS_TO_TICKS(2500));
        ESP_LOGI(TAG, "Playing startup speaker test tone");
        PlayTone(codec, 880, 450, std::max(codec->output_volume(), 75));
        vTaskDelete(nullptr);
    }

    void InitializeDisplayI2c() {
        i2c_master_bus_config_t bus_config = {
            .i2c_port = (i2c_port_t)0,
            .sda_io_num = DISPLAY_SDA_PIN,
            .scl_io_num = DISPLAY_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &display_i2c_bus_));
    }

    void InitializeSsd1306Display() {
        esp_lcd_panel_io_i2c_config_t io_config = {
            .dev_addr = 0x3C,
            .on_color_trans_done = nullptr,
            .user_ctx = nullptr,
            .control_phase_bytes = 1,
            .dc_bit_offset = 6,
            .lcd_cmd_bits = 8,
            .lcd_param_bits = 8,
            .flags = {
                .dc_low_on_data = 0,
                .disable_control_phase = 0,
            },
            .scl_speed_hz = 400 * 1000,
        };

        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c_v2(display_i2c_bus_, &io_config, &panel_io_));

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = -1;
        panel_config.bits_per_pixel = 1;

        esp_lcd_panel_ssd1306_config_t ssd1306_config = {
            .height = static_cast<uint8_t>(DISPLAY_HEIGHT),
        };
        panel_config.vendor_config = &ssd1306_config;

#ifdef SH1106
        ESP_ERROR_CHECK(esp_lcd_new_panel_sh1106(panel_io_, &panel_config, &panel_));
#else
        ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(panel_io_, &panel_config, &panel_));
#endif

        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        if (esp_lcd_panel_init(panel_) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize display");
            display_ = new NoDisplay();
            return;
        }

        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_, false));
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

        display_ = new OledFaceDisplay(panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });

        boot_button_.OnLongPress([this]() {
            ESP_LOGI(TAG, "Boot button long pressed, erasing NVS flash...");
            nvs_flash_erase();
            esp_restart();
        });
    }

    void InitializeTools() {
        static MotorController motor(MOTOR_LF_GPIO, MOTOR_LB_GPIO, MOTOR_RF_GPIO, MOTOR_RB_GPIO);
        static control_car::ChildEmotionController child_emotion_controller;
        control_car::ChildEmotionController::SetMotionRunner([](const std::string& style) {
            return motor.Dance(style);
        });
        auto& mcp_server = McpServer::GetInstance();
        mcp_server.AddTool("self.audio_speaker.test_tone",
            "Play a short sine tone on the MAX98357 speaker to verify speaker wiring and I2S pins.",
            PropertyList({
                Property("frequency_hz", kPropertyTypeInteger, 880, 120, 3000),
                Property("duration_ms", kPropertyTypeInteger, 600, 120, 2000),
                Property("volume_percent", kPropertyTypeInteger, 80, 1, 100),
            }),
            [](const PropertyList& properties) -> ReturnValue {
                auto& board = Board::GetInstance();
                auto codec = board.GetAudioCodec();
                int frequency_hz = properties["frequency_hz"].value<int>();
                int duration_ms = properties["duration_ms"].value<int>();
                int volume_percent = properties["volume_percent"].value<int>();

                PlayTone(codec, frequency_hz, duration_ms, volume_percent);
                return std::string("Speaker test tone played");
            });
    }

public:
    XiaozhiCarMiniS3Board() :
        boot_button_(BOOT_BUTTON_GPIO) {
        InitializeDisplayI2c();
        InitializeSsd1306Display();
        InitializeButtons();
        InitializeTools();
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT,
            AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
        if (!boot_tone_scheduled_) {
            boot_tone_scheduled_ = true;
            xTaskCreate(BootToneTask, "speaker_test", 4096, &audio_codec, 2, nullptr);
        }
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual void StartNetwork() override {
        WifiBoard::StartNetwork();
        child_command_server_.Start();
    }
};

DECLARE_BOARD(XiaozhiCarMiniS3Board);
