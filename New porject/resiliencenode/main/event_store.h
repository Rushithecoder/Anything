#ifndef RESILIENCE_NODE_EVENT_STORE_H
#define RESILIENCE_NODE_EVENT_STORE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EVENT_STORE_MAX_RAM_EVENTS     100
#define EVENT_STORE_MAX_NVS_EVENTS     20
#define EVENT_STORE_SOURCE_LEN         20
#define EVENT_STORE_MESSAGE_LEN        96
#define EVENT_STORE_DETAILS_LEN        64

/**
 * @brief Event severity levels
 */
typedef enum {
    EVENT_SEVERITY_INFO = 0,
    EVENT_SEVERITY_WARN,
    EVENT_SEVERITY_ERROR,
    EVENT_SEVERITY_CRITICAL
} event_severity_t;

/**
 * @brief Event record structure
 */
typedef struct {
    uint32_t id;
    int64_t timestamp_ms;
    event_severity_t severity;
    char source[EVENT_STORE_SOURCE_LEN];
    char message[EVENT_STORE_MESSAGE_LEN];
    char details[EVENT_STORE_DETAILS_LEN];
} event_record_t;

/**
 * @brief Initialize the event store system and reload any persisted events from NVS.
 * 
 * @return ESP_OK on success, or appropriate error code.
 */
esp_err_t event_store_init(void);

/**
 * @brief Log a new event to the bounded RAM buffer.
 *        If buffer is full (100 events), oldest event is replaced.
 * 
 * @param severity Event severity level.
 * @param source Originating subsystem/component name.
 * @param message Human readable event summary.
 * @param details Optional additional details/metrics (can be NULL or empty).
 * @return ESP_OK on success, or appropriate error code.
 */
esp_err_t event_store_log(event_severity_t severity, const char *source, const char *message, const char *details);

/**
 * @brief Retrieve all events currently stored in RAM (up to max_count).
 * 
 * @param out_events Pointer to destination array.
 * @param max_count Maximum number of events to copy.
 * @param actual_count Pointer to variable where number of copied events is stored.
 * @return ESP_OK on success.
 */
esp_err_t event_store_get_all(event_record_t *out_events, size_t max_count, size_t *actual_count);

/**
 * @brief Serialize up to 'limit' events to a cJSON array (newest first).
 * 
 * @param limit Maximum number of events to include (0 for all).
 * @return Pointer to allocated cJSON array, or NULL on error. Caller must free using cJSON_Delete.
 */
cJSON* event_store_to_cjson(size_t limit);

/**
 * @brief Persist the last 20 events to NVS flash.
 * 
 * @return ESP_OK on success.
 */
esp_err_t event_store_persist(void);

/**
 * @brief Load persisted events from NVS flash into the RAM buffer.
 * 
 * @return ESP_OK on success.
 */
esp_err_t event_store_load_persisted(void);

/**
 * @brief Clear all events in RAM and in NVS.
 * 
 * @return ESP_OK on success.
 */
esp_err_t event_store_clear(void);

/**
 * @brief Get the current number of events in the RAM buffer.
 * 
 * @return Number of events (0 to 100).
 */
size_t event_store_get_count(void);

/**
 * @brief Convert severity enum to string representation.
 * 
 * @param severity Severity enum.
 * @return String representation ("INFO", "WARN", "ERROR", "CRITICAL").
 */
const char* event_severity_to_str(event_severity_t severity);

#ifdef __cplusplus
}
#endif

#endif /* RESILIENCE_NODE_EVENT_STORE_H */
