#include "network_monitor.h"
#include "wifi_manager.h"
#include "incident_engine.h"
#include "event_store.h"

#include <string.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "network_monitor";

static network_metrics_t s_metrics;
static SemaphoreHandle_t s_monitor_mutex = NULL;
static TaskHandle_t s_task_handle = NULL;
static bool s_running = false;

// Rolling window for packet loss tracking
static bool s_recent_history[RN_PROBE_HISTORY_SIZE];
static size_t s_history_idx = 0;
static size_t s_history_count = 0;
static uint32_t s_recovery_streak = 0;

static inline int64_t get_time_ms(void)
{
    return (int64_t)(esp_timer_get_time() / 1000LL);
}

static void update_rolling_history(bool success)
{
    s_recent_history[s_history_idx] = success;
    s_history_idx = (s_history_idx + 1) % RN_PROBE_HISTORY_SIZE;
    if (s_history_count < RN_PROBE_HISTORY_SIZE) {
        s_history_count++;
    }

    size_t fails = 0;
    for (size_t i = 0; i < s_history_count; i++) {
        if (!s_recent_history[i]) {
            fails++;
        }
    }
    s_metrics.packet_loss_percent = ((float)fails / (float)s_history_count) * 100.0f;
}

/**
 * @brief Probe DNS resolution by querying RN_TARGET_DNS_HOST
 */
static bool probe_dns(uint32_t *out_latency_ms)
{
    struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res = NULL;

    int64_t start_us = esp_timer_get_time();
    int err = getaddrinfo(RN_TARGET_DNS_HOST, NULL, &hints, &res);
    int64_t end_us = esp_timer_get_time();

    if (err == 0 && res != NULL) {
        *out_latency_ms = (uint32_t)((end_us - start_us) / 1000LL);
        freeaddrinfo(res);
        return true;
    }

    *out_latency_ms = 0;
    return false;
}

/**
 * @brief Probe external reachability via TCP socket connection to 8.8.8.8:53
 */
static bool probe_external_reachability(uint32_t *out_latency_ms)
{
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Failed to create socket for external probe");
        return false;
    }

    // Set 2.5 second connect/recv timeout
    struct timeval tv = {
        .tv_sec = 2,
        .tv_usec = 500000
    };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = inet_addr(RN_TARGET_EXTERNAL_IP);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(RN_TARGET_EXTERNAL_PORT);

    int64_t start_us = esp_timer_get_time();
    int res = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    int64_t end_us = esp_timer_get_time();

    close(sock);

    if (res == 0) {
        *out_latency_ms = (uint32_t)((end_us - start_us) / 1000LL);
        return true;
    }

    *out_latency_ms = 0;
    return false;
}

/**
 * @brief Run a full health evaluation cycle
 */
static void run_health_cycle(void)
{
    node_state_t cur_state = incident_engine_get_state();

    // If unconfigured, do not run probes
    if (cur_state == NODE_STATE_UNCONFIGURED) {
        return;
    }

    // 1. Check WiFi STA association
    if (!wifi_manager_is_sta_connected()) {
        if (xSemaphoreTake(s_monitor_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            s_metrics.total_probes++;
            s_metrics.failed_probes++;
            s_metrics.consecutive_failures++;
            s_metrics.consecutive_successes = 0;
            s_metrics.last_probe_passed = false;
            s_metrics.last_probe_timestamp_ms = get_time_ms();
            strncpy(s_metrics.last_failure_reason, "WiFi link down", sizeof(s_metrics.last_failure_reason) - 1);
            update_rolling_history(false);
            s_recovery_streak = 0;
            xSemaphoreGive(s_monitor_mutex);
        }
        return;
    }

    // 2. Check IP Assignment
    esp_netif_ip_info_t ip_info;
    esp_netif_t *sta_netif = wifi_manager_get_sta_netif();
    if (sta_netif == NULL || esp_netif_get_ip_info(sta_netif, &ip_info) != ESP_OK || ip_info.ip.addr == 0) {
        if (xSemaphoreTake(s_monitor_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            s_metrics.total_probes++;
            s_metrics.failed_probes++;
            s_metrics.consecutive_failures++;
            s_metrics.consecutive_successes = 0;
            s_metrics.last_probe_passed = false;
            s_metrics.last_probe_timestamp_ms = get_time_ms();
            strncpy(s_metrics.last_failure_reason, "No valid IP address", sizeof(s_metrics.last_failure_reason) - 1);
            update_rolling_history(false);
            s_recovery_streak = 0;
            xSemaphoreGive(s_monitor_mutex);
        }
        incident_engine_set_state(NODE_STATE_IP_UNAVAILABLE, "DHCP lease missing");
        return;
    }

    // 3. Check DNS Resolution
    uint32_t dns_latency = 0;
    bool dns_ok = probe_dns(&dns_latency);
    if (!dns_ok) {
        if (xSemaphoreTake(s_monitor_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            s_metrics.total_probes++;
            s_metrics.failed_probes++;
            s_metrics.consecutive_failures++;
            s_metrics.consecutive_successes = 0;
            s_metrics.dns_latency_ms = 0;
            s_metrics.last_probe_passed = false;
            s_metrics.last_probe_timestamp_ms = get_time_ms();
            strncpy(s_metrics.last_failure_reason, "DNS lookup timeout/failure", sizeof(s_metrics.last_failure_reason) - 1);
            update_rolling_history(false);
            s_recovery_streak = 0;
            xSemaphoreGive(s_monitor_mutex);
        }
        incident_engine_set_state(NODE_STATE_DNS_DEGRADED, "Failed resolving target host");
        return;
    }

    // 4. Check External Reachability (to 8.8.8.8)
    uint32_t ping_latency = 0;
    bool external_ok = probe_external_reachability(&ping_latency);
    if (!external_ok) {
        if (xSemaphoreTake(s_monitor_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            s_metrics.total_probes++;
            s_metrics.failed_probes++;
            s_metrics.consecutive_failures++;
            s_metrics.consecutive_successes = 0;
            s_metrics.dns_latency_ms = dns_latency;
            s_metrics.ping_latency_ms = 0;
            s_metrics.last_probe_passed = false;
            s_metrics.last_probe_timestamp_ms = get_time_ms();
            strncpy(s_metrics.last_failure_reason, "External IP 8.8.8.8 unreachable", sizeof(s_metrics.last_failure_reason) - 1);
            update_rolling_history(false);
            s_recovery_streak = 0;
            xSemaphoreGive(s_monitor_mutex);
        }
        incident_engine_set_state(NODE_STATE_EXTERNAL_REACHABILITY_FAILED, "8.8.8.8 connection failed");
        return;
    }

    // All probes PASSED!
    if (xSemaphoreTake(s_monitor_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_metrics.total_probes++;
        s_metrics.successful_probes++;
        s_metrics.consecutive_successes++;
        s_metrics.consecutive_failures = 0;
        s_metrics.dns_latency_ms = dns_latency;
        s_metrics.ping_latency_ms = ping_latency;
        s_metrics.last_probe_passed = true;
        s_metrics.last_probe_timestamp_ms = get_time_ms();
        strncpy(s_metrics.last_failure_reason, "None", sizeof(s_metrics.last_failure_reason) - 1);
        update_rolling_history(true);
        xSemaphoreGive(s_monitor_mutex);
    }

    // Evaluate state recovery logic
    if (cur_state == NODE_STATE_CONNECTED) {
        // Steady state healthy - do nothing
    } else if (cur_state == NODE_STATE_RECOVERING) {
        s_recovery_streak++;
        if (s_recovery_streak >= RN_RECOVERY_SUCCESS_REQUIRED) {
            incident_engine_set_state(NODE_STATE_CONNECTED, "Network recovery confirmed");
            s_recovery_streak = 0;
        }
    } else if (cur_state == NODE_STATE_CONNECTING) {
        incident_engine_set_state(NODE_STATE_CONNECTED, "Initial connection verified");
    } else {
        // Transition from failure state into RECOVERING
        s_recovery_streak = 1;
        incident_engine_set_state(NODE_STATE_RECOVERING, "Probes passing; evaluating stability");
    }
}

static void network_monitor_task(void *pvParameters)
{
    (void)pvParameters;
    ESP_LOGI(TAG, "Network monitor task running (Probe target: %s:%d, Host: %s)",
             RN_TARGET_EXTERNAL_IP, RN_TARGET_EXTERNAL_PORT, RN_TARGET_DNS_HOST);

    // Initial warm-up delay for WiFi association
    vTaskDelay(pdMS_TO_TICKS(3000));

    while (s_running) {
        run_health_cycle();

        node_state_t cur_state = incident_engine_get_state();
        TickType_t delay_ticks;

        if (cur_state == NODE_STATE_CONNECTED) {
            delay_ticks = pdMS_TO_TICKS(RN_DEFAULT_PROBE_INTERVAL_MS);
        } else {
            // Faster probe interval during degraded or recovering states
            delay_ticks = pdMS_TO_TICKS(RN_DEGRADED_INTERVAL_MS);
        }

        // Wait for next scheduled cycle or immediate notification
        ulTaskNotifyTake(pdTRUE, delay_ticks);
    }

    ESP_LOGI(TAG, "Network monitor task stopped");
    vTaskDelete(NULL);
}

esp_err_t network_monitor_init(void)
{
    s_monitor_mutex = xSemaphoreCreateMutex();
    if (s_monitor_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create monitor mutex");
        return ESP_ERR_NO_MEM;
    }

    memset(&s_metrics, 0, sizeof(s_metrics));
    memset(s_recent_history, 0, sizeof(s_recent_history));
    s_history_idx = 0;
    s_history_count = 0;
    s_recovery_streak = 0;
    strncpy(s_metrics.last_failure_reason, "None", sizeof(s_metrics.last_failure_reason) - 1);

    ESP_LOGI(TAG, "Network monitor initialized");
    return ESP_OK;
}

esp_err_t network_monitor_start(void)
{
    if (s_running) {
        return ESP_OK;
    }

    s_running = true;
    BaseType_t res = xTaskCreate(network_monitor_task,
                                 "rn_net_mon",
                                 4096,
                                 NULL,
                                 4,
                                 &s_task_handle);

    if (res != pdPASS) {
        s_running = false;
        ESP_LOGE(TAG, "Failed to create network monitor task");
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t network_monitor_stop(void)
{
    if (!s_running) {
        return ESP_OK;
    }

    s_running = false;
    if (s_task_handle != NULL) {
        xTaskNotifyGive(s_task_handle);
    }
    return ESP_OK;
}

void network_monitor_get_metrics(network_metrics_t *out_metrics)
{
    if (out_metrics == NULL) {
        return;
    }

    if (s_monitor_mutex != NULL && xSemaphoreTake(s_monitor_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        *out_metrics = s_metrics;
        xSemaphoreGive(s_monitor_mutex);
    } else {
        memset(out_metrics, 0, sizeof(network_metrics_t));
    }
}

cJSON* network_monitor_metrics_to_json(void)
{
    network_metrics_t metrics;
    network_monitor_get_metrics(&metrics);

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }

    cJSON_AddNumberToObject(root, "total_probes", metrics.total_probes);
    cJSON_AddNumberToObject(root, "successful_probes", metrics.successful_probes);
    cJSON_AddNumberToObject(root, "failed_probes", metrics.failed_probes);
    cJSON_AddNumberToObject(root, "consecutive_successes", metrics.consecutive_successes);
    cJSON_AddNumberToObject(root, "consecutive_failures", metrics.consecutive_failures);
    cJSON_AddNumberToObject(root, "dns_latency_ms", metrics.dns_latency_ms);
    cJSON_AddNumberToObject(root, "ping_latency_ms", metrics.ping_latency_ms);
    cJSON_AddNumberToObject(root, "packet_loss_percent", metrics.packet_loss_percent);
    cJSON_AddNumberToObject(root, "last_probe_timestamp_ms", (double)metrics.last_probe_timestamp_ms);
    cJSON_AddStringToObject(root, "last_failure_reason", metrics.last_failure_reason);
    cJSON_AddBoolToObject(root, "last_probe_passed", metrics.last_probe_passed);

    return root;
}

esp_err_t network_monitor_run_probe_now(void)
{
    if (s_task_handle != NULL) {
        xTaskNotifyGive(s_task_handle);
        return ESP_OK;
    }
    return ESP_ERR_INVALID_STATE;
}
