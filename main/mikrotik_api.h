#ifndef MIKROTIK_API_H
#define MIKROTIK_API_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MIKROTIK_API_PORT          8728
#define MIKROTIK_API_TLS_PORT      8729
#define MIKROTIK_USERNAME_MAX      32
#define MIKROTIK_PASSWORD_MAX      64
#define MIKROTIK_HOST_MAX          64

typedef enum {
    MIKROTIK_OK = 0,
    MIKROTIK_ERR_CONNECT,
    MIKROTIK_ERR_LOGIN,
    MIKROTIK_ERR_SEND,
    MIKROTIK_ERR_RECV,
    MIKROTIK_ERR_TIMEOUT,
    MIKROTIK_ERR_INVALID,
    MIKROTIK_ERR_BUSY
} mikrotik_error_t;

typedef enum {
    MIKROTIK_DISCONNECTED = 0,
    MIKROTIK_CONNECTING,
    MIKROTIK_LOGGING_IN,
    MIKROTIK_CONNECTED,
    MIKROTIK_ERROR
} mikrotik_state_t;

typedef struct {
    char host[MIKROTIK_HOST_MAX];
    uint16_t port;
    char username[MIKROTIK_USERNAME_MAX];
    char password[MIKROTIK_PASSWORD_MAX];
    uint32_t timeout_ms;
    bool use_tls;
} mikrotik_config_t;

typedef struct {
    char username[33];
    char password[33];
    uint32_t duration_seconds;
    uint32_t created_at;
    bool valid;
} voucher_t;

typedef struct {
    char username[33];
    uint32_t uptime_used;
    uint32_t bytes_in;
    uint32_t bytes_out;
    bool is_active;
} user_status_t;

typedef void (*mikrotik_event_callback_t)(mikrotik_state_t state, void *user_data);

esp_err_t mikrotik_init(const mikrotik_config_t *config);
esp_err_t mikrotik_deinit(void);
esp_err_t mikrotik_connect(void);
esp_err_t mikrotik_disconnect(void);
bool mikrotik_is_connected(void);
mikrotik_state_t mikrotik_get_state(void);
esp_err_t mikrotik_set_callback(mikrotik_event_callback_t callback, void *user_data);

esp_err_t mikrotik_create_voucher(const char *profile, uint32_t duration_seconds, 
                                  voucher_t *out_voucher);
esp_err_t mikrotik_get_user_status(const char *username, user_status_t *out_status);
esp_err_t mikrotik_disable_user(const char *username);
esp_err_t mikrotik_remove_user(const char *username);

esp_err_t mikrotik_get_active_users(user_status_t **out_users, uint8_t *out_count);
esp_err_t mikrotik_get_all_users(user_status_t **out_users, uint8_t *out_count);

esp_err_t mikrotik_reconnect(void);
esp_err_t mikrotik_update_config(const mikrotik_config_t *config);

esp_err_t mikrotik_save_config_to_nvs(void);
esp_err_t mikrotik_load_config_from_nvs(void);

const char* mikrotik_error_to_string(mikrotik_error_t error);

#ifdef __cplusplus
}
#endif

#endif
