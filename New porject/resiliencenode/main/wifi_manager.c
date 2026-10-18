#include "wifi_manager.h"
#include "incident_engine.h"
#include "event_store.h"

#include <string.h>
#include <stdio.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "lwip/inet.h"

static const char *TAG = "wifi_manager";
static const char *NVS_NAMESPACE = "rn_wifi";
static const char *NVS_KEY_SSID = "sta_ssid";
static const char *NVS_KEY_PASS = "sta_pass";

static esp_netif_t *s_netif_sta = NULL;
static esp_netif_t *s_netif_ap = NULL;

static char s_ap_ssid[32] = {0};
static char s_sta_ssid[RN_MAX_SSID_LEN + 1] = {0};
static char s_sta_pass[RN_MAX_PASSWORD_LEN + 1] = {0};
static char s_sta_ip_str[RN_MAX_IP_STR_LEN] = "0.0.0.0";
static char s_ap_ip_str[RN_MAX_IP_STR_LEN] = "192.168.4.1";

static bool s_configured = false;
static bool s_sta_associated = false;
static bool s_sta_has_ip = false;
static uint32_t s_reconnect_attempts = 0;
static uint32_t s_current_backoff_sec = RN_BACKOFF_MIN_SEC;

static SemaphoreHandle_t s_wifi_mutex = NULL;
static TimerHandle_t s_reconnect_timer = NULL;
static bool s_initialized = false;

// Forward declarations
static void reconnect_timer_cb(TimerHandle_t xTimer);
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
static void ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

static void compute_ap_ssid(void)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_ap_ssid, sizeof(s_ap_ssid), "ResilienceNode-%02X%02X", mac[4], mac[5]);
}

static uint32_t calculate_backoff(uint32_t attempt)
{
    if (attempt == 0) return 1;
    // 1s, 2s, 4s, 8s, 16s, capped at 30s
    uint32_t delay = 1U << (attempt - 1);
    if (delay > RN_BACKOFF_MAX_SEC) {
        delay = RN_BACKOFF_MAX_SEC;
    }
    return delay;
}

static void schedule_reconnect(void)
{
    s_reconnect_attempts++;
    s_current_backoff_sec = calculate_backoff(s_reconnect_attempts);

    char reason[64];
    snprintf(reason, sizeof(reason), "Retry #%lu in %lu s", (unsigned long)s_reconnect_attempts, (unsigned long)s_current_backoff_sec);
    ESP_LOGW(TAG, "Reconnection scheduled: %s", reason);

    if (s_reconnect_timer != NULL) {
        xTimerStop(s_reconnect_timer, 0);
        xTimerChangePeriod(s_reconnect_timer, pdMS_TO_TICKS(s_current_backoff_sec * 1000), 0);
        xTimerStart(s_reconnect_timer, 0);
    }
}

static void reconnect_timer_cb(TimerHandle_t xTimer)
{
    (void)xTimer;
    if (!s_configured) {
        return;
    }

    ESP_LOGI(TAG, "Reconnection timer fired (attempt %lu). Connecting to '%s'...",
             (unsigned long)s_reconnect_attempts, s_sta_ssid);

    incident_engine_set_state(NODE_STATE_CONNECTING, "Backoff timer triggered reconnection");
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
        ESP_LOGE(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(err));
        schedule_reconnect();
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_base;

    switch (event_id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "WIFI_EVENT_STA_START");
            if (s_configured) {
                incident_engine_set_state(NODE_STATE_CONNECTING, "WiFi station started");
                esp_wifi_connect();
            } else {
                incident_engine_set_state(NODE_STATE_UNCONFIGURED, "No stored WiFi credentials");
            }
            break;

        case WIFI_EVENT_STA_CONNECTED: {
            wifi_event_sta_connected_t *event = (wifi_event_sta_connected_t *)event_data;
            s_sta_associated = true;
            char details[64];
            snprintf(details, sizeof(details), "SSID: %s, Ch: %d", (char *)event->ssid, event->channel);
            ESP_LOGI(TAG, "WiFi STA Connected to AP (%s)", details);
            event_store_log(EVENT_SEVERITY_INFO, "WIFI", "Associated with AP", details);
            // Still waiting for IP; DHCP is progressing
            break;
        }

        case WIFI_EVENT_STA_DISCONNECTED: {
            wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
            s_sta_associated = false;
            s_sta_has_ip = false;
            strncpy(s_sta_ip_str, "0.0.0.0", sizeof(s_sta_ip_str));

            char details[64];
            snprintf(details, sizeof(details), "Reason code: %d", event->reason);
            ESP_LOGW(TAG, "WiFi STA Disconnected (%s)", details);
            event_store_log(EVENT_SEVERITY_WARN, "WIFI", "Disconnected from AP", details);

            incident_engine_set_state(NODE_STATE_WIFI_DISCONNECTED, details);

            if (s_configured) {
                schedule_reconnect();
            }
            break;
        }

        case WIFI_EVENT_AP_START:
            ESP_LOGI(TAG, "SoftAP started: SSID='%s' IP='%s'", s_ap_ssid, s_ap_ip_str);
            event_store_log(EVENT_SEVERITY_INFO, "WIFI", "SoftAP started", s_ap_ssid);
            break;

        case WIFI_EVENT_AP_STACONNECTED: {
            wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
            char details[48];
            snprintf(details, sizeof(details), "MAC: %02x:%02x:%02x:%02x:%02x:%02x, AID: %d",
                     event->mac[0], event->mac[1], event->mac[2],
                     event->mac[3], event->mac[4], event->mac[5], event->aid);
            ESP_LOGI(TAG, "Client joined SoftAP: %s", details);
            event_store_log(EVENT_SEVERITY_INFO, "WIFI", "SoftAP client connected", details);
            break;
        }

        case WIFI_EVENT_AP_STADISCONNECTED: {
            wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
            char details[48];
            snprintf(details, sizeof(details), "MAC: %02x:%02x:%02x:%02x:%02x:%02x, AID: %d",
                     event->mac[0], event->mac[1], event->mac[2],
                     event->mac[3], event->mac[4], event->mac[5], event->aid);
            ESP_LOGI(TAG, "Client left SoftAP: %s", details);
            event_store_log(EVENT_SEVERITY_INFO, "WIFI", "SoftAP client disconnected", details);
            break;
        }

        default:
            break;
    }
}

static void ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_base;

    if (event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        snprintf(s_sta_ip_str, sizeof(s_sta_ip_str), IPSTR, IP2STR(&event->ip_info.ip));

        s_sta_has_ip = true;
        s_reconnect_attempts = 0;
        s_current_backoff_sec = RN_BACKOFF_MIN_SEC;

        if (s_reconnect_timer != NULL) {
            xTimerStop(s_reconnect_timer, 0);
        }

        char details[64];
        snprintf(details, sizeof(details), "IP: %s, GW: " IPSTR, s_sta_ip_str, IP2STR(&event->ip_info.gw));
        ESP_LOGI(TAG, "STA Got IP: %s", details);
        event_store_log(EVENT_SEVERITY_INFO, "WIFI", "DHCP lease acquired", details);

        // Network monitor task will perform DNS and ping reachability checks
        // before transitioning state machine to NODE_STATE_CONNECTED.
    } else if (event_id == IP_EVENT_STA_LOST_IP) {
        s_sta_has_ip = false;
        strncpy(s_sta_ip_str, "0.0.0.0", sizeof(s_sta_ip_str));
        ESP_LOGW(TAG, "STA Lost IP");
        event_store_log(EVENT_SEVERITY_WARN, "WIFI", "IP lease lost", "");
        incident_engine_set_state(NODE_STATE_IP_UNAVAILABLE, "DHCP lease lost");
    }
}

esp_err_t wifi_manager_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    s_wifi_mutex = xSemaphoreCreateMutex();
    if (s_wifi_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create wifi manager mutex");
        return ESP_ERR_NO_MEM;
    }

    compute_ap_ssid();

    // Create reconnect software timer
    s_reconnect_timer = xTimerCreate("rn_reconnect",
                                     pdMS_TO_TICKS(1000),
                                     pdFALSE,
                                     NULL,
                                     reconnect_timer_cb);
    if (s_reconnect_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create reconnect timer");
        return ESP_ERR_NO_MEM;
    }

    // Initialize network interfaces
    s_netif_ap = esp_netif_create_default_wifi_ap();
    s_netif_sta = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(err));
        return err;
    }

    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_LOST_IP, &ip_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    // Configure SoftAP
    wifi_config_t ap_config = {0};
    strncpy((char *)ap_config.ap.ssid, s_ap_ssid, sizeof(ap_config.ap.ssid));
    ap_config.ap.ssid_len = strlen(s_ap_ssid);
    strncpy((char *)ap_config.ap.password, RN_AP_PASSWORD, sizeof(ap_config.ap.password));
    ap_config.ap.channel = RN_AP_CHANNEL;
    ap_config.ap.max_connection = RN_AP_MAX_CONNECTIONS;
    ap_config.ap.authmode = (strlen(RN_AP_PASSWORD) == 0) ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    ap_config.ap.beacon_interval = 100;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));

    // Check NVS for stored STA credentials
    nvs_handle_t nvs_handle;
    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err == ESP_OK) {
        size_t ssid_len = sizeof(s_sta_ssid);
        size_t pass_len = sizeof(s_sta_pass);

        esp_err_t err_s = nvs_get_str(nvs_handle, NVS_KEY_SSID, s_sta_ssid, &ssid_len);
        esp_err_t err_p = nvs_get_str(nvs_handle, NVS_KEY_PASS, s_sta_pass, &pass_len);

        if (err_s == ESP_OK && strlen(s_sta_ssid) > 0) {
            s_configured = true;
            if (err_p != ESP_OK) {
                s_sta_pass[0] = '\0';
            }

            wifi_config_t sta_config = {0};
            strncpy((char *)sta_config.sta.ssid, s_sta_ssid, sizeof(sta_config.sta.ssid));
            strncpy((char *)sta_config.sta.password, s_sta_pass, sizeof(sta_config.sta.password));
            sta_config.sta.scan_method = WIFI_FAST_SCAN;
            sta_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
            sta_config.sta.threshold.rssi = -127;
            sta_config.sta.threshold.authmode = WIFI_AUTH_OPEN;

            esp_wifi_set_config(WIFI_IF_STA, &sta_config);
            ESP_LOGI(TAG, "Loaded target WiFi from NVS: SSID='%s'", s_sta_ssid);
        } else {
            s_configured = false;
            ESP_LOGI(TAG, "No WiFi credentials found in NVS");
        }
        nvs_close(nvs_handle);
    } else {
        s_configured = false;
        ESP_LOGI(TAG, "NVS namespace '%s' not present yet", NVS_NAMESPACE);
    }

    ESP_ERROR_CHECK(esp_wifi_start());

    s_initialized = true;
    ESP_LOGI(TAG, "WiFi Manager started. AP SSID='%s', Pass='%s'", s_ap_ssid, RN_AP_PASSWORD);
    return ESP_OK;
}

esp_err_t wifi_manager_set_credentials(const char *ssid, const char *password)
{
    if (ssid == NULL || strlen(ssid) == 0 || strlen(ssid) > RN_MAX_SSID_LEN) {
        return ESP_ERR_INVALID_ARG;
    }
    if (password != NULL && strlen(password) > RN_MAX_PASSWORD_LEN) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_wifi_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    // Save to NVS
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        xSemaphoreGive(s_wifi_mutex);
        ESP_LOGE(TAG, "Failed to open NVS for saving credentials: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_str(handle, NVS_KEY_SSID, ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(handle, NVS_KEY_PASS, (password != NULL) ? password : "");
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK) {
        xSemaphoreGive(s_wifi_mutex);
        ESP_LOGE(TAG, "Failed to commit credentials to NVS: %s", esp_err_to_name(err));
        return err;
    }

    // Update RAM copy
    strncpy(s_sta_ssid, ssid, sizeof(s_sta_ssid) - 1);
    s_sta_ssid[sizeof(s_sta_ssid) - 1] = '\0';
    if (password != NULL) {
        strncpy(s_sta_pass, password, sizeof(s_sta_pass) - 1);
        s_sta_pass[sizeof(s_sta_pass) - 1] = '\0';
    } else {
        s_sta_pass[0] = '\0';
    }

    s_configured = true;
    s_reconnect_attempts = 0;
    s_current_backoff_sec = RN_BACKOFF_MIN_SEC;

    if (s_reconnect_timer != NULL) {
        xTimerStop(s_reconnect_timer, 0);
    }

    // Configure WiFi driver
    wifi_config_t sta_config = {0};
    strncpy((char *)sta_config.sta.ssid, s_sta_ssid, sizeof(sta_config.sta.ssid));
    strncpy((char *)sta_config.sta.password, s_sta_pass, sizeof(sta_config.sta.password));
    sta_config.sta.scan_method = WIFI_FAST_SCAN;
    sta_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;

    esp_wifi_disconnect();
    esp_wifi_set_config(WIFI_IF_STA, &sta_config);

    char details[64];
    snprintf(details, sizeof(details), "Target SSID: %s", s_sta_ssid);
    event_store_log(EVENT_SEVERITY_INFO, "CONFIG", "WiFi credentials updated", details);

    incident_engine_set_state(NODE_STATE_CONNECTING, "New WiFi credentials applied");
    err = esp_wifi_connect();

    xSemaphoreGive(s_wifi_mutex);
    return err;
}

esp_err_t wifi_manager_get_credentials(char *ssid_out, size_t ssid_len, char *pass_out, size_t pass_len)
{
    if (ssid_out == NULL || ssid_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_wifi_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (!s_configured) {
        xSemaphoreGive(s_wifi_mutex);
        return ESP_ERR_NVS_NOT_FOUND;
    }

    strncpy(ssid_out, s_sta_ssid, ssid_len - 1);
    ssid_out[ssid_len - 1] = '\0';

    if (pass_out != NULL && pass_len > 0) {
        strncpy(pass_out, s_sta_pass, pass_len - 1);
        pass_out[pass_len - 1] = '\0';
    }

    xSemaphoreGive(s_wifi_mutex);
    return ESP_OK;
}

esp_err_t wifi_manager_connect(void)
{
    if (!s_configured) {
        return ESP_ERR_INVALID_STATE;
    }
    incident_engine_set_state(NODE_STATE_CONNECTING, "Manual connect triggered");
    return esp_wifi_connect();
}

esp_err_t wifi_manager_disconnect(void)
{
    if (s_reconnect_timer != NULL) {
        xTimerStop(s_reconnect_timer, 0);
    }
    return esp_wifi_disconnect();
}

bool wifi_manager_is_sta_connected(void)
{
    return s_sta_associated && s_sta_has_ip;
}

int8_t wifi_manager_get_rssi(void)
{
    if (!s_sta_associated) {
        return 0;
    }

    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        return ap_info.rssi;
    }
    return 0;
}

esp_err_t wifi_manager_get_sta_ip(char *ip_buf, size_t max_len)
{
    if (ip_buf == NULL || max_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_wifi_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    strncpy(ip_buf, s_sta_ip_str, max_len - 1);
    ip_buf[max_len - 1] = '\0';

    xSemaphoreGive(s_wifi_mutex);
    return ESP_OK;
}

esp_err_t wifi_manager_get_ap_ip(char *ip_buf, size_t max_len)
{
    if (ip_buf == NULL || max_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    strncpy(ip_buf, s_ap_ip_str, max_len - 1);
    ip_buf[max_len - 1] = '\0';
    return ESP_OK;
}

esp_err_t wifi_manager_get_ap_ssid(char *ssid_buf, size_t max_len)
{
    if (ssid_buf == NULL || max_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    strncpy(ssid_buf, s_ap_ssid, max_len - 1);
    ssid_buf[max_len - 1] = '\0';
    return ESP_OK;
}

uint32_t wifi_manager_get_reconnect_attempts(void)
{
    return s_reconnect_attempts;
}

uint32_t wifi_manager_get_current_backoff_sec(void)
{
    return s_current_backoff_sec;
}

esp_netif_t* wifi_manager_get_sta_netif(void)
{
    return s_netif_sta;
}

esp_netif_t* wifi_manager_get_ap_netif(void)
{
    return s_netif_ap;
}
