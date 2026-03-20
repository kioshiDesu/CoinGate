#ifndef RATE_CONFIG_H
#define RATE_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_RATES                10
#define RATE_NAME_MAX_LEN        32

typedef struct {
    uint8_t coins;
    uint32_t duration_seconds;
    char name[RATE_NAME_MAX_LEN];
    bool enabled;
} rate_t;

typedef struct {
    uint32_t coins_per_pulse;
    uint32_t min_coins;
    uint32_t max_coins;
    rate_t rates[MAX_RATES];
    uint8_t rate_count;
} rate_config_t;

esp_err_t rate_config_init(void);
esp_err_t rate_config_load_from_nvs(void);
esp_err_t rate_config_save_to_nvs(void);
esp_err_t rate_config_add_rate(const rate_t *rate);
esp_err_t rate_config_remove_rate(uint8_t index);
esp_err_t rate_config_update_rate(uint8_t index, const rate_t *rate);
const rate_t* rate_config_get_rate_by_index(uint8_t index);
const rate_t* rate_config_get_best_match(uint32_t coin_count);
uint8_t rate_config_get_count(void);
uint32_t rate_config_coins_to_time(uint32_t coins);
uint32_t rate_config_time_to_coins(uint32_t seconds);
esp_err_t rate_config_set_coins_per_pulse(uint32_t cpp);
uint32_t rate_config_get_coins_per_pulse(void);
esp_err_t rate_config_set_min_max_coins(uint32_t min, uint32_t max);
void rate_config_get_min_max_coins(uint32_t *min, uint32_t *max);
esp_err_t rate_config_reset_to_default(void);

#ifdef __cplusplus
}
#endif

#endif
