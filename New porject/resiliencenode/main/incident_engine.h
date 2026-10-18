#ifndef RESILIENCE_NODE_INCIDENT_ENGINE_H
#define RESILIENCE_NODE_INCIDENT_ENGINE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief ResilienceNode Network Health State Machine States
 */
typedef enum {
    NODE_STATE_UNCONFIGURED = 0,            /**< No WiFi credentials configured */
    NODE_STATE_CONNECTING,                  /**< Attempting connection to target WiFi */
    NODE_STATE_CONNECTED,                   /**< Fully connected, healthy internet reachability */
    NODE_STATE_WIFI_DISCONNECTED,           /**< STA disconnected from AP / beacon loss */
    NODE_STATE_IP_UNAVAILABLE,              /**< Associated with AP but DHCP failed / no IP */
    NODE_STATE_DNS_DEGRADED,                /**< IP acquired, but DNS query fails or timeouts */
    NODE_STATE_EXTERNAL_REACHABILITY_FAILED,/**< IP & DNS ok, but external probe to 8.8.8.8 failed */
    NODE_STATE_RECOVERING                   /**< Health checks recovering; validating stability */
} node_state_t;

/**
 * @brief Incident statistics and state tracking
 */
typedef struct {
    node_state_t current_state;
    node_state_t previous_state;
    int64_t state_entered_at_ms;
    int64_t state_duration_ms;

    bool is_incident_active;
    int64_t active_incident_start_ms;
    char active_incident_reason[128];

    uint32_t total_incidents;
    uint32_t resolved_incidents;
    int64_t total_downtime_ms;
    int64_t last_incident_duration_ms;
    int64_t mttr_ms;                        /**< Mean Time To Recovery in milliseconds */
} incident_stats_t;

/**
 * @brief Initialize the incident engine and state machine.
 * 
 * @return ESP_OK on success.
 */
esp_err_t incident_engine_init(void);

/**
 * @brief Get the current network health state.
 * 
 * @return Current node_state_t enum value.
 */
node_state_t incident_engine_get_state(void);

/**
 * @brief Transition to a new state and evaluate incident lifecycle.
 * 
 * @param new_state Target node_state_t state.
 * @param reason Descriptive string of the cause for the transition.
 * @return ESP_OK on success, or ESP_ERR_* on failure.
 */
esp_err_t incident_engine_set_state(node_state_t new_state, const char *reason);

/**
 * @brief Check if the current state is considered healthy.
 * 
 * @return true if CONNECTED, false otherwise.
 */
bool incident_engine_is_healthy(void);

/**
 * @brief Get human-readable string representation of a state.
 * 
 * @param state State enum.
 * @return Constant string pointer.
 */
const char* incident_engine_state_to_str(node_state_t state);

/**
 * @brief Get current incident engine statistics.
 * 
 * @param out_stats Pointer to destination struct.
 */
void incident_engine_get_stats(incident_stats_t *out_stats);

/**
 * @brief Build a cJSON representation of the current status and incidents.
 * 
 * @return Allocated cJSON object pointer. Caller must free using cJSON_Delete.
 */
cJSON* incident_engine_get_status_json(void);

#ifdef __cplusplus
}
#endif

#endif /* RESILIENCE_NODE_INCIDENT_ENGINE_H */
