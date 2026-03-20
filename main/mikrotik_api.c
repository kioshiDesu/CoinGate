#include "mikrotik_api.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_netif.h"
#include "esp_netif_types.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "mikrotik_api";

#define MIKROTIK_API_DEFAULT_TIMEOUT  5000
#define MIKROTIK_NVS_NAMESPACE      "mikrotik"

typedef struct {
    uint32_t length;
    uint32_t sentence_end;
    uint32_t next_length;
} api_length_info_t;

static mikrotik_config_t s_config = {
    .host = "",
    .port = MIKROTIK_API_PORT,
    .username = "admin",
    .password = "",
    .timeout_ms = MIKROTIK_API_DEFAULT_TIMEOUT,
    .use_tls = false
};

static int s_sockfd = -1;
static volatile mikrotik_state_t s_state = MIKROTIK_DISCONNECTED;
static mikrotik_event_callback_t s_callback = NULL;
static void *s_callback_user_data = NULL;

static const char* s_state_strings[] = {
    "DISCONNECTED", "CONNECTING", "LOGGING_IN", "CONNECTED", "ERROR"
};

static const char* s_error_strings[] = {
    "OK", "CONNECT_ERROR", "LOGIN_ERROR", "SEND_ERROR", 
    "RECV_ERROR", "TIMEOUT", "INVALID", "BUSY"
};

static void update_state(mikrotik_state_t new_state)
{
    if (s_state != new_state) {
        s_state = new_state;
        ESP_LOGD(TAG, "State changed to: %s", s_state_strings[new_state]);
        
        if (s_callback != NULL) {
            s_callback(new_state, s_callback_user_data);
        }
    }
}

static uint32_t read_length(const uint8_t *data)
{
    uint32_t len = 0;
    for (int i = 0; i < 4; i++) {
        len = (len << 7) | (data[i] & 0x7F);
        if ((data[i] & 0x80) == 0) break;
    }
    return len;
}

static uint32_t encode_length(uint8_t *out, uint32_t len)
{
    if (len < 0x80) {
        out[0] = len;
        return 1;
    } else if (len < 0x4000) {
        out[0] = 0x80 | (len >> 7);
        out[1] = len & 0x7F;
        return 2;
    } else if (len < 0x200000) {
        out[0] = 0x80 | (len >> 14);
        out[1] = 0x80 | ((len >> 7) & 0x7F);
        out[2] = len & 0x7F;
        return 3;
    } else {
        out[0] = 0x80 | (len >> 21);
        out[1] = 0x80 | ((len >> 14) & 0x7F);
        out[2] = 0x80 | ((len >> 7) & 0x7F);
        out[3] = len & 0x7F;
        return 4;
    }
}

static esp_err_t send_command(const char *command)
{
    if (s_sockfd < 0) {
        return ESP_FAIL;
    }
    
    uint32_t cmd_len = strlen(command);
    uint8_t len_buf[4];
    uint32_t len_size = encode_length(len_buf, cmd_len);
    
    ssize_t sent = send(s_sockfd, len_buf, len_size, 0);
    if (sent != len_size) {
        ESP_LOGE(TAG, "Failed to send length");
        return ESP_FAIL;
    }
    
    sent = send(s_sockfd, command, cmd_len, 0);
    if (sent != cmd_len) {
        ESP_LOGE(TAG, "Failed to send command");
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

static esp_err_t send_string(const char *str)
{
    uint32_t str_len = strlen(str);
    uint8_t len_buf[4];
    uint32_t len_size = encode_length(len_buf, str_len);
    
    ssize_t sent = send(s_sockfd, len_buf, len_size, 0);
    if (sent != len_size) {
        return ESP_FAIL;
    }
    
    if (str_len > 0) {
        sent = send(s_sockfd, str, str_len, 0);
        if (sent != str_len) {
            return ESP_FAIL;
        }
    }
    
    return ESP_OK;
}

static esp_err_t send_empty(void)
{
    uint8_t zero = 0;
    ssize_t sent = send(s_sockfd, &zero, 1, 0);
    return (sent == 1) ? ESP_OK : ESP_FAIL;
}

static esp_err_t recv_word(char *out, size_t max_len)
{
    if (s_sockfd < 0) {
        return ESP_FAIL;
    }
    
    uint8_t len_buf[4] = {0};
    ssize_t received = recv(s_sockfd, len_buf, 1, 0);
    if (received <= 0) {
        return ESP_FAIL;
    }
    
    uint32_t word_len = len_buf[0];
    if (word_len & 0x80) {
        uint32_t bytes_read = 1;
        uint32_t len = word_len & 0x7F;
        
        while (bytes_read < 4 && (len_buf[bytes_read - 1] & 0x80) == 0) {
            received = recv(s_sockfd, &len_buf[bytes_read], 1, 0);
            if (received <= 0) return ESP_FAIL;
            bytes_read++;
        }
        
        word_len = read_length(len_buf);
    }
    
    if (word_len >= max_len) {
        word_len = max_len - 1;
    }
    
    if (word_len > 0) {
        received = recv(s_sockfd, out, word_len, 0);
        if (received <= 0) {
            return ESP_FAIL;
        }
    }
    
    out[word_len] = '\0';
    return ESP_OK;
}

static bool is_end_of_reply(const char *word)
{
    return word && (strcmp(word, "!done") == 0 || 
                    strncmp(word, "!trap", 5) == 0 ||
                    strncmp(word, "!fatal", 6) == 0);
}

static esp_err_t mikrotik_login(void)
{
    update_state(MIKROTIK_LOGGING_IN);
    
    char response[128];
    esp_err_t err;
    
    err = send_command("/login");
    if (err != ESP_OK) return err;
    
    char reply[64];
    err = recv_word(reply, sizeof(reply));
    if (err != ESP_OK || strcmp(reply, "!done") != 0) {
        ESP_LOGE(TAG, "Login initiation failed");
        return ESP_FAIL;
    }
    
    err = recv_word(response, sizeof(response));
    if (err != ESP_OK || strncmp(response, "=ret=", 5) != 0) {
        ESP_LOGE(TAG, "No challenge received");
        return ESP_FAIL;
    }
    
    char *challenge = response + 5;
    ESP_LOGD(TAG, "Received challenge");
    
    err = send_command("/login");
    if (err != ESP_OK) return err;
    
    char username_reply[64];
    err = recv_word(username_reply, sizeof(username_reply));
    if (err != ESP_OK) return err;
    
    if (strncmp(username_reply, "=ret=", 5) == 0) {
        err = recv_word(reply, sizeof(reply));
    }
    
    if (strcmp(reply, "!done") == 0) {
        ESP_LOGI(TAG, "Login successful");
        update_state(MIKROTIK_CONNECTED);
        return ESP_OK;
    }
    
    ESP_LOGE(TAG, "Login failed");
    update_state(MIKROTIK_ERROR);
    return ESP_FAIL;
}

esp_err_t mikrotik_init(const mikrotik_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memcpy(&s_config, config, sizeof(mikrotik_config_t));
    
    if (s_config.timeout_ms == 0) {
        s_config.timeout_ms = MIKROTIK_API_DEFAULT_TIMEOUT;
    }
    
    if (s_config.host[0] == '\0') {
        ESP_LOGW(TAG, "No host configured");
    }
    
    ESP_LOGI(TAG, "MikroTik API initialized for %s:%d", 
             s_config.host, s_config.port);
    return ESP_OK;
}

esp_err_t mikrotik_deinit(void)
{
    mikrotik_disconnect();
    return ESP_OK;
}

esp_err_t mikrotik_connect(void)
{
    if (s_config.host[0] == '\0') {
        ESP_LOGE(TAG, "No host configured");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (s_sockfd >= 0) {
        close(s_sockfd);
        s_sockfd = -1;
    }
    
    update_state(MIKROTIK_CONNECTING);
    
    struct hostent *he = gethostbyname(s_config.host);
    if (he == NULL) {
        ESP_LOGE(TAG, "DNS lookup failed for %s", s_config.host);
        update_state(MIKROTIK_ERROR);
        return ESP_FAIL;
    }
    
    s_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (s_sockfd < 0) {
        ESP_LOGE(TAG, "Socket creation failed");
        update_state(MIKROTIK_ERROR);
        return ESP_FAIL;
    }
    
    struct timeval timeout;
    timeout.tv_sec = s_config.timeout_ms / 1000;
    timeout.tv_usec = (s_config.timeout_ms % 1000) * 1000;
    setsockopt(s_sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(s_sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(s_config.port);
    memcpy(&server_addr.sin_addr, he->h_addr, he->h_length);
    
    if (connect(s_sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Connection to %s:%d failed", s_config.host, s_config.port);
        close(s_sockfd);
        s_sockfd = -1;
        update_state(MIKROTIK_ERROR);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Connected to %s:%d", s_config.host, s_config.port);
    
    return mikrotik_login();
}

esp_err_t mikrotik_disconnect(void)
{
    if (s_sockfd >= 0) {
        close(s_sockfd);
        s_sockfd = -1;
    }
    
    update_state(MIKROTIK_DISCONNECTED);
    return ESP_OK;
}

bool mikrotik_is_connected(void)
{
    return (s_state == MIKROTIK_CONNECTED);
}

mikrotik_state_t mikrotik_get_state(void)
{
    return s_state;
}

esp_err_t mikrotik_set_callback(mikrotik_event_callback_t callback, void *user_data)
{
    s_callback = callback;
    s_callback_user_data = user_data;
    return ESP_OK;
}

esp_err_t mikrotik_create_voucher(const char *profile, uint32_t duration_seconds,
                                  voucher_t *out_voucher)
{
    if (!mikrotik_is_connected() || profile == NULL || out_voucher == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    static uint32_t voucher_counter = 0;
    uint32_t timestamp = esp_timer_get_time() / 1000000;
    
    snprintf(out_voucher->username, sizeof(out_voucher->username), 
             "user%u%u", (unsigned int)timestamp, (unsigned int)voucher_counter++);
    
    static const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    for (int i = 0; i < 8; i++) {
        out_voucher->password[i] = charset[esp_random() % (sizeof(charset) - 1)];
    }
    out_voucher->password[8] = '\0';
    
    out_voucher->duration_seconds = duration_seconds;
    out_voucher->created_at = timestamp;
    out_voucher->valid = true;
    
    char command[512];
    snprintf(command, sizeof(command), 
             "/ip/hotspot/user/add?name=%s&password=%s&profile=%s&limit-uptime=%us",
             out_voucher->username, out_voucher->password, profile, duration_seconds);
    
    esp_err_t err = send_command(command);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send create user command");
        return err;
    }
    
    char response[64];
    err = recv_word(response, sizeof(response));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to receive response");
        out_voucher->valid = false;
        return err;
    }
    
    if (strcmp(response, "!done") == 0) {
        ESP_LOGI(TAG, "Voucher created: %s / %s", 
                 out_voucher->username, out_voucher->password);
        return ESP_OK;
    } else if (strncmp(response, "!trap", 5) == 0) {
        ESP_LOGE(TAG, "Voucher creation failed: %s", response);
        out_voucher->valid = false;
        return ESP_FAIL;
    }
    
    recv_word(response, sizeof(response));
    
    ESP_LOGI(TAG, "Voucher created: %s / %s", 
             out_voucher->username, out_voucher->password);
    return ESP_OK;
}

esp_err_t mikrotik_get_user_status(const char *username, user_status_t *out_status)
{
    if (!mikrotik_is_connected() || username == NULL || out_status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    char command[256];
    snprintf(command, sizeof(command), 
             "/ip/hotspot/user/print?name=%s", username);
    
    esp_err_t err = send_command(command);
    if (err != ESP_OK) return err;
    
    memset(out_status, 0, sizeof(user_status_t));
    strncpy(out_status->username, username, sizeof(out_status->username) - 1);
    
    char word[256];
    bool found = false;
    
    while (recv_word(word, sizeof(word)) == ESP_OK) {
        if (is_end_of_reply(word)) {
            break;
        }
        
        if (strncmp(word, "=uptime=", 8) == 0) {
            found = true;
            out_status->is_active = true;
        } else if (strncmp(word, "=bytes-in=", 11) == 0) {
            out_status->bytes_in = atoll(word + 11);
        } else if (strncmp(word, "=bytes-out=", 11) == 0) {
            out_status->bytes_out = atoll(word + 11);
        }
    }
    
    if (!found) {
        out_status->is_active = false;
    }
    
    return ESP_OK;
}

esp_err_t mikrotik_disable_user(const char *username)
{
    if (!mikrotik_is_connected() || username == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    char command[256];
    snprintf(command, sizeof(command), 
             "/ip/hotspot/user/disable?name=%s", username);
    
    esp_err_t err = send_command(command);
    if (err != ESP_OK) return err;
    
    char response[64];
    return (recv_word(response, sizeof(response)) == ESP_OK && 
            strcmp(response, "!done") == 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t mikrotik_remove_user(const char *username)
{
    if (!mikrotik_is_connected() || username == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    char command[256];
    snprintf(command, sizeof(command), 
             "/ip/hotspot/user/remove?name=%s", username);
    
    esp_err_t err = send_command(command);
    if (err != ESP_OK) return err;
    
    char response[64];
    return (recv_word(response, sizeof(response)) == ESP_OK && 
            strcmp(response, "!done") == 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t mikrotik_reconnect(void)
{
    mikrotik_disconnect();
    vTaskDelay(pdMS_TO_TICKS(100));
    return mikrotik_connect();
}

esp_err_t mikrotik_update_config(const mikrotik_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    bool was_connected = mikrotik_is_connected();
    
    if (was_connected) {
        mikrotik_disconnect();
    }
    
    memcpy(&s_config, config, sizeof(mikrotik_config_t));
    
    if (was_connected) {
        return mikrotik_connect();
    }
    
    return ESP_OK;
}

esp_err_t mikrotik_save_config_to_nvs(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(MIKROTIK_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }
    
    err = nvs_set_str(nvs_handle, "host", s_config.host);
    err |= nvs_set_u16(nvs_handle, "port", s_config.port);
    err |= nvs_set_str(nvs_handle, "username", s_config.username);
    err |= nvs_set_str(nvs_handle, "password", s_config.password);
    err |= nvs_set_u32(nvs_handle, "timeout", s_config.timeout_ms);
    err |= nvs_set_u8(nvs_handle, "use_tls", s_config.use_tls ? 1 : 0);
    
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
    }
    
    nvs_close(nvs_handle);
    return err;
}

esp_err_t mikrotik_load_config_from_nvs(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(MIKROTIK_NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }
    
    size_t len;
    
    len = sizeof(s_config.host);
    nvs_get_str(nvs_handle, "host", s_config.host, &len);
    
    nvs_get_u16(nvs_handle, "port", &s_config.port);
    
    len = sizeof(s_config.username);
    nvs_get_str(nvs_handle, "username", s_config.username, &len);
    
    len = sizeof(s_config.password);
    nvs_get_str(nvs_handle, "password", s_config.password, &len);
    
    nvs_get_u32(nvs_handle, "timeout", &s_config.timeout_ms);
    
    uint8_t use_tls = 0;
    nvs_get_u8(nvs_handle, "use_tls", &use_tls);
    s_config.use_tls = use_tls != 0;
    
    nvs_close(nvs_handle);
    
    ESP_LOGI(TAG, "Loaded config for %s:%d", s_config.host, s_config.port);
    return ESP_OK;
}

const char* mikrotik_error_to_string(mikrotik_error_t error)
{
    if (error >= 0 && error <= MIKROTIK_ERR_BUSY) {
        return s_error_strings[error];
    }
    return "UNKNOWN";
}

esp_err_t mikrotik_get_active_users(user_status_t **out_users, uint8_t *out_count)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t mikrotik_get_all_users(user_status_t **out_users, uint8_t *out_count)
{
    return ESP_ERR_NOT_SUPPORTED;
}
