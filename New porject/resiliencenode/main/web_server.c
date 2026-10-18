#include "web_server.h"
#include "dashboard_html.h"
#include "wifi_manager.h"
#include "incident_engine.h"
#include "network_monitor.h"
#include "event_store.h"
#include "telemetry.h"

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "web_server";
static httpd_handle_t s_server = NULL;

static void set_cors_headers(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
}

static esp_err_t send_json_response(httpd_req_t *req, cJSON *json_obj, int status_code)
{
    set_cors_headers(req);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");

    if (status_code == 400) {
        httpd_resp_set_status(req, "400 Bad Request");
    } else if (status_code == 500) {
        httpd_resp_set_status(req, "500 Internal Server Error");
    } else {
        httpd_resp_set_status(req, "200 OK");
    }

    if (json_obj == NULL) {
        const char *err_resp = "{\"status\":\"error\",\"message\":\"Internal serialization error\"}";
        return httpd_resp_send(req, err_resp, strlen(err_resp));
    }

    char *json_str = cJSON_PrintUnformatted(json_obj);
    cJSON_Delete(json_obj);

    if (json_str == NULL) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_send(req, "{\"status\":\"error\",\"message\":\"Out of memory\"}", -1);
    }

    esp_err_t ret = httpd_resp_send(req, json_str, strlen(json_str));
    free(json_str);
    return ret;
}

// OPTIONS preflight handler for CORS
static esp_err_t api_options_handler(httpd_req_t *req)
{
    set_cors_headers(req);
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

// GET / - Dashboard HTML
static esp_err_t dashboard_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, DASHBOARD_HTML, strlen(DASHBOARD_HTML));
}

// GET /api/status
static esp_err_t api_status_get_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return send_json_response(req, NULL, 500);
    }

    incident_stats_t stats;
    incident_engine_get_stats(&stats);
    node_state_t cur_state = stats.current_state;

    cJSON_AddStringToObject(root, "state", incident_engine_state_to_str(cur_state));
    cJSON_AddNumberToObject(root, "state_id", (int)cur_state);
    cJSON_AddBoolToObject(root, "healthy", incident_engine_is_healthy());
    cJSON_AddNumberToObject(root, "uptime_sec", telemetry_get_uptime_sec());
    cJSON_AddNumberToObject(root, "free_heap", telemetry_get_free_heap());

    // WiFi summary
    char sta_ip[16] = {0};
    char ap_ssid[32] = {0};
    char ap_ip[16] = {0};
    char sta_ssid[RN_MAX_SSID_LEN + 1] = {0};

    wifi_manager_get_sta_ip(sta_ip, sizeof(sta_ip));
    wifi_manager_get_ap_ssid(ap_ssid, sizeof(ap_ssid));
    wifi_manager_get_ap_ip(ap_ip, sizeof(ap_ip));
    wifi_manager_get_credentials(sta_ssid, sizeof(sta_ssid), NULL, 0);

    cJSON *wifi = cJSON_CreateObject();
    if (wifi != NULL) {
        cJSON_AddBoolToObject(wifi, "connected", wifi_manager_is_sta_connected());
        cJSON_AddStringToObject(wifi, "target_ssid", sta_ssid);
        cJSON_AddStringToObject(wifi, "sta_ip", sta_ip);
        cJSON_AddNumberToObject(wifi, "rssi", wifi_manager_get_rssi());
        cJSON_AddStringToObject(wifi, "ap_ssid", ap_ssid);
        cJSON_AddStringToObject(wifi, "ap_ip", ap_ip);
        cJSON_AddItemToObject(root, "wifi", wifi);
    }

    // Incident summary
    cJSON *incident = cJSON_CreateObject();
    if (incident != NULL) {
        cJSON_AddBoolToObject(incident, "active", stats.is_incident_active);
        cJSON_AddStringToObject(incident, "reason", stats.active_incident_reason);
        cJSON_AddNumberToObject(incident, "total_count", stats.total_incidents);
        cJSON_AddNumberToObject(incident, "resolved_count", stats.resolved_incidents);
        cJSON_AddNumberToObject(incident, "mttr_ms", (double)stats.mttr_ms);
        cJSON_AddItemToObject(root, "incident", incident);
    }

    return send_json_response(req, root, 200);
}

// GET /api/metrics
static esp_err_t api_metrics_get_handler(httpd_req_t *req)
{
    cJSON *metrics = telemetry_get_full_metrics_json();
    return send_json_response(req, metrics, 200);
}

// GET /api/events
static esp_err_t api_events_get_handler(httpd_req_t *req)
{
    size_t limit = 100;
    char query[32];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char limit_val[16];
        if (httpd_query_key_value(query, "limit", limit_val, sizeof(limit_val)) == ESP_OK) {
            int parsed_limit = atoi(limit_val);
            if (parsed_limit > 0 && parsed_limit <= 100) {
                limit = (size_t)parsed_limit;
            }
        }
    }

    cJSON *events_array = event_store_to_cjson(limit);
    if (events_array == NULL) {
        events_array = cJSON_CreateArray();
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        cJSON_Delete(events_array);
        return send_json_response(req, NULL, 500);
    }

    cJSON_AddNumberToObject(root, "total_events", event_store_get_count());
    cJSON_AddNumberToObject(root, "returned_count", cJSON_GetArraySize(events_array));
    cJSON_AddItemToObject(root, "events", events_array);

    return send_json_response(req, root, 200);
}

// GET /api/config
static esp_err_t api_config_get_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return send_json_response(req, NULL, 500);
    }

    char sta_ssid[RN_MAX_SSID_LEN + 1] = {0};
    char sta_pass[RN_MAX_PASSWORD_LEN + 1] = {0};
    char ap_ssid[32] = {0};
    char ap_ip[16] = {0};

    esp_err_t err = wifi_manager_get_credentials(sta_ssid, sizeof(sta_ssid), sta_pass, sizeof(sta_pass));
    wifi_manager_get_ap_ssid(ap_ssid, sizeof(ap_ssid));
    wifi_manager_get_ap_ip(ap_ip, sizeof(ap_ip));

    cJSON_AddStringToObject(root, "configured_ssid", (err == ESP_OK) ? sta_ssid : "");
    cJSON_AddBoolToObject(root, "is_configured", (err == ESP_OK && strlen(sta_ssid) > 0));
    cJSON_AddBoolToObject(root, "password_set", (strlen(sta_pass) > 0));
    cJSON_AddStringToObject(root, "ap_ssid", ap_ssid);
    cJSON_AddStringToObject(root, "ap_ip", ap_ip);
    cJSON_AddNumberToObject(root, "backoff_min_sec", RN_BACKOFF_MIN_SEC);
    cJSON_AddNumberToObject(root, "backoff_max_sec", RN_BACKOFF_MAX_SEC);
    cJSON_AddNumberToObject(root, "probe_interval_ms", RN_DEFAULT_PROBE_INTERVAL_MS);
    cJSON_AddStringToObject(root, "probe_target_ip", RN_TARGET_EXTERNAL_IP);
    cJSON_AddStringToObject(root, "probe_dns_host", RN_TARGET_DNS_HOST);

    return send_json_response(req, root, 200);
}

// POST /api/config
static esp_err_t api_config_post_handler(httpd_req_t *req)
{
    set_cors_headers(req);

    int total_len = req->content_len;
    if (total_len <= 0 || total_len > 1024) {
        cJSON *err_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(err_obj, "status", "error");
        cJSON_AddStringToObject(err_obj, "message", "Invalid payload length");
        return send_json_response(req, err_obj, 400);
    }

    char *buf = malloc(total_len + 1);
    if (buf == NULL) {
        cJSON *err_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(err_obj, "status", "error");
        cJSON_AddStringToObject(err_obj, "message", "Out of memory");
        return send_json_response(req, err_obj, 500);
    }

    int cur_len = 0;
    while (cur_len < total_len) {
        int received = httpd_req_recv(req, buf + cur_len, total_len - cur_len);
        if (received <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            free(buf);
            cJSON *err_obj = cJSON_CreateObject();
            cJSON_AddStringToObject(err_obj, "status", "error");
            cJSON_AddStringToObject(err_obj, "message", "Connection timeout or read error");
            return send_json_response(req, err_obj, 400);
        }
        cur_len += received;
    }
    buf[total_len] = '\0';

    cJSON *json = cJSON_Parse(buf);
    free(buf);

    if (json == NULL) {
        cJSON *err_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(err_obj, "status", "error");
        cJSON_AddStringToObject(err_obj, "message", "Malformed JSON payload");
        return send_json_response(req, err_obj, 400);
    }

    cJSON *ssid_item = cJSON_GetObjectItem(json, "ssid");
    cJSON *pass_item = cJSON_GetObjectItem(json, "password");

    if (ssid_item == NULL || !cJSON_IsString(ssid_item) || strlen(ssid_item->valuestring) == 0) {
        cJSON_Delete(json);
        cJSON *err_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(err_obj, "status", "error");
        cJSON_AddStringToObject(err_obj, "message", "Field 'ssid' is required and cannot be empty");
        return send_json_response(req, err_obj, 400);
    }

    const char *ssid = ssid_item->valuestring;
    const char *pass = (pass_item != NULL && cJSON_IsString(pass_item)) ? pass_item->valuestring : "";

    ESP_LOGI(TAG, "Configuring WiFi credentials: SSID='%s'", ssid);
    esp_err_t err = wifi_manager_set_credentials(ssid, pass);
    cJSON_Delete(json);

    if (err != ESP_OK) {
        cJSON *err_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(err_obj, "status", "error");
        cJSON_AddStringToObject(err_obj, "message", "Failed to apply WiFi credentials");
        return send_json_response(req, err_obj, 500);
    }

    cJSON *resp_obj = cJSON_CreateObject();
    cJSON_AddStringToObject(resp_obj, "status", "ok");
    cJSON_AddStringToObject(resp_obj, "message", "WiFi credentials saved. Connecting to network...");
    cJSON_AddStringToObject(resp_obj, "ssid", ssid);

    return send_json_response(req, resp_obj, 200);
}

// GET /api/health
static esp_err_t api_health_get_handler(httpd_req_t *req)
{
    cJSON *health = telemetry_get_health_json();
    return send_json_response(req, health, 200);
}

esp_err_t web_server_start(void)
{
    if (s_server != NULL) {
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16;
    config.stack_size = 8192;
    config.lru_purge_enable = true;

    ESP_LOGI(TAG, "Starting web server on port %d...", config.server_port);
    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(ret));
        return ret;
    }

    // 1. GET / - Dashboard
    httpd_uri_t uri_dashboard = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = dashboard_get_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(s_server, &uri_dashboard);

    // 2. GET /api/status
    httpd_uri_t uri_status = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = api_status_get_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(s_server, &uri_status);

    // 3. GET /api/metrics
    httpd_uri_t uri_metrics = {
        .uri = "/api/metrics",
        .method = HTTP_GET,
        .handler = api_metrics_get_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(s_server, &uri_metrics);

    // 4. GET /api/events
    httpd_uri_t uri_events = {
        .uri = "/api/events",
        .method = HTTP_GET,
        .handler = api_events_get_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(s_server, &uri_events);

    // 5. GET /api/config
    httpd_uri_t uri_config_get = {
        .uri = "/api/config",
        .method = HTTP_GET,
        .handler = api_config_get_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(s_server, &uri_config_get);

    // 6. POST /api/config
    httpd_uri_t uri_config_post = {
        .uri = "/api/config",
        .method = HTTP_POST,
        .handler = api_config_post_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(s_server, &uri_config_post);

    // 7. GET /api/health
    httpd_uri_t uri_health = {
        .uri = "/api/health",
        .method = HTTP_GET,
        .handler = api_health_get_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(s_server, &uri_health);

    // CORS preflight OPTIONS handlers for all /api endpoints
    const char *cors_endpoints[] = {
        "/api/status",
        "/api/metrics",
        "/api/events",
        "/api/config",
        "/api/health"
    };

    for (size_t i = 0; i < sizeof(cors_endpoints) / sizeof(cors_endpoints[0]); i++) {
        httpd_uri_t uri_options = {
            .uri = cors_endpoints[i],
            .method = HTTP_OPTIONS,
            .handler = api_options_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(s_server, &uri_options);
    }

    ESP_LOGI(TAG, "Web server started with 7 endpoints registered");
    return ESP_OK;
}

esp_err_t web_server_stop(void)
{
    if (s_server == NULL) {
        return ESP_OK;
    }

    esp_err_t ret = httpd_stop(s_server);
    s_server = NULL;
    ESP_LOGI(TAG, "Web server stopped");
    return ret;
}

bool web_server_is_running(void)
{
    return s_server != NULL;
}
