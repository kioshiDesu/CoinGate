#include "config_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

static const char *TAG = "config_manager";
static const char *NVS_NS = "coingate";

static voucher_history_t s_voucher_history[MAX_VOUCHER_HISTORY];
static uint8_t s_voucher_count = 0;
static uint8_t s_voucher_next_index = 0;

esp_err_t config_manager_init(void)
{
    ESP_LOGI(TAG, "Initializing config manager");
    return ESP_OK;
}

esp_err_t config_manager_get_wifi_ssid(char *ssid, size_t max_len)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &nvs);
    if (err != ESP_OK) return err;
    
    size_t len = max_len;
    err = nvs_get_str(nvs, "wifi_ssid", ssid, &len);
    nvs_close(nvs);
    return err;
}

esp_err_t config_manager_set_wifi_ssid(const char *ssid)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    
    err = nvs_set_str(nvs, "wifi_ssid", ssid);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

esp_err_t config_manager_get_wifi_password(char *password, size_t max_len)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &nvs);
    if (err != ESP_OK) return err;
    
    size_t len = max_len;
    err = nvs_get_str(nvs, "wifi_pass", password, &len);
    nvs_close(nvs);
    return err;
}

esp_err_t config_manager_set_wifi_password(const char *password)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    
    err = nvs_set_str(nvs, "wifi_pass", password);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

esp_err_t config_manager_get_mikrotik_config(char *host, uint16_t *port, char *username, char *password, size_t max_len)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &nvs);
    if (err != ESP_OK) return err;
    
    size_t len;
    
    if (host) {
        len = max_len;
        nvs_get_str(nvs, "mt_host", host, &len);
    }
    
    if (port) {
        nvs_get_u16(nvs, "mt_port", port);
    }
    
    if (username) {
        len = max_len;
        nvs_get_str(nvs, "mt_user", username, &len);
    }
    
    if (password) {
        len = max_len;
        nvs_get_str(nvs, "mt_pass", password, &len);
    }
    
    nvs_close(nvs);
    return ESP_OK;
}

esp_err_t config_manager_set_mikrotik_config(const char *host, uint16_t port, const char *username, const char *password)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    
    if (host) err = nvs_set_str(nvs, "mt_host", host);
    if (err == ESP_OK && port) err = nvs_set_u16(nvs, "mt_port", port);
    if (err == ESP_OK && username) err = nvs_set_str(nvs, "mt_user", username);
    if (err == ESP_OK && password) err = nvs_set_str(nvs, "mt_pass", password);
    
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    
    nvs_close(nvs);
    return err;
}

esp_err_t config_manager_get_admin_password(char *password, size_t max_len)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &nvs);
    if (err != ESP_OK) return err;
    
    size_t len = max_len;
    err = nvs_get_str(nvs, "admin_pass", password, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        strncpy(password, "admin", max_len - 1);
        password[max_len - 1] = '\0';
        err = ESP_OK;
    }
    nvs_close(nvs);
    return err;
}

esp_err_t config_manager_set_admin_password(const char *password)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    
    err = nvs_set_str(nvs, "admin_pass", password);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

bool config_manager_check_admin_password(const char *password)
{
    char stored[65];
    size_t len = sizeof(stored);
    
    nvs_handle_t nvs;
    if (nvs_open(NVS_NS, NVS_READONLY, &nvs) != ESP_OK) {
        return strcmp(password, "admin") == 0;
    }
    
    esp_err_t err = nvs_get_str(nvs, "admin_pass", stored, &len);
    nvs_close(nvs);
    
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return strcmp(password, "admin") == 0;
    }
    
    return strcmp(password, stored) == 0;
}

bool config_manager_is_setup_complete(void)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NS, NVS_READONLY, &nvs) != ESP_OK) {
        return false;
    }
    
    uint8_t complete = 0;
    nvs_get_u8(nvs, "setup_done", &complete);
    nvs_close(nvs);
    
    return complete == 1;
}

esp_err_t config_manager_set_setup_complete(bool complete)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    
    err = nvs_set_u8(nvs, "setup_done", complete ? 1 : 0);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

esp_err_t config_manager_add_voucher_to_history(const voucher_history_t *voucher)
{
    if (voucher == NULL) return ESP_ERR_INVALID_ARG;
    
    memcpy(&s_voucher_history[s_voucher_next_index], voucher, sizeof(voucher_history_t));
    
    s_voucher_next_index = (s_voucher_next_index + 1) % MAX_VOUCHER_HISTORY;
    
    if (s_voucher_count < MAX_VOUCHER_HISTORY) {
        s_voucher_count++;
    }
    
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    
    char key[16];
    snprintf(key, sizeof(key), "vchr_%d", s_voucher_next_index - 1);
    err = nvs_set_blob(nvs, key, voucher, sizeof(voucher_history_t));
    
    if (err == ESP_OK) {
        err = nvs_set_u8(nvs, "vchr_cnt", s_voucher_count);
        err = nvs_set_u8(nvs, "vchr_next", s_voucher_next_index);
        
        uint32_t total_v, daily_v, total_p, daily_p;
        nvs_get_u32(nvs, "total_v", &total_v);
        nvs_get_u32(nvs, "daily_v", &daily_v);
        nvs_get_u32(nvs, "total_p", &total_p);
        nvs_get_u32(nvs, "daily_p", &daily_p);
        
        nvs_set_u32(nvs, "total_v", total_v + 1);
        nvs_set_u32(nvs, "daily_v", daily_v + 1);
        nvs_set_u32(nvs, "total_p", total_p + voucher->pulses_used);
        nvs_set_u32(nvs, "daily_p", daily_p + voucher->pulses_used);
        
        err = nvs_commit(nvs);
    }
    
    nvs_close(nvs);
    return err;
}

esp_err_t config_manager_get_voucher_history(voucher_history_t *history, uint8_t *count)
{
    if (history == NULL || count == NULL) return ESP_ERR_INVALID_ARG;
    
    *count = s_voucher_count;
    
    uint8_t start = (s_voucher_next_index + MAX_VOUCHER_HISTORY - s_voucher_count) % MAX_VOUCHER_HISTORY;
    
    for (uint8_t i = 0; i < s_voucher_count; i++) {
        uint8_t idx = (start + i) % MAX_VOUCHER_HISTORY;
        memcpy(&history[i], &s_voucher_history[idx], sizeof(voucher_history_t));
    }
    
    return ESP_OK;
}

esp_err_t config_manager_clear_voucher_history(void)
{
    s_voucher_count = 0;
    s_voucher_next_index = 0;
    memset(s_voucher_history, 0, sizeof(s_voucher_history));
    
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    
    nvs_set_u8(nvs, "vchr_cnt", 0);
    nvs_set_u8(nvs, "vchr_next", 0);
    
    err = nvs_commit(nvs);
    nvs_close(nvs);
    
    return err;
}

esp_err_t config_manager_get_sales_stats(sales_stats_t *stats)
{
    if (stats == NULL) return ESP_ERR_INVALID_ARG;
    
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &nvs);
    if (err != ESP_OK) return err;
    
    nvs_get_u32(nvs, "total_v", &stats->total_vouchers);
    nvs_get_u32(nvs, "total_p", &stats->total_pulses);
    nvs_get_u32(nvs, "daily_v", &stats->daily_vouchers);
    nvs_get_u32(nvs, "daily_p", &stats->daily_pulses);
    nvs_get_u32(nvs, "daily_reset", &stats->last_reset);
    
    nvs_close(nvs);
    return ESP_OK;
}

esp_err_t config_manager_reset_daily_stats(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    
    uint32_t now = esp_timer_get_time() / 1000000;
    
    err = nvs_set_u32(nvs, "daily_v", 0);
    err = nvs_set_u32(nvs, "daily_p", 0);
    err = nvs_set_u32(nvs, "daily_reset", now);
    
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    
    nvs_close(nvs);
    return err;
}

void config_manager_increment_voucher_count(uint32_t pulses)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NS, NVS_READWRITE, &nvs) != ESP_OK) return;
    
    uint32_t total_v = 0, daily_v = 0, total_p = 0, daily_p = 0;
    nvs_get_u32(nvs, "total_v", &total_v);
    nvs_get_u32(nvs, "daily_v", &daily_v);
    nvs_get_u32(nvs, "total_p", &total_p);
    nvs_get_u32(nvs, "daily_p", &daily_p);
    
    nvs_set_u32(nvs, "total_v", total_v + 1);
    nvs_set_u32(nvs, "daily_v", daily_v + 1);
    nvs_set_u32(nvs, "total_p", total_p + pulses);
    nvs_set_u32(nvs, "daily_p", daily_p + pulses);
    
    nvs_commit(nvs);
    nvs_close(nvs);
}

esp_err_t config_manager_factory_reset(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    
    nvs_erase_all(nvs);
    err = nvs_commit(nvs);
    nvs_close(nvs);
    
    s_voucher_count = 0;
    s_voucher_next_index = 0;
    memset(s_voucher_history, 0, sizeof(s_voucher_history));
    
    ESP_LOGI(TAG, "Factory reset complete");
    return err;
}
