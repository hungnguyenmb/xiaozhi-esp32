#ifndef CONTROL_CAR_PARENT_CHILD_REGISTRY_H
#define CONTROL_CAR_PARENT_CHILD_REGISTRY_H

#include <algorithm>
#include <cctype>
#include <mutex>
#include <string>
#include <vector>

#include <cJSON.h>
#include <esp_log.h>

#include "settings.h"

#ifndef CONFIG_CONTROL_CAR_CHILD_COMMAND_PORT
#define CONFIG_CONTROL_CAR_CHILD_COMMAND_PORT 8081
#endif

namespace control_car {

struct ChildCarInfo {
    std::string child_id;
    std::string display_name;
    std::vector<std::string> aliases;
    std::string ip;
    int port = CONFIG_CONTROL_CAR_CHILD_COMMAND_PORT;
    bool enabled = true;
};

class ChildRegistry {
private:
    static constexpr const char* TAG = "ChildRegistry";
    static constexpr size_t kMaxChildren = 8;
    static constexpr const char* kSettingsNamespace = "child_reg";
    static constexpr const char* kChildrenKey = "children";

    mutable std::mutex mutex_;
    std::vector<ChildCarInfo> children_;

    static std::string Trim(std::string value) {
        auto is_space = [](unsigned char ch) {
            return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
        };
        while (!value.empty() && is_space(static_cast<unsigned char>(value.front()))) {
            value.erase(value.begin());
        }
        while (!value.empty() && is_space(static_cast<unsigned char>(value.back()))) {
            value.pop_back();
        }
        return value;
    }

    static bool HasChildId(const std::vector<ChildCarInfo>& children, const std::string& child_id) {
        return std::any_of(children.begin(), children.end(), [&](const ChildCarInfo& child) {
            return child.child_id == child_id;
        });
    }

    static std::string NormalizeSelector(std::string value) {
        value = Trim(value);
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return value;
    }

    static std::vector<std::string> ParseAliases(cJSON* item) {
        std::vector<std::string> aliases;
        if (cJSON_IsArray(item)) {
            const int size = cJSON_GetArraySize(item);
            for (int i = 0; i < size; ++i) {
                cJSON* alias_item = cJSON_GetArrayItem(item, i);
                if (cJSON_IsString(alias_item) && alias_item->valuestring != nullptr) {
                    auto alias = Trim(alias_item->valuestring);
                    if (!alias.empty()) {
                        aliases.push_back(alias);
                    }
                }
            }
            return aliases;
        }

        if (cJSON_IsString(item) && item->valuestring != nullptr) {
            std::string raw = item->valuestring;
            size_t start = 0;
            while (start <= raw.size()) {
                const size_t end = raw.find_first_of(",;", start);
                auto alias = Trim(raw.substr(start, end == std::string::npos ? std::string::npos : end - start));
                if (!alias.empty()) {
                    aliases.push_back(alias);
                }
                if (end == std::string::npos) {
                    break;
                }
                start = end + 1;
            }
        }
        return aliases;
    }

    static void AddAliasesToJson(cJSON* item, const std::vector<std::string>& aliases) {
        auto* array = cJSON_CreateArray();
        for (const auto& alias : aliases) {
            cJSON_AddItemToArray(array, cJSON_CreateString(alias.c_str()));
        }
        cJSON_AddItemToObject(item, "aliases", array);
    }

    static bool MatchesSelector(const ChildCarInfo& child, const std::string& selector) {
        const auto normalized = NormalizeSelector(selector);
        if (normalized.empty()) {
            return false;
        }
        if (NormalizeSelector(child.child_id) == normalized || NormalizeSelector(child.display_name) == normalized) {
            return true;
        }
        return std::any_of(child.aliases.begin(), child.aliases.end(), [&](const std::string& alias) {
            return NormalizeSelector(alias) == normalized;
        });
    }

    void LoadFromSettings() {
        Settings settings(kSettingsNamespace, false);
        const auto payload = settings.GetString(kChildrenKey);
        if (payload.empty()) {
            ESP_LOGI(TAG, "No saved child registry");
            return;
        }

        cJSON* root = cJSON_Parse(payload.c_str());
        if (!cJSON_IsArray(root)) {
            ESP_LOGW(TAG, "Saved child registry is invalid");
            if (root != nullptr) {
                cJSON_Delete(root);
            }
            return;
        }

        std::vector<ChildCarInfo> loaded;
        const int size = cJSON_GetArraySize(root);
        for (int i = 0; i < size && loaded.size() < kMaxChildren; ++i) {
            cJSON* item = cJSON_GetArrayItem(root, i);
            cJSON* child_id_item = cJSON_GetObjectItem(item, "child_id");
            cJSON* display_name_item = cJSON_GetObjectItem(item, "display_name");
            cJSON* aliases_item = cJSON_GetObjectItem(item, "aliases");
            cJSON* ip_item = cJSON_GetObjectItem(item, "ip");
            cJSON* port_item = cJSON_GetObjectItem(item, "port");
            cJSON* enabled_item = cJSON_GetObjectItem(item, "enabled");

            if (!cJSON_IsString(child_id_item) || !cJSON_IsString(ip_item)) {
                continue;
            }

            ChildCarInfo child;
            child.child_id = Trim(child_id_item->valuestring);
            child.display_name = cJSON_IsString(display_name_item) ? Trim(display_name_item->valuestring) : "";
            child.aliases = ParseAliases(aliases_item);
            child.ip = Trim(ip_item->valuestring);
            child.port = cJSON_IsNumber(port_item) ? port_item->valueint : CONFIG_CONTROL_CAR_CHILD_COMMAND_PORT;
            child.enabled = cJSON_IsBool(enabled_item) ? cJSON_IsTrue(enabled_item) : true;

            if (child.child_id.empty() || child.ip.empty() || child.port <= 0 || child.port > 65535 ||
                HasChildId(loaded, child.child_id)) {
                continue;
            }
            loaded.push_back(child);
        }
        cJSON_Delete(root);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            children_ = std::move(loaded);
        }
        ESP_LOGI(TAG, "Loaded %d saved child car(s)", static_cast<int>(children_.size()));
    }

    void SaveLocked() const {
        cJSON* array = cJSON_CreateArray();
        for (const auto& child : children_) {
            cJSON* item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "child_id", child.child_id.c_str());
            cJSON_AddStringToObject(item, "display_name", child.display_name.c_str());
            AddAliasesToJson(item, child.aliases);
            cJSON_AddStringToObject(item, "ip", child.ip.c_str());
            cJSON_AddNumberToObject(item, "port", child.port);
            cJSON_AddBoolToObject(item, "enabled", child.enabled);
            cJSON_AddItemToArray(array, item);
        }

        char* payload = cJSON_PrintUnformatted(array);
        if (payload != nullptr) {
            Settings settings(kSettingsNamespace, true);
            settings.SetString(kChildrenKey, payload);
            cJSON_free(payload);
            ESP_LOGI(TAG, "Saved %d child car(s)", static_cast<int>(children_.size()));
        } else {
            ESP_LOGW(TAG, "Failed to serialize child registry");
        }
        cJSON_Delete(array);
    }

public:
    static ChildRegistry& GetInstance() {
        static ChildRegistry instance;
        return instance;
    }

    ChildRegistry() {
        LoadFromSettings();
    }

    bool Upsert(std::string child_id, std::string ip, int port, bool enabled, std::string* error = nullptr) {
        return Upsert(std::move(child_id), "", {}, std::move(ip), port, enabled, error);
    }

    bool Upsert(std::string child_id, std::string display_name, std::vector<std::string> aliases,
        std::string ip, int port, bool enabled, std::string* error = nullptr) {
        child_id = Trim(child_id);
        display_name = Trim(display_name);
        ip = Trim(ip);
        if (child_id.empty()) {
            if (error != nullptr) {
                *error = "missing_child_id";
            }
            return false;
        }
        if (ip.empty()) {
            if (error != nullptr) {
                *error = "missing_ip";
            }
            return false;
        }
        if (port <= 0 || port > 65535) {
            if (error != nullptr) {
                *error = "invalid_port";
            }
            return false;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& child : children_) {
            if (child.child_id == child_id) {
                child.display_name = display_name;
                child.aliases = std::move(aliases);
                child.ip = ip;
                child.port = port;
                child.enabled = enabled;
                SaveLocked();
                return true;
            }
        }

        if (children_.size() >= kMaxChildren) {
            if (error != nullptr) {
                *error = "too_many_children";
            }
            return false;
        }

        ChildCarInfo child;
        child.child_id = child_id;
        child.display_name = display_name;
        child.aliases = std::move(aliases);
        child.ip = ip;
        child.port = port;
        child.enabled = enabled;
        children_.push_back(child);
        SaveLocked();
        return true;
    }

    bool Remove(const std::string& child_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::remove_if(children_.begin(), children_.end(), [&](const ChildCarInfo& child) {
            return child.child_id == child_id;
        });
        if (it == children_.end()) {
            return false;
        }
        children_.erase(it, children_.end());
        SaveLocked();
        return true;
    }

    bool SetEnabled(const std::string& child_id, bool enabled) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& child : children_) {
            if (child.child_id == child_id) {
                child.enabled = enabled;
                SaveLocked();
                return true;
            }
        }
        return false;
    }

    bool FindBySelector(const std::string& selector, ChildCarInfo* out) const {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& child : children_) {
            if (MatchesSelector(child, selector)) {
                if (out != nullptr) {
                    *out = child;
                }
                return true;
            }
        }
        return false;
    }

    bool Find(const std::string& child_id, ChildCarInfo* out) const {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& child : children_) {
            if (child.child_id == child_id) {
                if (out != nullptr) {
                    *out = child;
                }
                return true;
            }
        }
        return false;
    }

    bool FindFirstEnabled(ChildCarInfo* out) const {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& child : children_) {
            if (child.enabled && !child.ip.empty()) {
                if (out != nullptr) {
                    *out = child;
                }
                return true;
            }
        }
        return false;
    }

    size_t Count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return children_.size();
    }

    std::vector<ChildCarInfo> ListEnabled() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<ChildCarInfo> out;
        for (const auto& child : children_) {
            if (child.enabled && !child.ip.empty()) {
                out.push_back(child);
            }
        }
        return out;
    }

    cJSON* ToJsonArray() const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto* array = cJSON_CreateArray();
        for (const auto& child : children_) {
            auto* item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "child_id", child.child_id.c_str());
            cJSON_AddStringToObject(item, "display_name", child.display_name.c_str());
            AddAliasesToJson(item, child.aliases);
            cJSON_AddStringToObject(item, "ip", child.ip.c_str());
            cJSON_AddNumberToObject(item, "port", child.port);
            cJSON_AddBoolToObject(item, "enabled", child.enabled);
            cJSON_AddItemToArray(array, item);
        }
        return array;
    }
};

} // namespace control_car

#endif // CONTROL_CAR_PARENT_CHILD_REGISTRY_H
