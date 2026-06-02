#ifndef CONTROL_CAR_CHILD_COMMAND_SERVER_H
#define CONTROL_CAR_CHILD_COMMAND_SERVER_H

#include "application.h"
#include "board.h"
#include "control_car/child/child_emotion_controller.h"
#include "control_car/shared/control_car_role.h"

#include <algorithm>
#include <cctype>
#include <mutex>
#include <string>

#include <cJSON.h>
#include <esp_http_server.h>
#include <esp_log.h>

namespace control_car {

class ChildCommandServer {
private:
    static constexpr const char* TAG = "ChildCommandServer";
    static constexpr int kDefaultPort = 8081;
    static constexpr size_t kMaxBodyBytes = 1024;

    httpd_handle_t server_ = nullptr;
    std::mutex mutex_;
    std::string last_action_ = "idle";
    std::string last_from_;
    std::string last_message_id_;

    static std::string Normalize(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return value;
    }

    static void AddString(cJSON* root, const char* key, const std::string& value) {
        cJSON_AddStringToObject(root, key, value.c_str());
    }

    static esp_err_t SendJson(httpd_req_t* req, cJSON* root, int status_code = 200) {
        char* raw = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        if (raw == nullptr) {
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_set_type(req, "application/json");
            httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"json_serialize_failed\"}");
            return ESP_FAIL;
        }

        if (status_code == 400) {
            httpd_resp_set_status(req, "400 Bad Request");
        } else if (status_code == 404) {
            httpd_resp_set_status(req, "404 Not Found");
        } else if (status_code == 409) {
            httpd_resp_set_status(req, "409 Conflict");
        } else if (status_code >= 500) {
            httpd_resp_set_status(req, "500 Internal Server Error");
        }

        httpd_resp_set_type(req, "application/json");
        auto err = httpd_resp_sendstr(req, raw);
        cJSON_free(raw);
        return err;
    }

    static cJSON* BuildBaseResponse(bool ok) {
        auto* root = cJSON_CreateObject();
        cJSON_AddBoolToObject(root, "ok", ok);
        cJSON_AddStringToObject(root, "role", "child");
        cJSON_AddStringToObject(root, "server", "child_command_server");
        return root;
    }

    static esp_err_t SendError(httpd_req_t* req, const char* error, int status_code = 400) {
        auto* root = BuildBaseResponse(false);
        cJSON_AddStringToObject(root, "error", error);
        return SendJson(req, root, status_code);
    }

    static std::string JsonString(cJSON* root, const char* key, const std::string& fallback = "") {
        auto* item = cJSON_GetObjectItem(root, key);
        if (cJSON_IsString(item) && item->valuestring != nullptr) {
            return item->valuestring;
        }
        return fallback;
    }

    static int JsonInt(cJSON* root, const char* key, int fallback = 0) {
        auto* item = cJSON_GetObjectItem(root, key);
        if (cJSON_IsNumber(item)) {
            return item->valueint;
        }
        return fallback;
    }

    static esp_err_t ReadJsonBody(httpd_req_t* req, cJSON** out_root) {
        *out_root = nullptr;
        if (req->content_len <= 0) {
            return ESP_FAIL;
        }
        if (req->content_len > kMaxBodyBytes) {
            return ESP_ERR_NO_MEM;
        }

        std::string body(req->content_len, '\0');
        int received = 0;
        while (received < req->content_len) {
            int ret = httpd_req_recv(req, body.data() + received, req->content_len - received);
            if (ret <= 0) {
                return ESP_FAIL;
            }
            received += ret;
        }

        *out_root = cJSON_Parse(body.c_str());
        return *out_root == nullptr ? ESP_FAIL : ESP_OK;
    }

    esp_err_t HandlePing(httpd_req_t* req) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto* root = BuildBaseResponse(true);
        AddString(root, "last_action", last_action_);
        AddString(root, "last_from", last_from_);
        AddString(root, "last_message_id", last_message_id_);
        cJSON_AddStringToObject(root, "state", "idle");
        ESP_LOGI(TAG, "GET /child/ping");
        return SendJson(req, root);
    }

    esp_err_t HandleStop(httpd_req_t* req) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            last_action_ = "stop";
        }

        auto* display = Board::GetInstance().GetDisplay();
        if (display != nullptr) {
            display->ForceEmotion("neutral");
        }

        auto* root = BuildBaseResponse(true);
        cJSON_AddStringToObject(root, "result", "stopped");
        ESP_LOGI(TAG, "POST /child/stop");
        return SendJson(req, root);
    }

    esp_err_t HandleCommand(httpd_req_t* req) {
        cJSON* body = nullptr;
        auto read_err = ReadJsonBody(req, &body);
        if (read_err == ESP_ERR_NO_MEM) {
            return SendError(req, "body_too_large", 400);
        }
        if (read_err != ESP_OK) {
            return SendError(req, "bad_json", 400);
        }

        std::string action = Normalize(JsonString(body, "action", ""));
        std::string from = JsonString(body, "from", "");
        std::string message_id = JsonString(body, "message_id", "");
        int duration_ms = JsonInt(body, "duration_ms", 0);
        cJSON_Delete(body);

        if (action.empty()) {
            return SendError(req, "missing_action", 400);
        }

        std::string result;
        if (action == "stop") {
            result = "stopped";
            auto* display = Board::GetInstance().GetDisplay();
            if (display != nullptr) {
                display->ForceEmotion("neutral");
            }
        } else if (ChildEmotionController::IsSoundPreset(action)) {
            result = ChildEmotionController::ApplySoundPreset(action);
        } else if (action == "happy" || action == "joy" || action == "vui" || action == "vui_suong" ||
                   action == "laugh" || action == "laughing" || action == "cuoi" || action == "cuoi_to" ||
                   action == "cry" || action == "crying" || action == "khoc" ||
                   action == "sad" || action == "buon" ||
                   action == "angry" || action == "gian" ||
                   action == "hate" || action == "ghet" || action == "disgust" ||
                   action == "love" || action == "loving" || action == "yeu" ||
                   action == "thinking" || action == "think" || action == "nghi" ||
                   action == "tango" || action == "dance_test") {
            const std::string emotion = action == "dance_test" ? "joy" : action;
            result = ChildEmotionController::ApplyEmotion(emotion, action != "thinking");
        } else {
            ESP_LOGW(TAG, "Unknown child action: %s", action.c_str());
            return SendError(req, "unknown_action", 404);
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            last_action_ = action;
            last_from_ = from;
            last_message_id_ = message_id;
        }

        ESP_LOGI(TAG, "POST /child/command action=%s from=%s message_id=%s duration_ms=%d",
            action.c_str(), from.c_str(), message_id.c_str(), duration_ms);

        auto* root = BuildBaseResponse(true);
        AddString(root, "action", action);
        AddString(root, "result", result);
        cJSON_AddNumberToObject(root, "duration_ms", duration_ms);
        return SendJson(req, root);
    }

    static esp_err_t PingHandler(httpd_req_t* req) {
        return static_cast<ChildCommandServer*>(req->user_ctx)->HandlePing(req);
    }

    static esp_err_t StopHandler(httpd_req_t* req) {
        return static_cast<ChildCommandServer*>(req->user_ctx)->HandleStop(req);
    }

    static esp_err_t CommandHandler(httpd_req_t* req) {
        return static_cast<ChildCommandServer*>(req->user_ctx)->HandleCommand(req);
    }

public:
    ~ChildCommandServer() {
        Stop();
    }

    bool Start(int port = kDefaultPort) {
        if (!control_car::IsChild()) {
            return false;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        if (server_ != nullptr) {
            return true;
        }

        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.server_port = port;
        config.ctrl_port = 32770;
        config.max_open_sockets = 4;
        config.lru_purge_enable = true;

        if (httpd_start(&server_, &config) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start child command server on port %d", port);
            server_ = nullptr;
            return false;
        }

        httpd_uri_t ping_uri = {
            .uri = "/child/ping",
            .method = HTTP_GET,
            .handler = PingHandler,
            .user_ctx = this
        };
        httpd_uri_t stop_uri = {
            .uri = "/child/stop",
            .method = HTTP_POST,
            .handler = StopHandler,
            .user_ctx = this
        };
        httpd_uri_t command_uri = {
            .uri = "/child/command",
            .method = HTTP_POST,
            .handler = CommandHandler,
            .user_ctx = this
        };

        httpd_register_uri_handler(server_, &ping_uri);
        httpd_register_uri_handler(server_, &stop_uri);
        httpd_register_uri_handler(server_, &command_uri);

        ESP_LOGI(TAG, "Child command server started on port %d", port);
        return true;
    }

    void Stop() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (server_ != nullptr) {
            httpd_stop(server_);
            server_ = nullptr;
            ESP_LOGI(TAG, "Child command server stopped");
        }
    }
};

} // namespace control_car

#endif // CONTROL_CAR_CHILD_COMMAND_SERVER_H
