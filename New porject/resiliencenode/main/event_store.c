#include "event_store.h"
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "event_store";
static const char *NVS_NAMESPACE = "rn_events";
static const char *NVS_KEY_BLOB = "records";
static const char *NVS_KEY_COUNT = "count";

static event_record_t s_events[EVENT_STORE_MAX_RAM_EVENTS];
static size_t s_head = 0;
static size_t s_count = 0;
static uint32_t s_next_id = 1;
static SemaphoreHandle_t s_event_mutex = NULL;
static bool s_initialized = false;

static int64_t get_current_timestamp_ms(void)
{
    time_t now = 0;
    time(&now);
    // If year > 2020 (1577836800), system clock has been set via SNTP
    if (now > 1577836800) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        return ((int64_t)tv.tv_sec * 1000LL) + (tv.tv_usec / 1000LL);
    }
    // Otherwise fallback to uptime in milliseconds
    return (int64_t)(esp_timer_get_time() / 1000LL);
}

const char* event_severity_to_str(event_severity_t severity)
{
    switch (severity) {
        case EVENT_SEVERITY_INFO:     return "INFO";
        case EVENT_SEVERITY_WARN:     return "WARN";
        case EVENT_SEVERITY_ERROR:    return "ERROR";
        case EVENT_SEVERITY_CRITICAL: return "CRITICAL";
        default:                      return "UNKNOWN";
    }
}

esp_err_t event_store_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    s_event_mutex = xSemaphoreCreateMutex();
    if (s_event_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create event store mutex");
        return ESP_ERR_NO_MEM;
    }

    memset(s_events, 0, sizeof(s_events));
    s_head = 0;
    s_count = 0;
    s_next_id = 1;
    s_initialized = true;

    // Load persisted events from NVS
    esp_err_t err = event_store_load_persisted();
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Notice: could not load persisted events (%s)", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "Event store initialized (RAM capacity: %d, loaded: %d)",
             EVENT_STORE_MAX_RAM_EVENTS, (int)s_count);

    return ESP_OK;
}

esp_err_t event_store_log(event_severity_t severity, const char *source, const char *message, const char *details)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (source == NULL || message == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_event_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take mutex in event_store_log");
        return ESP_ERR_TIMEOUT;
    }

    event_record_t *record = &s_events[s_head];
    record->id = s_next_id++;
    record->timestamp_ms = get_current_timestamp_ms();
    record->severity = severity;

    strncpy(record->source, source, sizeof(record->source) - 1);
    record->source[sizeof(record->source) - 1] = '\0';

    strncpy(record->message, message, sizeof(record->message) - 1);
    record->message[sizeof(record->message) - 1] = '\0';

    if (details != NULL && strlen(details) > 0) {
        strncpy(record->details, details, sizeof(record->details) - 1);
        record->details[sizeof(record->details) - 1] = '\0';
    } else {
        record->details[0] = '\0';
    }

    s_head = (s_head + 1) % EVENT_STORE_MAX_RAM_EVENTS;
    if (s_count < EVENT_STORE_MAX_RAM_EVENTS) {
        s_count++;
    }

    xSemaphoreGive(s_event_mutex);

    // Automatically persist critical/error events to NVS
    if (severity >= EVENT_SEVERITY_ERROR) {
        event_store_persist();
    }

    // Mirror to standard ESP log
    switch (severity) {
        case EVENT_SEVERITY_INFO:
            ESP_LOGI(TAG, "[%s] %s %s", source, message, record->details);
            break;
        case EVENT_SEVERITY_WARN:
            ESP_LOGW(TAG, "[%s] %s %s", source, message, record->details);
            break;
        case EVENT_SEVERITY_ERROR:
        case EVENT_SEVERITY_CRITICAL:
            ESP_LOGE(TAG, "[%s] %s %s", source, message, record->details);
            break;
    }

    return ESP_OK;
}

esp_err_t event_store_get_all(event_record_t *out_events, size_t max_count, size_t *actual_count)
{
    if (!s_initialized || out_events == NULL || actual_count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_event_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    size_t copy_count = (s_count < max_count) ? s_count : max_count;
    *actual_count = copy_count;

    for (size_t i = 0; i < copy_count; i++) {
        // Return newest first
        size_t idx = (s_head + EVENT_STORE_MAX_RAM_EVENTS - 1 - i) % EVENT_STORE_MAX_RAM_EVENTS;
        out_events[i] = s_events[idx];
    }

    xSemaphoreGive(s_event_mutex);
    return ESP_OK;
}

cJSON* event_store_to_cjson(size_t limit)
{
    if (!s_initialized) {
        return NULL;
    }

    if (xSemaphoreTake(s_event_mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        return NULL;
    }

    cJSON *array = cJSON_CreateArray();
    if (array == NULL) {
        xSemaphoreGive(s_event_mutex);
        return NULL;
    }

    size_t num_to_export = s_count;
    if (limit > 0 && limit < num_to_export) {
        num_to_export = limit;
    }

    for (size_t i = 0; i < num_to_export; i++) {
        // Newest first
        size_t idx = (s_head + EVENT_STORE_MAX_RAM_EVENTS - 1 - i) % EVENT_STORE_MAX_RAM_EVENTS;
        const event_record_t *rec = &s_events[idx];

        cJSON *item = cJSON_CreateObject();
        if (item == NULL) {
            continue;
        }

        cJSON_AddNumberToObject(item, "id", rec->id);
        cJSON_AddNumberToObject(item, "timestamp", (double)rec->timestamp_ms);
        cJSON_AddStringToObject(item, "severity", event_severity_to_str(rec->severity));
        cJSON_AddStringToObject(item, "source", rec->source);
        cJSON_AddStringToObject(item, "message", rec->message);
        cJSON_AddStringToObject(item, "details", rec->details);

        cJSON_AddItemToArray(array, item);
    }

    xSemaphoreGive(s_event_mutex);
    return array;
}

esp_err_t event_store_persist(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_event_mutex, pdMS_TO_TICKS(150)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    size_t nvs_count = (s_count < EVENT_STORE_MAX_NVS_EVENTS) ? s_count : EVENT_STORE_MAX_NVS_EVENTS;
    event_record_t persist_buffer[EVENT_STORE_MAX_NVS_EVENTS];

    for (size_t i = 0; i < nvs_count; i++) {
        // Persist newest events
        size_t idx = (s_head + EVENT_STORE_MAX_RAM_EVENTS - 1 - i) % EVENT_STORE_MAX_RAM_EVENTS;
        persist_buffer[i] = s_events[idx];
    }

    xSemaphoreGive(s_event_mutex);

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for event persistence: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_u32(handle, NVS_KEY_COUNT, (uint32_t)nvs_count);
    if (err == ESP_OK && nvs_count > 0) {
        err = nvs_set_blob(handle, NVS_KEY_BLOB, persist_buffer, nvs_count * sizeof(event_record_t));
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);

    if (err == ESP_OK) {
        ESP_LOGD(TAG, "Successfully persisted %d events to NVS", (int)nvs_count);
    } else {
        ESP_LOGE(TAG, "Failed to write events to NVS: %s", esp_err_to_name(err));
    }

    return err;
}

esp_err_t event_store_load_persisted(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    uint32_t stored_count = 0;
    err = nvs_get_u32(handle, NVS_KEY_COUNT, &stored_count);
    if (err != ESP_OK || stored_count == 0) {
        nvs_close(handle);
        return err;
    }

    if (stored_count > EVENT_STORE_MAX_NVS_EVENTS) {
        stored_count = EVENT_STORE_MAX_NVS_EVENTS;
    }

    event_record_t persist_buffer[EVENT_STORE_MAX_NVS_EVENTS];
    size_t required_size = stored_count * sizeof(event_record_t);
    err = nvs_get_blob(handle, NVS_KEY_BLOB, persist_buffer, &required_size);
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read event blob from NVS: %s", esp_err_to_name(err));
        return err;
    }

    if (xSemaphoreTake(s_event_mutex, pdMS_TO_TICKS(150)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    // Persisted events were saved in newest-to-oldest order.
    // Insert them in oldest-to-newest order so that s_head moves forward properly.
    for (int i = (int)stored_count - 1; i >= 0; i--) {
        s_events[s_head] = persist_buffer[i];
        if (persist_buffer[i].id >= s_next_id) {
            s_next_id = persist_buffer[i].id + 1;
        }
        s_head = (s_head + 1) % EVENT_STORE_MAX_RAM_EVENTS;
        if (s_count < EVENT_STORE_MAX_RAM_EVENTS) {
            s_count++;
        }
    }

    xSemaphoreGive(s_event_mutex);
    ESP_LOGI(TAG, "Restored %d events from NVS", (int)stored_count);
    return ESP_OK;
}

esp_err_t event_store_clear(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_event_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    memset(s_events, 0, sizeof(s_events));
    s_head = 0;
    s_count = 0;
    s_next_id = 1;

    xSemaphoreGive(s_event_mutex);

    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_erase_all(handle);
        nvs_commit(handle);
        nvs_close(handle);
    }

    ESP_LOGI(TAG, "Event store buffer and NVS cleared");
    return ESP_OK;
}

size_t event_store_get_count(void)
{
    size_t count = 0;
    if (s_initialized && s_event_mutex != NULL) {
        if (xSemaphoreTake(s_event_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            count = s_count;
            xSemaphoreGive(s_event_mutex);
        }
    }
    return count;
}
