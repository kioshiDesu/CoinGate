#include "web_server.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_vfs.h"
#include "esp_spiffs.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "wifi_manager.h"
#include "coin_acceptor.h"
#include "rate_config.h"
#include "anti_abuse.h"
#include "mikrotik_api.h"
#include "config_manager.h"

static const char *TAG = "web_server";
static httpd_handle_t s_server = NULL;
#define FILE_PATH_MAX 256

static const char *s_state_strings[] = {
    "DISCONNECTED", "CONNECTING", "LOGGING_IN", "CONNECTED", "ERROR"
};

static esp_err_t serve_file(httpd_req_t *req, const char *file_path)
{
    FILE *file = fopen(file_path, "r");
    if (file == NULL) {
        ESP_LOGW(TAG, "File not found: %s", file_path);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        return ESP_FAIL;
    }

    char buf[256];
    size_t total_sent = 0;

    while (!feof(file) && !ferror(file)) {
        size_t read = fread(buf, 1, sizeof(buf), file);
        if (read == 0) break;
        
        ssize_t sent = httpd_resp_send_chunk(req, buf, read);
        if (sent < 0) {
            fclose(file);
            return ESP_FAIL;
        }
        total_sent += read;
    }

    fclose(file);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t get_file_handler(httpd_req_t *req)
{
    char filepath[FILE_PATH_MAX];
    char full_uri[FILE_PATH_MAX];

    strncpy(full_uri, req->uri, sizeof(full_uri) - 1);

    // Skip API routes - they should be handled by specific handlers
    if (strncmp(full_uri, "/api", 4) == 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "API endpoint not found");
        return ESP_FAIL;
    }

    // For root or setup, serve index.html
    if (strcmp(full_uri, "/") == 0 || strcmp(full_uri, "/setup") == 0) {
        strcpy(filepath, "/spiffs/index.html");
    } else {
        // Build full path and try to serve the file
        snprintf(filepath, sizeof(filepath), "/spiffs%s", full_uri);
        
        struct stat st;
        if (stat(filepath, &st) == -1) {
            // File not found - serve index.html (SPA fallback)
            ESP_LOGW(TAG, "File not found: %s, serving index.html", filepath);
            strcpy(filepath, "/spiffs/index.html");
        }
    }

    // Set content type based on file
    const char *ext = strrchr(filepath, '.');
    if (ext) {
        if (strcmp(ext, ".html") == 0) {
            httpd_resp_set_type(req, "text/html");
        } else if (strcmp(ext, ".css") == 0) {
            httpd_resp_set_type(req, "text/css");
        } else if (strcmp(ext, ".js") == 0) {
            httpd_resp_set_type(req, "application/javascript");
        } else if (strcmp(ext, ".png") == 0) {
            httpd_resp_set_type(req, "image/png");
        } else if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) {
            httpd_resp_set_type(req, "image/jpeg");
        } else if (strcmp(ext, ".ico") == 0) {
            httpd_resp_set_type(req, "image/x-icon");
        } else {
            httpd_resp_set_type(req, "text/plain");
        }
    }

    return serve_file(req, filepath);
}

static esp_err_t api_status_handler(httpd_req_t *req)
{
    char response[1024];
    uint32_t min_coins, max_coins;
    rate_config_get_min_max_coins(&min_coins, &max_coins);
    
    anti_abuse_stats_t abuse_stats;
    anti_abuse_get_stats(&abuse_stats);
    
    sales_stats_t sales;
    config_manager_get_sales_stats(&sales);
    
    uint32_t uptime = esp_timer_get_time() / 1000000;

    int len = snprintf(response, sizeof(response),
        "{"
        "\"fw\":\"1.0.0\","
        "\"uptime\":%u,"
        "\"heap\":%u,"
        "\"wifi\":%s,"
        "\"pulses_session\":%u,"
        "\"pulses_total\":%u,"
        "\"coin_en\":%s,"
        "\"rates\":%d,"
        "\"cpp\":%u,"
        "\"min\":%u,"
        "\"max\":%u,"
        "\"rate_1m\":%u,"
        "\"suspicious\":%u,"
        "\"cooldown\":%s,"
        "\"mt_conn\":%s,"
        "\"mt_state\":\"%s\","
        "\"daily_vouchers\":%u,"
        "\"daily_pulses\":%u,"
        "\"total_vouchers\":%u,"
        "\"total_pulses\":%u,"
        "\"setup_done\":%s"
        "}",
        uptime,
        esp_get_free_heap_size(),
        wifi_manager_is_connected() ? "true" : "false",
        coin_acceptor_get_session_pulses(),
        coin_acceptor_get_total_pulses(),
        coin_acceptor_is_enabled() ? "true" : "false",
        rate_config_get_count(),
        rate_config_get_coins_per_pulse(),
        min_coins, max_coins,
        abuse_stats.pulse_count_1min,
        abuse_stats.suspicious_events,
        anti_abuse_is_in_cooldown() ? "true" : "false",
        mikrotik_is_connected() ? "true" : "false",
        s_state_strings[mikrotik_get_state()],
        sales.daily_vouchers,
        sales.daily_pulses,
        sales.total_vouchers,
        sales.total_pulses,
        config_manager_is_setup_complete() ? "true" : "false"
    );

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, len);
    return ESP_OK;
}

static esp_err_t api_rates_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        uint8_t count = rate_config_get_count();
        char response[2048];
        int offset = 0;
        
        offset += snprintf(response + offset, sizeof(response) - offset, "[");
        
        for (int i = 0; i < count; i++) {
            const rate_t *rate = rate_config_get_rate_by_index(i);
            if (rate) {
                if (i > 0) offset += snprintf(response + offset, sizeof(response) - offset, ",");
                offset += snprintf(response + offset, sizeof(response) - offset,
                    "{\"id\":%d,\"coins\":%d,\"duration\":%u,\"name\":\"%s\",\"enabled\":%s}",
                    i, rate->coins, rate->duration_seconds, rate->name,
                    rate->enabled ? "true" : "false");
            }
        }
        
        offset += snprintf(response + offset, sizeof(response) - offset, "]");
        
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, response, offset);
    } else if (req->method == HTTP_POST) {
        char buf[512];
        int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
        if (ret <= 0) return ESP_FAIL;
        buf[ret] = '\0';
        
        rate_t new_rate = {0};
        new_rate.enabled = true;
        
        char *coins = strstr(buf, "\"coins\":");
        char *duration = strstr(buf, "\"duration\":");
        char *name = strstr(buf, "\"name\":\"");
        
        if (coins) new_rate.coins = atoi(coins + 8);
        if (duration) new_rate.duration_seconds = atoi(duration + 10);
        if (name) {
            char *end = strchr(name + 8, '"');
            if (end) {
                size_t len = end - name - 8;
                if (len > RATE_NAME_MAX_LEN - 1) len = RATE_NAME_MAX_LEN - 1;
                strncpy(new_rate.name, name + 8, len);
                new_rate.name[len] = '\0';
            }
        }
        
        esp_err_t err = rate_config_add_rate(&new_rate);
        
        const char *resp = err == ESP_OK ? 
            "{\"success\":true}" : 
            "{\"success\":false,\"error\":\"Failed to add rate\"}";
        
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, resp, strlen(resp));
    }
    return ESP_OK;
}

static esp_err_t api_voucher_handler(httpd_req_t *req)
{
    if (req->method == HTTP_POST) {
        char buf[512];
        int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
        if (ret <= 0) return ESP_FAIL;
        buf[ret] = '\0';
        
        uint32_t coins = coin_acceptor_get_session_pulses();
        
        char *coins_override = strstr(buf, "\"coins\":");
        if (coins_override) {
            coins = atoi(coins_override + 8);
        }
        
        if (coins < rate_config_get_coins_per_pulse()) {
            const char *err_resp = "{\"success\":false,\"error\":\"Insufficient coins\"}";
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req, err_resp, strlen(err_resp));
            return ESP_OK;
        }
        
        uint32_t duration = rate_config_coins_to_time(coins);
        voucher_t voucher = {0};
        
        esp_err_t err = mikrotik_create_voucher("default", duration, &voucher);
        
        char response[512];
        if (err == ESP_OK && voucher.valid) {
            config_manager_increment_voucher_count(coins);
            
            voucher_history_t hist = {0};
            strncpy(hist.username, voucher.username, sizeof(hist.username) - 1);
            strncpy(hist.password, voucher.password, sizeof(hist.password) - 1);
            hist.duration_seconds = duration;
            hist.created_at = esp_timer_get_time() / 1000000;
            hist.pulses_used = coins;
            config_manager_add_voucher_to_history(&hist);
            
            int len = snprintf(response, sizeof(response),
                "{\"success\":true,\"username\":\"%s\",\"password\":\"%s\","
                "\"duration\":%u,\"coins\":%u}",
                voucher.username, voucher.password, duration, coins);
            
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req, response, len);
            
            coin_acceptor_reset_session();
        } else {
            const char *err_resp = "{\"success\":false,\"error\":\"Failed to create voucher\"}";
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req, err_resp, strlen(err_resp));
        }
    }
    return ESP_OK;
}

static esp_err_t api_coin_handler(httpd_req_t *req)
{
    char response[256];
    uint32_t pulses = coin_acceptor_get_session_pulses();
    uint32_t duration = rate_config_coins_to_time(pulses);
    uint32_t cpp = rate_config_get_coins_per_pulse();
    
    const rate_t *best_rate = rate_config_get_best_match(pulses);
    const char *match_name = best_rate ? best_rate->name : "Custom";
    
    int len = snprintf(response, sizeof(response),
        "{\"pulses\":%u,\"duration\":%u,\"cpp\":%u,"
        "\"coins_value\":%u,\"best_rate\":\"%s\",\"coin_enabled\":%s}",
        pulses, duration, cpp, pulses / cpp + (pulses % cpp ? 1 : 0),
        match_name,
        coin_acceptor_is_enabled() ? "true" : "false");
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, len);
    return ESP_OK;
}

static esp_err_t api_config_handler(httpd_req_t *req)
{
    char response[1024];
    uint32_t min_coins, max_coins;
    rate_config_get_min_max_coins(&min_coins, &max_coins);
    
    char ssid[64] = "";
    char mt_host[64] = "";
    uint16_t mt_port = 8728;
    char mt_user[32] = "";
    
    config_manager_get_wifi_ssid(ssid, sizeof(ssid));
    config_manager_get_mikrotik_config(mt_host, &mt_port, mt_user, NULL, sizeof(mt_user));
    
    int len = snprintf(response, sizeof(response),
        "{"
        "\"cpp\":%u,"
        "\"min\":%u,"
        "\"max\":%u,"
        "\"wifi_ssid\":\"%s\","
        "\"wifi_configured\":%s,"
        "\"mt_host\":\"%s\","
        "\"mt_port\":%d,"
        "\"mt_user\":\"%s\","
        "\"mt_configured\":%s,"
        "\"coin_gpio\":%d,"
        "\"setup_done\":%s"
        "}",
        rate_config_get_coins_per_pulse(),
        min_coins, max_coins,
        ssid,
        strlen(ssid) > 0 ? "true" : "false",
        mt_host,
        mt_port,
        mt_user,
        strlen(mt_host) > 0 ? "true" : "false",
        COIN_ACCEPTOR_GPIO_PIN,
        config_manager_is_setup_complete() ? "true" : "false"
    );
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, len);
    return ESP_OK;
}

static esp_err_t api_wifi_handler(httpd_req_t *req)
{
    if (req->method == HTTP_POST) {
        char buf[512];
        int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
        if (ret <= 0) return ESP_FAIL;
        buf[ret] = '\0';
        
        char ssid[64] = "", password[128] = "";
        
        char *ssid_val = strstr(buf, "ssid=");
        char *pass_val = strstr(buf, "password=");
        
        if (ssid_val) {
            ssid_val += 5;
            char *end = strchr(ssid_val, '&');
            if (end) {
                size_t len = end - ssid_val;
                strncpy(ssid, ssid_val, len < sizeof(ssid) - 1 ? len : sizeof(ssid) - 1);
            } else {
                strncpy(ssid, ssid_val, sizeof(ssid) - 1);
            }
        }
        
        if (pass_val) {
            pass_val += 9;
            char *end = strchr(pass_val, '&');
            if (end) {
                size_t len = end - pass_val;
                strncpy(password, pass_val, len < sizeof(password) - 1 ? len : sizeof(password) - 1);
            } else {
                strncpy(password, pass_val, sizeof(password) - 1);
            }
        }
        
        config_manager_set_wifi_ssid(ssid);
        config_manager_set_wifi_password(password);
        
        const char *resp = "{\"success\":true,\"message\":\"WiFi saved. Rebooting...\"}";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, resp, strlen(resp));
        
        wifi_manager_reconnect();
    }
    return ESP_OK;
}

static esp_err_t api_mikrotik_handler(httpd_req_t *req)
{
    if (req->method == HTTP_POST) {
        char buf[512];
        int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
        if (ret <= 0) return ESP_FAIL;
        buf[ret] = '\0';
        
        char host[64] = "", user[32] = "", pass[64] = "";
        uint16_t port = 8728;
        
        char *host_val = strstr(buf, "\"host\":");
        char *port_val = strstr(buf, "\"port\":");
        char *user_val = strstr(buf, "\"username\":");
        char *pass_val = strstr(buf, "\"password\":");
        
        if (host_val) {
            char *start = strchr(host_val, '"');
            char *end = start ? strchr(start + 1, '"') : NULL;
            if (start && end) {
                size_t len = end - start - 1;
                strncpy(host, start + 1, len < sizeof(host) - 1 ? len : sizeof(host) - 1);
            }
        }
        
        if (port_val) {
            port = atoi(port_val + 7);
            char *comma = strchr(port_val, ',');
            if (comma) {
                port = atoi(port_val + 7);
            }
        }
        
        if (user_val) {
            char *start = strchr(user_val, '"');
            char *end = start ? strchr(start + 1, '"') : NULL;
            if (start && end) {
                size_t len = end - start - 1;
                strncpy(user, start + 1, len < sizeof(user) - 1 ? len : sizeof(user) - 1);
            }
        }
        
        if (pass_val) {
            char *start = strchr(pass_val, '"');
            char *end = start ? strchr(start + 1, '"') : NULL;
            if (start && end) {
                size_t len = end - start - 1;
                strncpy(pass, start + 1, len < sizeof(pass) - 1 ? len : sizeof(pass) - 1);
            }
        }
        
        esp_err_t err = config_manager_set_mikrotik_config(host, port, user, pass);
        
        if (err == ESP_OK) {
            mikrotik_config_t mt_cfg = {
                .host = "",
                .port = port,
                .username = "",
                .password = "",
                .timeout_ms = 5000,
                .use_tls = false
            };
            strncpy(mt_cfg.host, host, sizeof(mt_cfg.host) - 1);
            strncpy(mt_cfg.username, user, sizeof(mt_cfg.username) - 1);
            strncpy(mt_cfg.password, pass, sizeof(mt_cfg.password) - 1);
            
            mikrotik_update_config(&mt_cfg);
            mikrotik_reconnect();
        }
        
        const char *resp = err == ESP_OK ?
            "{\"success\":true,\"message\":\"MikroTik settings saved\"}" :
            "{\"success\":false,\"error\":\"Failed to save\"}";
        
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, resp, strlen(resp));
    }
    return ESP_OK;
}

static esp_err_t api_mikrotik_test_handler(httpd_req_t *req)
{
    if (req->method == HTTP_POST) {
        char response[256];
        
        if (!mikrotik_is_connected()) {
            int len = snprintf(response, sizeof(response),
                "{\"success\":false,\"error\":\"Not connected\"}");
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req, response, len);
        } else {
            int len = snprintf(response, sizeof(response),
                "{\"success\":true,\"message\":\"Connected to MikroTik\"}");
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req, response, len);
        }
    }
    return ESP_OK;
}

static esp_err_t api_history_handler(httpd_req_t *req)
{
    voucher_history_t history[MAX_VOUCHER_HISTORY];
    uint8_t count = 0;
    
    config_manager_get_voucher_history(history, &count);
    
    char response[8192];
    int offset = 0;
    
    offset += snprintf(response + offset, sizeof(response) - offset, "[");
    
    for (int i = 0; i < count; i++) {
        if (i > 0) offset += snprintf(response + offset, sizeof(response) - offset, ",");
        
        offset += snprintf(response + offset, sizeof(response) - offset,
            "{\"username\":\"%s\",\"password\":\"%s\",\"duration\":%u,"
            "\"created_at\":%u,\"pulses\":%u}",
            history[i].username, history[i].password,
            history[i].duration_seconds, history[i].created_at,
            history[i].pulses_used);
    }
    
    offset += snprintf(response + offset, sizeof(response) - offset, "]");
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, offset);
    return ESP_OK;
}

static esp_err_t api_reset_handler(httpd_req_t *req)
{
    coin_acceptor_reset_session();
    anti_abuse_reset_all();
    
    const char *resp = "{\"success\":true,\"message\":\"Session reset\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
}

static esp_err_t api_password_handler(httpd_req_t *req)
{
    if (req->method == HTTP_POST) {
        char buf[256];
        int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
        if (ret <= 0) return ESP_FAIL;
        buf[ret] = '\0';
        
        char current_pass[65] = "", new_pass[65] = "";
        
        char *cur = strstr(buf, "\"current\":");
        char *newp = strstr(buf, "\"new\":");
        
        if (cur) {
            char *start = strchr(cur, '"');
            char *end = start ? strchr(start + 1, '"') : NULL;
            if (start && end) {
                size_t len = end - start - 1;
                strncpy(current_pass, start + 1, len < sizeof(current_pass) - 1 ? len : sizeof(current_pass) - 1);
            }
        }
        
        if (newp) {
            char *start = strchr(newp, '"');
            char *end = start ? strchr(start + 1, '"') : NULL;
            if (start && end) {
                size_t len = end - start - 1;
                strncpy(new_pass, start + 1, len < sizeof(new_pass) - 1 ? len : sizeof(new_pass) - 1);
            }
        }
        
        char response[256];
        
        if (!config_manager_check_admin_password(current_pass)) {
            int len = snprintf(response, sizeof(response),
                "{\"success\":false,\"error\":\"Current password incorrect\"}");
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req, response, len);
            return ESP_OK;
        }
        
        if (strlen(new_pass) < 4) {
            int len = snprintf(response, sizeof(response),
                "{\"success\":false,\"error\":\"Password too short\"}");
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req, response, len);
            return ESP_OK;
        }
        
        config_manager_set_admin_password(new_pass);
        
        int len = snprintf(response, sizeof(response),
            "{\"success\":true,\"message\":\"Password changed\"}");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, response, len);
    }
    return ESP_OK;
}

static esp_err_t api_setup_handler(httpd_req_t *req)
{
    if (req->method == HTTP_POST) {
        char buf[1024];
        int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
        if (ret <= 0) return ESP_FAIL;
        buf[ret] = '\0';
        
        config_manager_set_setup_complete(true);
        
        const char *resp = "{\"success\":true,\"message\":\"Setup complete\"}";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, resp, strlen(resp));
    }
    return ESP_OK;
}

static esp_err_t api_reboot_handler(httpd_req_t *req)
{
    const char *resp = "{\"success\":true,\"message\":\"Rebooting...\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, strlen(resp));
    
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

static esp_err_t api_factory_reset_handler(httpd_req_t *req)
{
    config_manager_factory_reset();
    rate_config_reset_to_default();
    
    const char *resp = "{\"success\":true,\"message\":\"Factory reset complete. Rebooting...\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, strlen(resp));
    
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

static esp_err_t api_coin_config_handler(httpd_req_t *req)
{
    if (req->method == HTTP_POST) {
        char buf[256];
        int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
        if (ret <= 0) return ESP_FAIL;
        buf[ret] = '\0';
        
        uint32_t cpp = 1, min = 1, max = 100;
        
        char *cpp_val = strstr(buf, "\"cpp\":");
        char *min_val = strstr(buf, "\"min\":");
        char *max_val = strstr(buf, "\"max\":");
        
        if (cpp_val) cpp = atoi(cpp_val + 6);
        if (min_val) min = atoi(min_val + 6);
        if (max_val) max = atoi(max_val + 6);
        
        rate_config_set_coins_per_pulse(cpp);
        rate_config_set_min_max_coins(min, max);
        
        const char *resp = "{\"success\":true}";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, resp, strlen(resp));
    }
    return ESP_OK;
}

static esp_err_t api_sales_handler(httpd_req_t *req)
{
    sales_stats_t stats;
    config_manager_get_sales_stats(&stats);
    
    char response[512];
    int len = snprintf(response, sizeof(response),
        "{"
        "\"total_vouchers\":%u,"
        "\"total_pulses\":%u,"
        "\"daily_vouchers\":%u,"
        "\"daily_pulses\":%u,"
        "\"last_reset\":%u"
        "}",
        stats.total_vouchers,
        stats.total_pulses,
        stats.daily_vouchers,
        stats.daily_pulses,
        stats.last_reset
    );
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, len);
    return ESP_OK;
}

static esp_err_t api_sales_reset_handler(httpd_req_t *req)
{
    config_manager_reset_daily_stats();
    
    const char *resp = "{\"success\":true}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
}

static const httpd_uri_t get_any = {
    .uri = "/*", .method = HTTP_GET, .handler = get_file_handler
};

static const httpd_uri_t api_status = {
    .uri = "/api/status", .method = HTTP_GET, .handler = api_status_handler
};

static const httpd_uri_t api_rates = {
    .uri = "/api/rates", .method = HTTP_GET | HTTP_POST, .handler = api_rates_handler
};

static const httpd_uri_t api_voucher = {
    .uri = "/api/voucher", .method = HTTP_POST, .handler = api_voucher_handler
};

static const httpd_uri_t api_coin = {
    .uri = "/api/coin", .method = HTTP_GET, .handler = api_coin_handler
};

static const httpd_uri_t api_config = {
    .uri = "/api/config", .method = HTTP_GET, .handler = api_config_handler
};

static const httpd_uri_t api_wifi = {
    .uri = "/api/wifi", .method = HTTP_POST, .handler = api_wifi_handler
};

static const httpd_uri_t api_mikrotik = {
    .uri = "/api/mikrotik", .method = HTTP_POST, .handler = api_mikrotik_handler
};

static const httpd_uri_t api_mikrotik_test = {
    .uri = "/api/mikrotik/test", .method = HTTP_POST, .handler = api_mikrotik_test_handler
};

static const httpd_uri_t api_history = {
    .uri = "/api/history", .method = HTTP_GET, .handler = api_history_handler
};

static const httpd_uri_t api_reset = {
    .uri = "/api/reset", .method = HTTP_POST, .handler = api_reset_handler
};

static const httpd_uri_t api_password = {
    .uri = "/api/password", .method = HTTP_POST, .handler = api_password_handler
};

static const httpd_uri_t api_setup = {
    .uri = "/api/setup", .method = HTTP_POST, .handler = api_setup_handler
};

static const httpd_uri_t api_reboot = {
    .uri = "/api/reboot", .method = HTTP_POST, .handler = api_reboot_handler
};

static const httpd_uri_t api_factory_reset = {
    .uri = "/api/factory_reset", .method = HTTP_POST, .handler = api_factory_reset_handler
};

static const httpd_uri_t api_coin_config = {
    .uri = "/api/coin/config", .method = HTTP_POST, .handler = api_coin_config_handler
};

static const httpd_uri_t api_sales = {
    .uri = "/api/sales", .method = HTTP_GET, .handler = api_sales_handler
};

static const httpd_uri_t api_sales_reset = {
    .uri = "/api/sales/reset", .method = HTTP_POST, .handler = api_sales_reset_handler
};

httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 4096;

    if (httpd_start(&s_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start server");
        return NULL;
    }

    // Register API handlers FIRST (must be before catch-all)
    httpd_register_uri_handler(s_server, &api_status);
    httpd_register_uri_handler(s_server, &api_rates);
    httpd_register_uri_handler(s_server, &api_voucher);
    httpd_register_uri_handler(s_server, &api_coin);
    httpd_register_uri_handler(s_server, &api_config);
    httpd_register_uri_handler(s_server, &api_wifi);
    httpd_register_uri_handler(s_server, &api_mikrotik);
    httpd_register_uri_handler(s_server, &api_mikrotik_test);
    httpd_register_uri_handler(s_server, &api_history);
    httpd_register_uri_handler(s_server, &api_reset);
    httpd_register_uri_handler(s_server, &api_password);
    httpd_register_uri_handler(s_server, &api_setup);
    httpd_register_uri_handler(s_server, &api_reboot);
    httpd_register_uri_handler(s_server, &api_factory_reset);
    httpd_register_uri_handler(s_server, &api_coin_config);
    httpd_register_uri_handler(s_server, &api_sales);
    httpd_register_uri_handler(s_server, &api_sales_reset);

    // Register catch-all handler LAST (serves all static files and SPA fallback)
    httpd_register_uri_handler(s_server, &get_any);

    ESP_LOGI(TAG, "Web server started");
    return s_server;
}

void stop_webserver(httpd_handle_t server)
{
    if (server) {
        httpd_stop(server);
    }
}

esp_err_t web_server_start(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 10,
        .format_if_mount_failed = true
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "SPIFFS mounted successfully");

    // Check if index.html exists
    FILE *f = fopen("/spiffs/index.html", "r");
    if (f) {
        ESP_LOGI(TAG, "index.html found in SPIFFS");
        fclose(f);
    } else {
        ESP_LOGW(TAG, "index.html NOT found in SPIFFS!");
    }

    start_webserver();
    ESP_LOGI(TAG, "Web server running. Access at http://192.168.4.1");
    return ESP_OK;
}

void web_server_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
    esp_vfs_spiffs_unregister(NULL);
}
