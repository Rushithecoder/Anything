#include "incident_engine.h"
#include "event_store.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "incident_engine";

static incident_stats_t s_stats;
static SemaphoreHandle_t s_mutex = NULL;
static bool s_initialized = false;

static inline int64_t get_time_ms(void)
{
    return (int64_t)(esp_timer_get_time() / 1000LL);
}

const char* incident_engine_state_to_str(node_state_t state)
{
    switch (state) {
        case NODE_STATE_UNCONFIGURED:                return "UNCONFIGURED";
        case NODE_STATE_CONNECTING:                  return "CONNECTING";
        case NODE_STATE_CONNECTED:                   return "CONNECTED";
        case NODE_STATE_WIFI_DISCONNECTED:           return "WIFI_DISCONNECTED";
        case NODE_STATE_IP_UNAVAILABLE:              return "IP_UNAVAILABLE";
        case NODE_STATE_DNS_DEGRADED:                return "DNS_DEGRADED";
        case NODE_STATE_EXTERNAL_REACHABILITY_FAILED:return "EXTERNAL_REACHABILITY_FAILED";
        case NODE_STATE_RECOVERING:                  return "RECOVERING";
        default:                                     return "UNKNOWN";
    }
}

esp_err_t incident_engine_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    memset(&s_stats, 0, sizeof(s_stats));
    s_stats.current_state = NODE_STATE_UNCONFIGURED;
    s_stats.previous_state = NODE_STATE_UNCONFIGURED;
    s_stats.state_entered_at_ms = get_time_ms();
    s_stats.is_incident_active = false;

    s_initialized = true;
    ESP_LOGI(TAG, "Incident engine initialized (Initial State: UNCONFIGURED)");
    return ESP_OK;
}

node_state_t incident_engine_get_state(void)
{
    if (!s_initialized || s_mutex == NULL) {
        return NODE_STATE_UNCONFIGURED;
    }

    node_state_t current = NODE_STATE_UNCONFIGURED;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        current = s_stats.current_state;
        xSemaphoreGive(s_mutex);
    }
    return current;
}

bool incident_engine_is_healthy(void)
{
    return incident_engine_get_state() == NODE_STATE_CONNECTED;
}

esp_err_t incident_engine_set_state(node_state_t new_state, const char *reason)
{
    if (!s_initialized || s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(150)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (s_stats.current_state == new_state) {
        xSemaphoreGive(s_mutex);
        return ESP_OK;
    }

    int64_t now = get_time_ms();
    node_state_t old_state = s_stats.current_state;
    const char *reason_str = (reason != NULL) ? reason : "State change";

    ESP_LOGW(TAG, "STATE TRANSITION: [%s] -> [%s] (Reason: %s)",
             incident_engine_state_to_str(old_state),
             incident_engine_state_to_str(new_state),
             reason_str);

    // Evaluate incident lifecycle:
    // If transitioning from healthy to an unhealthy failure state:
    bool is_failure_state = (new_state == NODE_STATE_WIFI_DISCONNECTED ||
                             new_state == NODE_STATE_IP_UNAVAILABLE ||
                             new_state == NODE_STATE_DNS_DEGRADED ||
                             new_state == NODE_STATE_EXTERNAL_REACHABILITY_FAILED);

    if (is_failure_state) {
        if (!s_stats.is_incident_active) {
            s_stats.is_incident_active = true;
            s_stats.active_incident_start_ms = now;
            s_stats.total_incidents++;
            strncpy(s_stats.active_incident_reason, reason_str, sizeof(s_stats.active_incident_reason) - 1);
            s_stats.active_incident_reason[sizeof(s_stats.active_incident_reason) - 1] = '\0';

            char details[64];
            snprintf(details, sizeof(details), "Incident #%lu started", (unsigned long)s_stats.total_incidents);
            event_store_log(EVENT_SEVERITY_ERROR, "STATE_MACHINE", incident_engine_state_to_str(new_state), details);
        } else {
            // Already in incident, escalate or update reason
            strncpy(s_stats.active_incident_reason, reason_str, sizeof(s_stats.active_incident_reason) - 1);
            s_stats.active_incident_reason[sizeof(s_stats.active_incident_reason) - 1] = '\0';
            event_store_log(EVENT_SEVERITY_WARN, "STATE_MACHINE", incident_engine_state_to_str(new_state), reason_str);
        }
    } else if (new_state == NODE_STATE_CONNECTED) {
        if (s_stats.is_incident_active) {
            int64_t duration = now - s_stats.active_incident_start_ms;
            s_stats.is_incident_active = false;
            s_stats.last_incident_duration_ms = duration;
            s_stats.total_downtime_ms += duration;
            s_stats.resolved_incidents++;
            if (s_stats.resolved_incidents > 0) {
                s_stats.mttr_ms = s_stats.total_downtime_ms / s_stats.resolved_incidents;
            }

            char details[64];
            snprintf(details, sizeof(details), "Recovered in %lld ms (MTTR: %lld ms)", duration, s_stats.mttr_ms);
            event_store_log(EVENT_SEVERITY_INFO, "STATE_MACHINE", "CONNECTED - Incident Resolved", details);
        } else {
            event_store_log(EVENT_SEVERITY_INFO, "STATE_MACHINE", "CONNECTED", "All probes healthy");
        }
    } else if (new_state == NODE_STATE_RECOVERING) {
        event_store_log(EVENT_SEVERITY_INFO, "STATE_MACHINE", "RECOVERING", "Probes passing; confirming stability");
    } else if (new_state == NODE_STATE_CONNECTING) {
        event_store_log(EVENT_SEVERITY_INFO, "STATE_MACHINE", "CONNECTING", reason_str);
    } else if (new_state == NODE_STATE_UNCONFIGURED) {
        event_store_log(EVENT_SEVERITY_WARN, "STATE_MACHINE", "UNCONFIGURED", "Awaiting WiFi credentials via AP");
    }

    s_stats.previous_state = old_state;
    s_stats.current_state = new_state;
    s_stats.state_entered_at_ms = now;

    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

void incident_engine_get_stats(incident_stats_t *out_stats)
{
    if (out_stats == NULL) {
        return;
    }

    if (!s_initialized || s_mutex == NULL) {
        memset(out_stats, 0, sizeof(incident_stats_t));
        return;
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        *out_stats = s_stats;
        int64_t now = get_time_ms();
        out_stats->state_duration_ms = now - s_stats.state_entered_at_ms;
        xSemaphoreGive(s_mutex);
    }
}

cJSON* incident_engine_get_status_json(void)
{
    incident_stats_t stats;
    incident_engine_get_stats(&stats);

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }

    cJSON_AddStringToObject(root, "state", incident_engine_state_to_str(stats.current_state));
    cJSON_AddNumberToObject(root, "state_id", (int)stats.current_state);
    cJSON_AddStringToObject(root, "previous_state", incident_engine_state_to_str(stats.previous_state));
    cJSON_AddNumberToObject(root, "state_duration_ms", (double)stats.state_duration_ms);

    cJSON_AddBoolToObject(root, "incident_active", stats.is_incident_active);
    if (stats.is_incident_active) {
        int64_t cur_duration = get_time_ms() - stats.active_incident_start_ms;
        cJSON_AddNumberToObject(root, "active_incident_duration_ms", (double)cur_duration);
        cJSON_AddStringToObject(root, "active_incident_reason", stats.active_incident_reason);
    } else {
        cJSON_AddNumberToObject(root, "active_incident_duration_ms", 0);
        cJSON_AddStringToObject(root, "active_incident_reason", "None");
    }

    cJSON_AddNumberToObject(root, "total_incidents", stats.total_incidents);
    cJSON_AddNumberToObject(root, "resolved_incidents", stats.resolved_incidents);
    cJSON_AddNumberToObject(root, "total_downtime_ms", (double)stats.total_downtime_ms);
    cJSON_AddNumberToObject(root, "last_incident_duration_ms", (double)stats.last_incident_duration_ms);
    cJSON_AddNumberToObject(root, "mttr_ms", (double)stats.mttr_ms);

    return root;
}
