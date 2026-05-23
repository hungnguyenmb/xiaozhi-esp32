#ifndef __CHILD_ROBOT_BRIDGE_CONTROLLER_H__
#define __CHILD_ROBOT_BRIDGE_CONTROLLER_H__

#include "board.h"
#include "mcp_server.h"
#include "application.h"
#include "assets/lang_config.h"

#include <cJSON.h>
#include <esp_log.h>

#include <mutex>
#include <string>

class ChildRobotBridgeController {
private:
    static constexpr const char* TAG = "ChildRobotBridge";
    static constexpr int kDefaultMoveMs = 600;
    static constexpr int kDefaultSpeed = 180;
    static constexpr int kMinMoveMs = 50;
    static constexpr int kMaxMoveMs = 4000;
    static constexpr int kDanceLongArmDelayMs = 700;
    static constexpr int kBirthChildRobotCryDelayMs = 0;

    std::mutex mutex_;

    static std::string BuildUrl(const char* path) {
        return std::string(CHILD_ROBOT_BRIDGE_URL) + path;
    }

    static cJSON* BuildBasePayload() {
        cJSON* root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "robot_id", CHILD_ROBOT_ID);
        return root;
    }

    static std::string SerializeJsonAndDelete(cJSON* root) {
        char* raw = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        if (raw == nullptr) {
            throw std::runtime_error("Failed to serialize child bridge JSON");
        }
        std::string payload(raw);
        cJSON_free(raw);
        return payload;
    }

    std::string ExecuteBridgePost(const char* path, cJSON* body, bool return_status_text) {
        std::string payload = SerializeJsonAndDelete(body);
        auto http = Board::GetInstance().GetNetwork()->CreateHttp(3);
        http->SetTimeout(CHILD_ROBOT_BRIDGE_TIMEOUT_MS);
        http->SetHeader("Content-Type", "application/json");
        http->SetContent(std::move(payload));

        std::string url = BuildUrl(path);
        ESP_LOGI(TAG, "POST %s", url.c_str());
        if (!http->Open("POST", url)) {
            int last_error = http->GetLastError();
            ESP_LOGE(TAG, "Bridge open failed: 0x%x", last_error);
            return "bridge_unreachable";
        }

        int status_code = http->GetStatusCode();
        std::string response = http->ReadAll();
        http->Close();

        ESP_LOGI(TAG, "Bridge response status=%d body=%s", status_code, response.c_str());
        cJSON* root = cJSON_Parse(response.c_str());
        if (root == nullptr) {
            if (status_code != 200) {
                return "bridge_http_" + std::to_string(status_code);
            }
            return "bridge_bad_json";
        }

        std::string result = "ok";
        cJSON* ok = cJSON_GetObjectItem(root, "ok");
        if (!cJSON_IsBool(ok) || !cJSON_IsTrue(ok)) {
            cJSON* error = cJSON_GetObjectItem(root, "error");
            cJSON* message = error ? cJSON_GetObjectItem(error, "message") : nullptr;
            if (cJSON_IsString(message) && message->valuestring != nullptr) {
                result = message->valuestring;
            } else {
                result = "bridge_error";
            }
            cJSON_Delete(root);
            return result;
        }

        if (status_code != 200 && status_code != -1) {
            cJSON_Delete(root);
            return "bridge_http_" + std::to_string(status_code);
        }

        cJSON* data = cJSON_GetObjectItem(root, "data");
        cJSON* status_text = data ? cJSON_GetObjectItem(data, "status_text") : nullptr;
        if (return_status_text && cJSON_IsString(status_text) && status_text->valuestring != nullptr) {
            result = status_text->valuestring;
        }

        cJSON_Delete(root);
        return result;
    }

    std::string MoveDescription() const {
        return
            "Điều khiển robot con qua local bridge trong cùng mạng LAN.\n"
            "Chỉ dùng tool này khi người dùng đang ra lệnh cho robot con của bạn.\n"
            "Không dùng cho chính robot cha.\n"
            "Call ngay khi user nói robot con tiến/lùi/rẽ.\n"
            "Sau khi tool thành công thì im lặng.";
    }

    std::string FaceDescription() const {
        return
            "Đổi biểu cảm khuôn mặt của robot con qua local bridge.\n"
            "Chỉ dùng khi user muốn robot con cười, buồn, giận, nghĩ, ngủ, cười nhẹ, cười lớn hoặc khóc.\n"
            "Sau khi tool thành công thì im lặng.";
    }

    std::string StopDescription() const {
        return
            "Dừng robot con ngay lập tức qua local bridge.\n"
            "Ưu tiên dùng tool này khi user nói dừng lại, đứng im, ngừng di chuyển.\n"
            "Sau khi tool thành công thì im lặng.";
    }

    std::string StatusDescription() const {
        return
            "Lấy trạng thái hiện tại của robot con qua local bridge.\n"
            "Dùng khi user hỏi robot con đang làm gì hoặc đang biểu cảm gì.";
    }

    std::string PerformActionDescription() const {
        return
            "Ra lệnh cho robot con thực hiện một hành động cấp cao qua local bridge.\n"
            "Ưu tiên dùng tool này khi user nói các câu tự nhiên như chào, nhảy múa, hoảng loạn, lại đây, đi ra, ngủ, ăn mừng, nhận diện.\n"
            "Các action hỗ trợ: greet_owner, come_here, go_away, dance, panic, happy_bounce, thinking_look, identify, sleep, celebrate.\n"
            "Không dùng tool này cho lệnh chính xác theo thời lượng như tiến 1 giây hay quay trái 300 mili giây.\n"
            "Sau khi tool thành công thì im lặng.";
    }

    std::string PerformShowDescription() const {
        return
            "Ra lệnh cho robot con chạy một choreography dài có soundtrack đồng bộ.\n"
            "Hiện tại chỉ hỗ trợ show: dance_long_v1.\n"
            "Khi dùng tool này, robot cha sẽ arm robot con trước với start delay cố định, rồi phát nhạc local ngay để hai bên vào nhịp gần nhau hơn.\n"
            "Dùng cho các câu như nhảy dài, nhảy tăng tăng, nhảy theo nhạc, biểu diễn bài nhảy.\n"
            "Sau khi tool thành công thì im lặng.";
    }

    std::string BirthChildRobotDescription() const {
        return
            "Dùng khi người dùng nói các câu như hãy đẻ cho anh một con robot con, đẻ cho anh robot con, đẻ ngay cho anh mấy con robot con.\n"
            "Khi dùng tool này, bạn phải phản hồi đúng nguyên văn bằng giọng TTS hiện tại: "
            "\"Dạ vâng đại ca, đại ca chờ em chút để em đẻ ngay cho đại ca mấy con robot con. "
            "Ui da... đau quá đại ca... đau quá đại ca... "
            "Em mới đẻ xong, đại ca xem dưới đít có mấy con robot.\"\n"
            "Tool này chỉ hẹn giờ phát tiếng em bé khóc local ở cuối câu nói, nên sau khi tool thành công không được thêm câu nào khác.";
    }

    void ScheduleShowSound(const std::string& show) {
        auto& app = Application::GetInstance();
        if (show == "dance_long_v1") {
            app.Schedule([]() {
                Application::GetInstance().PlayPrioritySound(Lang::Sounds::OGG_DANCE_LONG_V1);
            });
        }
    }

    void ScheduleDelayedPrioritySound(const std::string_view& sound, int delay_ms) {
        struct DelayedSoundContext {
            std::string sound;
            esp_timer_handle_t timer = nullptr;
        };

        auto* context = new DelayedSoundContext{std::string(sound)};
        esp_timer_create_args_t timer_args = {
            .callback = [](void* arg) {
                auto* ctx = static_cast<DelayedSoundContext*>(arg);
                auto sound_copy = ctx->sound;
                auto timer = ctx->timer;
                Application::GetInstance().Schedule([sound = std::move(sound_copy)]() {
                    Application::GetInstance().PlayPrioritySound(sound);
                });
                esp_timer_stop(timer);
                esp_timer_delete(timer);
                delete ctx;
            },
            .arg = context,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "child_sound_delay",
            .skip_unhandled_events = true
        };

        if (esp_timer_create(&timer_args, &context->timer) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create delayed sound timer");
            delete context;
            return;
        }

        if (esp_timer_start_once(context->timer, static_cast<uint64_t>(delay_ms) * 1000ULL) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start delayed sound timer");
            esp_timer_delete(context->timer);
            delete context;
        }
    }

    std::string BuildBirthChildRobotScript() const {
        return "Dạ vâng đại ca, đại ca chờ em chút để em đẻ ngay cho đại ca mấy con robot con. "
               "Ui da... đau quá đại ca... đau quá đại ca... "
               "Em mới đẻ xong, đại ca xem dưới đít có mấy con robot.";
    }

    std::string ScheduleBirthChildRobotScene() {
        ScheduleDelayedPrioritySound(Lang::Sounds::OGG_BIRTH_CHILD_ROBOT_BABY_CRY_V1, kBirthChildRobotCryDelayMs);
        return BuildBirthChildRobotScript();
    }

public:
    ChildRobotBridgeController() {
        auto& mcp_server = McpServer::GetInstance();

        mcp_server.AddTool(
            "self.child_robot.move",
            MoveDescription(),
            PropertyList({
                Property("direction", kPropertyTypeString),
                Property("duration_ms", kPropertyTypeInteger, kDefaultMoveMs, kMinMoveMs, kMaxMoveMs),
                Property("speed", kPropertyTypeInteger, kDefaultSpeed, 0, 255)
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                std::lock_guard<std::mutex> lock(mutex_);
                cJSON* body = BuildBasePayload();
                cJSON_AddStringToObject(body, "direction", properties["direction"].value<std::string>().c_str());
                cJSON_AddNumberToObject(body, "duration_ms", properties["duration_ms"].value<int>());
                cJSON_AddNumberToObject(body, "speed", properties["speed"].value<int>());
                return ExecuteBridgePost("/tool/child_robot.move", body, false);
            }
        );

        mcp_server.AddTool(
            "self.child_robot.stop",
            StopDescription(),
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                std::lock_guard<std::mutex> lock(mutex_);
                return ExecuteBridgePost("/tool/child_robot.stop", BuildBasePayload(), false);
            }
        );

        mcp_server.AddTool(
            "self.child_robot.set_face",
            FaceDescription(),
            PropertyList({Property("face", kPropertyTypeString)}),
            [this](const PropertyList& properties) -> ReturnValue {
                std::lock_guard<std::mutex> lock(mutex_);
                cJSON* body = BuildBasePayload();
                cJSON_AddStringToObject(body, "face", properties["face"].value<std::string>().c_str());
                return ExecuteBridgePost("/tool/child_robot.set_face", body, false);
            }
        );

        mcp_server.AddTool(
            "self.child_robot.perform_action",
            PerformActionDescription(),
            PropertyList({Property("action", kPropertyTypeString)}),
            [this](const PropertyList& properties) -> ReturnValue {
                std::lock_guard<std::mutex> lock(mutex_);
                cJSON* body = BuildBasePayload();
                cJSON_AddStringToObject(body, "action", properties["action"].value<std::string>().c_str());
                return ExecuteBridgePost("/tool/child_robot.perform_action", body, false);
            }
        );

        mcp_server.AddTool(
            "self.child_robot.perform_show",
            PerformShowDescription(),
            PropertyList({Property("show", kPropertyTypeString)}),
            [this](const PropertyList& properties) -> ReturnValue {
                std::lock_guard<std::mutex> lock(mutex_);
                const std::string show = properties["show"].value<std::string>();
                cJSON* body = BuildBasePayload();
                cJSON_AddStringToObject(body, "show", show.c_str());
                cJSON_AddNumberToObject(body, "start_delay_ms", kDanceLongArmDelayMs);
                std::string result = ExecuteBridgePost("/tool/child_robot.perform_show", body, false);
                if (result == "ok") {
                    ScheduleShowSound(show);
                }
                return result;
            }
        );

        mcp_server.AddTool(
            "self.child_robot.get_status",
            StatusDescription(),
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                std::lock_guard<std::mutex> lock(mutex_);
                return ExecuteBridgePost("/tool/child_robot.get_status", BuildBasePayload(), true);
            }
        );

        mcp_server.AddTool(
            "self.child_robot.birth_child_robot",
            BirthChildRobotDescription(),
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                std::lock_guard<std::mutex> lock(mutex_);
                return ScheduleBirthChildRobotScene();
            }
        );
    }
};

#endif
