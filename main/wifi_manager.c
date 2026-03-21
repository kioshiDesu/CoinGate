#include "wifi_manager.h"
#include <string.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "config_manager.h"

static const char *TAG = "wifi_manager";
static bool is_connected = false;
static bool is_initialized = false;

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        is_connected = false;
        ESP_LOGI(TAG, "WiFi disconnected, reconnecting...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        is_connected = true;
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "Station connected, MAC: " MACSTR, MAC2STR(event->mac));
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "Station disconnected, MAC: " MACSTR, MAC2STR(event->mac));
    }
}

static void start_sta_mode(const char *ssid, const char *password)
{
    wifi_config_t wifi_config = {0};
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    if (password && strlen(password) > 0) {
        strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    }
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI(TAG, "WiFi started in STA mode, connecting to %s", ssid);
}

static void start_ap_mode(void)
{
    wifi_config_t ap_config = {
        .ap = {
            .ssid = "CoinGate_Setup",
            .ssid_len = 0,
            .password = "CoinGate123",
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        }
    };
    ap_config.ap.ssid_len = strlen((char*)ap_config.ap.ssid);
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI(TAG, "WiFi started in AP mode. SSID: CoinGate_Setup");
}

esp_err_t wifi_manager_start(void)
{
    if (is_initialized) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Starting WiFi Manager");

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                      ESP_EVENT_ANY_ID,
                                                      &wifi_event_handler,
                                                      NULL,
                                                      NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                      IP_EVENT_STA_GOT_IP,
                                                      &wifi_event_handler,
                                                      NULL,
                                                      NULL));

    char ssid[64] = "", password[128] = "";
    esp_err_t err = config_manager_get_wifi_ssid(ssid, sizeof(ssid));
    if (err == ESP_OK && strlen(ssid) > 0) {
        config_manager_get_wifi_password(password, sizeof(password));
        start_sta_mode(ssid, password);
    } else {
        start_ap_mode();
    }

    is_initialized = true;
    return ESP_OK;
}

void wifi_manager_reconnect(void)
{
    if (!is_initialized) {
        wifi_manager_start();
        return;
    }
    
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    char ssid[64] = "", password[128] = "";
    esp_err_t err = config_manager_get_wifi_ssid(ssid, sizeof(ssid));
    
    if (err == ESP_OK && strlen(ssid) > 0) {
        config_manager_get_wifi_password(password, sizeof(password));
        
        wifi_config_t wifi_config = {0};
        strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
        if (strlen(password) > 0) {
            strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
        }
        
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_start());
        
        ESP_LOGI(TAG, "Reconnecting to %s", ssid);
    } else {
        start_ap_mode();
    }
}

bool wifi_manager_is_connected(void)
{
    return is_connected;
}
