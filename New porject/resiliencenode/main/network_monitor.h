#ifndef RESILIENCE_NODE_NETWORK_MONITOR_H
#define RESILIENCE_NODE_NETWORK_MONITOR_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RN_TARGET_EXTERNAL_IP        "8.8.8.8"
#define RN_TARGET_EXTERNAL_PORT      53
#define RN_TARGET_DNS_HOST           "dns.google"
#define RN_DEFAULT_PROBE_INTERVAL_MS 5000
#define RN_DEGRADED_INTERVAL_MS      3000
#define RN_RECOVERY_SUCCESS_REQUIRED 2
#define RN_PROBE_HISTORY_SIZE        20

/**
 * @brief Network monitor health metrics
 */
typedef struct {
    uint32_t total_probes;
    uint32_t successful_probes;
    uint32_t failed_probes;
    uint32_t consecutive_successes;
    uint32_t consecutive_failures;
    uint32_t dns_latency_ms;
    uint32_t ping_latency_ms;
    float packet_loss_percent;
    int64_t last_probe_timestamp_ms;
    char last_failure_reason[96];
    bool last_probe_passed;
} network_metrics_t;

/**
 * @brief Initialize the network monitor subsystem.
 * 
 * @return ESP_OK on success.
 */
esp_err_t network_monitor_init(void);

/**
 * @brief Start the background FreeRTOS network probe task.
 * 
 * @return ESP_OK on success.
 */
esp_err_t network_monitor_start(void);

/**
 * @brief Stop the background network probe task.
 * 
 * @return ESP_OK on success.
 */
esp_err_t network_monitor_stop(void);

/**
 * @brief Get a thread-safe snapshot of network metrics.
 * 
 * @param out_metrics Pointer to destination struct.
 */
void network_monitor_get_metrics(network_metrics_t *out_metrics);

/**
 * @brief Serialize network metrics to a cJSON object.
 * 
 * @return Allocated cJSON object. Caller must free using cJSON_Delete.
 */
cJSON* network_monitor_metrics_to_json(void);

/**
 * @brief Trigger an immediate probe cycle outside of regular timer.
 * 
 * @return ESP_OK on success.
 */
esp_err_t network_monitor_run_probe_now(void);

#ifdef __cplusplus
}
#endif

#endif /* RESILIENCE_NODE_NETWORK_MONITOR_H */
