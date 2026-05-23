#ifndef __CAR_BLE_CONTROLLER_H__
#define __CAR_BLE_CONTROLLER_H__

#include "mcp_server.h"
#include "application.h"

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <sdkconfig.h>

#include <cstring>
#include <mutex>
#include <string>

#if CONFIG_BT_ENABLED && CONFIG_BT_BLUEDROID_ENABLED
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_gap_ble_api.h>
#include <esp_gatt_common_api.h>
#include <esp_gattc_api.h>
#endif

class CarBleController {
private:
    static constexpr const char* TAG = "CarBleController";
    static constexpr int kDefaultMoveMs = 600;
    static constexpr int kDefaultSpeed = 180;
    static constexpr int kMinMoveMs = 50;
    static constexpr int kMaxMoveMs = 4000;
    static constexpr int kBleReadyTimeoutMs = 7000;
    static constexpr int kWriteTimeoutMs = 1500;
    static constexpr int kReadTimeoutMs = 1500;
    static constexpr int kStatusSettleDelayMs = 70;
    static constexpr int kBleShutdownDelayMs = 80;
    static constexpr uint16_t kGattAppId = 0x42;
    static constexpr char kTargetName[] = "deskbot-c3-child";

    std::mutex mutex_;

    struct AudioStateSnapshot {
        bool wake_word_running = false;
        bool audio_processor_running = false;
    };

#if CONFIG_BT_ENABLED && CONFIG_BT_BLUEDROID_ENABLED
    static constexpr EventBits_t kBitBtReady = BIT0;
    static constexpr EventBits_t kBitScanParamsReady = BIT1;
    static constexpr EventBits_t kBitConnectionReady = BIT2;
    static constexpr EventBits_t kBitWriteDone = BIT3;
    static constexpr EventBits_t kBitReadDone = BIT4;

    static inline CarBleController* instance_ = nullptr;

    EventGroupHandle_t event_group_ = nullptr;
    bool bt_ready_ = false;
    bool scanning_ = false;
    bool target_found_ = false;
    bool service_found_ = false;
    esp_gatt_if_t gattc_if_ = ESP_GATT_IF_NONE;
    uint16_t conn_id_ = 0;
    uint16_t service_start_handle_ = 0;
    uint16_t service_end_handle_ = 0;
    uint16_t command_handle_ = 0;
    uint16_t status_handle_ = 0;
    esp_ble_addr_type_t remote_addr_type_ = BLE_ADDR_TYPE_PUBLIC;
    esp_bd_addr_t remote_bda_ = {0};
    esp_gatt_status_t last_write_status_ = ESP_GATT_ERROR;
    esp_gatt_status_t last_read_status_ = ESP_GATT_ERROR;
    std::string last_read_value_;
    std::string last_error_ = "parent_ble_uninitialized";

    static constexpr uint8_t kServiceUuidBytes[16] = {0x60, 0x2d, 0xac, 0xb8, 0x59, 0xdf, 0x1c, 0xb8, 0x3f, 0x4e, 0x31, 0x57, 0xf0, 0x9d, 0x2d, 0x2a};
    static constexpr uint8_t kCommandUuidBytes[16] = {0x60, 0x2d, 0xac, 0xb8, 0x59, 0xdf, 0x1c, 0xb8, 0x3f, 0x4e, 0x31, 0x57, 0xf1, 0x9d, 0x2d, 0x2a};
    static constexpr uint8_t kStatusUuidBytes[16] = {0x60, 0x2d, 0xac, 0xb8, 0x59, 0xdf, 0x1c, 0xb8, 0x3f, 0x4e, 0x31, 0x57, 0xf2, 0x9d, 0x2d, 0x2a};

    static inline esp_ble_scan_params_t kScanParams = {
        .scan_type = BLE_SCAN_TYPE_ACTIVE,
        .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
        .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
        .scan_interval = 0x50,
        .scan_window = 0x30,
        .scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE
    };

    static esp_bt_uuid_t MakeUuid(const uint8_t (&bytes)[16]) {
        esp_bt_uuid_t uuid = {};
        uuid.len = ESP_UUID_LEN_128;
        memcpy(uuid.uuid.uuid128, bytes, sizeof(bytes));
        return uuid;
    }

    static bool UuidEquals(const esp_bt_uuid_t& lhs, const uint8_t (&bytes)[16]) {
        return lhs.len == ESP_UUID_LEN_128 && memcmp(lhs.uuid.uuid128, bytes, sizeof(bytes)) == 0;
    }

    static void GapCallback(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param) {
        if (instance_ != nullptr) {
            instance_->HandleGapEvent(event, param);
        }
    }

    static void GattcCallback(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t* param) {
        if (instance_ != nullptr) {
            instance_->HandleGattcEvent(event, gattc_if, param);
        }
    }

    void HandleGapEvent(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param) {
        switch (event) {
            case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
                xEventGroupSetBits(event_group_, kBitScanParamsReady);
                break;
            case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
                scanning_ = param->scan_start_cmpl.status == ESP_BT_STATUS_SUCCESS;
                break;
            case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
                scanning_ = false;
                if (target_found_ && gattc_if_ != ESP_GATT_IF_NONE) {
                    esp_ble_gattc_open(gattc_if_, remote_bda_, remote_addr_type_, true);
                }
                break;
            case ESP_GAP_BLE_SCAN_RESULT_EVT:
                HandleScanResult(param);
                break;
            default:
                break;
        }
    }

    void HandleScanResult(esp_ble_gap_cb_param_t* param) {
        auto& scan = param->scan_rst;
        if (scan.search_evt != ESP_GAP_SEARCH_INQ_RES_EVT) {
            return;
        }

        uint8_t length = 0;
        uint8_t* adv_name = esp_ble_resolve_adv_data(scan.ble_adv, ESP_BLE_AD_TYPE_NAME_CMPL, &length);
        if (adv_name == nullptr || length == 0) {
            adv_name = esp_ble_resolve_adv_data(scan.ble_adv, ESP_BLE_AD_TYPE_NAME_SHORT, &length);
        }
        if (adv_name == nullptr || length == 0) {
            return;
        }

        std::string name(reinterpret_cast<char*>(adv_name), length);
        if (name != kTargetName) {
            return;
        }

        ESP_LOGI(TAG, "Found child robot BLE advertiser: %s", name.c_str());
        memcpy(remote_bda_, scan.bda, sizeof(remote_bda_));
        remote_addr_type_ = scan.ble_addr_type;
        target_found_ = true;
        if (scanning_) {
            esp_ble_gap_stop_scanning();
        }
    }

    void HandleGattcEvent(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t* param) {
        switch (event) {
            case ESP_GATTC_REG_EVT:
                if (param->reg.status == ESP_GATT_OK) {
                    gattc_if_ = gattc_if;
                    xEventGroupSetBits(event_group_, kBitBtReady);
                }
                break;
            case ESP_GATTC_OPEN_EVT:
                if (param->open.status == ESP_GATT_OK) {
                    conn_id_ = param->open.conn_id;
                    service_found_ = false;
                    service_start_handle_ = 0;
                    service_end_handle_ = 0;
                    command_handle_ = 0;
                    status_handle_ = 0;
                    esp_ble_gattc_search_service(gattc_if_, conn_id_, nullptr);
                }
                break;
            case ESP_GATTC_SEARCH_RES_EVT:
                if (UuidEquals(param->search_res.srvc_id.uuid, kServiceUuidBytes)) {
                    service_found_ = true;
                    service_start_handle_ = param->search_res.start_handle;
                    service_end_handle_ = param->search_res.end_handle;
                }
                break;
            case ESP_GATTC_SEARCH_CMPL_EVT:
                ResolveHandlesFromCache();
                break;
            case ESP_GATTC_WRITE_CHAR_EVT:
                last_write_status_ = param->write.status;
                xEventGroupSetBits(event_group_, kBitWriteDone);
                break;
            case ESP_GATTC_READ_CHAR_EVT:
                last_read_status_ = param->read.status;
                if (param->read.status == ESP_GATT_OK && param->read.value != nullptr && param->read.value_len > 0) {
                    last_read_value_.assign(reinterpret_cast<char*>(param->read.value), param->read.value_len);
                } else {
                    last_read_value_.clear();
                }
                xEventGroupSetBits(event_group_, kBitReadDone);
                break;
            case ESP_GATTC_DISCONNECT_EVT:
                ClearConnectionState();
                break;
            default:
                break;
        }
    }

    void ResolveHandlesFromCache() {
        if (!service_found_ || service_start_handle_ == 0 || service_end_handle_ == 0) {
            return;
        }

        esp_gattc_char_elem_t command_char = {};
        esp_gattc_char_elem_t status_char = {};
        uint16_t count = 1;
        esp_gatt_status_t status = esp_ble_gattc_get_char_by_uuid(
            gattc_if_,
            conn_id_,
            service_start_handle_,
            service_end_handle_,
            MakeUuid(kCommandUuidBytes),
            &command_char,
            &count
        );
        if (status != ESP_GATT_OK || count == 0) {
            return;
        }

        count = 1;
        status = esp_ble_gattc_get_char_by_uuid(
            gattc_if_,
            conn_id_,
            service_start_handle_,
            service_end_handle_,
            MakeUuid(kStatusUuidBytes),
            &status_char,
            &count
        );
        if (status != ESP_GATT_OK || count == 0) {
            return;
        }

        command_handle_ = command_char.char_handle;
        status_handle_ = status_char.char_handle;
        xEventGroupSetBits(event_group_, kBitConnectionReady);
    }

    void ClearConnectionState() {
        xEventGroupClearBits(event_group_, kBitConnectionReady | kBitWriteDone | kBitReadDone);
        target_found_ = false;
        service_found_ = false;
        command_handle_ = 0;
        status_handle_ = 0;
        conn_id_ = 0;
        memset(remote_bda_, 0, sizeof(remote_bda_));
    }

    AudioStateSnapshot PauseAudioForBleOperationLocked() {
        auto& app = Application::GetInstance();
        auto& audio_service = Application::GetInstance().GetAudioService();
        auto state = app.GetDeviceState();
        if (state == kDeviceStateSpeaking || !audio_service.IsIdle()) {
            ESP_LOGI(TAG, "Waiting for playback to drain before BLE: state=%d idle=%d",
                static_cast<int>(state), audio_service.IsIdle());
            audio_service.WaitForPlaybackQueueEmpty();
            vTaskDelay(pdMS_TO_TICKS(kStatusSettleDelayMs));
        }

        AudioStateSnapshot snapshot = {
            .wake_word_running = audio_service.IsWakeWordRunning(),
            .audio_processor_running = audio_service.IsAudioProcessorRunning(),
        };
        ESP_LOGI(TAG, "Pause audio for BLE: wake_word=%d audio_processor=%d free_heap=%" PRIu32,
            snapshot.wake_word_running, snapshot.audio_processor_running, esp_get_free_heap_size());

        if (snapshot.audio_processor_running) {
            audio_service.EnableVoiceProcessing(false);
        }
        if (snapshot.wake_word_running) {
            audio_service.EnableWakeWordDetection(false);
        }
        return snapshot;
    }

    void ResumeAudioAfterBleOperationLocked(const AudioStateSnapshot& snapshot) {
        auto& audio_service = Application::GetInstance().GetAudioService();
        ESP_LOGI(TAG, "Resume audio after BLE: wake_word=%d audio_processor=%d free_heap=%" PRIu32,
            snapshot.wake_word_running, snapshot.audio_processor_running, esp_get_free_heap_size());
        if (snapshot.wake_word_running) {
            audio_service.EnableWakeWordDetection(true);
        }
        if (snapshot.audio_processor_running) {
            audio_service.EnableVoiceProcessing(true);
        }
    }

    void ShutdownBleLocked() {
        if (event_group_ == nullptr) {
            bt_ready_ = false;
            last_error_ = "parent_ble_uninitialized";
            return;
        }
        ESP_LOGI(TAG, "Shutdown BLE session start");

        if (scanning_) {
            esp_ble_gap_stop_scanning();
            vTaskDelay(pdMS_TO_TICKS(kBleShutdownDelayMs));
        }

        if (gattc_if_ != ESP_GATT_IF_NONE && conn_id_ != 0) {
            esp_ble_gattc_close(gattc_if_, conn_id_);
            vTaskDelay(pdMS_TO_TICKS(kBleShutdownDelayMs));
        }

        if (gattc_if_ != ESP_GATT_IF_NONE) {
            esp_ble_gattc_app_unregister(gattc_if_);
            vTaskDelay(pdMS_TO_TICKS(kBleShutdownDelayMs));
        }

        if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_ENABLED) {
            esp_bluedroid_disable();
        }
        if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_INITIALIZED) {
            esp_bluedroid_deinit();
        }

        auto controller_status = esp_bt_controller_get_status();
        if (controller_status == ESP_BT_CONTROLLER_STATUS_ENABLED) {
            esp_bt_controller_disable();
            controller_status = esp_bt_controller_get_status();
        }
        if (controller_status == ESP_BT_CONTROLLER_STATUS_INITED) {
            esp_bt_controller_deinit();
        }

        bt_ready_ = false;
        scanning_ = false;
        gattc_if_ = ESP_GATT_IF_NONE;
        ClearConnectionState();
        xEventGroupClearBits(event_group_, kBitBtReady | kBitScanParamsReady);
        last_error_ = "parent_ble_uninitialized";
        ESP_LOGI(TAG, "Shutdown BLE session done free_heap=%" PRIu32, esp_get_free_heap_size());
    }

    bool EnsureBleReadyLocked() {
        if (bt_ready_) {
            last_error_.clear();
            return true;
        }
        ESP_LOGI(TAG, "EnsureBleReady start free_heap=%" PRIu32, esp_get_free_heap_size());

        instance_ = this;
        if (event_group_ == nullptr) {
            event_group_ = xEventGroupCreate();
        }

        esp_err_t err = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "esp_bt_controller_mem_release failed: %s", esp_err_to_name(err));
            last_error_ = "parent_ble_mem_release_failed";
            return false;
        }

        esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
        err = esp_bt_controller_init(&bt_cfg);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "esp_bt_controller_init failed: %s", esp_err_to_name(err));
            last_error_ = "parent_ble_controller_init_failed";
            return false;
        }

        err = esp_bt_controller_enable(ESP_BT_MODE_BLE);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "esp_bt_controller_enable failed: %s", esp_err_to_name(err));
            last_error_ = "parent_ble_controller_enable_failed";
            return false;
        }

        err = esp_bluedroid_init();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "esp_bluedroid_init failed: %s", esp_err_to_name(err));
            last_error_ = "parent_ble_host_init_failed";
            return false;
        }

        err = esp_bluedroid_enable();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "esp_bluedroid_enable failed: %s", esp_err_to_name(err));
            last_error_ = "parent_ble_host_enable_failed";
            return false;
        }

        err = esp_ble_gap_register_callback(GapCallback);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "esp_ble_gap_register_callback failed: %s", esp_err_to_name(err));
            last_error_ = "parent_ble_gap_callback_failed";
            return false;
        }

        err = esp_ble_gattc_register_callback(GattcCallback);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "esp_ble_gattc_register_callback failed: %s", esp_err_to_name(err));
            last_error_ = "parent_ble_gattc_callback_failed";
            return false;
        }

        err = esp_ble_gattc_app_register(kGattAppId);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "esp_ble_gattc_app_register failed: %s", esp_err_to_name(err));
            last_error_ = "parent_ble_gattc_register_failed";
            return false;
        }

        EventBits_t ready = xEventGroupWaitBits(
            event_group_,
            kBitBtReady,
            pdFALSE,
            pdTRUE,
            pdMS_TO_TICKS(kBleReadyTimeoutMs)
        );
        if ((ready & kBitBtReady) == 0) {
            ESP_LOGE(TAG, "BLE GATTC register timeout");
            last_error_ = "parent_ble_gattc_register_timeout";
            return false;
        }

        err = esp_ble_gap_set_scan_params(&kScanParams);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "esp_ble_gap_set_scan_params failed: %s", esp_err_to_name(err));
            last_error_ = "parent_ble_scan_params_failed";
            return false;
        }

        EventBits_t scan_ready = xEventGroupWaitBits(
            event_group_,
            kBitScanParamsReady,
            pdFALSE,
            pdTRUE,
            pdMS_TO_TICKS(kBleReadyTimeoutMs)
        );
        if ((scan_ready & kBitScanParamsReady) == 0) {
            ESP_LOGE(TAG, "BLE scan params timeout");
            last_error_ = "parent_ble_scan_params_timeout";
            return false;
        }

        bt_ready_ = true;
        last_error_.clear();
        ESP_LOGI(TAG, "EnsureBleReady done free_heap=%" PRIu32, esp_get_free_heap_size());
        return true;
    }

    bool EnsureConnectedLocked() {
        if (!EnsureBleReadyLocked()) {
            return false;
        }
        ESP_LOGI(TAG, "EnsureConnected start");

        EventBits_t bits = xEventGroupGetBits(event_group_);
        if ((bits & kBitConnectionReady) != 0 && command_handle_ != 0 && status_handle_ != 0) {
            return true;
        }

        target_found_ = false;
        service_found_ = false;
        xEventGroupClearBits(event_group_, kBitConnectionReady | kBitWriteDone | kBitReadDone);

        esp_err_t err = esp_ble_gap_start_scanning(5);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ble_gap_start_scanning failed: %s", esp_err_to_name(err));
            last_error_ = "child_ble_scan_start_failed";
            return false;
        }

        EventBits_t ready = xEventGroupWaitBits(
            event_group_,
            kBitConnectionReady,
            pdFALSE,
            pdTRUE,
            pdMS_TO_TICKS(kBleReadyTimeoutMs)
        );
        if ((ready & kBitConnectionReady) == 0) {
            last_error_ = target_found_ ? "child_ble_connect_timeout" : "child_ble_not_found";
            ESP_LOGW(TAG, "EnsureConnected failed: %s", last_error_.c_str());
            return false;
        }
        last_error_.clear();
        ESP_LOGI(TAG, "EnsureConnected done handles: cmd=%u status=%u", command_handle_, status_handle_);
        return true;
    }

    std::string ExecuteBleCommandLocked(const std::string& command, bool read_after_write = true) {
        auto audio_state = PauseAudioForBleOperationLocked();
        std::string result;
        ESP_LOGI(TAG, "Execute BLE command: %s", command.c_str());

        if (!EnsureConnectedLocked()) {
            result = last_error_.empty() ? "ble_connect_failed" : last_error_;
            ShutdownBleLocked();
            ResumeAudioAfterBleOperationLocked(audio_state);
            return result;
        }

        xEventGroupClearBits(event_group_, kBitWriteDone | kBitReadDone);
        last_write_status_ = ESP_GATT_ERROR;
        last_read_status_ = ESP_GATT_ERROR;
        last_read_value_.clear();

        esp_err_t err = esp_ble_gattc_write_char(
            gattc_if_,
            conn_id_,
            command_handle_,
            command.size(),
            reinterpret_cast<uint8_t*>(const_cast<char*>(command.data())),
            ESP_GATT_WRITE_TYPE_RSP,
            ESP_GATT_AUTH_REQ_NONE
        );
        if (err != ESP_OK) {
            last_error_ = "child_ble_write_start_failed";
            result = "ble_write_start_failed";
            ShutdownBleLocked();
            ResumeAudioAfterBleOperationLocked(audio_state);
            return result;
        }

        EventBits_t write_bits = xEventGroupWaitBits(
            event_group_,
            kBitWriteDone,
            pdTRUE,
            pdTRUE,
            pdMS_TO_TICKS(kWriteTimeoutMs)
        );
        if ((write_bits & kBitWriteDone) == 0 || last_write_status_ != ESP_GATT_OK) {
            last_error_ = "child_ble_write_failed";
            result = "ble_write_failed";
            ShutdownBleLocked();
            ResumeAudioAfterBleOperationLocked(audio_state);
            return result;
        }

        if (!read_after_write) {
            result = "ok";
            ShutdownBleLocked();
            ResumeAudioAfterBleOperationLocked(audio_state);
            return result;
        }

        vTaskDelay(pdMS_TO_TICKS(kStatusSettleDelayMs));
        err = esp_ble_gattc_read_char(gattc_if_, conn_id_, status_handle_, ESP_GATT_AUTH_REQ_NONE);
        if (err != ESP_OK) {
            last_error_ = "child_ble_read_start_failed";
            result = "ble_read_start_failed";
            ShutdownBleLocked();
            ResumeAudioAfterBleOperationLocked(audio_state);
            return result;
        }

        EventBits_t read_bits = xEventGroupWaitBits(
            event_group_,
            kBitReadDone,
            pdTRUE,
            pdTRUE,
            pdMS_TO_TICKS(kReadTimeoutMs)
        );
        if ((read_bits & kBitReadDone) == 0 || last_read_status_ != ESP_GATT_OK) {
            last_error_ = "child_ble_read_failed";
            result = "ble_read_failed";
            ShutdownBleLocked();
            ResumeAudioAfterBleOperationLocked(audio_state);
            return result;
        }

        if (last_read_value_.empty()) {
            last_error_ = "child_ble_empty_status";
            result = "ble_empty_status";
            ShutdownBleLocked();
            ResumeAudioAfterBleOperationLocked(audio_state);
            return result;
        }
        last_error_.clear();
        result = last_read_value_;
        ShutdownBleLocked();
        ResumeAudioAfterBleOperationLocked(audio_state);
        ESP_LOGI(TAG, "Execute BLE command result: %s", result.c_str());
        return result;
    }
#endif

    std::string MoveDescription() const {
        return
            "Điều khiển robot con di chuyển qua BLE.\n"
            "Chỉ dùng tool này khi người dùng đang ra lệnh cho robot con của bạn.\n"
            "Không dùng cho chính robot cha.\n"
            "Call ngay khi user nói robot con tiến/lùi/rẽ.\n"
            "Sau khi tool thành công thì im lặng.";
    }

    std::string FaceDescription() const {
        return
            "Đổi biểu cảm khuôn mặt của robot con qua BLE.\n"
            "Chỉ dùng khi user muốn robot con cười, buồn, giận, nghĩ, ngủ, cười nhẹ, cười lớn hoặc khóc.\n"
            "Sau khi tool thành công thì im lặng.";
    }

    std::string StopDescription() const {
        return
            "Dừng robot con ngay lập tức qua BLE.\n"
            "Ưu tiên dùng tool này khi user nói dừng lại, đứng im, ngừng di chuyển.\n"
            "Sau khi tool thành công thì im lặng.";
    }

    std::string StatusDescription() const {
        return
            "Lấy trạng thái hiện tại của robot con qua BLE.\n"
            "Dùng khi user hỏi robot con đang làm gì hoặc đang biểu cảm gì.";
    }

public:
    CarBleController() {
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
                const auto direction = properties["direction"].value<std::string>();
                const int duration_ms = properties["duration_ms"].value<int>();
                const int speed = properties["speed"].value<int>();
                return SendMove(direction, duration_ms, speed);
            }
        );

        mcp_server.AddTool(
            "self.child_robot.stop",
            StopDescription(),
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                std::lock_guard<std::mutex> lock(mutex_);
                return SendStop();
            }
        );

        mcp_server.AddTool(
            "self.child_robot.set_face",
            FaceDescription(),
            PropertyList({Property("face", kPropertyTypeString)}),
            [this](const PropertyList& properties) -> ReturnValue {
                std::lock_guard<std::mutex> lock(mutex_);
                return SendFace(properties["face"].value<std::string>());
            }
        );

        mcp_server.AddTool(
            "self.child_robot.get_status",
            StatusDescription(),
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                std::lock_guard<std::mutex> lock(mutex_);
                return GetStatus();
            }
        );

    }

    std::string SendMove(const std::string& direction, int duration_ms, int speed) {
#if CONFIG_BT_ENABLED && CONFIG_BT_BLUEDROID_ENABLED
        const std::string command = "MOVE " + direction + " " + std::to_string(duration_ms) + " " + std::to_string(speed);
        return ExecuteBleCommandLocked(command);
#else
        (void)direction;
        (void)duration_ms;
        (void)speed;
        return "ble_disabled_in_build";
#endif
    }

    std::string SendStop() {
#if CONFIG_BT_ENABLED && CONFIG_BT_BLUEDROID_ENABLED
        return ExecuteBleCommandLocked("STOP");
#else
        return "ble_disabled_in_build";
#endif
    }

    std::string SendFace(const std::string& face) {
#if CONFIG_BT_ENABLED && CONFIG_BT_BLUEDROID_ENABLED
        return ExecuteBleCommandLocked("FACE " + face);
#else
        (void)face;
        return "ble_disabled_in_build";
#endif
    }

    std::string GetStatus() {
#if CONFIG_BT_ENABLED && CONFIG_BT_BLUEDROID_ENABLED
        return ExecuteBleCommandLocked("STATUS");
#else
        return "ble_disabled_in_build";
#endif
    }
};

#endif
