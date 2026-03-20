#include "anti_abuse.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "anti_abuse";

static DRAM_ATTR uint32_t s_pulse_timestamps[60];
static DRAM_ATTR uint8_t s_pulse_index = 0;
static DRAM_ATTR uint32_t s_pulses_in_current_minute = 0;
static DRAM_ATTR uint32_t s_last_minute_reset = 0;
static DRAM_ATTR uint32_t s_suspicious_events = 0;
static DRAM_ATTR uint32_t s_total_pulses = 0;
static DRAM_ATTR bool s_cooldown_active = false;
static DRAM_ATTR uint32_t s_cooldown_end_time = 0;
static DRAM_ATTR uint32_t s_cooldown_duration = ANTI_ABUSE_COOLDOWN_SECONDS * 1000;

void anti_abuse_init(void)
{
    memset(s_pulse_timestamps, 0, sizeof(s_pulse_timestamps));
    s_pulse_index = 0;
    s_pulses_in_current_minute = 0;
    s_last_minute_reset = esp_timer_get_time() / 1000;
    s_suspicious_events = 0;
    s_total_pulses = 0;
    s_cooldown_active = false;
    s_cooldown_end_time = 0;
    
    ESP_LOGI(TAG, "Anti-abuse system initialized");
    ESP_LOGI(TAG, "Max pulses/minute: %d, Cooldown: %ds", 
             ANTI_ABUSE_MAX_PULSES_PER_MINUTE, ANTI_ABUSE_COOLDOWN_SECONDS);
}

static bool check_rate_limit(void)
{
    uint32_t current_time = esp_timer_get_time() / 1000;
    
    if (current_time - s_last_minute_reset >= 60000) {
        s_pulses_in_current_minute = 0;
        s_last_minute_reset = current_time;
        for (int i = 0; i < 60; i++) {
            if (s_pulse_timestamps[i] > current_time - 60000) {
                s_pulses_in_current_minute++;
            }
        }
    }
    
    if (s_pulses_in_current_minute >= ANTI_ABUSE_MAX_PULSES_PER_MINUTE) {
        ESP_LOGW(TAG, "Rate limit exceeded: %lu pulses in current minute", 
                 s_pulses_in_current_minute);
        return false;
    }
    
    return true;
}

static bool check_pulse_spacing(uint32_t current_time_ms)
{
    uint32_t min_spacing_ms = 20;
    
    for (int i = 0; i < 60; i++) {
        if (s_pulse_timestamps[i] > 0) {
            uint32_t spacing = current_time_ms - s_pulse_timestamps[i];
            if (spacing < min_spacing_ms && spacing > 0) {
                ESP_LOGD(TAG, "Rapid pulse detected: %lu ms spacing", spacing);
                return false;
            }
        }
    }
    
    return true;
}

static bool check_cooldown(uint32_t current_time_ms)
{
    if (s_cooldown_active) {
        if (current_time_ms >= s_cooldown_end_time) {
            s_cooldown_active = false;
            ESP_LOGI(TAG, "Cooldown period ended");
        } else {
            return false;
        }
    }
    
    return true;
}

bool anti_abuse_check_pulse(uint32_t current_time_ms)
{
    if (!check_rate_limit()) {
        s_suspicious_events++;
        s_cooldown_active = true;
        s_cooldown_end_time = current_time_ms + s_cooldown_duration;
        ESP_LOGW(TAG, "Anti-abuse triggered: rate limit");
        return false;
    }
    
    if (!check_pulse_spacing(current_time_ms)) {
        s_suspicious_events++;
        s_cooldown_active = true;
        s_cooldown_end_time = current_time_ms + s_cooldown_duration;
        ESP_LOGW(TAG, "Anti-abuse triggered: rapid pulses");
        return false;
    }
    
    if (!check_cooldown(current_time_ms)) {
        ESP_LOGD(TAG, "Pulse rejected: cooldown active");
        return false;
    }
    
    s_pulse_timestamps[s_pulse_index] = current_time_ms;
    s_pulse_index = (s_pulse_index + 1) % 60;
    s_pulses_in_current_minute++;
    s_total_pulses++;
    
    return true;
}

bool anti_abuse_is_in_cooldown(void)
{
    return s_cooldown_active;
}

void anti_abuse_reset_minute_count(void)
{
    s_pulses_in_current_minute = 0;
    s_last_minute_reset = esp_timer_get_time() / 1000;
}

void anti_abuse_get_stats(anti_abuse_stats_t *stats)
{
    if (stats == NULL) return;
    
    uint32_t current_time = esp_timer_get_time() / 1000;
    
    stats->pulse_count_1min = 0;
    for (int i = 0; i < 60; i++) {
        if (s_pulse_timestamps[i] > current_time - 60000) {
            stats->pulse_count_1min++;
        }
    }
    
    stats->pulse_count_5min = 0;
    for (int i = 0; i < 60; i++) {
        if (s_pulse_timestamps[i] > current_time - 300000) {
            stats->pulse_count_5min++;
        }
    }
    
    stats->pulse_count_total = s_total_pulses;
    stats->suspicious_events = s_suspicious_events;
    stats->last_pulse_time = s_pulse_index > 0 ? 
        s_pulse_timestamps[s_pulse_index - 1] : 0;
    stats->cooldown_active = s_cooldown_active;
    stats->cooldown_end_time = s_cooldown_end_time;
}

void anti_abuse_reset_all(void)
{
    memset(s_pulse_timestamps, 0, sizeof(s_pulse_timestamps));
    s_pulse_index = 0;
    s_pulses_in_current_minute = 0;
    s_last_minute_reset = esp_timer_get_time() / 1000;
    s_suspicious_events = 0;
    s_total_pulses = 0;
    s_cooldown_active = false;
    s_cooldown_end_time = 0;
    
    ESP_LOGI(TAG, "Anti-abuse stats reset");
}

uint32_t anti_abuse_get_recommended_cooldown(void)
{
    if (s_suspicious_events > ANTI_ABUSE_SUSPICIOUS_THRESHOLD) {
        return 30000;
    } else if (s_suspicious_events > 20) {
        return 15000;
    }
    return ANTI_ABUSE_COOLDOWN_SECONDS * 1000;
}
