#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_VOUCHER_HISTORY    50

typedef struct {
    char username[33];
    char password[33];
    uint32_t duration_seconds;
    uint32_t created_at;
    uint32_t pulses_used;
} voucher_history_t;

typedef struct {
    uint32_t total_vouchers;
    uint32_t total_pulses;
    uint32_t daily_vouchers;
    uint32_t daily_pulses;
    uint32_t last_reset;
} sales_stats_t;

typedef struct {
    char admin_password[65];
    bool setup_complete;
    uint8_t sys_version_major;
    uint8_t sys_version_minor;
} system_config_t;

esp_err_t config_manager_init(void);

esp_err_t config_manager_get_wifi_ssid(char *ssid, size_t max_len);
esp_err_t config_manager_set_wifi_ssid(const char *ssid);
esp_err_t config_manager_get_wifi_password(char *password, size_t max_len);
esp_err_t config_manager_set_wifi_password(const char *password);

esp_err_t config_manager_get_mikrotik_config(char *host, uint16_t *port, char *username, char *password, size_t max_len);
esp_err_t config_manager_set_mikrotik_config(const char *host, uint16_t port, const char *username, const char *password);

esp_err_t config_manager_get_admin_password(char *password, size_t max_len);
esp_err_t config_manager_set_admin_password(const char *password);
bool config_manager_check_admin_password(const char *password);
bool config_manager_is_setup_complete(void);
esp_err_t config_manager_set_setup_complete(bool complete);

esp_err_t config_manager_add_voucher_to_history(const voucher_history_t *voucher);
esp_err_t config_manager_get_voucher_history(voucher_history_t *history, uint8_t *count);
esp_err_t config_manager_clear_voucher_history(void);

esp_err_t config_manager_get_sales_stats(sales_stats_t *stats);
esp_err_t config_manager_reset_daily_stats(void);
void config_manager_increment_voucher_count(uint32_t pulses);

esp_err_t config_manager_factory_reset(void);

#ifdef __cplusplus
}
#endif

#endif
