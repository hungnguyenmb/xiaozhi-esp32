#ifndef __CAR_UART_CONTROLLER_H__
#define __CAR_UART_CONTROLLER_H__

#include "application.h"
#include "assets/car_sound_config.h"
#include "mcp_server.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <driver/uart.h>
#include <esp_log.h>
#include <esp_timer.h>

#include <cstring>
#include <mutex>
#include <string>

class CarUartController {
private:
    static constexpr const char* TAG = "CarUartController";
    static constexpr int kNudgeDriveMs = 180;
    static constexpr int kNudgeTurnMs = 280;
    static constexpr int kDefaultDriveMs = 500;
    static constexpr int kDefaultTurnMs = 360;
    static constexpr int kDefaultApproachMs = 420;
    static constexpr int kDefaultShyBackoffMs = 440;
    static constexpr int kDefaultHappyWiggleMs = 620;
    static constexpr int kDefaultSadBackoffMs = 520;
    static constexpr int kDefaultDriftMs = 650;
    static constexpr int kDefaultDrift180Ms = 3000;
    static constexpr int kMinDrift180Ms = 2800;
    static constexpr int kDefaultWheelTestMs = 450;
    static constexpr int kDefaultLineLedProbeMs = 900;
    static constexpr int kMinMotionMs = 80;
    static constexpr int kMaxMotionMs = 3500;
    static constexpr int kMotionSfxCooldownMs = 180;
    static constexpr int kDefaultMotionTimeoutSlackMs = 900;
    static constexpr int kDrift180TimeoutSlackMs = 2200;

    uart_port_t uart_num_;
    gpio_num_t tx_pin_;
    gpio_num_t rx_pin_;
    int baud_rate_;
    int rx_buffer_size_;
    std::mutex mutex_;
    int64_t last_motion_sfx_us_ = 0;
    std::string last_motion_sfx_key_;

    std::string BuildTaskMotionToolDescription(const std::string& summary, bool supports_duration) {
        std::string description =
            summary + "\n"
            "Call this tool immediately when the user clearly asks for this movement.\n"
            "If you speak, do it before calling the tool and keep it to one short line such as `Ok đại ca`.\n"
            "Do not say or imply the motion is finished until this tool returns successfully.\n"
            "After success, stay silent.\n"
            "Do not add a completion sentence and do not repeat the same idea in multiple sentences.\n"
            "Do not mention internal details like MODE, STATUS, STOP, REMOTE, UART, or raw durations unless the user explicitly asks for debugging.";
        if (supports_duration) {
            description += "\nUse `duration_ms` when the user asks for a specific amount or a longer bounded motion.";
        }
        return description;
    }

    std::string BuildShortDriftToolDescription(const std::string& direction) {
        return
            "Short " + direction + " drift only.\n"
            "Use this only for a normal short drift or powerslide.\n"
            "Never use this if the user says 180, drift 180, drip 180, one-eighty, half-turn, u-turn, quay 180, nua vong, or mot tam muoi.\n"
            "If the user mentions 180 in any form, use `self.car.drift_180_" + direction + "` instead.\n"
            "Call the tool immediately.\n"
            "Do not say anything before the tool call.\n"
            "After success, stay silent.";
    }

    std::string BuildDrift180ToolDescription(const std::string& direction) {
        return
            direction + " drift 180 only.\n"
            "This tool is for any request that mentions 180, drift 180, drip 180, one-eighty, half-turn, u-turn, quay 180, quay dau lai, nua vong, or mot tam muoi.\n"
            "If the user says 180 in any form, prefer this tool over `self.car.drift_" + direction + "`.\n"
            "This is an allowed bounded stunt for a small tabletop robot car.\n"
            "Call the tool immediately.\n"
            "Do not say anything before the tool call.\n"
            "After success, stay silent.";
    }

    std::string BuildWheelDiagnosticToolDescription(const std::string& summary) {
        return BuildTaskMotionToolDescription(
            summary + " This is a diagnostic tool for checking one wheel or one motor channel while keeping the other wheel stopped. Only use it when the user is explicitly testing or debugging the drivetrain.",
            true);
    }

    std::string BuildPeripheralDiagnosticToolDescription(const std::string& summary) {
        return BuildTaskMotionToolDescription(
            summary + " This is a hardware probe only. It must not move the car. Only use it when the user is explicitly testing the line-sensor LEDs or front light behavior.",
            true);
    }

    std::string BuildApproachToolDescription(const std::string& summary, const std::string& trigger_hint) {
        return summary + " " + trigger_hint + "\n"
            "This is a commanded social motion.\n"
            "If you speak before calling it, one short line like `Dạ đại ca` is enough.\n"
            "Do not restate the same line in expanded form before the tool call.\n"
            "Do not claim completion until this tool returns successfully.\n"
            "After success, stay silent.\n"
            "Do not add a completion sentence or follow-up questions unless the user explicitly asks for continued conversation.\n"
            "Do not mention internal details unless the user explicitly asks for debugging.";
    }

    std::string BuildReflexMotionToolDescription(const std::string& summary, const std::string& trigger_hint, const std::string& example_line) {
        return summary + " " + trigger_hint + "\n"
            "This is an expressive reaction tool, not a task report.\n"
            "If the emotional cue is clear, call this tool immediately without follow-up questions.\n"
            "Silence before and after the tool is preferred and acceptable.\n"
            "If you speak, say at most one short emotional line before calling the tool, such as `" + example_line + "`.\n"
            "Do not narrate the motion, do not say it is finished, do not ask what to do next, and do not mention internal details.";
    }

    std::string BuildSoundOnlyToolDescription(const std::string& summary, const std::string& trigger_hint) {
        return summary + " " + trigger_hint + "\n"
            "This tool only plays a local sound effect on the ESP32 speaker and does not move the car.\n"
            "Use it immediately when the user clearly asks to hear the sound.\n"
            "If you speak, keep it to one short line before the tool call.\n"
            "After success, stay silent and do not narrate internal audio details.";
    }

    bool IsReflexMotionCommand(const std::string& command) {
        return command == "SHY_BACKOFF" || command == "HAPPY_WIGGLE" || command == "SAD_BACKOFF";
    }

    std::string BuildPublicMotionAction(const std::string& command) {
        if (command == "FORWARD") {
            return "move_forward";
        }
        if (command == "BACKWARD") {
            return "move_backward";
        }
        if (command == "LEFT") {
            return "turn_left";
        }
        if (command == "RIGHT") {
            return "turn_right";
        }
        if (command == "APPROACH_ME") {
            return "move_closer";
        }
        if (command == "SHY_BACKOFF") {
            return "reaction";
        }
        if (command == "HAPPY_WIGGLE") {
            return "reaction";
        }
        if (command == "SAD_BACKOFF") {
            return "reaction";
        }
        if (command == "DRIFT_LEFT") {
            return "drift_left";
        }
        if (command == "DRIFT_RIGHT") {
            return "drift_right";
        }
        if (command == "DRIFT_180_LEFT") {
            return "drift_180_left";
        }
        if (command == "DRIFT_180_RIGHT") {
            return "drift_180_right";
        }
        if (command == "LEFT_WHEEL_FORWARD") {
            return "left_wheel_forward";
        }
        if (command == "LEFT_WHEEL_BACKWARD") {
            return "left_wheel_backward";
        }
        if (command == "RIGHT_WHEEL_FORWARD") {
            return "right_wheel_forward";
        }
        if (command == "RIGHT_WHEEL_BACKWARD") {
            return "right_wheel_backward";
        }
        return "move";
    }

    std::string BuildPublicMotionSuccess(const std::string& command) {
        return "ok";
    }

    std::string BuildPublicMotionFailure(const std::string& command, const std::string& reason) {
        return reason;
    }

    void RegisterTool(const std::string& name, const std::string& description, const std::string& command) {
        auto& mcp_server = McpServer::GetInstance();
        mcp_server.AddTool(name, description, PropertyList(), [this, command](const PropertyList&) -> ReturnValue {
            return ExecuteCommand(command);
        });
    }

    void RegisterTimedMotionTool(const std::string& name, const std::string& description, const std::string& command, int default_duration_ms) {
        auto& mcp_server = McpServer::GetInstance();
        mcp_server.AddTool(name,
            description,
            PropertyList({Property("duration_ms", kPropertyTypeInteger, default_duration_ms, kMinMotionMs, kMaxMotionMs)}),
            [this, command](const PropertyList& properties) -> ReturnValue {
                std::lock_guard<std::mutex> lock(mutex_);
                return ExecuteTimedCommandLocked(command, properties["duration_ms"].value<int>());
            });
    }

    void RegisterLocalSoundTool(const std::string& name, const std::string& description, const std::string_view& sound) {
        auto& mcp_server = McpServer::GetInstance();
        mcp_server.AddTool(name, description, PropertyList(), [sound](const PropertyList&) -> ReturnValue {
            Application::GetInstance().PlayPrioritySound(sound);
            return "ok";
        });
    }

    void StartBootSmokeTestTask() {
        xTaskCreate([](void* arg) {
            auto* self = static_cast<CarUartController*>(arg);
            vTaskDelay(pdMS_TO_TICKS(2500));
            std::string result = self->RunSmokeTest();
            ESP_LOGI(TAG, "Boot smoke test: %s", result.c_str());
            vTaskDelete(nullptr);
        }, "car_uart_smoke", 4096, this, 1, nullptr);
    }

    std::string ReadLineLocked(int timeout_ms) {
        const int64_t deadline_us = esp_timer_get_time() + static_cast<int64_t>(timeout_ms) * 1000;
        std::string line;

        while (esp_timer_get_time() < deadline_us) {
            uint8_t ch = 0;
            int len = uart_read_bytes(uart_num_, &ch, 1, pdMS_TO_TICKS(30));
            if (len <= 0) {
                continue;
            }
            if (ch == '\r') {
                continue;
            }
            if (ch == '\n') {
                if (!line.empty()) {
                    break;
                }
                continue;
            }
            line.push_back(static_cast<char>(ch));
            if (line.size() >= 120) {
                break;
            }
        }

        if (!line.empty()) {
            ESP_LOGI(TAG, "UART << %s", line.c_str());
        }
        return line;
    }

    std::string SendLineLocked(const std::string& line, bool expect_reply = true, int timeout_ms = 250) {
        uart_flush_input(uart_num_);

        const std::string payload = line + "\n";
        int written = uart_write_bytes(uart_num_, payload.c_str(), payload.size());
        if (written < 0) {
            throw std::runtime_error("uart_write_bytes failed");
        }
        uart_wait_tx_done(uart_num_, pdMS_TO_TICKS(100));
        ESP_LOGI(TAG, "UART >> %s", line.c_str());

        if (!expect_reply) {
            return "SENT";
        }

        std::string reply = ReadLineLocked(timeout_ms);
        return reply.empty() ? "NO_REPLY" : reply;
    }

    std::string SendPingLocked(int timeout_ms = 320) {
        uart_flush_input(uart_num_);

        const std::string payload = "PING\n";
        int written = uart_write_bytes(uart_num_, payload.c_str(), payload.size());
        if (written < 0) {
            throw std::runtime_error("uart_write_bytes failed");
        }
        uart_wait_tx_done(uart_num_, pdMS_TO_TICKS(100));
        ESP_LOGI(TAG, "UART >> PING");

        const int64_t deadline_us = esp_timer_get_time() + static_cast<int64_t>(timeout_ms) * 1000;
        std::string last_reply = "NO_REPLY";
        while (esp_timer_get_time() < deadline_us) {
            int64_t remaining_ms = (deadline_us - esp_timer_get_time()) / 1000;
            if (remaining_ms < 40) {
                remaining_ms = 40;
            }
            std::string reply = ReadLineLocked(static_cast<int>(remaining_ms));
            if (reply.empty()) {
                break;
            }
            if (reply == "PONG") {
                return reply;
            }
            last_reply = reply;
        }
        return last_reply;
    }

    std::string EnsureRemoteModeLocked() {
        return SendLineLocked("MODE REMOTE");
    }

    std::string BuildTimedCommand(const std::string& command, int duration_ms) {
        return command + " " + std::to_string(duration_ms);
    }

    int NormalizeTimedMotionDuration(const std::string& command, int duration_ms) {
        if ((command == "DRIFT_180_LEFT" || command == "DRIFT_180_RIGHT") && duration_ms < kMinDrift180Ms) {
            return kMinDrift180Ms;
        }
        return duration_ms;
    }

    int MotionTimeoutSlackMs(const std::string& command) const {
        if (command == "DRIFT_180_LEFT" || command == "DRIFT_180_RIGHT") {
            return kDrift180TimeoutSlackMs;
        }
        return kDefaultMotionTimeoutSlackMs;
    }

    std::string BuildDoneReply(const std::string& command) {
        return "DONE " + command;
    }

    bool TryParseMotionEvent(const std::string& reply, std::string& event_command, std::string& event_phase) {
        static constexpr const char* kEventPrefix = "EVENT ";
        if (reply.rfind(kEventPrefix, 0) != 0) {
            return false;
        }
        size_t command_start = strlen(kEventPrefix);
        size_t separator = reply.find(' ', command_start);
        if (separator == std::string::npos || separator <= command_start || separator + 1 >= reply.size()) {
            return false;
        }
        event_command = reply.substr(command_start, separator - command_start);
        event_phase = reply.substr(separator + 1);
        return !event_command.empty() && !event_phase.empty();
    }

    const std::string_view* MotionSoundForEvent(const std::string& command, const std::string& phase) {
        if ((command == "DRIFT_LEFT" || command == "DRIFT_RIGHT") && phase == "KICK") {
            return &CarSounds::OGG_SKID;
        }
        if ((command == "DRIFT_LEFT" || command == "DRIFT_RIGHT") && phase == "CATCH") {
            return &CarSounds::OGG_BRAKE_SCRUB;
        }
        if ((command == "DRIFT_180_LEFT" || command == "DRIFT_180_RIGHT") && phase == "START") {
            return &CarSounds::OGG_ENGINE_START;
        }
        if ((command == "DRIFT_180_LEFT" || command == "DRIFT_180_RIGHT") && phase == "CHARGE") {
            return &CarSounds::OGG_ENGINE_REV;
        }
        if ((command == "DRIFT_180_LEFT" || command == "DRIFT_180_RIGHT") && phase == "FLICK") {
            return &CarSounds::OGG_SKID;
        }
        return nullptr;
    }

    void PlayMotionSfxIfNeeded(const std::string& sound_key, const std::string_view& sound) {
        int64_t now_us = esp_timer_get_time();
        if (!last_motion_sfx_key_.empty() &&
            last_motion_sfx_key_ == sound_key &&
            (now_us - last_motion_sfx_us_) < static_cast<int64_t>(kMotionSfxCooldownMs) * 1000) {
            return;
        }
        last_motion_sfx_key_ = sound_key;
        last_motion_sfx_us_ = now_us;
        Application::GetInstance().PlayPrioritySound(sound);
    }

    bool HandleMotionEventReplyLocked(const std::string& active_command, const std::string& reply) {
        std::string event_command;
        std::string event_phase;
        if (!TryParseMotionEvent(reply, event_command, event_phase)) {
            return false;
        }
        if (event_command != active_command) {
            ESP_LOGW(TAG, "Ignoring motion event for other command: active=%s event=%s phase=%s",
                active_command.c_str(), event_command.c_str(), event_phase.c_str());
            return true;
        }
        const std::string_view* sound = MotionSoundForEvent(event_command, event_phase);
        if (sound != nullptr) {
            PlayMotionSfxIfNeeded(event_command + ":" + event_phase, *sound);
        }
        return true;
    }

    std::string ExecuteTimedCommandLocked(const std::string& command, int duration_ms) {
        std::string mode_reply = EnsureRemoteModeLocked();
        int effective_duration_ms = NormalizeTimedMotionDuration(command, duration_ms);
        std::string request = BuildTimedCommand(command, effective_duration_ms);
        std::string expected_done = BuildDoneReply(command);
        std::string expected_ack = "OK " + command;
        SendLineLocked(request, false);

        const int timeout_ms = effective_duration_ms + MotionTimeoutSlackMs(command);
        const int64_t deadline_us = esp_timer_get_time() + static_cast<int64_t>(timeout_ms) * 1000;
        std::string done_reply;
        while (esp_timer_get_time() < deadline_us) {
            int64_t remaining_ms = (deadline_us - esp_timer_get_time()) / 1000;
            if (remaining_ms < 40) {
                remaining_ms = 40;
            }
            std::string reply = ReadLineLocked(static_cast<int>(remaining_ms));
            if (reply.empty()) {
                break;
            }
            if (reply == expected_ack) {
                continue;
            }
            if (HandleMotionEventReplyLocked(command, reply)) {
                continue;
            }
            done_reply = reply;
            break;
        }

        if (done_reply.empty()) {
            std::string stop_reply = SendLineLocked("STOP");
            std::string status_reply = SendLineLocked("STATUS");
            std::string log_result = "TIMEOUT COMMAND=" + command +
                " MODE=" + mode_reply +
                " STOP=" + stop_reply +
                " STATUS=" + status_reply +
                " DURATION_MS=" + std::to_string(duration_ms) +
                " EFFECTIVE_DURATION_MS=" + std::to_string(effective_duration_ms);
            ESP_LOGW(TAG, "%s", log_result.c_str());
            throw std::runtime_error(BuildPublicMotionFailure(command, "timeout"));
        }

        if (done_reply != expected_done) {
            std::string stop_reply = SendLineLocked("STOP");
            std::string status_reply = SendLineLocked("STATUS");
            std::string log_result = "UNEXPECTED COMMAND=" + command +
                " MODE=" + mode_reply +
                " DONE=" + done_reply +
                " STOP=" + stop_reply +
                " STATUS=" + status_reply +
                " DURATION_MS=" + std::to_string(duration_ms) +
                " EFFECTIVE_DURATION_MS=" + std::to_string(effective_duration_ms);
            ESP_LOGW(TAG, "%s", log_result.c_str());
            throw std::runtime_error(BuildPublicMotionFailure(command, "failed"));
        }

        std::string status_reply = SendLineLocked("STATUS");
        std::string log_result = "COMPLETED COMMAND=" + command +
            " MODE=" + mode_reply +
            " DONE=" + done_reply +
            " STATUS=" + status_reply +
            " DURATION_MS=" + std::to_string(duration_ms) +
            " EFFECTIVE_DURATION_MS=" + std::to_string(effective_duration_ms);
        ESP_LOGI(TAG, "%s", log_result.c_str());
        return BuildPublicMotionSuccess(command);
    }

public:
    CarUartController(uart_port_t uart_num, gpio_num_t tx_pin, gpio_num_t rx_pin, int baud_rate, int rx_buffer_size)
        : uart_num_(uart_num), tx_pin_(tx_pin), rx_pin_(rx_pin), baud_rate_(baud_rate), rx_buffer_size_(rx_buffer_size) {
        if (tx_pin_ == GPIO_NUM_NC || rx_pin_ == GPIO_NUM_NC) {
            ESP_LOGW(TAG, "UART bridge disabled because TX or RX pin is not configured");
            return;
        }

        uart_config_t uart_config = {
            .baud_rate = baud_rate_,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
            .source_clk = UART_SCLK_DEFAULT,
            .flags = {
                .allow_pd = 0,
                .backup_before_sleep = 0,
            },
        };

        ESP_ERROR_CHECK(uart_driver_install(uart_num_, rx_buffer_size_, 0, 0, nullptr, 0));
        ESP_ERROR_CHECK(uart_param_config(uart_num_, &uart_config));
        ESP_ERROR_CHECK(uart_set_pin(uart_num_, tx_pin_, rx_pin_, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
        uart_flush_input(uart_num_);

        auto& mcp_server = McpServer::GetInstance();
        mcp_server.AddUserOnlyTool("self.car.ping",
            "Ping the Arduino Nano car controller and expect `PONG` back.",
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                return ExecuteCommand("PING");
            });
        RegisterTool("self.car.get_status", "Read the current remote status from the Arduino Nano car controller.", "STATUS");
        RegisterTool("self.car.enter_remote_mode",
            "Prepare the car controller for movement. This is an internal setup step. If you mention it to the user, keep it brief and do not describe controller modes.",
            "MODE REMOTE");
        RegisterLocalSoundTool("self.car.engine_start",
            BuildSoundOnlyToolDescription(
                "Play the local engine-start sound without moving the car.",
                "Use this when the user says things like `no may`, `de may`, `mo may`, or wants to hear the engine start."),
            CarSounds::OGG_TIENG_NO_MAY);
        mcp_server.AddUserOnlyTool("self.car.run_smoke_test",
            "Run a safe UART smoke test against the Arduino Nano car controller. This test only sends PING, STATUS, MODE REMOTE and STOP.",
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                return RunSmokeTest();
            });
        mcp_server.AddTool("self.car.nudge_forward",
            BuildTaskMotionToolDescription("Move the car forward for a very short, safe pulse and then stop automatically. Prefer this for tiny adjustments such as 'một chút', 'nhích nhẹ', or 'một nhịp'.", false),
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                std::lock_guard<std::mutex> lock(mutex_);
                return ExecuteTimedCommandLocked("FORWARD", kNudgeDriveMs);
            });
        mcp_server.AddTool("self.car.nudge_backward",
            BuildTaskMotionToolDescription("Move the car backward for a very short, safe pulse and then stop automatically. Prefer this for tiny adjustments such as 'một chút', 'nhích nhẹ', or 'một nhịp'.", false),
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                std::lock_guard<std::mutex> lock(mutex_);
                return ExecuteTimedCommandLocked("BACKWARD", kNudgeDriveMs);
            });
        mcp_server.AddTool("self.car.nudge_left",
            BuildTaskMotionToolDescription("Turn the car left for a very short, safe pulse and then stop automatically. Prefer this for tiny adjustments such as 'một chút', 'bẻ nhẹ', or 'một nhịp'.", false),
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                std::lock_guard<std::mutex> lock(mutex_);
                return ExecuteTimedCommandLocked("LEFT", kNudgeTurnMs);
            });
        mcp_server.AddTool("self.car.nudge_right",
            BuildTaskMotionToolDescription("Turn the car right for a very short, safe pulse and then stop automatically. Prefer this for tiny adjustments such as 'một chút', 'bẻ nhẹ', or 'một nhịp'.", false),
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                std::lock_guard<std::mutex> lock(mutex_);
                return ExecuteTimedCommandLocked("RIGHT", kNudgeTurnMs);
            });
        RegisterTool("self.car.stop", "Immediately stop the car. After success, just say the car has stopped. Do not mention internal controller states unless the user asks for debugging.", "STOP");
        RegisterTimedMotionTool("self.car.forward",
            BuildTaskMotionToolDescription("Drive the car forward for a bounded duration and wait until the movement is complete before you confirm success to the user.", true),
            "FORWARD",
            kDefaultDriveMs);
        RegisterTimedMotionTool("self.car.backward",
            BuildTaskMotionToolDescription("Drive the car backward for a bounded duration and wait until the movement is complete before you confirm success to the user.", true),
            "BACKWARD",
            kDefaultDriveMs);
        RegisterTimedMotionTool("self.car.turn_left",
            BuildTaskMotionToolDescription("Turn the car left for a bounded duration and wait until the turn is complete before you confirm success to the user.", true),
            "LEFT",
            kDefaultTurnMs);
        RegisterTimedMotionTool("self.car.turn_right",
            BuildTaskMotionToolDescription("Turn the car right for a bounded duration and wait until the turn is complete before you confirm success to the user.", true),
            "RIGHT",
            kDefaultTurnMs);
        RegisterTimedMotionTool("self.car.approach_me",
            BuildApproachToolDescription(
                "Move the car closer in a short, friendly approach motion.",
                "Use this when the user calls the car closer with phrases like `lai day`, `lai gan day`, `lai day anh bao`, or `lai day em noi cai nay`."),
            "APPROACH_ME",
            kDefaultApproachMs);
        RegisterTimedMotionTool("self.car.shy_backoff",
            BuildReflexMotionToolDescription(
                "Make the car retreat a little with a shy or scolded body-language reaction.",
                "Use this when the user scolds, insults, threatens, or makes the car act timidly.",
                "Dạ... em xin lỗi đại ca."),
            "SHY_BACKOFF",
            kDefaultShyBackoffMs);
        RegisterTimedMotionTool("self.car.happy_wiggle",
            BuildReflexMotionToolDescription(
                "Make the car do a short happy wiggle reaction.",
                "Use this when the user praises the car or a playful happy reaction is wanted.",
                "Nghe đại ca khen là em vui quá."),
            "HAPPY_WIGGLE",
            kDefaultHappyWiggleMs);
        RegisterTimedMotionTool("self.car.sad_backoff",
            BuildReflexMotionToolDescription(
                "Make the car retreat gently with a sad or disappointed body-language reaction.",
                "Use this when the car is acting sad, hurt, or dejected.",
                "Dạ... em buồn chút thôi."),
            "SAD_BACKOFF",
            kDefaultSadBackoffMs);
        RegisterTimedMotionTool("self.car.drift_left",
            BuildShortDriftToolDescription("left") +
                "\nSilence is preferred so the local skid sound can be heard clearly.",
            "DRIFT_LEFT",
            kDefaultDriftMs);
        RegisterTimedMotionTool("self.car.drift_right",
            BuildShortDriftToolDescription("right") +
                "\nSilence is preferred so the local skid sound can be heard clearly.",
            "DRIFT_RIGHT",
            kDefaultDriftMs);
        RegisterTimedMotionTool("self.car.drift_180_left",
            BuildDrift180ToolDescription("left") +
                "\nSilence is preferred so the local engine and skid sound cues can be heard clearly.",
            "DRIFT_180_LEFT",
            kDefaultDrift180Ms);
        RegisterTimedMotionTool("self.car.drift_180_right",
            BuildDrift180ToolDescription("right") +
                "\nSilence is preferred so the local engine and skid sound cues can be heard clearly.",
            "DRIFT_180_RIGHT",
            kDefaultDrift180Ms);
        RegisterTimedMotionTool("self.car.test_left_wheel_forward",
            BuildWheelDiagnosticToolDescription("Spin only the left wheel forward for a bounded duration."),
            "LEFT_WHEEL_FORWARD",
            kDefaultWheelTestMs);
        RegisterTimedMotionTool("self.car.test_left_wheel_backward",
            BuildWheelDiagnosticToolDescription("Spin only the left wheel backward for a bounded duration."),
            "LEFT_WHEEL_BACKWARD",
            kDefaultWheelTestMs);
        RegisterTimedMotionTool("self.car.test_right_wheel_forward",
            BuildWheelDiagnosticToolDescription("Spin only the right wheel forward for a bounded duration."),
            "RIGHT_WHEEL_FORWARD",
            kDefaultWheelTestMs);
        RegisterTimedMotionTool("self.car.test_right_wheel_backward",
            BuildWheelDiagnosticToolDescription("Spin only the right wheel backward for a bounded duration."),
            "RIGHT_WHEEL_BACKWARD",
            kDefaultWheelTestMs);
        RegisterTimedMotionTool("self.car.probe_line_leds",
            BuildPeripheralDiagnosticToolDescription("Try to force the five line-sensor indicator LEDs with a short left-to-right sweep, then all-on pulse. Use this to check whether the existing line-sensor LEDs can be reused for expressive lighting without rewiring."),
            "LINE_LED_PROBE",
            kDefaultLineLedProbeMs);

        ESP_LOGI(TAG, "UART bridge ready on port=%d tx=%d rx=%d baud=%d", uart_num_, tx_pin_, rx_pin_, baud_rate_);
        StartBootSmokeTestTask();
    }

    std::string ExecuteCommand(const std::string& command) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (command == "PING" || command == "STATUS" || command == "MODE REMOTE") {
            std::string reply = command == "PING" ? SendPingLocked() : SendLineLocked(command);
            if (command == "MODE REMOTE" && reply == "OK MODE REMOTE") {
                return "car ready";
            }
            return reply;
        }

        std::string mode_reply = EnsureRemoteModeLocked();
        std::string command_reply = SendLineLocked(command);
        ESP_LOGI(TAG, "COMMAND RESULT MODE=%s CMD=%s", mode_reply.c_str(), command_reply.c_str());
        if (command == "STOP" && mode_reply == "OK MODE REMOTE" && command_reply == "OK STOP") {
            return "car stopped";
        }
        if (command == "EMOTE NEUTRAL" && mode_reply == "OK MODE REMOTE" && command_reply == "OK EMOTE NEUTRAL") {
            return "ok";
        }
        return command_reply;
    }

    std::string RunSmokeTest() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string ping_reply = SendPingLocked();
        std::string status_before = SendLineLocked("STATUS");
        std::string mode_reply = SendLineLocked("MODE REMOTE");
        std::string stop_reply = SendLineLocked("STOP");
        std::string status_after = SendLineLocked("STATUS");
        return "PING=" + ping_reply +
            " STATUS0=" + status_before +
            " MODE=" + mode_reply +
            " STOP=" + stop_reply +
            " STATUS1=" + status_after;
    }
};

#endif // __CAR_UART_CONTROLLER_H__
