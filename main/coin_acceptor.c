#include "coin_acceptor.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "anti_abuse.h"

static const char *TAG = "coin_acceptor";

static gpio_num_t s_gpio_pin = COIN_ACCEPTOR_GPIO_PIN;
static volatile uint32_t s_total_pulses = 0;
static volatile uint32_t s_session_pulses = 0;
static volatile uint32_t s_last_pulse_time = 0;
static volatile bool s_enabled = false;
static volatile bool s_initialized = false;
static QueueHandle_t s_event_queue = NULL;
static coin_callback_t s_callback = NULL;

static DRAM_ATTR volatile uint32_t s_last_interrupt_time = 0;
static DRAM_ATTR volatile uint32_t s_pulse_width = 0;
static DRAM_ATTR volatile bool s_coin_in_progress = false;

static void IRAM_ATTR coin_isr_handler(void *arg)
{
    uint32_t current_time = esp_timer_get_time() / 1000;
    uint32_t time_since_last = current_time - s_last_interrupt_time;
    
    s_last_interrupt_time = current_time;
    
    if (time_since_last < COIN_DEBOUNCE_TIME_MS) {
        return;
    }
    
    if (time_since_last < COIN_PULSE_MIN_WIDTH_MS || time_since_last > COIN_PULSE_MAX_WIDTH_MS * 10) {
        return;
    }
    
    s_pulse_width = time_since_last;
    s_total_pulses++;
    s_session_pulses++;
    
    BaseType_t higher_priority_task_woken = pdFALSE;
    
    if (s_event_queue != NULL) {
        coin_event_t event = {
            .type = COIN_EVENT_INSERTED,
            .pulse_count = 1,
            .timestamp = current_time,
            .pulse_width_ms = s_pulse_width
        };
        xQueueSendFromISR(s_event_queue, &event, &higher_priority_task_woken);
    }
    
    if (higher_priority_task_woken) {
        portYIELD_FROM_ISR();
    }
}

static void coin_task(void *pvParameters)
{
    coin_event_t event;
    uint32_t consecutive_rapid_pulses = 0;
    uint32_t last_check_time = 0;
    uint32_t current_time = 0;
    
    while (s_enabled) {
        if (xQueueReceive(s_event_queue, &event, pdMS_TO_TICKS(100)) == pdTRUE) {
            ESP_LOGD(TAG, "Coin pulse detected, session: %lu, width: %lu ms", 
                     s_session_pulses, event.pulse_width_ms);
            
            if (event.pulse_width_ms < COIN_PULSE_MIN_WIDTH_MS || 
                event.pulse_width_ms > COIN_PULSE_MAX_WIDTH_MS) {
                ESP_LOGW(TAG, "Suspicious pulse width: %lu ms", event.pulse_width_ms);
                event.type = COIN_EVENT_SUSPICIOUS;
            }
            
            current_time = esp_timer_get_time() / 1000;
            if (anti_abuse_check_pulse(current_time)) {
                consecutive_rapid_pulses++;
                if (consecutive_rapid_pulses > 10) {
                    ESP_LOGW(TAG, "Suspicious activity detected");
                    event.type = COIN_EVENT_SUSPICIOUS;
                }
            } else {
                consecutive_rapid_pulses = 0;
            }
            
            if (s_callback != NULL) {
                s_callback(&event);
            }
        }
        
        current_time = esp_timer_get_time() / 1000;
        if (current_time - last_check_time > 60000) {
            anti_abuse_reset_minute_count();
            last_check_time = current_time;
            consecutive_rapid_pulses = 0;
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    vTaskDelete(NULL);
}

esp_err_t coin_acceptor_init(gpio_num_t gpio_pin)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Coin acceptor already initialized");
        return ESP_OK;
    }
    
    s_gpio_pin = gpio_pin;
    
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << gpio_pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE
    };
    
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO config failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "GPIO ISR service install failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = gpio_isr_handler_add(gpio_pin, coin_isr_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO ISR handler add failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    s_event_queue = xQueueCreate(10, sizeof(coin_event_t));
    if (s_event_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create event queue");
        return ESP_ERR_NO_MEM;
    }
    
    s_initialized = true;
    s_total_pulses = 0;
    s_session_pulses = 0;
    
    ESP_LOGI(TAG, "Coin acceptor initialized on GPIO %d", gpio_pin);
    return ESP_OK;
}

esp_err_t coin_acceptor_deinit(void)
{
    if (!s_initialized) {
        return ESP_OK;
    }
    
    coin_acceptor_stop();
    
    gpio_isr_handler_remove(s_gpio_pin);
    gpio_reset_pin(s_gpio_pin);
    
    if (s_event_queue != NULL) {
        vQueueDelete(s_event_queue);
        s_event_queue = NULL;
    }
    
    s_initialized = false;
    ESP_LOGI(TAG, "Coin acceptor deinitialized");
    return ESP_OK;
}

esp_err_t coin_acceptor_start(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Coin acceptor not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_enabled) {
        ESP_LOGW(TAG, "Coin acceptor already running");
        return ESP_OK;
    }
    
    s_enabled = true;
    s_session_pulses = 0;
    
    BaseType_t result = xTaskCreatePinnedToCore(
        coin_task,
        "coin_task",
        2048,
        NULL,
        5,
        NULL,
        1
    );
    
    if (result != pdPASS) {
        s_enabled = false;
        ESP_LOGE(TAG, "Failed to create coin task");
        return ESP_ERR_NO_MEM;
    }
    
    ESP_LOGI(TAG, "Coin acceptor started");
    return ESP_OK;
}

esp_err_t coin_acceptor_stop(void)
{
    if (!s_enabled) {
        return ESP_OK;
    }
    
    s_enabled = false;
    
    if (s_event_queue != NULL) {
        coin_event_t dummy;
        while (xQueueReceive(s_event_queue, &dummy, 0) == pdTRUE) {
        }
    }
    
    ESP_LOGI(TAG, "Coin acceptor stopped");
    return ESP_OK;
}

uint32_t coin_acceptor_get_total_pulses(void)
{
    return s_total_pulses;
}

uint32_t coin_acceptor_get_session_pulses(void)
{
    return s_session_pulses;
}

void coin_acceptor_reset_session(void)
{
    s_session_pulses = 0;
    anti_abuse_reset_minute_count();
    ESP_LOGI(TAG, "Session pulses reset");
}

void coin_acceptor_set_callback(coin_callback_t callback)
{
    s_callback = callback;
}

bool coin_acceptor_is_enabled(void)
{
    return s_enabled;
}

esp_err_t coin_acceptor_set_enabled(bool enabled)
{
    if (enabled && !s_enabled) {
        return coin_acceptor_start();
    } else if (!enabled && s_enabled) {
        return coin_acceptor_stop();
    }
    return ESP_OK;
}
