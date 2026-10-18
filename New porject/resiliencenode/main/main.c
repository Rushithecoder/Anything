#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "event_store.h"
#include "incident_engine.h"
#include "wifi_manager.h"
#include "network_monitor.h"
#include "telemetry.h"
#include "web_server.h"

static const char *TAG = "main";

static void initialize_sntp(void)
{
    ESP_LOGI(TAG, "Initializing SNTP time synchronization...");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    esp_sntp_init();
}

static void print_banner(void)
{
    ESP_LOGI(TAG, "==========================================================");
    ESP_LOGI(TAG, "      ResilienceNode Autonomous Network Black Box         ");
    ESP_LOGI(TAG, "              ESP-IDF v5.x Firmware v1.0.0               ");
    ESP_LOGI(TAG, "==========================================================");
}

void app_main(void)
{
    print_banner();

    // 1. Initialize NVS Flash
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition truncated or new version found; erasing and reinitializing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Initialize TCP/IP stack and default system event loop
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 3. Initialize SNTP for real-world timestamps
    initialize_sntp();

    // 4. Initialize bounded event store (loads persisted NVS events)
    ESP_ERROR_CHECK(event_store_init());

    // 5. Initialize network health state machine and incident engine
    ESP_ERROR_CHECK(incident_engine_init());

    // 6. Initialize telemetry collector
    ESP_ERROR_CHECK(telemetry_init());

    // Log boot event to store
    char boot_details[64];
    snprintf(boot_details, sizeof(boot_details), "Reset: %s, Heap: %lu B",
             telemetry_get_reset_reason_str(), (unsigned long)telemetry_get_free_heap());
    event_store_log(EVENT_SEVERITY_INFO, "SYSTEM", "ResilienceNode Booted", boot_details);

    // 7. Initialize WiFi manager in AP+STA concurrent mode
    ESP_ERROR_CHECK(wifi_manager_init());

    // 8. Initialize and start network probe engine
    ESP_ERROR_CHECK(network_monitor_init());
    ESP_ERROR_CHECK(network_monitor_start());

    // 9. Start embedded HTTP server
    ESP_ERROR_CHECK(web_server_start());

    char ap_ssid[32] = {0};
    char ap_ip[16] = {0};
    wifi_manager_get_ap_ssid(ap_ssid, sizeof(ap_ssid));
    wifi_manager_get_ap_ip(ap_ip, sizeof(ap_ip));

    ESP_LOGI(TAG, "==========================================================");
    ESP_LOGI(TAG, " ResilienceNode is operational!");
    ESP_LOGI(TAG, " Web Dashboard & REST API: http://%s/", ap_ip);
    ESP_LOGI(TAG, " Setup SoftAP: SSID='%s' Password='%s'", ap_ssid, RN_AP_PASSWORD);
    ESP_LOGI(TAG, "==========================================================");

    // 10. Heartbeat monitoring loop
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(30000));

        incident_stats_t stats;
        incident_engine_get_stats(&stats);

        network_metrics_t net_metrics;
        network_monitor_get_metrics(&net_metrics);

        char sta_ip[16] = {0};
        wifi_manager_get_sta_ip(sta_ip, sizeof(sta_ip));

        ESP_LOGI(TAG, "[HEARTBEAT] State: %s | Uptime: %lu s | Heap: %lu B | Ping: %lu ms | Loss: %.1f%% | STA IP: %s | Active Incidents: %s",
                 incident_engine_state_to_str(stats.current_state),
                 (unsigned long)telemetry_get_uptime_sec(),
                 (unsigned long)telemetry_get_free_heap(),
                 (unsigned long)net_metrics.ping_latency_ms,
                 net_metrics.packet_loss_percent,
                 sta_ip,
                 stats.is_incident_active ? "YES" : "NO");
    }
}
