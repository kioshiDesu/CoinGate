#include "rate_config.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

static const char *TAG = "rate_config";
static const char *NVS_NAMESPACE = "rate_cfg";

static rate_config_t s_config = {
    .coins_per_pulse = 1,
    .min_coins = 1,
    .max_coins = 100,
    .rate_count = 0
};

static const rate_t s_default_rates[] = {
    {.coins = 5,  .duration_seconds = 3600,  .name = "1 Hour",   .enabled = true},
    {.coins = 10, .duration_seconds = 7200,  .name = "2 Hours",  .enabled = true},
    {.coins = 20, .duration_seconds = 14400, .name = "4 Hours",  .enabled = true},
    {.coins = 50, .duration_seconds = 43200, .name = "12 Hours", .enabled = true},
};

esp_err_t rate_config_init(void)
{
    esp_err_t err = rate_config_load_from_nvs();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load config from NVS, using defaults");
        rate_config_reset_to_default();
    }
    
    ESP_LOGI(TAG, "Rate config initialized with %d rates", s_config.rate_count);
    return ESP_OK;
}

esp_err_t rate_config_load_from_nvs(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }
    
    err = nvs_get_u32(nvs_handle, "cpp", &s_config.coins_per_pulse);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(nvs_handle);
        return err;
    }
    
    err = nvs_get_u32(nvs_handle, "min_coins", &s_config.min_coins);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(nvs_handle);
        return err;
    }
    
    err = nvs_get_u32(nvs_handle, "max_coins", &s_config.max_coins);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(nvs_handle);
        return err;
    }
    
    err = nvs_get_u8(nvs_handle, "rate_count", &s_config.rate_count);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(nvs_handle);
        return err;
    }
    
    if (s_config.rate_count > MAX_RATES) {
        s_config.rate_count = MAX_RATES;
    }
    
    for (int i = 0; i < s_config.rate_count; i++) {
        char key[16];
        snprintf(key, sizeof(key), "rate_%d", i);
        
        size_t size = sizeof(rate_t);
        err = nvs_get_blob(nvs_handle, key, &s_config.rates[i], &size);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to load rate %d", i);
        }
    }
    
    nvs_close(nvs_handle);
    return ESP_OK;
}

esp_err_t rate_config_save_to_nvs(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }
    
    err = nvs_set_u32(nvs_handle, "cpp", s_config.coins_per_pulse);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save cpp: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    err = nvs_set_u32(nvs_handle, "min_coins", s_config.min_coins);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save min_coins: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    err = nvs_set_u32(nvs_handle, "max_coins", s_config.max_coins);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save max_coins: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    err = nvs_set_u8(nvs_handle, "rate_count", s_config.rate_count);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save rate_count: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    for (int i = 0; i < s_config.rate_count; i++) {
        char key[16];
        snprintf(key, sizeof(key), "rate_%d", i);
        
        err = nvs_set_blob(nvs_handle, key, &s_config.rates[i], sizeof(rate_t));
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save rate %d: %s", i, esp_err_to_name(err));
            nvs_close(nvs_handle);
            return err;
        }
    }
    
    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Rate config saved to NVS");
    }
    
    return err;
}

esp_err_t rate_config_add_rate(const rate_t *rate)
{
    if (rate == NULL || s_config.rate_count >= MAX_RATES) {
        return ESP_ERR_INVALID_SIZE;
    }
    
    memcpy(&s_config.rates[s_config.rate_count], rate, sizeof(rate_t));
    s_config.rate_count++;
    
    return rate_config_save_to_nvs();
}

esp_err_t rate_config_remove_rate(uint8_t index)
{
    if (index >= s_config.rate_count) {
        return ESP_ERR_INVALID_ARG;
    }
    
    for (int i = index; i < s_config.rate_count - 1; i++) {
        memcpy(&s_config.rates[i], &s_config.rates[i + 1], sizeof(rate_t));
    }
    
    s_config.rate_count--;
    memset(&s_config.rates[s_config.rate_count], 0, sizeof(rate_t));
    
    return rate_config_save_to_nvs();
}

esp_err_t rate_config_update_rate(uint8_t index, const rate_t *rate)
{
    if (index >= s_config.rate_count || rate == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memcpy(&s_config.rates[index], rate, sizeof(rate_t));
    
    return rate_config_save_to_nvs();
}

const rate_t* rate_config_get_rate_by_index(uint8_t index)
{
    if (index >= s_config.rate_count) {
        return NULL;
    }
    return &s_config.rates[index];
}

const rate_t* rate_config_get_best_match(uint32_t coin_count)
{
    if (s_config.rate_count == 0) {
        return NULL;
    }
    
    const rate_t *best_match = NULL;
    int32_t best_diff = INT32_MAX;
    
    for (int i = 0; i < s_config.rate_count; i++) {
        if (!s_config.rates[i].enabled) continue;
        
        int32_t diff = (int32_t)coin_count - (int32_t)s_config.rates[i].coins;
        
        if (diff >= 0 && diff < best_diff) {
            best_diff = diff;
            best_match = &s_config.rates[i];
        }
    }
    
    return best_match;
}

uint8_t rate_config_get_count(void)
{
    return s_config.rate_count;
}

uint32_t rate_config_coins_to_time(uint32_t coins)
{
    if (s_config.coins_per_pulse == 0) {
        return 0;
    }
    
    uint32_t actual_coins = coins * s_config.coins_per_pulse;
    
    const rate_t *rate = rate_config_get_best_match(actual_coins);
    if (rate != NULL) {
        return rate->duration_seconds;
    }
    
    uint32_t base_seconds = 600;
    return (actual_coins * base_seconds) / 5;
}

uint32_t rate_config_time_to_coins(uint32_t seconds)
{
    if (s_config.rate_count == 0) {
        return (seconds * 5) / 600;
    }
    
    for (int i = 0; i < s_config.rate_count; i++) {
        if (s_config.rates[i].enabled && 
            s_config.rates[i].duration_seconds == seconds) {
            return s_config.rates[i].coins;
        }
    }
    
    return ((seconds * 5) / 600) + 1;
}

esp_err_t rate_config_set_coins_per_pulse(uint32_t cpp)
{
    if (cpp == 0) {
        cpp = 1;
    }
    
    s_config.coins_per_pulse = cpp;
    return rate_config_save_to_nvs();
}

uint32_t rate_config_get_coins_per_pulse(void)
{
    return s_config.coins_per_pulse;
}

esp_err_t rate_config_set_min_max_coins(uint32_t min, uint32_t max)
{
    if (min > max) {
        return ESP_ERR_INVALID_ARG;
    }
    
    s_config.min_coins = min;
    s_config.max_coins = max;
    
    return rate_config_save_to_nvs();
}

void rate_config_get_min_max_coins(uint32_t *min, uint32_t *max)
{
    if (min) *min = s_config.min_coins;
    if (max) *max = s_config.max_coins;
}

esp_err_t rate_config_reset_to_default(void)
{
    s_config.coins_per_pulse = 1;
    s_config.min_coins = 1;
    s_config.max_coins = 100;
    s_config.rate_count = sizeof(s_default_rates) / sizeof(rate_t);
    
    if (s_config.rate_count > MAX_RATES) {
        s_config.rate_count = MAX_RATES;
    }
    
    for (int i = 0; i < s_config.rate_count; i++) {
        memcpy(&s_config.rates[i], &s_default_rates[i], sizeof(rate_t));
    }
    
    ESP_LOGI(TAG, "Rate config reset to defaults");
    return rate_config_save_to_nvs();
}
