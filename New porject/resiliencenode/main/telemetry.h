#ifndef RESILIENCE_NODE_TELEMETRY_H
#define RESILIENCE_NODE_TELEMETRY_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the telemetry collector module.
 * 
 * @return ESP_OK on success.
 */
esp_err_t telemetry_init(void);

/**
 * @brief Get device uptime in seconds since boot.
 * 
 * @return Uptime in seconds.
 */
uint32_t telemetry_get_uptime_sec(void);

/**
 * @brief Get current free heap in bytes.
 * 
 * @return Free heap size.
 */
uint32_t telemetry_get_free_heap(void);

/**
 * @brief Get minimum free heap recorded since boot.
 * 
 * @return Minimum free heap size in bytes.
 */
uint32_t telemetry_get_min_free_heap(void);

/**
 * @brief Get string description of chip reset reason.
 * 
 * @return Human readable reset reason.
 */
const char* telemetry_get_reset_reason_str(void);

/**
 * @brief Generate comprehensive metrics cJSON object for GET /api/metrics.
 * 
 * @return Allocated cJSON object. Caller must free using cJSON_Delete.
 */
cJSON* telemetry_get_full_metrics_json(void);

/**
 * @brief Generate lightweight health check cJSON object for GET /api/health.
 * 
 * @return Allocated cJSON object. Caller must free using cJSON_Delete.
 */
cJSON* telemetry_get_health_json(void);

#ifdef __cplusplus
}
#endif

#endif /* RESILIENCE_NODE_TELEMETRY_H */
