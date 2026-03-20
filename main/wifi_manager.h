#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t wifi_manager_start(void);
void wifi_manager_reconnect(void);
bool wifi_manager_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif
