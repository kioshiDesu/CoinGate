#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "wifi_manager.h"
#include "web_server.h"
#include "coin_acceptor.h"
#include "rate_config.h"
#include "mikrotik_api.h"
#include "config_manager.h"

static const char *TAG = "main";

static void coin_event_handler(const coin_event_t *event)
{
    switch (event->type) {
        case COIN_EVENT_INSERTED:
            ESP_LOGI(TAG, "Coin inserted! Session pulses: %lu", 
                     coin_acceptor_get_session_pulses());
            break;
        case COIN_EVENT_SUSPICIOUS:
            ESP_LOGW(TAG, "Suspicious coin activity detected");
            break;
        case COIN_EVENT_ERROR:
            ESP_LOGE(TAG, "Coin acceptor error");
            break;
    }
}

static void mikrotik_state_handler(mikrotik_state_t state, void *user_data)
{
    switch (state) {
        case MIKROTIK_CONNECTED:
            ESP_LOGI(TAG, "MikroTik connected");
            break;
        case MIKROTIK_DISCONNECTED:
            ESP_LOGW(TAG, "MikroTik disconnected");
            break;
        case MIKROTIK_ERROR:
            ESP_LOGE(TAG, "MikroTik connection error");
            break;
        default:
            break;
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "===========================================");
    ESP_LOGI(TAG, "  CoinGate ESP32 Firmware v1.0");
    ESP_LOGI(TAG, "  Coin-operated WiFi Vending Machine");
    ESP_LOGI(TAG, "===========================================");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized");

    config_manager_init();
    ESP_LOGI(TAG, "Config manager initialized");

    ret = rate_config_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Rate config init failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Rate configuration loaded");
    }

    mikrotik_config_t mt_config = {0};
    char host[64] = "", user[32] = "", pass[64] = "";
    uint16_t port = 8728;
    
    if (config_manager_get_mikrotik_config(host, &port, user, pass, sizeof(pass)) == ESP_OK) {
        if (strlen(host) > 0) {
            strncpy(mt_config.host, host, sizeof(mt_config.host) - 1);
            mt_config.port = port;
            strncpy(mt_config.username, user, sizeof(mt_config.username) - 1);
            strncpy(mt_config.password, pass, sizeof(mt_config.password) - 1);
            mt_config.timeout_ms = 5000;
            mt_config.use_tls = false;
            
            mikrotik_init(&mt_config);
            ESP_LOGI(TAG, "MikroTik API initialized for %s", host);
            mikrotik_set_callback(mikrotik_state_handler, NULL);
        }
    }

    ret = coin_acceptor_init(COIN_ACCEPTOR_GPIO_PIN);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Coin acceptor init failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Coin acceptor initialized on GPIO %d", COIN_ACCEPTOR_GPIO_PIN);
        coin_acceptor_set_callback(coin_event_handler);
    }

    ret = wifi_manager_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi manager start failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "WiFi manager started");
    }

    ret = web_server_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Web server start failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Web server started");
    }

    ret = coin_acceptor_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Coin acceptor start failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Coin acceptor started");
    }

    if (mikrotik_is_connected()) {
        ESP_LOGI(TAG, "Connecting to MikroTik...");
        mikrotik_connect();
    }

    ESP_LOGI(TAG, "CoinGate initialized successfully!");
    ESP_LOGI(TAG, "Access web interface at setup AP or configured IP");

    uint32_t tick = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        tick++;
        
        if (tick % 12 == 0) {
            ESP_LOGD(TAG, "System running... | Pulses: %lu | MT: %s",
                     coin_acceptor_get_session_pulses(),
                     mikrotik_is_connected() ? "Connected" : "Disconnected");
        }
        
        if (tick % 60 == 0 && wifi_manager_is_connected() && !mikrotik_is_connected()) {
            if (strlen(mt_config.host) > 0) {
                ESP_LOGI(TAG, "Attempting MikroTik reconnection...");
                mikrotik_connect();
            }
        }
    }
}
