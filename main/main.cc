#include <esp_log.h>
#include "settings.h"
#include <esp_err.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <driver/gpio.h>
#include <esp_event.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "application.h"

#include <esp_mac.h>

#define TAG "main"

extern "C" void app_main(void)
{
    // Initialize NVS flash for WiFi configuration
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing NVS flash to fix corruption");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // --- MAC SPOOFING HACK ---
    // Change MAC address to force Xiaozhi server to think this is a brand new device
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    
    // We use Settings to store a random offset so the MAC stays consistent across reboots.
    // If NVS is erased (Factory Reset), a NEW random offset will be generated!
    Settings settings("system", true);
    int mac_offset = settings.GetInt("mac_offset", -1);
    if (mac_offset == -1) {
        mac_offset = esp_random() % 255;
        settings.SetInt("mac_offset", mac_offset);
        ESP_LOGW(TAG, "Generated new MAC offset: 0x%02x", mac_offset);
    }
    mac[5] = mac[5] ^ mac_offset;
    esp_base_mac_addr_set(mac);
    ESP_LOGW(TAG, "Spoofed MAC Address: %02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    // -------------------------

    // Initialize and run the application
    auto& app = Application::GetInstance();
    app.Initialize();
    app.Run();  // This function runs the main event loop and never returns
}
