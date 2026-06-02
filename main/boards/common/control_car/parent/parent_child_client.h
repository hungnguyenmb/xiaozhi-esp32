#ifndef CONTROL_CAR_PARENT_CHILD_CLIENT_H
#define CONTROL_CAR_PARENT_CHILD_CLIENT_H

#include "board.h"
#include "mcp_server.h"
#include "control_car/parent/child_registry.h"
#include "control_car/shared/control_car_role.h"

#include <cJSON.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef CONFIG_CONTROL_CAR_DEFAULT_CHILD_IP
#define CONFIG_CONTROL_CAR_DEFAULT_CHILD_IP ""
#endif

#ifndef CONFIG_CONTROL_CAR_CHILD_COMMAND_PORT
#define CONFIG_CONTROL_CAR_CHILD_COMMAND_PORT 8081
#endif

namespace control_car {

class ParentChildClient {
private:
    static constexpr const char* TAG = "ParentChildClient";
    static constexpr int kTimeoutMs = 5000;
    static constexpr int kMaxAttempts = 2;

    std::mutex mutex_;

    struct HttpResponse {
        int status_code = 0;
        std::string body;
    };

    struct ChildTarget {
        std::string child_id;
        std::string ip;
        int port = CONFIG_CONTROL_CAR_CHILD_COMMAND_PORT;
    };

    static esp_err_t HttpEventHandler(esp_http_client_event_t* evt) {
        if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data != nullptr && evt->data_len > 0) {
            auto* response = static_cast<HttpResponse*>(evt->user_data);
            response->body.append(static_cast<const char*>(evt->data), evt->data_len);
        }
        return ESP_OK;
    }

    static std::string NormalizeIp(const std::string& child_ip) {
        auto ip = child_ip;
        while (!ip.empty() && (ip.front() == ' ' || ip.front() == '\t' || ip.front() == '\n' || ip.front() == '\r')) {
            ip.erase(ip.begin());
        }
        while (!ip.empty() && (ip.back() == ' ' || ip.back() == '\t' || ip.back() == '\n' || ip.back() == '\r')) {
            ip.pop_back();
        }
        return ip;
    }

    static std::string BuildUrl(const std::string& child_ip, int port, const std::string& path) {
        return "http://" + child_ip + ":" + std::to_string(port) + path;
    }

    static bool IsAllSelector(const std::string& selector) {
        const auto value = NormalizeIp(selector);
        return value == "all" || value == "tat ca" || value == "tat_ca";
    }

    static ChildTarget ResolveTarget(const std::string& child_selector, const std::string& child_ip, int port) {
        const auto ip = NormalizeIp(child_ip);
        if (!ip.empty()) {
            return {"direct", ip, port};
        }

        const auto selector = NormalizeIp(child_selector);
        if (!selector.empty()) {
            ChildCarInfo selected;
            if (ChildRegistry::GetInstance().FindBySelector(selector, &selected)) {
                ESP_LOGI(TAG, "Using child from registry selector=%s child_id=%s ip=%s port=%d",
                    selector.c_str(), selected.child_id.c_str(), selected.ip.c_str(), selected.port);
                return {selected.child_id, selected.ip, selected.port};
            }
            throw std::runtime_error("child_not_found");
        }

        ChildCarInfo child;
        if (ChildRegistry::GetInstance().FindFirstEnabled(&child)) {
            ESP_LOGI(TAG, "Using default child from registry child_id=%s ip=%s port=%d",
                child.child_id.c_str(), child.ip.c_str(), child.port);
            return {child.child_id, child.ip, child.port};
        }

        throw std::runtime_error("missing_child_ip");
    }

    static std::string BuildCommandBody(const std::string& action, int duration_ms) {
        auto* root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "action", action.c_str());
        cJSON_AddStringToObject(root, "from", "parent");
        cJSON_AddStringToObject(root, "message_id", "parent-mcp");
        cJSON_AddNumberToObject(root, "duration_ms", duration_ms);

        char* raw = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        if (raw == nullptr) {
            throw std::runtime_error("json_serialize_failed");
        }

        std::string body(raw);
        cJSON_free(raw);
        return body;
    }

    static HttpResponse RequestOnce(const std::string& method, const ChildTarget& target, const std::string& path, const std::string& body) {
        const auto url = BuildUrl(target.ip, target.port, path);
        HttpResponse response;

        esp_http_client_config_t config = {};
        config.url = url.c_str();
        config.timeout_ms = kTimeoutMs;
        config.event_handler = HttpEventHandler;
        config.user_data = &response;

        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (client == nullptr) {
            throw std::runtime_error("http_client_init_failed");
        }

        if (method == "POST") {
            esp_http_client_set_method(client, HTTP_METHOD_POST);
            esp_http_client_set_header(client, "Content-Type", "application/json");
            if (!body.empty()) {
                esp_http_client_set_post_field(client, body.c_str(), body.size());
            }
        } else {
            esp_http_client_set_method(client, HTTP_METHOD_GET);
        }

        esp_err_t err = esp_http_client_perform(client);
        response.status_code = esp_http_client_get_status_code(client);
        esp_http_client_cleanup(client);

        if (err != ESP_OK) {
            ESP_LOGW(TAG, "HTTP %s %s failed: %s", method.c_str(), url.c_str(), esp_err_to_name(err));
            throw std::runtime_error(std::string("http_failed: ") + esp_err_to_name(err));
        }
        if (response.status_code < 200 || response.status_code >= 300) {
            ESP_LOGW(TAG, "HTTP %s %s status=%d body=%s", method.c_str(), url.c_str(), response.status_code, response.body.c_str());
            throw std::runtime_error("child_http_status_" + std::to_string(response.status_code) + ": " + response.body);
        }

        ESP_LOGI(TAG, "HTTP %s %s status=%d body=%s", method.c_str(), url.c_str(), response.status_code, response.body.c_str());
        return response;
    }

    static HttpResponse Request(const std::string& method, const ChildTarget& target, const std::string& path, const std::string& body = "") {
        Board::GetInstance().SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);

        std::runtime_error last_error("http_failed");
        for (int attempt = 1; attempt <= kMaxAttempts; ++attempt) {
            try {
                return RequestOnce(method, target, path, body);
            } catch (const std::runtime_error& e) {
                last_error = e;
                if (attempt < kMaxAttempts) {
                    ESP_LOGW(TAG, "Retrying child request after failure: %s", e.what());
                    vTaskDelay(pdMS_TO_TICKS(150));
                }
            }
        }

        throw last_error;
    }

    static std::string ExecuteTarget(const ChildTarget& target, const std::string& action, int duration_ms) {
        if (action == "ping") {
            return Request("GET", target, "/child/ping").body;
        }
        if (action == "stop") {
            return Request("POST", target, "/child/stop").body;
        }

        return Request("POST", target, "/child/command", BuildCommandBody(action, duration_ms)).body;
    }

    static std::string BuildMultiResponse(const std::string& action, const std::vector<ChildCarInfo>& children,
        const std::vector<std::string>& responses, const std::vector<std::string>& errors) {
        auto* root = cJSON_CreateObject();
        cJSON_AddBoolToObject(root, "ok", errors.empty());
        cJSON_AddStringToObject(root, "target", "all");
        cJSON_AddStringToObject(root, "action", action.c_str());
        auto* results = cJSON_CreateArray();
        for (size_t i = 0; i < children.size(); ++i) {
            auto* item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "child_id", children[i].child_id.c_str());
            cJSON_AddStringToObject(item, "ip", children[i].ip.c_str());
            cJSON_AddNumberToObject(item, "port", children[i].port);
            if (i < responses.size() && !responses[i].empty()) {
                cJSON_AddBoolToObject(item, "ok", true);
                cJSON_AddStringToObject(item, "response", responses[i].c_str());
            } else if (i < errors.size() && !errors[i].empty()) {
                cJSON_AddBoolToObject(item, "ok", false);
                cJSON_AddStringToObject(item, "error", errors[i].c_str());
            }
            cJSON_AddItemToArray(results, item);
        }
        cJSON_AddItemToObject(root, "results", results);

        char* raw = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        if (raw == nullptr) {
            throw std::runtime_error("json_serialize_failed");
        }
        std::string body(raw);
        cJSON_free(raw);
        return body;
    }

    static std::string ExecuteAll(const std::string& action, int duration_ms) {
        const auto children = ChildRegistry::GetInstance().ListEnabled();
        if (children.empty()) {
            throw std::runtime_error("no_enabled_children");
        }

        std::vector<std::string> responses(children.size());
        std::vector<std::string> errors(children.size());
        for (size_t i = 0; i < children.size(); ++i) {
            try {
                responses[i] = ExecuteTarget({children[i].child_id, children[i].ip, children[i].port}, action, duration_ms);
            } catch (const std::exception& ex) {
                errors[i] = ex.what();
            }
        }

        std::vector<std::string> actual_errors;
        for (const auto& error : errors) {
            if (!error.empty()) {
                actual_errors.push_back(error);
            }
        }
        return BuildMultiResponse(action, children, responses, actual_errors.empty() ? std::vector<std::string>() : errors);
    }

    static std::string Execute(const std::string& child_selector, const std::string& child_ip, const std::string& action, int duration_ms, int port) {
        if (IsAllSelector(child_selector)) {
            return ExecuteAll(action, duration_ms);
        }
        return ExecuteTarget(ResolveTarget(child_selector, child_ip, port), action, duration_ms);
    }

    static std::string ToolDescription() {
        return
            "Send a LAN command from the parent car to a child car or all enabled child cars.\n"
            "Use this only on PARENT firmware.\n"
            "Prefer child_id for normal use. child_id can be a child_id, display_name, alias, or `all`.\n"
            "Use child_ip only for direct manual tests. If child_id and child_ip are empty, the parent will use the first enabled child saved in the web config registry.\n"
            "For connectivity checks, use action `ping`.\n"
            "For emergency stop, use action `stop`.\n"
            "For child emotion, use actions such as happy, laugh, cry, sad, angry, hate, love, thinking, tango, or dance_test.\n"
            "For child preset local audio, use actions such as speak_hello, speak_yes, speak_no, sound_laugh, sound_cry, sound_success, sound_surprise, dance_music, or tango_voice.\n"
            "Do not use this for dangerous motion yet; current child firmware only has safe emotion/sound commands.\n"
            "After the tool succeeds, keep the spoken reply short.";
    }

public:
    static std::string SendCommand(const std::string& child_ip, const std::string& action, int duration_ms, int port = CONFIG_CONTROL_CAR_CHILD_COMMAND_PORT) {
        return Execute("", child_ip, action, duration_ms, port);
    }

    static std::string SendCommandToSelector(const std::string& child_selector, const std::string& action, int duration_ms) {
        return Execute(child_selector, "", action, duration_ms, CONFIG_CONTROL_CAR_CHILD_COMMAND_PORT);
    }

    ParentChildClient() {
        if (!control_car::IsParent()) {
            return;
        }

        auto& mcp_server = McpServer::GetInstance();
        mcp_server.AddTool(
            "self.child_robot.command",
            ToolDescription(),
            PropertyList({
                Property("child_id", kPropertyTypeString, std::string("")),
                Property("child_ip", kPropertyTypeString, std::string(CONFIG_CONTROL_CAR_DEFAULT_CHILD_IP)),
                Property("action", kPropertyTypeString),
                Property("duration_ms", kPropertyTypeInteger, 500, 0, 10000),
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                std::lock_guard<std::mutex> lock(mutex_);
                return Execute(
                    properties["child_id"].value<std::string>(),
                    properties["child_ip"].value<std::string>(),
                    properties["action"].value<std::string>(),
                    properties["duration_ms"].value<int>(),
                    CONFIG_CONTROL_CAR_CHILD_COMMAND_PORT);
            }
        );
    }
};

} // namespace control_car

#endif // CONTROL_CAR_PARENT_CHILD_CLIENT_H
