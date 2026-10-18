#include "telemetry.h"
#include "wifi_manager.h"
#include "network_monitor.h"
#include "incident_engine.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_chip_info.h"
#include "esp_netif.h"

static const char *TAG = "telemetry";

const char* telemetry_get_reset_reason_str(void)
{
    esp_reset_reason_t reason = esp_reset_reason();
    switch (reason) {
        case ESP_RST_POWERON:   return "Power-on";
        case ESP_RST_EXT:       return "External Pin";
        case ESP_RST_SW:        return "Software Reset";
        case ESP_RST_PANIC:     return "Software Panic";
        case ESP_RST_INT_WDT:   return "Interrupt Watchdog";
        case ESP_RST_TASK_WDT:  return "Task Watchdog";
        case ESP_RST_WDT:       return "Watchdog";
        case ESP_RST_DEEPSLEEP: return "Deep Sleep Exit";
        case ESP_RST_BROWNOUT:  return "Brownout Reset";
        case ESP_RST_SDIO:      return "SDIO Reset";
        default:                return "Unknown";
    }
}

uint32_t telemetry_get_uptime_sec(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000ULL);
}

uint32_t telemetry_get_free_heap(void)
{
    return esp_get_free_heap_size();
}

uint32_t telemetry_get_min_free_heap(void)
{
    return esp_get_minimum_free_heap_size();
}

esp_err_t telemetry_init(void)
{
    ESP_LOGI(TAG, "Telemetry module initialized. Reset reason: %s, Free Heap: %lu bytes",
             telemetry_get_reset_reason_str(), (unsigned long)telemetry_get_free_heap());
    return ESP_OK;
}

cJSON* telemetry_get_health_json(void)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }

    node_state_t state = incident_engine_get_state();
    bool is_healthy = (state == NODE_STATE_CONNECTED);

    cJSON_AddStringToObject(root, "status", is_healthy ? "UP" : "DEGRADED");
    cJSON_AddStringToObject(root, "state", incident_engine_state_to_str(state));
    cJSON_AddBoolToObject(root, "healthy", is_healthy);
    cJSON_AddNumberToObject(root, "uptime_sec", telemetry_get_uptime_sec());
    cJSON_AddNumberToObject(root, "free_heap", telemetry_get_free_heap());

    if (wifi_manager_is_sta_connected()) {
        cJSON_AddNumberToObject(root, "rssi", wifi_manager_get_rssi());
    } else {
        cJSON_AddNullToObject(root, "rssi");
    }

    network_metrics_t net_metrics;
    network_monitor_get_metrics(&net_metrics);
    cJSON_AddNumberToObject(root, "ping_latency_ms", net_metrics.ping_latency_ms);
    cJSON_AddNumberToObject(root, "packet_loss_percent", net_metrics.packet_loss_percent);

    return root;
}

cJSON* telemetry_get_full_metrics_json(void)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }

    // System Telemetry
    cJSON *sys = cJSON_CreateObject();
    if (sys != NULL) {
        cJSON_AddNumberToObject(sys, "uptime_sec", telemetry_get_uptime_sec());
        cJSON_AddNumberToObject(sys, "free_heap_bytes", telemetry_get_free_heap());
        cJSON_AddNumberToObject(sys, "min_free_heap_bytes", telemetry_get_min_free_heap());
        cJSON_AddStringToObject(sys, "reset_reason", telemetry_get_reset_reason_str());
        cJSON_AddStringToObject(sys, "idf_version", esp_get_idf_version());

        esp_chip_info_t chip_info;
        esp_chip_info(&chip_info);
        cJSON_AddNumberToObject(sys, "cpu_cores", chip_info.cores);
        cJSON_AddNumberToObject(sys, "chip_revision", chip_info.revision);
        cJSON_AddItemToObject(root, "system", sys);
    }

    // WiFi Telemetry
    cJSON *wifi = cJSON_CreateObject();
    if (wifi != NULL) {
        char ap_ssid[32] = {0};
        char ap_ip[16] = {0};
        char sta_ip[16] = {0};
        char sta_ssid[RN_MAX_SSID_LEN + 1] = {0};

        wifi_manager_get_ap_ssid(ap_ssid, sizeof(ap_ssid));
        wifi_manager_get_ap_ip(ap_ip, sizeof(ap_ip));
        wifi_manager_get_sta_ip(sta_ip, sizeof(sta_ip));
        wifi_manager_get_credentials(sta_ssid, sizeof(sta_ssid), NULL, 0);

        cJSON_AddStringToObject(wifi, "ap_ssid", ap_ssid);
        cJSON_AddStringToObject(wifi, "ap_ip", ap_ip);
        cJSON_AddStringToObject(wifi, "sta_configured_ssid", sta_ssid);
        cJSON_AddBoolToObject(wifi, "sta_connected", wifi_manager_is_sta_connected());
        cJSON_AddStringToObject(wifi, "sta_ip", sta_ip);
        cJSON_AddNumberToObject(wifi, "sta_rssi", wifi_manager_get_rssi());
        cJSON_AddNumberToObject(wifi, "reconnect_attempts", wifi_manager_get_reconnect_attempts());
        cJSON_AddNumberToObject(wifi, "current_backoff_sec", wifi_manager_get_current_backoff_sec());

        cJSON_AddItemToObject(root, "wifi", wifi);
    }

    // Network Probe Metrics
    cJSON *net_metrics = network_monitor_metrics_to_json();
    if (net_metrics != NULL) {
        cJSON_AddItemToObject(root, "network_probes", net_metrics);
    }

    // Incident Engine & State Status
    cJSON *incident_status = incident_engine_get_status_json();
    if (incident_status != NULL) {
        cJSON_AddItemToObject(root, "incident_engine", incident_status);
    }

    return root;
}
