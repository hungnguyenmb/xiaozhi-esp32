#ifndef CONTROL_CAR_PARENT_WEB_CONFIG_SERVER_H
#define CONTROL_CAR_PARENT_WEB_CONFIG_SERVER_H

#include "control_car/parent/child_registry.h"
#include "control_car/parent/parent_child_client.h"
#include "control_car/shared/control_car_role.h"

#include <cJSON.h>
#include <esp_http_server.h>
#include <esp_log.h>

#include <mutex>
#include <stdexcept>
#include <string>

#ifndef CONFIG_CONTROL_CAR_PARENT_WEB_PORT
#define CONFIG_CONTROL_CAR_PARENT_WEB_PORT 8080
#endif

#ifndef CONFIG_CONTROL_CAR_CHILD_COMMAND_PORT
#define CONFIG_CONTROL_CAR_CHILD_COMMAND_PORT 8081
#endif

namespace control_car {

class ParentWebConfigServer {
private:
    static constexpr const char* TAG = "ParentWebConfigServer";
    static constexpr size_t kMaxBodyBytes = 1024;

    httpd_handle_t server_ = nullptr;
    std::mutex mutex_;
    ChildRegistry& registry_ = ChildRegistry::GetInstance();

    static const char* IndexHtml() {
        return R"html(<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>Control Car Parent</title>
  <style>
    :root{font-family:ui-sans-serif,system-ui,sans-serif;color:#132018;background:#eef4e8}
    body{margin:0;padding:18px;max-width:880px}
    h1{margin:0 0 8px;font-size:24px}
    .card{background:#fff;border:1px solid #d8e2d2;border-radius:14px;padding:14px;margin:12px 0;box-shadow:0 8px 24px #15351d18}
    input,button,select{font:inherit;border-radius:10px;border:1px solid #b9c7b3;padding:9px;margin:4px}
    button{background:#173f2a;color:white;border:0;cursor:pointer}
    button.secondary{background:#4f6d5a}
    button.warn{background:#a33b24}
    .disabled{opacity:.55}
    .pill{display:inline-block;border-radius:999px;padding:3px 8px;font-size:12px;font-weight:700}
    .on{background:#d9f7df;color:#0f6b2c}
    .off{background:#f7ded9;color:#8a2c19}
    table{width:100%;border-collapse:collapse;margin-top:8px}
    th,td{text-align:left;border-bottom:1px solid #e2eadf;padding:8px}
    pre{white-space:pre-wrap;background:#162116;color:#dff7dd;border-radius:12px;padding:12px;min-height:92px}
  </style>
</head>
<body>
  <h1>Parent Car Web Config</h1>
  <div id="status" class="card">Loading...</div>
  <div class="card">
    <h2>Add or edit child</h2>
    <input id="child_id" placeholder="child_id" value="car_b">
    <input id="display_name" placeholder="display name, e.g. be do">
    <input id="aliases" placeholder="aliases, comma separated">
    <input id="ip" placeholder="child IP">
    <input id="port" placeholder="port" value="8081" type="number">
    <label><input id="enabled" type="checkbox" checked> enabled</label>
    <button onclick="addChild()">Save child</button>
    <button class="secondary" onclick="clearForm()">Clear form</button>
  </div>
  <div class="card">
    <h2>All enabled children</h2>
    <button onclick="post('/api/children/ping',{child_id:'all'})">Ping all</button>
    <button onclick="post('/api/children/command',{child_id:'all',action:'thinking',duration_ms:500})">Thinking all</button>
    <button onclick="post('/api/children/command',{child_id:'all',action:'happy',duration_ms:500})">Happy all</button>
    <button onclick="post('/api/children/command',{child_id:'all',action:'speak_hello',duration_ms:500})">Hello all</button>
    <button onclick="post('/api/children/command',{child_id:'all',action:'sound_laugh',duration_ms:500})">Laugh sound all</button>
    <button onclick="post('/api/children/command',{child_id:'all',action:'sound_cry',duration_ms:500})">Cry sound all</button>
    <button class="warn" onclick="post('/api/children/stop',{child_id:'all'})">Stop all</button>
  </div>
  <div class="card">
    <h2>Children</h2>
    <table>
      <thead><tr><th>ID</th><th>Name / aliases</th><th>IP</th><th>Port</th><th>State</th><th>Actions</th></tr></thead>
      <tbody id="children"></tbody>
    </table>
  </div>
  <div class="card">
    <h2>Log</h2>
    <pre id="log"></pre>
  </div>
<script>
const log = (value) => document.getElementById('log').textContent = typeof value === 'string' ? value : JSON.stringify(value, null, 2);
const last = {};
const safe = (value) => String(value ?? '').replace(/[&<>"']/g, ch => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[ch]));
const jsonAttr = (value) => safe(JSON.stringify(value ?? ''));
async function api(path, options) {
  const res = await fetch(path, options);
  const text = await res.text();
  try { return JSON.parse(text); } catch (e) { return {ok:false, raw:text}; }
}
async function post(path, body) {
  const out = await api(path, {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify(body)});
  if (body && body.child_id) last[body.child_id] = out.ok ? 'last ok' : `last error: ${out.error || 'failed'}`;
  log(out);
  await refresh();
}
async function refresh() {
  const status = await api('/api/status');
  document.getElementById('status').textContent = `role=${status.role} web_port=${status.web_port} children=${status.children_count}`;
  const children = await api('/api/children');
  document.getElementById('children').innerHTML = (children.children || []).map(c => `
    <tr class="${c.enabled ? '' : 'disabled'}">
      <td>${safe(c.child_id)}</td><td>${safe(c.display_name || '')}<br><small>${safe((c.aliases || []).join(', '))}</small></td><td>${safe(c.ip)}</td><td>${safe(c.port)}</td>
      <td><span class="pill ${c.enabled ? 'on' : 'off'}">${c.enabled ? 'enabled' : 'disabled'}</span><br>${safe(last[c.child_id] || 'not tested')}</td>
      <td>
        <button class="secondary" data-child="${jsonAttr(c)}" onclick="editChild(JSON.parse(this.dataset.child))">Edit</button>
        <button onclick="post('/api/children/ping',{child_id:${jsonAttr(c.child_id)}})">Ping</button>
        <button onclick="post('/api/children/command',{child_id:${jsonAttr(c.child_id)},action:'thinking',duration_ms:500})">Thinking</button>
        <button onclick="post('/api/children/command',{child_id:${jsonAttr(c.child_id)},action:'happy',duration_ms:500})">Happy</button>
        <button onclick="post('/api/children/command',{child_id:${jsonAttr(c.child_id)},action:'speak_hello',duration_ms:500})">Hello</button>
        <button onclick="post('/api/children/command',{child_id:${jsonAttr(c.child_id)},action:'sound_laugh',duration_ms:500})">Laugh sound</button>
        <button onclick="post('/api/children/command',{child_id:${jsonAttr(c.child_id)},action:'sound_cry',duration_ms:500})">Cry sound</button>
        <button class="secondary" onclick="post('/api/children/enabled',{child_id:${jsonAttr(c.child_id)},enabled:${!c.enabled}})">${c.enabled ? 'Disable' : 'Enable'}</button>
        <button class="warn" onclick="post('/api/children/stop',{child_id:${jsonAttr(c.child_id)}})">Stop</button>
        <button class="warn" onclick="post('/api/children/remove',{child_id:${jsonAttr(c.child_id)}})">Remove</button>
      </td>
    </tr>`).join('');
}
function editChild(child) {
  document.getElementById('child_id').value = child.child_id || '';
  document.getElementById('display_name').value = child.display_name || '';
  document.getElementById('aliases').value = (child.aliases || []).join(', ');
  document.getElementById('ip').value = child.ip || '';
  document.getElementById('port').value = child.port || 8081;
  document.getElementById('enabled').checked = child.enabled !== false;
  log({editing: child});
  window.scrollTo({top:0, behavior:'smooth'});
}
function clearForm() {
  document.getElementById('child_id').value = '';
  document.getElementById('display_name').value = '';
  document.getElementById('aliases').value = '';
  document.getElementById('ip').value = '';
  document.getElementById('port').value = 8081;
  document.getElementById('enabled').checked = true;
}
async function addChild() {
  await post('/api/children/add', {
    child_id: document.getElementById('child_id').value,
    display_name: document.getElementById('display_name').value,
    aliases: document.getElementById('aliases').value,
    ip: document.getElementById('ip').value,
    port: Number(document.getElementById('port').value || 8081),
    enabled: document.getElementById('enabled').checked
  });
}
refresh();
</script>
</body>
</html>)html";
    }

    static void SetStatus(httpd_req_t* req, int status_code) {
        if (status_code == 400) {
            httpd_resp_set_status(req, "400 Bad Request");
        } else if (status_code == 404) {
            httpd_resp_set_status(req, "404 Not Found");
        } else if (status_code == 409) {
            httpd_resp_set_status(req, "409 Conflict");
        } else if (status_code >= 500) {
            httpd_resp_set_status(req, "500 Internal Server Error");
        }
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

        SetStatus(req, status_code);
        httpd_resp_set_type(req, "application/json");
        auto err = httpd_resp_sendstr(req, raw);
        cJSON_free(raw);
        return err;
    }

    static cJSON* BaseResponse(bool ok) {
        auto* root = cJSON_CreateObject();
        cJSON_AddBoolToObject(root, "ok", ok);
        cJSON_AddStringToObject(root, "role", "parent");
        cJSON_AddStringToObject(root, "server", "parent_web_config_server");
        return root;
    }

    static esp_err_t SendError(httpd_req_t* req, const char* error, int status_code = 400) {
        auto* root = BaseResponse(false);
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

    static int JsonInt(cJSON* root, const char* key, int fallback) {
        auto* item = cJSON_GetObjectItem(root, key);
        if (cJSON_IsNumber(item)) {
            return item->valueint;
        }
        return fallback;
    }

    static bool JsonBool(cJSON* root, const char* key, bool fallback) {
        auto* item = cJSON_GetObjectItem(root, key);
        if (cJSON_IsBool(item)) {
            return cJSON_IsTrue(item);
        }
        return fallback;
    }

    static std::vector<std::string> JsonAliases(cJSON* root) {
        std::vector<std::string> aliases;
        auto* item = cJSON_GetObjectItem(root, "aliases");
        if (cJSON_IsArray(item)) {
            const int size = cJSON_GetArraySize(item);
            for (int i = 0; i < size; ++i) {
                auto* alias_item = cJSON_GetArrayItem(item, i);
                if (cJSON_IsString(alias_item) && alias_item->valuestring != nullptr) {
                    aliases.push_back(alias_item->valuestring);
                }
            }
            return aliases;
        }
        if (cJSON_IsString(item) && item->valuestring != nullptr) {
            std::string raw = item->valuestring;
            size_t start = 0;
            while (start <= raw.size()) {
                const size_t end = raw.find_first_of(",;", start);
                auto alias = raw.substr(start, end == std::string::npos ? std::string::npos : end - start);
                while (!alias.empty() && (alias.front() == ' ' || alias.front() == '\t')) {
                    alias.erase(alias.begin());
                }
                while (!alias.empty() && (alias.back() == ' ' || alias.back() == '\t')) {
                    alias.pop_back();
                }
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

    bool GetChildFromBody(httpd_req_t* req, ChildCarInfo* child, cJSON** body_out = nullptr) {
        cJSON* body = nullptr;
        auto err = ReadJsonBody(req, &body);
        if (err == ESP_ERR_NO_MEM) {
            SendError(req, "body_too_large", 400);
            return false;
        }
        if (err != ESP_OK) {
            SendError(req, "bad_json", 400);
            return false;
        }

        const auto child_id = JsonString(body, "child_id", "");
        if (!registry_.FindBySelector(child_id, child)) {
            cJSON_Delete(body);
            SendError(req, "child_not_found", 404);
            return false;
        }
        if (!child->enabled) {
            cJSON_Delete(body);
            SendError(req, "child_disabled", 409);
            return false;
        }

        if (body_out != nullptr) {
            *body_out = body;
        } else {
            cJSON_Delete(body);
        }
        return true;
    }

    esp_err_t HandleIndex(httpd_req_t* req) {
        httpd_resp_set_type(req, "text/html");
        return httpd_resp_sendstr(req, IndexHtml());
    }

    esp_err_t HandleStatus(httpd_req_t* req) {
        auto* root = BaseResponse(true);
        cJSON_AddNumberToObject(root, "web_port", CONFIG_CONTROL_CAR_PARENT_WEB_PORT);
        cJSON_AddNumberToObject(root, "child_command_port", CONFIG_CONTROL_CAR_CHILD_COMMAND_PORT);
        cJSON_AddNumberToObject(root, "children_count", static_cast<double>(registry_.Count()));
        ESP_LOGI(TAG, "GET /api/status");
        return SendJson(req, root);
    }

    esp_err_t HandleChildren(httpd_req_t* req) {
        auto* root = BaseResponse(true);
        cJSON_AddItemToObject(root, "children", registry_.ToJsonArray());
        ESP_LOGI(TAG, "GET /api/children");
        return SendJson(req, root);
    }

    esp_err_t HandleAddChild(httpd_req_t* req) {
        cJSON* body = nullptr;
        auto err = ReadJsonBody(req, &body);
        if (err == ESP_ERR_NO_MEM) {
            return SendError(req, "body_too_large", 400);
        }
        if (err != ESP_OK) {
            return SendError(req, "bad_json", 400);
        }

        const auto child_id = JsonString(body, "child_id", "");
        const auto display_name = JsonString(body, "display_name", "");
        const auto aliases = JsonAliases(body);
        const auto ip = JsonString(body, "ip", "");
        const int port = JsonInt(body, "port", CONFIG_CONTROL_CAR_CHILD_COMMAND_PORT);
        const bool enabled = JsonBool(body, "enabled", true);
        cJSON_Delete(body);

        std::string error;
        if (!registry_.Upsert(child_id, display_name, aliases, ip, port, enabled, &error)) {
            return SendError(req, error.c_str(), 400);
        }

        auto* root = BaseResponse(true);
        cJSON_AddStringToObject(root, "result", "child_saved");
        cJSON_AddStringToObject(root, "child_id", child_id.c_str());
        ESP_LOGI(TAG, "POST /api/children/add child_id=%s ip=%s port=%d", child_id.c_str(), ip.c_str(), port);
        return SendJson(req, root);
    }

    esp_err_t HandleRemoveChild(httpd_req_t* req) {
        cJSON* body = nullptr;
        auto err = ReadJsonBody(req, &body);
        if (err == ESP_ERR_NO_MEM) {
            return SendError(req, "body_too_large", 400);
        }
        if (err != ESP_OK) {
            return SendError(req, "bad_json", 400);
        }

        const auto child_id = JsonString(body, "child_id", "");
        cJSON_Delete(body);
        if (!registry_.Remove(child_id)) {
            return SendError(req, "child_not_found", 404);
        }

        auto* root = BaseResponse(true);
        cJSON_AddStringToObject(root, "result", "child_removed");
        cJSON_AddStringToObject(root, "child_id", child_id.c_str());
        ESP_LOGI(TAG, "POST /api/children/remove child_id=%s", child_id.c_str());
        return SendJson(req, root);
    }

    esp_err_t HandleSetChildEnabled(httpd_req_t* req) {
        cJSON* body = nullptr;
        auto err = ReadJsonBody(req, &body);
        if (err == ESP_ERR_NO_MEM) {
            return SendError(req, "body_too_large", 400);
        }
        if (err != ESP_OK) {
            return SendError(req, "bad_json", 400);
        }

        const auto child_id = JsonString(body, "child_id", "");
        const bool enabled = JsonBool(body, "enabled", true);
        cJSON_Delete(body);

        if (!registry_.SetEnabled(child_id, enabled)) {
            return SendError(req, "child_not_found", 404);
        }

        auto* root = BaseResponse(true);
        cJSON_AddStringToObject(root, "result", enabled ? "child_enabled" : "child_disabled");
        cJSON_AddStringToObject(root, "child_id", child_id.c_str());
        cJSON_AddBoolToObject(root, "enabled", enabled);
        ESP_LOGI(TAG, "POST /api/children/enabled child_id=%s enabled=%d", child_id.c_str(), enabled ? 1 : 0);
        return SendJson(req, root);
    }

    esp_err_t HandlePingChild(httpd_req_t* req) {
        cJSON* body = nullptr;
        auto err = ReadJsonBody(req, &body);
        if (err == ESP_ERR_NO_MEM) {
            return SendError(req, "body_too_large", 400);
        }
        if (err != ESP_OK) {
            return SendError(req, "bad_json", 400);
        }
        const auto child_id = JsonString(body, "child_id", "");
        cJSON_Delete(body);
        return ExecuteSelector(req, child_id, "ping", 0);
    }

    esp_err_t HandleStopChild(httpd_req_t* req) {
        cJSON* body = nullptr;
        auto err = ReadJsonBody(req, &body);
        if (err == ESP_ERR_NO_MEM) {
            return SendError(req, "body_too_large", 400);
        }
        if (err != ESP_OK) {
            return SendError(req, "bad_json", 400);
        }
        const auto child_id = JsonString(body, "child_id", "");
        cJSON_Delete(body);
        return ExecuteSelector(req, child_id, "stop", 0);
    }

    esp_err_t HandleCommandChild(httpd_req_t* req) {
        cJSON* body = nullptr;
        auto err = ReadJsonBody(req, &body);
        if (err == ESP_ERR_NO_MEM) {
            return SendError(req, "body_too_large", 400);
        }
        if (err != ESP_OK) {
            return SendError(req, "bad_json", 400);
        }
        const auto child_id = JsonString(body, "child_id", "");
        const auto action = JsonString(body, "action", "");
        const int duration_ms = JsonInt(body, "duration_ms", 500);
        cJSON_Delete(body);

        if (action.empty()) {
            return SendError(req, "missing_action", 400);
        }
        return ExecuteSelector(req, child_id, action, duration_ms);
    }

    static bool IsAllTarget(const std::string& child_id) {
        return child_id == "all" || child_id == "tat ca" || child_id == "tat_ca";
    }

    esp_err_t ExecuteSelector(httpd_req_t* req, const std::string& child_id, const std::string& action, int duration_ms) {
        if (IsAllTarget(child_id)) {
            try {
                const auto response = ParentChildClient::SendCommandToSelector("all", action, duration_ms);
                auto* root = BaseResponse(true);
                cJSON_AddStringToObject(root, "target", "all");
                cJSON_AddStringToObject(root, "action", action.c_str());
                cJSON_AddStringToObject(root, "children_response", response.c_str());
                ESP_LOGI(TAG, "Child command target=all action=%s done", action.c_str());
                return SendJson(req, root);
            } catch (const std::exception& ex) {
                auto* root = BaseResponse(false);
                cJSON_AddStringToObject(root, "target", "all");
                cJSON_AddStringToObject(root, "action", action.c_str());
                cJSON_AddStringToObject(root, "error", ex.what());
                ESP_LOGW(TAG, "Child command target=all action=%s failed: %s", action.c_str(), ex.what());
                return SendJson(req, root, 500);
            }
        }

        ChildCarInfo child;
        if (!registry_.FindBySelector(child_id, &child)) {
            return SendError(req, "child_not_found", 404);
        }
        if (!child.enabled) {
            return SendError(req, "child_disabled", 409);
        }
        return ExecuteChild(req, child, action, duration_ms);
    }

    esp_err_t ExecuteChild(httpd_req_t* req, const ChildCarInfo& child, const std::string& action, int duration_ms) {
        try {
            const auto child_response = ParentChildClient::SendCommand(child.ip, action, duration_ms, child.port);
            auto* root = BaseResponse(true);
            cJSON_AddStringToObject(root, "child_id", child.child_id.c_str());
            cJSON_AddStringToObject(root, "action", action.c_str());
            cJSON_AddStringToObject(root, "child_response", child_response.c_str());
            ESP_LOGI(TAG, "Child command child_id=%s action=%s ok", child.child_id.c_str(), action.c_str());
            return SendJson(req, root);
        } catch (const std::exception& ex) {
            auto* root = BaseResponse(false);
            cJSON_AddStringToObject(root, "child_id", child.child_id.c_str());
            cJSON_AddStringToObject(root, "action", action.c_str());
            cJSON_AddStringToObject(root, "error", ex.what());
            ESP_LOGW(TAG, "Child command child_id=%s action=%s failed: %s", child.child_id.c_str(), action.c_str(), ex.what());
            return SendJson(req, root, 500);
        }
    }

    static esp_err_t IndexHandler(httpd_req_t* req) {
        return static_cast<ParentWebConfigServer*>(req->user_ctx)->HandleIndex(req);
    }

    static esp_err_t StatusHandler(httpd_req_t* req) {
        return static_cast<ParentWebConfigServer*>(req->user_ctx)->HandleStatus(req);
    }

    static esp_err_t ChildrenHandler(httpd_req_t* req) {
        return static_cast<ParentWebConfigServer*>(req->user_ctx)->HandleChildren(req);
    }

    static esp_err_t AddChildHandler(httpd_req_t* req) {
        return static_cast<ParentWebConfigServer*>(req->user_ctx)->HandleAddChild(req);
    }

    static esp_err_t RemoveChildHandler(httpd_req_t* req) {
        return static_cast<ParentWebConfigServer*>(req->user_ctx)->HandleRemoveChild(req);
    }

    static esp_err_t SetChildEnabledHandler(httpd_req_t* req) {
        return static_cast<ParentWebConfigServer*>(req->user_ctx)->HandleSetChildEnabled(req);
    }

    static esp_err_t PingChildHandler(httpd_req_t* req) {
        return static_cast<ParentWebConfigServer*>(req->user_ctx)->HandlePingChild(req);
    }

    static esp_err_t StopChildHandler(httpd_req_t* req) {
        return static_cast<ParentWebConfigServer*>(req->user_ctx)->HandleStopChild(req);
    }

    static esp_err_t CommandChildHandler(httpd_req_t* req) {
        return static_cast<ParentWebConfigServer*>(req->user_ctx)->HandleCommandChild(req);
    }

    void RegisterUri(const char* uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t*)) {
        httpd_uri_t route = {
            .uri = uri,
            .method = method,
            .handler = handler,
            .user_ctx = this
        };
        ESP_ERROR_CHECK(httpd_register_uri_handler(server_, &route));
    }

public:
    ~ParentWebConfigServer() {
        Stop();
    }

    bool Start(int port = CONFIG_CONTROL_CAR_PARENT_WEB_PORT) {
        if (!control_car::IsParent()) {
            return false;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        if (server_ != nullptr) {
            return true;
        }

        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.server_port = port;
        config.ctrl_port = 32772;
        config.max_uri_handlers = 12;
        config.max_open_sockets = 4;
        config.lru_purge_enable = true;

        if (httpd_start(&server_, &config) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start parent web config server on port %d", port);
            server_ = nullptr;
            return false;
        }

        RegisterUri("/", HTTP_GET, IndexHandler);
        RegisterUri("/api/status", HTTP_GET, StatusHandler);
        RegisterUri("/api/children", HTTP_GET, ChildrenHandler);
        RegisterUri("/api/children/add", HTTP_POST, AddChildHandler);
        RegisterUri("/api/children/remove", HTTP_POST, RemoveChildHandler);
        RegisterUri("/api/children/enabled", HTTP_POST, SetChildEnabledHandler);
        RegisterUri("/api/children/ping", HTTP_POST, PingChildHandler);
        RegisterUri("/api/children/command", HTTP_POST, CommandChildHandler);
        RegisterUri("/api/children/stop", HTTP_POST, StopChildHandler);

        ESP_LOGI(TAG, "Parent web config server started on port %d", port);
        return true;
    }

    void Stop() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (server_ != nullptr) {
            httpd_stop(server_);
            server_ = nullptr;
        }
    }
};

} // namespace control_car

#endif // CONTROL_CAR_PARENT_WEB_CONFIG_SERVER_H
