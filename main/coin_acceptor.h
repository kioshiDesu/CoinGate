#ifndef COIN_ACCEPTOR_H
#define COIN_ACCEPTOR_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define COIN_ACCEPTOR_GPIO_PIN         4
#define COIN_DEBOUNCE_TIME_MS         50
#define COIN_PULSE_MIN_WIDTH_MS       30
#define COIN_PULSE_MAX_WIDTH_MS       200

typedef enum {
    COIN_EVENT_INSERTED,
    COIN_EVENT_ERROR,
    COIN_EVENT_SUSPICIOUS
} coin_event_type_t;

typedef struct {
    coin_event_type_t type;
    uint32_t pulse_count;
    uint32_t timestamp;
    uint32_t pulse_width_ms;
} coin_event_t;

typedef void (*coin_callback_t)(const coin_event_t *event);

esp_err_t coin_acceptor_init(gpio_num_t gpio_pin);
esp_err_t coin_acceptor_deinit(void);
esp_err_t coin_acceptor_start(void);
esp_err_t coin_acceptor_stop(void);
uint32_t coin_acceptor_get_total_pulses(void);
uint32_t coin_acceptor_get_session_pulses(void);
void coin_acceptor_reset_session(void);
void coin_acceptor_set_callback(coin_callback_t callback);
bool coin_acceptor_is_enabled(void);
esp_err_t coin_acceptor_set_enabled(bool enabled);

#ifdef __cplusplus
}
#endif

#endif
