#ifndef ANTI_ABUSE_H
#define ANTI_ABUSE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ANTI_ABUSE_MAX_PULSES_PER_MINUTE    30
#define ANTI_ABUSE_COOLDOWN_SECONDS         5
#define ANTI_ABUSE_SUSPICIOUS_THRESHOLD      50

typedef struct {
    uint32_t pulse_count_1min;
    uint32_t pulse_count_5min;
    uint32_t pulse_count_total;
    uint32_t suspicious_events;
    uint32_t last_pulse_time;
    bool cooldown_active;
    uint32_t cooldown_end_time;
} anti_abuse_stats_t;

void anti_abuse_init(void);
bool anti_abuse_check_pulse(uint32_t current_time_ms);
bool anti_abuse_is_in_cooldown(void);
void anti_abuse_reset_minute_count(void);
void anti_abuse_get_stats(anti_abuse_stats_t *stats);
void anti_abuse_reset_all(void);
uint32_t anti_abuse_get_recommended_cooldown(void);

#ifdef __cplusplus
}
#endif

#endif
